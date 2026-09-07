/*
 * godot_lightmap_styles.h — Public API for BSP lightmap style management.
 *
 * MOHAA maps can have up to 4 lightmap styles per surface, controlled by
 * configstrings.  Light switches, flickering lights, and pulsing lights
 * use alternate lightmap styles that the map compiler baked into the BSP.
 *
 * This module tracks brightness for up to MAX_LIGHTSTYLES * 2 (64) style
 * slots.  Style 0 is always full brightness.  Styles 1-31 are switchable
 * (set by the server game).  Style 255 means "unused slot" in the BSP
 * surface data.
 *
 * Integration point for BSP lightmap rendering:
 *   - For StandardMaterial3D: modulate the lightmap texture brightness by
 *     the multiplier returned from Godot_LightStyles_GetBrightness().
 *   - For ShaderMaterial: set a `lightstyle_brightness` uniform with the
 *     value from Godot_LightStyles_GetBrightness().
 *   - The final surface light is the sum of all active style contributions.
 */

#ifndef GODOT_LIGHTMAP_STYLES_H
#define GODOT_LIGHTMAP_STYLES_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_LightStyles_Init — Initialise the lightmap style manager.
 *
 * Resets all tracked styles to default values.  Style 0 defaults to
 * full brightness (1.0); all others default to 0.0.
 * Call once at map load time.
 */
void Godot_LightStyles_Init(void);

/*
 * Godot_LightStyles_Update — Poll current style brightness from the engine.
 *
 * Reads each style's configstring pattern via the C accessor and updates
 * the internal brightness cache.  Applies simple interpolation for smooth
 * transitions between brightness steps.
 *
 * @param delta  Frame delta time in seconds.
 *
 * Call once per frame from the main Godot process loop.
 */
void Godot_LightStyles_Update(float delta);

/*
 * Godot_LightStyles_Shutdown — Release any resources held by the manager.
 *
 * Resets all state.  Safe to call multiple times.
 */
void Godot_LightStyles_Shutdown(void);

/*
 * Godot_LightStyles_GetBrightness — Return the current brightness for a
 *   light style as a float in the range [0.0, 1.0].
 *
 * @param style_index  Style index (0–63).  Index 255 is treated as unused
 *                     and returns 0.0.
 *
 * @return Brightness multiplier: 0.0 = fully off, 1.0 = fully on.
 */
float Godot_LightStyles_GetBrightness(int style_index);

/* ---------------------------------------------------------------
 *  C accessor declarations (implemented in
 *  godot_lightmap_styles_accessors.c).
 * --------------------------------------------------------------- */

/*
 * Godot_LightStyle_GetPattern — Return the raw pattern string for a
 *   light style from the server's configstring array.
 *
 * Returns NULL if the index is out of range or the configstring is empty.
 */
const char *Godot_LightStyle_GetPattern(int style_index);

/*
 * Godot_LightStyle_GetValue — Return the current brightness for a given
 *   light style as an integer in the range [0, 255].
 */
int Godot_LightStyle_GetValue(int style_index);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_LIGHTMAP_STYLES_H */
