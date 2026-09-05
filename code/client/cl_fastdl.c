/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
===========================================================================
*/

/*
 * cl_fastdl.c
 *
 * MOH-DB Fast-DL fallback after pr_downloads.
 * Stays CA_CONNECTED (curl_multi + existing cl_download* UI).
 * Does not disconnect, does not use FS_ComparePaks / CL_NextDownload.
 */

#include "client.h"
#include "../sys/sys_curl.h"
#include "../qcommon/unzip.h"

#define FASTDL_DEFAULT_URL "https://api.moh-db.com/api/v1/fastdl"
#define FASTDL_MAX_DOWNLOAD_BYTES (400L * 1024L * 1024L)
#define FASTDL_DOWNLOAD_LOW_SPEED_LIMIT 1L
#define FASTDL_DOWNLOAD_LOW_SPEED_TIME 30L
#define FASTDL_RETRY_DELAY_MS 5000
#define FASTDL_MAX_RETRIES 4
#define FASTDL_USERAGENT "OpenMoHAA-fastdl/1.0"

#ifdef HAS_LIBCURL

static char         s_fdAttemptedMap[MAX_QPATH];
static qboolean     s_fdActive;
static qboolean     s_fdRetryPending;
static qboolean     s_fdWriteFailed;
static qboolean     s_fdSizeExceeded;
static int          s_fdRetryAt;
static int          s_fdRetryCount;
static size_t       s_fdBytesWritten;
static CURL        *s_fdCurl;
static CURLM       *s_fdCurlM;
static fileHandle_t s_fdFile;
static char         s_fdLocalName[MAX_OSPATH];
static char         s_fdTempName[MAX_OSPATH];
static char         s_fdUrl[MAX_CVAR_VALUE_STRING + MAX_OSPATH + 32];

static int    s_fdLastProgressPct = -1;
static double s_fdSpeedSampleBytes;
static int    s_fdSpeedSampleTime;
static float  s_fdDisplaySpeed;

static void CL_FD_CleanupActive(void);
static void CL_FD_DeleteTemp(void);
static qboolean CL_FD_StartDownload(void);

static qboolean CL_FD_IsHttpUrl(const char *url)
{
    if (!url || !url[0]) {
        return qfalse;
    }
    return (Q_stricmpn(url, "http://", 7) == 0 ||
            Q_stricmpn(url, "https://", 8) == 0);
}

static qboolean CL_FD_IsSafePakBasename(const char *name)
{
    int i;
    int len;

    if (!name || !name[0]) {
        return qfalse;
    }

    len = (int)strlen(name);
    if (len < 5 || len >= MAX_OSPATH) {
        return qfalse;
    }

    if (!COM_CompareExtension(name, ".pk3")) {
        return qfalse;
    }

    if (name[0] == '.' || strstr(name, "..")) {
        return qfalse;
    }

    for (i = 0; i < len; i++) {
        char c = name[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            continue;
        }
        return qfalse;
    }

    return qtrue;
}

static void CL_FD_SetHttpOnly(CURL *curl)
{
    curlImport.qcurl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                                 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curlImport.qcurl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                                 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
}

static void CL_FD_ResetSpeed(void)
{
    s_fdSpeedSampleBytes = 0.0;
    s_fdSpeedSampleTime  = cls.realtime;
    s_fdDisplaySpeed     = 0.0f;
    Cvar_Set("cl_downloadSpeed", "0");
}

static void CL_FD_SetUserErrorStatus(const char *reason)
{
    char statusLine[256];

    if (!reason || !reason[0]) {
        reason = "Download failed";
    }
    Com_sprintf(statusLine, sizeof(statusLine), "Fast-DL error: %s", reason);
    Cvar_Set("cl_downloadError", statusLine);
}

static void CL_FD_ClearUi(void)
{
    Cvar_Set("cl_downloadName", "");
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadCurrent", "0");
    Cvar_Set("cl_downloadTotal", "0");
    Cvar_Set("cl_downloadSize", "0");
    Cvar_Set("cl_downloadCount", "0");
    CL_FD_ResetSpeed();
}

static void CL_FD_DeleteTemp(void)
{
    if (!s_fdTempName[0]) {
        return;
    }
    FS_Remove_HomeData(s_fdTempName);
}

static void CL_FD_CleanupActive(void)
{
    if (s_fdFile) {
        FS_FCloseFile(s_fdFile);
        s_fdFile = 0;
    }

    if (s_fdCurlM && s_fdCurl) {
        curlImport.qcurl_multi_remove_handle(s_fdCurlM, s_fdCurl);
    }

    if (s_fdCurl) {
        curlImport.qcurl_easy_cleanup(s_fdCurl);
        s_fdCurl = NULL;
    }

    if (s_fdCurlM) {
        curlImport.qcurl_multi_cleanup(s_fdCurlM);
        s_fdCurlM = NULL;
    }

    s_fdActive = qfalse;
}

static void CL_FD_PrintProgressBar(const char *name, int pct)
{
    char bar[21];
    int  i;
    int  filled;

    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }

    filled = pct / 5;
    for (i = 0; i < 20; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[20] = '\0';

    Com_Printf("Fast-DL: [%s] %3d%% %s\n", bar, pct, name ? name : "");
}

static size_t CL_FD_FileWriteCB(void *ptr, size_t size, size_t nmemb, void *userp)
{
    fileHandle_t *fh    = (fileHandle_t *)userp;
    size_t        total = size * nmemb;
    size_t        wrote;

    if (!fh || !*fh || total == 0) {
        return 0;
    }

    if (s_fdBytesWritten + total > (size_t)FASTDL_MAX_DOWNLOAD_BYTES) {
        s_fdSizeExceeded = qtrue;
        s_fdWriteFailed  = qtrue;
        return 0;
    }

    wrote = FS_Write(ptr, total, *fh);
    if (wrote != total) {
        s_fdWriteFailed = qtrue;
        return 0;
    }

    s_fdBytesWritten += wrote;
    return wrote;
}

static int CL_FD_ProgressCB(void *userp, double dltotal, double dlnow,
                            double ultotal, double ulnow)
{
    const char *name = (const char *)userp;
    int         pct;

    (void)ultotal;
    (void)ulnow;

    if (dltotal > (double)FASTDL_MAX_DOWNLOAD_BYTES ||
        dlnow > (double)FASTDL_MAX_DOWNLOAD_BYTES) {
        s_fdSizeExceeded = qtrue;
        return 1;
    }

    Cvar_SetValue("cl_downloadSize",  (float)dltotal);
    Cvar_SetValue("cl_downloadCount", (float)dlnow);

    if (cls.realtime - s_fdSpeedSampleTime >= 200) {
        double dt = (cls.realtime - s_fdSpeedSampleTime) / 1000.0;

        if (dt > 0.0) {
            double instant = (dlnow - s_fdSpeedSampleBytes) / dt;

            if (instant < 0.0) {
                instant = 0.0;
            }

            if (s_fdDisplaySpeed <= 0.0f) {
                s_fdDisplaySpeed = (float)instant;
            } else {
                s_fdDisplaySpeed = (float)(s_fdDisplaySpeed * 0.55 + instant * 0.45);
            }

            Cvar_SetValue("cl_downloadSpeed", s_fdDisplaySpeed);
        }

        s_fdSpeedSampleBytes = dlnow;
        s_fdSpeedSampleTime  = cls.realtime;
    }

    if (dltotal > 0.0) {
        pct = (int)((dlnow * 100.0) / dltotal);
        if (pct > 100) {
            pct = 100;
        }

        if (pct != s_fdLastProgressPct) {
            s_fdLastProgressPct = pct;
            if ((pct % 10) == 0 || pct == 100) {
                CL_FD_PrintProgressBar(name, pct);
            }
        }
    }

    return 0;
}

static void CL_FD_BuildUrl(const char *remoteName, char *out, int outSize)
{
    char        fastDlClean[MAX_CVAR_VALUE_STRING];
    const char *fastDlBase;
    size_t      len;

    fastDlBase = Cvar_VariableString("cl_fastdl_url");
    if (!fastDlBase || !fastDlBase[0]) {
        fastDlBase = FASTDL_DEFAULT_URL;
    }

    while (*fastDlBase == ' ' || *fastDlBase == '"' || *fastDlBase == '\'') {
        fastDlBase++;
    }
    Q_strncpyz(fastDlClean, fastDlBase, sizeof(fastDlClean));
    len = strlen(fastDlClean);
    while (len > 0 && (fastDlClean[len - 1] == ' ' ||
                       fastDlClean[len - 1] == '"' ||
                       fastDlClean[len - 1] == '\'')) {
        fastDlClean[--len] = '\0';
    }

    if (len == 0 || !CL_FD_IsHttpUrl(fastDlClean)) {
        Q_strncpyz(fastDlClean, FASTDL_DEFAULT_URL, sizeof(fastDlClean));
        len = strlen(fastDlClean);
    }

    if (fastDlClean[len - 1] == '/') {
        Com_sprintf(out, outSize, "%s%s", fastDlClean, remoteName);
    } else {
        Com_sprintf(out, outSize, "%s/%s", fastDlClean, remoteName);
    }
}

static qboolean CL_FD_ExtractNestedPk3s(const char *ospath, qboolean *foundNested)
{
    unzFile         uf;
    unz_global_info gi;
    int             err;
    int             i;
    int             extracted = 0;
    int             nestedCount = 0;

    *foundNested = qfalse;

    uf = unzOpen(ospath);
    if (!uf) {
        return qfalse;
    }

    err = unzGetGlobalInfo(uf, &gi);
    if (err != UNZ_OK || gi.number_entry == 0) {
        unzClose(uf);
        return qfalse;
    }

    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        return qfalse;
    }

    for (i = 0; i < (int)gi.number_entry; i++) {
        char          filename_inzip[MAX_OSPATH];
        unz_file_info file_info;
        size_t        nlen;

        err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip,
                                    sizeof(filename_inzip), NULL, 0, NULL, 0);
        if (err != UNZ_OK) {
            unzClose(uf);
            return qfalse;
        }

        nlen = strlen(filename_inzip);
        if (nlen > 4 && !Q_stricmp(filename_inzip + nlen - 4, ".pk3")) {
            const char *cleanName = COM_SkipPath(filename_inzip);

            *foundNested = qtrue;
            nestedCount++;

            if (CL_FD_IsSafePakBasename(cleanName) &&
                unzOpenCurrentFile(uf) == UNZ_OK) {
                char stagedName[MAX_OSPATH];
                fileHandle_t outF;

                Com_sprintf(stagedName, sizeof(stagedName), "%s.fastdl.tmp", cleanName);
                outF = FS_FOpenFileWrite_HomeData(stagedName);

                if (outF) {
                    char   chunk[65536];
                    int    readBytes;
                    size_t totalExtracted = 0;
                    qboolean failed = qfalse;

                    while ((readBytes = unzReadCurrentFile(uf, chunk, sizeof(chunk))) > 0) {
                        totalExtracted += (size_t)readBytes;
                        if (totalExtracted > (size_t)FASTDL_MAX_DOWNLOAD_BYTES) {
                            Com_Printf("Fast-DL: extracted %s exceeds 400 MiB cap, aborting\n",
                                       cleanName);
                            failed = qtrue;
                            break;
                        }
                        if (FS_Write(chunk, (size_t)readBytes, outF) != (size_t)readBytes) {
                            failed = qtrue;
                            break;
                        }
                    }
                    FS_FCloseFile(outF);

                    if (readBytes < 0 || unzCloseCurrentFile(uf) != UNZ_OK) {
                        failed = qtrue;
                    }

                    if (!failed && FS_Replace_HomeData(stagedName, cleanName)) {
                        extracted++;
                        Com_Printf("Fast-DL: extracted '%s' (%u KB)\n",
                                   cleanName,
                                   (unsigned int)(totalExtracted / 1024));
                    } else {
                        FS_Remove_HomeData(stagedName);
                    }
                } else {
                    unzCloseCurrentFile(uf);
                }
            }
        }

        if (i + 1 < (int)gi.number_entry && unzGoToNextFile(uf) != UNZ_OK) {
            unzClose(uf);
            return qfalse;
        }
    }

    unzClose(uf);

    if (extracted > 0 && extracted == nestedCount) {
        Com_Printf("Fast-DL: archive contained %d PK3 package(s)\n", extracted);
        return qtrue;
    }
    return qfalse;
}

static qboolean CL_FD_InstallDownload(void)
{
    const char *homedatapath;
    char       *ospath;
    char        ospathCopy[MAX_OSPATH];
    qboolean    foundNested;

    homedatapath = Cvar_VariableString("fs_homedatapath");
    ospath = FS_BuildOSPath(homedatapath, FS_Gamedir(), s_fdTempName);
    Q_strncpyz(ospathCopy, ospath, sizeof(ospathCopy));

    if (CL_FD_ExtractNestedPk3s(ospathCopy, &foundNested)) {
        FS_Remove_HomeData(s_fdTempName);
        return qtrue;
    }

    if (foundNested) {
        return qfalse;
    }

    return FS_Replace_HomeData(s_fdTempName, s_fdLocalName);
}

static void CL_FD_ScheduleRetry(const char *reason)
{
    if (!reason || !reason[0]) {
        reason = "network error";
    }

    if (s_fdRetryCount >= FASTDL_MAX_RETRIES) {
        Com_Printf("Fast-DL: giving up after %d retries (%s)\n",
                   FASTDL_MAX_RETRIES, reason);
        CL_FD_DeleteTemp();
        CL_FD_CleanupActive();
        CL_FD_ClearUi();
        CL_DownloadsComplete();
        return;
    }

    s_fdRetryCount++;
    s_fdRetryPending = qtrue;
    s_fdRetryAt      = cls.realtime + FASTDL_RETRY_DELAY_MS;
    CL_FD_SetUserErrorStatus(va("retrying in %d sec (%s)",
                                FASTDL_RETRY_DELAY_MS / 1000, reason));
    Com_Printf("Fast-DL: %s, retrying in %d seconds (%s)\n",
               s_fdLocalName, FASTDL_RETRY_DELAY_MS / 1000, reason);
}

static qboolean CL_FD_StartDownload(void)
{
    if (!CL_FD_IsHttpUrl(s_fdUrl)) {
        Com_Printf("Fast-DL: refusing non-HTTP URL: %s\n", s_fdUrl);
        return qfalse;
    }

    Com_sprintf(s_fdTempName, sizeof(s_fdTempName), "%s.tmp", s_fdLocalName);

    s_fdFile = FS_FOpenFileWrite_HomeData(s_fdTempName);
    if (!s_fdFile) {
        Com_Printf("Fast-DL: could not open %s for writing\n", s_fdTempName);
        return qfalse;
    }

    s_fdCurl = curlImport.qcurl_easy_init();
    if (!s_fdCurl) {
        FS_FCloseFile(s_fdFile);
        s_fdFile = 0;
        return qfalse;
    }

    s_fdCurlM = curlImport.qcurl_multi_init();
    if (!s_fdCurlM) {
        curlImport.qcurl_easy_cleanup(s_fdCurl);
        s_fdCurl = NULL;
        FS_FCloseFile(s_fdFile);
        s_fdFile = 0;
        return qfalse;
    }

    Cvar_Set("cl_downloadName", s_fdLocalName);
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadSize", "0");
    Cvar_Set("cl_downloadCount", "0");
    Cvar_SetValue("cl_downloadCurrent", 1.0f);
    Cvar_SetValue("cl_downloadTotal", 1.0f);
    Cvar_SetValue("cl_downloadTime", cls.realtime);
    s_fdLastProgressPct = -1;
    s_fdWriteFailed     = qfalse;
    s_fdSizeExceeded    = qfalse;
    s_fdBytesWritten    = 0;
    s_fdRetryPending    = qfalse;
    CL_FD_ResetSpeed();

    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_URL,              s_fdUrl);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_WRITEFUNCTION,    CL_FD_FileWriteCB);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_WRITEDATA,        &s_fdFile);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_NOPROGRESS,       0L);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_PROGRESSFUNCTION, CL_FD_ProgressCB);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_PROGRESSDATA,     (void *)s_fdLocalName);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_FOLLOWLOCATION,   1L);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_MAXREDIRS,        5L);
    CL_FD_SetHttpOnly(s_fdCurl);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_USERAGENT,        FASTDL_USERAGENT);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_MAXFILESIZE,      FASTDL_MAX_DOWNLOAD_BYTES);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_FAILONERROR,      1L);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_LOW_SPEED_LIMIT,  FASTDL_DOWNLOAD_LOW_SPEED_LIMIT);
    curlImport.qcurl_easy_setopt(s_fdCurl, CURLOPT_LOW_SPEED_TIME,   FASTDL_DOWNLOAD_LOW_SPEED_TIME);

    if (curlImport.qcurl_multi_add_handle(s_fdCurlM, s_fdCurl) != CURLM_OK) {
        CL_FD_CleanupActive();
        return qfalse;
    }

    s_fdActive = qtrue;
    clc.state  = CA_CONNECTED;
    UI_AbortLoad();
    Com_Printf("Fast-DL: downloading %s from %s\n", s_fdLocalName, s_fdUrl);
    return qtrue;
}

qboolean CL_FastDLPending(void)
{
    return (s_fdActive || s_fdRetryPending) ? qtrue : qfalse;
}

qboolean CL_TryFastDLMapFallback(void)
{
    const char *info;
    const char *mapname;
    const char *baseName;
    char        pk3Name[MAX_OSPATH];

    if (CL_FastDLPending()) {
        return qtrue;
    }

    if (Cvar_VariableIntegerValue("cl_fastdl") <= 0) {
        return qfalse;
    }

    if (!Com_IsCurlImportValid(&curlImport)) {
        Com_Printf("Fast-DL: cURL not available, skipping map fallback\n");
        return qfalse;
    }

    info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
    mapname = Info_ValueForKey(info, "mapname");
    if (!mapname || !mapname[0]) {
        return qfalse;
    }

    if (!Q_stricmp(s_fdAttemptedMap, mapname)) {
        return qfalse;
    }

    baseName = COM_SkipPath(mapname);
    Com_sprintf(pk3Name, sizeof(pk3Name), "%s.pk3", baseName);

    if (!CL_FD_IsSafePakBasename(pk3Name)) {
        Com_Printf("Fast-DL: refusing unsafe map pak name: %s\n", pk3Name);
        return qfalse;
    }

    if (FS_MapExists(mapname) || FS_FileExists_HomeData(pk3Name)) {
        return qfalse;
    }

    Q_strncpyz(s_fdAttemptedMap, mapname, sizeof(s_fdAttemptedMap));
    Q_strncpyz(s_fdLocalName, pk3Name, sizeof(s_fdLocalName));
    CL_FD_BuildUrl(pk3Name, s_fdUrl, sizeof(s_fdUrl));

    if (!CL_FD_IsHttpUrl(s_fdUrl)) {
        Com_Printf("Fast-DL: ignoring non-HTTP URL: %s\n", s_fdUrl);
        return qfalse;
    }

    Com_Printf("Fast-DL: server map '%s' is missing after pr_downloads, fetching via MOH-DB\n",
               mapname);

    s_fdRetryCount = 0;
    clc.state = CA_CONNECTED;

    if (!CL_FD_StartDownload()) {
        Com_Printf("Fast-DL: failed to start map download\n");
        CL_FD_ScheduleRetry("start failed");
        return CL_FastDLPending();
    }

    return qtrue;
}

void CL_FastDLFrame(void)
{
    CURLMcode res;
    CURLMsg  *msg;
    CURLMsg  *doneMsg;
    int       running;
    int       msgCount;

    if (!CL_FastDLPending()) {
        return;
    }

    clc.state = CA_CONNECTED;
    clc.connectStartTime = cls.realtime;

    if (!s_fdActive) {
        if (s_fdRetryPending && cls.realtime >= s_fdRetryAt) {
            s_fdRetryPending = qfalse;
            if (!CL_FD_StartDownload()) {
                CL_FD_ScheduleRetry("start failed");
            }
        }
        return;
    }

    if (!s_fdCurlM) {
        return;
    }

    do {
        res = curlImport.qcurl_multi_perform(s_fdCurlM, &running);
    } while (res == CURLM_CALL_MULTI_PERFORM);

    if (res != CURLM_OK) {
        const char *reason = curlImport.qcurl_multi_strerror(res);
        Com_Printf("Fast-DL: multi_perform failed: %s\n", reason);
        CL_FD_CleanupActive();
        CL_FD_DeleteTemp();
        CL_FD_ScheduleRetry(reason);
        return;
    }

    doneMsg = NULL;
    while ((msg = curlImport.qcurl_multi_info_read(s_fdCurlM, &msgCount)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            doneMsg = msg;
            break;
        }
    }

    if (!doneMsg) {
        return;
    }

    if (s_fdFile) {
        FS_FCloseFile(s_fdFile);
        s_fdFile = 0;
    }

    if (s_fdSizeExceeded || doneMsg->data.result == CURLE_FILESIZE_EXCEEDED) {
        Com_Printf("Fast-DL: %s exceeds 400 MiB cap, aborting\n", s_fdLocalName);
        CL_FD_DeleteTemp();
        CL_FD_CleanupActive();
        CL_FD_ClearUi();
        CL_DownloadsComplete();
        return;
    }

    if (doneMsg->data.result == CURLE_OK) {
        if (!CL_FD_InstallDownload()) {
            CL_FD_CleanupActive();
            CL_FD_DeleteTemp();
            CL_FD_ScheduleRetry("failed to install downloaded pak");
            return;
        }
        clc.downloadRestart = qtrue;
        s_fdRetryCount = 0;
        Com_Printf("Fast-DL: %s downloaded successfully\n", s_fdLocalName);
        CL_FD_CleanupActive();
        CL_FD_ClearUi();
        CL_DownloadsComplete();
        return;
    }

    {
        const char *errorReason = curlImport.qcurl_easy_strerror(doneMsg->data.result);

        Com_Printf("Fast-DL: download of %s failed: %s\n", s_fdLocalName, errorReason);
        if (s_fdWriteFailed) {
            errorReason = "disk full or local write error";
        }
        if (doneMsg->data.result == CURLE_OPERATION_TIMEDOUT) {
            errorReason = "transfer stalled / timed out";
        }

        CL_FD_SetUserErrorStatus(errorReason);
        CL_FD_DeleteTemp();
        CL_FD_CleanupActive();
        CL_FD_ScheduleRetry(errorReason);
    }
}

void CL_ShutdownFastDL(void)
{
    CL_FD_CleanupActive();
    CL_FD_DeleteTemp();
    s_fdTempName[0]   = '\0';
    s_fdLocalName[0]  = '\0';
    s_fdUrl[0]        = '\0';
    s_fdAttemptedMap[0] = '\0';
    s_fdRetryPending  = qfalse;
    s_fdRetryAt       = 0;
    s_fdRetryCount    = 0;
    s_fdSizeExceeded  = qfalse;
    s_fdBytesWritten  = 0;
    CL_FD_ClearUi();
}

#else

qboolean CL_TryFastDLMapFallback(void) { return qfalse; }
qboolean CL_FastDLPending(void) { return qfalse; }
void CL_FastDLFrame(void) {}
void CL_ShutdownFastDL(void) {}

#endif /* HAS_LIBCURL */
