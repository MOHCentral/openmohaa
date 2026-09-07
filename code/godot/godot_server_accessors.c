/*
 * godot_server_accessors.c — Thin accessor functions for server state.
 *
 * MoHAARunner.cpp (C++) cannot easily include server.h because of
 * the deep header dependency chain (q_shared.h → qcommon.h → server.h
 * collides with godot-cpp headers).  Instead, these C accessor
 * functions are compiled as part of the openmohaa source tree where
 * server.h is available, and MoHAARunner.cpp calls them via extern "C".
 */

#include "../server/server.h"

int Godot_GetServerState(void) {
    return (int)sv.state;
}

const char *Godot_GetMapName(void) {
    return svs.mapName;
}

int Godot_GetPlayerCount(void) {
    return svs.iNumClients;
}

/*
 * Godot_GetMaxClients — return sv_maxclients cvar value.
 */
int Godot_GetMaxClients(void) {
    if (sv_maxclients) {
        return sv_maxclients->integer;
    }
    return 0;
}

/*
 * Godot_GetScoreboardPlayer — read scoreboard data for client slot i.
 *
 * Returns 1 if the slot is active (CS_CONNECTED or CS_ACTIVE), 0 otherwise.
 * out_name receives the player name (max out_name_len chars).
 * out_kills, out_deaths, out_ping are written from playerState_t stats
 * and client_t fields.
 */
int Godot_GetScoreboardPlayer(int i,
                              char *out_name, int out_name_len,
                              int *out_kills, int *out_deaths,
                              int *out_ping) {
    int maxcl;
    client_t *cl;
    playerState_t *ps;

    maxcl = sv_maxclients ? sv_maxclients->integer : 0;
    if (i < 0 || i >= maxcl || i >= svs.iNumClients) {
        return 0;
    }

    cl = &svs.clients[i];
    if (cl->state < CS_CONNECTED) {
        return 0;
    }

    /* Player name */
    if (out_name && out_name_len > 0) {
        Q_strncpyz(out_name, cl->name, out_name_len);
    }

    /* Ping */
    if (out_ping) {
        *out_ping = (cl->state == CS_ACTIVE) ? cl->ping : -1;
    }

    /* Kills and deaths from playerState_t stats (set by fgame each frame) */
    ps = SV_GameClientNum(i);
    if (ps) {
        if (out_kills)  *out_kills  = ps->stats[STAT_KILLS];
        if (out_deaths) *out_deaths = ps->stats[STAT_DEATHS];
    } else {
        if (out_kills)  *out_kills  = 0;
        if (out_deaths) *out_deaths = 0;
    }

    return 1;
}
