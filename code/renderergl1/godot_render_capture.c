/* godot_render_capture.c — Engine render pipeline capture for GDExtension
 *
 * This file lives in code/renderergl1/ so it can #include tr_local.h and
 * access the real renderer data structures (model_t, sprite_t, backEnd,
 * tess, etc.).
 *
 * It provides:
 *   1. Godot_Backend_SetupView()  — populate backEnd.viewParms from camera
 *   2. Godot_ComputeSpriteQuad()  — call the engine's RB_DrawSprite and
 *                                    return the computed world-space vertices
 *   3. Godot_RealModel_RegisterSprite() — register a sprite in tr.models[]
 *   4. Godot_RealModel_GetSpriteDims()  — read sprite dimensions from the
 *                                          real model table
 *
 * This allows the Godot side to use the engine's exact geometry computation
 * instead of reimplementing it, ensuring 100% parity with the original
 * renderer.
 */

#ifdef GODOT_GDEXTENSION

#include "tr_local.h"
#include <string.h>

/* ===================================================================
 *  Backend view setup
 * ================================================================ */

void Godot_Backend_SetupView(const float vieworg[3],
                             const float viewaxis[3][3])
{
    VectorCopy(vieworg, backEnd.viewParms.ori.origin);
    VectorCopy(viewaxis[0], backEnd.viewParms.ori.axis[0]);
    VectorCopy(viewaxis[1], backEnd.viewParms.ori.axis[1]);
    VectorCopy(viewaxis[2], backEnd.viewParms.ori.axis[2]);
    backEnd.viewParms.isMirror = qfalse;
    backEnd.viewParms.fog.extrafrustums = 0;
}

/* ===================================================================
 *  Sprite model registration in real model table
 * ================================================================ */

int Godot_RealModel_RegisterSprite(const char *name)
{
    int             i;
    model_t        *mod;
    sprite_t       *spr;
    char            clean[MAX_QPATH];

    if (!name || !name[0]) return 0;

    Q_strncpyz(clean, name, sizeof(clean));

    /* Check if already registered */
    for (i = 1; i < tr.numModels; i++) {
        if (!Q_stricmp(tr.models[i].name, clean))
            return i;
    }

    if (tr.numModels >= MAX_MOD_KNOWN) {
        ri.Printf(PRINT_WARNING,
                   "[RenderCapture] Cannot register sprite '%s' — model table full\n",
                   clean);
        return 0;
    }

    spr = SPR_RegisterSprite(clean);
    if (!spr) {
        /* SPR_RegisterSprite logged the error */
        return 0;
    }

    mod = &tr.models[tr.numModels];
    Com_Memset(mod, 0, sizeof(*mod));
    Q_strncpyz(mod->name, clean, sizeof(mod->name));
    mod->type      = MOD_SPRITE;
    mod->index     = tr.numModels;
    mod->d.sprite  = spr;

    tr.numModels++;

    return mod->index;
}

/* ===================================================================
 *  Read sprite dimensions from the real model table
 * ================================================================ */

int Godot_RealModel_GetSpriteDims(int realHandle,
                                  float *out_width,
                                  float *out_height,
                                  float *out_sprite_scale)
{
    model_t *mod;

    if (realHandle <= 0 || realHandle >= tr.numModels) return 0;

    mod = &tr.models[realHandle];
    if (mod->type != MOD_SPRITE || !mod->d.sprite) return 0;

    if (out_width)        *out_width        = mod->d.sprite->width;
    if (out_height)       *out_height       = mod->d.sprite->height;
    if (out_sprite_scale) *out_sprite_scale = mod->d.sprite->scale;

    return 1;
}

/* ===================================================================
 *  Compute sprite quad using the engine's RB_DrawSprite
 * ================================================================ */

int Godot_ComputeSpriteQuad(int realModelHandle,
                            const float origin[3],
                            float entityScale,
                            const float entityAxis[3][3],
                            const unsigned char rgba[4],
                            float out_xyz[4][3],
                            float out_uv[4][2])
{
    refSprite_t spr;
    int         base;

    if (realModelHandle <= 0 || realModelHandle >= tr.numModels) return 0;
    if (tr.models[realModelHandle].type != MOD_SPRITE)           return 0;
    if (!tr.models[realModelHandle].d.sprite)                    return 0;

    /* Build a refSprite_t for RB_DrawSprite */
    Com_Memset(&spr, 0, sizeof(spr));
    spr.surftype = SF_SPRITE;
    spr.hModel   = realModelHandle;
    VectorCopy(origin, spr.origin);
    spr.scale    = entityScale;

    if (entityAxis) {
        VectorCopy(entityAxis[0], spr.axis[0]);
        VectorCopy(entityAxis[1], spr.axis[1]);
        VectorCopy(entityAxis[2], spr.axis[2]);
    } else {
        spr.axis[0][0] = 1.0f;
        spr.axis[1][1] = 1.0f;
        spr.axis[2][2] = 1.0f;
    }

    if (rgba) {
        spr.shaderRGBA[0] = rgba[0];
        spr.shaderRGBA[1] = rgba[1];
        spr.shaderRGBA[2] = rgba[2];
        spr.shaderRGBA[3] = rgba[3];
    } else {
        spr.shaderRGBA[0] = 255;
        spr.shaderRGBA[1] = 255;
        spr.shaderRGBA[2] = 255;
        spr.shaderRGBA[3] = 255;
    }

    /* Reset tess before calling RB_DrawSprite */
    base = tess.numVertexes;
    tess.numVertexes = 0;
    tess.numIndexes  = 0;

    RB_DrawSprite(&spr);

    if (tess.numVertexes >= 4) {
        int v;
        for (v = 0; v < 4; v++) {
            VectorCopy(tess.xyz[v], out_xyz[v]);
            if (out_uv) {
                out_uv[v][0] = tess.texCoords[v][0][0];
                out_uv[v][1] = tess.texCoords[v][0][1];
            }
        }
        tess.numVertexes = 0;
        tess.numIndexes  = 0;
        return 1;
    }

    tess.numVertexes = base;
    tess.numIndexes  = 0;
    return 0;
}

/* ===================================================================
 *  Sprite size verification — compare engine data with expected values
 * ================================================================ */

int Godot_VerifySpriteSize(int realHandle,
                           float entityScale,
                           float *out_engine_full_width,
                           float *out_engine_full_height)
{
    model_t *mod;
    float scale;

    if (realHandle <= 0 || realHandle >= tr.numModels) return 0;

    mod = &tr.models[realHandle];
    if (mod->type != MOD_SPRITE || !mod->d.sprite) return 0;

    /* This is exactly what RB_DrawSprite computes for the extent:
     *   scale = entity.scale * sprite.scale
     *   org_x = origin_x * scale  (= width/2 * scale)
     *   full extent = width * scale
     */
    scale = entityScale * mod->d.sprite->scale;
    if (out_engine_full_width)
        *out_engine_full_width  = mod->d.sprite->width * scale;
    if (out_engine_full_height)
        *out_engine_full_height = mod->d.sprite->height * scale;

    return 1;
}

#endif /* GODOT_GDEXTENSION */
