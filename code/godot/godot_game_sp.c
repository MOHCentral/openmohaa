/*
 * godot_game_sp.c — Single-player state accessors for Godot.
 *
 * Compiled as part of the main openmohaa library.  Includes server.h
 * (which transitively brings in bg_public.h / g_public.h) so we can
 * read player stats, server state, and VFS save-file presence.
 *
 * MoHAARunner.cpp calls these via extern "C" declarations in
 * godot_game_sp.h.
 */

#include "../server/server.h"
#include "godot_game_sp.h"

/* ------------------------------------------------------------------ */
/* Cutscene detection                                                  */
/* ------------------------------------------------------------------ */

int Godot_SP_IsCutsceneActive(void)
{
    if (sv.state != SS_GAME) {
        return 0;
    }
    if (!svs.clients
        || !svs.clients->gentity
        || !svs.clients->gentity->client) {
        return 0;
    }

    /*
     * STAT_CINEMATIC: bit 0 = level.cinematic, bit 1 = actor_camera.
     * STAT_LETTERBOX: non-zero when letterbox black bars are active.
     * Either condition means a scripted cutscene is playing.
     */
    {
        int cin = svs.clients->gentity->client->ps.stats[STAT_CINEMATIC];
        int lb  = svs.clients->gentity->client->ps.stats[STAT_LETTERBOX];
        return (cin || lb) ? 1 : 0;
    }
}

/* ------------------------------------------------------------------ */
/* Objective tracking                                                  */
/* ------------------------------------------------------------------ */

int Godot_SP_GetObjectiveCount(void)
{
    /*
     * Objectives are created at runtime by script commands
     * (addobjective) and stored in the Level C++ object and entity
     * list.  There is no simple integer counter accessible from pure-C
     * server headers.  Return 0 until a C++ accessor bridge is added.
     */
    return 0;
}

int Godot_SP_GetObjectiveComplete(int i)
{
    /*
     * Objective completion state lives in the fgame C++ entity layer
     * (func_objective).  Not reachable from C server headers.
     * Return 0 (incomplete) for all indices.
     */
    (void)i;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Save / load eligibility                                             */
/* ------------------------------------------------------------------ */

int Godot_SP_CanSave(void)
{
    /*
     * Mirrors the checks in SV_AllowSaveGame() (sv_ccmds.c) but
     * works under GODOT_GDEXTENSION where DEDICATED is defined.
     * We skip the com_cl_running check (Godot drives the client)
     * and the clc.state / cg_gametype check (client headers not
     * available here when DEDICATED is set).
     */
    if (!com_sv_running || !com_sv_running->integer) {
        return 0;
    }
    if (sv.state != SS_GAME) {
        return 0;
    }
    if (g_gametype && g_gametype->integer != GT_SINGLE_PLAYER) {
        return 0;
    }
    if (!svs.clients
        || !svs.clients->gentity
        || !svs.clients->gentity->client
        || !svs.clients->gentity->client->ps.stats[STAT_HEALTH]) {
        return 0;
    }

    return 1;
}

int Godot_SP_CanLoad(void)
{
    const char *name;
    long        len;

    /*
     * Check whether the "quick" save archive exists.  The engine
     * stores save files via Com_GetArchiveFileName(<name>, "ssv").
     * A positive FS_ReadFileEx length means the file is present.
     */
    name = Com_GetArchiveFileName("quick", "ssv");
    len  = FS_ReadFileEx(name, NULL, qtrue);
    return (len > 0) ? 1 : 0;
}
