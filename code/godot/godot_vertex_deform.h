/*
 * godot_vertex_deform.h — Vertex deformation GLSL code generators.
 *
 * Generates Godot shader vertex code snippets for id Tech 3 style
 * deformVertexes directives: autosprite, autosprite2, wave, bulge,
 * and move.  The generated GLSL is intended to be injected into
 * Godot ShaderMaterial vertex() functions.
 *
 * Reads deform parameters from GodotShaderProps (owned by Agent 2).
 * Called by the material builder to produce correct vertex shaders.
 */

#ifndef GODOT_VERTEX_DEFORM_H
#define GODOT_VERTEX_DEFORM_H

/* ── Deform type constants (match GodotShaderProps::deform_type) ── */
#define DEFORM_WAVE        0
#define DEFORM_BULGE       1
#define DEFORM_MOVE        2
#define DEFORM_AUTOSPRITE  3
#define DEFORM_AUTOSPRITE2 4

#ifdef __cplusplus

#include <godot_cpp/variant/string.hpp>

/*
 * Generate a GLSL vertex shader code snippet for the given deform type.
 *
 * @param deform_type   One of DEFORM_WAVE, DEFORM_BULGE, DEFORM_MOVE,
 *                      DEFORM_AUTOSPRITE, DEFORM_AUTOSPRITE2.
 * @param div           Wave divisor (spatial frequency divider).
 * @param base          Base displacement.
 * @param amplitude     Wave amplitude.
 * @param frequency     Temporal frequency (Hz).
 * @param phase         Phase offset.
 * @return  GLSL code string for insertion into a Godot shader's
 *          vertex() function, or empty string if type is unknown.
 */
godot::String Godot_Deform_GenerateVertexShader(
    int deform_type,
    float div,
    float base,
    float amplitude,
    float frequency,
    float phase);

/*
 * Generate the full Godot shader_type + uniforms + vertex() function
 * for a deformVertexes shader, suitable for use as a complete
 * ShaderMaterial shader code string.
 *
 * @param deform_type   Deform type constant.
 * @param div, base, amplitude, frequency, phase — deform parameters.
 * @return  Complete Godot shader code string.
 */
godot::String Godot_Deform_GenerateFullShader(
    int deform_type,
    float div,
    float base,
    float amplitude,
    float frequency,
    float phase);

#endif /* __cplusplus */
#endif /* GODOT_VERTEX_DEFORM_H */
