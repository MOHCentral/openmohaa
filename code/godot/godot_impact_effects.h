/*
 * godot_impact_effects.h — Impact effect spawner: per-surface particle
 * bursts and decals for bullet/projectile hits.
 *
 * The engine classifies surfaces via SURF_* flags (surfaceflags.h).
 * This module maps those to Godot-rendered particle effects — small
 * billboard quads that fly outward in a cone around the hit normal,
 * fade over their lifetime, and fall under gravity.
 *
 * Integration: MoHAARunner (or whoever processes impact events)
 * calls Godot_Impact_Init() once after the 3D scene is set up,
 * Godot_Impact_Spawn() for each impact, and Godot_Impact_Update()
 * every frame.  If Agent 12's VFX foundation (godot_vfx.h) is
 * available, the impact root is attached under the VFX parent;
 * otherwise a standalone Node3D is created.
 */

#ifndef GODOT_IMPACT_EFFECTS_H
#define GODOT_IMPACT_EFFECTS_H

#ifdef __cplusplus

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

/* ── Surface type enum (matches engine SURF_* categories) ── */
enum ImpactSurfaceType {
    IMPACT_DEFAULT = 0,
    IMPACT_METAL,
    IMPACT_WOOD,
    IMPACT_STONE,
    IMPACT_DIRT,
    IMPACT_GRASS,
    IMPACT_WATER,
    IMPACT_GLASS,
    IMPACT_FLESH,
    IMPACT_SAND,
    IMPACT_SNOW,
    IMPACT_MUD,
    IMPACT_GRAVEL,
    IMPACT_FOLIAGE,
    IMPACT_CARPET,
    IMPACT_PAPER,
    IMPACT_GRILL,
    IMPACT_COUNT
};

/* ── Per-surface effect template ── */
struct ImpactTemplate {
    int   particle_count;       /* number of particles to spawn (5–30) */
    float particle_velocity;    /* base outward speed in m/s           */
    float particle_lifetime;    /* seconds before particle fades out   */
    float particle_colour[4];   /* RGBA start colour                   */
    float particle_size;        /* billboard quad half-size in metres   */
    const char *decal_texture;  /* engine texture path for decal       */
    float decal_size;           /* decal quad half-size in metres       */
    float decal_lifetime;       /* seconds (0 = permanent)             */
    float gravity_scale;        /* multiplier on downward acceleration  */
    float spread_angle;         /* cone half-angle in degrees           */
};

/*
 * Initialise the impact effect system.
 * Creates the particle pool and a root Node3D under the given parent.
 *
 * @param parent  Scene node to parent impact effects under.
 */
void Godot_Impact_Init(godot::Node3D *parent);

/*
 * Spawn an impact burst at the given position facing along the
 * surface normal.
 *
 * @param type      Surface type — selects the visual template.
 * @param position  Hit point in Godot world coordinates.
 * @param normal    Surface normal at the hit point (Godot coords).
 */
void Godot_Impact_Spawn(ImpactSurfaceType type,
                        const godot::Vector3 &position,
                        const godot::Vector3 &normal);

/*
 * Update all active impact particles: apply velocity, gravity,
 * alpha fade, and recycle expired particles back to the pool.
 *
 * @param delta  Frame delta time in seconds.
 */
void Godot_Impact_Update(float delta);

/*
 * Shut down the impact system: free all particles and remove nodes.
 */
void Godot_Impact_Shutdown(void);

/*
 * Convert an engine SURF_* bit-mask to an ImpactSurfaceType enum.
 * Only the surface-material bits (MASK_SURF_TYPE + SURF_FLESH) are
 * inspected.  Returns IMPACT_DEFAULT if no known bit is set.
 */
ImpactSurfaceType Godot_Impact_SurfaceFromFlags(int surfaceFlags);

#endif /* __cplusplus */

#endif /* GODOT_IMPACT_EFFECTS_H */
