/*
 * godot_anim_blend.cpp — Animation blending verification and helpers.
 *
 * Implements the API declared in godot_anim_blend.h.  Reads the
 * renderer entity buffer (gr_entities[]) via the existing C accessor
 * layer in godot_renderer.c and provides blending state extraction,
 * validation, and a convenience wrapper for bone computation.
 *
 * Architecture note:
 *   The engine's skeletor system (ri.TIKI_SetPoseInternal) already
 *   performs correct multi-channel animation blending.  All 16
 *   frameInfo channels are passed through, and actionWeight is
 *   applied by the engine to blend group A (action/upper body)
 *   vs group B (legs/movement).  Godot_Skel_PrepareBones() in
 *   godot_skel_model_accessors.cpp delegates to this engine path.
 *
 *   These helpers provide debug inspection and validation — they do
 *   NOT replace the engine's blending.  Use Godot_AnimBlend_DebugLogEntity()
 *   to verify channel weights at runtime.
 *
 * Phase 241 — Animation blending accuracy.
 */

#include "../qcommon/q_shared.h"
#include "godot_anim_blend.h"

#include <math.h>
#include <string.h>

/*
 * Size of skelBoneCache_t (float offset[4] + float matrix[3][4] = 64 bytes).
 * Defined locally to avoid pulling in the full skeletor header chain.
 */
#define SKEL_BONE_CACHE_SIZE 64

/* ── External accessors (implemented in godot_renderer.c) ── */
extern "C" {

int  Godot_Renderer_GetEntityCount(void);
int  Godot_Renderer_GetEntityAnim(int index,
                                   void **outTiki,
                                   int *outEntityNumber,
                                   void *outFrameInfo,
                                   int *outBoneTag,
                                   float *outBoneQuat,
                                   float *outActionWeight,
                                   float *outScale);

/* Bone preparation (implemented in godot_skel_model_accessors.cpp) */
void *Godot_Skel_PrepareBones(void *tikiPtr, int entityNumber,
                               const frameInfo_t *frameInfo,
                               const int *bone_tag,
                               const float *bone_quat,
                               float actionWeight,
                               int *outBoneCount);

} /* extern "C" */

/* ===================================================================
 *  Godot_AnimBlend_ExtractFromEntity
 * ================================================================ */

extern "C"
int Godot_AnimBlend_ExtractFromEntity(int entity_index, AnimBlendInput *out)
{
    if (!out) return 0;

    memset(out, 0, sizeof(*out));

    frameInfo_t frameInfo[MAX_FRAMEINFOS];
    float actionWeight = 0.0f;
    void *tiki = NULL;
    int entityNumber = -1;

    if (!Godot_Renderer_GetEntityAnim(entity_index,
                                       &tiki, &entityNumber,
                                       frameInfo, NULL, NULL,
                                       &actionWeight, NULL)) {
        return 0;
    }

    out->action_weight = actionWeight;
    out->active_channels = 0;
    out->weight_sum_a = 0.0f;
    out->weight_sum_b = 0.0f;

    for (int i = 0; i < MAX_ANIM_CHANNELS; i++) {
        out->frame_indices[i] = frameInfo[i].index;
        out->frame_weights[i] = frameInfo[i].weight;
        out->frame_times[i]   = frameInfo[i].time;

        if (frameInfo[i].weight > 0.0f) {
            out->active_channels++;

            if (i < ANIM_BLEND_SPLIT) {
                out->weight_sum_a += frameInfo[i].weight;
            } else {
                out->weight_sum_b += frameInfo[i].weight;
            }
        }
    }

    return 1;
}

/* ===================================================================
 *  Godot_AnimBlend_Validate
 * ================================================================ */

extern "C"
int Godot_AnimBlend_Validate(const AnimBlendInput *input,
                             AnimBlendValidation *result)
{
    if (!input || !result) return 0;

    memset(result, 0, sizeof(*result));

    result->valid = 1;

    /* Count active channels per group */
    for (int i = 0; i < MAX_ANIM_CHANNELS; i++) {
        if (input->frame_weights[i] <= 0.0f) {
            result->zero_weight_channels++;
        } else {
            if (i < ANIM_BLEND_SPLIT) {
                result->group_a_active++;
            } else {
                result->group_b_active++;
            }
        }
    }

    /* Compute effective total weight.
     * The engine blends:
     *   final = group_a * (1 - actionWeight) + group_b * actionWeight
     * Each group's channels are individually weighted, so effective
     * total = sum_a * (1 - actionWeight) + sum_b * actionWeight.
     */
    float sum_a = input->weight_sum_a;
    float sum_b = input->weight_sum_b;
    float aw = input->action_weight;

    /* Clamp actionWeight for computation */
    if (aw < 0.0f) aw = 0.0f;
    if (aw > 1.0f) aw = 1.0f;

    /* If only one group is active, actionWeight doesn't matter */
    if (sum_a > 0.0f && sum_b <= 0.0f) {
        result->total_weight = sum_a;
    } else if (sum_b > 0.0f && sum_a <= 0.0f) {
        result->total_weight = sum_b;
    } else {
        result->total_weight = sum_a * (1.0f - aw) + sum_b * aw;
    }

    /* Validate: at least one channel should be active */
    if (result->group_a_active == 0 && result->group_b_active == 0) {
        result->valid = 0;
    }

    /* Validate: actionWeight should be in [0, 1] */
    if (input->action_weight < -0.001f || input->action_weight > 1.001f) {
        result->valid = 0;
    }

    /* Validate: group weights should sum to approximately 1.0
     * (tolerance for floating-point imprecision) */
    if (sum_a > 0.0f && fabsf(sum_a - 1.0f) > 0.1f) {
        result->valid = 0;
    }
    if (sum_b > 0.0f && fabsf(sum_b - 1.0f) > 0.1f) {
        result->valid = 0;
    }

    return result->valid;
}

/* ===================================================================
 *  Godot_AnimBlend_ComputeBones
 * ================================================================ */

extern "C"
int Godot_AnimBlend_ComputeBones(void *tiki_ptr,
                                 int entity_number,
                                 const AnimBlendInput *input,
                                 const int *bone_tag,
                                 const float *bone_quat,
                                 void *out_bones,
                                 int max_bones,
                                 int *out_bone_count)
{
    if (!tiki_ptr || !input || !out_bones || max_bones <= 0) return 0;

    /* Reconstruct frameInfo_t array from AnimBlendInput */
    frameInfo_t frameInfo[MAX_FRAMEINFOS];
    for (int i = 0; i < MAX_FRAMEINFOS; i++) {
        frameInfo[i].index  = input->frame_indices[i];
        frameInfo[i].weight = input->frame_weights[i];
        frameInfo[i].time   = input->frame_times[i];
    }

    /* Delegate to the engine's bone computation pipeline.
     * Godot_Skel_PrepareBones → ri.TIKI_SetPoseInternal (handles all
     * channel blending + actionWeight) → ri.GetFrameInternal → bone cache.
     */
    int boneCount = 0;
    void *bones = Godot_Skel_PrepareBones(tiki_ptr, entity_number,
                                           frameInfo, bone_tag,
                                           bone_quat,
                                           input->action_weight,
                                           &boneCount);
    if (!bones) return 0;

    /* Copy into caller's buffer, clamped to max_bones */
    int copyCount = boneCount;
    if (copyCount > max_bones) {
        copyCount = max_bones;
    }

    /* skelBoneCache_t is SKEL_BONE_CACHE_SIZE bytes: float offset[4] + float matrix[3][4] */
    memcpy(out_bones, bones, (size_t)copyCount * SKEL_BONE_CACHE_SIZE);

    free(bones);

    if (out_bone_count) *out_bone_count = copyCount;
    return 1;
}

/* ===================================================================
 *  Godot_AnimBlend_GetActiveChannelCount
 * ================================================================ */

extern "C"
int Godot_AnimBlend_GetActiveChannelCount(int entity_index)
{
    AnimBlendInput blend;
    if (!Godot_AnimBlend_ExtractFromEntity(entity_index, &blend)) {
        return 0;
    }
    return blend.active_channels;
}

/* ===================================================================
 *  Godot_AnimBlend_DebugLogEntity
 * ================================================================ */

extern "C"
void Godot_AnimBlend_DebugLogEntity(int entity_index)
{
    AnimBlendInput blend;
    if (!Godot_AnimBlend_ExtractFromEntity(entity_index, &blend)) {
        Com_Printf("[AnimBlend] Entity %d: no TIKI or invalid index\n",
                   entity_index);
        return;
    }

    AnimBlendValidation val;
    Godot_AnimBlend_Validate(&blend, &val);

    Com_Printf("[AnimBlend] Entity %d: actionWeight=%.3f  "
               "active=%d  groupA=%d(sum=%.3f)  groupB=%d(sum=%.3f)  "
               "totalWeight=%.3f  valid=%d\n",
               entity_index,
               blend.action_weight,
               blend.active_channels,
               val.group_a_active, blend.weight_sum_a,
               val.group_b_active, blend.weight_sum_b,
               val.total_weight,
               val.valid);

    /* Log individual active channels */
    for (int i = 0; i < MAX_ANIM_CHANNELS; i++) {
        if (blend.frame_weights[i] > 0.0f) {
            Com_Printf("  ch[%d]: frame=%d  time=%.3f  weight=%.3f  (%s)\n",
                       i,
                       blend.frame_indices[i],
                       blend.frame_times[i],
                       blend.frame_weights[i],
                       (i < ANIM_BLEND_SPLIT) ? "groupA" : "groupB");
        }
    }
}
