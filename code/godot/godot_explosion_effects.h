/*
 * godot_explosion_effects.h — Explosion visual effects and camera shake API.
 *
 * Provides a pooled explosion system (fireball, smoke ring, debris chunks,
 * short-lived omni light) and a camera shake API that multiple systems can
 * trigger.  Explosions automatically invoke camera shake based on distance.
 *
 * Integration: MoHAARunner (or any Godot-side system) calls the Init/Update/
 * Shutdown functions.  Engine code can trigger explosions through the C
 * accessor Godot_Explosion_Spawn().
 */

#ifndef GODOT_EXPLOSION_EFFECTS_H
#define GODOT_EXPLOSION_EFFECTS_H

#ifdef __cplusplus

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

/* -------------------------------------------------------------------
 *  Camera Shake API
 * ---------------------------------------------------------------- */

/*
 * Trigger a camera shake event.
 *
 * @param intensity        Maximum offset amplitude in metres.
 * @param duration         How long the shake lasts (seconds).
 * @param falloff_distance Distance (metres) beyond which the shake is silent.
 * @param source_pos       World-space origin of the shake event.
 */
void Godot_CameraShake_Trigger(float intensity, float duration,
                               float falloff_distance,
                               const godot::Vector3 &source_pos);

/*
 * Apply accumulated camera shake to the given camera.
 * Call once per frame from the main update loop.  The offset is applied
 * additively and restored on the next call.
 *
 * @param delta   Frame delta time in seconds.
 * @param camera  The active Camera3D node.
 */
void Godot_CameraShake_Update(float delta, godot::Camera3D *camera);

/*
 * Clear all pending camera shake events (e.g. on map change).
 */
void Godot_CameraShake_Clear(void);

/* -------------------------------------------------------------------
 *  Explosion Effect API
 * ---------------------------------------------------------------- */

/*
 * Initialise the explosion system.  Creates pooled node hierarchies
 * as children of the given parent node.
 *
 * @param parent  Scene node to attach explosion objects to.
 */
void Godot_Explosion_Init(godot::Node3D *parent);

/*
 * Spawn an explosion at the given world position.
 *
 * @param position   World-space centre of the explosion.
 * @param radius     Maximum expansion radius in metres.
 * @param intensity  Visual intensity (affects light brightness and shake).
 */
void Godot_Explosion_Spawn(const godot::Vector3 &position, float radius,
                           float intensity);

/*
 * Update all active explosions.  Call once per frame.
 *
 * @param delta  Frame delta time in seconds.
 */
void Godot_Explosion_Update(float delta);

/*
 * Shut down the explosion system: free pooled nodes and resources.
 */
void Godot_Explosion_Shutdown(void);

/*
 * Clear all active explosions without freeing pools (e.g. on map change).
 */
void Godot_Explosion_Clear(void);

#endif /* __cplusplus */

#endif /* GODOT_EXPLOSION_EFFECTS_H */
