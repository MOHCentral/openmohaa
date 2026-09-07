/*
 * godot_frustum_cull.cpp — Camera frustum culling helpers.
 *
 * Uses Camera3D::get_frustum() to obtain the six frustum planes,
 * then provides AABB / sphere visibility tests using p-vertex method.
 */

#include "godot_frustum_cull.h"

#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

/* ── Internal state ── */

static Plane s_planes[6];
static int   s_num_planes = 0;
static bool  s_valid = false;
static int   s_tested = 0;
static int   s_culled = 0;

/* ===================================================================
 *  Init / Shutdown
 * ================================================================ */

void Godot_FrustumCull_Init(void)
{
    for (int i = 0; i < 6; i++) {
        s_planes[i] = Plane();
    }
    s_num_planes = 0;
    s_valid  = false;
    s_tested = 0;
    s_culled = 0;
}

void Godot_FrustumCull_Shutdown(void)
{
    s_valid  = false;
    s_num_planes = 0;
    s_tested = 0;
    s_culled = 0;
}

/* ===================================================================
 *  Frustum plane extraction — delegates to Camera3D::get_frustum()
 * ================================================================ */

void Godot_FrustumCull_UpdateCamera(Camera3D *camera)
{
    s_tested = 0;
    s_culled = 0;

    if (!camera) {
        s_valid = false;
        return;
    }

    TypedArray<Plane> planes = camera->get_frustum();
    s_num_planes = (planes.size() > 6) ? 6 : (int)planes.size();

    for (int i = 0; i < s_num_planes; i++) {
        s_planes[i] = (Plane)planes[i];
    }

    s_valid = (s_num_planes >= 6);
}

/* ===================================================================
 *  AABB test — "p-vertex" method
 *
 *  For each plane, compute the "positive vertex" (the corner of the
 *  AABB furthest in the direction of the plane normal).  If that vertex
 *  is behind the plane (signed distance < 0) the AABB is entirely
 *  outside.
 * ================================================================ */

bool Godot_FrustumCull_TestAABB(const AABB &bounds)
{
    s_tested++;

    if (!s_valid) {
        return true;  /* No frustum → assume visible. */
    }

    Vector3 min_pt = bounds.position;
    Vector3 max_pt = bounds.position + bounds.size;

    for (int i = 0; i < s_num_planes; i++) {
        const Vector3 &n = s_planes[i].normal;
        real_t         d = s_planes[i].d;

        /* Positive vertex: for each axis, pick max if normal > 0, min otherwise. */
        Vector3 p_vertex(
            (n.x >= 0.0f) ? max_pt.x : min_pt.x,
            (n.y >= 0.0f) ? max_pt.y : min_pt.y,
            (n.z >= 0.0f) ? max_pt.z : min_pt.z
        );

        /* Signed distance = dot(normal, vertex) + d.
         * Godot's Plane stores: normal · X = d  (point on plane satisfies this).
         * distance_to(point) = normal.dot(point) - d.
         * If distance_to(p_vertex) < 0, the p-vertex is behind the plane. */
        if (s_planes[i].distance_to(p_vertex) < 0.0f) {
            s_culled++;
            return false;
        }
    }

    return true;
}

/* ===================================================================
 *  Sphere test
 * ================================================================ */

bool Godot_FrustumCull_TestSphere(const Vector3 &center, float radius)
{
    s_tested++;

    if (!s_valid) {
        return true;
    }

    for (int i = 0; i < s_num_planes; i++) {
        if (s_planes[i].distance_to(center) < -radius) {
            s_culled++;
            return false;
        }
    }

    return true;
}

/* ===================================================================
 *  Stats
 * ================================================================ */

void Godot_FrustumCull_GetStats(int *tested, int *culled)
{
    if (tested) *tested = s_tested;
    if (culled) *culled = s_culled;
}
