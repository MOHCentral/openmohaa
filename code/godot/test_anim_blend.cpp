/*
 * test_anim_blend.cpp - Unit tests for godot_anim_blend.cpp
 *
 * This test file mocks the necessary engine dependencies to test
 * Godot_AnimBlend_Validate in isolation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h> // For va_list

#include "../qcommon/q_shared.h"

// Stub functions required by godot_anim_blend.cpp
// These must be declared extern "C" to match the declarations in godot_anim_blend.cpp

extern "C" {

// Stub for Godot_Renderer_GetEntityCount
int Godot_Renderer_GetEntityCount(void) {
    return 0;
}

// Stub for Godot_Renderer_GetEntityAnim
int Godot_Renderer_GetEntityAnim(int index,
                                   void **outTiki,
                                   int *outEntityNumber,
                                   void *outFrameInfo,
                                   int *outBoneTag,
                                   float *outBoneQuat,
                                   float *outActionWeight,
                                   float *outScale) {
    return 0;
}

// Stub for Godot_Skel_PrepareBones
void *Godot_Skel_PrepareBones(void *tikiPtr, int entityNumber,
                               const frameInfo_t *frameInfo,
                               const int *bone_tag,
                               const float *bone_quat,
                               float actionWeight,
                               int *outBoneCount) {
    return NULL;
}

// Stub for Com_Printf
// godot_anim_blend.cpp uses Com_Printf for logging.
void Com_Printf( const char *msg, ... ) {
    va_list argptr;
    va_start(argptr, msg);
    vprintf(msg, argptr);
    va_end(argptr);
}

} // extern "C"

// Include the source file directly to test static/internal functions if needed,
// and to compile it as part of this unit test.
#include "godot_anim_blend.cpp"

// Helper to run a test
#define RUN_TEST(name) \
    printf("Running %s... ", #name); \
    if (name()) { \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
        failed++; \
    }

// Test function signature
typedef int (*TestFunc)(void);

// --- Test Cases ---

int test_null_pointers() {
    // Should return 0 if input or result is NULL
    AnimBlendInput input;
    AnimBlendValidation result;

    if (Godot_AnimBlend_Validate(NULL, &result) != 0) return 0;
    if (Godot_AnimBlend_Validate(&input, NULL) != 0) return 0;

    return 1;
}

int test_single_active_channel_group_a() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    // Setup valid input for channel 0 (Group A)
    input.frame_weights[0] = 1.0f;
    input.weight_sum_a = 1.0f;
    input.weight_sum_b = 0.0f;
    input.action_weight = 0.0f; // All weight on A
    input.active_channels = 1;

    if (Godot_AnimBlend_Validate(&input, &result) != 1) {
        printf("Expected valid, got invalid\n");
        return 0;
    }

    if (result.group_a_active != 1) return 0;
    if (result.group_b_active != 0) return 0;
    if (fabs(result.total_weight - 1.0f) > 0.001f) return 0;

    return 1;
}

int test_single_active_channel_group_b() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    // Setup valid input for channel 8 (Group B, since split is 8)
    int channel = ANIM_BLEND_SPLIT; // 8
    input.frame_weights[channel] = 1.0f;
    input.weight_sum_a = 0.0f;
    input.weight_sum_b = 1.0f;
    input.action_weight = 1.0f; // All weight on B
    input.active_channels = 1;

    if (Godot_AnimBlend_Validate(&input, &result) != 1) {
        printf("Expected valid, got invalid\n");
        return 0;
    }

    if (result.group_a_active != 0) return 0;
    if (result.group_b_active != 1) return 0;
    if (fabs(result.total_weight - 1.0f) > 0.001f) return 0;

    return 1;
}

int test_mixed_channels() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    // 0.5 on A, 0.5 on B. actionWeight 0.5
    // Effective weight = 1.0 * (1-0.5) + 1.0 * 0.5 = 0.5 + 0.5 = 1.0
    input.frame_weights[0] = 1.0f; // One channel fully active in A
    input.frame_weights[ANIM_BLEND_SPLIT] = 1.0f; // One channel fully active in B

    input.weight_sum_a = 1.0f;
    input.weight_sum_b = 1.0f;
    input.action_weight = 0.5f;
    input.active_channels = 2;

    if (Godot_AnimBlend_Validate(&input, &result) != 1) {
        printf("Expected valid, got invalid\n");
        return 0;
    }

    if (fabs(result.total_weight - 1.0f) > 0.001f) return 0;

    return 1;
}

int test_invalid_weight_sum() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    // Group A sum is 0.5 (should be ~1.0 if active)
    input.frame_weights[0] = 0.5f;
    input.weight_sum_a = 0.5f;
    input.weight_sum_b = 0.0f;
    input.action_weight = 0.0f;
    input.active_channels = 1;

    if (Godot_AnimBlend_Validate(&input, &result) != 0) {
        printf("Expected invalid (sum=0.5), got valid\n");
        return 0;
    }

    return 1;
}

int test_invalid_action_weight() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    input.frame_weights[0] = 1.0f;
    input.weight_sum_a = 1.0f;
    input.active_channels = 1;

    // Invalid action weight > 1.0
    input.action_weight = 1.5f;

    if (Godot_AnimBlend_Validate(&input, &result) != 0) {
        printf("Expected invalid (actionWeight=1.5), got valid\n");
        return 0;
    }

    // Invalid action weight < 0.0
    input.action_weight = -0.5f;
    if (Godot_AnimBlend_Validate(&input, &result) != 0) {
        printf("Expected invalid (actionWeight=-0.5), got valid\n");
        return 0;
    }

    return 1;
}

int test_no_active_channels() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    input.active_channels = 0;
    input.weight_sum_a = 0.0f;
    input.weight_sum_b = 0.0f;

    if (Godot_AnimBlend_Validate(&input, &result) != 0) {
        printf("Expected invalid (no active channels), got valid\n");
        return 0;
    }

    return 1;
}

int test_zero_weight_channels_count() {
    AnimBlendInput input;
    memset(&input, 0, sizeof(input));
    AnimBlendValidation result;

    // Set 1 channel to > 0.
    input.frame_weights[0] = 1.0f;

    // Set valid sums to pass validation
    input.weight_sum_a = 1.0f;
    input.active_channels = 1;

    Godot_AnimBlend_Validate(&input, &result);

    // 1 active, 15 zero (MAX_ANIM_CHANNELS is 16)
    int expected_zeros = MAX_ANIM_CHANNELS - 1;

    if (result.zero_weight_channels != expected_zeros) {
        printf("Expected %d zero channels, got %d\n", expected_zeros, result.zero_weight_channels);
        return 0;
    }

    return 1;
}

int main() {
    int failed = 0;

    RUN_TEST(test_null_pointers);
    RUN_TEST(test_single_active_channel_group_a);
    RUN_TEST(test_single_active_channel_group_b);
    RUN_TEST(test_mixed_channels);
    RUN_TEST(test_invalid_weight_sum);
    RUN_TEST(test_invalid_action_weight);
    RUN_TEST(test_no_active_channels);
    RUN_TEST(test_zero_weight_channels_count);

    if (failed > 0) {
        printf("\n%d tests failed.\n", failed);
        return 1;
    } else {
        printf("\nAll tests passed.\n");
        return 0;
    }
}
