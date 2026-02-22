/* godot_weapon_effects.cpp — Muzzle flash sprites & ejected shell casings
 *
 * Implements two pooled visual-effect systems driven by the weapon code:
 *
 *  • Muzzle flashes — billboard quad (additive blending, bright yellow-white)
 *    plus a short-lived OmniLight3D (warm yellow, ~3 m range, 0.08 s).
 *    Pool of 8 slots recycled in ring-buffer order.
 *
 *  • Shell casings — small brass-coloured cylinder MeshInstance3D ejected
 *    with parabolic gravity, random spin, one bounce, and a 2 s fade-out.
 *    Pool of 32 slots recycled in ring-buffer order.
 *
 * Both pools pre-create their Godot scene nodes once during
 * Godot_WeaponEffects_Init() and reuse them every frame.
 *
 * Phase 224.
 */

#include "godot_weapon_effects.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <cmath>
#include <cstdlib>
#include <ctime>

using namespace godot;

/* ─────────────────────────────────────────────────────────────────────────
 *  Constants
 * ───────────────────────────────────────────────────────────────────────── */

#define MUZZLE_FLASH_MAX       8
#define MUZZLE_FLASH_LIFETIME  0.08f   /* seconds — fast fade              */
#define MUZZLE_FLASH_RANGE     3.0f    /* metres — OmniLight3D range       */

#define SHELL_CASING_MAX       32
#define SHELL_CASING_LIFETIME  2.0f    /* seconds before fade-out          */
#define SHELL_CASING_GRAVITY   9.8f    /* m/s²                             */
#define SHELL_CASING_BOUNCE    0.3f    /* velocity retention on bounce     */
#define SHELL_CASING_SPIN_SPEED 720.0f /* degrees per second               */

/* ─────────────────────────────────────────────────────────────────────────
 *  Muzzle flash pool
 * ───────────────────────────────────────────────────────────────────────── */

struct MuzzleFlashSlot {
    MeshInstance3D *mesh;    /* billboard quad                            */
    OmniLight3D    *light;   /* warm dynamic light                        */
    float           age;     /* seconds since spawn                       */
    float           initial_energy; /* light energy at spawn time         */
    bool            active;  /* currently visible?                        */
};

static MuzzleFlashSlot s_flashes[MUZZLE_FLASH_MAX];
static int             s_flash_next = 0;   /* ring-buffer write index     */

/* ─────────────────────────────────────────────────────────────────────────
 *  Shell casing pool
 * ───────────────────────────────────────────────────────────────────────── */

struct ShellCasingSlot {
    MeshInstance3D *mesh;
    Vector3         position;
    Vector3         velocity;
    Vector3         spin_axis;
    float           spin_angle;   /* accumulated degrees                  */
    float           age;
    bool            active;
    bool            bounced;      /* has already bounced once?            */
};

static ShellCasingSlot s_casings[SHELL_CASING_MAX];
static int             s_casing_next = 0;

/* ─────────────────────────────────────────────────────────────────────────
 *  Shared materials (created once, reused by all slots)
 * ───────────────────────────────────────────────────────────────────────── */

static Ref<StandardMaterial3D> s_flash_material;
static Ref<StandardMaterial3D> s_casing_materials[3];  /* pistol / rifle / shotgun */

static Node3D *s_parent = nullptr;

/* ─────────────────────────────────────────────────────────────────────────
 *  Helpers
 * ───────────────────────────────────────────────────────────────────────── */

static Ref<StandardMaterial3D> make_flash_material() {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
    mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    mat->set_albedo(Color(1.0f, 0.95f, 0.7f, 1.0f)); /* warm yellow-white */
    mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
    return mat;
}

static Ref<StandardMaterial3D> make_casing_material() {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(Color(0.72f, 0.56f, 0.3f, 1.0f)); /* brass colour */
    mat->set_metallic(0.8f);
    mat->set_roughness(0.3f);
    return mat;
}

/* Return a random float in [0, 1). */
static float rand01() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

/* Return a random unit vector. */
static Vector3 rand_unit_vector() {
    Vector3 v(rand01() - 0.5f, rand01() - 0.5f, rand01() - 0.5f);
    float len = v.length();
    if (len < 0.0001f) {
        return Vector3(0.0f, 1.0f, 0.0f);
    }
    return v / len;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Lifecycle
 * ───────────────────────────────────────────────────────────────────────── */

void Godot_WeaponEffects_Init(Node3D *parent) {
    s_parent = parent;
    if (!s_parent) {
        return;
    }

    /* — seed random for spin axis variation — */
    srand(static_cast<unsigned>(time(nullptr)));

    /* — shared materials — */
    s_flash_material = make_flash_material();

    for (int i = 0; i < 3; i++) {
        s_casing_materials[i] = make_casing_material();
    }

    /* — muzzle flash pool — */
    for (int i = 0; i < MUZZLE_FLASH_MAX; i++) {
        MuzzleFlashSlot &slot = s_flashes[i];

        /* billboard quad */
        slot.mesh = memnew(MeshInstance3D);
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(0.25f, 0.25f));
        quad->set_material(s_flash_material);
        slot.mesh->set_mesh(quad);
        slot.mesh->set_visible(false);
        s_parent->add_child(slot.mesh);

        /* omni light */
        slot.light = memnew(OmniLight3D);
        slot.light->set_color(Color(1.0f, 0.85f, 0.4f));
        slot.light->set_param(Light3D::PARAM_RANGE, MUZZLE_FLASH_RANGE);
        slot.light->set_param(Light3D::PARAM_ATTENUATION, 2.0f);
        slot.light->set_visible(false);
        s_parent->add_child(slot.light);

        slot.age    = 0.0f;
        slot.active = false;
    }

    /* — shell casing pool — */
    for (int i = 0; i < SHELL_CASING_MAX; i++) {
        ShellCasingSlot &slot = s_casings[i];

        slot.mesh = memnew(MeshInstance3D);
        Ref<CylinderMesh> cyl;
        cyl.instantiate();
        /* default pistol size; overridden per-eject */
        cyl->set_top_radius(0.0025f);
        cyl->set_bottom_radius(0.0025f);
        cyl->set_height(0.01f);
        cyl->set_material(s_casing_materials[CASING_TYPE_PISTOL]);
        slot.mesh->set_mesh(cyl);
        slot.mesh->set_visible(false);
        s_parent->add_child(slot.mesh);

        slot.active = false;
        slot.bounced = false;
        slot.age = 0.0f;
        slot.spin_angle = 0.0f;
    }

    s_flash_next  = 0;
    s_casing_next = 0;
}

void Godot_WeaponEffects_Cleanup() {
    /* Hide everything — nodes are owned by s_parent and freed with the
     * scene tree, so we only need to deactivate. */
    for (int i = 0; i < MUZZLE_FLASH_MAX; i++) {
        s_flashes[i].active = false;
        if (s_flashes[i].mesh) {
            s_flashes[i].mesh->set_visible(false);
        }
        if (s_flashes[i].light) {
            s_flashes[i].light->set_visible(false);
        }
    }
    for (int i = 0; i < SHELL_CASING_MAX; i++) {
        s_casings[i].active = false;
        if (s_casings[i].mesh) {
            s_casings[i].mesh->set_visible(false);
        }
    }

    s_parent       = nullptr;
    s_flash_next   = 0;
    s_casing_next  = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Muzzle flash
 * ───────────────────────────────────────────────────────────────────────── */

void Godot_MuzzleFlash_Spawn(const Vector3 &position,
                             const Vector3 &direction,
                             float intensity) {
    if (!s_parent) {
        return;
    }

    MuzzleFlashSlot &slot = s_flashes[s_flash_next];
    s_flash_next = (s_flash_next + 1) % MUZZLE_FLASH_MAX;

    /* Position the quad slightly ahead of the muzzle */
    Vector3 pos = position + direction.normalized() * 0.05f;

    slot.mesh->set_global_transform(Transform3D(Basis(), pos));
    slot.mesh->set_scale(Vector3(1.0f, 1.0f, 1.0f));
    slot.mesh->set_visible(true);

    slot.light->set_global_transform(Transform3D(Basis(), pos));
    slot.light->set_param(Light3D::PARAM_ENERGY, intensity);
    slot.light->set_visible(true);

    slot.age            = 0.0f;
    slot.initial_energy = intensity;
    slot.active         = true;
}

void Godot_MuzzleFlash_Update(float delta) {
    for (int i = 0; i < MUZZLE_FLASH_MAX; i++) {
        MuzzleFlashSlot &slot = s_flashes[i];
        if (!slot.active) {
            continue;
        }

        slot.age += delta;
        if (slot.age >= MUZZLE_FLASH_LIFETIME) {
            /* expired — hide and deactivate */
            slot.mesh->set_visible(false);
            slot.light->set_visible(false);
            slot.active = false;
            continue;
        }

        /* fade out alpha + light energy linearly */
        float t = slot.age / MUZZLE_FLASH_LIFETIME;  /* 0→1 */
        float alpha = 1.0f - t;

        /* Scale billboard quad down as it fades (visual fade substitute) */
        slot.mesh->set_scale(Vector3(alpha, alpha, alpha));

        /* Dim the light proportionally from its initial energy */
        slot.light->set_param(Light3D::PARAM_ENERGY,
                              slot.initial_energy * alpha);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 *  Shell casings
 * ───────────────────────────────────────────────────────────────────────── */

void Godot_ShellCasing_Eject(const Vector3 &position,
                             const Vector3 &velocity,
                             int casing_type) {
    if (!s_parent) {
        return;
    }

    if (casing_type < 0 || casing_type > 2) {
        casing_type = CASING_TYPE_PISTOL;
    }

    ShellCasingSlot &slot = s_casings[s_casing_next];
    s_casing_next = (s_casing_next + 1) % SHELL_CASING_MAX;

    /* Size the cylinder mesh for the casing type */
    Ref<CylinderMesh> cyl;
    cyl.instantiate();
    switch (casing_type) {
        case CASING_TYPE_RIFLE:
            cyl->set_top_radius(0.003f);
            cyl->set_bottom_radius(0.003f);
            cyl->set_height(0.014f);
            break;
        case CASING_TYPE_SHOTGUN:
            cyl->set_top_radius(0.005f);
            cyl->set_bottom_radius(0.005f);
            cyl->set_height(0.018f);
            break;
        default: /* pistol */
            cyl->set_top_radius(0.0025f);
            cyl->set_bottom_radius(0.0025f);
            cyl->set_height(0.01f);
            break;
    }
    cyl->set_material(s_casing_materials[casing_type]);
    slot.mesh->set_mesh(cyl);

    slot.position   = position;
    slot.velocity   = velocity;
    slot.spin_axis  = rand_unit_vector();
    slot.spin_angle = 0.0f;
    slot.age        = 0.0f;
    slot.active     = true;
    slot.bounced    = false;

    slot.mesh->set_visible(true);
    slot.mesh->set_global_transform(Transform3D(Basis(), position));
}

void Godot_ShellCasing_Update(float delta) {
    for (int i = 0; i < SHELL_CASING_MAX; i++) {
        ShellCasingSlot &slot = s_casings[i];
        if (!slot.active) {
            continue;
        }

        slot.age += delta;

        /* Lifetime expiry — fade and recycle */
        if (slot.age >= SHELL_CASING_LIFETIME) {
            slot.mesh->set_visible(false);
            slot.active = false;
            continue;
        }

        /* Gravity (Godot Y-up) */
        slot.velocity.y -= SHELL_CASING_GRAVITY * delta;

        /* Integrate position */
        slot.position += slot.velocity * delta;

        /* Simple ground bounce at Y = 0 */
        if (slot.position.y < 0.0f && !slot.bounced) {
            slot.position.y = 0.0f;
            slot.velocity.y = -slot.velocity.y * SHELL_CASING_BOUNCE;
            slot.velocity.x *= SHELL_CASING_BOUNCE;
            slot.velocity.z *= SHELL_CASING_BOUNCE;
            slot.bounced = true;
        } else if (slot.position.y < 0.0f && slot.bounced) {
            /* Already bounced — rest on the ground */
            slot.position.y = 0.0f;
            slot.velocity   = Vector3(0.0f, 0.0f, 0.0f);
        }

        /* Spin rotation */
        slot.spin_angle += SHELL_CASING_SPIN_SPEED * delta;

        Basis spin_basis;
        spin_basis = spin_basis.rotated(slot.spin_axis,
                                        Math::deg_to_rad(slot.spin_angle));

        slot.mesh->set_global_transform(
            Transform3D(spin_basis, slot.position));

        /* Fade: scale down in the last 0.5 s of lifetime */
        float fade_start = SHELL_CASING_LIFETIME - 0.5f;
        if (slot.age > fade_start) {
            float s = 1.0f - (slot.age - fade_start) / 0.5f;
            slot.mesh->set_scale(Vector3(s, s, s));
        } else {
            slot.mesh->set_scale(Vector3(1.0f, 1.0f, 1.0f));
        }
    }
}

void Godot_ShellCasing_Clear() {
    for (int i = 0; i < SHELL_CASING_MAX; i++) {
        s_casings[i].active = false;
        if (s_casings[i].mesh) {
            s_casings[i].mesh->set_visible(false);
        }
    }
    s_casing_next = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 *  C Accessor Wrappers (Phase 224 Extension)
 * ───────────────────────────────────────────────────────────────────────── */

static constexpr float MOHAA_UNIT_SCALE_C = 1.0f / 39.37f;

/* Convert id Tech 3 coordinates (X=Forward, Y=Left, Z=Up) to Godot (X=Right, Y=Up, Z=Back) */
static inline Vector3 id_to_godot_c(float ix, float iy, float iz) {
    return Vector3(-iy * MOHAA_UNIT_SCALE_C,
                    iz * MOHAA_UNIT_SCALE_C,
                   -ix * MOHAA_UNIT_SCALE_C);
}

/* Convert id Tech 3 direction vector (no scale) */
static inline Vector3 id_to_godot_dir_c(float ix, float iy, float iz) {
    return Vector3(-iy, iz, -ix);
}

extern "C" {

void Godot_MuzzleFlash_Spawn_C(float *pos, float *dir, float intensity) {
    if (!pos || !dir) return;
    Vector3 g_pos = id_to_godot_c(pos[0], pos[1], pos[2]);
    Vector3 g_dir = id_to_godot_dir_c(dir[0], dir[1], dir[2]);
    Godot_MuzzleFlash_Spawn(g_pos, g_dir, intensity);
}

void Godot_ShellCasing_Eject_C(float *pos, float *vel, int type) {
    if (!pos || !vel) return;
    Vector3 g_pos = id_to_godot_c(pos[0], pos[1], pos[2]);
    /* Velocity is a vector with units/sec, so scale it too */
    Vector3 g_vel = id_to_godot_c(vel[0], vel[1], vel[2]);

    Godot_ShellCasing_Eject(g_pos, g_vel, type);
}

}
