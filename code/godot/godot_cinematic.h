/*
 * godot_cinematic.h — RoQ cinematic playback stub for the OpenMoHAA GDExtension.
 *
 * Detects when the engine is playing a RoQ cinematic video (via the
 * gr_cin_active flag set by GR_DrawStretchRaw in godot_renderer.c) and
 * displays a fullscreen black overlay with a "Press ESC to skip" hint.
 *
 * The actual RoQ frame decoding and display is handled by MoHAARunner's
 * cinematic bridge (Phase 11).  This module provides the skip-hint overlay
 * on a high z-index CanvasLayer so it sits above everything else.
 *
 * Integration: MoHAARunner calls Godot_Cinematic_Init() once during setup
 * and Godot_Cinematic_Update() each frame.
 *
 * Phase 56 — Cinematic Playback Stub.
 */

#ifndef GODOT_CINEMATIC_H
#define GODOT_CINEMATIC_H

#ifdef __cplusplus

#include <godot_cpp/classes/node.hpp>

/*
 * Godot_Cinematic_Init — create the cinematic overlay nodes.
 *
 * Builds a CanvasLayer (z_index 200) with a fullscreen black ColorRect
 * and a "Press ESC to skip" label centred at the bottom.  The overlay
 * starts hidden and is shown/hidden automatically by Update().
 *
 * @param parent  Godot Node to parent the overlay CanvasLayer to.
 */
void Godot_Cinematic_Init(godot::Node *parent);

/*
 * Godot_Cinematic_Update — per-frame update.
 *
 * Checks the renderer's cinematic-active flag.  When a cinematic is
 * playing, makes the overlay visible and pulses the label alpha.
 * When the cinematic ends, hides the overlay.
 *
 * @param delta  Frame time in seconds.
 */
void Godot_Cinematic_Update(float delta);

/*
 * Godot_Cinematic_Shutdown — remove overlay nodes and free resources.
 */
void Godot_Cinematic_Shutdown(void);

/*
 * Godot_Cinematic_IsActive — query whether a cinematic is currently playing.
 *
 * @return true if the renderer's cinematic flag is set, false otherwise.
 */
bool Godot_Cinematic_IsActive(void);

#endif /* __cplusplus */

#endif /* GODOT_CINEMATIC_H */
