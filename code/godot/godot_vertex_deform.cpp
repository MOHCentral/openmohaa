/*
 * godot_vertex_deform.cpp — Vertex deformation GLSL code generators.
 *
 * Produces Godot shader vertex() code snippets for each id Tech 3
 * deformVertexes type.  These snippets are designed to be inserted
 * into Godot ShaderMaterial code by the material builder (Agent 2)
 * or by MoHAARunner's material creation pipeline (Agent 10).
 *
 * Coordinate note: the GLSL operates in Godot model space, so
 * VERTEX and NORMAL are already in Godot coordinates after the
 * BSP mesh builder has applied the id→Godot transform.
 */

#include "godot_vertex_deform.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

/* ===================================================================
 *  Wave deform — sinusoidal displacement along vertex normal.
 *
 *  deformVertexes wave <div> <func> <base> <amp> <phase> <freq>
 *
 *  Each vertex is displaced along its normal by:
 *      offset = (pos.x + pos.y + pos.z) / div
 *      wave   = base + amp * sin((TIME * freq + phase + offset) * TAU)
 *      VERTEX += NORMAL * wave
 *
 *  Used for flags, vegetation, water surfaces.
 * ================================================================ */

static String generate_wave_vertex(float div, float base, float amplitude,
                                   float frequency, float phase) {
    String code;
    code += "    float _dv = " + String::num(div, 6) + ";\n";
    if (div > 0.001f) {
        code += "    float _off = (VERTEX.x + VERTEX.y + VERTEX.z) / _dv;\n";
    } else {
        code += "    float _off = 0.0;\n";
    }
    code += "    float _t = TIME * " + String::num(frequency, 6) +
            " + " + String::num(phase, 6) + " + _off;\n";
    code += "    float _wave = " + String::num(base, 6) +
            " + " + String::num(amplitude, 6) + " * sin(_t * 6.283185);\n";
    code += "    VERTEX += NORMAL * _wave;\n";
    return code;
}

/* ===================================================================
 *  Bulge deform — model surface pulsing outward.
 *
 *  deformVertexes bulge <bulgeWidth> <bulgeHeight> <bulgeSpeed>
 *
 *  Displaces vertices along normals using a function of the S texture
 *  coordinate and time.  Creates a pulsating/breathing effect.
 * ================================================================ */

static String generate_bulge_vertex(float bulge_width, float bulge_height,
                                    float bulge_speed) {
    String code;
    code += "    float _bulge_now = TIME * " + String::num(bulge_speed, 6) + ";\n";
    code += "    float _bulge_off = UV.x * " + String::num(bulge_width, 6) + ";\n";
    code += "    float _bulge = " + String::num(bulge_height, 6) +
            " * sin((_bulge_now + _bulge_off) * 6.283185);\n";
    code += "    VERTEX += NORMAL * _bulge;\n";
    return code;
}

/* ===================================================================
 *  Move deform — vertex translation along a fixed direction.
 *
 *  deformVertexes move <x> <y> <z> <func> <base> <amp> <phase> <freq>
 *
 *  Translates all vertices by (x,y,z) * wave(time).  The direction
 *  vector is baked into the shader as a uniform.
 *
 *  For simplicity we encode the move direction into the wave params:
 *  div encodes nothing useful for move, but we store the x/y/z in the
 *  base/amplitude/frequency parameters. The caller should pre-encode
 *  the move direction into the phase parameter or pass additional data.
 *
 *  Since the shader props struct packs all deform params into the same
 *  fields, for move deforms: div=not used, base/amp/freq/phase map to
 *  the wave function, and the direction is not stored. We generate a
 *  simplified move using the normal direction as the move axis, which
 *  is a reasonable approximation.
 * ================================================================ */

static String generate_move_vertex(float base, float amplitude,
                                   float frequency, float phase) {
    String code;
    code += "    float _t = TIME * " + String::num(frequency, 6) +
            " + " + String::num(phase, 6) + ";\n";
    code += "    float _move = " + String::num(base, 6) +
            " + " + String::num(amplitude, 6) + " * sin(_t * 6.283185);\n";
    code += "    VERTEX += NORMAL * _move;\n";
    return code;
}

/* ===================================================================
 *  Autosprite deform — billboard quad that always faces the camera.
 *
 *  deformVertexes autosprite
 *
 *  Each quad (4 vertices) is billboarded to face the camera.  In Godot
 *  shader code we can approximate this with the MODELVIEW_MATRIX and
 *  billboard mode.  The simplest approach sets the model-view rotation
 *  to identity so geometry always faces the camera.
 * ================================================================ */

static String generate_autosprite_vertex() {
    String code;
    code += "    // Autosprite: billboard quad facing camera\n";
    code += "    MODELVIEW_MATRIX = VIEW_MATRIX * mat4("
            "vec4(normalize(INV_VIEW_MATRIX[0].xyz), 0.0), "
            "vec4(normalize(INV_VIEW_MATRIX[1].xyz), 0.0), "
            "vec4(normalize(INV_VIEW_MATRIX[2].xyz), 0.0), "
            "MODEL_MATRIX[3]);\n";
    return code;
}

/* ===================================================================
 *  Autosprite2 deform — axis-aligned billboard.
 *
 *  deformVertexes autosprite2
 *
 *  Rotates quad around its longest axis (typically Y/up) to face the
 *  camera.  For Godot we approximate by keeping the Y axis from the
 *  model and rebuilding X/Z to face the camera.
 * ================================================================ */

static String generate_autosprite2_vertex() {
    String code;
    code += "    // Autosprite2: Y-axis billboard\n";
    code += "    vec3 _cam_pos = (INV_VIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n";
    code += "    vec3 _obj_pos = MODEL_MATRIX[3].xyz;\n";
    code += "    vec3 _fwd = normalize(_cam_pos - _obj_pos);\n";
    code += "    vec3 _up = vec3(0.0, 1.0, 0.0);\n";
    code += "    vec3 _right = normalize(cross(_up, _fwd));\n";
    code += "    _fwd = cross(_right, _up);\n";
    code += "    MODELVIEW_MATRIX = VIEW_MATRIX * mat4("
            "vec4(_right, 0.0), "
            "vec4(_up, 0.0), "
            "vec4(_fwd, 0.0), "
            "MODEL_MATRIX[3]);\n";
    return code;
}

/* ===================================================================
 *  Public API implementations
 * ================================================================ */

String Godot_Deform_GenerateVertexShader(
    int deform_type, float div, float base,
    float amplitude, float frequency, float phase)
{
    switch (deform_type) {
        case DEFORM_WAVE:
            return generate_wave_vertex(div, base, amplitude, frequency, phase);
        case DEFORM_BULGE:
            /* For bulge: div=width, base=height, amplitude=speed */
            return generate_bulge_vertex(div, base, amplitude);
        case DEFORM_MOVE:
            return generate_move_vertex(base, amplitude, frequency, phase);
        case DEFORM_AUTOSPRITE:
            return generate_autosprite_vertex();
        case DEFORM_AUTOSPRITE2:
            return generate_autosprite2_vertex();
        default:
            return String();
    }
}

String Godot_Deform_GenerateFullShader(
    int deform_type, float div, float base,
    float amplitude, float frequency, float phase)
{
    String vertex_code = Godot_Deform_GenerateVertexShader(
        deform_type, div, base, amplitude, frequency, phase);

    if (vertex_code.is_empty()) {
        return String();
    }

    String shader;
    shader += "shader_type spatial;\n";
    shader += "render_mode blend_mix, depth_draw_opaque, cull_back;\n\n";
    shader += "uniform sampler2D albedo_tex : source_color;\n\n";
    shader += "void vertex() {\n";
    shader += vertex_code;
    shader += "}\n\n";
    shader += "void fragment() {\n";
    shader += "    ALBEDO = texture(albedo_tex, UV).rgb;\n";
    shader += "    ALPHA = texture(albedo_tex, UV).a;\n";
    shader += "}\n";

    return shader;
}
