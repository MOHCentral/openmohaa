/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

#include "../tr_ghost.h"
#include <iostream>
#include <cmath>

extern int frameTime;

extern "C" void Z_Free(void *ptr) { }
extern "C" void *Z_Malloc(int size) { return malloc(size); }
extern "C" void *Z_TagMalloc(int size, int tag) { return malloc(size); }
extern "C" void Com_Error(int code, const char *fmt, ...) {
    std::cerr << "Com_Error called" << std::endl;
    exit(1);
}

refdef_t tr_refdef;
trGlobals_t tr;
image_t *glState_defaultImage;

bool check_vector(const Vector& v, float x, float y, float z, const char* name, float epsilon = 0.001f) {
    if (std::abs(v.x - x) > epsilon ||
        std::abs(v.y - y) > epsilon ||
        std::abs(v.z - z) > epsilon) {
        std::cerr << "Expected " << name << " to be (" << x << ", " << y << ", " << z
                  << ") but was (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
        return false;
    }
    return true;
}

bool test_particle_update_basic() {
    Vector pos(0, 0, 0);
    Vector vel(10, 0, 0);
    Vector acc(0, 0, 0);

    Particle p(pos, vel, acc,
               0xFF0000, 0x00FF00, 1.0f,
               qfalse, 0.0f,
               0.0f, 10.0f,
               100.0f,
               Vector(0,0,0),
               qfalse, 0, 0
               );

    frameTime = 1000;

    p.Update(1.0f);

    if (!check_vector(p.m_position, 10.0f, 0.0f, 0.0f, "position")) return false;

    p.Update(2.0f);
    if (!check_vector(p.m_position, 20.0f, 0.0f, 0.0f, "position after 2nd update")) return false;

    return true;
}

bool test_particle_update_acceleration() {
    Vector pos(0, 0, 0);
    Vector vel(0, 0, 0);
    Vector acc(0, 5, 0);

    Particle p(pos, vel, acc,
               0xFF0000, 0x00FF00, 1.0f,
               qfalse, 0.0f,
               0.0f, 10.0f,
               100.0f,
               Vector(0,0,0),
               qfalse, 0, 0
               );

    frameTime = 1000;

    p.Update(1.0f);

    if (!check_vector(p.m_position, 0.0f, 2.5f, 0.0f, "position after acc")) return false;

    p.Update(2.0f);
    if (!check_vector(p.m_position, 0.0f, 10.0f, 0.0f, "position after 2nd acc update")) return false;

    return true;
}

bool test_particle_update_color() {
    Vector pos(0,0,0); Vector vel(0,0,0); Vector acc(0,0,0);

    // Original srcColor extraction:
    // m_srcR = (m_srcColor & 0xff); -> 0x00
    // m_srcG = (m_srcColor & 0xff00) >> 8; -> 0x00
    // m_srcB = (m_srcColor & 0xff0000) >> 16; -> 0xFF
    // So src is (0, 0, 255)

    // dstColor extraction:
    // m_dstR = (m_dstColor & 0xff); -> 0x00
    // m_dstG = (m_dstColor & 0xff00) >> 8; -> 0xFF
    // m_dstB = (m_dstColor & 0xff0000) >> 16; -> 0x00
    // So dst is (0, 255, 0)

    Particle p(pos, vel, acc,
               0xFF0000, 0x00FF00, 1.0f,
               qfalse, 0.0f,
               0.0f, 10.0f, // life is 10
               100.0f,
               Vector(0,0,0),
               qfalse, 0, 0
               );

    frameTime = 1000;

    // At currentTime = 5.0f,
    // factor = 1.0 - (m_dieTime - currentTime) / m_life
    // factor = 1.0 - (10.0 - 5.0) / 10.0 = 1.0 - 0.5 = 0.5
    // r = m_deltaR * factor = (0 - 0) * 0.5 = 0
    // g = m_deltaG * factor = (255 - 0) * 0.5 = 127
    // b = m_deltaB * factor = (0 - 255) * 0.5 = -127

    // Note: the original code doesn't clamp r, g, b to >= 0 or <= 255.
    // And wait, if src R is 0 and dst R is 0, deltaR is 0.
    // If src G is 0 and dst G is 255, deltaG is 255.
    // If src B is 255 and dst B is 0, deltaB is -255.

    // m_color = (r) | (g << 8) | (b << 8); // the code literally shifts b by 8. Let's see what happens.
    // Since b = -127 (int), shifting -127 by 8 bits gives -32512. Wait, the original code logic is flawed,
    // but we want to test its current behavior to match what's there.

    // r = 0
    // g = 127
    // b = -127
    // m_color = (0) | (127 << 8) | (-127 << 8) -> m_color = 0 | 32512 | -32512 = -32512

    p.Update(5.0f);

    int expected_r = 0;
    int expected_g = 127;
    int expected_b = -127;

    int expected_color = (expected_r) | (expected_g << 8) | (expected_b << 8);

    if (p.m_color != expected_color) {
        std::cerr << "Expected color to be " << expected_color << " but was " << p.m_color << std::endl;
        return false;
    }

    return true;
}

bool test_particle_update_wavy() {
    Vector pos(0, 0, 0);
    Vector vel(10, 0, 0);
    Vector acc(0, 0, 0);

    Particle p(pos, vel, acc,
               0xFF0000, 0x00FF00, 1.0f,
               qtrue, 5.0f,
               0.0f, 10.0f,
               100.0f,
               Vector(0,0,0),
               qfalse, 0, 0
               );

    frameTime = 1000;

    p.Update(1.0f);

    if (std::abs(p.m_position.x - 10.0f) > 0.001f) {
        std::cerr << "Expected wavy X position to be 10, was " << p.m_position.x << std::endl;
        return false;
    }

    if (std::abs(p.m_position.z - 0.0f) > 0.001f) {
        std::cerr << "Expected wavy Z position to be 0, was " << p.m_position.z << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char *argv[]) {
    int failed = 0;

    if (!test_particle_update_basic()) {
        std::cerr << "test_particle_update_basic FAILED" << std::endl;
        failed++;
    } else {
        std::cout << "test_particle_update_basic PASSED" << std::endl;
    }

    if (!test_particle_update_acceleration()) {
        std::cerr << "test_particle_update_acceleration FAILED" << std::endl;
        failed++;
    } else {
        std::cout << "test_particle_update_acceleration PASSED" << std::endl;
    }

    if (!test_particle_update_color()) {
        std::cerr << "test_particle_update_color FAILED" << std::endl;
        failed++;
    } else {
        std::cout << "test_particle_update_color PASSED" << std::endl;
    }

    if (!test_particle_update_wavy()) {
        std::cerr << "test_particle_update_wavy FAILED" << std::endl;
        failed++;
    } else {
        std::cout << "test_particle_update_wavy PASSED" << std::endl;
    }

    if (failed > 0) {
        std::cerr << failed << " tests FAILED!" << std::endl;
        return 1;
    }

    std::cout << "All tests PASSED!" << std::endl;
    return 0;
}
