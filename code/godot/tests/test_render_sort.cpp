/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../godot_render_sort.h"

#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

// Helper to check conditions
#define ASSERT(cond, msg) \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s at line %d\n", msg, __LINE__); \
        return 1; \
    }

#define CHECK_EQUAL(a, b, msg) \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s (%d != %d) at line %d\n", msg, (int)(a), (int)(b), __LINE__); \
        return 1; \
    }

int main() {
    printf("Running Godot_RenderSort tests...\n");

    // Test Case 1: Empty Array
    // Ensure it doesn't crash
    Godot_RenderSort_SortEntities(nullptr, 0, godot::Vector3());
    printf("Test 1 Passed: Empty/Null handled.\n");

    // Test Case 2: Sorting Logic
    std::vector<SortableEntity> entities;

    // Opaque (sort_key <= 2): Front-to-back (ascending distance)
    entities.push_back({0, 2.0f, 100.0f, false}); // Index 0: Opaque, Far
    entities.push_back({1, 2.0f, 10.0f, false});  // Index 1: Opaque, Near
    entities.push_back({2, 1.0f, 50.0f, false});  // Index 2: Opaque (lower key), Medium

    // Transparent (sort_key > 2 && < 16): Back-to-front (descending distance)
    entities.push_back({3, 8.0f, 20.0f, false});  // Index 3: Transparent, Near
    entities.push_back({4, 8.0f, 80.0f, false});  // Index 4: Transparent, Far
    entities.push_back({5, 14.0f, 30.0f, false}); // Index 5: Transparent (higher key), Medium

    // Additive (sort_key >= 16): Back-to-front (descending distance)
    entities.push_back({6, 16.0f, 5.0f, true});   // Index 6: Additive, Near
    entities.push_back({7, 16.0f, 15.0f, true});  // Index 7: Additive, Far

    // Expected order after sort:
    // 1. Opaque, Key 1.0f, dist 50.0f (Index 2)
    // 2. Opaque, Key 2.0f, dist 10.0f (Index 1) - Closer
    // 3. Opaque, Key 2.0f, dist 100.0f (Index 0) - Further
    // 4. Transparent, Key 8.0f, dist 80.0f (Index 4) - Further first
    // 5. Transparent, Key 8.0f, dist 20.0f (Index 3) - Closer last
    // 6. Transparent, Key 14.0f, dist 30.0f (Index 5)
    // 7. Additive, Key 16.0f, dist 15.0f (Index 7) - Further first
    // 8. Additive, Key 16.0f, dist 5.0f (Index 6) - Closer last

    godot::Vector3 camera_pos; // Dummy, unused by function logic directly
    Godot_RenderSort_SortEntities(entities.data(), (int)entities.size(), camera_pos);

    // Verify
    CHECK_EQUAL(entities[0].entity_index, 2, "1st should be Opaque Key 1");
    CHECK_EQUAL(entities[1].entity_index, 1, "2nd should be Opaque Key 2 Near");
    CHECK_EQUAL(entities[2].entity_index, 0, "3rd should be Opaque Key 2 Far");
    CHECK_EQUAL(entities[3].entity_index, 4, "4th should be Transp Key 8 Far");
    CHECK_EQUAL(entities[4].entity_index, 3, "5th should be Transp Key 8 Near");
    CHECK_EQUAL(entities[5].entity_index, 5, "6th should be Transp Key 14");
    CHECK_EQUAL(entities[6].entity_index, 7, "7th should be Additive Key 16 Far");
    CHECK_EQUAL(entities[7].entity_index, 6, "8th should be Additive Key 16 Near");

    printf("Test 2 Passed: Sorting logic verified.\n");

    return 0;
}
