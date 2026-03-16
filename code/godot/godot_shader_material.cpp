/* godot_shader_material.cpp — Multi-stage shader → Godot ShaderMaterial builder
 *
 * Generates .gdshader code from parsed MOHAA
 * multi-stage shader definitions and creates ShaderMaterial instances.
 *
 * The generated shader composites multiple texture stages using their
 * per-stage blendFunc, applies rgbGen/alphaGen modulations (including
 * wave functions), handles tcGen environment/lightmap/vector UV
 * generation, tcMod UV animations, and animMap texture sequences.
 */

#include "godot_shader_material.h"
#include "godot_vertex_deform.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

/* ===================================================================
 *  Renderer colour-space detection
 *
 *  Forward+ / Mobile work in linear space and apply a linear→sRGB
 *  conversion at output.  Our shaders composite in gamma space, so
 *  the final result needs a pow(2.2) gamma→linear conversion before
 *  being written to ALBEDO — the sRGB encode undoes it.
 *
 *  The GL Compatibility renderer (used on WebGL2) handles colour
 *  space differently: the sRGB output conversion is either absent or
 *  applied at a different stage.  The pow(2.2) double-darkens the
 *  output.  On this backend we skip the conversion.
 * ================================================================ */
static int s_needs_gamma_to_linear = -1;  /* -1 = not yet checked */

static bool needs_gamma_to_linear_conversion() {
    if (s_needs_gamma_to_linear < 0) {
        /* RenderingDevice is only available on RD-based backends
         * (Forward+ and Mobile).  Returns nullptr on GL Compatibility. */
        RenderingServer *rs = RenderingServer::get_singleton();
        s_needs_gamma_to_linear = (rs && rs->get_rendering_device() != nullptr) ? 1 : 0;
    }
    return s_needs_gamma_to_linear != 0;
}

/* ===================================================================
 *  Internal shader cache — avoids regenerating identical shaders
 * ================================================================ */
static std::unordered_map<std::string, Ref<Shader>> *s_shader_cache =
    new std::unordered_map<std::string, Ref<Shader>>();

/* ===================================================================
 *  Material registry — tracks all built ShaderMaterials so
 *  bsp_shadow_darkness can be updated at runtime without rebuilding.
 * ================================================================ */
#include <vector>
static std::vector<Ref<ShaderMaterial>> *s_mat_registry =
    new std::vector<Ref<ShaderMaterial>>();

/* ===================================================================
 *  Helper: float → string with minimal decimal places
 * ================================================================ */
static std::string ftos(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", v);
    /* Trim trailing zeros but keep at least one decimal */
    char *dot = strchr(buf, '.');
    if (dot) {
        char *e = buf + strlen(buf) - 1;
        while (e > dot + 1 && *e == '0') *e-- = '\0';
    }
    return std::string(buf);
}

/* ===================================================================
 *  Wave function GLSL code generation
 * ================================================================ */

/* Returns true if any stage uses a wave function (rgbGen wave, alphaGen wave, tcMod stretch) */
static bool needs_wave_functions(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        if (s->rgbGen == STAGE_RGBGEN_WAVE) return true;
        if (s->alphaGen == STAGE_ALPHAGEN_WAVE) return true;
        for (int t = 0; t < s->tcModCount; t++) {
            if (s->tcMods[t].type == TCMOD_STRETCH ||
                s->tcMods[t].type == TCMOD_WAVETRANS ||
                s->tcMods[t].type == TCMOD_WAVETRANT) return true;
        }
    }
    return false;
}

/* Returns true if any stage uses tcGen environment */
static bool needs_env_mapping(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].tcGen == STAGE_TCGEN_ENVIRONMENT)
            return true;
    }
    return false;
}

/* Returns true if any stage uses tcGen vector */
static bool needs_tcgen_vector(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].tcGen == STAGE_TCGEN_VECTOR)
            return true;
    }
    return false;
}

/* Returns true if any stage has tcMod directives */
static bool needs_tcmod(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].tcModCount > 0) return true;
    }
    return false;
}

/* Returns true if any stage uses animMap */
static bool needs_animmap(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].animMapFrameCount > 0) return true;
    }
    return false;
}

/* Returns true if any stage uses entity color */
static bool needs_entity_color(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        if (s->rgbGen == STAGE_RGBGEN_ENTITY || s->rgbGen == STAGE_RGBGEN_ONE_MINUS_ENTITY)
            return true;
        if (s->alphaGen == STAGE_ALPHAGEN_ENTITY || s->alphaGen == STAGE_ALPHAGEN_ONE_MINUS_ENTITY)
            return true;
    }
    return false;
}

/* Returns true if any stage uses global colour (SetColor() state) */
static bool needs_global_color(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        if (s->rgbGen == STAGE_RGBGEN_GLOBAL_COLOR) return true;
        if (s->alphaGen == STAGE_ALPHAGEN_GLOBAL_ALPHA) return true;
    }
    return false;
}

/* Returns true if any stage uses sky alpha */
static bool needs_sky_alpha(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        if (s->alphaGen == STAGE_ALPHAGEN_SKYALPHA ||
            s->alphaGen == STAGE_ALPHAGEN_ONE_MINUS_SKYALPHA) return true;
    }
    return false;
}

/* Returns true if any stage uses lighting diffuse (needs lit shading) */
static bool needs_diffuse_lighting(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].rgbGen == STAGE_RGBGEN_LIGHTING_DIFFUSE)
            return true;
    }
    return false;
}

/* Returns true if any stage uses tcMod turb or tcGen vector (which need v_pos local coordinates) */
static bool needs_v_pos(const GodotShaderProps *props) {
    if (needs_tcgen_vector(props)) return true;
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        for (int t = 0; t < s->tcModCount; t++) {
            if (s->tcMods[t].type == TCMOD_TURB) return true;
        }
    }
    return false;
}

/* ===================================================================
 *  Shader code generation
 * ================================================================ */

/* Generate a unique cache key for the shader props */
static std::string make_cache_key(const GodotShaderProps *props) {
    /* Build a key from the stage configuration */
    std::string key;
    key += "t" + std::to_string(props->transparency);
    key += "c" + std::to_string(props->cull);
    key += "n" + std::to_string(props->stage_count);
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        key += "|s" + std::to_string(i);
        key += "m" + std::string(s->map);
        key += "b" + std::to_string(s->blendSrc) + "," + std::to_string(s->blendDst);
        key += "h" + std::to_string(s->hasBlendFunc);
        key += "r" + std::to_string(s->rgbGen);
        key += "a" + std::to_string(s->alphaGen);
        key += "g" + std::to_string(s->tcGen);
        key += "tc" + std::to_string(s->tcModCount);
        key += "af" + std::to_string(s->animMapFrameCount);
        key += "cl" + std::to_string(s->isClampMap);
        key += "lm" + std::to_string(s->isLightmap);
        key += "nb" + std::to_string(s->hasNextBundleLightmap);
        if (s->rgbGen == STAGE_RGBGEN_WAVE) {
            key += "rw" + ftos(s->rgbWave.base) + "," + ftos(s->rgbWave.amplitude) +
                   "," + ftos(s->rgbWave.phase) + "," + ftos(s->rgbWave.frequency) +
                   "," + std::to_string(s->rgbWave.func);
        }
        if (s->alphaGen == STAGE_ALPHAGEN_WAVE) {
            key += "aw" + ftos(s->alphaWave.base) + "," + ftos(s->alphaWave.amplitude) +
                   "," + ftos(s->alphaWave.phase) + "," + ftos(s->alphaWave.frequency) +
                   "," + std::to_string(s->alphaWave.func);
        }
        for (int t = 0; t < s->tcModCount; t++) {
            const MohaaStageTcMod *tm = &s->tcMods[t];
            key += "tm" + std::to_string(tm->type);
            key += "f" + std::to_string(tm->flags);
            key += "," + ftos(tm->params[0]) + "," + ftos(tm->params[1]);
            key += "," + ftos(tm->params[2]) + "," + ftos(tm->params[3]);
            key += "," + ftos(tm->params[4]) + "," + ftos(tm->params[5]);
            key += "," + ftos(tm->params[6]) + "," + ftos(tm->params[7]);
            key += ",w" + std::to_string(tm->wave.func);
            key += "," + ftos(tm->wave.base) + "," + ftos(tm->wave.amplitude);
            key += "," + ftos(tm->wave.phase) + "," + ftos(tm->wave.frequency);
        }
        if (s->animMapFrameCount > 0) {
            key += "freq" + ftos(s->animMapFreq);
            key += "fc" + std::to_string(s->animMapFrameCount);
        }
    }
    /* deformVertexes parameters — different deforms produce different vertex shaders */
    if (props->has_deform) {
        key += "D" + std::to_string(props->deform_type);
        key += "," + ftos(props->deform_div);
        key += "," + ftos(props->deform_base);
        key += "," + ftos(props->deform_amplitude);
        key += "," + ftos(props->deform_frequency);
        key += "," + ftos(props->deform_phase);
    }

    return key;
}

/* Emit GLSL wave function definitions */
static void emit_wave_functions(std::string &code) {
    code += "float wave_sin(float b, float a, float ph, float fr, float t) {\n";
    code += "    return b + a * sin((t * fr + ph) * 6.283185);\n";
    code += "}\n";
    code += "float wave_triangle(float b, float a, float ph, float fr, float t) {\n";
    code += "    float p = fract(t * fr + ph);\n";
    code += "    return b + a * (p < 0.5 ? p * 4.0 - 1.0 : 3.0 - p * 4.0);\n";
    code += "}\n";
    code += "float wave_square(float b, float a, float ph, float fr, float t) {\n";
    code += "    return b + a * sign(sin((t * fr + ph) * 6.283185));\n";
    code += "}\n";
    code += "float wave_sawtooth(float b, float a, float ph, float fr, float t) {\n";
    code += "    return b + a * fract(t * fr + ph);\n";
    code += "}\n";
    code += "float wave_inversesawtooth(float b, float a, float ph, float fr, float t) {\n";
    code += "    return b + a * (1.0 - fract(t * fr + ph));\n";
    code += "}\n\n";
}

/* Generate GLSL call for a specific wave function */
static std::string wave_call(const MohaaWaveParams *w, const char *time_var) {
    const char *func_name = "wave_sin";
    switch (w->func) {
        case WAVE_SIN:                func_name = "wave_sin"; break;
        case WAVE_TRIANGLE:           func_name = "wave_triangle"; break;
        case WAVE_SQUARE:             func_name = "wave_square"; break;
        case WAVE_SAWTOOTH:           func_name = "wave_sawtooth"; break;
        case WAVE_INVERSE_SAWTOOTH:   func_name = "wave_inversesawtooth"; break;
    }
    return std::string(func_name) + "(" + ftos(w->base) + ", " + ftos(w->amplitude) +
           ", " + ftos(w->phase) + ", " + ftos(w->frequency) + ", " + time_var + ")";
}

/* Generate UV computation code for a stage, accounting for tcGen and tcMod */
static std::string gen_uv_code(int stage_idx, const MohaaShaderStage *s) {
    std::string si = std::to_string(stage_idx);
    std::string code;

    /* Base UV selection based on tcGen.
     * Safety: if the stage is flagged as isLightmap (from `map $lightmap`),
     * always use UV2 regardless of tcGen — the parser should already set
     * tcGen = STAGE_TCGEN_LIGHTMAP, but this guards against any mismatch. */
    MohaaStageTcGen effective_tcgen = s->tcGen;
    if (s->isLightmap && effective_tcgen == STAGE_TCGEN_BASE)
        effective_tcgen = STAGE_TCGEN_LIGHTMAP;

    switch (effective_tcgen) {
        case STAGE_TCGEN_LIGHTMAP:
            code += "    vec2 uv" + si + " = UV2;\n";
            break;
        case STAGE_TCGEN_ENVIRONMENT:
            code += "    vec3 env_view" + si + " = normalize(VERTEX);\n";
            code += "    vec3 env_refl" + si + " = reflect(env_view" + si + ", NORMAL);\n";
            code += "    vec2 uv" + si + " = env_refl" + si + ".xy * 0.5 + 0.5;\n";
            break;
        case STAGE_TCGEN_VECTOR:
            code += "    {\n";
            code += "        vec3 q_pos" + si + " = vec3(-v_pos.z, -v_pos.x, v_pos.y) * 39.37;\n";
            code += "        uv" + si + " = vec2(dot(q_pos" + si + ", vec3(" +
                    ftos(s->tcGenVecS[0]) + ", " + ftos(s->tcGenVecS[1]) + ", " + ftos(s->tcGenVecS[2]) +
                    ")), dot(q_pos" + si + ", vec3(" +
                    ftos(s->tcGenVecT[0]) + ", " + ftos(s->tcGenVecT[1]) + ", " + ftos(s->tcGenVecT[2]) +
                    ")));\n";
            code += "    }\n";
            break;
        case STAGE_TCGEN_BASE:
        default:
            code += "    vec2 uv" + si + " = UV;\n";
            break;
    }

    /* Apply tcMod transformations in order */
    for (int t = 0; t < s->tcModCount; t++) {
        const MohaaStageTcMod *tm = &s->tcMods[t];
        switch (tm->type) {
            case TCMOD_SCROLL: {
                std::string s_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_S)
                    ? "entity_tcmod_scroll.x"
                    : ftos(tm->params[0]);
                std::string t_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_T)
                    ? "entity_tcmod_scroll.y"
                    : ftos(tm->params[1]);
                code += "    uv" + si + " += vec2(" + s_expr + ", " + t_expr + ") * TIME;\n";
                break;
            }
            case TCMOD_ROTATE: {
                code += "    {\n";
                std::string speed_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_ROT_SPEED)
                    ? "entity_tcmod_rotate_speed"
                    : ftos(tm->params[0]);
                std::string start_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_ROT_START)
                    ? "entity_tcmod_rotate_start"
                    : ftos(tm->params[1]);
                code += "        float rot_angle" + si + " = radians(-(" + speed_expr + " * " + ftos(tm->params[2]) + " * TIME + " +
                        start_expr + "));\n";
                code += "        float rc" + si + " = cos(rot_angle" + si + ");\n";
                code += "        float rs" + si + " = sin(rot_angle" + si + ");\n";
                code += "        vec2 rot_center" + si + " = vec2(0.5, 0.5);\n";
                code += "        uv" + si + " -= rot_center" + si + ";\n";
                code += "        uv" + si + " = vec2(uv" + si + ".x * rc" + si + " - uv" + si + ".y * rs" + si +
                        ", uv" + si + ".x * rs" + si + " + uv" + si + ".y * rc" + si + ");\n";
                code += "        uv" + si + " += rot_center" + si + ";\n";
                code += "    }\n";
                break;
            }
            case TCMOD_SCALE:
                code += "    uv" + si + " *= vec2(" + ftos(tm->params[0]) + ", " +
                        ftos(tm->params[1]) + ");\n";
                break;
            case TCMOD_TURB: {
                /* turb: params[0]=base, params[1]=amp, params[2]=phase, params[3]=freq
                 * OpenMOHAA parity: turbulence uses vertex position for spatial offset.
                 * Formula: uv += amp * sin((pos.xz + pos.y) * scale + (phase + time*freq) * 2PI)
                 * In idTech3 coordinates: S maps to X+Z, T maps to Y
                 * In Godot coordinates: Quake X is -Z, Quake Z is Y, Quake Y is -X
                 * And Godot local pos is scaled down by 39.37 relative to Quake space. */
                std::string now_expr = "(" + ftos(tm->params[2]) + " + TIME * " + ftos(tm->params[3]) + ") * 6.283185";
                code += "    uv" + si + " += " + ftos(tm->params[1]) + " * sin(vec2(-v_pos.z + v_pos.y, -v_pos.x) * (39.37 * 6.283185 / 1024.0) + vec2(" + now_expr + "));\n";
                break;
            }
            case TCMOD_STRETCH: {
                std::string stretch_val = wave_call(&tm->wave, "TIME");
                code += "    {\n";
                code += "        float stretch" + si + " = " + stretch_val + ";\n";
                code += "        if (abs(stretch" + si + ") > 0.0001) {\n";
                code += "            float inv_s" + si + " = 1.0 / stretch" + si + ";\n";
                code += "            uv" + si + " = (uv" + si + " - vec2(0.5)) * inv_s" + si + " + vec2(0.5);\n";
                code += "        }\n";
                code += "    }\n";
                break;
            }
            case TCMOD_OFFSET:
            {
                std::string s_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_S)
                    ? "entity_tcmod_offset.x"
                    : ftos(tm->params[0]);
                std::string t_expr = (tm->flags & TCMOD_FLAG_FROMENTITY_T)
                    ? "entity_tcmod_offset.y"
                    : ftos(tm->params[1]);
                code += "    uv" + si + " += vec2(" + s_expr + ", " + t_expr + ");\n";
                break;
            }
            case TCMOD_WAVETRANS: {
                std::string wv = wave_call(&tm->wave, "TIME");
                code += "    uv" + si + ".x += " + wv + ";\n";
                break;
            }
            case TCMOD_WAVETRANT: {
                std::string wv = wave_call(&tm->wave, "TIME");
                code += "    uv" + si + ".y += " + wv + ";\n";
                break;
            }
            case TCMOD_BULGE:
                /* OpenMOHAA parity: tr_shade_calc.c RB_CalcBulgeTexCoords calculates
                 * an offset but never applies it to 'st'. Bulge is a no-op for tex mapping. */
                break;
            case TCMOD_TRANSFORM:
                code += "    uv" + si + " = mat2(" + ftos(tm->params[0]) + ", " + ftos(tm->params[1]) + ", " +
                        ftos(tm->params[2]) + ", " + ftos(tm->params[3]) + ") * uv" + si +
                        " + vec2(" + ftos(tm->params[4]) + ", " + ftos(tm->params[5]) + ");\n";
                break;
            case TCMOD_ENTITY_TRANSLATE:
                code += "    uv" + si + " += entity_tcmod_translate;\n";
                break;
            case TCMOD_PARALLAX:
                /* Parallax: offsetS = tr.refdef.vieworg[0] * rateS, offsetT = vieworg[1] * rateT
                 * vieworg is Quake World Space Camera Position.
                 * Godot World Camera Position is INV_VIEW_MATRIX[3].xyz
                 * Quake X = -Godot Z * 39.37. Quake Y = -Godot X * 39.37 */
                code += "    uv" + si + " += vec2(-INV_VIEW_MATRIX[3].z, -INV_VIEW_MATRIX[3].x) * 39.37 * vec2(" + ftos(tm->params[0]) + ", " +
                        ftos(tm->params[1]) + ");\n";
                break;
            case TCMOD_MACRO:
                code += "    uv" + si + " *= vec2(" + ftos(tm->params[0]) + ", " +
                        ftos(tm->params[1]) + ");\n";
                break;
            default:
                break;
        }
    }

    return code;
}

/* Generate GLSL code for sampling a stage's texture (handling animMap) */
static std::string gen_sample_code(int stage_idx, const MohaaShaderStage *s) {
    std::string si = std::to_string(stage_idx);
    std::string code;

    if (s->animMapFrameCount > 0 && s->animMapFreq > 0.0f) {
        /* animMap: cycle through frames based on TIME.
         * Uses if/else chain bounded by MOHAA_SHADER_STAGE_MAX_ANIM_FRAMES (8). */
        int fc = s->animMapFrameCount;
        float period = (float)fc / s->animMapFreq;
        code += "    float anim_t" + si + " = mod(TIME, " + ftos(period) + ");\n";
        code += "    int anim_frame" + si + " = int(anim_t" + si + " * " + ftos(s->animMapFreq) + ");\n";
        code += "    vec4 s" + si + ";\n";
        for (int f = 0; f < fc; f++) {
            if (f == 0)
                code += "    if (anim_frame" + si + " == 0) s" + si + " = texture(stage" + si + "_frame0, uv" + si + ");\n";
            else if (f < fc - 1)
                code += "    else if (anim_frame" + si + " == " + std::to_string(f) + ") s" + si +
                        " = texture(stage" + si + "_frame" + std::to_string(f) + ", uv" + si + ");\n";
            else
                code += "    else s" + si + " = texture(stage" + si + "_frame" + std::to_string(f) + ", uv" + si + ");\n";
        }
    } else {
        /* Simple single texture sample */
        code += "    vec4 s" + si + " = texture(stage" + si + "_tex, uv" + si + ");\n";
    }

    return code;
}

/* Generate GLSL code for rgbGen modulation of a stage */
static std::string gen_rgbgen_code(int stage_idx, const MohaaShaderStage *s) {
    std::string si = std::to_string(stage_idx);
    std::string code;

    switch (s->rgbGen) {
        case STAGE_RGBGEN_IDENTITY:
        case STAGE_RGBGEN_IDENTITY_LIGHTING:
            /* No modulation — use texture RGB as-is */
            break;
        case STAGE_RGBGEN_VERTEX:
            code += "    s" + si + ".rgb *= COLOR.rgb;\n";
            break;
        case STAGE_RGBGEN_WAVE: {
            std::string wv = wave_call(&s->rgbWave, "TIME");
            code += "    s" + si + ".rgb *= clamp(" + wv + ", 0.0, 1.0);\n";
            break;
        }
        case STAGE_RGBGEN_ENTITY:
            code += "    s" + si + ".rgb *= entity_color.rgb;\n";
            break;
        case STAGE_RGBGEN_ONE_MINUS_ENTITY:
            code += "    s" + si + ".rgb *= (vec3(1.0) - entity_color.rgb);\n";
            break;
        case STAGE_RGBGEN_LIGHTING_DIFFUSE:
            /* In Godot, diffuse lighting is handled by the spatial shader when
               not unshaded; we multiply by vertex color as a lighting proxy */
            code += "    s" + si + ".rgb *= COLOR.rgb;\n";
            break;
        case STAGE_RGBGEN_CONST:
            code += "    s" + si + ".rgb *= vec3(" + ftos(s->rgbConst[0]) + ", " +
                    ftos(s->rgbConst[1]) + ", " + ftos(s->rgbConst[2]) + ");\n";
            break;
        case STAGE_RGBGEN_GLOBAL_COLOR:
            /* Global colour from SetColor() — exposed as uniform for runtime use */
            code += "    s" + si + ".rgb *= global_color.rgb;\n";
            break;
        case STAGE_RGBGEN_SCOORD:
            /* Use S texture coordinate as RGB */
            code += "    s" + si + ".rgb *= vec3(UV.x);\n";
            break;
        case STAGE_RGBGEN_TCOORD:
            /* Use T texture coordinate as RGB */
            code += "    s" + si + ".rgb *= vec3(UV.y);\n";
            break;
        case STAGE_RGBGEN_DOT:
            /* dot(normal, view)² — Fresnel-like effect */
            code += "    s" + si + ".rgb *= vec3(pow(max(dot(NORMAL, VIEW), 0.0), 2.0));\n";
            break;
        case STAGE_RGBGEN_ONE_MINUS_DOT:
            /* 1 - dot(normal, view)² — inverse Fresnel */
            code += "    s" + si + ".rgb *= vec3(1.0 - pow(max(dot(NORMAL, VIEW), 0.0), 2.0));\n";
            break;
    }

    return code;
}

/* Generate GLSL code for alphaGen modulation of a stage */
static std::string gen_alphagen_code(int stage_idx, const MohaaShaderStage *s) {
    std::string si = std::to_string(stage_idx);
    std::string code;

    switch (s->alphaGen) {
        case STAGE_ALPHAGEN_IDENTITY:
            /* No modulation — use texture alpha as-is */
            break;
        case STAGE_ALPHAGEN_VERTEX:
            code += "    s" + si + ".a *= COLOR.a;\n";
            break;
        case STAGE_ALPHAGEN_WAVE: {
            std::string wv = wave_call(&s->alphaWave, "TIME");
            code += "    s" + si + ".a *= clamp(" + wv + ", 0.0, 1.0);\n";
            break;
        }
        case STAGE_ALPHAGEN_ENTITY:
            code += "    s" + si + ".a *= entity_color.a;\n";
            break;
        case STAGE_ALPHAGEN_ONE_MINUS_ENTITY:
            code += "    s" + si + ".a *= (1.0 - entity_color.a);\n";
            break;
        case STAGE_ALPHAGEN_PORTAL:
            /* Portal alpha fades with distance — use linear depth approximation */
            code += "    s" + si + ".a *= clamp(length(VERTEX) / " + ftos(s->alphaPortalDist) + ", 0.0, 1.0);\n";
            break;
        case STAGE_ALPHAGEN_CONST:
            code += "    s" + si + ".a *= " + ftos(s->alphaConst) + ";\n";
            break;
        case STAGE_ALPHAGEN_GLOBAL_ALPHA:
            /* Global alpha from SetColor() — exposed as uniform for runtime use */
            code += "    s" + si + ".a *= global_color.a;\n";
            break;
        case STAGE_ALPHAGEN_DOT: {
            /* dot(normal, view)² scaled by [alphaMin, alphaMax] */
            std::string amin = ftos(s->alphaMin);
            std::string amax = ftos(s->alphaMax);
            code += "    { float _dot" + si + " = pow(max(dot(NORMAL, VIEW), 0.0), 2.0);\n";
            code += "      s" + si + ".a *= clamp((" + amax + " - " + amin + ") * _dot" + si + " + " + amin + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_ONE_MINUS_DOT: {
            /* 1 - dot(normal, view)² scaled by [alphaMin, alphaMax] */
            std::string amin = ftos(s->alphaMin);
            std::string amax = ftos(s->alphaMax);
            code += "    { float _dot" + si + " = 1.0 - pow(max(dot(NORMAL, VIEW), 0.0), 2.0);\n";
            code += "      s" + si + ".a *= clamp((" + amax + " - " + amin + ") * _dot" + si + " + " + amin + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_DOT_VIEW: {
            /* abs(dot(viewForward, normal)) scaled by [alphaMin, alphaMax]
             * viewForward = INV_VIEW_MATRIX[2].xyz in Godot */
            std::string amin = ftos(s->alphaMin);
            std::string amax = ftos(s->alphaMax);
            code += "    { float _dotv" + si + " = abs(dot(normalize(VIEW), NORMAL));\n";
            code += "      s" + si + ".a *= clamp((" + amax + " - " + amin + ") * _dotv" + si + " + " + amin + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_ONE_MINUS_DOT_VIEW: {
            std::string amin = ftos(s->alphaMin);
            std::string amax = ftos(s->alphaMax);
            code += "    { float _dotv" + si + " = 1.0 - abs(dot(normalize(VIEW), NORMAL));\n";
            code += "      s" + si + ".a *= clamp((" + amax + " - " + amin + ") * _dotv" + si + " + " + amin + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_DIST_FADE:
        case STAGE_ALPHAGEN_TIKI_DIST_FADE: {
            /* Distance fade: alpha = clamp((dist - near) / range, 0, 1)
             * alphaMin = near distance, alphaMax = range */
            std::string near_d = ftos(s->alphaMin);
            std::string range = ftos(s->alphaMax > 0.0f ? s->alphaMax : 1.0f);
            code += "    { float _ddist" + si + " = length(VERTEX);\n";
            code += "      s" + si + ".a *= clamp((_ddist" + si + " - " + near_d + ") / " + range + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_ONE_MINUS_DIST_FADE:
        case STAGE_ALPHAGEN_ONE_MINUS_TIKI_DIST_FADE: {
            std::string near_d = ftos(s->alphaMin);
            std::string range = ftos(s->alphaMax > 0.0f ? s->alphaMax : 1.0f);
            code += "    { float _ddist" + si + " = length(VERTEX);\n";
            code += "      s" + si + ".a *= 1.0 - clamp((_ddist" + si + " - " + near_d + ") / " + range + ", 0.0, 1.0); }\n";
            break;
        }
        case STAGE_ALPHAGEN_HEIGHT_FADE: {
            /* Height-based fade using Z distance: alpha = 1 - (dist - min) / (max - min)
             * In Godot view space, VERTEX.y is the vertical component */
            std::string amin = ftos(s->alphaMin);
            std::string amax = ftos(s->alphaMax > s->alphaMin ? s->alphaMax : s->alphaMin + 1.0f);
            code += "    { float _hdist" + si + " = abs((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).y * 39.37 - " +
                    "(INV_VIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).y * 39.37);\n";
            code += "      _hdist" + si + " = clamp(_hdist" + si + ", " + amin + ", " + amax + ");\n";
            code += "      s" + si + ".a *= 1.0 - (_hdist" + si + " - " + amin + ") / (" + amax + " - " + amin + "); }\n";
            break;
        }
        case STAGE_ALPHAGEN_SKYALPHA:
            code += "    s" + si + ".a *= sky_alpha;\n";
            break;
        case STAGE_ALPHAGEN_ONE_MINUS_SKYALPHA:
            code += "    s" + si + ".a *= (1.0 - sky_alpha);\n";
            break;
        case STAGE_ALPHAGEN_SCOORD:
            code += "    s" + si + ".a *= UV.x;\n";
            break;
        case STAGE_ALPHAGEN_TCOORD:
            code += "    s" + si + ".a *= UV.y;\n";
            break;
        case STAGE_ALPHAGEN_LIGHTING_SPECULAR: {
            /* Specular: (reflect(-lightDir, normal) . view)⁴
             * Use a simple approximation with the camera direction */
            code += "    { vec3 _refl" + si + " = reflect(-normalize(VIEW), NORMAL);\n";
            code += "      float _spec" + si + " = pow(max(dot(_refl" + si + ", normalize(VIEW)), 0.0), 4.0);\n";
            code += "      s" + si + ".a *= _spec" + si + "; }\n";
            break;
        }
    }

    return code;
}

/* Generate GLSL compositing code for blending stage onto accumulated result */
static std::string gen_blend_code(int stage_idx, const MohaaShaderStage *s) {
    std::string si = std::to_string(stage_idx);

    if (stage_idx == 0 && !s->hasBlendFunc) {
        /* First stage with no blendFunc: replace */
        return "    vec4 result = s0;\n";
    }

    if (!s->hasBlendFunc) {
        /* No blendFunc — replace (unusual for non-first stages but handle it) */
        return "    result = s" + si + ";\n";
    }

    /* Detect common blend patterns for cleaner code */
    /* GL_ONE GL_ONE → screen blend (prevents clipping to white).
     * In multi-pass GL, this pass ran additively over the already-blended
     * framebuffer.  In single-pass GLSL we have no access to the
     * framebuffer backdrop, so straight += doubles bright textures to
     * white.  Screen blend (a + b - a*b) is a non-clipping equivalent. */
    if (s->blendSrc == BLEND_ONE && s->blendDst == BLEND_ONE) {
        return "    result.rgb = result.rgb + s" + si + ".rgb - result.rgb * s" + si + ".rgb;\n"
               "    result.a = clamp(result.a + s" + si + ".a, 0.0, 1.0);\n";
    }
    /* GL_DST_COLOR GL_ZERO → modulate */
    if (s->blendSrc == BLEND_DST_COLOR && s->blendDst == BLEND_ZERO) {
        return "    result.rgb *= s" + si + ".rgb;\n";
    }
    /* GL_ZERO GL_SRC_COLOR → modulate (alt) */
    if (s->blendSrc == BLEND_ZERO && s->blendDst == BLEND_SRC_COLOR) {
        return "    result.rgb *= s" + si + ".rgb;\n";
    }
    /* GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA → alpha blend */
    if (s->blendSrc == BLEND_SRC_ALPHA && s->blendDst == BLEND_ONE_MINUS_SRC_ALPHA) {
        return "    result.rgb = mix(result.rgb, s" + si + ".rgb, s" + si + ".a);\n"
               "    result.a = mix(result.a, 1.0, s" + si + ".a);\n";
    }
    /* GL_SRC_ALPHA GL_ONE → additive with alpha */
    if (s->blendSrc == BLEND_SRC_ALPHA && s->blendDst == BLEND_ONE) {
        return "    result.rgb += s" + si + ".rgb * s" + si + ".a;\n";
    }
    /* GL_ONE GL_ZERO → replace */
    if (s->blendSrc == BLEND_ONE && s->blendDst == BLEND_ZERO) {
        return "    result = s" + si + ";\n";
    }

    /* Generic blend — generate full src*factor + dst*factor code */
    auto factor_code = [&](MohaaBlendFactor f, const std::string &src, const std::string &dst) -> std::string {
        switch (f) {
            case BLEND_ONE:                    return "vec4(1.0)";
            case BLEND_ZERO:                   return "vec4(0.0)";
            case BLEND_SRC_ALPHA:              return "vec4(" + src + ".a)";
            case BLEND_ONE_MINUS_SRC_ALPHA:    return "vec4(1.0 - " + src + ".a)";
            case BLEND_DST_COLOR:              return dst;
            case BLEND_SRC_COLOR:              return src;
            case BLEND_ONE_MINUS_DST_COLOR:    return "(vec4(1.0) - " + dst + ")";
            case BLEND_ONE_MINUS_SRC_COLOR:    return "(vec4(1.0) - " + src + ")";
            case BLEND_DST_ALPHA:              return "vec4(" + dst + ".a)";
            case BLEND_ONE_MINUS_DST_ALPHA:    return "vec4(1.0 - " + dst + ".a)";
            default:                           return "vec4(1.0)";
        }
    };

    std::string src_name = "s" + si;
    std::string sf = factor_code(s->blendSrc, src_name, "result");
    std::string df = factor_code(s->blendDst, src_name, "result");

    return "    result = " + src_name + " * " + sf + " + result * " + df + ";\n";
}

/* ===================================================================
 *  Main shader code generation
 * ================================================================ */

String Godot_Shader_GenerateCode(const GodotShaderProps *props) {
    if (!props || props->stage_count <= 0)
        return String();

    std::string code;

    /* Shader type and render mode */
    code += "shader_type spatial;\n";

    /* Build render_mode */
    /* ambient_light_disabled replaces the old render_mode unshaded.  The
     * custom light() function below reproduces unshaded behaviour at
     * bsp_shadow_darkness = 0.0 (DIFFUSE_LIGHT += 1.0 regardless of
     * ATTENUATION), and enables real shadow reception at darkness > 0.
     * Using ambient_light_disabled prevents the scene ambient from
     * double-lighting the lightmap-composited ALBEDO. */
    std::string render_mode;
    render_mode += "ambient_light_disabled";

    switch (props->transparency) {
        case SHADER_ADDITIVE:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "blend_add";
            break;
        case SHADER_MULTIPLICATIVE:
        case SHADER_MULTIPLICATIVE_INV:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "blend_mul";
            break;
        case SHADER_ALPHA_BLEND:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "blend_mix";
            break;
        case SHADER_ALPHA_TEST:
            /* Alpha-test: opaque pass with per-stage discard() in fragment shader.
             * Do NOT use blend_mix here — that puts the surface in the transparent
             * pass which skips depth writes.  MOHAA's original GL renderer runs
             * alpha-test geometry as opaque (GLS_ATEST_*) with depth writes ON.
             * The per-stage 'discard' calls generated below handle the cutout. */
            break;
        default:
            break;
    }

    /* OpenMoHAA enables depth writes by default for the first pass of every
     * shader, including transparent ones.  Godot's blend_mix/add/mul puts
     * surfaces in the transparent pass which skips depth writes, allowing
     * geometry behind (e.g. terrain) to show through solid walls/floors.
     * Fix: always write depth unless the shader explicitly says nodepthwrite. */
    if (props->transparency != SHADER_OPAQUE
        && props->transparency != SHADER_ALPHA_TEST) {
        bool no_depth = false;
        for (int si = 0; si < props->stage_count; si++) {
            if (props->stages[si].active
                && props->stages[si].depthWriteExplicit
                && !props->stages[si].depthWriteEnabled) {
                no_depth = true;
                break;
            }
        }
        if (!no_depth) {
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "depth_draw_always";
        }
    }

    switch (props->cull) {
        case SHADER_CULL_NONE:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "cull_disabled";
            break;
        case SHADER_CULL_FRONT:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "cull_front";
            break;
        default:
            break;
    }

    if (!render_mode.empty())
        code += "render_mode " + render_mode + ";\n";
    code += "\n";

    /* Uniforms */
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        if (s->animMapFrameCount > 0) {
            for (int f = 0; f < s->animMapFrameCount; f++) {
                code += "uniform sampler2D stage" + std::to_string(i) + "_frame" + std::to_string(f);
                if (s->isClampMap) code += " : repeat_disable";
                code += ";\n";
            }
        } else {
            code += "uniform sampler2D stage" + std::to_string(i) + "_tex";
            if (s->isClampMap) code += " : repeat_disable";
            code += ";\n";
            /* nextBundle $lightmap: emit a separate lightmap sampler */
            if (s->hasNextBundleLightmap) {
                code += "uniform sampler2D stage" + std::to_string(i) + "_lm;\n";
            }
        }
    }

    /* Overbright factor for lightmap compositing */
    bool has_lightmap = false;
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].isLightmap || props->stages[i].hasNextBundleLightmap) {
            has_lightmap = true; break;
        }
    }
    if (has_lightmap)
        /* Overbright is already baked into lightmap textures (<<1 in
         * load_lightmaps, matching R_ColorShiftLightingBytes).  No
         * additional multiply needed — factor = 1.0. */
        code += "uniform float overbright_factor = 1.0;\n";

    if (needs_entity_color(props))
        code += "uniform vec4 entity_color = vec4(1.0, 1.0, 1.0, 1.0);\n";

    if (needs_global_color(props))
        code += "uniform vec4 global_color = vec4(1.0, 1.0, 1.0, 1.0);\n";

    if (needs_sky_alpha(props))
        code += "uniform float sky_alpha = 1.0;\n";

    /* Shadow reception uniform.  Default 0.0 = same result as render_mode unshaded.
     * Set to 0.5 from MoHAARunner when r_shadows >= 1 to darken BSP floors in shadow. */
    code += "uniform float bsp_shadow_darkness = 0.0;\n";

    if (needs_tcmod(props)) {
        code += "uniform vec2 entity_tcmod_scroll = vec2(0.0, 0.0);\n";
        code += "uniform vec2 entity_tcmod_offset = vec2(0.0, 0.0);\n";
        code += "uniform float entity_tcmod_rotate_speed = 0.0;\n";
        code += "uniform float entity_tcmod_rotate_start = 0.0;\n";
        code += "uniform vec2 entity_tcmod_translate = vec2(0.0, 0.0);\n";
    }

    if (needs_v_pos(props)) {
        code += "varying vec3 v_pos;\n";
    }

    code += "\n";

    /* Wave function definitions (if needed) */
    if (needs_wave_functions(props))
        emit_wave_functions(code);

    /* Vertex shader — injected when deformVertexes is present */
    if (props->has_deform) {
        String deform_glsl = Godot_Deform_GenerateVertexShader(
            props->deform_type, props->deform_div, props->deform_base,
            props->deform_amplitude, props->deform_frequency, props->deform_phase);
        if (!deform_glsl.is_empty()) {
            code += "void vertex() {\n";
            code += std::string(deform_glsl.utf8().get_data());
            if (needs_v_pos(props)) {
                code += "    v_pos = VERTEX;\n";
            }
            code += "}\n\n";
        }
    } else if (needs_v_pos(props)) {
        code += "void vertex() {\n";
        code += "    v_pos = VERTEX;\n";
        code += "}\n\n";
    }

    /* Fragment shader */
    code += "void fragment() {\n";

    /* Per-stage UV computation, texture sampling, and color modulation */
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        std::string si = std::to_string(i);

        code += "    // Stage " + si + "\n";
        code += gen_uv_code(i, s);
        code += gen_sample_code(i, s);
        code += gen_rgbgen_code(i, s);
        code += gen_alphagen_code(i, s);

        /* Alpha test per stage */
        if (s->hasAlphaFunc) {
            code += "    if (s" + si + ".a < " + ftos(s->alphaFuncThreshold) + ") discard;\n";
        }
    }

    /* Compositing */
    code += "\n    // Compositing\n";
    bool have_result = false;
    for (int i = 0; i < props->stage_count; i++) {
        const MohaaShaderStage *s = &props->stages[i];
        if (!s->active) continue;
        std::string si = std::to_string(i);

        if (!have_result) {
            /* First active stage always provides the base colour.
             * Its blendFunc (if any) controls how the FINAL surface
             * blends with the framebuffer (handled by render_mode),
             * NOT internal stage compositing.  Starting from vec4(0.0)
             * was wrong: filter blending (DST_COLOR * ZERO) produced
             * all-black, and alpha-blend double-applied the alpha. */
            code += "    vec4 result = s" + si + ";\n";
            /* nextBundle $lightmap: multiply diffuse by lightmap from UV2 */
            if (s->hasNextBundleLightmap) {
                code += "    result.rgb *= texture(stage" + si + "_lm, UV2).rgb * overbright_factor;\n";
            }
            have_result = true;
        } else {
            /* Lightmap modulation special case */
            if (s->isLightmap && s->blendSrc == BLEND_DST_COLOR && s->blendDst == BLEND_ZERO) {
                code += "    result.rgb *= s" + si + ".rgb * overbright_factor;\n";
            } else {
                code += gen_blend_code(i, s);
            }
        }
    }

    if (!have_result) {
        code += "    vec4 result = vec4(0.0);\n";
    }

    /* Output — gamma-to-linear conversion (Forward+ / Mobile only).
     *
     * MOHAA's original renderer works entirely in gamma space: textures
     * are gamma-encoded, lightmaps are gamma-encoded, and the framebuffer
     * output is gamma-encoded.  All compositing (blend, mul, add) happens
     * in gamma space.
     *
     * Godot Forward+ works in linear space internally and applies a
     * linear→sRGB conversion at output.  Our samplers intentionally lack
     * the `source_color` hint so textures are read as raw gamma values,
     * preserving MOHAA's gamma-space compositing math.  Without this
     * correction the output chain would be:
     *     display = sRGB_encode(gamma_tex × gamma_lm)
     * which brightens mid-tones (~47% at value 0.5).
     *
     * By converting the gamma-space result to linear here, the output
     * chain becomes:
     *     display = sRGB_encode(pow(gamma_result, 2.2)) ≈ gamma_result
     * which matches the original MOHAA output.
     *
     * The GL Compatibility renderer (WebGL2) handles colour space
     * differently — the pow(2.2) double-darkens the image.  We skip
     * the conversion on that backend. */
    if (needs_gamma_to_linear_conversion()) {
        code += "\n    // Gamma→linear: compensate for Godot's sRGB output encode\n";
        code += "    result.rgb = pow(max(result.rgb, vec3(0.0)), vec3(2.2));\n";
    }

    /* EMISSION carries the always-visible lightmap contribution.  This ensures
     * the BSP looks identical to render_mode unshaded when bsp_shadow_darkness = 0.0
     * (i.e. r_shadows 0) regardless of whether any Godot lights are active.
     *
     * With ambient_light_disabled the output formula is:
     *   color = EMISSION + ALBEDO * DIFFUSE_LIGHT
     *         = result * (1-dark) + result * (dark * ATTENUATION)
     * In full light  (atten=1): (1-d) + d = 1.0  → no change
     * In full shadow (atten=0): (1-d) + 0 = 1-d  → darkened
     * With no active light (mode 0, dark=0): 1.0 + 0    = 1.0  → unshaded */
    code += "    EMISSION = result.rgb * (1.0 - bsp_shadow_darkness);\n";
    code += "    ALBEDO = result.rgb;\n";
    code += "    METALLIC = 0.0;\n";
    code += "    ROUGHNESS = 1.0;\n";

    if (props->transparency == SHADER_ALPHA_BLEND ||
        props->transparency == SHADER_ADDITIVE) {
        code += "    ALPHA = result.a;\n";
    }
    /* SHADER_ALPHA_TEST: do not set ALPHA — the per-stage discard() calls
     * already cut out transparent pixels in the opaque pass.  Setting ALPHA
     * here would be meaningless without blend_mix and could confuse the
     * renderer's depth pre-pass.  No ALPHA_SCISSOR_THRESHOLD needed either
     * since we rely on explicit GLSL discard. */

    code += "}\n";

    /* Shadow reception light() function.
     * DIFFUSE_LIGHT += dark * ATTENUATION
     *   ATTENUATION = 1.0 in full light, 0.0 in full shadow.
     * Combined with EMISSION = result * (1-dark) in fragment():
     *   total = result*(1-dark) + result*(dark*ATTENUATION)
     * → no change in full light; darkens proportionally in shadow.
     * OmniLights (muzzle flashes, explosions) are intentionally ignored —
     * BSP uses baked lightmaps and should not be affected by dynamic lights. */
    code += "\nvoid light() {\n";
    code += "    if (LIGHT_IS_DIRECTIONAL) {\n";
    code += "        DIFFUSE_LIGHT += bsp_shadow_darkness * ATTENUATION;\n";
    code += "    }\n";
    code += "}\n";

    return String(code.c_str());
}

/* ===================================================================
 *  Material builder
 * ================================================================ */

Ref<ShaderMaterial> Godot_Shader_BuildMaterial(const GodotShaderProps *props) {
    if (!props || props->stage_count <= 0)
        return Ref<ShaderMaterial>();

    /* Check cache */
    std::string key = make_cache_key(props);
    Ref<Shader> shader;

    auto it = s_shader_cache->find(key);
    if (it != s_shader_cache->end() && it->second.is_valid()) {
        shader = it->second;
    } else {
        /* Generate shader code */
        String code = Godot_Shader_GenerateCode(props);
        if (code.is_empty())
            return Ref<ShaderMaterial>();

        shader.instantiate();
        shader->set_code(code);
        (*s_shader_cache)[key] = shader;
    }

    /* Create material */
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    mat->set_shader(shader);

    /* Register for runtime bsp_shadow_darkness updates */
    s_mat_registry->push_back(mat);

    return mat;
}

void Godot_Shader_ClearCache() {
    s_shader_cache->clear();
}

/* Clear the material registry.  Call at the start of every map load
 * to drop stale references from the previous map. */
void Godot_Shader_ClearMaterialRegistry() {
    s_mat_registry->clear();
}

/* Set the shadow darkness on every registered ShaderMaterial.
 * darkness = 0.0 → identical to the old render_mode unshaded (no shadow).
 * darkness = 0.5 → fully-shadowed fragments are rendered at 50 % of their
 *                  lightmap colour; fully-lit fragments are at 100 %.
 * Called from MoHAARunner::apply_player_shadow_mode() whenever r_shadows changes
 * and once after each BSP load to synchronise freshly-built materials. */
void Godot_Shader_SetShadowDarkness(float darkness) {
    for (auto &mat : *s_mat_registry) {
        if (mat.is_valid()) {
            mat->set_shader_parameter("bsp_shadow_darkness", darkness);
        }
    }
}
