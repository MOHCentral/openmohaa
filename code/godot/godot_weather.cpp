/*
 * godot_weather.cpp — Rain and snow particle system manager.
 *
 * Creates and manages Godot GPUParticles3D nodes for rain and snow
 * effects.  Weather state is read each frame from the engine via
 * C accessors (godot_weather_accessors.c) that query the server
 * configstrings (CS_RAIN_*).
 *
 * Rain parameters (density, speed, slant, length, width) are mapped
 * to Godot particle properties so that the visual matches the
 * original engine behaviour.
 *
 * The particle volume follows the camera position each frame so that
 * weather is always visible around the player.
 */

#include "godot_weather.h"

#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

/* ── Static state ── */
static GPUParticles3D *s_rain_emitter  = nullptr;
static GPUParticles3D *s_snow_emitter  = nullptr;
static Node3D         *s_weather_root  = nullptr;
static int             s_current_state = 0;  /* WEATHER_NONE */

/* ── Volume dimensions (Godot units = metres) ── */
static constexpr float WEATHER_VOLUME_WIDTH  = 30.0f;  /* ±15m around camera */
static constexpr float WEATHER_VOLUME_HEIGHT = 20.0f;  /* 20m tall column */

/* ── Coordinate conversion ── */
static constexpr float MOHAA_UNIT_SCALE = 1.0f / 39.37f; /* inches → metres */

/* ===================================================================
 *  Create a rain particle emitter
 * ================================================================ */
static GPUParticles3D *create_rain_emitter(Node3D *parent) {
    GPUParticles3D *rain = memnew(GPUParticles3D);
    rain->set_name("WeatherRain");

    rain->set_amount(2000);
    rain->set_lifetime(1.0);
    rain->set_one_shot(false);
    rain->set_visibility_aabb(AABB(
        Vector3(-WEATHER_VOLUME_WIDTH * 0.5f, 0.0f, -WEATHER_VOLUME_WIDTH * 0.5f),
        Vector3(WEATHER_VOLUME_WIDTH, WEATHER_VOLUME_HEIGHT, WEATHER_VOLUME_WIDTH)));

    /* Process material — gravity-driven downward particles */
    Ref<ParticleProcessMaterial> mat;
    mat.instantiate();
    mat->set_direction(Vector3(0.0f, -1.0f, 0.0f));
    mat->set_spread(5.0f);
    mat->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 8.0f);
    mat->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 12.0f);
    mat->set_gravity(Vector3(0.0f, -9.8f, 0.0f));

    /* Emission box covering the volume */
    mat->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
    mat->set_emission_box_extents(
        Vector3(WEATHER_VOLUME_WIDTH * 0.5f, 1.0f, WEATHER_VOLUME_WIDTH * 0.5f));

    rain->set_process_material(mat);

    /* Draw pass: thin stretched quad for rain streaks */
    Ref<QuadMesh> draw_mesh;
    draw_mesh.instantiate();
    draw_mesh->set_size(Vector2(0.02f, 0.4f));

    Ref<StandardMaterial3D> draw_mat;
    draw_mat.instantiate();
    draw_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    draw_mat->set_albedo(Color(0.7f, 0.75f, 0.85f, 0.4f));
    draw_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    draw_mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
    draw_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    draw_mesh->set_material(draw_mat);

    rain->set_draw_pass_mesh(0, draw_mesh);

    rain->set_emitting(false);
    parent->add_child(rain);
    return rain;
}

/* ===================================================================
 *  Create a snow particle emitter
 * ================================================================ */
static GPUParticles3D *create_snow_emitter(Node3D *parent) {
    GPUParticles3D *snow = memnew(GPUParticles3D);
    snow->set_name("WeatherSnow");

    snow->set_amount(1500);
    snow->set_lifetime(3.0);
    snow->set_one_shot(false);
    snow->set_visibility_aabb(AABB(
        Vector3(-WEATHER_VOLUME_WIDTH * 0.5f, 0.0f, -WEATHER_VOLUME_WIDTH * 0.5f),
        Vector3(WEATHER_VOLUME_WIDTH, WEATHER_VOLUME_HEIGHT, WEATHER_VOLUME_WIDTH)));

    /* Process material — slow drifting downward */
    Ref<ParticleProcessMaterial> mat;
    mat.instantiate();
    mat->set_direction(Vector3(0.0f, -1.0f, 0.0f));
    mat->set_spread(30.0f);
    mat->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.0f);
    mat->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.5f);
    mat->set_gravity(Vector3(0.3f, -1.5f, 0.2f));

    /* Emission box */
    mat->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
    mat->set_emission_box_extents(
        Vector3(WEATHER_VOLUME_WIDTH * 0.5f, 1.0f, WEATHER_VOLUME_WIDTH * 0.5f));

    snow->set_process_material(mat);

    /* Draw pass: small white quad for snowflakes */
    Ref<QuadMesh> draw_mesh;
    draw_mesh.instantiate();
    draw_mesh->set_size(Vector2(0.06f, 0.06f));

    Ref<StandardMaterial3D> draw_mat;
    draw_mat.instantiate();
    draw_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    draw_mat->set_albedo(Color(0.95f, 0.95f, 1.0f, 0.7f));
    draw_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    draw_mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
    draw_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    draw_mesh->set_material(draw_mat);

    snow->set_draw_pass_mesh(0, draw_mesh);

    snow->set_emitting(false);
    parent->add_child(snow);
    return snow;
}

/* ===================================================================
 *  Apply engine rain parameters to the rain particle emitter.
 *
 *  Maps the engine's speed, slant, length, and width values to
 *  Godot particle material properties.
 * ================================================================ */
static void apply_rain_params(GPUParticles3D *rain, float density) {
    if (!rain) return;

    /* Read engine parameters */
    float engine_speed  = Godot_Weather_GetSpeed();    /* default 2048 */
    int   speed_vary    = Godot_Weather_GetSpeedVary(); /* default 512 */
    int   slant         = Godot_Weather_GetSlant();     /* default 50 */
    float engine_length = Godot_Weather_GetLength();    /* default 90 */
    float engine_width  = Godot_Weather_GetWidth();     /* default 1 */

    /* Convert speed from engine units to Godot particle velocity (m/s).
     * Engine speed is in game-units/frame (~200fps); the 0.005 factor
     * accounts for the frame-rate difference and prevents excessively
     * fast particles in Godot's continuous simulation. */
    float speed_ms  = engine_speed * MOHAA_UNIT_SCALE * 0.005f;
    float vary_ms   = (float)speed_vary * MOHAA_UNIT_SCALE * 0.005f;
    float speed_min = speed_ms - vary_ms * 0.5f;
    float speed_max = speed_ms + vary_ms * 0.5f;
    if (speed_min < 1.0f) speed_min = 1.0f;
    if (speed_max < speed_min + 0.5f) speed_max = speed_min + 0.5f;

    /* Convert slant to a horizontal wind component.  The engine slant
     * is a unitless factor (default 50); we scale by 0.01 to produce
     * a gentle horizontal drift in metres/s². */
    float slant_x = (float)slant * MOHAA_UNIT_SCALE * 0.01f;

    /* Particle count: base 2000 at full density, clamped to
     * [100, 4000] for visual quality vs GPU performance. */
    int amount = (int)(2000.0f * density);
    if (amount < 100)  amount = 100;
    if (amount > 4000) amount = 4000;
    rain->set_amount(amount);

    /* Update process material */
    Ref<Material> base_mat = rain->get_process_material();
    Ref<ParticleProcessMaterial> mat = base_mat;
    if (mat.is_valid()) {
        mat->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed_min);
        mat->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed_max);
        /* Wind: apply slant as a horizontal gravity component
         * In Godot coords, X=right; engine slant is unitless so we
         * scale it gently. */
        mat->set_gravity(Vector3(slant_x, -9.8f, 0.0f));
    }

    /* Update draw mesh size from engine length/width (in inches → metres).
     * Clamp to sane bounds: min 0.5cm wide / 5cm tall (invisible below),
     * max 20cm wide / 2m tall (absurdly large above). */
    Ref<Mesh> draw = rain->get_draw_pass_mesh(0);
    if (draw.is_valid()) {
        QuadMesh *qm = Object::cast_to<QuadMesh>(draw.ptr());
        if (qm) {
            float w = engine_width * MOHAA_UNIT_SCALE;
            float h = engine_length * MOHAA_UNIT_SCALE;
            if (w < 0.005f) w = 0.005f;
            if (h < 0.05f)  h = 0.05f;
            if (w > 0.2f)   w = 0.2f;
            if (h > 2.0f)   h = 2.0f;
            qm->set_size(Vector2(w, h));
        }
    }
}

/* ===================================================================
 *  Public API
 * ================================================================ */

void Godot_Weather_Init(Node3D *parent) {
    if (!parent) return;

    /* Clean up previous weather state */
    Godot_Weather_Shutdown();

    s_weather_root = parent;
    s_rain_emitter = create_rain_emitter(parent);
    s_snow_emitter = create_snow_emitter(parent);
    s_current_state = WEATHER_NONE;

    UtilityFunctions::print("[Weather] Initialised rain and snow emitters.");
}

void Godot_Weather_Update(const Vector3 &camera_pos, float delta) {
    (void)delta;

    int state = Godot_Weather_GetState();
    float density = Godot_Weather_GetDensity();

    /* Reposition emitter volume above the camera */
    Vector3 emitter_pos = camera_pos + Vector3(0.0f, WEATHER_VOLUME_HEIGHT * 0.5f, 0.0f);

    if (s_rain_emitter) {
        s_rain_emitter->set_global_position(emitter_pos);
        bool want_rain = (state == WEATHER_RAIN && density > 0.001f);
        if (want_rain != s_rain_emitter->is_emitting()) {
            s_rain_emitter->set_emitting(want_rain);
        }
        if (want_rain) {
            apply_rain_params(s_rain_emitter, density);
        }
    }

    if (s_snow_emitter) {
        s_snow_emitter->set_global_position(emitter_pos);
        bool want_snow = (state == WEATHER_SNOW && density > 0.001f);
        if (want_snow != s_snow_emitter->is_emitting()) {
            s_snow_emitter->set_emitting(want_snow);
            if (want_snow) {
                int amount = (int)(1500.0f * density);
                if (amount < 100) amount = 100;
                s_snow_emitter->set_amount(amount);
            }
        }
    }

    s_current_state = state;
}

void Godot_Weather_Shutdown(void) {
    if (s_rain_emitter) {
        s_rain_emitter->set_emitting(false);
        if (s_rain_emitter->get_parent()) {
            s_rain_emitter->get_parent()->remove_child(s_rain_emitter);
        }
        memdelete(s_rain_emitter);
        s_rain_emitter = nullptr;
    }
    if (s_snow_emitter) {
        s_snow_emitter->set_emitting(false);
        if (s_snow_emitter->get_parent()) {
            s_snow_emitter->get_parent()->remove_child(s_snow_emitter);
        }
        memdelete(s_snow_emitter);
        s_snow_emitter = nullptr;
    }
    s_weather_root = nullptr;
    s_current_state = WEATHER_NONE;
}
