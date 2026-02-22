/* godot_shader_material.h — Multi-stage shader → Godot ShaderMaterial builder
 *
 * Phases 66–68, 71–72: Generates Godot ShaderMaterial instances from
 * parsed MOHAA multi-stage shader definitions.  Composites multiple
 * texture stages with per-stage blendFunc, rgbGen/alphaGen wave
 * functions, tcGen environment/lightmap/vector, tcMod animations,
 * and animMap texture sequences into a single .gdshader program.
 *
 * Usage:
 *   const GodotShaderProps *sp = Godot_ShaderProps_Find("textures/foo");
 *   if (sp && sp->stage_count > 0) {
 *       Ref<ShaderMaterial> mat = Godot_Shader_BuildMaterial(sp);
 *       // Set stage textures:
 *       //   mat->set_shader_parameter("stage0_tex", my_texture);
 *       //   mat->set_shader_parameter("stage1_tex", lightmap_tex);
 *   }
 *
 * MoHAARunner Integration Required:
 *   1. Call Godot_Shader_BuildMaterial() where StandardMaterial3D is currently
 *      created (check_world_load, update_entities, update_polys, get_shader_texture).
 *   2. Animated shaders (animMap, rgbGen wave, tcMod scroll/rotate) use the
 *      built-in TIME uniform in the generated shader — no per-frame uniform
 *      update needed.  The existing update_shader_animations() can be kept for
 *      StandardMaterial3D fallback but is not needed for ShaderMaterial paths.
 *   3. Call Godot_Shader_ClearCache() on map change to release cached shaders.
 */

#ifndef GODOT_SHADER_MATERIAL_H
#define GODOT_SHADER_MATERIAL_H

#include "godot_shader_props.h"

#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/object.hpp>

using namespace godot;

/*
 * Build a Godot ShaderMaterial from parsed multi-stage shader properties.
 * Returns a configured ShaderMaterial with the generated .gdshader code.
 * The caller must set per-stage texture uniforms:
 *   "stage<N>_tex" — sampler2D for each stage's map texture
 *   "stage<N>_frame<F>" — sampler2D for animMap frames (when animMapFrameCount > 0)
 *
 * Uniform naming convention:
 *   stage0_tex, stage1_tex, ...        — primary texture per stage
 *   stage0_frame0, stage0_frame1, ...  — animMap frame textures
 *   overbright_factor                  — lightmap overbright multiplier (default 2.0)
 *   entity_color                       — vec4 for rgbGen/alphaGen entity
 *
 * Returns null Ref if props is NULL or has no stages.
 */
Ref<ShaderMaterial> Godot_Shader_BuildMaterial(const GodotShaderProps *props);

/*
 * Generate the .gdshader source code string for a multi-stage shader.
 * Useful for debugging/inspection.  Returns empty string if props is NULL.
 */
String Godot_Shader_GenerateCode(const GodotShaderProps *props);

/*
 * Clear the internal shader cache.  Call on map change.
 */
void Godot_Shader_ClearCache();

#endif /* GODOT_SHADER_MATERIAL_H */
