/*
 * godot_terrain_normal.h — Terrain normal computation logic.
 */

#ifndef GODOT_TERRAIN_NORMAL_H
#define GODOT_TERRAIN_NORMAL_H

#include <godot_cpp/variant/vector3.hpp>
#include <cmath>
#include <cstdint>

namespace godot {

/* ===================================================================
 *  Coordinate conversion
 * ================================================================ */

inline Vector3 id_to_godot_dir(const float *v) {
    return Vector3(-v[1], v[2], -v[0]);
}

/* ===================================================================
 *  Terrain normal computation
 *
 *  Computes a smooth normal for a vertex in a 9×9 terrain heightmap
 *  using central finite differences (forward/backward at edges).
 *  Returns the normal in Godot coordinate space.
 * ================================================================ */

inline Vector3 compute_terrain_normal(const uint8_t *heightmap, int col, int row) {
    // Central differences with clamping at edges
    int left  = (col > 0) ? col - 1 : col;
    int right = (col < 8) ? col + 1 : col;
    int down  = (row > 0) ? row - 1 : row;
    int up    = (row < 8) ? row + 1 : row;

    // Height difference in each direction (heights are scaled ×2 in world)
    float dz_dx = (float)(heightmap[row * 9 + right] - heightmap[row * 9 + left]) * 2.0f;
    float dz_dy = (float)(heightmap[up * 9 + col]    - heightmap[down * 9 + col])  * 2.0f;

    // Actual world-space step (64 units per cell, ×1 or ×2 for forward/central)
    float step_x = (float)(right - left) * 64.0f;
    float step_y = (float)(up    - down)  * 64.0f;

    // Normal via cross product of tangent vectors:
    //   T_x = (step_x, 0, dz_dx),  T_y = (0, step_y, dz_dy)
    //   N = T_x × T_y = (-dz_dx * step_y, -step_x * dz_dy, step_x * step_y)
    float nx = -dz_dx * step_y;
    float ny = -step_x * dz_dy;
    float nz = step_x * step_y;

    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.001f) {
        nx /= len;
        ny /= len;
        nz /= len;
    } else {
        nx = 0.0f;
        ny = 0.0f;
        nz = 1.0f;
    }

    // Convert engine normal (x, y, z) → Godot
    float engine_n[3] = {nx, ny, nz};
    return id_to_godot_dir(engine_n);
}

} // namespace godot

#endif // GODOT_TERRAIN_NORMAL_H
