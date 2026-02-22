/*
 * godot_explosion_effects.cpp — Explosion visual effects and camera shake.
 *
 * Implements a pooled explosion system with three visual phases:
 *   Phase 1 (0–0.2s): Expanding bright-orange fireball sphere.
 *   Phase 2 (0.1–0.8s): Dark smoke ring expanding outward, alpha fading.
 *   Phase 3 (0–0.5s): 5–10 debris chunks flying outward with gravity.
 * Each explosion also spawns a short-lived OmniLight3D and triggers
 * camera shake.
 *
 * Camera shake events are stored in a fixed-size array and stack
 * additively, with the total offset capped to ±0.15 m per axis.
 */

#include "godot_explosion_effects.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/torus_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cmath>

using namespace godot;

/* ===================================================================
 *  Constants
 * ================================================================ */

static constexpr int   MAX_EXPLOSIONS         = 16;
static constexpr int   MAX_DEBRIS_PER_EXPL    = 10;
static constexpr int   MIN_DEBRIS_PER_EXPL    = 5;
static constexpr int   MAX_SHAKES             = 32;
static constexpr float MAX_SHAKE_OFFSET       = 0.15f;

/* Phase timing (seconds) */
static constexpr float FIREBALL_START         = 0.0f;
static constexpr float FIREBALL_END           = 0.2f;
static constexpr float SMOKE_START            = 0.1f;
static constexpr float SMOKE_END              = 0.8f;
static constexpr float DEBRIS_START           = 0.0f;
static constexpr float DEBRIS_END             = 0.5f;
static constexpr float LIGHT_DURATION         = 0.3f;
static constexpr float DEBRIS_FADE_MIN        = 1.0f;
static constexpr float DEBRIS_FADE_MAX        = 2.0f;
static constexpr float DEBRIS_SIZE_MIN        = 0.05f;
static constexpr float DEBRIS_SIZE_MAX        = 0.15f;
static constexpr float GRAVITY                = 9.8f;

/* ===================================================================
 *  Camera Shake
 * ================================================================ */

struct ShakeEvent {
    Vector3 source_pos;
    float   intensity;
    float   duration;
    float   falloff_distance;
    float   elapsed;
    bool    active;
};

static ShakeEvent  s_shakes[MAX_SHAKES];
static int         s_shake_count       = 0;
static Vector3     s_last_shake_offset = Vector3();
static bool        s_shake_applied     = false;

void Godot_CameraShake_Trigger(float intensity, float duration,
                               float falloff_distance,
                               const Vector3 &source_pos) {
    if (s_shake_count >= MAX_SHAKES) {
        /* Overwrite oldest */
        for (int i = 0; i < MAX_SHAKES - 1; i++) {
            s_shakes[i] = s_shakes[i + 1];
        }
        s_shake_count = MAX_SHAKES - 1;
    }

    ShakeEvent &ev   = s_shakes[s_shake_count++];
    ev.source_pos       = source_pos;
    ev.intensity        = intensity;
    ev.duration         = duration;
    ev.falloff_distance = falloff_distance;
    ev.elapsed          = 0.0f;
    ev.active           = true;
}

void Godot_CameraShake_Update(float delta, Camera3D *camera) {
    if (!camera) return;

    /* Restore previous frame offset */
    if (s_shake_applied) {
        Transform3D t = camera->get_global_transform();
        t.origin -= s_last_shake_offset;
        camera->set_global_transform(t);
        s_last_shake_offset = Vector3();
        s_shake_applied     = false;
    }

    Vector3 total_offset = Vector3();
    Vector3 cam_pos      = camera->get_global_transform().origin;

    for (int i = 0; i < s_shake_count; ) {
        ShakeEvent &ev = s_shakes[i];
        if (!ev.active) {
            /* Remove by swap-with-last */
            s_shakes[i] = s_shakes[--s_shake_count];
            continue;
        }

        ev.elapsed += delta;
        if (ev.elapsed >= ev.duration) {
            ev.active = false;
            s_shakes[i] = s_shakes[--s_shake_count];
            continue;
        }

        /* Linear decay: 1 at start → 0 at duration */
        float t = 1.0f - (ev.elapsed / ev.duration);

        /* Distance attenuation */
        float dist = cam_pos.distance_to(ev.source_pos);
        float atten = 1.0f;
        if (ev.falloff_distance > 0.0f && dist > 0.0f) {
            atten = 1.0f - (dist / ev.falloff_distance);
            if (atten < 0.0f) atten = 0.0f;
        }

        float mag = ev.intensity * t * atten;

        /* Random offset per axis (Godot RNG — thread-safe, good quality) */
        float rx = (float)UtilityFunctions::randf_range(-1.0, 1.0) * mag;
        float ry = (float)UtilityFunctions::randf_range(-1.0, 1.0) * mag;
        float rz = (float)UtilityFunctions::randf_range(-1.0, 1.0) * mag;

        total_offset.x += rx;
        total_offset.y += ry;
        total_offset.z += rz;

        i++;
    }

    /* Clamp total offset */
    if (total_offset.x >  MAX_SHAKE_OFFSET) total_offset.x =  MAX_SHAKE_OFFSET;
    if (total_offset.x < -MAX_SHAKE_OFFSET) total_offset.x = -MAX_SHAKE_OFFSET;
    if (total_offset.y >  MAX_SHAKE_OFFSET) total_offset.y =  MAX_SHAKE_OFFSET;
    if (total_offset.y < -MAX_SHAKE_OFFSET) total_offset.y = -MAX_SHAKE_OFFSET;
    if (total_offset.z >  MAX_SHAKE_OFFSET) total_offset.z =  MAX_SHAKE_OFFSET;
    if (total_offset.z < -MAX_SHAKE_OFFSET) total_offset.z = -MAX_SHAKE_OFFSET;

    /* Apply offset */
    if (total_offset.length_squared() > 0.0f) {
        Transform3D t = camera->get_global_transform();
        t.origin += total_offset;
        camera->set_global_transform(t);
        s_last_shake_offset = total_offset;
        s_shake_applied     = true;
    }
}

void Godot_CameraShake_Clear(void) {
    s_shake_count       = 0;
    s_last_shake_offset = Vector3();
    s_shake_applied     = false;
}

/* ===================================================================
 *  Debris chunk
 * ================================================================ */

struct DebrisChunk {
    MeshInstance3D          *mesh;
    Ref<StandardMaterial3D>  mat;       /* per-instance for alpha fade */
    Vector3                  velocity;
    float                    lifetime;  /* total fade time */
    float                    elapsed;
    bool                     active;
};

/* ===================================================================
 *  Explosion instance
 * ================================================================ */

struct ExplosionInstance {
    /* Core state */
    Vector3 position;
    float   radius;
    float   intensity;
    float   elapsed;
    bool    active;

    /* Fireball (Phase 1) */
    MeshInstance3D          *fireball;
    Ref<StandardMaterial3D>  fireball_mat;  /* per-instance for alpha */

    /* Smoke ring (Phase 2) */
    MeshInstance3D          *smoke;
    Ref<StandardMaterial3D>  smoke_mat;     /* per-instance for alpha */

    /* Debris (Phase 3) */
    DebrisChunk debris[MAX_DEBRIS_PER_EXPL];
    int         debris_count;

    /* Light */
    OmniLight3D *light;
};

/* ===================================================================
 *  Module state
 * ================================================================ */

static ExplosionInstance s_explosions[MAX_EXPLOSIONS];
static Node3D           *s_expl_root = nullptr;

/* ===================================================================
 *  Material helpers
 * ================================================================ */

static Ref<StandardMaterial3D> create_fireball_material() {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(Color(1.0f, 0.6f, 0.1f, 0.9f));
    mat->set_emission(Color(1.0f, 0.5f, 0.0f));
    mat->set_emission_energy_multiplier(4.0f);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    return mat;
}

static Ref<StandardMaterial3D> create_smoke_material() {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(Color(0.2f, 0.2f, 0.2f, 0.6f));
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    return mat;
}

static Ref<StandardMaterial3D> create_debris_material() {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(Color(0.3f, 0.25f, 0.2f, 1.0f));
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    return mat;
}

/* ===================================================================
 *  Node creation helpers
 * ================================================================ */

static MeshInstance3D *create_fireball_node(Node3D *parent) {
    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<SphereMesh> sphere;
    sphere.instantiate();
    sphere->set_radius(0.5f);
    sphere->set_height(1.0f);
    sphere->set_radial_segments(12);
    sphere->set_rings(6);
    mi->set_mesh(sphere);
    mi->set_visible(false);
    parent->add_child(mi);
    return mi;
}

static MeshInstance3D *create_smoke_node(Node3D *parent) {
    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<TorusMesh> torus;
    torus.instantiate();
    torus->set_inner_radius(0.3f);
    torus->set_outer_radius(0.6f);
    torus->set_rings(12);
    torus->set_ring_segments(8);
    mi->set_mesh(torus);
    mi->set_visible(false);
    parent->add_child(mi);
    return mi;
}

static MeshInstance3D *create_debris_node(Node3D *parent) {
    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<BoxMesh> box;
    box.instantiate();
    float sz = DEBRIS_SIZE_MIN +
               (float)UtilityFunctions::randf_range(0.0, 1.0) * (DEBRIS_SIZE_MAX - DEBRIS_SIZE_MIN);
    box->set_size(Vector3(sz, sz, sz));
    mi->set_mesh(box);
    mi->set_visible(false);
    parent->add_child(mi);
    return mi;
}

static OmniLight3D *create_light_node(Node3D *parent) {
    OmniLight3D *l = memnew(OmniLight3D);
    l->set_color(Color(1.0f, 0.6f, 0.2f));
    l->set_param(Light3D::PARAM_RANGE, 10.0f);
    l->set_visible(false);
    parent->add_child(l);
    return l;
}

/* ===================================================================
 *  Initialise one pool slot
 * ================================================================ */

static void init_explosion_slot(ExplosionInstance &ex, Node3D *parent) {
    ex.active       = false;
    ex.elapsed      = 0.0f;
    ex.debris_count = 0;

    /* Per-instance materials so alpha can be changed without allocation */
    ex.fireball_mat = create_fireball_material();
    ex.smoke_mat    = create_smoke_material();

    ex.fireball = create_fireball_node(parent);
    ex.fireball->set_material_override(ex.fireball_mat);

    ex.smoke = create_smoke_node(parent);
    ex.smoke->set_material_override(ex.smoke_mat);

    ex.light = create_light_node(parent);

    for (int i = 0; i < MAX_DEBRIS_PER_EXPL; i++) {
        ex.debris[i].mat     = create_debris_material();
        ex.debris[i].mesh    = create_debris_node(parent);
        ex.debris[i].mesh->set_material_override(ex.debris[i].mat);
        ex.debris[i].active  = false;
        ex.debris[i].elapsed = 0.0f;
    }
}

/* ===================================================================
 *  Public API — Explosion
 * ================================================================ */

void Godot_Explosion_Init(Node3D *parent) {
    if (!parent) return;

    s_expl_root = memnew(Node3D);
    s_expl_root->set_name("ExplosionPool");
    parent->add_child(s_expl_root);

    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        init_explosion_slot(s_explosions[i], s_expl_root);
    }
}

void Godot_Explosion_Spawn(const Vector3 &position, float radius,
                           float intensity) {
    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!s_explosions[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* Pool full — overwrite oldest (highest elapsed) */
        float best = -1.0f;
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (s_explosions[i].elapsed > best) {
                best = s_explosions[i].elapsed;
                slot = i;
            }
        }
    }
    if (slot < 0) return;

    ExplosionInstance &ex = s_explosions[slot];
    ex.position  = position;
    ex.radius    = radius;
    ex.intensity = intensity;
    ex.elapsed   = 0.0f;
    ex.active    = true;

    /* Reset fireball */
    ex.fireball->set_global_position(position);
    ex.fireball->set_scale(Vector3(0.01f, 0.01f, 0.01f));
    ex.fireball->set_visible(true);

    /* Reset smoke */
    ex.smoke->set_global_position(position);
    ex.smoke->set_scale(Vector3(0.01f, 0.01f, 0.01f));
    ex.smoke->set_visible(false);  /* becomes visible at SMOKE_START */

    /* Reset light */
    ex.light->set_global_position(position);
    ex.light->set_param(Light3D::PARAM_ENERGY, intensity * 2.0f);
    ex.light->set_param(Light3D::PARAM_RANGE, radius * 2.0f);
    ex.light->set_visible(true);

    /* Spawn debris chunks (random count between MIN and MAX) */
    int count = MIN_DEBRIS_PER_EXPL +
                (int)UtilityFunctions::randi_range(0, MAX_DEBRIS_PER_EXPL - MIN_DEBRIS_PER_EXPL);
    ex.debris_count = count;

    for (int i = 0; i < MAX_DEBRIS_PER_EXPL; i++) {
        DebrisChunk &dc = ex.debris[i];
        if (i < count) {
            dc.active  = true;
            dc.elapsed = 0.0f;
            dc.lifetime = DEBRIS_FADE_MIN +
                          (float)UtilityFunctions::randf_range(0.0, 1.0) *
                          (DEBRIS_FADE_MAX - DEBRIS_FADE_MIN);

            /* Random outward + upward velocity */
            float angle = (float)UtilityFunctions::randf_range(0.0, 2.0 * M_PI);
            float speed = (float)UtilityFunctions::randf_range(2.0, 5.0);
            dc.velocity = Vector3(
                cosf(angle) * speed,
                (float)UtilityFunctions::randf_range(3.0, 5.0),
                sinf(angle) * speed);

            dc.mesh->set_global_position(position);
            dc.mesh->set_visible(true);
        } else {
            dc.active = false;
            dc.mesh->set_visible(false);
        }
    }

    /* Trigger camera shake */
    Godot_CameraShake_Trigger(
        intensity * 0.08f,      /* offset amplitude in metres */
        0.5f,                   /* duration */
        radius * 4.0f,          /* falloff distance */
        position);
}

/* ── Per-frame update helpers ── */

static void update_fireball(ExplosionInstance &ex) {
    float t = ex.elapsed;
    if (t >= FIREBALL_START && t <= FIREBALL_END) {
        float frac = (t - FIREBALL_START) / (FIREBALL_END - FIREBALL_START);
        float s    = frac * ex.radius;
        ex.fireball->set_scale(Vector3(s, s, s));

        /* Fade alpha near the end using per-instance material */
        float alpha = 1.0f - frac * 0.5f;
        if (ex.fireball_mat.is_valid()) {
            Color c = ex.fireball_mat->get_albedo();
            c.a     = alpha;
            ex.fireball_mat->set_albedo(c);
        }
        ex.fireball->set_visible(true);
    } else if (t > FIREBALL_END) {
        ex.fireball->set_visible(false);
    }
}

static void update_smoke(ExplosionInstance &ex) {
    float t = ex.elapsed;
    if (t >= SMOKE_START && t <= SMOKE_END) {
        float frac = (t - SMOKE_START) / (SMOKE_END - SMOKE_START);
        float s    = frac * ex.radius * 1.5f;
        ex.smoke->set_scale(Vector3(s, s * 0.3f, s));

        /* Fade smoke alpha over its lifetime */
        float alpha = 0.6f * (1.0f - frac);
        if (ex.smoke_mat.is_valid()) {
            Color c = ex.smoke_mat->get_albedo();
            c.a     = alpha;
            ex.smoke_mat->set_albedo(c);
        }
        ex.smoke->set_visible(true);
    } else if (t > SMOKE_END) {
        ex.smoke->set_visible(false);
    }
}

static void update_debris(ExplosionInstance &ex, float delta) {
    for (int i = 0; i < ex.debris_count; i++) {
        DebrisChunk &dc = ex.debris[i];
        if (!dc.active) continue;

        dc.elapsed += delta;
        if (dc.elapsed >= dc.lifetime) {
            dc.active = false;
            dc.mesh->set_visible(false);
            continue;
        }

        /* Apply gravity */
        dc.velocity.y -= GRAVITY * delta;

        /* Move */
        Vector3 pos = dc.mesh->get_global_position();
        pos += dc.velocity * delta;
        dc.mesh->set_global_position(pos);

        /* Fade alpha over lifetime using cached per-instance material */
        float alpha = 1.0f - (dc.elapsed / dc.lifetime);
        if (alpha < 0.0f) alpha = 0.0f;

        if (dc.mat.is_valid()) {
            Color c = dc.mat->get_albedo();
            c.a     = alpha;
            dc.mat->set_albedo(c);
        }
    }
}

static void update_light(ExplosionInstance &ex) {
    if (ex.elapsed < LIGHT_DURATION) {
        float frac   = ex.elapsed / LIGHT_DURATION;
        float energy = ex.intensity * 2.0f * (1.0f - frac);
        ex.light->set_param(Light3D::PARAM_ENERGY, energy);
        ex.light->set_visible(true);
    } else {
        ex.light->set_visible(false);
    }
}

void Godot_Explosion_Update(float delta) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        ExplosionInstance &ex = s_explosions[i];
        if (!ex.active) continue;

        ex.elapsed += delta;

        update_fireball(ex);
        update_smoke(ex);
        update_debris(ex, delta);
        update_light(ex);

        /* Deactivate when all phases are complete and debris is done */
        if (ex.elapsed > SMOKE_END) {
            bool any_debris = false;
            for (int d = 0; d < ex.debris_count; d++) {
                if (ex.debris[d].active) { any_debris = true; break; }
            }
            if (!any_debris) {
                ex.active = false;
                ex.fireball->set_visible(false);
                ex.smoke->set_visible(false);
                ex.light->set_visible(false);
            }
        }
    }
}

void Godot_Explosion_Clear(void) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        ExplosionInstance &ex = s_explosions[i];
        ex.active  = false;
        ex.elapsed = 0.0f;
        if (ex.fireball) ex.fireball->set_visible(false);
        if (ex.smoke)    ex.smoke->set_visible(false);
        if (ex.light)    ex.light->set_visible(false);
        for (int d = 0; d < MAX_DEBRIS_PER_EXPL; d++) {
            ex.debris[d].active = false;
            if (ex.debris[d].mesh) ex.debris[d].mesh->set_visible(false);
        }
    }
    Godot_CameraShake_Clear();
}

void Godot_Explosion_Shutdown(void) {
    Godot_Explosion_Clear();

    /* The node tree owns the MeshInstance3D / OmniLight3D nodes;
     * removing s_expl_root frees them all.  */
    if (s_expl_root) {
        if (s_expl_root->get_parent()) {
            s_expl_root->get_parent()->remove_child(s_expl_root);
        }
        memdelete(s_expl_root);
        s_expl_root = nullptr;
    }

    /* Clear node pointers and per-instance materials in pool slots */
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        s_explosions[i].fireball = nullptr;
        s_explosions[i].smoke    = nullptr;
        s_explosions[i].light    = nullptr;
        s_explosions[i].fireball_mat.unref();
        s_explosions[i].smoke_mat.unref();
        for (int d = 0; d < MAX_DEBRIS_PER_EXPL; d++) {
            s_explosions[i].debris[d].mesh = nullptr;
            s_explosions[i].debris[d].mat.unref();
        }
    }
}
