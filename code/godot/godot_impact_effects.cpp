/*
 * godot_impact_effects.cpp — Impact effect spawner + per-surface templates.
 *
 * Manages a pool of billboard-quad particles that burst outward in a
 * cone around the surface normal when a bullet or projectile hits a
 * surface.  Each surface type has its own visual template (colour,
 * speed, count, etc.).  Decals are small quads aligned to the hit
 * surface that fade after a configurable lifetime.
 *
 * Particle pool: a fixed-size ring buffer of MeshInstance3D nodes is
 * pre-allocated at init time.  Spawning grabs free slots; expired
 * particles are recycled.  This avoids per-frame allocation.
 */

#include "godot_impact_effects.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>

#include <cmath>
#include <cstdlib>

extern "C" {
    int Godot_Renderer_RegisterShader(const char *name);
}
extern godot::Ref<godot::ImageTexture> Godot_GetShaderTexture(int shader_handle);

using namespace godot;

/* ── Constants ── */
static constexpr int   MAX_IMPACT_PARTICLES = 256;
static constexpr float GRAVITY_ACCEL        = 9.8f;   /* m/s² */
static constexpr int   MAX_DECALS           = 64;
static constexpr float DEFAULT_DECAL_LIFE   = 10.0f;  /* seconds */

/* ── Per-particle state ── */
struct ImpactParticle {
    MeshInstance3D *node;
    Vector3         velocity;
    float           lifetime;       /* remaining seconds */
    float           max_lifetime;   /* total seconds (for alpha fade) */
    float           gravity_scale;  /* per-template gravity multiplier */
    bool            active;
};

/* ── Per-decal state ── */
struct ImpactDecal {
    MeshInstance3D *node;
    float           lifetime;       /* remaining seconds (0 = permanent) */
    float           max_lifetime;
    bool            active;
};

/* ── Module state ── */
static Node3D          *s_impact_root     = nullptr;
static ImpactParticle   s_particles[MAX_IMPACT_PARTICLES];
static int              s_next_particle   = 0;
static ImpactDecal      s_decals[MAX_DECALS];
static int              s_next_decal      = 0;
static bool             s_initialised     = false;

/* ── Per-surface templates ── */
static const ImpactTemplate s_templates[IMPACT_COUNT] = {
    /* IMPACT_DEFAULT — grey dust puff */
    {  8, 3.0f, 0.5f, {0.6f, 0.6f, 0.6f, 1.0f}, 0.02f,
       "textures/decals/bullethole_default", 0.03f, 0.0f, 1.0f, 45.0f },
    /* IMPACT_METAL — bright-orange sparks */
    { 15, 8.0f, 0.3f, {1.0f, 0.7f, 0.2f, 1.0f}, 0.015f,
       "textures/decals/bullethole_metal", 0.025f, 0.0f, 0.3f, 35.0f },
    /* IMPACT_WOOD — brown splinters */
    { 10, 4.0f, 0.5f, {0.55f, 0.35f, 0.15f, 1.0f}, 0.02f,
       "textures/decals/bullethole_wood", 0.03f, 0.0f, 1.0f, 40.0f },
    /* IMPACT_STONE — grey chips */
    { 12, 5.0f, 0.4f, {0.5f, 0.5f, 0.5f, 1.0f}, 0.018f,
       "textures/decals/bullethole_stone", 0.03f, 0.0f, 1.0f, 40.0f },
    /* IMPACT_DIRT — brown puffs */
    {  8, 2.0f, 0.6f, {0.45f, 0.35f, 0.2f, 0.9f}, 0.03f,
       "textures/decals/bullethole_dirt", 0.04f, 0.0f, 1.2f, 55.0f },
    /* IMPACT_GRASS — green-brown puffs */
    {  8, 2.5f, 0.5f, {0.3f, 0.45f, 0.15f, 0.9f}, 0.025f,
       "textures/decals/bullethole_dirt", 0.03f, 0.0f, 1.0f, 50.0f },
    /* IMPACT_WATER — white droplets, no decal */
    { 20, 3.0f, 0.8f, {0.8f, 0.85f, 1.0f, 0.7f}, 0.02f,
       nullptr, 0.0f, 0.0f, 1.5f, 70.0f },
    /* IMPACT_GLASS — white/transparent shards */
    { 15, 6.0f, 0.4f, {0.9f, 0.95f, 1.0f, 0.8f}, 0.015f,
       "textures/decals/crack_glass", 0.04f, 0.0f, 0.5f, 30.0f },
    /* IMPACT_FLESH — red particles */
    { 10, 3.0f, 0.5f, {0.7f, 0.05f, 0.05f, 1.0f}, 0.02f,
       "textures/decals/blood_splat", 0.05f, 8.0f, 1.0f, 50.0f },
    /* IMPACT_SAND — tan puffs */
    { 10, 2.0f, 0.6f, {0.76f, 0.7f, 0.5f, 0.85f}, 0.025f,
       "textures/decals/bullethole_dirt", 0.04f, 0.0f, 1.2f, 55.0f },
    /* IMPACT_SNOW — white puffs */
    { 10, 2.0f, 0.6f, {0.95f, 0.95f, 1.0f, 0.8f}, 0.025f,
       "textures/decals/bullethole_dirt", 0.03f, 0.0f, 0.8f, 55.0f },
    /* IMPACT_MUD — dark brown puffs */
    {  8, 1.5f, 0.7f, {0.3f, 0.2f, 0.1f, 0.9f}, 0.03f,
       "textures/decals/bullethole_dirt", 0.04f, 0.0f, 1.5f, 60.0f },
    /* IMPACT_GRAVEL — grey chips */
    { 10, 4.0f, 0.4f, {0.55f, 0.5f, 0.45f, 1.0f}, 0.018f,
       "textures/decals/bullethole_stone", 0.03f, 0.0f, 1.0f, 40.0f },
    /* IMPACT_FOLIAGE — green bits */
    {  8, 2.5f, 0.5f, {0.2f, 0.5f, 0.1f, 0.9f}, 0.02f,
       nullptr, 0.0f, 0.0f, 0.8f, 50.0f },
    /* IMPACT_CARPET — soft puffs */
    {  6, 1.5f, 0.5f, {0.5f, 0.4f, 0.35f, 0.8f}, 0.025f,
       "textures/decals/bullethole_default", 0.03f, 0.0f, 0.5f, 50.0f },
    /* IMPACT_PAPER — light puffs */
    {  8, 3.0f, 0.4f, {0.85f, 0.82f, 0.75f, 0.9f}, 0.02f,
       "textures/decals/bullethole_default", 0.03f, 0.0f, 0.5f, 45.0f },
    /* IMPACT_GRILL — metal sparks (like metal but fewer) */
    { 12, 7.0f, 0.3f, {1.0f, 0.65f, 0.15f, 1.0f}, 0.015f,
       "textures/decals/bullethole_metal", 0.025f, 0.0f, 0.4f, 40.0f },
};

/* ===================================================================
 *  Random helpers
 * ================================================================ */

/* Returns a float in [0, 1). */
static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

/* ===================================================================
 *  Build a randomised velocity vector within a cone around `normal`.
 *  spread_deg is the cone half-angle in degrees.
 * ================================================================ */
static Vector3 random_cone_velocity(const Vector3 &normal, float speed,
                                    float spread_deg) {
    /* Build a tangent frame around the normal */
    Vector3 n = normal.normalized();
    Vector3 tangent;
    if (fabs(n.x) < 0.9f) {
        tangent = n.cross(Vector3(1, 0, 0)).normalized();
    } else {
        tangent = n.cross(Vector3(0, 1, 0)).normalized();
    }
    Vector3 bitangent = n.cross(tangent);

    float spread_rad = spread_deg * (float)M_PI / 180.0f;
    float angle      = randf() * spread_rad;
    float phi        = randf() * 2.0f * (float)M_PI;

    float sin_a = sinf(angle);
    Vector3 dir = n * cosf(angle)
                + tangent * (sin_a * cosf(phi))
                + bitangent * (sin_a * sinf(phi));

    /* Add slight speed variation (±20%) */
    float v = speed * (0.8f + randf() * 0.4f);
    return dir * v;
}

/* ===================================================================
 *  Create a shared billboard material for impact particles.
 * ================================================================ */
static Ref<StandardMaterial3D> create_particle_material(const float colour[4]) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    mat->set_albedo(Color(colour[0], colour[1], colour[2], colour[3]));
    mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    return mat;
}

/* ===================================================================
 *  Create a decal material (face-aligned, unshaded).
 * ================================================================ */
static Ref<StandardMaterial3D> create_decal_material(const char *texture_path) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_albedo(Color(0.8f, 0.8f, 0.8f, 0.8f)); // Brighter base, let texture do the work
    mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    mat->set_render_priority(1);

    if (texture_path && texture_path[0]) {
        // Find or register the shader for this decal texture to use the renderer's texture loading
        int shader_handle = Godot_Renderer_RegisterShader(texture_path);
        if (shader_handle > 0) {
            Ref<ImageTexture> tex = Godot_GetShaderTexture(shader_handle);
            if (tex.is_valid()) {
                mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
            }
        }
    }

    return mat;
}

/* ===================================================================
 *  Public API
 * ================================================================ */

void Godot_Impact_Init(Node3D *parent) {
    if (!parent) return;

    /* Clean up any previous state */
    Godot_Impact_Shutdown();

    s_impact_root = memnew(Node3D);
    s_impact_root->set_name("ImpactEffects");
    parent->add_child(s_impact_root);

    /* Pre-allocate particle pool */
    for (int i = 0; i < MAX_IMPACT_PARTICLES; i++) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_visible(false);
        s_impact_root->add_child(mi);

        Ref<QuadMesh> qm;
        qm.instantiate();
        qm->set_size(Vector2(0.04f, 0.04f));  /* default; resized on spawn */
        mi->set_mesh(qm);

        s_particles[i].node         = mi;
        s_particles[i].velocity     = Vector3();
        s_particles[i].lifetime     = 0.0f;
        s_particles[i].max_lifetime = 0.0f;
        s_particles[i].gravity_scale = 1.0f;
        s_particles[i].active       = false;
    }

    /* Pre-allocate decal pool */
    for (int i = 0; i < MAX_DECALS; i++) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_visible(false);
        s_impact_root->add_child(mi);

        Ref<QuadMesh> qm;
        qm.instantiate();
        qm->set_size(Vector2(0.06f, 0.06f));  /* default; resized on spawn */
        mi->set_mesh(qm);

        s_decals[i].node         = mi;
        s_decals[i].lifetime     = 0.0f;
        s_decals[i].max_lifetime = 0.0f;
        s_decals[i].active       = false;
    }

    s_initialised = true;
    UtilityFunctions::print("[ImpactFX] Initialised — ",
                            MAX_IMPACT_PARTICLES, " particles, ",
                            MAX_DECALS, " decals.");
}

/* ─────────────────────────────────────────────────────────────────── */

void Godot_Impact_Spawn(ImpactSurfaceType type,
                        const Vector3 &position,
                        const Vector3 &normal) {
    if (!s_initialised) return;
    if (type < 0 || type >= IMPACT_COUNT) type = IMPACT_DEFAULT;

    const ImpactTemplate &tmpl = s_templates[type];

    /* — Spawn particles — */
    Ref<StandardMaterial3D> pmat = create_particle_material(tmpl.particle_colour);
    float quad_size = tmpl.particle_size * 2.0f;

    for (int i = 0; i < tmpl.particle_count; i++) {
        ImpactParticle &p = s_particles[s_next_particle];

        /* Recycle: hide previous occupant if still active */
        if (p.active && p.node) {
            p.node->set_visible(false);
        }

        p.velocity     = random_cone_velocity(normal, tmpl.particle_velocity,
                                              tmpl.spread_angle);
        p.lifetime     = tmpl.particle_lifetime * (0.7f + randf() * 0.6f);
        p.max_lifetime = p.lifetime;
        p.gravity_scale = tmpl.gravity_scale;
        p.active       = true;

        if (p.node) {
            /* Resize quad */
            Ref<Mesh> mesh = p.node->get_mesh();
            Ref<QuadMesh> qm = mesh;
            if (qm.is_valid()) {
                qm->set_size(Vector2(quad_size, quad_size));
            }
            /* Apply material */
            p.node->set_surface_override_material(0, pmat);
            p.node->set_global_position(position);
            p.node->set_visible(true);
        }

        s_next_particle = (s_next_particle + 1) % MAX_IMPACT_PARTICLES;
    }

    /* — Spawn decal — */
    if (tmpl.decal_texture != nullptr && tmpl.decal_size > 0.0f) {
        ImpactDecal &d = s_decals[s_next_decal];

        if (d.active && d.node) {
            d.node->set_visible(false);
        }

        float life = (tmpl.decal_lifetime > 0.0f) ? tmpl.decal_lifetime
                                                   : DEFAULT_DECAL_LIFE;
        d.lifetime     = life;
        d.max_lifetime = life;
        d.active       = true;

        if (d.node) {
            float dsize = tmpl.decal_size * 2.0f;
            Ref<Mesh> mesh = d.node->get_mesh();
            Ref<QuadMesh> qm = mesh;
            if (qm.is_valid()) {
                qm->set_size(Vector2(dsize, dsize));
            }

            Ref<StandardMaterial3D> dmat = create_decal_material(tmpl.decal_texture);
            d.node->set_surface_override_material(0, dmat);

            /* Position slightly offset from surface to avoid z-fighting */
            Vector3 n = normal.normalized();
            d.node->set_global_position(position + n * 0.005f);

            /* Orient the decal to face along the surface normal */
            if (n.length_squared() > 0.001f) {
                Vector3 up = Vector3(0, 1, 0);
                if (fabs(n.dot(up)) > 0.99f) {
                    up = Vector3(0, 0, 1);
                }
                Transform3D t = Transform3D();
                t = t.looking_at(position + n * 0.005f + n, up);
                t.origin = position + n * 0.005f;
                d.node->set_global_transform(t);
            }

            d.node->set_visible(true);
        }

        s_next_decal = (s_next_decal + 1) % MAX_DECALS;
    }
}

/* ─────────────────────────────────────────────────────────────────── */

void Godot_Impact_Update(float delta) {
    if (!s_initialised) return;

    /* — Update particles — */
    for (int i = 0; i < MAX_IMPACT_PARTICLES; i++) {
        ImpactParticle &p = s_particles[i];
        if (!p.active) continue;

        p.lifetime -= delta;
        if (p.lifetime <= 0.0f) {
            p.active = false;
            if (p.node) p.node->set_visible(false);
            continue;
        }

        /* Apply gravity (scaled per surface template) */
        p.velocity.y -= GRAVITY_ACCEL * p.gravity_scale * delta;

        /* Move */
        if (p.node) {
            Vector3 pos = p.node->get_global_position();
            pos += p.velocity * delta;
            p.node->set_global_position(pos);

            /* Fade alpha over lifetime */
            float alpha = p.lifetime / p.max_lifetime;
            Ref<Material> mat = p.node->get_surface_override_material(0);
            Ref<StandardMaterial3D> smat = mat;
            if (smat.is_valid()) {
                Color c = smat->get_albedo();
                c.a = alpha;
                smat->set_albedo(c);
            }
        }
    }

    /* — Update decals — */
    for (int i = 0; i < MAX_DECALS; i++) {
        ImpactDecal &d = s_decals[i];
        if (!d.active) continue;

        d.lifetime -= delta;
        if (d.lifetime <= 0.0f) {
            d.active = false;
            if (d.node) d.node->set_visible(false);
            continue;
        }

        /* Fade decal alpha in the last 20% of its life */
        if (d.node && d.max_lifetime > 0.0f) {
            float ratio = d.lifetime / d.max_lifetime;
            if (ratio < 0.2f) {
                float alpha = ratio / 0.2f;
                Ref<Material> mat = d.node->get_surface_override_material(0);
                Ref<StandardMaterial3D> smat = mat;
                if (smat.is_valid()) {
                    Color c = smat->get_albedo();
                    c.a = 0.8f * alpha;
                    smat->set_albedo(c);
                }
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────── */

void Godot_Impact_Shutdown(void) {
    if (s_impact_root) {
        /* Particles and decals are children — remove_child + memdelete */
        for (int i = 0; i < MAX_IMPACT_PARTICLES; i++) {
            if (s_particles[i].node) {
                if (s_particles[i].node->get_parent()) {
                    s_particles[i].node->get_parent()->remove_child(
                        s_particles[i].node);
                }
                memdelete(s_particles[i].node);
                s_particles[i].node   = nullptr;
                s_particles[i].active = false;
            }
        }
        for (int i = 0; i < MAX_DECALS; i++) {
            if (s_decals[i].node) {
                if (s_decals[i].node->get_parent()) {
                    s_decals[i].node->get_parent()->remove_child(
                        s_decals[i].node);
                }
                memdelete(s_decals[i].node);
                s_decals[i].node   = nullptr;
                s_decals[i].active = false;
            }
        }
        if (s_impact_root->get_parent()) {
            s_impact_root->get_parent()->remove_child(s_impact_root);
        }
        memdelete(s_impact_root);
        s_impact_root = nullptr;
    }

    s_next_particle = 0;
    s_next_decal    = 0;
    s_initialised   = false;
}

/* ─────────────────────────────────────────────────────────────────── */

ImpactSurfaceType Godot_Impact_SurfaceFromFlags(int surfaceFlags) {
    if (surfaceFlags & 0x8000)    return IMPACT_METAL;    /* SURF_METAL   */
    if (surfaceFlags & 0x4000)    return IMPACT_WOOD;     /* SURF_WOOD    */
    if (surfaceFlags & 0x10000)   return IMPACT_STONE;    /* SURF_ROCK    */
    if (surfaceFlags & 0x20000)   return IMPACT_DIRT;     /* SURF_DIRT    */
    if (surfaceFlags & 0x80000)   return IMPACT_GRASS;    /* SURF_GRASS   */
    if (surfaceFlags & 0x400000)  return IMPACT_GLASS;    /* SURF_GLASS   */
    if (surfaceFlags & 0x40)      return IMPACT_FLESH;    /* SURF_FLESH   */
    if (surfaceFlags & 0x1000000) return IMPACT_SAND;     /* SURF_SAND    */
    if (surfaceFlags & 0x4000000) return IMPACT_SNOW;     /* SURF_SNOW    */
    if (surfaceFlags & 0x100000)  return IMPACT_MUD;      /* SURF_MUD     */
    if (surfaceFlags & 0x800000)  return IMPACT_GRAVEL;   /* SURF_GRAVEL  */
    if (surfaceFlags & 0x2000000) return IMPACT_FOLIAGE;  /* SURF_FOLIAGE */
    if (surfaceFlags & 0x8000000) return IMPACT_CARPET;   /* SURF_CARPET  */
    if (surfaceFlags & 0x2000)    return IMPACT_PAPER;    /* SURF_PAPER   */
    if (surfaceFlags & 0x40000)   return IMPACT_GRILL;    /* SURF_GRILL   */
    return IMPACT_DEFAULT;
}
