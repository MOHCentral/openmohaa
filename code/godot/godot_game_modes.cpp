/*
 * godot_game_modes.cpp — Game mode state accessors for MoHAARunner.
 *
 * Compiled as C++ because DM_Manager is a C++ class.  All public
 * functions use extern "C" linkage so they can be called from
 * MoHAARunner.cpp via a simple extern "C" declaration block.
 *
 * This file is compiled as part of the main GDExtension library
 * (code/godot/ is already in the SCons source list).
 */

#include "../fgame/g_local.h"
#include "../fgame/dm_manager.h"
#include "../fgame/level.h"
#include "../fgame/gamecvars.h"

extern "C" {

int Godot_GameMode_GetType(void)
{
    return g_gametype ? g_gametype->integer : 0;
}

int Godot_GameMode_GetRoundState(void)
{
    /* 0 = inactive / single-player
       1 = warmup / waiting for players
       2 = round active
       3 = intermission */
    if (level.intermissiontime > 0) {
        return 3;
    }
    if (g_gametype && g_gametype->integer >= GT_FFA) {
        if (dmManager.RoundActive()) {
            return 2;
        }
        /* Round-based game but round not active → warmup / between rounds */
        if (dmManager.GameHasRounds()) {
            return 1;
        }
        /* Non-round mode (FFA/TDM): game is running if gametype set */
        return 2;
    }
    return 0;
}

int Godot_GameMode_GetScoreLimit(void)
{
    /* Round-based modes use roundlimit; FFA/TDM use fraglimit. */
    if (g_gametype && g_gametype->integer >= GT_TEAM_ROUNDS) {
        return roundlimit ? roundlimit->integer : 0;
    }
    return fraglimit ? fraglimit->integer : 0;
}

int Godot_GameMode_GetTimeLimit(void)
{
    return timelimit ? timelimit->integer : 0;
}

int Godot_GameMode_GetTeamScore(int team)
{
    if (team == 0) {
        return dmManager.GetTeamAllies()->m_teamwins;
    }
    if (team == 1) {
        return dmManager.GetTeamAxis()->m_teamwins;
    }
    return 0;
}

int Godot_GameMode_IsWarmup(void)
{
    if (!g_warmup || !g_gametype) {
        return 0;
    }
    /* Single-player has no warmup. */
    if (g_gametype->integer <= GT_SINGLE_PLAYER) {
        return 0;
    }
    /* In round-based modes, warmup is when round is not active and
       the match start time has not arrived yet. */
    if (dmManager.GameHasRounds() && !dmManager.RoundActive()) {
        return 1;
    }
    return 0;
}

int Godot_GameMode_GetPlayerCount(void)
{
    return dmManager.PlayerCount();
}

} /* extern "C" */
