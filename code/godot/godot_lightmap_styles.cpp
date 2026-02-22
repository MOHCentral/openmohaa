/*
 * godot_lightmap_styles.cpp — Lightmap style manager for BSP rendering.
 *
 * Tracks brightness for up to MAX_LIGHTSTYLES_TOTAL (64) light styles,
 * reads configstring patterns from the engine each frame via the C
 * accessor layer, and provides smooth brightness values for the BSP
 * lightmap rendering pipeline.
 *
 * MOHAA light-style pattern format:
 *   Each character in the pattern string encodes a brightness step:
 *     'a' = 0.0 (off),  'm' ≈ 0.48 (normal/mid),  'z' = 1.0 (full)
 *   The pattern advances at 10 Hz (100 ms per character) and repeats.
 *
 * Integration notes for BSP lightmap rendering (godot_bsp_mesh / shader):
 *   - Each BSP surface has lightmapStyles[4] with up to 4 style indices.
 *   - Style 0 = always full brightness (1.0).
 *   - Style 255 = unused slot (returns 0.0).
 *   - Styles 1–63 = switchable, brightness from this module.
 *   - Final surface light = SUM of (lightmap_i * brightness_i) for each
 *     active style slot.
 *   - For StandardMaterial3D: modulate lightmap texture brightness.
 *   - For ShaderMaterial: set `lightstyle_brightness` uniform.
 */

#include "godot_lightmap_styles.h"

#include <cstring>
#include <cmath>

/* -------------------------------------------------------------------
 *  Constants
 * ---------------------------------------------------------------- */

/* Total tracked styles: MAX_LIGHTSTYLES (32) × 2 = 64, matching the
   cgame's cg_lightstyle[] array sizing convention. */
#define MAX_LIGHTSTYLES_TOTAL 64

/* Interpolation speed (units per second).  Higher = snappier transitions.
   A value of 10.0 gives ~0.1 s transition per brightness step, which
   matches the 10 Hz pattern advance rate. */
static const float INTERP_SPEED = 10.0f;

/* Style index that means "unused slot" in BSP surface data. */
#define LIGHTSTYLE_UNUSED 255

/* -------------------------------------------------------------------
 *  Internal state
 * ---------------------------------------------------------------- */

static struct {
    /* Target brightness read from the engine configstring each frame. */
    float target[MAX_LIGHTSTYLES_TOTAL];

    /* Current (interpolated) brightness presented to the renderer. */
    float current[MAX_LIGHTSTYLES_TOTAL];

    /* Non-zero once Init has been called. */
    int   initialised;
} ls_state;

/* -------------------------------------------------------------------
 *  Public API
 * ---------------------------------------------------------------- */

void Godot_LightStyles_Init(void)
{
    std::memset(&ls_state, 0, sizeof(ls_state));

    /* Style 0 defaults to full brightness. */
    ls_state.target[0]  = 1.0f;
    ls_state.current[0] = 1.0f;

    ls_state.initialised = 1;
}

void Godot_LightStyles_Update(float delta)
{
    int i;

    if (!ls_state.initialised) {
        return;
    }

    /* Read target brightness from the engine for each style. */
    for (i = 0; i < MAX_LIGHTSTYLES_TOTAL; i++) {
        int raw = Godot_LightStyle_GetValue(i);
        ls_state.target[i] = (float)raw / 255.0f;
    }

    /* Smoothly interpolate current brightness towards target. */
    for (i = 0; i < MAX_LIGHTSTYLES_TOTAL; i++) {
        float diff = ls_state.target[i] - ls_state.current[i];

        if (std::fabs(diff) < 0.001f) {
            /* Close enough — snap to target. */
            ls_state.current[i] = ls_state.target[i];
        } else {
            float step = INTERP_SPEED * delta;
            if (step > 1.0f) {
                step = 1.0f;
            }
            ls_state.current[i] += diff * step;
        }

        /* Clamp to valid range. */
        if (ls_state.current[i] < 0.0f) {
            ls_state.current[i] = 0.0f;
        } else if (ls_state.current[i] > 1.0f) {
            ls_state.current[i] = 1.0f;
        }
    }
}

void Godot_LightStyles_Shutdown(void)
{
    std::memset(&ls_state, 0, sizeof(ls_state));
}

float Godot_LightStyles_GetBrightness(int style_index)
{
    if (!ls_state.initialised) {
        /* Not yet initialised — return sensible defaults. */
        return (style_index == 0) ? 1.0f : 0.0f;
    }

    /* Unused slot marker. */
    if (style_index == LIGHTSTYLE_UNUSED) {
        return 0.0f;
    }

    if (style_index < 0 || style_index >= MAX_LIGHTSTYLES_TOTAL) {
        return 0.0f;
    }

    return ls_state.current[style_index];
}
