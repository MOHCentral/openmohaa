/*
 * godot_vfx.h — VFX Manager public API declarations.
 *
 * Provides C++ functions for initialising, updating, and shutting down
 * the sprite-based VFX pipeline, plus C-linkage accessor functions used
 * by the VFX manager to read sprite data from the renderer entity buffer.
 *
 * Phase 221: VFX Manager Foundation
 */

#ifndef GODOT_VFX_H
#define GODOT_VFX_H

#ifdef __cplusplus
#include <godot_cpp/classes/node3d.hpp>

/* C++ management API — called from MoHAARunner or integration code */
void Godot_VFX_Init(godot::Node3D *parent);
void Godot_VFX_Update(float delta);
void Godot_VFX_Shutdown(void);
void Godot_VFX_Clear(void);   /* hide all sprites on map change */
#endif

/* C-linkage accessor API — implemented in godot_vfx_accessors.c */
#ifdef __cplusplus
extern "C" {
#endif

int  Godot_VFX_GetSpriteCount(void);
void Godot_VFX_GetSprite(int idx, float *origin, float *radius,
                         int *shaderHandle, float *rotation,
                         unsigned char *rgba, float *scale);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_VFX_H */
