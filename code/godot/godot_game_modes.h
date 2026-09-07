/*
 * godot_game_modes.h — Game mode state accessor declarations.
 *
 * Thin C-linkage API so MoHAARunner.cpp can query the current game
 * mode, round state, scores, warmup, and player count without
 * including the heavy fgame headers (g_local.h, dm_manager.h).
 */

#ifndef GODOT_GAME_MODES_H
#define GODOT_GAME_MODES_H

#ifdef __cplusplus
extern "C" {
#endif

/* g_gametype cvar value (GT_SINGLE_PLAYER=0, GT_FFA=1, GT_TEAM=2,
   GT_TEAM_ROUNDS=3, GT_OBJECTIVE=4, GT_TOW=5, GT_LIBERATION=6). */
int Godot_GameMode_GetType(void);

/* Round state: 0=inactive, 1=warmup/pre-round, 2=round active,
   3=intermission. */
int Godot_GameMode_GetRoundState(void);

/* fraglimit (FFA/TDM) or roundlimit (round-based modes). */
int Godot_GameMode_GetScoreLimit(void);

/* timelimit cvar value in minutes. */
int Godot_GameMode_GetTimeLimit(void);

/* Team score (team 0=allies, team 1=axis).  Returns team win count
   for round-based modes or team kill count for TDM/FFA. */
int Godot_GameMode_GetTeamScore(int team);

/* Returns 1 if the warmup period is active (match has not started). */
int Godot_GameMode_IsWarmup(void);

/* Number of players currently in the DM manager player list. */
int Godot_GameMode_GetPlayerCount(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_GAME_MODES_H */
