/* godot_shader_material.cpp — Multi-stage shader → Godot ShaderMaterial builder
 *
 * Phases 66–68, 71–72: Generates .gdshader code from parsed MOHAA
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

using namespace godot;

/* ===================================================================
 *  Internal shader cache — avoids regenerating identical shaders
 * ================================================================ */
static std::unordered_map<std::string, Ref<Shader>> s_shader_cache;

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

/* Returns true if any stage uses lighting diffuse (needs lit shading) */
static bool needs_diffuse_lighting(const GodotShaderProps *props) {
    for (int i = 0; i < props->stage_count; i++) {
        if (!props->stages[i].active) continue;
        if (props->stages[i].rgbGen == STAGE_RGBGEN_LIGHTING_DIFFUSE)
            return true;
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
            code += "    vec2 uv" + si + " = vec2(dot(VERTEX, vec3(" +
                    ftos(s->tcGenVecS[0]) + ", " + ftos(s->tcGenVecS[1]) + ", " + ftos(s->tcGenVecS[2]) +
                    ")), dot(VERTEX, vec3(" +
                    ftos(s->tcGenVecT[0]) + ", " + ftos(s->tcGenVecT[1]) + ", " + ftos(s->tcGenVecT[2]) +
                    ")));\n";
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
                code += "        float rot_angle" + si + " = radians((" + speed_expr + " * TIME + " +
                        start_expr + ") * " + ftos(tm->params[2]) + ");\n";
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
            case TCMOD_TURB:
                /* turb: params[0]=base, params[1]=amp, params[2]=phase, params[3]=freq */
                code += "    uv" + si + " += vec2(sin(uv" + si + ".y * " + ftos(tm->params[3]) +
                        " * 6.283185 + TIME * " + ftos(tm->params[3]) + ") * " + ftos(tm->params[1]) +
                        ", sin(uv" + si + ".x * " + ftos(tm->params[3]) +
                        " * 6.283185 + TIME * " + ftos(tm->params[3]) + ") * " + ftos(tm->params[1]) + ");\n";
                break;
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
                code += "    uv" + si + " += vec2(sin((uv" + si + ".y * " + ftos(tm->wave.frequency) +
                        " + TIME + " + ftos(tm->wave.phase) + ") * 6.283185), "
                        "sin((uv" + si + ".x * " + ftos(tm->wave.frequency) +
                        " + TIME + " + ftos(tm->wave.phase) + ") * 6.283185)) * " +
                        ftos(tm->wave.amplitude) + " + vec2(" + ftos(tm->wave.base) + ");\n";
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
                code += "    uv" + si + " += normalize(VIEW).xy * vec2(" + ftos(tm->params[0]) + ", " +
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
    /* GL_ONE GL_ONE → additive */
    if (s->blendSrc == BLEND_ONE && s->blendDst == BLEND_ONE) {
        return "    result.rgb += s" + si + ".rgb;\n"
               "    result.a += s" + si + ".a;\n";
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
    std::string render_mode;
    if (!needs_diffuse_lighting(props))
        render_mode += "unshaded";

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
        case SHADER_ALPHA_TEST:
            if (!render_mode.empty()) render_mode += ", ";
            render_mode += "blend_mix";
            break;
        default:
            break;
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
        code += "uniform float overbright_factor = 2.0;\n";

    if (needs_entity_color(props))
        code += "uniform vec4 entity_color = vec4(1.0, 1.0, 1.0, 1.0);\n";

    if (needs_tcmod(props)) {
        code += "uniform vec2 entity_tcmod_scroll = vec2(0.0, 0.0);\n";
        code += "uniform vec2 entity_tcmod_offset = vec2(0.0, 0.0);\n";
        code += "uniform float entity_tcmod_rotate_speed = 0.0;\n";
        code += "uniform float entity_tcmod_rotate_start = 0.0;\n";
        code += "uniform vec2 entity_tcmod_translate = vec2(0.0, 0.0);\n";
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
            code += "}\n\n";
        }
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

    /* Output */
    code += "\n    ALBEDO = result.rgb;\n";

    if (props->transparency == SHADER_ALPHA_BLEND ||
        props->transparency == SHADER_ADDITIVE ||
        props->transparency == SHADER_ALPHA_TEST) {
        code += "    ALPHA = result.a;\n";
    }

    if (props->transparency == SHADER_ALPHA_TEST) {
        code += "    ALPHA_SCISSOR_THRESHOLD = " + ftos(props->alpha_threshold) + ";\n";
    }

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

    auto it = s_shader_cache.find(key);
    if (it != s_shader_cache.end() && it->second.is_valid()) {
        shader = it->second;
    } else {
        /* Generate shader code */
        String code = Godot_Shader_GenerateCode(props);
        if (code.is_empty())
            return Ref<ShaderMaterial>();

        shader.instantiate();
        shader->set_code(code);
        s_shader_cache[key] = shader;
    }

    /* Create material */
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    mat->set_shader(shader);

    return mat;
}

void Godot_Shader_ClearCache() {
    s_shader_cache.clear();
}
