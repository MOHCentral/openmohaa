/* godot_shader_accessors.c — Bridge from real shader_t to GodotShaderProps
 *
 * This file lives in code/renderergl1/ so it can #include tr_local.h and
 * access the real renderer data structures (shader_t, trGlobals_t tr, etc.).
 *
 * It implements the same API as godot_shader_props.cpp (Godot_ShaderProps_*)
 * but reads from the REAL shader data parsed by R_StartupShaders() and
 * created by R_FindShader(), instead of re-parsing .shader files.
 *
 * This replaces the 1700-line custom shader parser with ~400 lines of
 * straightforward struct-to-struct conversion.
 */

#include "tr_local.h"
#include "../godot/godot_shader_props.h"

#include <string.h>
#include <ctype.h>

/* ===================================================================
 *  Forward declarations for real renderer functions
 * ================================================================ */
extern shader_t *R_FindShader(const char *name, int lightmapIndex,
                               qboolean mipRawImage, qboolean picmip,
                               qboolean wrapx, qboolean wrapy);
extern shader_t *R_FindShaderByName(const char *name);

/* ===================================================================
 *  Conversion helpers: real renderer enums → GodotShaderProps enums
 * ================================================================ */

static enum GodotShaderCull convert_cull(cullType_t ct) {
    switch (ct) {
        case CT_FRONT_SIDED: return SHADER_CULL_BACK;
        case CT_BACK_SIDED:  return SHADER_CULL_FRONT;
        case CT_TWO_SIDED:   return SHADER_CULL_NONE;
        default:             return SHADER_CULL_BACK;
    }
}

static enum MohaaWaveFunc convert_genfunc(genFunc_t gf) {
    switch (gf) {
        case GF_SIN:                return WAVE_SIN;
        case GF_TRIANGLE:           return WAVE_TRIANGLE;
        case GF_SQUARE:             return WAVE_SQUARE;
        case GF_SAWTOOTH:           return WAVE_SAWTOOTH;
        case GF_INVERSE_SAWTOOTH:   return WAVE_INVERSE_SAWTOOTH;
        default:                    return WAVE_SIN;
    }
}

static void convert_waveform(const waveForm_t *src, struct MohaaWaveParams *dst) {
    dst->func      = convert_genfunc(src->func);
    dst->base      = src->base;
    dst->amplitude = src->amplitude;
    dst->phase     = src->phase;
    dst->frequency = src->frequency;
}

/* Convert GLS_SRCBLEND_* bitmask value to MohaaBlendFactor */
static enum MohaaBlendFactor convert_src_blend(unsigned bits) {
    switch (bits & GLS_SRCBLEND_BITS) {
        case GLS_SRCBLEND_ZERO:                  return BLEND_ZERO;
        case GLS_SRCBLEND_ONE:                   return BLEND_ONE;
        case GLS_SRCBLEND_DST_COLOR:             return BLEND_DST_COLOR;
        case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:   return BLEND_ONE_MINUS_DST_COLOR;
        case GLS_SRCBLEND_SRC_ALPHA:             return BLEND_SRC_ALPHA;
        case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:   return BLEND_ONE_MINUS_SRC_ALPHA;
        case GLS_SRCBLEND_DST_ALPHA:             return BLEND_DST_ALPHA;
        case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:   return BLEND_ONE_MINUS_DST_ALPHA;
        case GLS_SRCBLEND_ALPHA_SATURATE:        return BLEND_ONE; /* closest approximation */
        default:                                 return BLEND_ONE;
    }
}

/* Convert GLS_DSTBLEND_* bitmask value to MohaaBlendFactor */
static enum MohaaBlendFactor convert_dst_blend(unsigned bits) {
    switch (bits & GLS_DSTBLEND_BITS) {
        case GLS_DSTBLEND_ZERO:                  return BLEND_ZERO;
        case GLS_DSTBLEND_ONE:                   return BLEND_ONE;
        case GLS_DSTBLEND_SRC_COLOR:             return BLEND_SRC_COLOR;
        case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:   return BLEND_ONE_MINUS_SRC_COLOR;
        case GLS_DSTBLEND_SRC_ALPHA:             return BLEND_SRC_ALPHA;
        case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:   return BLEND_ONE_MINUS_SRC_ALPHA;
        case GLS_DSTBLEND_DST_ALPHA:             return BLEND_DST_ALPHA;
        case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:   return BLEND_ONE_MINUS_DST_ALPHA;
        default:                                 return BLEND_ZERO;
    }
}

/* Classify transparency from blend factors (same logic as old godot_shader_props.cpp) */
static enum GodotShaderTransparency classify_blend_factors(
    enum MohaaBlendFactor src, enum MohaaBlendFactor dst)
{
    if (src == BLEND_SRC_ALPHA && dst == BLEND_ONE_MINUS_SRC_ALPHA)
        return SHADER_ALPHA_BLEND;
    if (src == BLEND_ONE_MINUS_SRC_ALPHA && dst == BLEND_SRC_ALPHA)
        return SHADER_ALPHA_BLEND_INV;
    if (src == BLEND_ONE && dst == BLEND_ONE)
        return SHADER_ADDITIVE;
    if (src == BLEND_SRC_ALPHA && dst == BLEND_ONE)
        return SHADER_ADDITIVE;
    if (src == BLEND_DST_COLOR && dst == BLEND_ZERO)
        return SHADER_MULTIPLICATIVE;
    if (src == BLEND_ZERO && dst == BLEND_SRC_COLOR)
        return SHADER_MULTIPLICATIVE;
    if (src == BLEND_ZERO && dst == BLEND_ONE_MINUS_SRC_COLOR)
        return SHADER_MULTIPLICATIVE_INV;
    if (src == BLEND_ONE && dst == BLEND_ZERO)
        return SHADER_OPAQUE;
    if (src == BLEND_ZERO && dst == BLEND_ONE)
        return SHADER_OPAQUE;
    if (src == BLEND_ONE && dst == BLEND_ONE_MINUS_SRC_ALPHA)
        return SHADER_ALPHA_BLEND;
    if (src == BLEND_SRC_ALPHA)
        return SHADER_ALPHA_BLEND;
    if (src == BLEND_DST_COLOR || dst == BLEND_SRC_COLOR)
        return SHADER_MULTIPLICATIVE;
    if (src == BLEND_ONE)
        return SHADER_ADDITIVE;
    ri.Printf(PRINT_DEVELOPER, "[ShaderAccessors] classify_blend_factors FALLBACK: src=%d dst=%d -> ALPHA_BLEND\n", (int)src, (int)dst);
    return SHADER_ALPHA_BLEND;
}

/* Convert rgbGen (colorGen_t) to the integer codes used in GodotShaderProps */
static int convert_rgbgen_type(colorGen_t cg) {
    switch (cg) {
        case CGEN_IDENTITY:
        case CGEN_IDENTITY_LIGHTING:
            return 0; /* identity */
        case CGEN_VERTEX:
        case CGEN_EXACT_VERTEX:
        case CGEN_ONE_MINUS_VERTEX:
        case CGEN_LIGHTING_GRID:
        case CGEN_LIGHTING_SPHERICAL:
            return 1; /* vertex/lighting */
        case CGEN_WAVEFORM:
        case CGEN_MULTIPLY_BY_WAVEFORM:
            return 2; /* wave */
        case CGEN_ENTITY:
        case CGEN_ONE_MINUS_ENTITY:
            return 3; /* entity */
        case CGEN_CONSTANT:
            return 4; /* const */
        default:
            return 0;
    }
}

static enum MohaaStageRgbGen convert_stage_rgbgen(colorGen_t cg) {
    switch (cg) {
        case CGEN_IDENTITY:           return STAGE_RGBGEN_IDENTITY;
        case CGEN_IDENTITY_LIGHTING:  return STAGE_RGBGEN_IDENTITY_LIGHTING;
        case CGEN_VERTEX:
        case CGEN_EXACT_VERTEX:
        case CGEN_ONE_MINUS_VERTEX:   return STAGE_RGBGEN_VERTEX;
        case CGEN_WAVEFORM:
        case CGEN_MULTIPLY_BY_WAVEFORM: return STAGE_RGBGEN_WAVE;
        case CGEN_ENTITY:             return STAGE_RGBGEN_ENTITY;
        case CGEN_ONE_MINUS_ENTITY:   return STAGE_RGBGEN_ONE_MINUS_ENTITY;
        case CGEN_LIGHTING_GRID:
        case CGEN_LIGHTING_SPHERICAL: return STAGE_RGBGEN_LIGHTING_DIFFUSE;
        case CGEN_CONSTANT:           return STAGE_RGBGEN_CONST;
        default:                      return STAGE_RGBGEN_IDENTITY;
    }
}

/* Convert alphaGen (alphaGen_t) to the integer codes used in GodotShaderProps */
static int convert_alphagen_type(alphaGen_t ag) {
    switch (ag) {
        case AGEN_IDENTITY:
        case AGEN_SKIP:
            return 0;
        case AGEN_VERTEX:
        case AGEN_ONE_MINUS_VERTEX:
            return 1;
        case AGEN_WAVEFORM:
            return 2;
        case AGEN_ENTITY:
        case AGEN_ONE_MINUS_ENTITY:
            return 3;
        case AGEN_CONSTANT:
            return 4;
        default:
            return 0;
    }
}

static enum MohaaStageAlphaGen convert_stage_alphagen(alphaGen_t ag) {
    switch (ag) {
        case AGEN_IDENTITY:
        case AGEN_SKIP:          return STAGE_ALPHAGEN_IDENTITY;
        case AGEN_VERTEX:
        case AGEN_ONE_MINUS_VERTEX: return STAGE_ALPHAGEN_VERTEX;
        case AGEN_WAVEFORM:      return STAGE_ALPHAGEN_WAVE;
        case AGEN_ENTITY:
        case AGEN_ONE_MINUS_ENTITY: return STAGE_ALPHAGEN_ENTITY;
        case AGEN_PORTAL:        return STAGE_ALPHAGEN_PORTAL;
        case AGEN_CONSTANT:      return STAGE_ALPHAGEN_CONST;
        default:                 return STAGE_ALPHAGEN_IDENTITY;
    }
}

static enum MohaaStageTcGen convert_tcgen(texCoordGen_t tcg) {
    switch (tcg) {
        case TCGEN_TEXTURE:
        case TCGEN_IDENTITY:
        case TCGEN_BAD:
            return STAGE_TCGEN_BASE;
        case TCGEN_LIGHTMAP:
            return STAGE_TCGEN_LIGHTMAP;
        case TCGEN_ENVIRONMENT_MAPPED:
        case TCGEN_ENVIRONMENT_MAPPED2:
        case TCGEN_SUN_REFLECTION:
            return STAGE_TCGEN_ENVIRONMENT;
        case TCGEN_VECTOR:
            return STAGE_TCGEN_VECTOR;
        case TCGEN_FOG:
            return STAGE_TCGEN_BASE;
        default:
            return STAGE_TCGEN_BASE;
    }
}

static enum MohaaStageTcModType convert_tcmod_type(texMod_t tm) {
    switch (tm) {
        case TMOD_SCROLL:       return TCMOD_SCROLL;
        case TMOD_ROTATE:       return TCMOD_ROTATE;
        case TMOD_SCALE:        return TCMOD_SCALE;
        case TMOD_TURBULENT:    return TCMOD_TURB;
        case TMOD_STRETCH:      return TCMOD_STRETCH;
        case TMOD_OFFSET:       return TCMOD_OFFSET;
        case TMOD_PARALLAX:     return TCMOD_PARALLAX;
        case TMOD_MACRO:        return TCMOD_MACRO;
        case TMOD_WAVETRANS:    return TCMOD_WAVETRANS;
        case TMOD_WAVETRANT:    return TCMOD_WAVETRANT;
        case TMOD_BULGETRANS:   return TCMOD_BULGE;
        case TMOD_TRANSFORM:    return TCMOD_TRANSFORM;
        case TMOD_ENTITY_TRANSLATE: return TCMOD_ENTITY_TRANSLATE;
        default:                return TCMOD_NONE;
    }
}

/* ===================================================================
 *  Convert a single shaderStage_t to MohaaShaderStage
 * ================================================================ */
static void convert_stage(const shaderStage_t *src, struct MohaaShaderStage *dst, int bundle_idx) {
    memset(dst, 0, sizeof(*dst));

    if (!src || !src->active) {
        dst->active = 0;
        return;
    }
    dst->active = 1;

    /* Texture path from bundle 0 image */
    textureBundle_t *b0 = (textureBundle_t *)&src->bundle[0];
    if (b0->image[0] && b0->image[0]->imgName[0]) {
        Q_strncpyz(dst->map, b0->image[0]->imgName, sizeof(dst->map));
    }

    /* Lightmap detection */
    if (b0->isLightmap) {
        dst->isLightmap = 1;
        dst->tcGen = STAGE_TCGEN_LIGHTMAP;
    }

    /* Check bundle 1 for $lightmap (multi-texture lightmap) */
    if (src->bundle[1].isLightmap || (src->bundle[1].image[0] &&
        !Q_stricmp(src->bundle[1].image[0]->imgName, "*lightmap"))) {
        dst->hasNextBundleLightmap = 1;
    }

    /* Blend factors from stateBits */
    unsigned sb = src->stateBits;
    int hasSrcBits = (sb & GLS_SRCBLEND_BITS) != 0;
    int hasDstBits = (sb & GLS_DSTBLEND_BITS) != 0;

    dst->blendSrc = convert_src_blend(sb);
    dst->blendDst = convert_dst_blend(sb);
    dst->hasBlendFunc = (hasSrcBits || hasDstBits) ? 1 : 0;

    /* Alpha test from stateBits */
    unsigned atest = sb & GLS_ATEST_BITS;
    if (atest) {
        dst->hasAlphaFunc = 1;
        if (atest == GLS_ATEST_GT_0)
            dst->alphaFuncThreshold = 0.01f;
        else
            dst->alphaFuncThreshold = 0.5f;
    }

    /* Depth write */
    if (sb & GLS_DEPTHMASK_TRUE) {
        dst->depthWriteExplicit = 1;
        dst->depthWriteEnabled  = 1;
    }
    if (sb & GLS_DEPTHTEST_DISABLE) {
        dst->noDepthTest = 1;
    }

    /* ClampMap — detected by image wrap mode */
    if (b0->image[0]) {
        if (b0->image[0]->wrapClampModeX == GL_CLAMP_TO_EDGE ||
            b0->image[0]->wrapClampModeX == GL_CLAMP) {
            dst->isClampMap = 1;
        }
    }

    /* rgbGen */
    dst->rgbGen = convert_stage_rgbgen(src->rgbGen);
    if (src->rgbGen == CGEN_WAVEFORM || src->rgbGen == CGEN_MULTIPLY_BY_WAVEFORM) {
        convert_waveform(&src->rgbWave, &dst->rgbWave);
    }
    if (src->rgbGen == CGEN_CONSTANT) {
        dst->rgbConst[0] = src->colorConst[0] / 255.0f;
        dst->rgbConst[1] = src->colorConst[1] / 255.0f;
        dst->rgbConst[2] = src->colorConst[2] / 255.0f;
    } else {
        dst->rgbConst[0] = dst->rgbConst[1] = dst->rgbConst[2] = 1.0f;
    }

    /* alphaGen */
    dst->alphaGen = convert_stage_alphagen(src->alphaGen);
    if (src->alphaGen == AGEN_WAVEFORM) {
        convert_waveform(&src->alphaWave, &dst->alphaWave);
    }
    if (src->alphaGen == AGEN_CONSTANT) {
        dst->alphaConst = src->alphaConst / 255.0f;
    } else {
        dst->alphaConst = 1.0f;
    }
    if (src->alphaGen == AGEN_PORTAL) {
        /* Portal distance not directly accessible from shaderStage_t;
         * it's stored in shader.portalRange — we'll set it at the
         * shader level.  Default to 0. */
        dst->alphaPortalDist = 0.0f;
    }

    /* tcGen */
    if (!dst->isLightmap) {
        dst->tcGen = convert_tcgen(b0->tcGen);
    }
    if (b0->tcGen == TCGEN_VECTOR) {
        dst->tcGenVecS[0] = b0->tcGenVectors[0][0];
        dst->tcGenVecS[1] = b0->tcGenVectors[0][1];
        dst->tcGenVecS[2] = b0->tcGenVectors[0][2];
        dst->tcGenVecT[0] = b0->tcGenVectors[1][0];
        dst->tcGenVecT[1] = b0->tcGenVectors[1][1];
        dst->tcGenVecT[2] = b0->tcGenVectors[1][2];
    }

    /* tcMods */
    dst->tcModCount = 0;
    if (b0->texMods && b0->numTexMods > 0) {
        int count = b0->numTexMods;
        if (count > MOHAA_SHADER_STAGE_MAX_TCMODS)
            count = MOHAA_SHADER_STAGE_MAX_TCMODS;

        for (int i = 0; i < count; i++) {
            const texModInfo_t *tm = &b0->texMods[i];
            struct MohaaStageTcMod *dtm = &dst->tcMods[dst->tcModCount];
            memset(dtm, 0, sizeof(*dtm));

            dtm->type = convert_tcmod_type(tm->type);
            if (dtm->type == TCMOD_NONE)
                continue;

            switch (tm->type) {
                case TMOD_SCROLL:
                    dtm->params[0] = tm->scroll[0];
                    dtm->params[1] = tm->scroll[1];
                    break;
                case TMOD_ROTATE:
                    dtm->params[0] = tm->rotateSpeed;
                    dtm->params[1] = tm->rotateStart;
                    dtm->params[2] = tm->rotateCoef;
                    break;
                case TMOD_SCALE:
                    dtm->params[0] = tm->scale[0];
                    dtm->params[1] = tm->scale[1];
                    break;
                case TMOD_TURBULENT:
                    dtm->params[0] = tm->wave.base;
                    dtm->params[1] = tm->wave.amplitude;
                    dtm->params[2] = tm->wave.phase;
                    dtm->params[3] = tm->wave.frequency;
                    break;
                case TMOD_STRETCH:
                    convert_waveform(&tm->wave, &dtm->wave);
                    break;
                case TMOD_OFFSET:
                    dtm->params[0] = tm->translate[0];
                    dtm->params[1] = tm->translate[1];
                    break;
                case TMOD_PARALLAX:
                    dtm->params[0] = tm->rate[0];
                    dtm->params[1] = tm->rate[1];
                    break;
                case TMOD_MACRO:
                    dtm->params[0] = tm->scale[0];
                    dtm->params[1] = tm->scale[1];
                    break;
                case TMOD_WAVETRANS:
                case TMOD_WAVETRANT:
                case TMOD_BULGETRANS:
                    convert_waveform(&tm->wave, &dtm->wave);
                    break;
                case TMOD_TRANSFORM:
                    dtm->params[0] = tm->matrix[0][0];
                    dtm->params[1] = tm->matrix[0][1];
                    dtm->params[2] = tm->matrix[1][0];
                    dtm->params[3] = tm->matrix[1][1];
                    dtm->params[4] = tm->translate[0];
                    dtm->params[5] = tm->translate[1];
                    break;
                case TMOD_ENTITY_TRANSLATE:
                    break;
                default:
                    break;
            }
            dst->tcModCount++;
        }
    }

    /* animMap */
    if (b0->numImageAnimations > 1) {
        dst->animMapFreq = b0->imageAnimationSpeed;
        dst->animMapFrameCount = b0->numImageAnimations;
        if (dst->animMapFrameCount > MOHAA_SHADER_STAGE_MAX_ANIM_FRAMES)
            dst->animMapFrameCount = MOHAA_SHADER_STAGE_MAX_ANIM_FRAMES;
        for (int i = 0; i < dst->animMapFrameCount; i++) {
            if (b0->image[i])
                Q_strncpyz(dst->animMapFrames[i], b0->image[i]->imgName, 64);
        }
    }
}

/* ===================================================================
 *  Convert a full shader_t to GodotShaderProps
 * ================================================================ */
static void convert_shader(const shader_t *sh, GodotShaderProps *out) {
    memset(out, 0, sizeof(*out));

    /* Defaults */
    out->transparency   = SHADER_OPAQUE;
    out->alpha_threshold = 0.5f;
    out->cull           = convert_cull(sh->cullType);
    out->is_sky         = sh->isSky ? 1 : 0;
    out->is_portal      = sh->isPortalSky ? 1 : 0;
    out->tcmod_scale_s  = 1.0f;
    out->tcmod_scale_t  = 1.0f;
    out->sprite_scale   = sh->sprite.scale > 0.0f ? sh->sprite.scale : 1.0f;
    out->alphagen_const = 1.0f;
    out->rgbgen_const[0] = out->rgbgen_const[1] = out->rgbgen_const[2] = 1.0f;

    /* Sky environment path */
    if (sh->isSky && sh->sky.outerbox[0]) {
        /* The sky box image names are e.g. "env/m5l2_ft", we want "env/m5l2".
         * Strip the suffix (_ft/_bk/_rt/_lf/_up/_dn) and extension. */
        const char *skyname = sh->sky.outerbox[0]->imgName;
        Q_strncpyz(out->sky_env, skyname, sizeof(out->sky_env));
        /* Remove the _XX suffix (last 3 chars if they match _ft etc.) */
        int len = (int)strlen(out->sky_env);
        if (len >= 3 && out->sky_env[len - 3] == '_') {
            out->sky_env[len - 3] = '\0';
        }
        /* Also strip any file extension */
        char *dot = strrchr(out->sky_env, '.');
        if (dot) *dot = '\0';
    }

    /* Surface parms */
    if (sh->surfaceFlags & SURF_NODRAW)
        out->is_portal = 1;
    if (sh->contentFlags & CONTENTS_TRANSLUCENT)
        out->has_surfaceparm_trans = 1;

    /* Fog */
    if (sh->fogParms.depthForOpaque > 0.0f) {
        out->has_fog = 1;
        out->fog_color[0] = sh->fogParms.color[0];
        out->fog_color[1] = sh->fogParms.color[1];
        out->fog_color[2] = sh->fogParms.color[2];
        out->fog_distance  = sh->fogParms.depthForOpaque;
    }

    /* Sort key */
    out->sort_key = (int)sh->sort;

    /* No mipmaps / no picmip */
    out->no_mipmaps = sh->noMipMaps ? 1 : 0;
    out->no_picmip  = sh->noPicMip ? 1 : 0;

    /* Deforms */
    if (sh->numDeforms > 0) {
        const deformStage_t *d = &sh->deforms[0];
        out->has_deform = 1;
        switch (d->deformation) {
            case DEFORM_WAVE:
                out->deform_type = 0;
                out->deform_div = d->deformationSpread;
                out->deform_base      = d->deformationWave.base;
                out->deform_amplitude = d->deformationWave.amplitude;
                out->deform_frequency = d->deformationWave.frequency;
                out->deform_phase     = d->deformationWave.phase;
                break;
            case DEFORM_BULGE:
                out->deform_type = 1;
                out->deform_base      = d->bulgeWidth;
                out->deform_amplitude = d->bulgeHeight;
                out->deform_frequency = d->bulgeSpeed;
                break;
            case DEFORM_MOVE:
                out->deform_type = 2;
                out->deform_base      = d->deformationWave.base;
                out->deform_amplitude = d->deformationWave.amplitude;
                out->deform_frequency = d->deformationWave.frequency;
                out->deform_phase     = d->deformationWave.phase;
                break;
            case DEFORM_AUTOSPRITE:
                out->deform_type = 3;
                break;
            case DEFORM_AUTOSPRITE2:
                out->deform_type = 4;
                break;
            default:
                out->has_deform = 0;
                break;
        }
    }

    /* ── Convert stages ── */
    out->stage_count = 0;
    out->num_stages = 0;
    for (int i = 0; i < MAX_SHADER_STAGES && i < MOHAA_SHADER_STAGE_MAX; i++) {
        if (!sh->unfoggedStages[i] || !sh->unfoggedStages[i]->active)
            break;
        convert_stage(sh->unfoggedStages[i], &out->stages[i], 0);
        out->stage_count = i + 1;
        out->num_stages = out->stage_count;
    }

    /* ── First non-lightmap stage convenience fields ── */
    /* Copy first-stage data into the top-level convenience fields
     * that MoHAARunner references (rgbgen_*, alphagen_*, tcmod_*, etc.) */
    const shaderStage_t *firstStage = NULL;
    int firstIdx = -1;
    for (int i = 0; i < sh->numUnfoggedPasses; i++) {
        if (!sh->unfoggedStages[i] || !sh->unfoggedStages[i]->active)
            continue;
        if (sh->unfoggedStages[i]->bundle[0].isLightmap)
            continue;
        firstStage = sh->unfoggedStages[i];
        firstIdx = i;
        break;
    }

    if (firstStage) {
        /* rgbGen top-level */
        out->rgbgen_type = convert_rgbgen_type(firstStage->rgbGen);
        if (firstStage->rgbGen == CGEN_WAVEFORM || firstStage->rgbGen == CGEN_MULTIPLY_BY_WAVEFORM) {
            out->rgbgen_wave_func  = convert_genfunc(firstStage->rgbWave.func);
            out->rgbgen_wave_base  = firstStage->rgbWave.base;
            out->rgbgen_wave_amp   = firstStage->rgbWave.amplitude;
            out->rgbgen_wave_phase = firstStage->rgbWave.phase;
            out->rgbgen_wave_freq  = firstStage->rgbWave.frequency;
        }
        if (firstStage->rgbGen == CGEN_CONSTANT) {
            out->rgbgen_const[0] = firstStage->colorConst[0] / 255.0f;
            out->rgbgen_const[1] = firstStage->colorConst[1] / 255.0f;
            out->rgbgen_const[2] = firstStage->colorConst[2] / 255.0f;
        }

        /* alphaGen top-level */
        out->alphagen_type = convert_alphagen_type(firstStage->alphaGen);
        if (firstStage->alphaGen == AGEN_WAVEFORM) {
            out->alphagen_wave_func  = convert_genfunc(firstStage->alphaWave.func);
            out->alphagen_wave_base  = firstStage->alphaWave.base;
            out->alphagen_wave_amp   = firstStage->alphaWave.amplitude;
            out->alphagen_wave_phase = firstStage->alphaWave.phase;
            out->alphagen_wave_freq  = firstStage->alphaWave.frequency;
        }
        if (firstStage->alphaGen == AGEN_CONSTANT) {
            out->alphagen_const = firstStage->alphaConst / 255.0f;
        }

        /* tcGen environment */
        if (firstStage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED ||
            firstStage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED2) {
            out->tcgen_environment = 1;
        }

        /* tcMod top-level convenience (first stage, first mods) */
        if (firstStage->bundle[0].texMods && firstStage->bundle[0].numTexMods > 0) {
            out->has_tcmod = 1;
            for (int i = 0; i < firstStage->bundle[0].numTexMods; i++) {
                const texModInfo_t *tm = &firstStage->bundle[0].texMods[i];
                switch (tm->type) {
                    case TMOD_SCROLL:
                        out->tcmod_scroll_s = tm->scroll[0];
                        out->tcmod_scroll_t = tm->scroll[1];
                        break;
                    case TMOD_ROTATE:
                        out->tcmod_rotate = tm->rotateSpeed;
                        break;
                    case TMOD_SCALE:
                        out->tcmod_scale_s = tm->scale[0];
                        out->tcmod_scale_t = tm->scale[1];
                        break;
                    case TMOD_TURBULENT:
                        out->tcmod_turb_amp  = tm->wave.amplitude;
                        out->tcmod_turb_freq = tm->wave.frequency;
                        break;
                    case TMOD_STRETCH:
                        out->has_tcmod_stretch = 1;
                        out->tcmod_stretch_base  = tm->wave.base;
                        out->tcmod_stretch_amp   = tm->wave.amplitude;
                        out->tcmod_stretch_freq  = tm->wave.frequency;
                        out->tcmod_stretch_phase = tm->wave.phase;
                        break;
                    case TMOD_OFFSET:
                        out->tcmod_offset_s = tm->translate[0];
                        out->tcmod_offset_t = tm->translate[1];
                        break;
                    default:
                        break;
                }
            }
        }

        /* animMap top-level convenience */
        if (firstStage->bundle[0].numImageAnimations > 1) {
            out->has_animmap = 1;
            out->animmap_freq = firstStage->bundle[0].imageAnimationSpeed;
            out->animmap_num_frames = firstStage->bundle[0].numImageAnimations;
            if (out->animmap_num_frames > 8)
                out->animmap_num_frames = 8;
            for (int i = 0; i < out->animmap_num_frames; i++) {
                if (firstStage->bundle[0].image[i])
                    Q_strncpyz(out->animmap_frames[i],
                               firstStage->bundle[0].image[i]->imgName, 64);
            }
        }

        /* nolightmap: true if shader has no lightmap index and
         * no stage references a lightmap */
        if (sh->lightmapIndex < 0) {
            qboolean hasLm = qfalse;
            for (int i = 0; i < sh->numUnfoggedPasses; i++) {
                if (sh->unfoggedStages[i] && sh->unfoggedStages[i]->bundle[0].isLightmap) {
                    hasLm = qtrue;
                    break;
                }
            }
            if (!hasLm)
                out->no_lightmap = 1;
        }
    }

    /* ── Transparency classification ──
     * The engine's FinishShader() sets sort order based on stage blend bits.
     * sort <= SS_OPAQUE means the surface is opaque — any blendFunc on
     * later stages is internal multi-pass compositing (e.g. envmap +
     * alpha-blend window texture), NOT world transparency.
     * Use the engine sort as the primary signal.  Alpha test still
     * overrides because it clips pixels rather than blending. */
    {
        /* Check for alpha test first — takes priority regardless of sort */
        qboolean hasAlphaTest = qfalse;
        for (int i = 0; i < out->stage_count; i++) {
            if (out->stages[i].hasAlphaFunc) {
                out->transparency = SHADER_ALPHA_TEST;
                out->alpha_threshold = out->stages[i].alphaFuncThreshold;
                hasAlphaTest = qtrue;
                break;
            }
        }

        if (!hasAlphaTest && out->stage_count > 0) {
            /* Engine sort <= SS_OPAQUE → opaque surface */
            if (sh->sort <= SS_OPAQUE) {
                out->transparency = SHADER_OPAQUE;
            } else {
                /* Transparent sort order — classify blend type */
                /* Detect lightmap presence */
                qboolean has_lightmap = qfalse;
                for (int s = 0; s < out->stage_count; s++) {
                    if (!out->stages[s].active) continue;
                    if (out->stages[s].isLightmap || out->stages[s].hasNextBundleLightmap) {
                        has_lightmap = qtrue;
                        break;
                    }
                }

                /* Find first non-lightmap, non-env stage with blendFunc */
                int best = -1;
                int fallback_nl = -1;
                for (int s = 0; s < out->stage_count; s++) {
                    if (!out->stages[s].active) continue;
                    if (out->stages[s].isLightmap) continue;
                    if (!out->stages[s].hasBlendFunc) {
                        if (out->stages[s].tcGen != STAGE_TCGEN_ENVIRONMENT)
                            break; /* opaque */
                        continue;
                    }
                    if (fallback_nl < 0) fallback_nl = s;
                    if (out->stages[s].tcGen == STAGE_TCGEN_ENVIRONMENT) continue;
                    best = s;
                    break;
                }
                if (best < 0) best = fallback_nl;

                if (best >= 0) {
                    enum MohaaBlendFactor bs = out->stages[best].blendSrc;
                    enum MohaaBlendFactor bd = out->stages[best].blendDst;
                    qboolean is_filter = (bs == BLEND_DST_COLOR && bd == BLEND_ZERO) ||
                                         (bs == BLEND_ZERO && bd == BLEND_SRC_COLOR);
                    if (has_lightmap && is_filter) {
                        /* Multi-pass lightmap modulation → stays opaque */
                    } else {
                        out->transparency = classify_blend_factors(bs, bd);
                    }
                }
            }
        }

    }
}

/* ===================================================================
 *  Internal cache — avoid re-converting shader_t every call
 * ================================================================ */

/* Simple cache: hash table mapping shader name → converted GodotShaderProps.
 * Uses a fixed-size table with chaining (no STL). */
#define SHADER_CACHE_SIZE 4096
#define SHADER_CACHE_MASK (SHADER_CACHE_SIZE - 1)

typedef struct shader_cache_entry_s {
    char name[MAX_QPATH];
    GodotShaderProps props;
    struct shader_cache_entry_s *next;
} shader_cache_entry_t;

static shader_cache_entry_t *s_shaderCache[SHADER_CACHE_SIZE];
static int s_shaderCacheCount = 0;
/* Pool allocator for cache entries — avoids many small allocations */
#define SHADER_CACHE_POOL_SIZE 8192
static shader_cache_entry_t s_cachePool[SHADER_CACHE_POOL_SIZE];
static int s_cachePoolUsed = 0;

static unsigned int shader_cache_hash(const char *name) {
    unsigned int hash = 0;
    const char *p = name;
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash = hash * 31 + c;
    }
    return hash & SHADER_CACHE_MASK;
}

static void shader_cache_clear(void) {
    memset(s_shaderCache, 0, sizeof(s_shaderCache));
    s_shaderCacheCount = 0;
    s_cachePoolUsed = 0;
}

static const GodotShaderProps *shader_cache_find(const char *name) {
    unsigned int h = shader_cache_hash(name);
    shader_cache_entry_t *e = s_shaderCache[h];
    while (e) {
        if (!Q_stricmp(e->name, name))
            return &e->props;
        e = e->next;
    }
    return NULL;
}

static GodotShaderProps *shader_cache_insert(const char *name) {
    if (s_cachePoolUsed >= SHADER_CACHE_POOL_SIZE) {
        ri.Printf(PRINT_WARNING, "[ShaderAccessors] Cache pool exhausted (%d entries)\n",
                   SHADER_CACHE_POOL_SIZE);
        return NULL;
    }

    unsigned int h = shader_cache_hash(name);
    shader_cache_entry_t *e = &s_cachePool[s_cachePoolUsed++];
    memset(e, 0, sizeof(*e));
    Q_strncpyz(e->name, name, sizeof(e->name));
    /* Lowercase the name for consistent lookup */
    for (char *p = e->name; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    }
    e->next = s_shaderCache[h];
    s_shaderCache[h] = e;
    s_shaderCacheCount++;
    return &e->props;
}

/* ===================================================================
 *  Resolve a shader name → shader_t, with on-demand R_FindShader()
 * ================================================================ */
static shader_t *resolve_shader(const char *name) {
    shader_t *sh;

    /* First try already-loaded lookup (cheap) */
    sh = R_FindShaderByName(name);
    if (sh && sh != tr.defaultShader) {
        return sh;
    }

    /* Not yet loaded — call R_FindShader which will parse the .shader
     * text definition (or create an implicit shader from the image).
     * Use LIGHTMAP_NONE for generic lookups. */
    sh = R_FindShader(name, LIGHTMAP_NONE, qtrue, qtrue, qtrue, qtrue);
    /* Check both the defaultShader flag AND pointer identity.
     * CreateInternalShaders doesn't set tr.defaultShader->defaultShader = qtrue,
     * so pointer comparison is the reliable check. */
    if (sh && sh != tr.defaultShader && !sh->defaultShader) {
        return sh;
    }

    return NULL;
}

/* ===================================================================
 *  Public API — same interface as godot_shader_props.cpp
 * ================================================================ */

void Godot_ShaderProps_Load(void) {
    shader_cache_clear();

    /* R_Init() has already been called from GR_BeginRegistration,
     * which ran R_StartupShaders() to parse all .shader text files.
     * Individual shader_t objects are created on demand by R_FindShader.
     * Nothing else to do here — we resolve shaders lazily.
     *
     * Pre-populate cache with any shaders already loaded in tr.shaders[]
     * (e.g. from R_Init builtin shaders). */
    for (int i = 0; i < tr.numShaders; i++) {
        shader_t *sh = tr.shaders[i];
        if (!sh || sh->defaultShader || !sh->name[0])
            continue;

        /* Lowercase name for cache key */
        char lname[MAX_QPATH];
        Q_strncpyz(lname, sh->name, sizeof(lname));
        for (char *p = lname; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
        }

        if (!shader_cache_find(lname)) {
            GodotShaderProps *props = shader_cache_insert(lname);
            if (props)
                convert_shader(sh, props);
        }
    }

    /* ri.Printf may be NULL in dedicated mode (renderer not initialised). */
    if (ri.Printf) {
        ri.Printf(PRINT_ALL, "[ShaderAccessors] Pre-cached %d shaders from tr.shaders[]\n",
                  s_shaderCacheCount);
    } else {
        Com_Printf("[ShaderAccessors] Pre-cached %d shaders from tr.shaders[]\n",
                   s_shaderCacheCount);
    }
}

void Godot_ShaderProps_Unload(void) {
    shader_cache_clear();
}

/* Invalidate the renderer's shader dimension cache — called after
 * shader props are loaded so stale dimension data is cleared. */
extern void Godot_Renderer_InvalidateShaderDimCache(void);

const GodotShaderProps *Godot_ShaderProps_Find(const char *shader_name) {
    const GodotShaderProps *cached;
    shader_t *sh;
    GodotShaderProps *props;
    char lname[MAX_QPATH];

    if (!shader_name || !shader_name[0])
        return NULL;

    /* Lowercase for consistent lookup */
    Q_strncpyz(lname, shader_name, sizeof(lname));
    for (char *p = lname; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    }

    /* 1. Check cache */
    cached = shader_cache_find(lname);
    if (cached)
        return cached;

    /* 2. Resolve via real renderer */
    sh = resolve_shader(shader_name);
    if (!sh)
        return NULL;

    /* 3. Convert and cache */
    props = shader_cache_insert(lname);
    if (!props)
        return NULL;

    convert_shader(sh, props);
    return props;
}

int Godot_ShaderProps_Count(void) {
    return s_shaderCacheCount;
}

const char *Godot_ShaderProps_GetSkyEnv(void) {
    /* Scan all loaded shaders for a sky shader */
    for (int i = 0; i < tr.numShaders; i++) {
        shader_t *sh = tr.shaders[i];
        if (!sh || !sh->isSky)
            continue;
        if (!sh->sky.outerbox[0])
            continue;

        /* Extract env basename from sky box image name */
        static char s_skyEnv[MAX_QPATH];
        const char *skyname = sh->sky.outerbox[0]->imgName;
        Q_strncpyz(s_skyEnv, skyname, sizeof(s_skyEnv));
        /* Strip _ft/_bk/_rt/_lf/_up/_dn suffix */
        int len = (int)strlen(s_skyEnv);
        if (len >= 3 && s_skyEnv[len - 3] == '_')
            s_skyEnv[len - 3] = '\0';
        /* Strip file extension */
        char *dot = strrchr(s_skyEnv, '.');
        if (dot) *dot = '\0';
        return s_skyEnv;
    }

    /* Also check the cache */
    for (int h = 0; h < SHADER_CACHE_SIZE; h++) {
        shader_cache_entry_t *e = s_shaderCache[h];
        while (e) {
            if (e->props.is_sky && e->props.sky_env[0])
                return e->props.sky_env;
            e = e->next;
        }
    }

    return NULL;
}

/* C-linkage helper for godot_renderer.c */
int Godot_ShaderProps_GetTextureMap(const char *shader_name, char *out_path, int out_size) {
    const GodotShaderProps *sp;
    int i;

    if (!shader_name || !out_path || out_size <= 0)
        return 0;

    sp = Godot_ShaderProps_Find(shader_name);

    /* Retry with basename if namespaced (e.g. "MENU/multiarrow" → "multiarrow") */
    if (!sp) {
        const char *slash = strrchr(shader_name, '/');
        if (slash && slash[1])
            sp = Godot_ShaderProps_Find(slash + 1);
    }

    if (!sp || sp->stage_count <= 0)
        return 0;

    for (i = 0; i < sp->stage_count; i++) {
        const struct MohaaShaderStage *st = &sp->stages[i];
        if (!st->active) continue;
        if (st->isLightmap) continue;
        if (st->map[0] == '\0') continue;
        if (st->map[0] == '$') continue;

        {
            int len = (int)strlen(st->map);
            if (len >= out_size) len = out_size - 1;
            memcpy(out_path, st->map, len);
            out_path[len] = '\0';
            return 1;
        }
    }

    return 0;
}

/* ===================================================================
 *  Sprite dimension accessor — reads from real engine shader_t/image_t
 *
 *  Mirrors the data path in SPR_RegisterSprite (tr_sprite.c):
 *    shader = R_FindShader(name, ...)
 *    img    = shader->unfoggedStages[0]->bundle[0].image[0]
 *    width  = img->width;   height = img->height;
 *    scale  = shader->sprite.scale;
 *
 *  This guarantees our Godot-side sprite sizing uses the exact same
 *  dimensions that the engine's RB_DrawSprite would use.
 * ================================================================ */

int Godot_Sprite_GetEngineSize(const char *shader_name,
                               int *out_width, int *out_height,
                               float *out_sprite_scale)
{
    shader_t *sh;
    image_t  *img;

    if (!shader_name || !shader_name[0])
        return 0;

    /* Look up existing shader first (cheap), then force-create if needed */
    sh = R_FindShaderByName(shader_name);
    if (!sh || sh == tr.defaultShader)
        sh = R_FindShader(shader_name, -1, qfalse, qfalse, qfalse, qfalse);
    if (!sh || sh == tr.defaultShader)
        return 0;

    /* Read the first stage's first image — same as SPR_RegisterSprite */
    img = NULL;
    if (sh->unfoggedStages[0])
        img = sh->unfoggedStages[0]->bundle[0].image[0];
    if (!img)
        return 0;

    if (out_width)        *out_width        = img->width;
    if (out_height)       *out_height       = img->height;
    if (out_sprite_scale) *out_sprite_scale = sh->sprite.scale;
    return 1;
}
