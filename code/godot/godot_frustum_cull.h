/*
 * godot_frustum_cull.h — Camera frustum culling helpers for entities & effects.
 *
 * Phase 258: Extract the six frustum planes from the active Camera3D and
 *            provide fast AABB / sphere visibility tests.  Entities and
 *            effects whose bounding volumes are entirely outside the
 *            frustum can be skipped, reducing draw calls when many objects
 *            exist off-screen.
 *
 * MoHAARunner Integration Required:
 *   1. Call Godot_FrustumCull_Init() once at startup (e.g. in _ready()).
 *   2. Each frame, after updating the Camera3D, call
 *      Godot_FrustumCull_UpdateCamera(camera) to refresh the frustum
 *      planes from the current view-projection matrix.
 *   3. In update_entities(), for each entity:
 *        - compute the entity AABB (from model bounds + position)
 *        - if !Godot_FrustumCull_TestAABB(aabb): skip this entity
 *   4. Call Godot_FrustumCull_Shutdown() on teardown.
 */

#ifndef GODOT_FRUSTUM_CULL_H
#define GODOT_FRUSTUM_CULL_H

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/vector3.hpp>

/*
 * Godot_FrustumCull_Init — initialise internal state.
 *
 * Safe to call multiple times; resets planes and stats.
 */
void Godot_FrustumCull_Init(void);

/*
 * Godot_FrustumCull_UpdateCamera — extract 6 frustum planes from camera.
 *
 * @param camera  The active Camera3D node.  Must be valid and inside the
 *                scene tree.  Reads its projection and global transform
 *                to build the combined view-projection matrix, then
 *                extracts left/right/bottom/top/near/far planes.
 *
 * Also resets per-frame stats counters.
 */
void Godot_FrustumCull_UpdateCamera(godot::Camera3D *camera);

/*
 * Godot_FrustumCull_TestAABB — test an axis-aligned bounding box.
 *
 * @param bounds  AABB in Godot world-space.
 * @return true if the AABB is potentially visible (intersects or is
 *         inside the frustum), false if entirely outside.
 *
 * Uses the standard "p-vertex" test: for each frustum plane, find the
 * AABB vertex most in the direction of the plane normal.  If that vertex
 * is behind the plane, the entire box is outside.
 *
 * Increments the internal "tested" counter; increments "culled" on false.
 */
bool Godot_FrustumCull_TestAABB(const godot::AABB &bounds);

/*
 * Godot_FrustumCull_TestSphere — test a bounding sphere.
 *
 * @param center  Sphere centre in Godot world-space.
 * @param radius  Sphere radius (metres).
 * @return true if potentially visible, false if entirely outside.
 *
 * For each plane, computes the signed distance from center to the plane.
 * If distance < -radius for any plane, the sphere is outside.
 *
 * Increments the internal "tested" counter; increments "culled" on false.
 */
bool Godot_FrustumCull_TestSphere(const godot::Vector3 &center, float radius);

/*
 * Godot_FrustumCull_GetStats — retrieve per-frame culling statistics.
 *
 * @param tested  [out] Number of entities/effects tested this frame.
 * @param culled  [out] Number of entities/effects culled (outside frustum).
 *
 * Counters are reset each time Godot_FrustumCull_UpdateCamera() is called.
 */
void Godot_FrustumCull_GetStats(int *tested, int *culled);

/*
 * Godot_FrustumCull_Shutdown — release internal state.
 */
void Godot_FrustumCull_Shutdown(void);

#endif /* GODOT_FRUSTUM_CULL_H */
