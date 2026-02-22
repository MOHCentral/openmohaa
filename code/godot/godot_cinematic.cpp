/*
 * godot_cinematic.cpp — RoQ cinematic playback stub / skip-hint overlay.
 *
 * When the engine plays a RoQ cinematic, GR_DrawStretchRaw() in
 * godot_renderer.c sets gr_cin_active = 1.  This module detects that
 * flag (via the existing Godot_Renderer_IsCinematicActive accessor)
 * and shows a fullscreen overlay with "Press ESC to skip" text that
 * pulses in and out.
 *
 * The overlay sits on CanvasLayer z_index 200 — above MoHAARunner's
 * cinematic frame display (layer 11) and HUD — so the hint is always
 * visible during a cinematic.
 *
 * Phase 56 — Cinematic Playback Stub.
 */

#include "godot_cinematic.h"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;

/* ── Renderer accessor (defined in godot_renderer.c) ── */
extern "C" {
    int  Godot_Renderer_IsCinematicActive(void);
}

/* ── Static state ── */
static CanvasLayer *s_cin_layer   = nullptr;
static ColorRect   *s_cin_bg      = nullptr;
static Label       *s_cin_label   = nullptr;
static bool         s_was_active  = false;
static float        s_pulse_time  = 0.0f;

/* ── Constants ── */
static constexpr int   CIN_LAYER_Z      = 200;
static constexpr float PULSE_SPEED      = 2.5f;   /* radians per second */
static constexpr float ALPHA_MIN        = 0.3f;
static constexpr float ALPHA_MAX        = 1.0f;
static constexpr int   LABEL_FONT_SIZE  = 20;

/* ===================================================================
 *  Godot_Cinematic_Init
 * ================================================================ */
void Godot_Cinematic_Init(Node *parent) {
    if (!parent) return;

    /* Tear down any previous instance */
    Godot_Cinematic_Shutdown();

    /* ── CanvasLayer ── */
    s_cin_layer = memnew(CanvasLayer);
    s_cin_layer->set_name("CinematicSkipLayer");
    s_cin_layer->set_layer(CIN_LAYER_Z);
    parent->add_child(s_cin_layer);

    /* ── Fullscreen black background ── */
    s_cin_bg = memnew(ColorRect);
    s_cin_bg->set_name("CinematicBlackBG");
    s_cin_bg->set_anchors_preset(Control::PRESET_FULL_RECT);
    s_cin_bg->set_color(Color(0.0f, 0.0f, 0.0f, 1.0f));
    s_cin_layer->add_child(s_cin_bg);

    /* ── "Press ESC to skip" label ── */
    s_cin_label = memnew(Label);
    s_cin_label->set_name("CinematicSkipLabel");
    s_cin_label->set_text("Press ESC to skip");
    s_cin_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    s_cin_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);

    /* Position: centred horizontally, near the bottom of the screen */
    s_cin_label->set_anchors_preset(Control::PRESET_BOTTOM_WIDE);
    s_cin_label->set_anchor(SIDE_TOP,    0.9f);
    s_cin_label->set_anchor(SIDE_BOTTOM, 1.0f);
    s_cin_label->set_anchor(SIDE_LEFT,   0.0f);
    s_cin_label->set_anchor(SIDE_RIGHT,  1.0f);
    s_cin_label->set_offset(SIDE_TOP,    0.0f);
    s_cin_label->set_offset(SIDE_BOTTOM, 0.0f);
    s_cin_label->set_offset(SIDE_LEFT,   0.0f);
    s_cin_label->set_offset(SIDE_RIGHT,  0.0f);

    s_cin_label->add_theme_font_size_override("font_size", LABEL_FONT_SIZE);
    s_cin_label->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f, 1.0f));

    s_cin_layer->add_child(s_cin_label);

    /* Hidden by default */
    s_cin_layer->set_visible(false);

    s_was_active = false;
    s_pulse_time = 0.0f;

    UtilityFunctions::print("[MoHAA] Cinematic skip-hint overlay initialised (Phase 56).");
}

/* ===================================================================
 *  Godot_Cinematic_Update
 * ================================================================ */
void Godot_Cinematic_Update(float delta) {
    if (!s_cin_layer) return;

    bool active = Godot_Renderer_IsCinematicActive() != 0;

    if (active) {
        if (!s_was_active) {
            /* Cinematic just started — show the overlay */
            s_cin_layer->set_visible(true);
            s_pulse_time = 0.0f;
            s_was_active = true;
        }

        /* Pulse the label alpha: smoothly oscillate between ALPHA_MIN and ALPHA_MAX */
        s_pulse_time += delta * PULSE_SPEED;
        float alpha = ALPHA_MIN + (ALPHA_MAX - ALPHA_MIN) * 0.5f *
                      (1.0f + sinf(s_pulse_time));

        if (s_cin_label) {
            s_cin_label->add_theme_color_override(
                "font_color", Color(1.0f, 1.0f, 1.0f, alpha));
        }
    } else if (s_was_active) {
        /* Cinematic ended — hide the overlay */
        s_cin_layer->set_visible(false);
        s_was_active = false;
        s_pulse_time = 0.0f;
    }
}

/* ===================================================================
 *  Godot_Cinematic_Shutdown
 * ================================================================ */
void Godot_Cinematic_Shutdown(void) {
    if (s_cin_layer) {
        /* Remove from parent and free the node tree */
        if (s_cin_layer->get_parent()) {
            s_cin_layer->get_parent()->remove_child(s_cin_layer);
        }
        s_cin_layer->queue_free();
        s_cin_layer = nullptr;
        s_cin_bg    = nullptr;
        s_cin_label = nullptr;
    }
    s_was_active = false;
    s_pulse_time = 0.0f;
}

/* ===================================================================
 *  Godot_Cinematic_IsActive
 * ================================================================ */
bool Godot_Cinematic_IsActive(void) {
    return Godot_Renderer_IsCinematicActive() != 0;
}
