/*
 * godot_particles.h — Particle and effect rendering manager.
 *
 * Consolidates all particle-type effect rendering:
 * - Bullet tracers (from cg_parsemsg.cpp buffered data)
 * - Muzzle flashes (weapon fire events)
 * - Shell casings (weapon eject events)
 * - Blood spray particles (flesh hit events)
 * - Explosion debris (temp models + VFX)
 * - Smoke puffs (impact effects)
 *
 * Separates particle rendering from the generic poly buffer to allow
 * specialized handling (GPU particles, instanced meshes, particle pools).
 *
 * Phase 134: Particle Rendering Parity
 */

#ifndef GODOT_PARTICLES_H
#define GODOT_PARTICLES_H

#ifdef __cplusplus
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

/* ── C++ API for MoHAARunner ── */
namespace GodotParticles {
    void Init(Node3D *parent);
    void Shutdown(void);
    void Update(float delta_time);
    void Clear(void);
}

extern "C" {
#endif

/* ── C API for cgame/engine access ── */

/* Bullet tracer rendering from cg_parsemsg.cpp buffer */
void Godot_Particles_RenderTracers(void);

/* Muzzle flash creation (called from weapon fire events) */
void Godot_Particles_CreateMuzzleFlash(const float origin[3],
                                       const float angles[3],
                                       int large);

/* Shell casing ejection (called from weapon animation events) */
void Godot_Particles_EjectShellCasing(const float origin[3],
                                      const float velocity[3],
                                      int weapon_type);

/* Blood spray particle burst (called from flesh impact events) */
void Godot_Particles_CreateBloodSpray(const float origin[3],
                                      const float direction[3],
                                      int intensity);

/* Explosion debris cloud (called from explosion effects) */
void Godot_Particles_CreateExplosionDebris(const float origin[3],
                                           int explosion_type,
                                           float radius);

/* Smoke puff (impact effects, grenades) */
void Godot_Particles_CreateSmokePuff(const float origin[3],
                                     const float velocity[3],
                                     int smoke_type,
                                     float lifetime);

/* Frame reset — clear one-shot particles */
void Godot_Particles_BeginFrame(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_PARTICLES_H */
