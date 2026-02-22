/*
 * godot_debug_render.h — Debug rendering overlays for developer diagnostics.
 *
 * Phase 84: Implements wireframe overlay (r_showtris), surface normal
 * visualisation (r_shownormals), performance stats (r_speeds), PVS
 * lock (r_lockpvs), and entity bounding box display (r_showbbox).
 *
 * The C accessors read engine cvars from a plain-C translation unit
 * (godot_debug_render_accessors.c) to avoid header conflicts.  The
 * C++ functions manage Godot scene nodes for visual overlays.
 *
 * MoHAARunner Integration Required:
 *   1. Call Godot_DebugRender_Init(this) once in _ready() after
 *      the 3D scene is set up.
 *   2. Call Godot_DebugRender_Update(delta) each frame in _process().
 *   3. Call Godot_DebugRender_Shutdown() in destructor / map unload.
 */

#ifndef GODOT_DEBUG_RENDER_H
#define GODOT_DEBUG_RENDER_H

/* ── C accessor API (callable from both C and C++) ── */
#ifdef __cplusplus
extern "C" {
#endif

int Godot_Debug_GetShowTris(void);
int Godot_Debug_GetShowNormals(void);
int Godot_Debug_GetSpeeds(void);
int Godot_Debug_GetLockPVS(void);
int Godot_Debug_GetDrawBBox(void);

#ifdef __cplusplus
}
#endif

/* ── C++ manager API (Godot node management) ── */
#ifdef __cplusplus

#include <godot_cpp/classes/node.hpp>

/*
 * Initialise the debug rendering system.
 *
 * Creates overlay nodes (CanvasLayer for stats, ImmediateMesh pool
 * for normals/bbox) as children of the given parent node.
 *
 * @param parent  Typically the MoHAARunner node or 3D scene root.
 */
void Godot_DebugRender_Init(godot::Node *parent);

/*
 * Per-frame update.  Reads debug cvars and updates overlays:
 * - r_showtris:    sets viewport debug draw mode
 * - r_speeds:      updates stats Label text (every 10 frames)
 * - r_shownormals: draws normal lines for nearby entities
 * - r_showbbox:    draws wireframe bounding boxes
 *
 * @param delta  Frame delta time in seconds.
 */
void Godot_DebugRender_Update(float delta);

/*
 * Shut down: remove all debug overlay nodes and free resources.
 */
void Godot_DebugRender_Shutdown(void);

#endif /* __cplusplus */

#endif /* GODOT_DEBUG_RENDER_H */
