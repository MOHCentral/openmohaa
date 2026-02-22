/*
 * godot_vfx_accessors.c — C accessor to read RT_SPRITE entities from the
 * renderer entity buffer.
 *
 * The entity buffer (gr_entities[]) is static inside godot_renderer.c, so
 * we call through the existing Godot_Renderer_* accessor functions.
 * This file filters entities to RT_SPRITE only and presents a flat
 * sprite-specific interface for godot_vfx.cpp.
 *
 * Phase 221: VFX Manager Foundation
 */

/* ── Forward declarations of renderer accessors (defined in godot_renderer.c) ── */
int  Godot_Renderer_GetEntityCount(void);
int  Godot_Renderer_GetEntity(int index,
                              float *origin, float *axis, float *scale,
                              int *hModel, int *entityNumber,
                              unsigned char *rgba, int *renderfx);
void Godot_Renderer_GetEntitySprite(int index,
                                    float *radius, float *rotation,
                                    int *customShader);
int  Godot_Model_GetShaderHandle(int hModel);

/* RT_SPRITE enum value from renderercommon/tr_types.h */
#define RT_SPRITE_VALUE 2

/* ── Sprite index cache — rebuilt each time GetSpriteCount is called ── */
#define VFX_MAX_SPRITES 512
static int  vfx_sprite_indices[VFX_MAX_SPRITES];
static int  vfx_sprite_count = 0;

/*
 * Godot_VFX_GetSpriteCount — scan the entity buffer, cache indices of
 * RT_SPRITE entities, and return the count.  Must be called once per
 * frame before any Godot_VFX_GetSprite() calls.
 */
int Godot_VFX_GetSpriteCount(void)
{
    int total = Godot_Renderer_GetEntityCount();
    vfx_sprite_count = 0;

    for (int i = 0; i < total && vfx_sprite_count < VFX_MAX_SPRITES; i++) {
        float origin[3], axis[9], scale;
        int hModel, entityNumber, renderfx;
        unsigned char rgba[4];

        int reType = Godot_Renderer_GetEntity(i, origin, axis, &scale,
                                              &hModel, &entityNumber,
                                              rgba, &renderfx);
        if (reType == RT_SPRITE_VALUE) {
            vfx_sprite_indices[vfx_sprite_count++] = i;
        }
    }

    return vfx_sprite_count;
}

/*
 * Godot_VFX_GetSprite — retrieve data for the Nth sprite (0-based index
 * into the filtered sprite list, NOT the raw entity index).
 *
 *   origin       — [3] floats, id Tech 3 coordinates (inches)
 *   radius       — sprite half-size (inches)
 *   shaderHandle — resolved shader handle (customShader if set, else
 *                  resolved from model→shader chain via the model table)
 *   rotation     — rotation angle in degrees
 *   rgba         — [4] bytes, colour tint
 */
void Godot_VFX_GetSprite(int idx,
                         float *origin,
                         float *radius,
                         int   *shaderHandle,
                         float *rotation,
                         unsigned char *rgba,
                         float *scale)
{
    if (idx < 0 || idx >= vfx_sprite_count) return;

    int entIdx = vfx_sprite_indices[idx];

    /* Fetch common entity data (origin, rgba) */
    float axis[9], scaleVal;
    int hModel, entityNumber, renderfx;
    unsigned char tmpRgba[4];

    Godot_Renderer_GetEntity(entIdx, origin, axis, &scaleVal,
                             &hModel, &entityNumber,
                             tmpRgba, &renderfx);

    if (rgba) {
        rgba[0] = tmpRgba[0];
        rgba[1] = tmpRgba[1];
        rgba[2] = tmpRgba[2];
        rgba[3] = tmpRgba[3];
    }

    /* Fetch sprite-specific data */
    float tmpRadius = 0.0f, tmpRotation = 0.0f;
    int customShader = 0;
    Godot_Renderer_GetEntitySprite(entIdx, &tmpRadius, &tmpRotation,
                                   &customShader);

    if (radius)       *radius       = tmpRadius;
    if (rotation)     *rotation     = tmpRotation;
    if (scale)        *scale        = scaleVal;

    /* Resolve the shader handle:
     *  1. customShader (explicit per-entity override) — highest priority
     *  2. Model's associated shader (hModel → gr_models[].shaderHandle)
     *     For GR_MOD_SPRITE models, shaderHandle is set at registration
     *     time from the .spr filename stripped of extension.
     *  3. Fallback to raw hModel (last resort) */
    if (shaderHandle) {
        if (customShader > 0) {
            *shaderHandle = customShader;
        } else if (hModel > 0) {
            int modelShader = Godot_Model_GetShaderHandle(hModel);
            *shaderHandle = (modelShader > 0) ? modelShader : hModel;
        } else {
            *shaderHandle = 0;
        }
    }
}
