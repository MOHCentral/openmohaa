/*
 * godot_entity_lighting.cpp — Lightgrid + dynamic light sampling for entities.
 *
 * Phase 63: Lightgrid entity lighting.
 * Phase 64: Dynamic lights on entities.
 * Phase 39: Rewritten to use Godot_EntityGridLighting() which replicates
 *           RB_GetEntityGridLighting() exactly (lightgrid + dlights +
 *           overbright + normalise + clamp).
 */

#include "godot_entity_lighting.h"

#include <cmath>
#include <cstring>
#include <algorithm>

/* ── Engine-parity entity lightgrid accessor (godot_bsp_accessors.c) ── */
extern "C" {
    void Godot_EntityGridLighting(const float origin[3],
                                  float *out_r, float *out_g, float *out_b);
}

/* ===================================================================
 *  Phase 63: Lightgrid Entity Lighting — now delegates to the real
 *  engine R_GetLightingGridValue() via the accessor.
 * ================================================================ */

extern "C"
void Godot_EntityLight_Sample(const float id_origin[3],
                              float *out_r, float *out_g, float *out_b)
{
    /* Godot_EntityGridLighting already returns the combined lightgrid
     * colour including overbright, normalised to [0,1].  No further
     * processing needed here. */
    Godot_EntityGridLighting(id_origin, out_r, out_g, out_b);
}

/* ===================================================================
 *  Phase 64: Dynamic Lights on Entities — now handled inside
 *  Godot_EntityGridLighting() using the engine's exact dlight formula.
 *  This function is kept for API compatibility but returns zero since
 *  dlights are already included in the lightgrid result.
 * ================================================================ */

extern "C"
void Godot_EntityLight_Dlights(const float id_origin[3],
                               int max_lights,
                               float *out_r, float *out_g, float *out_b)
{
    (void)id_origin;
    (void)max_lights;
    /* Dlights are now accumulated inside Godot_EntityGridLighting() */
    if (out_r) *out_r = 0.0f;
    if (out_g) *out_g = 0.0f;
    if (out_b) *out_b = 0.0f;
}

/* ===================================================================
 *  Convenience: Combined lightgrid + dlights (for API compatibility)
 * ================================================================ */

extern "C"
void Godot_EntityLight_Combined(const float id_origin[3],
                                int max_dlights,
                                float *out_r, float *out_g, float *out_b)
{
    (void)max_dlights;
    /* Godot_EntityGridLighting handles everything: lightgrid sample,
     * dlight accumulation, overbright, normalise, clamp. */
    Godot_EntityGridLighting(id_origin, out_r, out_g, out_b);
}
