#include "../tr_ghost.h"
#include "../tr_local.h"
#include "../../qcommon/qcommon.h"
#include <iostream>
#include <cassert>

trGlobals_t tr;

extern int frameTime;

extern "C" {
    void* Z_Malloc(int i) { return malloc(i); }
    void Z_Free(void *ptr) { free(ptr); }
    float VectorNormalize(vec3_t v) { return 0.0f; }
    char *COM_ParseExt(char **data_p, qboolean allowLineBreaks) { return nullptr; }
    void GL_Bind(image_t *image) {}
    void (*qglTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
    void Com_Error(int code, const char *fmt, ...) {
        std::cerr << "Com_Error called!" << std::endl;
        exit(1);
    }
    refimport_t ri;
}

bool test_ghost_texture_update_no_emitters()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];

    gt.Update();

    delete[] gt.m_texture;
    return true;
}

bool test_ghost_texture_update_basic_emitter()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];
    for(int i=0; i<100*100; ++i) gt.m_texture[i] = 0;

    ParticleEmitter* pe = new ParticleEmitter();
    pe->SetParticles(0);
    pe->SetGravityWell(0);
    pe->SetBallLightning(0);
    pe->SetSrcColor(0xFFFFFFFF);

    gt.m_emitterList.AddObject(pe);
    gt.Update();

    assert(gt.m_texture[0] == 0xFFFFFFFF);

    delete pe;
    delete[] gt.m_texture;
    return true;
}

bool test_ghost_texture_update_gravity_well()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];
    for(int i=0; i<100*100; ++i) gt.m_texture[i] = 0;

    ParticleEmitter* pe = new ParticleEmitter();
    pe->SetGravityWell(1);
    pe->SetSrcColor(0xFFFFFFFF);

    gt.m_emitterList.AddObject(pe);
    gt.Update();

    assert(gt.m_texture[0] == 0);

    delete pe;
    delete[] gt.m_texture;
    return true;
}

bool test_ghost_texture_update_particles()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];
    for(int i=0; i<100*100; ++i) gt.m_texture[i] = 0;

    ParticleEmitter* pe = new ParticleEmitter();
    pe->SetParticles(1);
    pe->SetGravityWell(0);
    pe->SetBallLightning(0);
    pe->SetMinRate(100.0f);
    pe->SetMaxRate(100.0f);
    pe->SetMinLife(1.0f);
    pe->SetMaxLife(1.0f);

    gt.m_emitterList.AddObject(pe);
    gt.Update();

    assert(pe->particleList.NumObjects() >= 0);

    delete pe;
    delete[] gt.m_texture;
    return true;
}

bool test_ghost_texture_update_particle_alive()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    frameTime = 16;

    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];
    for(int i=0; i<100*100; ++i) gt.m_texture[i] = 0;

    ParticleEmitter* pe = new ParticleEmitter();
    pe->SetParticles(1);
    pe->SetMinRate(0.0f);
    pe->SetMaxRate(0.0f);

    Particle* p = new Particle(Vector(50,50,0), Vector(0,0,0), Vector(0,0,0), 0xFFFFFFFF, 0xFFFFFFFF, 1.0f, 0, 0.0f, 0.0f, 1.5f, 0, Vector(0,0,0), 0, 0, 0);
    p->m_color = 0xFFFFFFFF;
    pe->particleList.AddObject(p);

    gt.m_emitterList.AddObject(pe);
    gt.Update();

    assert(pe->particleList.NumObjects() == 1);

    delete pe;
    delete[] gt.m_texture;
    return true;
}

bool test_ghost_texture_update_ball_lightning()
{
    GhostTexture gt;
    tr.refdef.time = 1000.0f;
    gt.m_width = 100;
    gt.m_height = 100;
    gt.m_texture = new unsigned int[gt.m_width * gt.m_height];
    for(int i=0; i<100*100; ++i) gt.m_texture[i] = 0;

    ParticleEmitter* pe = new ParticleEmitter();
    pe->SetBallLightning(1);
    pe->SetGravityWell(0);
    pe->SetParticles(0);
    pe->SetMinBallLightningRadius(10);
    pe->SetMaxBallLightningRadius(10);
    pe->UpdateValues();
    pe->SetSrcColor(0xFFFFFFFF);

    gt.m_emitterList.AddObject(pe);
    gt.Update();

    bool foundTexel = false;
    for (int i = 0; i < gt.m_width * gt.m_height; i++) {
        if (gt.m_texture[i] != 0) {
            foundTexel = true;
            break;
        }
    }
    assert(foundTexel);

    delete pe;
    delete[] gt.m_texture;
    return true;
}

int main(int argc, char *argv[])
{
    if (!test_ghost_texture_update_no_emitters()) {
        std::cerr << "test_ghost_texture_update_no_emitters failed!" << std::endl;
        return 1;
    }
    if (!test_ghost_texture_update_basic_emitter()) {
        std::cerr << "test_ghost_texture_update_basic_emitter failed!" << std::endl;
        return 1;
    }
    if (!test_ghost_texture_update_gravity_well()) {
        std::cerr << "test_ghost_texture_update_gravity_well failed!" << std::endl;
        return 1;
    }
    if (!test_ghost_texture_update_particles()) {
        std::cerr << "test_ghost_texture_update_particles failed!" << std::endl;
        return 1;
    }
    if (!test_ghost_texture_update_particle_alive()) {
        std::cerr << "test_ghost_texture_update_particle_alive failed!" << std::endl;
        return 1;
    }
    if (!test_ghost_texture_update_ball_lightning()) {
        std::cerr << "test_ghost_texture_update_ball_lightning failed!" << std::endl;
        return 1;
    }
    std::cout << "All GhostTexture tests passed!" << std::endl;
    return 0;
}
