/* godot_weapon_effects.h — Muzzle flash & shell casing visual effects
 *
 * Provides a pooled system for two weapon-related visual effects:
 *
 *  1. Muzzle flashes: short-lived billboard sprites with additive blending
 *     and a warm OmniLight3D that fades over ~0.08 seconds.
 *
 *  2. Shell casings: small brass-coloured cylinder meshes ejected with
 *     parabolic trajectories, spin, a single bounce, and a 2-second fade.
 *
 * Both systems use fixed-size pools recycled in ring-buffer order.
 *
 * Usage:
 *   Godot_MuzzleFlash_Spawn(pos, dir, intensity);
 *   Godot_ShellCasing_Eject(pos, vel, type);
 *   // each frame:
 *   Godot_MuzzleFlash_Update(delta);
 *   Godot_ShellCasing_Update(delta);
 *   // on map change / shutdown:
 *   Godot_ShellCasing_Clear();
 *
 * Phase 224.
 */

#ifndef GODOT_WEAPON_EFFECTS_H
#define GODOT_WEAPON_EFFECTS_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

/* ── Casing type constants ── */
#define CASING_TYPE_PISTOL  0
#define CASING_TYPE_RIFLE   1
#define CASING_TYPE_SHOTGUN 2

/* ── Muzzle flash API ── */

/* Spawn a muzzle flash billboard + OmniLight3D at the given world position.
 * |direction| is the barrel forward vector (used to offset the quad).
 * |intensity| scales the light energy.  */
void Godot_MuzzleFlash_Spawn(const godot::Vector3 &position,
                             const godot::Vector3 &direction,
                             float intensity);

/* Advance all active flashes by |delta| seconds, fading and recycling.  */
void Godot_MuzzleFlash_Update(float delta);

/* ── Shell casing API ── */

/* Eject a shell casing from |position| with initial |velocity|.
 * |casing_type| is one of CASING_TYPE_PISTOL / RIFLE / SHOTGUN.  */
void Godot_ShellCasing_Eject(const godot::Vector3 &position,
                             const godot::Vector3 &velocity,
                             int casing_type);

/* Advance all active casings by |delta| seconds (gravity, spin, bounce).  */
void Godot_ShellCasing_Update(float delta);

/* Remove all active casings and hide their scene nodes.  */
void Godot_ShellCasing_Clear(void);

/* ── Lifecycle helpers (called from MoHAARunner or similar) ── */

/* Attach internal pool nodes to |parent|.  Call once after scene is ready.  */
void Godot_WeaponEffects_Init(godot::Node3D *parent);

/* Detach and free all pool nodes.  Call on shutdown / map unload.  */
void Godot_WeaponEffects_Cleanup(void);

#endif /* GODOT_WEAPON_EFFECTS_H */
