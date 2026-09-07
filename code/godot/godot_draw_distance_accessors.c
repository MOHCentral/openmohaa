/*
 * godot_draw_distance_accessors.c — C accessors for draw distance cvars.
 *
 * Exposes r_znear, r_zfar, cg_farplane, cg_farplane_color, and
 * farplane_cull to the Godot C++ side without requiring engine
 * headers that conflict with godot-cpp.
 *
 * The farplane values (distance, colour, cull) are captured per-frame
 * from refdef_t in godot_renderer.c.  This accessor delegates to
 * the existing Godot_Renderer_GetFarplane() for those.  r_znear and
 * r_zfar are read directly via Cvar_Get().
 */

#ifdef GODOT_GDEXTENSION

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* ── Renderer farplane accessor (godot_renderer.c) ── */
extern void Godot_Renderer_GetFarplane(float *distance, float *bias, float *color, int *cull);

/* Default near clip plane in inches (matches upstream r_znear). */
#define DEFAULT_ZNEAR_INCHES 4.0f

/*
 * Godot_DrawDistance_GetZNear — near clip plane in inches.
 *
 * The upstream renderer registers r_znear with default "4".
 * Under the Godot stub renderer, the cvar may not exist yet,
 * so we create/fetch it here with the same default.
 */
float Godot_DrawDistance_GetZNear(void)
{
    cvar_t *cv = Cvar_Get("r_znear", "4", 0);
    if (cv && cv->value > 0.0f) {
        return cv->value;
    }
    return DEFAULT_ZNEAR_INCHES;
}

/*
 * Godot_DrawDistance_GetZFar — far clip plane override in inches.
 *
 * Default 0 means "auto" (use cg_farplane or engine default).
 */
float Godot_DrawDistance_GetZFar(void)
{
    cvar_t *cv = Cvar_Get("r_zfar", "0", 0);
    if (cv) {
        return cv->value;
    }
    return 0.0f;
}

/*
 * Godot_DrawDistance_GetFarplane — cg_farplane distance in inches.
 *
 * Reads from the per-frame refdef capture in godot_renderer.c.
 * Returns 0 when farplane fog is disabled.
 */
float Godot_DrawDistance_GetFarplane(void)
{
    float dist = 0.0f;
    Godot_Renderer_GetFarplane(&dist, NULL, NULL, NULL);
    return dist;
}

/*
 * Godot_DrawDistance_GetFarplaneColor — RGB fog colour (0–1 range).
 */
void Godot_DrawDistance_GetFarplaneColor(float *r, float *g, float *b)
{
    float color[3] = { 0.0f, 0.0f, 0.0f };
    Godot_Renderer_GetFarplane(NULL, NULL, color, NULL);
    if (r) *r = color[0];
    if (g) *g = color[1];
    if (b) *b = color[2];
}

/*
 * Godot_DrawDistance_GetFarplaneCull — 1 if entities beyond
 * the far plane should be culled, 0 otherwise.
 */
int Godot_DrawDistance_GetFarplaneCull(void)
{
    int cull = 0;
    Godot_Renderer_GetFarplane(NULL, NULL, NULL, &cull);
    return cull;
}

#endif /* GODOT_GDEXTENSION */
