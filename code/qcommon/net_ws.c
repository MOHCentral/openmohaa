/*
 * net_ws.c — WebSocket-to-UDP relay transport for Emscripten web builds
 *
 * Replaces BSD socket operations with WebSocket communication through
 * a relay server. Each binary WebSocket message carries a 6-byte header:
 *   [4 bytes IPv4 addr][2 bytes port (network byte order)][N bytes data]
 *
 * The relay server (relay/mohaa_relay.js) bridges WebSocket <-> UDP.
 *
 * JavaScript bridge functions (opm_ws_*) are injected by build-web.sh
 * and resolved at SIDE_MODULE import time via the patched runtime stub.
 */

#if defined(GODOT_GDEXTENSION) && defined(__EMSCRIPTEN__)

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#define WS_ADDR_HEADER_SIZE 6

/*
 * These JavaScript functions are injected into the Emscripten runtime
 * by build-web.sh's patch_web_ws_relay() step. They are registered on
 * globalThis (not Module, which isn't available at injection time).
 * The SIDE_MODULE linker marks them as unresolved imports; the patched
 * JS stub handler in mohaa.js resolves them via globalThis[name] lookup.
 */
extern int  opm_ws_open(const char *url);
extern void opm_ws_close(void);
extern int  opm_ws_send(const void *data, int length);
extern int  opm_ws_recv(void *data, int maxlen);
extern int  opm_ws_status(void);
extern int  opm_ws_is_secure(void);

static cvar_t  *net_ws_relay = NULL;
static qboolean ws_was_connected = qfalse;
static int      ws_last_reconnect_time = 0;
static int      ws_last_drop_warning = 0;
static int      ws_dropped_packets = 0;

#define WS_RECONNECT_INTERVAL_MS  5000  /* retry relay connection every 5 s */
#define WS_DROP_WARNING_INTERVAL  5000  /* throttle "packet dropped" warnings */

/* Forward declarations */
static void NET_WS_Status_f(void);

/*
 * Build a WebSocket URL from the cvar value.
 *
 * The cvar never stores the scheme because the Quake command parser
 * treats // as a comment delimiter.  The scheme is determined by
 * querying the browser's page protocol via opm_ws_is_secure():
 *
 *   HTTPS page  →  wss://...
 *   HTTP  page  →  ws://...
 *
 * Accepted cvar formats:
 *   host:port          →  ws(s)://host:port       (direct relay)
 *   host/path          →  ws(s)://host/path        (reverse-proxy relay)
 *   host:port/path     →  ws(s)://host:port/path   (non-standard port proxy)
 *
 * Returns a pointer to a static buffer — valid until the next call.
 */
static const char *NET_WS_BuildURL(const char *hostport)
{
    static char url[512];
    const char *scheme = opm_ws_is_secure() ? "wss" : "ws";
    Com_sprintf(url, sizeof(url), "%s://%s", scheme, hostport);
    return url;
}

/*
 * NET_WS_Init — Register net_ws_relay cvar and open the relay connection.
 * Called from NET_Init() during Com_Init().
 */
void NET_WS_Init(void)
{
    net_ws_relay = Cvar_Get("net_ws_relay", "", CVAR_ARCHIVE);

    Cmd_AddCommand("net_ws_status", NET_WS_Status_f);

    if (net_ws_relay->string[0]) {
        const char *url = NET_WS_BuildURL(net_ws_relay->string);
        opm_ws_open(url);
        Com_Printf("NET_WS: Connecting to relay %s\n", url);
    } else {
        Com_Printf("NET_WS: No relay configured — set net_ws_relay to enable remote multiplayer\n");
        Com_Printf("NET_WS: Example: +set net_ws_relay 192.168.1.100:12300\n");
    }
}

/*
 * NET_WS_Shutdown — Close the relay WebSocket.
 */
void NET_WS_Shutdown(void)
{
    opm_ws_close();
    ws_was_connected = qfalse;
    Com_Printf("NET_WS: Shutdown\n");
}

/*
 * NET_WS_SendPacket — Wrap a MOHAA packet in a relay header and send it.
 * Address header: [4 bytes dest IPv4][2 bytes dest port][payload]
 */
void NET_WS_SendPacket(int length, const void *data, netadr_t to)
{
    byte buf[MAX_MSGLEN + WS_ADDR_HEADER_SIZE];

    /* Auto-reconnect if the user changed the relay cvar at runtime */
    if (net_ws_relay && net_ws_relay->modified) {
        net_ws_relay->modified = qfalse;
        if (net_ws_relay->string[0]) {
            const char *url = NET_WS_BuildURL(net_ws_relay->string);
            opm_ws_open(url);
            Com_Printf("NET_WS: Reconnecting to relay %s\n", url);
        } else {
            opm_ws_close();
        }
    }

    if (!opm_ws_status()) {
        ws_dropped_packets++;
        /* Throttled warning so the console isn't spammed */
        int now = Sys_Milliseconds();
        if (now - ws_last_drop_warning > WS_DROP_WARNING_INTERVAL) {
            ws_last_drop_warning = now;
            if (net_ws_relay && net_ws_relay->string[0]) {
                Com_Printf("NET_WS: Relay not connected — %d packet(s) dropped (relay=%s)\n",
                           ws_dropped_packets, net_ws_relay->string);
            } else {
                Com_Printf("NET_WS: No relay configured — %d packet(s) dropped. Set net_ws_relay to connect.\n",
                           ws_dropped_packets);
            }
        }
        return;
    }

    if (length + WS_ADDR_HEADER_SIZE > (int)sizeof(buf)) {
        Com_Printf("NET_WS: Packet too large (%d bytes)\n", length);
        return;
    }

    /* Build address header */
    if (to.type == NA_BROADCAST) {
        buf[0] = 255; buf[1] = 255; buf[2] = 255; buf[3] = 255;
    } else if (to.type == NA_IP) {
        Com_Memcpy(buf, to.ip, 4);
    } else {
        /* IPv6 / multicast not supported through the relay */
        return;
    }
    /* netadr_t.port is already in network byte order — copy as-is */
    Com_Memcpy(&buf[4], &to.port, 2);
    Com_Memcpy(&buf[WS_ADDR_HEADER_SIZE], data, length);

    opm_ws_send(buf, length + WS_ADDR_HEADER_SIZE);
}

/*
 * NET_WS_GetPacket — Dequeue one packet received from the relay.
 * Returns qfalse when the queue is empty.
 */
qboolean NET_WS_GetPacket(netadr_t *net_from, msg_t *net_message)
{
    byte buf[MAX_MSGLEN + WS_ADDR_HEADER_SIZE];
    int  len;

    len = opm_ws_recv(buf, sizeof(buf));
    if (len < WS_ADDR_HEADER_SIZE) {
        return qfalse;
    }

    /* Parse address header */
    Com_Memset(net_from, 0, sizeof(*net_from));
    net_from->type = NA_IP;
    net_from->ip[0] = buf[0];
    net_from->ip[1] = buf[1];
    net_from->ip[2] = buf[2];
    net_from->ip[3] = buf[3];
    Com_Memcpy(&net_from->port, &buf[4], 2);

    int datalen = len - WS_ADDR_HEADER_SIZE;
    if (datalen > net_message->maxsize) {
        Com_Printf("NET_WS: Oversize packet from %s\n", NET_AdrToString(*net_from));
        return qfalse;
    }

    Com_Memcpy(net_message->data, buf + WS_ADDR_HEADER_SIZE, datalen);
    net_message->cursize = datalen;
    net_message->readcount = 0;

    return qtrue;
}

/*
 * NET_WS_Poll — Drain the WebSocket receive queue and dispatch packets
 * to the engine's server/client packet handlers.
 * Called from NET_Sleep() as a replacement for select()+NET_Event().
 */
void NET_WS_Poll(void)
{
    byte     bufData[MAX_MSGLEN + 1];
    netadr_t from = {0};
    msg_t    netmsg;

    /* Log connection state changes */
    qboolean connected = opm_ws_status() ? qtrue : qfalse;
    if (connected && !ws_was_connected) {
        Com_Printf("NET_WS: Connected to relay\n");
        ws_dropped_packets = 0; /* reset counter on successful connect */
    } else if (!connected && ws_was_connected) {
        Com_Printf("NET_WS: Disconnected from relay\n");
    }
    ws_was_connected = connected;

    /* Auto-reconnect: if relay is configured but not connected, retry periodically */
    if (!connected && net_ws_relay && net_ws_relay->string[0]) {
        int now = Sys_Milliseconds();
        if (now - ws_last_reconnect_time > WS_RECONNECT_INTERVAL_MS) {
            ws_last_reconnect_time = now;
            const char *url = NET_WS_BuildURL(net_ws_relay->string);
            opm_ws_open(url);
            Com_Printf("NET_WS: Auto-reconnecting to relay %s\n", url);
        }
    }

    /* Drain the receive queue */
    while (1) {
        MSG_Init(&netmsg, bufData, sizeof(bufData));

        if (NET_WS_GetPacket(&from, &netmsg)) {
            if (com_sv_running->integer) {
                Com_RunAndTimeServerPacket(&from, &netmsg);
            } else {
                CL_PacketEvent(from, &netmsg);
            }
        } else {
            break;
        }
    }
}

/*
 * NET_WS_Status_f — Console command: print relay connection diagnostics.
 */
static void NET_WS_Status_f(void)
{
    if (!net_ws_relay || !net_ws_relay->string[0]) {
        Com_Printf("NET_WS: No relay configured (net_ws_relay is empty)\n");
        Com_Printf("  Set it with: set net_ws_relay <host:port> or <host/path>\n");
        return;
    }

    const char *url = NET_WS_BuildURL(net_ws_relay->string);
    qboolean connected = opm_ws_status() ? qtrue : qfalse;

    Com_Printf("NET_WS Relay Status:\n");
    Com_Printf("  Cvar:      net_ws_relay = \"%s\"\n", net_ws_relay->string);
    Com_Printf("  URL:       %s\n", url);
    Com_Printf("  Connected: %s\n", connected ? "YES" : "NO");
    Com_Printf("  Dropped:   %d packet(s)\n", ws_dropped_packets);
}

#endif /* GODOT_GDEXTENSION && __EMSCRIPTEN__ */
