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
#include <cstdlib>

// DO NOT DEFINE frameTime here. It's defined in tr_ghost.cpp.
extern int frameTime;

extern "C" void Z_Free(void *ptr) { }
extern "C" void *Z_Malloc(int size) { return malloc(size); }
extern "C" void *Z_TagMalloc(int size, int tag) { return malloc(size); }
extern "C" void Com_Error(int code, const char *fmt, ...) {
    std::cerr << "Com_Error called" << std::endl;
    exit(1);
}

// Additional stubs for linking
extern "C" void GL_Bind(image_t *image) { }
// qglTexImage2D is defined as a function pointer by QGL macros in tr_local.h
TexImage2Dproc *qglTexImage2D;
extern "C" void Com_Printf(const char *fmt, ...) { }

refdef_t tr_refdef;
trGlobals_t tr;
image_t *glState_defaultImage;
refimport_t ri; // Mock refimport_t for linking (used as ri in renderer)

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

    Particle p2(pos, vel, acc,
               0x000000, 0x040404, 1.0f,
               qfalse, 0.0f,
               0.0f, 10.0f, // life is 10
               100.0f,
               Vector(0,0,0),
               qfalse, 0, 0
               );

    p2.Update(5.0f); // half life

    // dst R=4, G=4, B=4
    // delta R=4, G=4, B=4
    // at half life: r=2, g=2, b=2
    // color = 2 | (2<<8) | (2<<16) = 2 | 512 | 131072 = 131586

    int expected_r = 2;
    int expected_g = 2;
    int expected_b = 2;

    int expected_color = (expected_r) | (expected_g << 8) | (expected_b << 16);

    if (p2.m_color != expected_color) {
        std::cerr << "Expected color to be " << expected_color << " but was " << p2.m_color << std::endl;
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
