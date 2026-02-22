/*
 * godot_entity_lighting.h — Lightgrid + dynamic light sampling for entities.
 *
 * Phase 63: Sample BSP lightgrid at entity positions for ambient/directed
 *           light modulation.
 * Phase 64: Accumulate nearby dynamic lights (muzzle flashes, explosions)
 *           for per-entity light contribution.
 *
 * Coordinate convention:
 *   All positions passed to these functions are in **id Tech 3** space
 *   (X=Forward, Y=Left, Z=Up, inches).  The lightgrid sampling is done
 *   in id space via Godot_BSP_LightForPoint().  The returned colours are
 *   normalised RGB [0,1] suitable for Godot material modulation.
 *
 * MoHAARunner Integration Required:
 *   In update_entities(), for each entity:
 *     1. Call Godot_EntityLight_Sample() with entity origin (id-space)
 *        to get the lightgrid ambient colour.
 *     2. Call Godot_EntityLight_Dlights() with entity origin (id-space)
 *        to get additive dynamic light contribution.
 *     3. Combine: final_color = lightgrid_color + dlight_color
 *     4. Apply to material: albedo_color *= final_color
 *
 *   Handle RF_LIGHTING_ORIGIN (0x0080): when set, sample lightgrid at
 *   the entity's lightingOrigin instead of its render origin.
 */

#ifndef GODOT_ENTITY_LIGHTING_H
#define GODOT_ENTITY_LIGHTING_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_EntityLight_Sample — sample lightgrid at an entity position.
 *
 * @param id_origin  Entity position in id Tech 3 coordinates [3].
 * @param out_r, out_g, out_b  Returned ambient+directed light as RGB [0,1].
 *
 * Internally calls Godot_BSP_LightForPoint() and combines ambient +
 * directed into a single colour suitable for material modulation.
 * Falls back to (0.5, 0.5, 0.5) if lightgrid data is unavailable.
 */
void Godot_EntityLight_Sample(const float id_origin[3],
                              float *out_r, float *out_g, float *out_b);

/*
 * Godot_EntityLight_Dlights — accumulate dynamic light contribution.
 *
 * @param id_origin  Entity position in id Tech 3 coordinates [3].
 * @param max_lights Maximum number of dynamic lights to consider (≤64).
 *                   Pass 4 for typical per-entity limit.
 * @param out_r, out_g, out_b  Returned additive dynamic light RGB [0,1].
 *
 * Reads gr_dlights[] via Godot_Renderer_GetDlight() accessor.
 * Selects the N closest lights, computes attenuation, and returns
 * the accumulated colour contribution.
 */
void Godot_EntityLight_Dlights(const float id_origin[3],
                               int max_lights,
                               float *out_r, float *out_g, float *out_b);

/*
 * Godot_EntityLight_Combined — convenience: lightgrid + dlights.
 *
 * @param id_origin  Entity position in id Tech 3 coordinates [3].
 * @param max_dlights  Max dynamic lights to consider.
 * @param out_r, out_g, out_b  Returned combined light colour RGB [0,1].
 *
 * Clamps result to [0,1] per channel.
 */
void Godot_EntityLight_Combined(const float id_origin[3],
                                int max_dlights,
                                float *out_r, float *out_g, float *out_b);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_ENTITY_LIGHTING_H */
