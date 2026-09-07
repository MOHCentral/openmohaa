/*
 * godot_network_accessors.c — Thin accessor functions for network state.
 *
 * Exposes connection status, ping, client counts, and protocol info
 * to MoHAARunner.cpp (Godot C++ side) without requiring engine headers
 * that conflict with godot-cpp.
 *
 * Compiled as C so it can include both server.h and client.h (which
 * include qcommon.h via include guards with no conflict).
 */

#include "../server/server.h"
#include "../client/client.h"

/*
 * Client connection state (connstate_t enum value).
 * CA_UNINITIALIZED(0) .. CA_ACTIVE(8).
 */
int Godot_Net_GetClientState(void) {
    return (int)clc.state;
}

/*
 * Count of CS_ACTIVE server-side client slots.
 */
int Godot_Net_GetServerClientCount(void) {
    int i, count = 0;

    if (!com_sv_running || !com_sv_running->integer || !svs.clients) {
        return 0;
    }

    for (i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state == CS_ACTIVE) {
            count++;
        }
    }
    return count;
}

/*
 * Count of server-side client slots at CS_CONNECTED or above
 * (includes CS_CONNECTED, CS_PRIMED, CS_ACTIVE).
 */
int Godot_Net_GetServerConnectedCount(void) {
    int i, count = 0;

    if (!com_sv_running || !com_sv_running->integer || !svs.clients) {
        return 0;
    }

    for (i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state >= CS_CONNECTED) {
            count++;
        }
    }
    return count;
}

/*
 * Snapshot ping for the local client (milliseconds).
 * Returns the ping from the most recent valid snapshot, or 0 if none.
 */
int Godot_Net_GetPing(void) {
    if (clc.state != CA_ACTIVE || !cl.snap.valid) {
        return 0;
    }
    return cl.snap.ping;
}

/*
 * Current client snapshot request rate (snaps cvar).
 * Falls back to 20 if the cvar is not yet registered.
 */
int Godot_Net_GetSnapshotRate(void) {
    cvar_t *cl_snaps = Cvar_Get("snaps", "20", 0);
    return cl_snaps ? cl_snaps->integer : 20;
}

/*
 * Textual address of the server the client is connected to.
 * Returns static buffer — valid until next call.
 */
const char *Godot_Net_GetServerAddress(void) {
    if (clc.state < CA_CONNECTING) {
        return "";
    }
    return NET_AdrToString(clc.serverAddress);
}

/*
 * Returns 1 if the current server address is a LAN address.
 */
int Godot_Net_IsLANGame(void) {
    if (clc.state < CA_CONNECTING) {
        return 0;
    }
    return (int)Sys_IsLANAddress(clc.serverAddress);
}

/*
 * Server running state — 1 if com_sv_running is true, 0 otherwise.
 */
int Godot_Net_IsServerRunning(void) {
    return (com_sv_running && com_sv_running->integer) ? 1 : 0;
}

/*
 * UDP port the server is currently bound to (net_port cvar value).
 */
int Godot_Net_GetPort(void) {
    cvar_t *port = Cvar_Get("net_port", "12203", 0);
    return port ? port->integer : 12203;
}

/*
 * Engine protocol version number.
 */
int Godot_Net_GetProtocolVersion(void) {
    return PROTOCOL_VERSION;
}
