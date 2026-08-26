/*
 * godot_lightmap_styles_accessors.c — C accessor for lightstyle configstrings.
 *
 * Reads light style pattern strings from the server's configstring array
 * (sv.configstrings[CS_LIGHTSTYLES + index]).  These patterns are set by
 * the server game via SV_SetLightStyle() and encode per-frame brightness
 * values as character sequences where 'a' = 0 (off) through 'z' = 25 (max).
 *
 * This file is compiled as C within the openmohaa source tree where
 * server.h is available.  MoHAARunner.cpp (and godot_lightmap_styles.cpp)
 * call these functions via extern "C".
 */

#include "../server/server.h"

/*
 * Godot_LightStyle_GetPattern — Return the raw pattern string for a
 *   light style index (0 to MAX_LIGHTSTYLES-1).
 *
 * Returns NULL if the index is out of range or the configstring is empty.
 * The returned pointer is into the server's configstring storage and must
 * not be freed or modified by the caller.
 */
const char *Godot_LightStyle_GetPattern(int style_index) {
    if (style_index < 0 || style_index >= MAX_LIGHTSTYLES) {
        return NULL;
    }

    if (sv.state == SS_DEAD) {
        return NULL;
    }

    {
        const char *s = sv.configstrings[CS_LIGHTSTYLES + style_index];
        if (!s || !s[0]) {
            return NULL;
        }
        return s;
    }
}

/*
 * Godot_LightStyle_GetValue — Return the current brightness for a given
 *   light style as an integer 0-255.
 *
 * Evaluates the pattern string at the current server time.  Each character
 * in the pattern represents a brightness step: 'a' = 0, 'b' = ~10, ...,
 * 'm' = ~128 (normal), 'z' = 255.  The pattern repeats at 10 Hz
 * (100 ms per character), matching the engine's LIGHT_STYLE_FRAMETIME.
 *
 * Style 0 with no explicit pattern defaults to full brightness (255).
 */
int Godot_LightStyle_GetValue(int style_index) {
    const char *pattern;
    int len, idx, val;

    pattern = Godot_LightStyle_GetPattern(style_index);

    if (!pattern) {
        /* Style 0 defaults to full brightness; others default to off. */
        return (style_index == 0) ? 255 : 0;
    }

    len = (int)strlen(pattern);
    if (len == 0) {
        return (style_index == 0) ? 255 : 0;
    }

    if (len == 1) {
        /* Single character — constant brightness. */
        val = (pattern[0] - 'a') * 255 / 25;
        if (val < 0)   val = 0;
        if (val > 255)  val = 255;
        return val;
    }

    /* Animate through the pattern at ~10 Hz using server time. */
    {
        int time_ms = (sv.state != SS_DEAD) ? svs.time : 0;
        idx = (time_ms / 100) % len;
        val = (pattern[idx] - 'a') * 255 / 25;
        if (val < 0)   val = 0;
        if (val > 255)  val = 255;
        return val;
    }
}
