/*
 * godot_game_sp.h — Single-player state accessor declarations.
 *
 * Provides read-only accessors for cutscene, objective, and save/load
 * state.  Called from MoHAARunner.cpp via extern "C"; the implementation
 * lives in godot_game_sp.c which includes server.h / bg_public.h.
 */

#ifndef GODOT_GAME_SP_H
#define GODOT_GAME_SP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_SP_IsCutsceneActive
 *   Returns non-zero when a scripted cutscene is active.
 *   Checks STAT_CINEMATIC (level.cinematic or actor_camera) and
 *   STAT_LETTERBOX (black-bar letterbox mode) on the first client.
 */
int Godot_SP_IsCutsceneActive(void);

/*
 * Godot_SP_GetObjectiveCount
 *   Returns the number of objectives tracked by the current level.
 *   Objectives are managed via script commands (addobjective) and
 *   stored in the Level C++ object; this accessor returns 0 until
 *   a deeper C++ bridge is wired up.
 */
int Godot_SP_GetObjectiveCount(void);

/*
 * Godot_SP_GetObjectiveComplete
 *   Returns 1 if objective index @i is marked complete, 0 otherwise.
 *   Currently stubbed — objective completion state lives in the
 *   Level/Entity C++ layer, unreachable from pure-C server headers.
 */
int Godot_SP_GetObjectiveComplete(int i);

/*
 * Godot_SP_CanSave
 *   Returns 1 when saving is allowed: server running in SS_GAME,
 *   single-player gametype, player alive and not loading.
 *   Mirrors SV_AllowSaveGame() from sv_ccmds.c but works under
 *   GODOT_GDEXTENSION (where DEDICATED is defined).
 */
int Godot_SP_CanSave(void);

/*
 * Godot_SP_CanLoad
 *   Returns 1 when a quick-save file exists and could be loaded.
 *   Checks for the "quick" save archive via the engine VFS.
 */
int Godot_SP_CanLoad(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_GAME_SP_H */
