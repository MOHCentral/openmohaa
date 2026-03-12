/* godot_shader_props.h — Shader transparency / blend property lookup
 *
 * Parses MOHAA/Q3-style .shader script files from the engine VFS and
 * provides a lookup table mapping shader names to their transparency
 * and cull properties.  Used by the BSP mesh builder and entity
 * material creation to apply correct Godot material transparency.
 *
 * The parser reads scripts/shaderlist.txt to discover which .shader
 * files to load, then extracts alphaFunc, blendFunc, surfaceparm trans,
 * and cull directives from each shader definition.
 *
 * Phases 66–72: Extended to parse ALL stages per shader (not just the
 * first), with per-stage rgbGen, alphaGen, tcGen, tcMod, animMap, and
 * blend function data.  The MohaaShaderStage struct holds per-stage data;
 * GodotShaderProps::stages[] stores up to MOHAA_SHADER_STAGE_MAX stages.
 */

#ifndef GODOT_SHADER_PROPS_H
#define GODOT_SHADER_PROPS_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

/* ── Transparency classification ── */
typedef enum GodotShaderTransparency {
    SHADER_OPAQUE = 0,
    SHADER_ALPHA_TEST,       /* alphaFunc GT0/LT128/GE128 → ALPHA_SCISSOR */
    SHADER_ALPHA_BLEND,      /* blendFunc blend / GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA → TRANSPARENCY_ALPHA */
    SHADER_ADDITIVE,         /* blendFunc add / GL_ONE GL_ONE → BLEND_MODE_ADD */
    SHADER_MULTIPLICATIVE,   /* blendFunc filter / GL_DST_COLOR GL_ZERO → BLEND_MODE_MUL */
    SHADER_MULTIPLICATIVE_INV, /* GL_ZERO GL_ONE_MINUS_SRC_COLOR → dst*(1-src) */
    SHADER_ALPHA_BLEND_INV,  /* GL_ONE_MINUS_SRC_ALPHA GL_SRC_ALPHA → src*(1-a)+dst*a (inverted alpha) */
} GodotShaderTransparency;

/* ── Cull mode ── */
typedef enum GodotShaderCull {
    SHADER_CULL_BACK  = 0,   /* default: back-face culling */
    SHADER_CULL_FRONT = 1,   /* cull front */
    SHADER_CULL_NONE  = 2,   /* cull none / cull twosided / cull disable */
} GodotShaderCull;

/* ── Phase 66–72: Per-stage enums and structs ── */

#define MOHAA_SHADER_STAGE_MAX          8
#define MOHAA_SHADER_STAGE_MAX_TCMODS   4
#define MOHAA_SHADER_STAGE_MAX_ANIM_FRAMES 8

/* GL blend factors as used in Q3 .shader blendFunc directives */
typedef enum MohaaBlendFactor {
    BLEND_ONE = 0,
    BLEND_ZERO,
    BLEND_SRC_ALPHA,
    BLEND_ONE_MINUS_SRC_ALPHA,
    BLEND_DST_COLOR,
    BLEND_SRC_COLOR,
    BLEND_ONE_MINUS_DST_COLOR,
    BLEND_ONE_MINUS_SRC_COLOR,
    BLEND_DST_ALPHA,
    BLEND_ONE_MINUS_DST_ALPHA,
} MohaaBlendFactor;

/* Wave function types for rgbGen wave / alphaGen wave / tcMod stretch */
typedef enum MohaaWaveFunc {
    WAVE_SIN = 0,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_INVERSE_SAWTOOTH,
} MohaaWaveFunc;

/* Per-stage rgbGen type */
typedef enum MohaaStageRgbGen {
    STAGE_RGBGEN_IDENTITY = 0,
    STAGE_RGBGEN_IDENTITY_LIGHTING,
    STAGE_RGBGEN_VERTEX,
    STAGE_RGBGEN_WAVE,
    STAGE_RGBGEN_ENTITY,
    STAGE_RGBGEN_ONE_MINUS_ENTITY,
    STAGE_RGBGEN_LIGHTING_DIFFUSE,
    STAGE_RGBGEN_CONST,
    STAGE_RGBGEN_GLOBAL_COLOR,      /* CGEN_GLOBAL_COLOR: modulate texture by SetColor() */
    STAGE_RGBGEN_SCOORD,            /* CGEN_SCOORD: use S texture coordinate as RGB */
    STAGE_RGBGEN_TCOORD,            /* CGEN_TCOORD: use T texture coordinate as RGB */
    STAGE_RGBGEN_DOT,               /* CGEN_DOT: dot(normal, view)² */
    STAGE_RGBGEN_ONE_MINUS_DOT,     /* CGEN_ONE_MINUS_DOT: 1 - dot(normal, view)² */
} MohaaStageRgbGen;

/* Per-stage alphaGen type */
typedef enum MohaaStageAlphaGen {
    STAGE_ALPHAGEN_IDENTITY = 0,
    STAGE_ALPHAGEN_VERTEX,
    STAGE_ALPHAGEN_WAVE,
    STAGE_ALPHAGEN_ENTITY,
    STAGE_ALPHAGEN_ONE_MINUS_ENTITY,
    STAGE_ALPHAGEN_PORTAL,
    STAGE_ALPHAGEN_CONST,
    STAGE_ALPHAGEN_GLOBAL_ALPHA,            /* AGEN_GLOBAL_ALPHA: use alpha from SetColor() */
    STAGE_ALPHAGEN_DOT,                     /* AGEN_DOT: dot(normal, view)² scaled by [alphaMin, alphaMax] */
    STAGE_ALPHAGEN_ONE_MINUS_DOT,           /* AGEN_ONE_MINUS_DOT: 1 - dot(normal, view)² */
    STAGE_ALPHAGEN_DOT_VIEW,                /* AGEN_DOT_VIEW: abs(dot(viewForward, normal)) */
    STAGE_ALPHAGEN_ONE_MINUS_DOT_VIEW,      /* AGEN_ONE_MINUS_DOT_VIEW */
    STAGE_ALPHAGEN_DIST_FADE,               /* AGEN_DIST_FADE: distance fade [near, range] */
    STAGE_ALPHAGEN_ONE_MINUS_DIST_FADE,     /* AGEN_ONE_MINUS_DIST_FADE */
    STAGE_ALPHAGEN_TIKI_DIST_FADE,          /* AGEN_TIKI_DIST_FADE: TIKI distance fade */
    STAGE_ALPHAGEN_ONE_MINUS_TIKI_DIST_FADE,/* AGEN_ONE_MINUS_TIKI_DIST_FADE */
    STAGE_ALPHAGEN_HEIGHT_FADE,             /* AGEN_HEIGHT_FADE: vertical Z height fade */
    STAGE_ALPHAGEN_SKYALPHA,                /* AGEN_SKYALPHA: sky atmosphere alpha */
    STAGE_ALPHAGEN_ONE_MINUS_SKYALPHA,      /* AGEN_ONE_MINUS_SKYALPHA */
    STAGE_ALPHAGEN_SCOORD,                  /* AGEN_SCOORD: S texture coordinate as alpha */
    STAGE_ALPHAGEN_TCOORD,                  /* AGEN_TCOORD: T texture coordinate as alpha */
    STAGE_ALPHAGEN_LIGHTING_SPECULAR,       /* AGEN_LIGHTING_SPECULAR: specular highlight */
} MohaaStageAlphaGen;

/* Per-stage tcGen type */
typedef enum MohaaStageTcGen {
    STAGE_TCGEN_BASE = 0,
    STAGE_TCGEN_LIGHTMAP,
    STAGE_TCGEN_ENVIRONMENT,
    STAGE_TCGEN_VECTOR,
} MohaaStageTcGen;

/* Per-stage tcMod type */
typedef enum MohaaStageTcModType {
    TCMOD_NONE = 0,
    TCMOD_SCROLL,
    TCMOD_ROTATE,
    TCMOD_SCALE,
    TCMOD_TURB,
    TCMOD_STRETCH,
    TCMOD_OFFSET,
    TCMOD_WAVETRANS,
    TCMOD_WAVETRANT,
    TCMOD_BULGE,
    TCMOD_TRANSFORM,
    TCMOD_ENTITY_TRANSLATE,
    TCMOD_PARALLAX,
    TCMOD_MACRO,
} MohaaStageTcModType;

typedef enum MohaaStageTcModFlags {
    TCMOD_FLAG_FROMENTITY_S         = 1 << 0,
    TCMOD_FLAG_FROMENTITY_T         = 1 << 1,
    TCMOD_FLAG_FROMENTITY_ROT_SPEED = 1 << 2,
    TCMOD_FLAG_FROMENTITY_ROT_START = 1 << 3,
} MohaaStageTcModFlags;

/* Wave function parameters (shared by rgbGen wave, alphaGen wave, tcMod stretch) */
typedef struct MohaaWaveParams {
    MohaaWaveFunc func;
    float base;
    float amplitude;
    float phase;
    float frequency;
} MohaaWaveParams;

/* Single tcMod directive within a stage */
typedef struct MohaaStageTcMod {
    MohaaStageTcModType type;
    float params[8];   /* type-dependent payload */
    unsigned int flags; /* MohaaStageTcModFlags */
    MohaaWaveParams wave; /* only for TCMOD_STRETCH */
} MohaaStageTcMod;

/* Per-stage shader data — one stage = one { } block inside a shader definition */
typedef struct MohaaShaderStage {
    bool active;                    /* false when ifCvar/ifCvarnot disables this stage */
    char map[64];                    /* texture path or "$lightmap" */
    MohaaBlendFactor blendSrc;      /* GL blend source factor */
    MohaaBlendFactor blendDst;      /* GL blend destination factor */
    bool hasBlendFunc;               /* explicit blendFunc present */

    MohaaStageRgbGen rgbGen;
    MohaaWaveParams  rgbWave;       /* valid when rgbGen == STAGE_RGBGEN_WAVE */
    float rgbConst[3];               /* valid when rgbGen == STAGE_RGBGEN_CONST */

    MohaaStageAlphaGen alphaGen;
    MohaaWaveParams    alphaWave;   /* valid when alphaGen == STAGE_ALPHAGEN_WAVE */
    float alphaConst;                /* valid when alphaGen == STAGE_ALPHAGEN_CONST */
    float alphaPortalDist;           /* valid when alphaGen == STAGE_ALPHAGEN_PORTAL */

    MohaaStageTcGen tcGen;
    float tcGenVecS[3];              /* valid when tcGen == STAGE_TCGEN_VECTOR */
    float tcGenVecT[3];              /* valid when tcGen == STAGE_TCGEN_VECTOR */

    MohaaStageTcMod tcMods[MOHAA_SHADER_STAGE_MAX_TCMODS];
    int tcModCount;

    float animMapFreq;               /* animMap frequency (0 = not animated) */
    char  animMapFrames[MOHAA_SHADER_STAGE_MAX_ANIM_FRAMES][64];
    int   animMapFrameCount;

    bool isClampMap;                 /* clampMap vs map */
    bool isLightmap;                 /* stage uses $lightmap */
    bool hasNextBundleLightmap;      /* nextBundle contains $lightmap (multi-tex lightmap) */

    bool hasAlphaFunc;               /* alphaFunc present in this stage */
    float alphaFuncThreshold;        /* 0.01 for GT0, 0.5 for GE128/LT128 */

    /* Phase 143: depth write control */
    bool depthWriteExplicit;         /* true if depthwrite/nodepthwrite was specified */
    bool depthWriteEnabled;          /* true = force depth write; false = disable depth write */
    bool noDepthTest;                /* true = disable depth testing (noDepthTest) */

    /* Phase 39: alphaMin/alphaMax — used by DOT, DOT_VIEW, DIST_FADE, HEIGHT_FADE alphaGen types */
    float alphaMin;                  /* lower bound / near distance */
    float alphaMax;                  /* upper bound / range */
    float specOrigin[3];             /* for AGEN_LIGHTING_SPECULAR */
} MohaaShaderStage;

/* ── Per-shader properties ── */
typedef struct GodotShaderProps {
    GodotShaderTransparency transparency;
    float alpha_threshold;   /* for SHADER_ALPHA_TEST: 0.01 (GT0), 0.5 (GE128/LT128) */
    GodotShaderCull cull;
    bool is_portal;          /* surfaceparm portal — skip rendering */
    bool is_sky;             /* surfaceparm sky */
    bool no_lightmap;        /* surfaceparm nolightmap — render fullbright */
    bool has_surfaceparm_trans; /* surfaceparm trans — BSP compiler flag only */
    char sky_env[64];        /* skyParms env basename (e.g. "env/m5l2") */
    float sky_cloud_height;  /* skyParms cloudHeight (0 = no clouds) */

    /* Phase 36: tcMod scroll/rotate/scale animation parameters */
    float tcmod_scroll_s;    /* texcoord S scroll speed (units/sec) */
    float tcmod_scroll_t;    /* texcoord T scroll speed (units/sec) */
    float tcmod_rotate;      /* texcoord rotation speed (degrees/sec) */
    float tcmod_scale_s;     /* texcoord S scale factor (1.0 = no change) */
    float tcmod_scale_t;     /* texcoord T scale factor (1.0 = no change) */
    float tcmod_turb_amp;    /* turbulence amplitude */
    float tcmod_turb_freq;   /* turbulence frequency */
    float tcmod_offset_s;    /* Phase 144: static UV S offset */
    float tcmod_offset_t;    /* Phase 144: static UV T offset */
    bool  has_tcmod;         /* true if any tcMod directive was found */

    /* Phase 61: animMap — animated texture sequence */
    float animmap_freq;             /* frames per second */
    int   animmap_num_frames;       /* number of textures in sequence */
    char  animmap_frames[8][64];    /* up to 8 texture names */
    bool  has_animmap;

    /* Phase 62: tcGen environment — cubic environment mapping */
    bool  tcgen_environment;

    /* Phase 63: deformVertexes */
    bool  has_deform;
    int   deform_type;              /* 0=wave, 1=bulge, 2=move, 3=autosprite, 4=autosprite2 */
    float deform_div;               /* wave divisor */
    float deform_base;
    float deform_amplitude;
    float deform_frequency;
    float deform_phase;

    /* Phase 64: rgbGen */
    int   rgbgen_type;              /* 0=identity, 1=vertex, 2=wave, 3=entity, 4=const */
    MohaaWaveFunc rgbgen_wave_func; /* Phase 141: wave function type (sin/triangle/square/sawtooth/inversesawtooth) */
    float rgbgen_wave_base;
    float rgbgen_wave_amp;
    float rgbgen_wave_freq;
    float rgbgen_wave_phase;
    float rgbgen_const[3];

    /* Phase 65: alphaGen */
    int   alphagen_type;            /* 0=identity, 1=vertex, 2=wave, 3=entity, 4=const */
    MohaaWaveFunc alphagen_wave_func; /* Phase 141: wave function type */
    float alphagen_wave_base;
    float alphagen_wave_amp;
    float alphagen_wave_freq;
    float alphagen_wave_phase;
    float alphagen_const;

    /* Phase 66: tcMod stretch */
    bool  has_tcmod_stretch;
    MohaaWaveFunc tcmod_stretch_func;
    float tcmod_stretch_base;
    float tcmod_stretch_amp;
    float tcmod_stretch_freq;
    float tcmod_stretch_phase;

    /* Phase 67: sort hint */
    int   sort_key;                 /* 0=default, 1-16 explicit sort order */

    /* Phase 68: nomipmaps / nopicmip */
    bool  no_mipmaps;
    bool  no_picmip;

    /* Phase 69: multi-stage count (first stage is primary) */
    int   num_stages;

    /* Phase 70: fog parameters */
    bool  has_fog;
    float fog_color[3];
    float fog_distance;

    /* Sprite model scaling — from "spritescale" keyword (default 1.0) */
    float sprite_scale;

    /* Phase 66–72: Per-stage shader data (all stages parsed) */
    MohaaShaderStage stages[MOHAA_SHADER_STAGE_MAX];
    int stage_count;             /* number of parsed stages (== num_stages) */
} GodotShaderProps;

/* ── API ── */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse all .shader files listed in scripts/shaderlist.txt and build
 * the internal lookup table.  Safe to call multiple times (clears
 * previous data).  Uses the engine VFS for file access.
 */
void Godot_ShaderProps_Load();

/*
 * Release all parsed shader data.
 */
void Godot_ShaderProps_Unload();

/*
 * Look up properties for a shader by name.
 * Returns nullptr if the shader has no .shader definition (treat as opaque).
 */
const GodotShaderProps *Godot_ShaderProps_Find(const char *shader_name);

/*
 * Look up properties for a shader by name with an explicit lightmap domain.
 * Use LIGHTMAP_NONE for 3D/world path and LIGHTMAP_2D for NoMip/UI path.
 */
const GodotShaderProps *Godot_ShaderProps_Find_Lightmap(const char *shader_name, int lightmap_index);

/*
 * Look up properties for a 2D/UI shader by name.
 * Uses LIGHTMAP_2D resolution (matching RE_RegisterShaderNoMip parity)
 * so implicit shaders get CGEN_GLOBAL_COLOR (respects SetColor).
 */
const GodotShaderProps *Godot_ShaderProps_Find_2D(const char *shader_name);

/*
 * Look up properties for a shader by handle, using the same lightmap domain
 * that was used when the handle was registered in the renderer.
 */
const GodotShaderProps *Godot_ShaderProps_Find_ByHandle(int shader_handle);

/*
 * Returns the number of parsed shader definitions.
 */
int Godot_ShaderProps_Count();

/*
 * Returns the skyParms env basename for the first sky shader found,
 * or nullptr if no sky shader was parsed.  E.g. "env/m5l2".
 */
const char *Godot_ShaderProps_GetSkyEnv();

/*
 * Returns sky cloud data for the first sky shader found.
 * out_cloud_height is set to the cloud height value.
 * out_cloud_map receives the first non-lightmap stage texture path.
 * out_scroll_s/out_scroll_t receive tcMod scroll speeds (may be NULL).
 * out_is_additive receives 1 if blendFunc is additive, 0 otherwise (may be NULL).
 * Returns 1 if cloud data is available, 0 otherwise.
 */
int Godot_ShaderProps_GetSkyCloudData(float *out_cloud_height,
                                      char *out_cloud_map, int map_size,
                                      float *out_scroll_s, float *out_scroll_t,
                                      int *out_is_additive);

/*
 * C-linkage helper: look up a shader's first non-lightmap stage `map`
 * texture path.  Writes into the caller's buffer.
 * Returns 1 if found, 0 if no shader definition or no valid stage.
 */
int Godot_ShaderProps_GetTextureMap(const char *shader_name, char *out_path, int out_size);

/*
 * Override a font shader's stages so that SetColor controls text colour.
 * Forces CGEN_GLOBAL_COLOR / AGEN_GLOBAL_ALPHA on all lightmapIndex
 * variants (LIGHTMAP_NONE and LIGHTMAP_2D) of the given shader name.
 * Must be called AFTER GR_RegisterShaderNoMip so the real shader_t exists.
 */
void Godot_ShaderAccessor_OverrideFontShader(const char *shader_name);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_SHADER_PROPS_H */
