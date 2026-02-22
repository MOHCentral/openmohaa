/*
 * godot_screen_effects.cpp — Screen-space post-processing effects.
 *
 * Manages fullscreen colour overlays for damage flash (red), underwater
 * tint (blue-green), flash-bang white-out, and a temporary camera pitch
 * offset for pain flinch.
 *
 * All overlays live on a single CanvasLayer (z_index 100) using separate
 * ColorRect nodes so they stack and fade independently.
 *
 * Phase 227 — Screen Post-Processing Effects.
 */

#include "godot_screen_effects.h"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;

/* ── Constants ── */
static constexpr float DAMAGE_FADE_TIME    = 0.3f;   /* seconds to fade out */
static constexpr float DAMAGE_ALPHA_SCALE  = 0.4f;   /* intensity → alpha */
static constexpr float DAMAGE_ALPHA_CAP    = 0.6f;   /* max stacked alpha */

static constexpr float UNDERWATER_ALPHA_MIN = 0.25f;
static constexpr float UNDERWATER_ALPHA_MAX = 0.35f;
static constexpr float UNDERWATER_OSC_FREQ  = 0.5f;  /* Hz */

static constexpr float FLASH_FADE_TIME     = 2.0f;   /* seconds to fade out */

static constexpr float FLINCH_RECOVER_TIME = 0.2f;   /* seconds to return to 0 */

/* ── Static state ── */
static CanvasLayer *s_canvas        = nullptr;
static ColorRect   *s_damage_rect   = nullptr;
static ColorRect   *s_underwater_rect = nullptr;
static ColorRect   *s_flash_rect    = nullptr;

static float s_damage_alpha         = 0.0f;
static float s_damage_alpha_start   = 0.0f;  /* alpha at trigger time */
static float s_damage_timer         = 0.0f;

static bool  s_underwater_active    = false;
static float s_underwater_time      = 0.0f;

static float s_flash_alpha          = 0.0f;
static float s_flash_alpha_start    = 0.0f;  /* alpha at trigger time */
static float s_flash_timer          = 0.0f;

static float s_flinch_offset        = 0.0f;  /* current pitch offset (rad) */
static float s_flinch_applied       = 0.0f;  /* offset applied last frame */
static float s_flinch_timer         = 0.0f;
static float s_flinch_start         = 0.0f;  /* offset at trigger time */

/* ===================================================================
 *  Helper — create a fullscreen ColorRect anchored to fill the viewport
 * ================================================================ */
static ColorRect *create_fullscreen_rect(CanvasLayer *layer, const char *name) {
    ColorRect *rect = memnew(ColorRect);
    rect->set_name(name);
    rect->set_anchors_preset(Control::PRESET_FULL_RECT);
    rect->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    rect->set_color(Color(0.0f, 0.0f, 0.0f, 0.0f));
    layer->add_child(rect);
    return rect;
}

/* ===================================================================
 *  Init
 * ================================================================ */
void Godot_ScreenFX_Init(Node *parent) {
    if (!parent) {
        return;
    }

    /* Clean up any prior state */
    Godot_ScreenFX_Shutdown();

    s_canvas = memnew(CanvasLayer);
    s_canvas->set_name("ScreenFXLayer");
    s_canvas->set_layer(100);
    parent->add_child(s_canvas);

    s_damage_rect     = create_fullscreen_rect(s_canvas, "DamageFlash");
    s_underwater_rect = create_fullscreen_rect(s_canvas, "UnderwaterTint");
    s_flash_rect      = create_fullscreen_rect(s_canvas, "FlashBang");

    /* Reset effect state */
    s_damage_alpha       = 0.0f;
    s_damage_alpha_start = 0.0f;
    s_damage_timer       = 0.0f;
    s_underwater_active  = false;
    s_underwater_time    = 0.0f;
    s_flash_alpha        = 0.0f;
    s_flash_alpha_start  = 0.0f;
    s_flash_timer        = 0.0f;
    s_flinch_offset      = 0.0f;
    s_flinch_applied     = 0.0f;
    s_flinch_timer       = 0.0f;
    s_flinch_start       = 0.0f;
}

/* ===================================================================
 *  Shutdown
 * ================================================================ */
void Godot_ScreenFX_Shutdown(void) {
    if (s_canvas) {
        s_canvas->queue_free();
        s_canvas        = nullptr;
        s_damage_rect   = nullptr;
        s_underwater_rect = nullptr;
        s_flash_rect    = nullptr;
    }

    s_damage_alpha       = 0.0f;
    s_damage_alpha_start = 0.0f;
    s_damage_timer       = 0.0f;
    s_underwater_active  = false;
    s_underwater_time    = 0.0f;
    s_flash_alpha        = 0.0f;
    s_flash_alpha_start  = 0.0f;
    s_flash_timer        = 0.0f;
    s_flinch_offset      = 0.0f;
    s_flinch_applied     = 0.0f;
    s_flinch_timer       = 0.0f;
    s_flinch_start       = 0.0f;
}

/* ===================================================================
 *  Per-frame update
 * ================================================================ */
void Godot_ScreenFX_Update(float delta, Camera3D *camera) {
    /* ── Damage flash fade ── */
    if (s_damage_timer > 0.0f) {
        s_damage_timer -= delta;
        if (s_damage_timer <= 0.0f) {
            s_damage_timer = 0.0f;
            s_damage_alpha = 0.0f;
        } else {
            /* Linear fade based on remaining time ratio */
            s_damage_alpha = s_damage_alpha_start * (s_damage_timer / DAMAGE_FADE_TIME);
        }
    }
    if (s_damage_rect) {
        s_damage_rect->set_color(Color(1.0f, 0.0f, 0.0f, s_damage_alpha));
    }

    /* ── Underwater tint oscillation ── */
    if (s_underwater_active) {
        s_underwater_time += delta;
        float mid   = (UNDERWATER_ALPHA_MIN + UNDERWATER_ALPHA_MAX) * 0.5f;
        float range = (UNDERWATER_ALPHA_MAX - UNDERWATER_ALPHA_MIN) * 0.5f;
        float alpha = mid + range * std::sin(s_underwater_time * UNDERWATER_OSC_FREQ * 2.0f * 3.14159265f);
        if (s_underwater_rect) {
            s_underwater_rect->set_color(Color(0.0f, 0.1f, 0.3f, alpha));
        }
    } else if (s_underwater_rect) {
        s_underwater_rect->set_color(Color(0.0f, 0.0f, 0.0f, 0.0f));
    }

    /* ── Flash-bang fade ── */
    if (s_flash_timer > 0.0f) {
        s_flash_timer -= delta;
        if (s_flash_timer <= 0.0f) {
            s_flash_timer = 0.0f;
            s_flash_alpha = 0.0f;
        } else {
            /* Linear fade based on remaining time ratio */
            s_flash_alpha = s_flash_alpha_start * (s_flash_timer / FLASH_FADE_TIME);
        }
    }
    if (s_flash_rect) {
        s_flash_rect->set_color(Color(1.0f, 1.0f, 1.0f, s_flash_alpha));
    }

    /* ── Pain flinch recovery ── */
    if (camera) {
        float prev_applied = s_flinch_applied;
        if (s_flinch_timer > 0.0f) {
            s_flinch_timer -= delta;
            if (s_flinch_timer <= 0.0f) {
                s_flinch_timer  = 0.0f;
                s_flinch_offset = 0.0f;
            } else {
                /* Smooth interpolation back to zero */
                float t = s_flinch_timer / FLINCH_RECOVER_TIME;
                s_flinch_offset = s_flinch_start * t;
            }
        }
        /* Apply delta between current and previously applied offset */
        float offset_delta = s_flinch_offset - prev_applied;
        if (offset_delta != 0.0f) {
            Vector3 rot = camera->get_rotation();
            rot.x += offset_delta;
            camera->set_rotation(rot);
        }
        s_flinch_applied = s_flinch_offset;
    }
}

/* ===================================================================
 *  Trigger — Damage Flash
 * ================================================================ */
void Godot_ScreenFX_DamageFlash(float intensity) {
    if (intensity <= 0.0f) {
        return;
    }
    /* Stack additively, capped */
    s_damage_alpha += intensity * DAMAGE_ALPHA_SCALE;
    if (s_damage_alpha > DAMAGE_ALPHA_CAP) {
        s_damage_alpha = DAMAGE_ALPHA_CAP;
    }
    s_damage_alpha_start = s_damage_alpha;
    s_damage_timer = DAMAGE_FADE_TIME;
}

/* ===================================================================
 *  Trigger — Underwater Tint
 * ================================================================ */
void Godot_ScreenFX_UnderwaterTint(bool active) {
    if (active == s_underwater_active) {
        return;
    }
    s_underwater_active = active;
    if (!active) {
        s_underwater_time = 0.0f;
    }
}

/* ===================================================================
 *  Trigger — Flash-Bang
 * ================================================================ */
void Godot_ScreenFX_FlashBang(float intensity) {
    if (intensity <= 0.0f) {
        return;
    }
    s_flash_alpha       = intensity;
    s_flash_alpha_start = intensity;
    s_flash_timer       = FLASH_FADE_TIME;
}

/* ===================================================================
 *  Trigger — Pain Flinch
 * ================================================================ */
void Godot_ScreenFX_PainFlinch(float pitch_offset) {
    s_flinch_start  = pitch_offset;
    s_flinch_offset = pitch_offset;
    s_flinch_timer  = FLINCH_RECOVER_TIME;
}
