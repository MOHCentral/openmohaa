/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.
===========================================================================
*/

/*
 * Local webcam face overlay. Windows Media Foundation capture.
 * Not networked. A second local client can read frames via shared memory.
 */

#include "client.h"

#define WEBCAM_TEX_SIZE 128
#define WEBCAM_TEX_BYTES (WEBCAM_TEX_SIZE * WEBCAM_TEX_SIZE * 4)

static cvar_t *cl_webcam;
static cvar_t *cl_webcamDevice;
static cvar_t *cl_webcamFps;
static cvar_t *cl_webcamCrop;
static cvar_t *cl_webcamMirror;
static cvar_t *cl_webcamYBias;
static cvar_t *cl_webcamReady;

static qboolean s_webcamInited;
static qboolean s_webcamStarted;
static qboolean s_captureFailed;
static qboolean s_shareAnnounced;
static qboolean s_loggedOff;
static qboolean s_loggedNoRenderer;
static qboolean s_loggedFirstFrame;
static qboolean s_loggedUpload;
static int      s_lastGrabTime;
static int      s_grabFailCount;
static byte     s_tex[WEBCAM_TEX_BYTES];

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#define WEBCAM_MAX_W 1920
#define WEBCAM_MAX_H 1080
#define WEBCAM_SHARE_NAME  "Local\\OpenMoHAAWebcamFace"
#define WEBCAM_SHARE_MAGIC 0x5743414D

enum {
	WEBCAM_FMT_RGB32 = 0,
	WEBCAM_FMT_RGB24,
	WEBCAM_FMT_YUY2,
	WEBCAM_FMT_NV12
};

static IMFSourceReader *s_reader;
static int              s_srcWidth;
static int              s_srcHeight;
static int              s_srcStride;
static int              s_srcFormat;
static qboolean         s_srcBottomUp;
static qboolean         s_haveFrame;
static byte             s_lastFrame[WEBCAM_MAX_W * WEBCAM_MAX_H * 4];

typedef struct {
	volatile LONG seq;
	volatile LONG magic;
	byte          rgba[WEBCAM_TEX_BYTES];
} webcamShare_t;

static HANDLE         s_shareMap;
static webcamShare_t *s_share;
static CRITICAL_SECTION s_logCs;
static CRITICAL_SECTION s_texCs;
static qboolean         s_csInited;
static DWORD            s_mainTid;
static HANDLE           s_thread;
static HANDLE           s_stopEvent;
static HANDLE           s_camMutex;
static volatile LONG    s_hasTex;
static qboolean         s_streamDead;
static byte             s_texPub[WEBCAM_TEX_BYTES];

static void Webcam_Log(const char *fmt, ...)
{
	char        msg[768];
	char        line[1024];
	char        dir[MAX_OSPATH];
	char        path[MAX_OSPATH];
	const char *appdata;
	va_list     ap;
	FILE       *f;
	SYSTEMTIME  st;
	DWORD       pid;

	va_start(ap, fmt);
	Q_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	appdata = getenv("APPDATA");
	if (!appdata || !appdata[0]) {
		return;
	}

	Com_sprintf(dir, sizeof(dir), "%s\\openmohaa", appdata);
	CreateDirectoryA(dir, NULL);
	Com_sprintf(path, sizeof(path), "%s\\openmohaa\\webcam.log", appdata);

	if (s_csInited) {
		EnterCriticalSection(&s_logCs);
	}

	GetLocalTime(&st);
	pid = GetCurrentProcessId();
	Com_sprintf(
		line,
		sizeof(line),
		"%04d-%02d-%02d %02d:%02d:%02d.%03d pid=%lu tid=%lu %s\n",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond,
		st.wMilliseconds,
		(unsigned long)pid,
		(unsigned long)GetCurrentThreadId(),
		msg
	);

	f = fopen(path, "a");
	if (f) {
		fputs(line, f);
		fflush(f);
		fclose(f);
	}
	if (s_csInited) {
		LeaveCriticalSection(&s_logCs);
	}

	if (GetCurrentThreadId() == s_mainTid) {
		Com_Printf("webcam: %s\n", msg);
	}
}

static void Webcam_SetReady(qboolean ready)
{
	Cvar_Set("cl_webcamReady", ready ? "1" : "0");
}

static void Webcam_OpenShare(void)
{
	DWORD err;

	if (s_share) {
		return;
	}
	s_shareMap = CreateFileMappingA(
		INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)sizeof(webcamShare_t), WEBCAM_SHARE_NAME
	);
	err = GetLastError();
	if (!s_shareMap) {
		Webcam_Log("share CreateFileMapping failed err=%lu name=%s", (unsigned long)err, WEBCAM_SHARE_NAME);
		return;
	}
	s_share = (webcamShare_t *)MapViewOfFile(s_shareMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(webcamShare_t));
	if (!s_share) {
		Webcam_Log("share MapViewOfFile failed err=%lu", (unsigned long)GetLastError());
		CloseHandle(s_shareMap);
		s_shareMap = NULL;
		return;
	}
	Webcam_Log(
		"share mapping %s (%s) size=%u",
		WEBCAM_SHARE_NAME,
		err == ERROR_ALREADY_EXISTS ? "opened existing" : "created",
		(unsigned)sizeof(webcamShare_t)
	);
}

static void Webcam_CloseShare(void)
{
	if (s_share) {
		UnmapViewOfFile(s_share);
		s_share = NULL;
	}
	if (s_shareMap) {
		CloseHandle(s_shareMap);
		s_shareMap = NULL;
	}
}

static void Webcam_WriteShare(void)
{
	Webcam_OpenShare();
	if (!s_share) {
		return;
	}
	InterlockedIncrement(&s_share->seq);
	MemoryBarrier();
	memcpy(s_share->rgba, s_tex, WEBCAM_TEX_BYTES);
	s_share->magic = WEBCAM_SHARE_MAGIC;
	MemoryBarrier();
	InterlockedIncrement(&s_share->seq);
}

static qboolean Webcam_ReadShare(void)
{
	LONG a, b;
	int  spins;

	Webcam_OpenShare();
	if (!s_share || s_share->magic != WEBCAM_SHARE_MAGIC) {
		return qfalse;
	}
	for (spins = 0; spins < 8; spins++) {
		a = s_share->seq;
		if (a & 1) {
			continue;
		}
		MemoryBarrier();
		memcpy(s_tex, s_share->rgba, WEBCAM_TEX_BYTES);
		MemoryBarrier();
		b = s_share->seq;
		if (a == b && !(b & 1) && a != 0) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Webcam_ReleaseReader(void)
{
	if (s_reader) {
		s_reader->Release();
		s_reader = NULL;
	}
}

static void Webcam_StopCapture(void)
{
	Webcam_ReleaseReader();
	s_webcamStarted = qfalse;
	s_haveFrame     = qfalse;
	if (s_camMutex) {
		ReleaseMutex(s_camMutex);
		CloseHandle(s_camMutex);
		s_camMutex = NULL;
		Webcam_Log("released camera mutex");
	}
}

static const char *Webcam_SubtypeName(const GUID *g)
{
	if (IsEqualGUID(*g, MFVideoFormat_RGB32)) {
		return "RGB32";
	}
	if (IsEqualGUID(*g, MFVideoFormat_RGB24)) {
		return "RGB24";
	}
	if (IsEqualGUID(*g, MFVideoFormat_YUY2)) {
		return "YUY2";
	}
	if (IsEqualGUID(*g, MFVideoFormat_NV12)) {
		return "NV12";
	}
	if (IsEqualGUID(*g, MFVideoFormat_MJPG)) {
		return "MJPG";
	}
	return "other";
}

static int Webcam_FormatFromGuid(const GUID *g)
{
	if (IsEqualGUID(*g, MFVideoFormat_RGB32)) {
		return WEBCAM_FMT_RGB32;
	}
	if (IsEqualGUID(*g, MFVideoFormat_RGB24)) {
		return WEBCAM_FMT_RGB24;
	}
	if (IsEqualGUID(*g, MFVideoFormat_YUY2)) {
		return WEBCAM_FMT_YUY2;
	}
	if (IsEqualGUID(*g, MFVideoFormat_NV12)) {
		return WEBCAM_FMT_NV12;
	}
	return -1;
}

static byte Webcam_ClampByte(int v)
{
	if (v < 0) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return (byte)v;
}

static void Webcam_Yuy2ToBgra(const byte *src, int width, int height, int stride, qboolean bottomUp, byte *dst)
{
	int x, y;

	for (y = 0; y < height; y++) {
		int         srcY = bottomUp ? (height - 1 - y) : y;
		const byte *row  = src + srcY * stride;
		byte       *out  = dst + y * width * 4;

		for (x = 0; x + 1 < width; x += 2) {
			int y0 = row[0] - 16;
			int u  = row[1] - 128;
			int y1 = row[2] - 16;
			int v  = row[3] - 128;
			int r, g, b;

			r        = (298 * y0 + 409 * v + 128) >> 8;
			g        = (298 * y0 - 100 * u - 208 * v + 128) >> 8;
			b        = (298 * y0 + 516 * u + 128) >> 8;
			out[0]   = Webcam_ClampByte(b);
			out[1]   = Webcam_ClampByte(g);
			out[2]   = Webcam_ClampByte(r);
			out[3]   = 255;
			r        = (298 * y1 + 409 * v + 128) >> 8;
			g        = (298 * y1 - 100 * u - 208 * v + 128) >> 8;
			b        = (298 * y1 + 516 * u + 128) >> 8;
			out[4]   = Webcam_ClampByte(b);
			out[5]   = Webcam_ClampByte(g);
			out[6]   = Webcam_ClampByte(r);
			out[7]   = 255;
			row += 4;
			out += 8;
		}
	}
}

static void Webcam_Nv12ToBgra(const byte *src, int width, int height, int stride, byte *dst)
{
	int         x, y;
	const byte *uvBase = src + stride * height;

	for (y = 0; y < height; y++) {
		const byte *yRow = src + y * stride;
		const byte *uv   = uvBase + (y / 2) * stride;
		byte       *out  = dst + y * width * 4;

		for (x = 0; x < width; x++) {
			int yy = yRow[x] - 16;
			int u  = uv[(x & ~1)] - 128;
			int v  = uv[(x & ~1) + 1] - 128;
			int r  = (298 * yy + 409 * v + 128) >> 8;
			int g  = (298 * yy - 100 * u - 208 * v + 128) >> 8;
			int b  = (298 * yy + 516 * u + 128) >> 8;
			out[0] = Webcam_ClampByte(b);
			out[1] = Webcam_ClampByte(g);
			out[2] = Webcam_ClampByte(r);
			out[3] = 255;
			out += 4;
		}
	}
}

static void Webcam_RgbToBgra(const byte *src, int width, int height, int stride, int bpp, qboolean bottomUp, byte *dst)
{
	int x, y;

	for (y = 0; y < height; y++) {
		int         srcY = bottomUp ? (height - 1 - y) : y;
		const byte *row  = src + srcY * stride;
		byte       *out  = dst + y * width * 4;

		for (x = 0; x < width; x++) {
			out[0] = row[0];
			out[1] = row[1];
			out[2] = row[2];
			out[3] = 255;
			row += bpp;
			out += 4;
		}
	}
}

static void Webcam_LogNativeTypes(IMFSourceReader *reader)
{
	DWORD i;

	for (i = 0; i < 16; i++) {
		IMFMediaType *type = NULL;
		GUID          subtype;
		UINT32        w = 0, h = 0;
		HRESULT       hr;

		hr = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type);
		if (FAILED(hr) || !type) {
			break;
		}
		Com_Memset(&subtype, 0, sizeof(subtype));
		type->GetGUID(MF_MT_SUBTYPE, &subtype);
		MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
		Webcam_Log("native [%lu] %s %ux%u", (unsigned long)i, Webcam_SubtypeName(&subtype), w, h);
		type->Release();
	}
}

static qboolean Webcam_TrySetType(IMFSourceReader *reader, const GUID *subtype, UINT32 width, UINT32 height)
{
	IMFMediaType *type = NULL;
	HRESULT       hr;

	hr = MFCreateMediaType(&type);
	if (FAILED(hr)) {
		Webcam_Log("MFCreateMediaType failed hr=0x%08lx", (unsigned long)hr);
		return qfalse;
	}
	type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	type->SetGUID(MF_MT_SUBTYPE, *subtype);
	if (width && height) {
		MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
	}
	hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, type);
	type->Release();
	Webcam_Log(
		"SetCurrentMediaType %s%s hr=0x%08lx",
		Webcam_SubtypeName(subtype),
		(width && height) ? " (sized)" : "",
		(unsigned long)hr
	);
	return SUCCEEDED(hr) ? qtrue : qfalse;
}

static qboolean Webcam_TryNativeSubtype(IMFSourceReader *reader, const GUID *want, int preferW, int preferH)
{
	DWORD         i;
	IMFMediaType *best     = NULL;
	int           bestScore = 0x7fffffff;

	for (i = 0; i < 48; i++) {
		IMFMediaType *type = NULL;
		GUID          subtype;
		UINT32        w = 0, h = 0;
		int           score;
		HRESULT       hr;

		hr = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type);
		if (FAILED(hr) || !type) {
			break;
		}
		Com_Memset(&subtype, 0, sizeof(subtype));
		type->GetGUID(MF_MT_SUBTYPE, &subtype);
		MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
		if (!IsEqualGUID(subtype, *want) || w == 0 || h == 0 || w > WEBCAM_MAX_W || h > WEBCAM_MAX_H) {
			type->Release();
			continue;
		}
		score = (int)w * (int)h - preferW * preferH;
		if (score < 0) {
			score = -score + 100000;
		}
		if (score < bestScore) {
			if (best) {
				best->Release();
			}
			best      = type;
			bestScore = score;
		} else {
			type->Release();
		}
	}
	if (!best) {
		return qfalse;
	}
	{
		UINT32  w = 0, h = 0;
		HRESULT hr;

		MFGetAttributeSize(best, MF_MT_FRAME_SIZE, &w, &h);
		hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, best);
		Webcam_Log(
			"SetCurrentMediaType native %s %ux%u hr=0x%08lx",
			Webcam_SubtypeName(want),
			w,
			h,
			(unsigned long)hr
		);
		best->Release();
		return SUCCEEDED(hr) ? qtrue : qfalse;
	}
}

static qboolean Webcam_ReadCurrentFormat(IMFSourceReader *reader)
{
	IMFMediaType *type = NULL;
	GUID          subtype;
	UINT32        w = 0, h = 0;
	INT32         stride = 0;
	HRESULT       hr;
	int           fmt;

	hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
	if (FAILED(hr) || !type) {
		Webcam_Log("GetCurrentMediaType failed hr=0x%08lx", (unsigned long)hr);
		return qfalse;
	}
	memset(&subtype, 0, sizeof(subtype));
	type->GetGUID(MF_MT_SUBTYPE, &subtype);
	hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
	if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&stride))) {
		stride = 0;
	}
	type->Release();
	fmt = Webcam_FormatFromGuid(&subtype);
	if (FAILED(hr) || w == 0 || h == 0 || w > WEBCAM_MAX_W || h > WEBCAM_MAX_H || fmt < 0) {
		Webcam_Log(
			"unsupported output %s %ux%u stride=%d hr=0x%08lx",
			Webcam_SubtypeName(&subtype),
			w,
			h,
			stride,
			(unsigned long)hr
		);
		return qfalse;
	}

	s_srcWidth    = (int)w;
	s_srcHeight   = (int)h;
	s_srcFormat   = fmt;
	s_srcBottomUp = (stride < 0) ? qtrue : qfalse;
	if (stride < 0) {
		stride = -stride;
	}
	if (stride == 0) {
		if (fmt == WEBCAM_FMT_YUY2) {
			stride = s_srcWidth * 2;
		} else if (fmt == WEBCAM_FMT_NV12) {
			stride = s_srcWidth;
		} else if (fmt == WEBCAM_FMT_RGB24) {
			stride = s_srcWidth * 3;
		} else {
			stride = s_srcWidth * 4;
		}
	}
	s_srcStride = stride;
	Webcam_Log(
		"output %s %dx%d stride=%d bottomUp=%d",
		Webcam_SubtypeName(&subtype),
		s_srcWidth,
		s_srcHeight,
		s_srcStride,
		s_srcBottomUp
	);
	return qtrue;
}

static qboolean Webcam_ConfigureOutput(IMFSourceReader *reader)
{
	Webcam_LogNativeTypes(reader);

	if (Webcam_TryNativeSubtype(reader, &MFVideoFormat_YUY2, 640, 480) && Webcam_ReadCurrentFormat(reader)) {
		return qtrue;
	}
	if (Webcam_TryNativeSubtype(reader, &MFVideoFormat_NV12, 640, 480) && Webcam_ReadCurrentFormat(reader)) {
		return qtrue;
	}
	if (Webcam_TryNativeSubtype(reader, &MFVideoFormat_RGB32, 640, 480) && Webcam_ReadCurrentFormat(reader)) {
		return qtrue;
	}
	if (Webcam_TrySetType(reader, &MFVideoFormat_RGB32, 640, 480) && Webcam_ReadCurrentFormat(reader)) {
		return qtrue;
	}
	Webcam_Log("could not configure a usable camera format");
	return qfalse;
}

static HRESULT Webcam_CreateReader(IMFMediaSource *source, IMFSourceReader **out)
{
	HRESULT hr;

	*out = NULL;
	hr   = MFCreateSourceReaderFromMediaSource(source, NULL, out);
	Webcam_Log("CreateSourceReader raw hr=0x%08lx", (unsigned long)hr);
	return hr;
}

static qboolean Webcam_StartCapture(void)
{
	IMFAttributes  *attr    = NULL;
	IMFActivate   **devices = NULL;
	IMFMediaSource *source  = NULL;
	UINT32          count   = 0;
	UINT32          index;
	HRESULT         hr;
	WCHAR           name[256];
	UINT32          nameLen;

	if (s_webcamStarted) {
		return qtrue;
	}

	s_streamDead = qfalse;
	Webcam_OpenShare();
	if (s_share && s_share->magic == WEBCAM_SHARE_MAGIC && s_share->seq != 0) {
		Webcam_Log("other client already sharing frames, not opening camera");
		return qfalse;
	}

	s_camMutex = CreateMutexA(NULL, FALSE, "Local\\OpenMoHAAWebcamCapture");
	if (!s_camMutex) {
		Webcam_Log("CreateMutex failed err=%lu", (unsigned long)GetLastError());
		return qfalse;
	}
	{
		DWORD wait = WaitForSingleObject(s_camMutex, 0);
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
			Webcam_Log("camera already in use by another local client");
			CloseHandle(s_camMutex);
			s_camMutex = NULL;
			return qfalse;
		}
	}
	Webcam_Log("got camera mutex");

	hr = MFCreateAttributes(&attr, 1);
	if (FAILED(hr)) {
		Webcam_Log("MFCreateAttributes failed hr=0x%08lx", (unsigned long)hr);
		return qfalse;
	}
	attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	hr = MFEnumDeviceSources(attr, &devices, &count);
	attr->Release();
	attr = NULL;
	Webcam_Log("MFEnumDeviceSources hr=0x%08lx count=%u", (unsigned long)hr, count);
	if (FAILED(hr) || count == 0) {
		Webcam_Log("no Media Foundation cameras found");
		if (devices) {
			CoTaskMemFree(devices);
		}
		return qfalse;
	}

	Webcam_Log("%u camera(s)", count);
	for (index = 0; index < count; index++) {
		name[0] = 0;
		nameLen = 0;
		if (SUCCEEDED(devices[index]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 256, &nameLen))) {
			char utf[256];
			WideCharToMultiByte(CP_UTF8, 0, name, -1, utf, sizeof(utf), NULL, NULL);
			Webcam_Log("[%u] %s", index, utf);
		}
	}

	index = (UINT32)(cl_webcamDevice->integer < 0 ? 0 : cl_webcamDevice->integer);
	if (index >= count) {
		Webcam_Log("cl_webcamDevice %d out of range, using 0", cl_webcamDevice->integer);
		index = 0;
	}

	Webcam_Log("activating device %u", index);
	hr = devices[index]->ActivateObject(IID_PPV_ARGS(&source));
	Webcam_Log("ActivateObject hr=0x%08lx", (unsigned long)hr);
	for (UINT32 i = 0; i < count; i++) {
		devices[i]->Release();
	}
	CoTaskMemFree(devices);
	if (FAILED(hr) || !source) {
		Webcam_Log("ActivateObject failed hr=0x%08lx device=%u", (unsigned long)hr, index);
		return qfalse;
	}

	hr = Webcam_CreateReader(source, &s_reader);
	source->Release();
	if (FAILED(hr) || !s_reader) {
		Webcam_Log("CreateSourceReader failed hr=0x%08lx", (unsigned long)hr);
		return qfalse;
	}

	s_reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	s_reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

	if (!Webcam_ConfigureOutput(s_reader)) {
		Webcam_ReleaseReader();
		return qfalse;
	}

	s_webcamStarted = qtrue;
	s_lastGrabTime  = 0;
	s_haveFrame     = qfalse;
	s_loggedFirstFrame = qfalse;
	Webcam_Log("MF capture %dx%d fmt=%d on device %u", s_srcWidth, s_srcHeight, s_srcFormat, index);
	return qtrue;
}

static qboolean Webcam_GrabFrame(void)
{
	IMFSample      *sample = NULL;
	IMFMediaBuffer *buffer = NULL;
	DWORD           flags  = 0;
	BYTE           *data   = NULL;
	DWORD           maxLen = 0, curLen = 0;
	HRESULT         hr;

	if (!s_reader) {
		return qfalse;
	}

	hr = s_reader->ReadSample(
		(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, NULL, &sample
	);
	if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
		s_grabFailCount++;
		if (s_grabFailCount == 1 || (s_grabFailCount % 30) == 0) {
			Webcam_Log("ReadSample failed hr=0x%08lx flags=0x%lx fails=%d", (unsigned long)hr, (unsigned long)flags, s_grabFailCount);
		}
		if (FAILED(hr) || s_grabFailCount >= 8) {
			s_streamDead = qtrue;
		}
		if (sample) {
			sample->Release();
		}
		return qfalse;
	}
	if (!sample) {
		return qfalse;
	}

	hr = sample->ConvertToContiguousBuffer(&buffer);
	sample->Release();
	if (FAILED(hr) || !buffer) {
		Webcam_Log("ConvertToContiguousBuffer failed hr=0x%08lx", (unsigned long)hr);
		return qfalse;
	}

	hr = buffer->Lock(&data, &maxLen, &curLen);
	if (FAILED(hr) || !data || curLen == 0) {
		Webcam_Log("IMFMediaBuffer::Lock failed hr=0x%08lx curLen=%lu", (unsigned long)hr, (unsigned long)curLen);
		buffer->Release();
		return qfalse;
	}

	if (s_srcFormat == WEBCAM_FMT_YUY2) {
		Webcam_Yuy2ToBgra(data, s_srcWidth, s_srcHeight, s_srcStride, s_srcBottomUp, s_lastFrame);
	} else if (s_srcFormat == WEBCAM_FMT_NV12) {
		Webcam_Nv12ToBgra(data, s_srcWidth, s_srcHeight, s_srcStride, s_lastFrame);
	} else if (s_srcFormat == WEBCAM_FMT_RGB24) {
		Webcam_RgbToBgra(data, s_srcWidth, s_srcHeight, s_srcStride, 3, s_srcBottomUp, s_lastFrame);
	} else {
		Webcam_RgbToBgra(data, s_srcWidth, s_srcHeight, s_srcStride, 4, s_srcBottomUp, s_lastFrame);
	}
	s_haveFrame = qtrue;

	buffer->Unlock();
	buffer->Release();
	return qtrue;
}

static byte Webcam_SampleChannel(const byte *src, int x, int y, int channel)
{
	if (x < 0) {
		x = 0;
	} else if (x >= s_srcWidth) {
		x = s_srcWidth - 1;
	}
	if (y < 0) {
		y = 0;
	} else if (y >= s_srcHeight) {
		y = s_srcHeight - 1;
	}
	return src[(y * s_srcWidth + x) * 4 + channel];
}

static void Webcam_BuildTexture(void)
{
	int   x, y, cropSize, sx0, sy0;
	float crop, yBias;

	if (!s_haveFrame || s_srcWidth <= 0 || s_srcHeight <= 0) {
		return;
	}

	crop = cl_webcamCrop->value;
	if (crop < 0.25f) {
		crop = 0.25f;
	} else if (crop > 1.0f) {
		crop = 1.0f;
	}
	yBias = cl_webcamYBias->value;
	if (yBias < -0.4f) {
		yBias = -0.4f;
	} else if (yBias > 0.4f) {
		yBias = 0.4f;
	}

	cropSize = (int)((s_srcWidth < s_srcHeight ? s_srcWidth : s_srcHeight) * crop);
	if (cropSize < 16) {
		cropSize = 16;
	}
	sx0 = (s_srcWidth - cropSize) / 2;
	sy0 = (int)((s_srcHeight - cropSize) * (0.5f + yBias));
	if (sy0 < 0) {
		sy0 = 0;
	}
	if (sy0 + cropSize > s_srcHeight) {
		sy0 = s_srcHeight - cropSize;
	}

	for (y = 0; y < WEBCAM_TEX_SIZE; y++) {
		for (x = 0; x < WEBCAM_TEX_SIZE; x++) {
			int   dx, b, g, r, a;
			float nx, ny, d, srcX, srcY;

			dx   = cl_webcamMirror->integer ? (WEBCAM_TEX_SIZE - 1 - x) : x;
			srcX = sx0 + (dx + 0.5f) * cropSize / (float)WEBCAM_TEX_SIZE;
			srcY = sy0 + (y + 0.5f) * cropSize / (float)WEBCAM_TEX_SIZE;
			b    = Webcam_SampleChannel(s_lastFrame, (int)srcX, (int)srcY, 0);
			g    = Webcam_SampleChannel(s_lastFrame, (int)srcX, (int)srcY, 1);
			r    = Webcam_SampleChannel(s_lastFrame, (int)srcX, (int)srcY, 2);
			nx   = (x + 0.5f) / (float)WEBCAM_TEX_SIZE * 2.0f - 1.0f;
			ny   = (y + 0.5f) / (float)WEBCAM_TEX_SIZE * 2.0f - 1.0f;
			d    = nx * nx + ny * ny;
			if (d >= 1.0f) {
				a = 0;
			} else if (d > 0.82f) {
				a = (int)((1.0f - d) / 0.18f * 255.0f);
			} else {
				a = 255;
			}
			s_tex[(y * WEBCAM_TEX_SIZE + x) * 4 + 0] = (byte)r;
			s_tex[(y * WEBCAM_TEX_SIZE + x) * 4 + 1] = (byte)g;
			s_tex[(y * WEBCAM_TEX_SIZE + x) * 4 + 2] = (byte)b;
			s_tex[(y * WEBCAM_TEX_SIZE + x) * 4 + 3] = (byte)a;
		}
	}
}

static void Webcam_TryUpload(void)
{
	if (!CL_WebcamRendererReady()) {
		if (!s_loggedNoRenderer) {
			Webcam_Log("frame ready but renderer UploadWebcam is missing");
			s_loggedNoRenderer = qtrue;
		}
		return;
	}
	CL_UploadWebcamTex(WEBCAM_TEX_SIZE, WEBCAM_TEX_SIZE, s_tex);
	if (!s_loggedUpload) {
		Webcam_Log("uploaded %dx%d frame to renderer", WEBCAM_TEX_SIZE, WEBCAM_TEX_SIZE);
		s_loggedUpload = qtrue;
	}
}

static DWORD WINAPI Webcam_ThreadProc(LPVOID param)
{
	HRESULT hr;
	int     interval;

	(void)param;
	Webcam_Log("capture thread start");

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	Webcam_Log("thread CoInitializeEx hr=0x%08lx", (unsigned long)hr);
	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	Webcam_Log("thread MFStartup hr=0x%08lx", (unsigned long)hr);
	if (FAILED(hr) && hr != MF_E_ALREADY_INITIALIZED) {
		s_captureFailed = qtrue;
		return 0;
	}

	if (!Webcam_StartCapture()) {
		s_captureFailed = qtrue;
		Webcam_Log("no local camera, waiting for the other client");
	} else {
		Webcam_Log("capture thread looping");
		while (s_stopEvent && WaitForSingleObject(s_stopEvent, 0) != WAIT_OBJECT_0) {
			interval = 1000 / (cl_webcamFps && cl_webcamFps->integer > 0 ? cl_webcamFps->integer : 15);
			if (interval < 33) {
				interval = 33;
			}
			if (Webcam_GrabFrame()) {
				if (!s_loggedFirstFrame) {
					Webcam_Log("got first camera frame %dx%d", s_srcWidth, s_srcHeight);
					s_loggedFirstFrame = qtrue;
				}
				Webcam_BuildTexture();
				if (s_csInited) {
					EnterCriticalSection(&s_texCs);
				}
				memcpy(s_texPub, s_tex, WEBCAM_TEX_BYTES);
				s_hasTex = 1;
				if (s_csInited) {
					LeaveCriticalSection(&s_texCs);
				}
				Webcam_WriteShare();
			} else if (s_streamDead) {
				Webcam_Log("camera stream died, falling back to shared frames");
				s_captureFailed = qtrue;
				break;
			}
			if (s_stopEvent && WaitForSingleObject(s_stopEvent, (DWORD)interval) == WAIT_OBJECT_0) {
				break;
			}
		}
	}

	Webcam_StopCapture();
	MFShutdown();
	CoUninitialize();
	Webcam_Log("capture thread exit");
	return 0;
}

static void Webcam_StopThread(void)
{
	HANDLE th;

	th = s_thread;
	if (!th) {
		Webcam_StopCapture();
		return;
	}
	if (s_stopEvent) {
		SetEvent(s_stopEvent);
	}
	if (WaitForSingleObject(th, 2000) != WAIT_OBJECT_0) {
		Webcam_Log("capture thread still busy after 2s, detaching");
	}
	CloseHandle(th);
	s_thread = NULL;
	if (s_stopEvent) {
		CloseHandle(s_stopEvent);
		s_stopEvent = NULL;
	}
	s_webcamStarted = qfalse;
	s_hasTex        = 0;
}

static void Webcam_StartThread(void)
{
	if (s_thread) {
		return;
	}
	s_captureFailed    = qfalse;
	s_loggedFirstFrame = qfalse;
	s_hasTex           = 0;
	if (s_stopEvent) {
		CloseHandle(s_stopEvent);
	}
	s_stopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	s_thread    = CreateThread(NULL, 0, Webcam_ThreadProc, NULL, 0, NULL);
	if (!s_thread) {
		Webcam_Log("CreateThread failed err=%lu", (unsigned long)GetLastError());
		s_captureFailed = qtrue;
		return;
	}
	Webcam_Log("capture thread launched");
}

static void Webcam_Start_f(void)
{
	Cvar_Set("cl_webcam", "1");
	Webcam_Log("webcam_start");
	Webcam_StartThread();
}

static void Webcam_Stop_f(void)
{
	Cvar_Set("cl_webcam", "0");
	Webcam_StopThread();
	Webcam_SetReady(qfalse);
	Webcam_Log("stopped");
}

static void Webcam_Status_f(void)
{
	Webcam_Log(
		"status capture=%s thread=%s share=%s device=%d size=%dx%d ready=%d upload=%s",
		s_webcamStarted ? "on" : "off",
		s_thread ? "on" : "off",
		(s_share && s_share->magic == WEBCAM_SHARE_MAGIC) ? "yes" : "no",
		cl_webcamDevice->integer,
		s_srcWidth,
		s_srcHeight,
		cl_webcamReady->integer,
		CL_WebcamRendererReady() ? "yes" : "no"
	);
}

void CL_InitWebcam(void)
{
	const char *appdata;

	if (s_webcamInited) {
		return;
	}

	s_mainTid = GetCurrentThreadId();
	InitializeCriticalSection(&s_logCs);
	InitializeCriticalSection(&s_texCs);
	s_csInited = qtrue;

	cl_webcam       = Cvar_Get("cl_webcam", "1", CVAR_ARCHIVE);
	cl_webcamDevice = Cvar_Get("cl_webcamDevice", "0", CVAR_ARCHIVE);
	cl_webcamFps    = Cvar_Get("cl_webcamFps", "15", CVAR_ARCHIVE);
	cl_webcamCrop   = Cvar_Get("cl_webcamCrop", "0.62", CVAR_ARCHIVE);
	cl_webcamMirror = Cvar_Get("cl_webcamMirror", "1", CVAR_ARCHIVE);
	cl_webcamYBias  = Cvar_Get("cl_webcamYBias", "-0.08", CVAR_ARCHIVE);
	cl_webcamReady  = Cvar_Get("cl_webcamReady", "0", 0);

	Cmd_AddCommand("webcam_start", Webcam_Start_f);
	Cmd_AddCommand("webcam_stop", Webcam_Stop_f);
	Cmd_AddCommand("webcam_status", Webcam_Status_f);
	s_webcamInited = qtrue;

	appdata = getenv("APPDATA");
	Webcam_Log(
		"init cl_webcam=%d device=%d fps=%d crop=%.2f mirror=%d ybias=%.2f upload=%s log=%s\\openmohaa\\webcam.log",
		cl_webcam->integer,
		cl_webcamDevice->integer,
		cl_webcamFps->integer,
		cl_webcamCrop->value,
		cl_webcamMirror->integer,
		cl_webcamYBias->value,
		CL_WebcamRendererReady() ? "yes" : "no",
		appdata ? appdata : "?"
	);

	if (cl_webcam->integer) {
		Webcam_StartThread();
	} else {
		Webcam_Log("off (set cl_webcam 1, then webcam_start)");
	}
}

void CL_ShutdownWebcam(void)
{
	if (!s_webcamInited) {
		return;
	}
	Webcam_Log("shutdown");
	Webcam_StopThread();
	Webcam_CloseShare();
	Webcam_SetReady(qfalse);
	Cmd_RemoveCommand("webcam_start");
	Cmd_RemoveCommand("webcam_stop");
	Cmd_RemoveCommand("webcam_status");
	s_webcamInited = qfalse;
	if (s_csInited) {
		DeleteCriticalSection(&s_logCs);
		DeleteCriticalSection(&s_texCs);
		s_csInited = qfalse;
	}
}

void CL_WebcamFrame(void)
{
	if (!s_webcamInited || !cl_webcam) {
		return;
	}
	if (!cl_webcam->integer) {
		if (s_thread) {
			Webcam_StopThread();
		}
		Webcam_SetReady(qfalse);
		if (!s_loggedOff) {
			Webcam_Log("off (set cl_webcam 1, then webcam_start)");
			s_loggedOff = qtrue;
		}
		return;
	}
	s_loggedOff = qfalse;

	if (!s_thread && !s_captureFailed) {
		Webcam_StartThread();
	}

	if (s_hasTex) {
		if (s_csInited) {
			EnterCriticalSection(&s_texCs);
		}
		memcpy(s_tex, s_texPub, WEBCAM_TEX_BYTES);
		if (s_csInited) {
			LeaveCriticalSection(&s_texCs);
		}
		Webcam_TryUpload();
		Webcam_SetReady(qtrue);
		return;
	}

	if (Webcam_ReadShare()) {
		if (!s_shareAnnounced) {
			Webcam_Log("using the other local client's camera");
			s_shareAnnounced = qtrue;
		}
		Webcam_TryUpload();
		Webcam_SetReady(qtrue);
	}
}

#else

void CL_InitWebcam(void)
{
	cl_webcam      = Cvar_Get("cl_webcam", "0", CVAR_ARCHIVE);
	cl_webcamReady = Cvar_Get("cl_webcamReady", "0", 0);
	Com_Printf("webcam: only available on Windows\n");
}

void CL_ShutdownWebcam(void) {}

void CL_WebcamFrame(void) {}

#endif
