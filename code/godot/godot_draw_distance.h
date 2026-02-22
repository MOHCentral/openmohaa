/*
 * godot_draw_distance.h — Public API for draw distance management.
 *
 * Maps engine draw distance cvars (r_znear, r_zfar, cg_farplane,
 * cg_farplane_color) to Godot Camera3D near/far planes and
 * Environment fog.
 */

#ifndef GODOT_DRAW_DISTANCE_H
#define GODOT_DRAW_DISTANCE_H

#ifdef __cplusplus

namespace godot {
class Camera3D;
class Environment;
}  // namespace godot

/*
 * Godot_DrawDistance_Init — one-time initialisation (registers cvars).
 */
void Godot_DrawDistance_Init(void);

/*
 * Godot_DrawDistance_Update — apply draw distance settings to camera
 * and environment.  Call once per frame (internally rate-limited to
 * poll cvars once per second via a delta accumulator).
 *
 * camera: the active Camera3D node.
 * env:    the WorldEnvironment's Environment resource.
 * delta:  frame delta in seconds (from _process).
 */
void Godot_DrawDistance_Update(godot::Camera3D *camera,
                               godot::Environment *env,
                               float delta);

/*
 * Godot_DrawDistance_GetCullDistance — returns the far-plane cull
 * distance in Godot metres, or 0 if culling is disabled.
 *
 * MoHAARunner integration point: before spawning entity MeshInstance3D
 * nodes, compare entity distance to this value and skip entities that
 * are beyond it.
 */
float Godot_DrawDistance_GetCullDistance(void);

#endif /* __cplusplus */

/* ── C accessors (godot_draw_distance_accessors.c) ── */
#ifdef __cplusplus
extern "C" {
#endif

float Godot_DrawDistance_GetZNear(void);
float Godot_DrawDistance_GetZFar(void);
float Godot_DrawDistance_GetFarplane(void);
void  Godot_DrawDistance_GetFarplaneColor(float *r, float *g, float *b);
int   Godot_DrawDistance_GetFarplaneCull(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_DRAW_DISTANCE_H */
