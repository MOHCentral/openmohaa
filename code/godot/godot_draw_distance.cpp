/*
 * godot_draw_distance.cpp — Draw distance manager.
 *
 * Maps MOHAA draw distance cvars to Godot Camera3D near/far planes
 * and Environment fog settings:
 *
 *   r_znear         → camera->set_near()   (inches → metres)
 *   r_zfar          → camera->set_far()    (inches → metres, 0=auto)
 *   cg_farplane     → overrides far plane  (inches → metres)
 *   cg_farplane_color → fog colour
 *   farplane_cull   → entity cull distance
 *
 * Cvars are polled once per second via a delta accumulator to avoid
 * per-frame overhead.
 */

#ifdef GODOT_GDEXTENSION

#include "godot_draw_distance.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/environment.hpp>

using namespace godot;

/* ── Constants ── */
static constexpr float INCHES_TO_METRES   = 1.0f / 39.37f;
static constexpr float DEFAULT_FAR_METRES  = 1000.0f;
static constexpr float MIN_NEAR_METRES     = 0.001f;
static constexpr float POLL_INTERVAL       = 1.0f; /* seconds */

/* ── Cached state ── */
static float s_near_metres       = 4.0f * INCHES_TO_METRES;
static float s_far_metres        = DEFAULT_FAR_METRES;
static float s_fog_color_r       = 0.0f;
static float s_fog_color_g       = 0.0f;
static float s_fog_color_b       = 0.0f;
static bool  s_fog_enabled       = false;
static float s_fog_density       = 0.0f;
static float s_cull_distance     = 0.0f; /* Godot metres; 0 = disabled */
static float s_poll_accumulator  = 0.0f;
static bool  s_initialised       = false;

/* ── C accessor declarations ── */
extern "C" {
    float Godot_DrawDistance_GetZNear(void);
    float Godot_DrawDistance_GetZFar(void);
    float Godot_DrawDistance_GetFarplane(void);
    void  Godot_DrawDistance_GetFarplaneColor(float *r, float *g, float *b);
    int   Godot_DrawDistance_GetFarplaneCull(void);
}

/* ── Internal: refresh cached values from engine cvars ── */
static void draw_distance_poll(void)
{
    /* Near plane */
    float znear = Godot_DrawDistance_GetZNear();
    s_near_metres = znear * INCHES_TO_METRES;
    if (s_near_metres < MIN_NEAR_METRES) {
        s_near_metres = MIN_NEAR_METRES;
    }

    /* Far plane — cg_farplane overrides r_zfar */
    float farplane = Godot_DrawDistance_GetFarplane();
    float zfar     = Godot_DrawDistance_GetZFar();

    if (farplane > 0.0f) {
        s_far_metres  = farplane * INCHES_TO_METRES;
        s_fog_enabled = true;

        /* Fog colour */
        Godot_DrawDistance_GetFarplaneColor(&s_fog_color_r,
                                            &s_fog_color_g,
                                            &s_fog_color_b);

        /*
         * Fog density: Godot uses exponential fog (exp(-density * dist)).
         * To reach ~90 % opacity at the far plane distance:
         *   exp(-density * dist) = 0.1  →  density = 2.3 / dist
         */
        if (s_far_metres > 0.01f) {
            s_fog_density = 2.3f / s_far_metres;
        } else {
            s_fog_density = 0.0f;
        }
    } else if (zfar > 0.0f) {
        s_far_metres  = zfar * INCHES_TO_METRES;
        s_fog_enabled = false;
    } else {
        s_far_metres  = DEFAULT_FAR_METRES;
        s_fog_enabled = false;
    }

    /* Cull distance */
    if (Godot_DrawDistance_GetFarplaneCull() && s_fog_enabled) {
        s_cull_distance = s_far_metres;
    } else {
        s_cull_distance = 0.0f;
    }
}

/* ── Public API ── */

void Godot_DrawDistance_Init(void)
{
    draw_distance_poll();
    s_poll_accumulator = 0.0f;
    s_initialised = true;
}

void Godot_DrawDistance_Update(Camera3D *camera, Environment *env,
                               float delta)
{
    if (!camera || !env) {
        return;
    }

    /* Rate-limit cvar polling to once per second */
    s_poll_accumulator += delta;
    if (s_poll_accumulator >= POLL_INTERVAL || !s_initialised) {
        s_poll_accumulator = 0.0f;
        s_initialised = true;
        draw_distance_poll();
    }

    /* Apply near/far planes */
    camera->set_near((double)s_near_metres);
    camera->set_far((double)s_far_metres);

    /* Apply fog */
    if (s_fog_enabled) {
        env->set_fog_enabled(true);
        env->set_fog_light_color(Color(s_fog_color_r,
                                        s_fog_color_g,
                                        s_fog_color_b));
        env->set_fog_density(s_fog_density);
        env->set_fog_sky_affect(1.0f);
    } else {
        if (env->is_fog_enabled()) {
            env->set_fog_enabled(false);
        }
    }
}

float Godot_DrawDistance_GetCullDistance(void)
{
    return s_cull_distance;
}

#endif /* GODOT_GDEXTENSION */
