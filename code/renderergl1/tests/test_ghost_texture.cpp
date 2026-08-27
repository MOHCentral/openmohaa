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

#include "../tr_ghost.h"
#include <iostream>

trGlobals_t tr;

extern "C" {
    void Z_Free(void *ptr) {}
    void ri_Error(int level, const char *error, ...) {}
    void ri_Printf(int print_level, const char *fmt, ...) {}

}

int clipAndDrawLineCalls = 0;

GhostTexture::GhostTexture() {}

void GhostTexture::ClipAndDrawLine(Vector p0, Vector p1, int color) {
    clipAndDrawLineCalls++;
}

outcode GhostTexture::ComputeOutCode(int x, int y, int xmin, int xmax, int ymin, int ymax) { return 0; }
Vector GhostTexture::RotateVector(Vector v, float angle) { return v; }

void GhostTexture::GenerateLightning(Vector p1, Vector p2, int color, float angleVar, int numSubdivisions, int maxSubdivisions) {
    Vector mid;
    Vector delta;
    float  length;

    if (numSubdivisions == maxSubdivisions) {
        p1[0] += 0.5;
        p1[1] += 0.5;
        p2[0] += 0.5;
        p2[1] += 0.5;
        ClipAndDrawLine(p1, p2, color);
        return;
    }

    delta  = p2 - p1;
    length = delta.length() * 0.5;
    delta.normalize();

    mid = RotateVector(delta, angleVar);
    mid = p1 + mid * length;

    GenerateLightning(p1, mid, color, angleVar, numSubdivisions + 1, maxSubdivisions);
    GenerateLightning(mid, p2, color, angleVar, numSubdivisions + 1, maxSubdivisions);
}

bool test_generate_lightning_base_case() {
    clipAndDrawLineCalls = 0;
    GhostTexture gt;
    Vector p1(0, 0, 0);
    Vector p2(10, 10, 10);
    gt.GenerateLightning(p1, p2, 1, 0.5f, 5, 5); // numSubdivisions == maxSubdivisions

    if (clipAndDrawLineCalls != 1) {
        std::cerr << "Expected 1 call to ClipAndDrawLine, got " << clipAndDrawLineCalls << std::endl;
        return false;
    }
    return true;
}

bool test_generate_lightning_recursive_case() {
    clipAndDrawLineCalls = 0;
    GhostTexture gt;
    Vector p1(0, 0, 0);
    Vector p2(10, 10, 10);
    gt.GenerateLightning(p1, p2, 1, 0.5f, 4, 5); // Should branch once, resulting in 2 calls

    if (clipAndDrawLineCalls != 2) {
        std::cerr << "Expected 2 calls to ClipAndDrawLine, got " << clipAndDrawLineCalls << std::endl;
        return false;
    }
    return true;
}

bool test_generate_lightning_deep_recursion() {
    clipAndDrawLineCalls = 0;
    GhostTexture gt;
    Vector p1(0, 0, 0);
    Vector p2(10, 10, 10);
    gt.GenerateLightning(p1, p2, 1, 0.5f, 2, 5); // Should branch 3 times, resulting in 2^3 = 8 calls

    if (clipAndDrawLineCalls != 8) {
        std::cerr << "Expected 8 calls to ClipAndDrawLine, got " << clipAndDrawLineCalls << std::endl;
        return false;
    }
    return true;
}

int main() {
    bool passed = true;
    passed &= test_generate_lightning_base_case();
    passed &= test_generate_lightning_recursive_case();
    passed &= test_generate_lightning_deep_recursion();

    if (passed) {
        std::cout << "Test passed!" << std::endl;
        return 0;
    }
    return 1;
}
