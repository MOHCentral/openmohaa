/*
 * godot_debug_render_accessors.c — C accessors for debug rendering cvars.
 *
 * Exposes engine debug cvars (r_showtris, r_shownormals, r_speeds,
 * r_lockpvs, r_showbbox) to the Godot C++ side without requiring
 * engine headers that conflict with godot-cpp.
 *
 * Each function calls Cvar_Get() to retrieve (or create) the cvar
 * and returns its integer value.  Cvars are created with default 0
 * and CVAR_CHEAT flag so they match upstream renderer behaviour.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* CVAR_CHEAT = 0x0200 (from q_shared.h) */

/*
 * Godot_Debug_GetShowTris — r_showtris cvar.
 *
 * 0 = off, 1 = wireframe overlay, 2 = wireframe + solid.
 */
int Godot_Debug_GetShowTris(void) {
    cvar_t *cv = Cvar_Get("r_showtris", "0", CVAR_CHEAT);
    if (!cv) return 0;
    return cv->integer;
}

/*
 * Godot_Debug_GetShowNormals — r_shownormals cvar.
 *
 * 0 = off, 1 = draw surface normals.
 */
int Godot_Debug_GetShowNormals(void) {
    cvar_t *cv = Cvar_Get("r_shownormals", "0", CVAR_CHEAT);
    if (!cv) return 0;
    return cv->integer;
}

/*
 * Godot_Debug_GetSpeeds — r_speeds cvar.
 *
 * 0 = off, 1 = show per-frame rendering stats overlay.
 */
int Godot_Debug_GetSpeeds(void) {
    cvar_t *cv = Cvar_Get("r_speeds", "0", CVAR_CHEAT);
    if (!cv) return 0;
    return cv->integer;
}

/*
 * Godot_Debug_GetLockPVS — r_lockpvs cvar.
 *
 * 0 = off, 1 = freeze PVS culling at current position.
 */
int Godot_Debug_GetLockPVS(void) {
    cvar_t *cv = Cvar_Get("r_lockpvs", "0", CVAR_CHEAT);
    if (!cv) return 0;
    return cv->integer;
}

/*
 * Godot_Debug_GetDrawBBox — r_showbbox cvar (custom).
 *
 * 0 = off, 1 = draw entity bounding boxes.
 */
int Godot_Debug_GetDrawBBox(void) {
    cvar_t *cv = Cvar_Get("r_showbbox", "0", CVAR_CHEAT);
    if (!cv) return 0;
    return cv->integer;
}
