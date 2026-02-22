#include "../godot_terrain_normal.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

// Helper to check approximate equality
bool is_close(float a, float b, float epsilon = 0.005f) {
    return std::abs(a - b) < epsilon;
}

bool check_vector(const godot::Vector3& v, float x, float y, float z, const char* name) {
    if (is_close(v.x, x) && is_close(v.y, y) && is_close(v.z, z)) {
        std::cout << "[PASS] " << name << " -> (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
        return true;
    } else {
        std::cout << "[FAIL] " << name << " -> Expected (" << x << ", " << y << ", " << z << "), got ("
                  << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
        return false;
    }
}

int main() {
    // 9x9 heightmap (81 bytes)
    uint8_t heightmap[81] = {0};

    std::cout << "Testing Terrain Normal Computation..." << std::endl;

    // Test 1: Flat terrain
    // Height 0 everywhere.
    // Expected normal: Godot (0, 1, 0)

    godot::Vector3 n_flat = godot::compute_terrain_normal(heightmap, 4, 4);
    if (!check_vector(n_flat, 0.0f, 1.0f, 0.0f, "Flat Center (4,4)")) return 1;

    godot::Vector3 n_flat_edge = godot::compute_terrain_normal(heightmap, 0, 0);
    if (!check_vector(n_flat_edge, 0.0f, 1.0f, 0.0f, "Flat Edge (0,0)")) return 1;

    // Test 2: Sloped terrain (Engine X direction)
    // Engine X corresponds to Godot -Z.
    // If height increases with X, the slope goes UP as we move forward (-Z).
    // Normal should tilt backwards (towards +Z).
    // Or if we look at `id_to_godot_dir`: (-v[1], v[2], -v[0]) => (-y, z, -x)
    // Engine Normal for slope in X:
    // T_x = (1, 0, dz/dx), T_y = (0, 1, 0)
    // N = T_x x T_y = (-dz/dx * 0, -1 * 0, 1 * 1) -> Wait.
    // My manual calculation earlier:
    // N = (-dz_dx * step_y, -step_x * dz_dy, step_x * step_y)
    // dz_dx > 0 (height increases with x). dz_dy = 0.
    // nx < 0. ny = 0. nz > 0.
    // Engine Normal: (-x, 0, z)
    // Godot Normal: (-0, z, -(-x)) = (0, z, x).
    // So if nx is negative, Godot Z component (x) is positive. Correct.

    // Fill heightmap with x * 10
    for(int row=0; row<9; ++row) {
        for(int col=0; col<9; ++col) {
            heightmap[row*9 + col] = col * 10;
        }
    }

    // Center (4,4)
    // Expect: Godot (0, 0.954, 0.298)
    godot::Vector3 n_slope = godot::compute_terrain_normal(heightmap, 4, 4);
    if (!check_vector(n_slope, 0.0f, 0.954f, 0.298f, "Slope X Center (4,4)")) return 1;

    // Edge (0,4) - Left edge
    // Clamping logic: left=0, right=1.
    // Expect same normal because slope is constant.
    godot::Vector3 n_slope_edge = godot::compute_terrain_normal(heightmap, 0, 4);
    if (!check_vector(n_slope_edge, 0.0f, 0.954f, 0.298f, "Slope X Edge (0,4)")) return 1;

    // Corner (0,0)
    // Expect same normal.
    godot::Vector3 n_slope_corner = godot::compute_terrain_normal(heightmap, 0, 0);
    if (!check_vector(n_slope_corner, 0.0f, 0.954f, 0.298f, "Slope X Corner (0,0)")) return 1;

    // Test 3: Sloped terrain (Engine Y direction)
    // Engine Y corresponds to Godot -X.
    // Fill with y * 10
    memset(heightmap, 0, 81);
    for(int row=0; row<9; ++row) {
        for(int col=0; col<9; ++col) {
            heightmap[row*9 + col] = row * 10;
        }
    }

    // Center (4,4)
    // dz_dx = 0.
    // dz_dy = (50-30)*2 = 40.
    // step_x = 128.
    // step_y = 128.

    // nx = -0 * 128 = 0
    // ny = -128 * 40 = -5120
    // nz = 128 * 128 = 16384

    // Engine N: (0, -0.298, 0.954)
    // Godot N: (-(-0.298), 0.954, -0) => (0.298, 0.954, 0)

    godot::Vector3 n_slope_y = godot::compute_terrain_normal(heightmap, 4, 4);
    if (!check_vector(n_slope_y, 0.298f, 0.954f, 0.0f, "Slope Y Center (4,4)")) return 1;

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
