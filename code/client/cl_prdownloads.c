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
 * cl_prdownloads.c
 *
 * Auto-downloader for server-hosted pk3 files via pr_downloads.
 *
 * The server sets:
 *   sets pr_downloads "https://example.com/path/filelist.txt"
 *
 * The filelist uses the format:
 *   map {
 *     alias "some name"
 *     md5   "lowercase-hex-md5"
 *     url   "https://example.com/path/file.pk3"
 *   }
 *
 * On connect, the client:
 *   1. Fetches filelist.txt synchronously
 *   2. Checks MD5 of each locally-installed pk3
 *   3. Downloads any that are missing or have an incorrect MD5
 *   4. Re-checks MD5 of the downloaded temp file before installing
 *   5. Sets clc.downloadRestart so the engine reloads paks on reconnect
 *
 * Local names are always main/<safe-basename>.pk3. File URLs must be http(s).
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "client.h"
#include "../sys/sys_curl.h"

#ifdef HAS_LIBCURL

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} prFetchBuf_t;

typedef struct {
    char localName[MAX_OSPATH];
    char url[1024];
    char md5Expected[33];
} prDownloadEntry_t;

#define PR_MAX_DOWNLOADS 128
/* Hard cap per file: 400 MiB. CURLOPT_MAXFILESIZE plus write/progress abort. */
#define PR_MAX_DOWNLOAD_BYTES (400L * 1024L * 1024L)
#define PR_DOWNLOAD_LOW_SPEED_LIMIT 1L
#define PR_DOWNLOAD_LOW_SPEED_TIME 30L
#define PR_RETRY_DELAY_MS 5000
#define PR_MAX_RETRIES 4
#define PR_USERAGENT "OpenMoHAA-pr_downloads/1.0"

static prDownloadEntry_t s_prQueue[PR_MAX_DOWNLOADS];
static int               s_prQueueCount;
static int               s_prQueueIndex;
static qboolean          s_prDownloadActive;
static qboolean          s_prHadDownloads;

static CURL             *s_prCurl;
static CURLM            *s_prCurlM;
static fileHandle_t      s_prFile;
static char              s_prTempName[MAX_OSPATH];
static char              s_prCurrentLocalName[MAX_OSPATH];
static char              s_prCurrentMD5[33];
static qboolean          s_prWriteFailed;
static qboolean          s_prSizeExceeded;
static qboolean          s_prRetryPending;
static int               s_prRetryAt;
static int               s_prRetryCount;
static qboolean          s_prFilelistRetry;
static qboolean          s_prFailed;
static size_t            s_prBytesWritten;

static int s_prLastProgressPct = -1;

static double s_prSpeedSampleBytes;
static int    s_prSpeedSampleTime;
static float  s_prDisplaySpeed;

static void CL_PR_DeleteTemp(void);
static void CL_PR_CleanupActiveDownload(void);
static void CL_PR_SetPendingStatus(qboolean pending);
static void CL_PR_SetUserErrorStatus(const char *reason);

static qboolean CL_PR_IsHttpUrl(const char *url)
{
    if (!url || !url[0]) {
        return qfalse;
    }

    return (Q_stricmpn(url, "http://", 7) == 0 ||
            Q_stricmpn(url, "https://", 8) == 0);
}

static qboolean CL_PR_IsPublicIPv4(const byte *ip)
{
    if (ip[0] == 0 || ip[0] == 10 || ip[0] == 127 || ip[0] >= 224 ||
        (ip[0] == 100 && (ip[1] & 0xc0) == 64) ||
        (ip[0] == 169 && ip[1] == 254) ||
        (ip[0] == 172 && (ip[1] & 0xf0) == 16) ||
        (ip[0] == 192 && ip[1] == 168)) {
        return qfalse;
    }

    return qtrue;
}

/* cURL calls this for every connection, including connections created after
 * redirects. Validating the resolved address here also closes DNS-rebinding
 * and hostname-spelling bypasses that a URL-only check cannot catch. */
static curl_socket_t CL_PR_OpenPublicSocket(void *clientp, curlsocktype purpose,
                                            struct curl_sockaddr *address)
{
    static const byte ipv6Unspecified[16] = {0};
    static const byte ipv6Loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                          0, 0, 0, 0, 0, 0, 0, 1};
    static const byte ipv4MappedPrefix[12] = {0, 0, 0, 0, 0, 0,
                                              0, 0, 0, 0, 0xff, 0xff};
    const byte *ip;

    (void)clientp;
    (void)purpose;

    if (address->family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)&address->addr;
        ip = (const byte *)&sin->sin_addr;
        if (!CL_PR_IsPublicIPv4(ip)) {
            Com_Printf("pr_downloads: refusing private/local IPv4 destination\n");
            return CURL_SOCKET_BAD;
        }
    } else if (address->family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)&address->addr;
        ip = (const byte *)&sin6->sin6_addr;

        if (!memcmp(ip, ipv6Unspecified, sizeof(ipv6Unspecified)) ||
            !memcmp(ip, ipv6Loopback, sizeof(ipv6Loopback)) ||
            (ip[0] & 0xfe) == 0xfc ||
            (ip[0] == 0xfe && (ip[1] & 0xc0) == 0x80) ||
            ip[0] == 0xff) {
            Com_Printf("pr_downloads: refusing private/local IPv6 destination\n");
            return CURL_SOCKET_BAD;
        }

        if (!memcmp(ip, ipv4MappedPrefix, sizeof(ipv4MappedPrefix)) &&
            !CL_PR_IsPublicIPv4(ip + 12)) {
            Com_Printf("pr_downloads: refusing private/local IPv4-mapped destination\n");
            return CURL_SOCKET_BAD;
        }
    } else {
        return CURL_SOCKET_BAD;
    }

    return socket(address->family, address->socktype, address->protocol);
}

static qboolean CL_PR_IsValidMd5(const char *md5)
{
    int i;

    if (!md5 || strlen(md5) != 32) {
        return qfalse;
    }

    for (i = 0; i < 32; i++) {
        char c = md5[i];

        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')) {
            continue;
        }
        return qfalse;
    }

    return qtrue;
}

static qboolean CL_PR_IsSafePakBasename(const char *name)
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

static qboolean CL_PR_MakeLocalPakName(const char *url, char *out, int outSize)
{
    const char *start;
    const char *end;
    const char *slash;
    const char *bslash;
    char        fname[MAX_OSPATH];
    int         len;

    if (!url || !out || outSize < 8) {
        return qfalse;
    }

    slash  = strrchr(url, '/');
    bslash = strrchr(url, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }
    start = slash ? slash + 1 : url;

    end = start;
    while (*end && *end != '?' && *end != '#') {
        end++;
    }

    len = (int)(end - start);
    if (len <= 0 || len >= (int)sizeof(fname)) {
        return qfalse;
    }

    memcpy(fname, start, (size_t)len);
    fname[len] = '\0';

    if (!CL_PR_IsSafePakBasename(fname)) {
        return qfalse;
    }

    Com_sprintf(out, outSize, "main/%s", fname);
    return qtrue;
}

static void CL_PR_SetHttpOnly(CURL *curl)
{
    curlImport.qcurl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                                 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curlImport.qcurl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                                 (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
}

static void CL_PR_ApplyEasyOpts(CURL *curl, long maxBytes)
{
    CL_PR_SetHttpOnly(curl);
    /* Do not let an environment proxy turn a public-looking URL into a
     * proxy-side request to an internal destination. */
    curlImport.qcurl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    curlImport.qcurl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION,
                                 CL_PR_OpenPublicSocket);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_USERAGENT, PR_USERAGENT);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_MAXFILESIZE, maxBytes);
}

static void CL_PR_GiveUp(const char *reason)
{
    char msg[256];

    if (!reason || !reason[0]) {
        reason = "download failed";
    }

    s_prFailed       = qtrue;
    s_prRetryPending = qfalse;
    s_prFilelistRetry = qfalse;

    CL_PR_DeleteTemp();
    CL_PR_CleanupActiveDownload();
    CL_PR_SetUserErrorStatus(reason);
    CL_PR_SetPendingStatus(qfalse);

    Com_sprintf(msg, sizeof(msg), "Download aborted: %s", reason);
    Com_Printf("pr_downloads: giving up: %s\n", reason);
    Com_Error(ERR_DROP, "%s", msg);
}

static void CL_PR_DeleteTemp(void)
{
    const char *home;

    if (!s_prTempName[0]) {
        return;
    }

    home = Cvar_VariableString("fs_homedatapath");
    if (!home[0]) {
        return;
    }

    FS_Remove(FS_BaseDir_BuildOSPath(home, s_prTempName));
}

static void CL_PR_ResetSpeed(void)
{
    s_prSpeedSampleBytes = 0.0;
    s_prSpeedSampleTime  = cls.realtime;
    s_prDisplaySpeed     = 0.0f;
    Cvar_Set("cl_downloadSpeed", "0");
}

static void CL_PR_SetPendingStatus(qboolean pending)
{
    Cvar_SetValue("cl_prdownloads_pending", pending ? 1.0f : 0.0f);
}

static void CL_PR_SetUserErrorStatus(const char *reason)
{
    double downloaded = Cvar_VariableValue("cl_downloadCount");
    double total      = Cvar_VariableValue("cl_downloadSize");
    int    percent    = 0;
    char   statusLine[256];

    if (total > 0.0) {
        if (downloaded < 0.0) {
            downloaded = 0.0;
        }
        if (downloaded > total) {
            downloaded = total;
        }
        percent = (int)((downloaded / total) * 100.0);
        if (percent < 0) {
            percent = 0;
        }
        if (percent > 100) {
            percent = 100;
        }
    }

    if (!reason || !reason[0]) {
        reason = "Download failed";
    }

    Com_sprintf(statusLine, sizeof(statusLine), "Download error at %d%%: %s", percent, reason);
    Cvar_Set("cl_downloadError", statusLine);
}

static qboolean CL_PR_IsRetryableCurlError(CURLcode code)
{
    switch (code) {
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
        return qtrue;
    default:
        return qfalse;
    }
}

static void CL_PR_ScheduleRetry(const char *reason)
{
    char msg[256];

    if (!reason || !reason[0]) {
        reason = "network error";
    }

    if (s_prRetryCount >= PR_MAX_RETRIES) {
        Com_sprintf(msg, sizeof(msg), "%s (after %d retries)", reason, PR_MAX_RETRIES);
        CL_PR_GiveUp(msg);
        return;
    }

    s_prRetryCount++;
    s_prRetryPending = qtrue;
    s_prRetryAt      = cls.realtime + PR_RETRY_DELAY_MS;

    Com_sprintf(msg, sizeof(msg), "reconnecting in %d sec (%s)", PR_RETRY_DELAY_MS / 1000, reason);
    CL_PR_SetUserErrorStatus(msg);

    Com_Printf("pr_downloads: %s, retrying in %d seconds (%s)\n",
               s_prCurrentLocalName[0] ? s_prCurrentLocalName : "download",
               PR_RETRY_DELAY_MS / 1000,
               reason);
}

static void CL_PR_PrintProgressBar(const char *name, int pct)
{
    char bar[21];
    int  i;
    int  filled;

    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }

    filled = pct / 5; /* 20 chars total */
    for (i = 0; i < 20; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[20] = '\0';

    Com_Printf("pr_downloads: [%s] %3d%% %s\n", bar, pct, name ? name : "");
}

static size_t CL_PR_TextWriteCB(void *ptr, size_t size, size_t nmemb, void *userp)
{
    size_t       total = size * nmemb;
    prFetchBuf_t *buf  = (prFetchBuf_t *)userp;

    if (buf->size + total + 1 > buf->capacity) {
        return 0; /* signal error - buffer full */
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static size_t CL_PR_FileWriteCB(void *ptr, size_t size, size_t nmemb, void *userp)
{
    fileHandle_t *fh    = (fileHandle_t *)userp;
    size_t        total = size * nmemb;
    size_t        wrote;

    if (!fh || !*fh || total == 0) {
        return 0;
    }

    if (s_prBytesWritten + total > (size_t)PR_MAX_DOWNLOAD_BYTES) {
        s_prSizeExceeded = qtrue;
        s_prWriteFailed  = qtrue;
        return 0;
    }

    wrote = FS_Write(ptr, total, *fh);
    if (wrote != total) {
        s_prWriteFailed = qtrue;
        return 0;
    }

    s_prBytesWritten += wrote;
    return wrote;
}

static int CL_PR_ProgressCB(void *userp, double dltotal, double dlnow,
                            double ultotal, double ulnow)
{
    const char *name = (const char *)userp;
    int         pct;

    (void)ultotal;
    (void)ulnow;

    if (dltotal > (double)PR_MAX_DOWNLOAD_BYTES ||
        dlnow > (double)PR_MAX_DOWNLOAD_BYTES) {
        s_prSizeExceeded = qtrue;
        return 1;
    }

    Cvar_SetValue("cl_downloadSize",  (float)dltotal);
    Cvar_SetValue("cl_downloadCount", (float)dlnow);

    if (cls.realtime - s_prSpeedSampleTime >= 200) {
        double dt = (cls.realtime - s_prSpeedSampleTime) / 1000.0;

        if (dt > 0.0) {
            double instant = (dlnow - s_prSpeedSampleBytes) / dt;

            if (instant < 0.0) {
                instant = 0.0;
            }

            if (s_prDisplaySpeed <= 0.0f) {
                s_prDisplaySpeed = (float)instant;
            } else {
                s_prDisplaySpeed = (float)(s_prDisplaySpeed * 0.55 + instant * 0.45);
            }

            Cvar_SetValue("cl_downloadSpeed", s_prDisplaySpeed);
        }

        s_prSpeedSampleBytes = dlnow;
        s_prSpeedSampleTime  = cls.realtime;
    }

    if (dltotal > 0.0) {
        pct = (int)((dlnow * 100.0) / dltotal);
        if (pct > 100) {
            pct = 100;
        }

        /* Print only when percentage changes to avoid log spam. */
        if (pct != s_prLastProgressPct) {
            s_prLastProgressPct = pct;

            if ((pct % 10) == 0 || pct == 100) {
                CL_PR_PrintProgressBar(name, pct);
            }
        }
    }

    return 0;
}

static void CL_PR_CleanupActiveDownload(void)
{
    if (s_prFile) {
        FS_FCloseFile(s_prFile);
        s_prFile = 0;
    }

    if (s_prCurlM && s_prCurl) {
        curlImport.qcurl_multi_remove_handle(s_prCurlM, s_prCurl);
    }

    if (s_prCurl) {
        curlImport.qcurl_easy_cleanup(s_prCurl);
        s_prCurl = NULL;
    }

    if (s_prCurlM) {
        curlImport.qcurl_multi_cleanup(s_prCurlM);
        s_prCurlM = NULL;
    }

    s_prDownloadActive = qfalse;
}

static qboolean CL_PR_StartNextDownload(void)
{
    const prDownloadEntry_t *entry;

    if (s_prQueueIndex >= s_prQueueCount) {
        return qfalse;
    }

    entry = &s_prQueue[s_prQueueIndex];
    Q_strncpyz(s_prCurrentLocalName, entry->localName, sizeof(s_prCurrentLocalName));
    Q_strncpyz(s_prCurrentMD5, entry->md5Expected, sizeof(s_prCurrentMD5));
    Com_sprintf(s_prTempName, sizeof(s_prTempName), "%s.tmp", entry->localName);

    s_prFile = FS_BaseDir_FOpenFileWrite_HomeData(s_prTempName);
    if (!s_prFile) {
        Com_Printf("pr_downloads: could not open %s for writing\n", s_prTempName);
        return qfalse;
    }

    s_prCurl = curlImport.qcurl_easy_init();
    if (!s_prCurl) {
        FS_FCloseFile(s_prFile);
        s_prFile = 0;
        return qfalse;
    }

    s_prCurlM = curlImport.qcurl_multi_init();
    if (!s_prCurlM) {
        curlImport.qcurl_easy_cleanup(s_prCurl);
        s_prCurl = NULL;
        FS_FCloseFile(s_prFile);
        s_prFile = 0;
        return qfalse;
    }

    Cvar_Set("cl_downloadName", entry->localName);
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadSize", "0");
    Cvar_Set("cl_downloadCount", "0");
    Cvar_SetValue("cl_downloadCurrent", (float)(s_prQueueIndex + 1));
    Cvar_SetValue("cl_downloadTime", cls.realtime);
    s_prLastProgressPct = -1;
    s_prWriteFailed     = qfalse;
    s_prSizeExceeded    = qfalse;
    s_prBytesWritten    = 0;
    s_prRetryPending    = qfalse;
    CL_PR_ResetSpeed();

    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_URL,              entry->url);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_WRITEFUNCTION,    CL_PR_FileWriteCB);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_WRITEDATA,        &s_prFile);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_NOPROGRESS,       0L);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_PROGRESSFUNCTION, CL_PR_ProgressCB);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_PROGRESSDATA,     (void *)entry->localName);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_FOLLOWLOCATION,   1L);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_MAXREDIRS,        5L);
    CL_PR_ApplyEasyOpts(s_prCurl, PR_MAX_DOWNLOAD_BYTES);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_FAILONERROR,      1L);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_LOW_SPEED_LIMIT,  PR_DOWNLOAD_LOW_SPEED_LIMIT);
    curlImport.qcurl_easy_setopt(s_prCurl, CURLOPT_LOW_SPEED_TIME,   PR_DOWNLOAD_LOW_SPEED_TIME);

    if (curlImport.qcurl_multi_add_handle(s_prCurlM, s_prCurl) != CURLM_OK) {
        CL_PR_CleanupActiveDownload();
        return qfalse;
    }

    s_prDownloadActive = qtrue;
    Com_Printf("pr_downloads: downloading %s...\n", entry->localName);
    return qtrue;
}

/*
 * CL_PR_FetchText
 * Synchronously fetches a small text file into buffer.
 * Returns qtrue on success.
 */
static qboolean CL_PR_FetchText(const char *url, char *buffer, int bufferSize)
{
    CURL         *curl;
    CURLcode      res;
    prFetchBuf_t  buf;

    if (!Com_IsCurlImportValid(&curlImport)) {
        return qfalse;
    }

    curl = curlImport.qcurl_easy_init();
    if (!curl) {
        return qfalse;
    }

    buf.data     = buffer;
    buf.size     = 0;
    buf.capacity = (size_t)(bufferSize - 1);
    buffer[0]    = '\0';

    curlImport.qcurl_easy_setopt(curl, CURLOPT_URL,           url);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CL_PR_TextWriteCB);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_MAXREDIRS,     5L);
    CL_PR_ApplyEasyOpts(curl, (long)buf.capacity);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_TIMEOUT,       30L);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_FAILONERROR,   1L);

    res = curlImport.qcurl_easy_perform(curl);
    curlImport.qcurl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        Com_Printf("pr_downloads: failed to fetch file list: %s\n",
                   curlImport.qcurl_easy_strerror(res));
        return qfalse;
    }
    return qtrue;
}

/*
 * CL_PR_GetFieldValue
 * Finds  key "value"  inside [blockStart, blockEnd) and copies the
 * quoted value into out[outSize].  Returns qtrue if found.
 */
static qboolean CL_PR_GetFieldValue(const char *blockStart, const char *blockEnd,
                                    const char *key, char *out, int outSize)
{
    size_t     keyLen = strlen(key);
    const char *p     = blockStart;

    out[0] = '\0';
    while (p < blockEnd) {
        /* skip whitespace */
        while (p < blockEnd && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            p++;

        if ((size_t)(blockEnd - p) >= keyLen &&
            strncmp(p, key, keyLen) == 0 &&
            (p[keyLen] == ' ' || p[keyLen] == '\t' || p[keyLen] == '"'))
        {
            const char *v, *e;
            p += keyLen;
            while (p < blockEnd && (*p == ' ' || *p == '\t')) p++;
            if (*p != '"') break;
            v = p + 1;
            e = v;
            while (e < blockEnd && *e != '"') e++;
            if (e < blockEnd) {
                int len = (int)(e - v);
                if (len >= outSize) len = outSize - 1;
                Q_strncpyz(out, v, len + 1);
                return qtrue;
            }
            break;
        }
        /* advance to next line */
        while (p < blockEnd && *p != '\n') p++;
        if (p < blockEnd) p++;
    }
    return qfalse;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/*
=================
CL_InitPrDownloads

Fetch the server's pr_downloads filelist, check MD5s of installed pk3s,
queue any missing/outdated files, then start asynchronous curl_multi
downloads so UI can update each frame.
Called from CL_InitDownloads regardless of cl_allowDownload / DLF_ENABLE.
pr_downloads is not gated by that unused/broken stock cvar.
=================
*/
qboolean CL_InitPrDownloads(void)
{
    static char  filelistBuf[65536];
    const char  *p;
    const char  *blockEnd;
    char         md5Expected[64];
    char         url[1024];
    char         localName[MAX_OSPATH];
    int          queued     = 0;
    int          upToDate   = 0;

    CL_PR_CleanupActiveDownload();
    s_prQueueCount      = 0;
    s_prQueueIndex      = 0;
    s_prHadDownloads    = qfalse;
    s_prRetryPending    = qfalse;
    s_prRetryAt         = 0;
    s_prFailed          = qfalse;
    s_prSizeExceeded    = qfalse;
    s_prBytesWritten    = 0;
    if (!s_prFilelistRetry) {
        s_prRetryCount = 0;
    }
    s_prFilelistRetry = qfalse;
    s_prCurrentLocalName[0] = '\0';
    s_prCurrentMD5[0]       = '\0';
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadCurrent", "0");
    Cvar_Set("cl_downloadTotal", "0");
    CL_PR_ResetSpeed();

    if (!clc.sv_prDownloadsURL[0]) {
        Com_Printf("pr_downloads: server did not send a download URL, skipping\n");
        return qfalse;
    }

    /* Reject non-HTTP(S) URLs to prevent SSRF */
    if (!CL_PR_IsHttpUrl(clc.sv_prDownloadsURL)) {
        Com_Printf("pr_downloads: ignoring non-HTTP URL: %s\n",
                   clc.sv_prDownloadsURL);
        return qfalse;
    }

    if (!Com_IsCurlImportValid(&curlImport)) {
        Com_Printf("pr_downloads: cURL not available, skipping auto-download\n");
        return qfalse;
    }

    CL_PR_SetPendingStatus(qtrue);

    /* Strict pre-join gate: while pr_downloads is active, stay in connected
     * state until verification and required downloads are fully complete. */
    clc.state = CA_CONNECTED;

    Com_Printf("pr_downloads: fetching file list from %s\n", clc.sv_prDownloadsURL);

    if (!CL_PR_FetchText(clc.sv_prDownloadsURL, filelistBuf, sizeof(filelistBuf))) {
        Cvar_Set("cl_downloadError", "Download error: failed to fetch file list");
        s_prFilelistRetry = qtrue;
        CL_PR_ScheduleRetry("failed to fetch file list");
        return qtrue;
    }

    /* Parse   map { ... }  blocks */
    p = filelistBuf;
    while ((p = strstr(p, "map {")) != NULL) {
        p       += 5;
        blockEnd = strchr(p, '}');
        if (!blockEnd) break;

        md5Expected[0] = '\0';
        url[0]         = '\0';

        CL_PR_GetFieldValue(p, blockEnd, "md5", md5Expected, (int)sizeof(md5Expected));
        CL_PR_GetFieldValue(p, blockEnd, "url", url,         (int)sizeof(url));

        p = blockEnd + 1;

        if (!url[0]) continue;

        if (!CL_PR_IsHttpUrl(url)) {
            Com_Printf("pr_downloads: ignoring non-HTTP file URL: %s\n", url);
            continue;
        }

        if (!CL_PR_IsValidMd5(md5Expected)) {
            Com_Printf("pr_downloads: skipping entry with missing/invalid MD5: %s\n", url);
            continue;
        }

        if (!CL_PR_MakeLocalPakName(url, localName, (int)sizeof(localName))) {
            Com_Printf("pr_downloads: refusing unsafe pak name from URL: %s\n", url);
            continue;
        }

        /* Check whether we already have this file with the right MD5 */
        {
            const char *localMD5 = Com_MD5File(localName, 0, "", 0);
            if (localMD5[0] && Q_stricmp(localMD5, md5Expected) == 0) {
                Com_DPrintf("pr_downloads: %s is up to date\n", localName);
                upToDate++;
                continue;
            }
        }

        if (s_prQueueCount >= PR_MAX_DOWNLOADS) {
            Com_Printf("pr_downloads: queue full (%d), refusing remaining required pak: %s\n",
                       PR_MAX_DOWNLOADS, localName);
            CL_PR_GiveUp("too many required paks (queue full)");
            return qtrue;
        }

        Q_strncpyz(s_prQueue[s_prQueueCount].localName, localName,
                   sizeof(s_prQueue[s_prQueueCount].localName));
        Q_strncpyz(s_prQueue[s_prQueueCount].url, url,
                   sizeof(s_prQueue[s_prQueueCount].url));
        Q_strncpyz(s_prQueue[s_prQueueCount].md5Expected, md5Expected,
                   sizeof(s_prQueue[s_prQueueCount].md5Expected));

        s_prQueueCount++;
        queued++;
    }

    Com_Printf("pr_downloads: %d up to date, %d queued\n", upToDate, queued);
    Cvar_SetValue("cl_downloadTotal", (float)s_prQueueCount);

    if (!s_prQueueCount) {
        CL_PR_SetPendingStatus(qfalse);
        if (CL_TryFastDLMapFallback()) {
            return qtrue;
        }
        CL_DownloadsComplete();
        return qtrue;
    }

    /* Downloads are required: cancel the loading screen that UI_BeginLoad
     * already opened so the connect/progress screen stays visible. */
    UI_AbortLoad();

    if (!CL_PR_StartNextDownload()) {
        Com_Printf("pr_downloads: failed to start first download\n");
        CL_PR_ScheduleRetry("start failed");
    }

    return qtrue;
}

/*
=================
CL_PrDownloadsPending
=================
*/
qboolean CL_PrDownloadsPending(void)
{
    if (s_prDownloadActive || s_prRetryPending ||
        (s_prQueueCount > 0 && s_prQueueIndex < s_prQueueCount)) {
        return qtrue;
    }
    return qfalse;
}

/*
=================
CL_PrDownloadsFrame
=================
*/
void CL_PrDownloadsFrame(void)
{
    CURLMcode res;
    CURLMsg  *msg;
    CURLMsg  *doneMsg;
    int       running;
    int       msgCount;

    /* Keep the UI on the pre-join connect/loading screen while required
     * pr_downloads work is still in progress. After giving up, do not
     * refresh connectStartTime — that used to prevent disconnect forever. */
    if (s_prFailed) {
        CL_PR_SetPendingStatus(qfalse);
        return;
    }

    if (CL_PrDownloadsPending()) {
        CL_PR_SetPendingStatus(qtrue);
        clc.state = CA_CONNECTED;
        clc.connectStartTime = cls.realtime;
    } else {
        CL_PR_SetPendingStatus(qfalse);
    }

    if (!s_prDownloadActive) {
        if (s_prRetryPending && cls.realtime >= s_prRetryAt) {
            s_prRetryPending = qfalse;
            if (s_prQueueCount > 0 && s_prQueueIndex < s_prQueueCount) {
                if (!CL_PR_StartNextDownload()) {
                    CL_PR_ScheduleRetry("start failed");
                }
            } else {
                CL_InitPrDownloads();
            }
        }
        return;
    }

    if (!s_prCurlM) {
        return;
    }

    do {
        res = curlImport.qcurl_multi_perform(s_prCurlM, &running);
    } while (res == CURLM_CALL_MULTI_PERFORM);

    if (res != CURLM_OK) {
        const char *reason = curlImport.qcurl_multi_strerror(res);
        Com_Printf("pr_downloads: multi_perform failed: %s\n", reason);
        CL_PR_CleanupActiveDownload();
        CL_PR_DeleteTemp();
        CL_PR_ScheduleRetry(reason);
        return;
    }

    doneMsg = NULL;
    while ((msg = curlImport.qcurl_multi_info_read(s_prCurlM, &msgCount)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            doneMsg = msg;
            break;
        }
    }

    if (!doneMsg) {
        return;
    }

    /* On Windows, rename fails while the destination/source file is still open. */
    if (s_prFile) {
        FS_FCloseFile(s_prFile);
        s_prFile = 0;
    }

    if (s_prSizeExceeded || doneMsg->data.result == CURLE_FILESIZE_EXCEEDED) {
        Com_Printf("pr_downloads: %s exceeds 400 MiB cap, aborting\n",
                   s_prCurrentLocalName);
        CL_PR_DeleteTemp();
        CL_PR_CleanupActiveDownload();
        CL_PR_GiveUp("file exceeds 400 MiB download cap");
        return;
    }

    if (doneMsg->data.result == CURLE_OK) {
        const char *gotMD5;

        if (!CL_PR_IsValidMd5(s_prCurrentMD5)) {
            Com_Printf("pr_downloads: missing expected MD5 for %s, discarding\n",
                       s_prCurrentLocalName);
            CL_PR_DeleteTemp();
            CL_PR_CleanupActiveDownload();
            CL_PR_ScheduleRetry("missing md5");
            return;
        }

        gotMD5 = Com_MD5File(s_prTempName, 0, "", 0);
        if (!gotMD5[0] || Q_stricmp(gotMD5, s_prCurrentMD5) != 0) {
            Com_Printf("pr_downloads: MD5 mismatch for %s (got %s, expected %s)\n",
                       s_prCurrentLocalName,
                       gotMD5[0] ? gotMD5 : "(unreadable)",
                       s_prCurrentMD5);
            CL_PR_DeleteTemp();
            CL_PR_CleanupActiveDownload();
            CL_PR_ScheduleRetry("md5 mismatch");
            return;
        }

        if (!FS_BaseDir_Replace_HomeData(s_prTempName, s_prCurrentLocalName)) {
            CL_PR_CleanupActiveDownload();
            CL_PR_DeleteTemp();
            CL_PR_ScheduleRetry("failed to install downloaded pak");
            return;
        }
        s_prHadDownloads = qtrue;
        s_prRetryCount   = 0;
        Com_Printf("pr_downloads: %s downloaded successfully\n", s_prCurrentLocalName);
    } else {
        const char *errorReason = curlImport.qcurl_easy_strerror(doneMsg->data.result);

        Com_Printf("pr_downloads: download of %s failed: %s\n",
                   s_prCurrentLocalName,
                   errorReason);

        if (s_prWriteFailed) {
            Com_Printf("pr_downloads: local write failed for %s (disk full or file write error)\n",
                       s_prTempName);
            errorReason = "disk full or local write error";
        }

        if (doneMsg->data.result == CURLE_OPERATION_TIMEDOUT) {
            Com_Printf("pr_downloads: transfer stalled for at least %ld seconds below %ld byte/sec\n",
                       PR_DOWNLOAD_LOW_SPEED_TIME,
                       PR_DOWNLOAD_LOW_SPEED_LIMIT);
            errorReason = "transfer stalled / timed out";
        }

        CL_PR_SetUserErrorStatus(errorReason);
        CL_PR_DeleteTemp();
        CL_PR_CleanupActiveDownload();

        /* Required files are not skipped. Retry the same item, then
         * disconnect after PR_MAX_RETRIES. */
        CL_PR_ScheduleRetry(errorReason);
        return;
    }

    CL_PR_CleanupActiveDownload();
    s_prQueueIndex++;

    if (s_prQueueIndex < s_prQueueCount) {
        if (!CL_PR_StartNextDownload()) {
            CL_PR_ScheduleRetry("start failed");
        }
        return;
    }

    if (s_prHadDownloads) {
        clc.downloadRestart = qtrue;
    }

    Cvar_Set("cl_downloadName", "");
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadCurrent", "0");
    Cvar_Set("cl_downloadTotal", "0");
    CL_PR_ResetSpeed();
    CL_PR_SetPendingStatus(qfalse);
    if (s_prHadDownloads) {
        /* This restarts the search paths and requests a new gamestate. Only
         * that gamestate can reliably decide whether an installed pak now
         * supplies the requested map. */
        CL_DownloadsComplete();
        return;
    }
    if (CL_TryFastDLMapFallback()) {
        return;
    }
    CL_DownloadsComplete();
}

void CL_ShutdownPrDownloads(void)
{
    CL_PR_CleanupActiveDownload();
    CL_PR_DeleteTemp();
    s_prTempName[0]     = '\0';
    s_prQueueCount      = 0;
    s_prQueueIndex      = 0;
    s_prHadDownloads    = qfalse;
    s_prRetryPending    = qfalse;
    s_prRetryAt         = 0;
    s_prRetryCount      = 0;
    s_prFilelistRetry   = qfalse;
    s_prFailed          = qfalse;
    s_prSizeExceeded    = qfalse;
    s_prBytesWritten    = 0;
    s_prCurrentLocalName[0] = '\0';
    s_prCurrentMD5[0]       = '\0';
    Cvar_Set("cl_downloadError", "");
    Cvar_Set("cl_downloadCurrent", "0");
    Cvar_Set("cl_downloadTotal", "0");
    CL_PR_ResetSpeed();
    CL_PR_SetPendingStatus(qfalse);
}

#else

qboolean CL_InitPrDownloads(void) { return qfalse; }
qboolean CL_PrDownloadsPending(void) { return qfalse; }
void CL_PrDownloadsFrame(void) {}
void CL_ShutdownPrDownloads(void) {}

#endif /* HAS_LIBCURL */
