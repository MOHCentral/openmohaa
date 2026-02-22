/*
 * godot_screen_effects.h — Screen-space post-processing effects.
 *
 * Provides fullscreen colour overlays for damage flash, underwater tint,
 * flash-bang white-out, and a temporary camera pitch offset for pain
 * flinch.  All effects use a shared CanvasLayer with independent
 * ColorRect nodes so they can stack and fade independently.
 *
 * Integration: MoHAARunner calls Godot_ScreenFX_Init() once after
 * scene setup, Godot_ScreenFX_Update() every frame, and the trigger
 * functions when the engine signals the corresponding events (via
 * cgame view damage state or content detection).
 *
 * Phase 227 — Screen Post-Processing Effects.
 */

#ifndef GODOT_SCREEN_EFFECTS_H
#define GODOT_SCREEN_EFFECTS_H

#ifdef __cplusplus

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/camera3d.hpp>

/*
 * Godot_ScreenFX_Init — create the overlay CanvasLayer and ColorRects.
 *
 * @param parent  Scene node to attach the CanvasLayer to (MoHAARunner).
 */
void Godot_ScreenFX_Init(godot::Node *parent);

/*
 * Godot_ScreenFX_Update — per-frame update of all active effects.
 *
 * Fades damage flash and flash-bang overlays, oscillates underwater
 * tint alpha, and interpolates pain flinch back to zero.
 *
 * @param delta   Frame delta time in seconds.
 * @param camera  Camera3D to apply pain flinch pitch offset to.
 */
void Godot_ScreenFX_Update(float delta, godot::Camera3D *camera);

/*
 * Godot_ScreenFX_Shutdown — remove overlay nodes and reset state.
 */
void Godot_ScreenFX_Shutdown(void);

/*
 * Godot_ScreenFX_DamageFlash — trigger a red damage flash overlay.
 *
 * Multiple hits stack additively up to a maximum alpha of 0.6.
 *
 * @param intensity  Flash strength in the range 0.0–1.0.
 */
void Godot_ScreenFX_DamageFlash(float intensity);

/*
 * Godot_ScreenFX_UnderwaterTint — toggle the underwater colour overlay.
 *
 * When active a blue-green tint with sine-wave alpha oscillation is
 * shown.  Toggling off hides the overlay immediately.
 *
 * @param active  true to enable, false to disable.
 */
void Godot_ScreenFX_UnderwaterTint(bool active);

/*
 * Godot_ScreenFX_FlashBang — trigger a white flash-bang overlay.
 *
 * Fades to transparent over 2.0 seconds.
 *
 * @param intensity  Flash strength in the range 0.0–1.0.
 */
void Godot_ScreenFX_FlashBang(float intensity);

/*
 * Godot_ScreenFX_PainFlinch — apply a temporary camera pitch offset.
 *
 * The offset is interpolated back to zero over 0.2 seconds.
 * Only rotation is affected — camera position is unchanged.
 *
 * @param pitch_offset  Pitch deflection in radians.
 */
void Godot_ScreenFX_PainFlinch(float pitch_offset);

#endif /* __cplusplus */

#endif /* GODOT_SCREEN_EFFECTS_H */
