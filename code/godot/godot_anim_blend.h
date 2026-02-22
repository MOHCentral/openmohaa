/*
 * godot_anim_blend.h — Animation blending verification and helpers.
 *
 * Provides utilities to extract, inspect, and verify multi-channel
 * animation blending state from the entity buffer.  The engine's
 * skeletor (ri.TIKI_SetPoseInternal) already performs correct
 * multi-channel blending internally; these helpers enable debug
 * validation and provide a standalone quaternion-based blending
 * path for verification.
 *
 * MOHAA uses up to MAX_FRAMEINFOS (16) animation channels split
 * into two groups by FRAMEINFO_BLEND (8):
 *   Group A: channels [0 .. FRAMEINFO_BLEND-1]  (action/upper body)
 *   Group B: channels [FRAMEINFO_BLEND .. MAX_FRAMEINFOS-1] (legs/movement)
 * actionWeight blends Group A vs Group B (0.0 = all A, 1.0 = all B).
 *
 * Phase 241 — Animation blending accuracy.
 */

#ifndef GODOT_ANIM_BLEND_H
#define GODOT_ANIM_BLEND_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MAX_ANIM_CHANNELS mirrors MAX_FRAMEINFOS from q_shared.h.
 * Defined locally to avoid pulling engine headers into Godot C++ TUs.
 */
#define MAX_ANIM_CHANNELS 16
#define ANIM_BLEND_SPLIT  ( MAX_ANIM_CHANNELS >> 1 ) /* 8 */

/*
 * AnimBlendInput — extracted animation state for a single entity.
 *
 * Populated by Godot_AnimBlend_ExtractFromEntity() from the renderer's
 * entity buffer (gr_entities[]).
 */
typedef struct {
    int   frame_indices[MAX_ANIM_CHANNELS]; /* per-channel frame index      */
    float frame_weights[MAX_ANIM_CHANNELS]; /* per-channel blend weight     */
    float frame_times[MAX_ANIM_CHANNELS];   /* per-channel animation time   */
    float action_weight;                    /* group A vs group B blend     */
    int   active_channels;                  /* channels with weight > 0     */
    float weight_sum_a;                     /* sum of weights in group A    */
    float weight_sum_b;                     /* sum of weights in group B    */
} AnimBlendInput;

/*
 * AnimBlendValidation — diagnostic output from weight validation.
 */
typedef struct {
    int   valid;                /* 1 if blending state looks correct       */
    float total_weight;         /* combined effective weight (should be ~1) */
    int   zero_weight_channels; /* channels with weight == 0               */
    int   group_a_active;       /* active channels in group A              */
    int   group_b_active;       /* active channels in group B              */
} AnimBlendValidation;

/*
 * Godot_AnimBlend_ExtractFromEntity — read animation blending state
 * from the entity buffer.
 *
 * @param entity_index  Index into gr_entities[] (0-based).
 * @param out           Output struct filled with channel data.
 * @return 1 on success, 0 if entity has no TIKI or index is invalid.
 */
int Godot_AnimBlend_ExtractFromEntity(int entity_index, AnimBlendInput *out);

/*
 * Godot_AnimBlend_Validate — check that blending weights are well-formed.
 *
 * Verifies:
 *   - At least one channel has weight > 0
 *   - Group weights sum to sensible values
 *   - actionWeight is in [0, 1]
 *
 * @param input   Blending state from Godot_AnimBlend_ExtractFromEntity().
 * @param result  Output validation diagnostics.
 * @return 1 if valid, 0 if anomalies detected.
 */
int Godot_AnimBlend_Validate(const AnimBlendInput *input,
                             AnimBlendValidation *result);

/*
 * Godot_AnimBlend_ComputeBones — compute final bone transforms with
 * proper multi-channel blending via the engine's skeletor.
 *
 * This is a convenience wrapper around Godot_Skel_PrepareBones()
 * that accepts an AnimBlendInput and returns the bone cache.
 * The engine's TIKI_SetPoseInternal already blends all channels
 * and applies actionWeight internally.
 *
 * @param tiki_ptr      dtiki_t pointer for the model.
 * @param entity_number Game entity number (for skeletor lookup).
 * @param input         Blending state (frameInfo + actionWeight).
 * @param bone_tag      Controller bone indices (int[5]), may be NULL.
 * @param bone_quat     Controller bone quaternions (float[5][4]), may be NULL.
 * @param out_bones     Output: caller-provided buffer for skelBoneCache_t[].
 *                      Must have room for max_bones entries.
 * @param max_bones     Size of out_bones buffer.
 * @param out_bone_count Actual number of bones written.
 * @return 1 on success, 0 on failure.
 */
int Godot_AnimBlend_ComputeBones(void *tiki_ptr,
                                 int entity_number,
                                 const AnimBlendInput *input,
                                 const int *bone_tag,
                                 const float *bone_quat,
                                 void *out_bones,
                                 int max_bones,
                                 int *out_bone_count);

/*
 * Godot_AnimBlend_GetActiveChannelCount — quick query for the number
 * of channels with non-zero weight for a given entity.
 *
 * @param entity_index  Index into gr_entities[].
 * @return Number of active channels, or 0 if entity is invalid.
 */
int Godot_AnimBlend_GetActiveChannelCount(int entity_index);

/*
 * Godot_AnimBlend_DebugLogEntity — log animation blending state for
 * a single entity to the engine console (Com_Printf).
 *
 * Only logs when developer cvar is enabled to avoid spam.
 *
 * @param entity_index  Index into gr_entities[].
 */
void Godot_AnimBlend_DebugLogEntity(int entity_index);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_ANIM_BLEND_H */
