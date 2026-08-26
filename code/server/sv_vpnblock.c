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
 * Server-side VPN/proxy blocker.
 *
 * Players are checked on getchallenge and again on connect. LAN, loopback,
 * bots, and whitelisted IPs skip the check. Known VPN CIDRs are matched
 * locally; unknown IPs are looked up through a configurable HTTP API.
 */

#include "server.h"
#include "../sys/sys_curl.h"

#define VPN_MAX_RANGES     65536
#define VPN_MAX_CACHE      512
#define VPN_FETCH_BUF      8192
#define VPN_DEFAULT_MSG    "VPN/proxy connections are not allowed on this server."
#define VPN_DEFAULT_API    "http://proxycheck.io/v2/$ip?vpn=1&asn=1"
#define VPN_DEFAULT_LIST   "vpn_ranges.txt"
#define VPN_DEFAULT_LISTURL \
    "https://raw.githubusercontent.com/X4BNet/lists_vpn/main/output/vpn/ipv4.txt"

typedef struct {
    netadr_t ip;
    int      subnet;
} vpnRange_t;

typedef struct {
    char     ip[64];
    qboolean blocked;
    int      expireAt;
} vpnCache_t;

typedef struct {
    char  *data;
    size_t size;
    size_t capacity;
} vpnBuf_t;

static cvar_t *sv_vpnBlock;
static cvar_t *sv_vpnBlockHosting;
static cvar_t *sv_vpnFailOpen;
static cvar_t *sv_vpnApi;
static cvar_t *sv_vpnApiKey;
static cvar_t *sv_vpnList;
static cvar_t *sv_vpnListUrl;
static cvar_t *sv_vpnWhitelist;
static cvar_t *sv_vpnMessage;
static cvar_t *sv_vpnCacheHours;
static cvar_t *sv_vpnTimeout;

static vpnRange_t *s_vpnRanges;
static int         s_vpnRangeCount;
static int         s_vpnRangeCapacity;
static vpnCache_t  s_vpnCache[VPN_MAX_CACHE];
static int         s_vpnCacheCount;
static qboolean    s_vpnListLoaded;

#ifdef HAS_LIBCURL
static size_t SV_VpnWriteCB(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    vpnBuf_t *buf = (vpnBuf_t *)userdata;
    size_t    n   = size * nmemb;
    size_t    need;

    if (!buf || !n) {
        return 0;
    }

    need = buf->size + n + 1;
    if (need > buf->capacity) {
        size_t  cap = buf->capacity ? buf->capacity : 4096;
        char   *p;

        while (cap < need) {
            cap *= 2;
        }
        p = (char *)Z_Malloc(cap);
        if (buf->data && buf->size) {
            Com_Memcpy(p, buf->data, buf->size);
        }
        if (buf->data) {
            Z_Free(buf->data);
        }
        buf->data     = p;
        buf->capacity = cap;
    }

    Com_Memcpy(buf->data + buf->size, ptr, n);
    buf->size += n;
    buf->data[buf->size] = '\0';
    return n;
}

static qboolean SV_VpnHttpGet(const char *url, char *out, int outSize)
{
    CURL    *curl;
    CURLcode res;
    vpnBuf_t buf;
    long     timeout;

    if (!out || outSize < 2) {
        return qfalse;
    }
    out[0] = '\0';

    if (!Com_IsCurlImportValid(&curlImport) || !curlImport.qcurl_easy_init) {
        return qfalse;
    }

    Com_Memset(&buf, 0, sizeof(buf));
    curl = curlImport.qcurl_easy_init();
    if (!curl) {
        return qfalse;
    }

    timeout = sv_vpnTimeout && sv_vpnTimeout->integer > 0 ? sv_vpnTimeout->integer : 3;

    curlImport.qcurl_easy_setopt(curl, CURLOPT_URL, url);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SV_VpnWriteCB);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_USERAGENT, "OpenMoHAA-VPNBlock/1.0");
    curlImport.qcurl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    res = curlImport.qcurl_easy_perform(curl);
    curlImport.qcurl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data) {
        if (buf.data) {
            Z_Free(buf.data);
        }
        return qfalse;
    }

    Q_strncpyz(out, buf.data, outSize);
    Z_Free(buf.data);

    if (!strchr(out, '{') || Q_stristr(out, "<html") || Q_stristr(out, "Just a moment")) {
        return qfalse;
    }

    return qtrue;
}
#endif

static const char *SV_VpnJsonValue(const char *json, const char *key)
{
    char        quoted[64];
    const char *p;

    if (!json || !key) {
        return NULL;
    }

    Com_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
    p = json;
    while ((p = strstr(p, quoted)) != NULL) {
        p += strlen(quoted);
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == ':') {
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                p++;
            }
            return p;
        }
        p++;
    }
    return NULL;
}

static qboolean SV_VpnValueMeansYes(const char *value)
{
    if (!value || !*value) {
        return qfalse;
    }
    if (!Q_stricmpn(value, "true", 4) || !Q_stricmpn(value, "yes", 3)) {
        return qtrue;
    }
    if (value[0] == '1' && (value[1] == '\0' || value[1] == ',' || value[1] == '}' ||
                            value[1] == ' ' || value[1] == '\n')) {
        return qtrue;
    }
    if (value[0] == '"') {
        value++;
        if (!Q_stricmpn(value, "yes", 3) || !Q_stricmpn(value, "true", 4) ||
            !Q_stricmpn(value, "vpn", 3) || !Q_stricmpn(value, "proxy", 5) ||
            !Q_stricmpn(value, "tor", 3) || !Q_stricmpn(value, "socks", 5) ||
            !Q_stricmpn(value, "pub", 3) || !Q_stricmpn(value, "web", 3) ||
            !Q_stricmpn(value, "http", 4) || !Q_stricmpn(value, "comp", 4) ||
            !Q_stricmpn(value, "hosting", 7) || !Q_stricmpn(value, "datacenter", 10) ||
            !Q_stricmpn(value, "relay", 5)) {
            return qtrue;
        }
    }
    return qfalse;
}

static qboolean SV_VpnJsonSaysBlocked(const char *json)
{
    const char *v;
    qboolean    hosting;

    if (!json || !json[0]) {
        return qfalse;
    }

    v = SV_VpnJsonValue(json, "vpn");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "proxy");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "tor");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "relay");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "is_vpn");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "is_proxy");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "is_tor");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }
    v = SV_VpnJsonValue(json, "type");
    if (SV_VpnValueMeansYes(v)) {
        return qtrue;
    }

    hosting = sv_vpnBlockHosting && sv_vpnBlockHosting->integer;
    if (hosting) {
        v = SV_VpnJsonValue(json, "hosting");
        if (SV_VpnValueMeansYes(v)) {
            return qtrue;
        }
        v = SV_VpnJsonValue(json, "is_datacenter");
        if (SV_VpnValueMeansYes(v)) {
            return qtrue;
        }
        v = SV_VpnJsonValue(json, "is_hosting");
        if (SV_VpnValueMeansYes(v)) {
            return qtrue;
        }
    }

    return qfalse;
}

static qboolean SV_VpnParseCidr(const char *line, vpnRange_t *out)
{
    char  buf[128];
    char *slash;
    int   mask;
    int   i;

    if (!line || !out) {
        return qfalse;
    }

    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (!*line || *line == '#' || *line == '/' || *line == ';') {
        return qfalse;
    }

    Q_strncpyz(buf, line, sizeof(buf));
    for (i = 0; buf[i]; i++) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ' || buf[i] == '\t' || buf[i] == '#') {
            buf[i] = '\0';
            break;
        }
    }
    if (!buf[0] || strchr(buf, ':')) {
        return qfalse;
    }

    mask  = 32;
    slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        mask   = atoi(slash + 1);
        if (mask < 1 || mask > 32) {
            return qfalse;
        }
    }

    Com_Memset(out, 0, sizeof(*out));
    if (!NET_StringToAdr(buf, &out->ip, NA_IP)) {
        return qfalse;
    }
    out->subnet = mask;
    return qtrue;
}

static void SV_VpnAddRange(const vpnRange_t *range)
{
    if (!range) {
        return;
    }
    if (s_vpnRangeCount >= VPN_MAX_RANGES) {
        return;
    }
    if (s_vpnRangeCount >= s_vpnRangeCapacity) {
        int         cap = s_vpnRangeCapacity ? s_vpnRangeCapacity * 2 : 1024;
        vpnRange_t *p   = (vpnRange_t *)Z_Malloc(sizeof(vpnRange_t) * cap);

        if (s_vpnRanges && s_vpnRangeCount) {
            Com_Memcpy(p, s_vpnRanges, sizeof(vpnRange_t) * s_vpnRangeCount);
        }
        if (s_vpnRanges) {
            Z_Free(s_vpnRanges);
        }
        s_vpnRanges        = p;
        s_vpnRangeCapacity = cap;
    }
    s_vpnRanges[s_vpnRangeCount++] = *range;
}

static int SV_VpnParseRangeText(const char *text)
{
    const char *p;
    const char *end;
    char        line[128];
    vpnRange_t  range;
    int         added = 0;
    int         n;

    if (!text) {
        return 0;
    }

    p = text;
    while (*p) {
        end = p;
        while (*end && *end != '\n') {
            end++;
        }
        n = (int)(end - p);
        if (n >= (int)sizeof(line)) {
            n = (int)sizeof(line) - 1;
        }
        Com_Memcpy(line, p, n);
        line[n] = '\0';
        if (SV_VpnParseCidr(line, &range)) {
            SV_VpnAddRange(&range);
            added++;
        }
        p = *end ? end + 1 : end;
    }
    return added;
}

static void SV_VpnLoadList(void)
{
    const char *name;
    char       *buf;
    int         added;

    s_vpnListLoaded = qtrue;
    name            = (sv_vpnList && sv_vpnList->string[0]) ? sv_vpnList->string : VPN_DEFAULT_LIST;

    if (FS_ReadFile(name, (void **)&buf) <= 0) {
        return;
    }
    added = SV_VpnParseRangeText(buf);
    FS_FreeFile(buf);
    Com_Printf("vpnblock: loaded %d ranges from %s (%d total)\n", added, name, s_vpnRangeCount);
}

static qboolean SV_VpnInRanges(netadr_t from)
{
    int i;

    if (!s_vpnListLoaded) {
        SV_VpnLoadList();
    }
    for (i = 0; i < s_vpnRangeCount; i++) {
        if (NET_CompareBaseAdrMask(s_vpnRanges[i].ip, from, s_vpnRanges[i].subnet)) {
            return qtrue;
        }
    }
    return qfalse;
}

static qboolean SV_VpnIsWhitelisted(const char *ip)
{
    const char *list;
    char        token[64];
    int         i, n;

    if (!ip || !ip[0] || !sv_vpnWhitelist || !sv_vpnWhitelist->string[0]) {
        return qfalse;
    }

    list = sv_vpnWhitelist->string;
    while (*list) {
        while (*list == ' ' || *list == '\t' || *list == ',' || *list == ';') {
            list++;
        }
        if (!*list) {
            break;
        }
        n = 0;
        while (list[n] && list[n] != ' ' && list[n] != '\t' && list[n] != ',' && list[n] != ';') {
            n++;
        }
        if (n >= (int)sizeof(token)) {
            n = (int)sizeof(token) - 1;
        }
        for (i = 0; i < n; i++) {
            token[i] = list[i];
        }
        token[n] = '\0';
        if (!Q_stricmp(token, ip)) {
            return qtrue;
        }
        {
            vpnRange_t range;
            netadr_t   adr;

            if (SV_VpnParseCidr(token, &range) && NET_StringToAdr(ip, &adr, NA_UNSPEC) &&
                NET_CompareBaseAdrMask(range.ip, adr, range.subnet)) {
                return qtrue;
            }
        }
        list += n;
    }
    return qfalse;
}

static vpnCache_t *SV_VpnCacheFind(const char *ip)
{
    int i;
    int now = Sys_Milliseconds();

    for (i = 0; i < s_vpnCacheCount; i++) {
        if (!Q_stricmp(s_vpnCache[i].ip, ip)) {
            if (s_vpnCache[i].expireAt && now > s_vpnCache[i].expireAt) {
                continue;
            }
            return &s_vpnCache[i];
        }
    }
    return NULL;
}

static void SV_VpnCacheStore(const char *ip, qboolean blocked)
{
    vpnCache_t *slot;
    int         hours;
    int         i;
    int         oldest;
    int         oldestExpire;

    if (!ip || !ip[0]) {
        return;
    }

    slot = SV_VpnCacheFind(ip);
    if (!slot) {
        if (s_vpnCacheCount < VPN_MAX_CACHE) {
            slot = &s_vpnCache[s_vpnCacheCount++];
        } else {
            oldest       = 0;
            oldestExpire = s_vpnCache[0].expireAt;
            for (i = 1; i < VPN_MAX_CACHE; i++) {
                if (s_vpnCache[i].expireAt < oldestExpire) {
                    oldest       = i;
                    oldestExpire = s_vpnCache[i].expireAt;
                }
            }
            slot = &s_vpnCache[oldest];
        }
        Q_strncpyz(slot->ip, ip, sizeof(slot->ip));
    }

    hours = sv_vpnCacheHours && sv_vpnCacheHours->integer > 0 ? sv_vpnCacheHours->integer : 24;
    if (hours > 168) {
        hours = 168;
    }
    slot->blocked  = blocked;
    slot->expireAt = Sys_Milliseconds() + hours * 3600 * 1000;
}

static qboolean SV_VpnLookupApi(const char *ip, qboolean *blockedOut)
{
#ifdef HAS_LIBCURL
    char        url[MAX_OSPATH];
    char        json[VPN_FETCH_BUF];
    const char *tmpl;
    const char *src;
    char       *dst;
    char       *end;

    *blockedOut = qfalse;
    tmpl        = (sv_vpnApi && sv_vpnApi->string[0]) ? sv_vpnApi->string : VPN_DEFAULT_API;
    src         = tmpl;
    dst         = url;
    end         = url + sizeof(url) - 1;

    while (*src && dst < end) {
        if (!Q_stricmpn(src, "$ip", 3)) {
            Q_strncpyz(dst, ip, (int)(end - dst + 1));
            dst += strlen(dst);
            src += 3;
        } else if (!Q_stricmpn(src, "{ip}", 4)) {
            Q_strncpyz(dst, ip, (int)(end - dst + 1));
            dst += strlen(dst);
            src += 4;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    if (sv_vpnApiKey && sv_vpnApiKey->string[0] && !strstr(url, "key=")) {
        Q_strcat(url, sizeof(url), strchr(url, '?') ? "&key=" : "?key=");
        Q_strcat(url, sizeof(url), sv_vpnApiKey->string);
    }

    if (!SV_VpnHttpGet(url, json, sizeof(json))) {
        Com_Printf("vpnblock: API lookup failed for %s\n", ip);
        return qfalse;
    }

    *blockedOut = SV_VpnJsonSaysBlocked(json);
    return qtrue;
#else
    *blockedOut = qfalse;
    return qfalse;
#endif
}

static const char *SV_VpnDenyMessage(void)
{
    if (sv_vpnMessage && sv_vpnMessage->string[0]) {
        return sv_vpnMessage->string;
    }
    return VPN_DEFAULT_MSG;
}

qboolean SV_VpnBlockShouldRefuse(netadr_t from, char *reason, int reasonSize)
{
    const char  *ip;
    vpnCache_t  *cached;
    qboolean     blocked;
    qboolean     lookedUp;

    if (reason && reasonSize > 0) {
        reason[0] = '\0';
    }

    if (!sv_vpnBlock || !sv_vpnBlock->integer) {
        return qfalse;
    }
    if (from.type == NA_LOOPBACK || from.type == NA_BOT || NET_IsLocalAddress(from)) {
        Com_Printf("vpnblock: skip local/loopback client\n");
        return qfalse;
    }

    ip = NET_AdrToString(from);
    if (!ip || !ip[0] || !Q_stricmp(ip, "loopback") || !Q_stricmp(ip, "bot")) {
        return qfalse;
    }
    if (Sys_IsLANAddress(from) || (from.type == NA_IP && (from.ip[0] == 10 || from.ip[0] == 127 ||
        (from.ip[0] == 192 && from.ip[1] == 168) || (from.ip[0] == 172 && (from.ip[1] & 0xf0) == 16)))) {
        Com_Printf("vpnblock: skip private/LAN IP %s (enable a VPN on another network to test)\n", ip);
        return qfalse;
    }
    if (SV_VpnIsWhitelisted(ip)) {
        return qfalse;
    }

    cached = SV_VpnCacheFind(ip);
    if (cached) {
        if (cached->blocked) {
            if (reason && reasonSize > 0) {
                Q_strncpyz(reason, SV_VpnDenyMessage(), reasonSize);
            }
            Com_Printf("vpnblock: refused %s (cached)\n", ip);
            return qtrue;
        }
        return qfalse;
    }

    if (SV_VpnInRanges(from)) {
        SV_VpnCacheStore(ip, qtrue);
        if (reason && reasonSize > 0) {
            Q_strncpyz(reason, SV_VpnDenyMessage(), reasonSize);
        }
        Com_Printf("vpnblock: refused %s (vpn range)\n", ip);
        return qtrue;
    }

    lookedUp = SV_VpnLookupApi(ip, &blocked);
    if (!lookedUp) {
        if (sv_vpnFailOpen && !sv_vpnFailOpen->integer) {
            if (reason && reasonSize > 0) {
                Q_strncpyz(reason, SV_VpnDenyMessage(), reasonSize);
            }
            Com_Printf("vpnblock: refused %s (lookup failed, fail-closed)\n", ip);
            return qtrue;
        }
        Com_Printf("vpnblock: allowing %s (lookup failed, fail-open)\n", ip);
        return qfalse;
    }

    SV_VpnCacheStore(ip, blocked);
    if (blocked) {
        if (reason && reasonSize > 0) {
            Q_strncpyz(reason, SV_VpnDenyMessage(), reasonSize);
        }
        Com_Printf("vpnblock: refused %s (vpn/proxy detected)\n", ip);
        return qtrue;
    }

    Com_Printf("vpnblock: allowed %s (not a vpn/proxy)\n", ip);
    return qfalse;
}

static void SV_VpnReload_f(void)
{
    if (s_vpnRanges) {
        Z_Free(s_vpnRanges);
        s_vpnRanges        = NULL;
        s_vpnRangeCount    = 0;
        s_vpnRangeCapacity = 0;
    }
    s_vpnListLoaded = qfalse;
    SV_VpnLoadList();
    Com_Printf("vpnblock: reload complete, %d ranges\n", s_vpnRangeCount);
}

static void SV_VpnUpdate_f(void)
{
#ifdef HAS_LIBCURL
    const char *url;
    vpnBuf_t    buf;
    CURL       *curl;
    CURLcode    res;
    long        timeout;
    int         added;

    url = (sv_vpnListUrl && sv_vpnListUrl->string[0]) ? sv_vpnListUrl->string : VPN_DEFAULT_LISTURL;
    if (!Com_IsCurlImportValid(&curlImport) || !curlImport.qcurl_easy_init) {
        Com_Printf("vpnblock: cURL not available\n");
        return;
    }

    Com_Memset(&buf, 0, sizeof(buf));
    curl = curlImport.qcurl_easy_init();
    if (!curl) {
        Com_Printf("vpnblock: cURL init failed\n");
        return;
    }

    timeout = 30;
    curlImport.qcurl_easy_setopt(curl, CURLOPT_URL, url);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SV_VpnWriteCB);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curlImport.qcurl_easy_setopt(curl, CURLOPT_USERAGENT, "OpenMoHAA-VPNBlock/1.0");
    curlImport.qcurl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    Com_Printf("vpnblock: downloading range list from %s\n", url);
    res = curlImport.qcurl_easy_perform(curl);
    curlImport.qcurl_easy_cleanup(curl);

    if (res != CURLE_OK || !buf.data || buf.size < 8) {
        Com_Printf("vpnblock: range list download failed\n");
        if (buf.data) {
            Z_Free(buf.data);
        }
        return;
    }

    FS_WriteFile(sv_vpnList && sv_vpnList->string[0] ? sv_vpnList->string : VPN_DEFAULT_LIST,
                 buf.data, (int)buf.size);

    if (s_vpnRanges) {
        Z_Free(s_vpnRanges);
        s_vpnRanges        = NULL;
        s_vpnRangeCount    = 0;
        s_vpnRangeCapacity = 0;
    }
    added           = SV_VpnParseRangeText(buf.data);
    s_vpnListLoaded = qtrue;
    Z_Free(buf.data);
    Com_Printf("vpnblock: installed %d ranges\n", added);
#else
    Com_Printf("vpnblock: HTTP support not compiled in\n");
#endif
}

static void SV_VpnCheck_f(void)
{
    netadr_t adr;
    char     reason[MAX_REASON_LENGTH];

    if (Cmd_Argc() < 2) {
        Com_Printf("usage: vpnblock_check <ip>\n");
        return;
    }
    Com_Memset(&adr, 0, sizeof(adr));
    if (!NET_StringToAdr(Cmd_Argv(1), &adr, NA_UNSPEC)) {
        Com_Printf("vpnblock: invalid address %s\n", Cmd_Argv(1));
        return;
    }
    if (SV_VpnBlockShouldRefuse(adr, reason, sizeof(reason))) {
        Com_Printf("vpnblock: %s BLOCKED (%s)\n", Cmd_Argv(1), reason[0] ? reason : "vpn/proxy");
    } else {
        Com_Printf("vpnblock: %s allowed\n", Cmd_Argv(1));
    }
}

static void SV_VpnStatus_f(void)
{
    Com_Printf("vpnblock: enabled=%d hosting=%d failopen=%d ranges=%d cache=%d\n",
               sv_vpnBlock ? sv_vpnBlock->integer : 0,
               sv_vpnBlockHosting ? sv_vpnBlockHosting->integer : 0,
               sv_vpnFailOpen ? sv_vpnFailOpen->integer : 0, s_vpnRangeCount, s_vpnCacheCount);
}

void SV_InitVpnBlock(void)
{
    sv_vpnBlock        = Cvar_Get("sv_vpnBlock", "1", CVAR_ARCHIVE);
    sv_vpnBlockHosting = Cvar_Get("sv_vpnBlockHosting", "1", CVAR_ARCHIVE);
    sv_vpnFailOpen     = Cvar_Get("sv_vpnFailOpen", "1", CVAR_ARCHIVE);
    sv_vpnApi          = Cvar_Get("sv_vpnApi", VPN_DEFAULT_API, CVAR_ARCHIVE);
    sv_vpnApiKey       = Cvar_Get("sv_vpnApiKey", "", CVAR_ARCHIVE);
    sv_vpnList         = Cvar_Get("sv_vpnList", VPN_DEFAULT_LIST, CVAR_ARCHIVE);
    sv_vpnListUrl      = Cvar_Get("sv_vpnListUrl", VPN_DEFAULT_LISTURL, CVAR_ARCHIVE);
    sv_vpnWhitelist    = Cvar_Get("sv_vpnWhitelist", "", CVAR_ARCHIVE);
    sv_vpnMessage      = Cvar_Get("sv_vpnMessage", VPN_DEFAULT_MSG, CVAR_ARCHIVE);
    sv_vpnCacheHours   = Cvar_Get("sv_vpnCacheHours", "24", CVAR_ARCHIVE);
    sv_vpnTimeout      = Cvar_Get("sv_vpnTimeout", "3", CVAR_ARCHIVE);

    Cmd_AddCommand("vpnblock_reload", SV_VpnReload_f);
    Cmd_AddCommand("vpnblock_update", SV_VpnUpdate_f);
    Cmd_AddCommand("vpnblock_check", SV_VpnCheck_f);
    Cmd_AddCommand("vpnblock_status", SV_VpnStatus_f);
}

void SV_ShutdownVpnBlock(void)
{
    if (s_vpnRanges) {
        Z_Free(s_vpnRanges);
        s_vpnRanges = NULL;
    }
    s_vpnRangeCount    = 0;
    s_vpnRangeCapacity = 0;
    s_vpnCacheCount    = 0;
    s_vpnListLoaded    = qfalse;
}
