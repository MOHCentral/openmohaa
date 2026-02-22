/*
 * godot_vfx.cpp — VFX Manager: billboard sprite pool and lifecycle.
 *
 * Reads RT_SPRITE entities from the renderer entity buffer (via the
 * C accessor in godot_vfx_accessors.c) and renders them as camera-facing
 * billboard quads in the Godot 3D scene.
 *
 * Pool of up to VFX_SPRITE_POOL_SIZE MeshInstance3D nodes.  Each frame the
 * active sprites are assigned to pool slots; unused slots are hidden.
 *
 * Phase 221: VFX Manager Foundation
 */

#include "godot_vfx.h"
#include "godot_shader_props.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>
#include <cstdint>

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

/* ── C-linkage accessors (godot_vfx_accessors.c, godot_renderer.c, godot_vfs_accessors.c) ── */
extern "C" {
    int  Godot_VFX_GetSpriteCount(void);
    void Godot_VFX_GetSprite(int idx, float *origin, float *radius,
                             int *shaderHandle, float *rotation,
                             unsigned char *rgba, float *scale);

    const char *Godot_Renderer_GetShaderName(int handle);
    const char *Godot_Renderer_GetShaderRemap(const char *name);
    const char *Godot_Model_GetName(int hModel);
    int         Godot_Model_GetShaderHandle(int hModel);

    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer);
    void Godot_VFS_FreeFile(void *buffer);

    /* Engine-side sprite dimension accessor (godot_shader_accessors.c).
     * Reads image_t->width/height and shader_t->sprite.scale from the
     * real renderer structs — same data path as SPR_RegisterSprite. */
    int  Godot_Sprite_GetEngineSize(const char *shader_name,
                                    int *out_width, int *out_height,
                                    float *out_sprite_scale);
}

/* ── Constants ── */
static constexpr int   VFX_SPRITE_POOL_SIZE = 512;
static constexpr float MOHAA_UNIT_SCALE     = 1.0f / 39.37f;

/* ── id Tech 3 → Godot coordinate conversion ── */
static inline Vector3 id_to_godot(float ix, float iy, float iz) {
    return Vector3(-iy * MOHAA_UNIT_SCALE,
                    iz * MOHAA_UNIT_SCALE,
                   -ix * MOHAA_UNIT_SCALE);
}

/* ── Module state ── */
static Node3D                              *vfx_parent     = nullptr;
static MeshInstance3D                      *vfx_pool[VFX_SPRITE_POOL_SIZE] = {};
static bool                                 vfx_initialised = false;
static std::unordered_map<int, Ref<ImageTexture>> vfx_tex_cache;
static std::unordered_set<int> vfx_logged_shaders;

/* Cached sprite size info: image pixel dimensions + shader spritescale */
struct VfxSpriteSize {
    int   width        = 0;
    int   height       = 0;
    float sprite_scale = 1.0f;
};
static std::unordered_map<int, VfxSpriteSize> vfx_size_cache;

/* Material cache keyed by (shaderHandle << 32 | rgba32) to avoid per-frame allocation */
static std::unordered_map<uint64_t, Ref<StandardMaterial3D>> vfx_mat_cache;

static inline uint64_t vfx_mat_key(int shaderHandle, const unsigned char rgba[4])
{
    uint32_t c = (uint32_t)rgba[0] | ((uint32_t)rgba[1] << 8)
               | ((uint32_t)rgba[2] << 16) | ((uint32_t)rgba[3] << 24);
    return ((uint64_t)(unsigned)shaderHandle << 32) | c;
}

/* ── Texture loading (mirrors MoHAARunner::get_shader_texture — resolves shader stage maps) ── */

static Ref<ImageTexture> vfx_load_from_qpath(const char *qpath)
{
    if (!qpath || !qpath[0]) return Ref<ImageTexture>();

    const char *extensions[] = { "", ".tga", ".jpg", ".png", nullptr };
    for (int ext_i = 0; extensions[ext_i]; ext_i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", qpath, extensions[ext_i]);

        void *buf = nullptr;
        long len = Godot_VFS_ReadFile(path, &buf);
        if (len <= 0 || !buf) {
            if (buf) Godot_VFS_FreeFile(buf);
            continue;
        }

        PackedByteArray pba;
        pba.resize(len);
        memcpy(pba.ptrw(), buf, len);
        Godot_VFS_FreeFile(buf);

        Ref<Image> img;
        img.instantiate();
        Error err;

        const uint8_t *data = pba.ptr();
        if (len > 2 && data[0] == 0xFF && data[1] == 0xD8) {
            err = img->load_jpg_from_buffer(pba);
        } else if (len > 3 && data[0] == 0x89 && data[1] == 'P') {
            err = img->load_png_from_buffer(pba);
        } else {
            err = img->load_tga_from_buffer(pba);
            if (err != OK) {
                err = img->load_jpg_from_buffer(pba);
            }
        }

        if (err == OK && !img->is_empty()) {
            img->generate_mipmaps();
            return ImageTexture::create_from_image(img);
        }
    }

    return Ref<ImageTexture>();
}

static Ref<ImageTexture> vfx_load_texture_by_name(const char *name)
{
    if (!name || !name[0]) return Ref<ImageTexture>();

    /* Look up shader definition to find the actual texture path from stages. */
    const GodotShaderProps *sp = Godot_ShaderProps_Find(name);
    Ref<ImageTexture> tex;

    if (sp && sp->stage_count > 0) {
        for (int st = 0; st < sp->stage_count && tex.is_null(); st++) {
            if (sp->stages[st].isLightmap) continue;

            const char *stage_map = nullptr;
            if (sp->stages[st].map[0]) {
                stage_map = sp->stages[st].map;
            } else if (sp->stages[st].animMapFrameCount > 0
                       && sp->stages[st].animMapFrames[0][0]) {
                stage_map = sp->stages[st].animMapFrames[0];
            }
            if (!stage_map) continue;
            if (strcmp(stage_map, "$lightmap") == 0) continue;
            if (strcmp(stage_map, "$whiteimage") == 0) continue;
            if (sp->stages[st].tcGen == STAGE_TCGEN_ENVIRONMENT) continue;

            tex = vfx_load_from_qpath(stage_map);
            if (tex.is_null()) {
                UtilityFunctions::print(String("[VFX-TEX] WARN: stage map load failed: '") +
                    String(stage_map) + String("' for sprite shader '") + String(name) + String("'"));
            }
        }
    } else {
        UtilityFunctions::print(String("[VFX-TEX] No shader props for sprite shader '") +
            String(name) + String("' (props=") + String(sp ? "YES" : "NO") +
            String(" stage_count=") + String::num_int64(sp ? sp->stage_count : -1) + String(")"));
    }

    /* Fallback 1: try using the name itself as a texture path */
    if (tex.is_null()) {
        tex = vfx_load_from_qpath(name);
    }

    /* Fallback 2: try common sprite/effect texture directories.
     * Many MOHAA sprite shaders reference textures at
     * textures/sprites/<name>.tga or textures/effects/<name>.tga. */
    if (tex.is_null()) {
        /* Extract basename from any path prefix in the shader name */
        const char *basename = strrchr(name, '/');
        basename = basename ? basename + 1 : name;
        char path_buf[256];
        snprintf(path_buf, sizeof(path_buf), "textures/sprites/%s", basename);
        tex = vfx_load_from_qpath(path_buf);
        if (tex.is_null()) {
            snprintf(path_buf, sizeof(path_buf), "textures/effects/%s", basename);
            tex = vfx_load_from_qpath(path_buf);
        }
        if (tex.is_null()) {
            snprintf(path_buf, sizeof(path_buf), "models/fx/%s", basename);
            tex = vfx_load_from_qpath(path_buf);
        }
        if (tex.is_valid()) {
            UtilityFunctions::print(String("[VFX-TEX] Fallback found texture for '") +
                String(name) + String("' via directory search"));
        }
    }

    return tex;
}

static Ref<ImageTexture> vfx_load_texture(int shader_handle)
{
    /* Check cache */
    auto it = vfx_tex_cache.find(shader_handle);
    if (it != vfx_tex_cache.end()) {
        return it->second;
    }

    Ref<ImageTexture> tex;

    /* Strategy 1: Look up as a shader handle in the shader table */
    const char *raw_name = Godot_Renderer_GetShaderName(shader_handle);
    const char *remapped = Godot_Renderer_GetShaderRemap(raw_name);
    const char *name     = (remapped && remapped[0]) ? remapped : raw_name;
    if (name && name[0]) {
        tex = vfx_load_texture_by_name(name);
    }

    /* Strategy 2: If shader table lookup failed, try as a model handle.
     * In MOHAA, RT_SPRITE's hModel can be either a shader handle or a
     * model handle (.spr files).  For .spr models, the model name
     * (without .spr extension) IS the shader name. */
    if (tex.is_null()) {
        const char *model_name = Godot_Model_GetName(shader_handle);
        if (model_name && model_name[0]) {
            /* Strip .spr extension if present */
            char stripped[256];
            strncpy(stripped, model_name, sizeof(stripped) - 1);
            stripped[sizeof(stripped) - 1] = '\0';
            char *dot = strrchr(stripped, '.');
            if (dot) *dot = '\0';

            tex = vfx_load_texture_by_name(stripped);

            if (tex.is_null()) {
                /* Also try the raw model name */
                tex = vfx_load_texture_by_name(model_name);
            }
        }
    }

    vfx_tex_cache[shader_handle] = tex;
    return tex;
}

/* ── Sprite size lookup — uses engine's real image_t dimensions ── */
static VfxSpriteSize vfx_get_sprite_size(int shader_handle)
{
    auto it = vfx_size_cache.find(shader_handle);
    if (it != vfx_size_cache.end()) return it->second;

    VfxSpriteSize sz;

    /* Resolve the shader name for this handle */
    const char *raw_name = Godot_Renderer_GetShaderName(shader_handle);
    const char *remapped = Godot_Renderer_GetShaderRemap(raw_name);
    const char *lookup   = (remapped && remapped[0]) ? remapped : raw_name;

    /* Primary: read dimensions from the engine's real shader_t/image_t.
     * This mirrors SPR_RegisterSprite's data path exactly:
     *   shader->unfoggedStages[0]->bundle[0].image[0]->width/height
     * Guarantees parity with the engine's RB_DrawSprite sizing. */
    if (lookup && lookup[0]) {
        int eng_w = 0, eng_h = 0;
        float eng_scale = 1.0f;
        if (Godot_Sprite_GetEngineSize(lookup, &eng_w, &eng_h, &eng_scale)) {
            sz.width        = eng_w;
            sz.height       = eng_h;
            sz.sprite_scale = eng_scale;
        }
    }

    /* Fallback: if engine lookup failed, try Godot-loaded texture */
    if (sz.width <= 0 || sz.height <= 0) {
        Ref<ImageTexture> tex = vfx_load_texture(shader_handle);
        if (tex.is_valid()) {
            sz.width  = tex->get_width();
            sz.height = tex->get_height();
        }
        /* Fallback spritescale from shader props */
        if (lookup && lookup[0]) {
            const GodotShaderProps *sp = Godot_ShaderProps_Find(lookup);
            if (sp) sz.sprite_scale = sp->sprite_scale;
        }
    }

    vfx_size_cache[shader_handle] = sz;
    return sz;
}

/* ── Pre-built unit quad mesh (shared by all pool slots) ── */
static Ref<ArrayMesh> vfx_unit_quad;

static Ref<ArrayMesh> vfx_get_unit_quad()
{
    if (vfx_unit_quad.is_valid()) return vfx_unit_quad;

    PackedVector3Array pos;
    PackedVector2Array uv;
    PackedInt32Array   idx;
    pos.resize(4);
    uv.resize(4);
    idx.resize(6);

    /* 1×1 quad centred at origin — scaled per-sprite via node scale */
    pos.set(0, Vector3(-0.5f, -0.5f, 0.0f));
    pos.set(1, Vector3( 0.5f, -0.5f, 0.0f));
    pos.set(2, Vector3( 0.5f,  0.5f, 0.0f));
    pos.set(3, Vector3(-0.5f,  0.5f, 0.0f));
    uv.set(0, Vector2(0, 1));
    uv.set(1, Vector2(1, 1));
    uv.set(2, Vector2(1, 0));
    uv.set(3, Vector2(0, 0));
    idx.set(0, 0); idx.set(1, 1); idx.set(2, 2);
    idx.set(3, 0); idx.set(4, 2); idx.set(5, 3);

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = pos;
    arrays[Mesh::ARRAY_TEX_UV] = uv;
    arrays[Mesh::ARRAY_INDEX]  = idx;

    vfx_unit_quad.instantiate();
    vfx_unit_quad->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return vfx_unit_quad;
}

/* ──────────────────────────────────────────── */
/*  Public API                                  */
/* ──────────────────────────────────────────── */

void Godot_VFX_Init(Node3D *parent)
{
    if (vfx_initialised || !parent) return;

    vfx_parent = parent;

    for (int i = 0; i < VFX_SPRITE_POOL_SIZE; i++) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(vfx_get_unit_quad());
        mi->set_visible(false);
        /* Cast no shadows — sprites are VFX, not geometry */
        mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        parent->add_child(mi);
        vfx_pool[i] = mi;
    }

    vfx_initialised = true;
}

void Godot_VFX_Update(float delta)
{
    (void)delta;
    if (!vfx_initialised) return;

    /* Scan entity buffer for sprites (rebuilds the cached index list) */
    int count = Godot_VFX_GetSpriteCount();

    /* Diagnostic: log sprite count changes */
    {
        static int last_count = -1;
        if (count != last_count && count > 0) {
            UtilityFunctions::print(String("[VFX] Sprite count changed: ") + String::num_int64(count));
            last_count = count;
        }
    }

    for (int i = 0; i < VFX_SPRITE_POOL_SIZE; i++) {
        MeshInstance3D *mi = vfx_pool[i];
        if (!mi) continue;

        if (i >= count) {
            mi->set_visible(false);
            continue;
        }

        float origin[3]      = {0};
        float radius          = 0.0f;
        float rotation        = 0.0f;
        float entityScale     = 1.0f;
        int   shaderHandle    = 0;
        unsigned char rgba[4] = {255, 255, 255, 255};

        Godot_VFX_GetSprite(i, origin, &radius, &shaderHandle,
                            &rotation, rgba, &entityScale);

        /* Diagnostic: log each new unique shader handle */
        {
            if (shaderHandle > 0 && vfx_logged_shaders.find(shaderHandle) == vfx_logged_shaders.end()) {
                vfx_logged_shaders.insert(shaderHandle);
                const char *sn = Godot_Renderer_GetShaderName(shaderHandle);
                const char *remap = Godot_Renderer_GetShaderRemap(sn);
                const char *lookup = (remap && remap[0]) ? remap : sn;
                Ref<ImageTexture> tex = vfx_load_texture(shaderHandle);
                const GodotShaderProps *sp = (lookup && lookup[0]) ? Godot_ShaderProps_Find(lookup) : nullptr;
                const char *transp_names[] = {"OPAQUE","ALPHA_TEST","ALPHA_BLEND","ADDITIVE","MULTIPLICATIVE","MULT_INV","ALPHA_INV"};
                int tn = sp ? sp->transparency : -1;
                UtilityFunctions::print(String("[VFX-DIAG] New sprite shader: handle=") +
                    String::num_int64(shaderHandle) +
                    " name='" + String(sn ? sn : "(null)") + "'" +
                    " remap='" + String((remap && remap[0]) ? remap : "none") + "'" +
                    " props=" + String(sp ? "YES" : "NO") +
                    " shader_transp=" + String(tn >= 0 && tn < 7 ? transp_names[tn] : "?") +
                    " tex=" + String(tex.is_valid() ? "LOADED" : "MISSING") +
                    " rgba=(" + String::num_int64(rgba[0]) + "," + String::num_int64(rgba[1]) + "," +
                    String::num_int64(rgba[2]) + "," + String::num_int64(rgba[3]) + ")" +
                    " radius=" + String::num(radius, 1));

                /* Log engine dimensions vs Godot dimensions for comparison */
                VfxSpriteSize diag_sz = vfx_get_sprite_size(shaderHandle);
                int godot_w = 0, godot_h = 0;
                if (tex.is_valid()) {
                    godot_w = tex->get_width();
                    godot_h = tex->get_height();
                }
                int eng_w = 0, eng_h = 0;
                float eng_spr_scale = 1.0f;
                int eng_ok = 0;
                if (lookup && lookup[0])
                    eng_ok = Godot_Sprite_GetEngineSize(lookup, &eng_w, &eng_h, &eng_spr_scale);

                UtilityFunctions::print(String("[VFX-DIAG]   engine_img=") +
                    String::num_int64(eng_w) + "x" + String::num_int64(eng_h) +
                    " godot_img=" + String::num_int64(godot_w) + "x" + String::num_int64(godot_h) +
                    (eng_ok && (eng_w != godot_w || eng_h != godot_h) ? " ** MISMATCH **" : "") +
                    " spritescale=" + String::num(diag_sz.sprite_scale, 3) +
                    " eng_spritescale=" + String::num(eng_spr_scale, 3) +
                    " entityScale=" + String::num(entityScale, 4));

                /* At scale=1, compute what the sprite extent would be in metres */
                float extent_m = (float)diag_sz.width * diag_sz.sprite_scale * MOHAA_UNIT_SCALE;
                UtilityFunctions::print(String("[VFX-DIAG]   base_extent=") +
                    String::num(extent_m, 3) + "m (at entityScale=1.0)" +
                    " actual_extent=" + String::num(extent_m * entityScale, 3) + "m");

                if (sp && sp->stage_count > 0) {
                    for (int st = 0; st < sp->stage_count; st++) {
                        if (sp->stages[st].map[0]) {
                            UtilityFunctions::print(String("[VFX-DIAG]   stage[") + String::num_int64(st) +
                                "] map='" + String(sp->stages[st].map) + "'" +
                                " isLM=" + String(sp->stages[st].isLightmap ? "Y" : "N") +
                                " tcGen=" + String::num_int64(sp->stages[st].tcGen));
                        }
                    }
                }
            }
        }

        /* Sprite sizing: MOHAA's RB_DrawSprite computes half-extent as
         *   image_pixels × 0.5 × entity.scale × shader.spritescale
         * Full extent = image_pixels × entity.scale × spritescale (in inches).
         * Our unit quad spans ±0.5, so scaling by full_extent gives the
         * correct world-space size after converting inches → metres.
         *
         * Note: refEntity_t.radius is NOT used for sprite sizing in the real
         * renderer (RB_DrawSprite uses only entity.scale × shader.spritescale
         * × image pixels).  cgame sets radius to arbitrary values (4.0 for
         * tempmodels, 0.0 for volumetric smoke) — it is purely for culling. */
        float sprite_w = 0.0f, sprite_h = 0.0f;
        if (shaderHandle > 0) {
            VfxSpriteSize sz = vfx_get_sprite_size(shaderHandle);
            if (sz.width > 0 && sz.height > 0) {
                sprite_w = (float)sz.width  * entityScale * sz.sprite_scale * MOHAA_UNIT_SCALE;
                sprite_h = (float)sz.height * entityScale * sz.sprite_scale * MOHAA_UNIT_SCALE;
            }
        }

        if (sprite_w < 0.0001f || sprite_h < 0.0001f) {
            mi->set_visible(false);
            continue;
        }

        /* Coordinate conversion + positioning */
        Vector3 pos = id_to_godot(origin[0], origin[1], origin[2]);

        /* Cached billboard material (keyed by shader handle + RGBA) */
        uint64_t mkey = vfx_mat_key(shaderHandle, rgba);
        Ref<StandardMaterial3D> mat;
        auto mit = vfx_mat_cache.find(mkey);
        if (mit != vfx_mat_cache.end()) {
            mat = mit->second;
        } else {
            mat.instantiate();
            mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
            mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
            mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
            mat->set_albedo(Color(rgba[0] / 255.0f, rgba[1] / 255.0f,
                                  rgba[2] / 255.0f, rgba[3] / 255.0f));

            if (shaderHandle > 0) {
                Ref<ImageTexture> tex = vfx_load_texture(shaderHandle);
                if (tex.is_valid()) {
                    mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                }

                /* Apply shader properties: blendFunc, alphaFunc, cull, deform */
                const char *sn = Godot_Renderer_GetShaderName(shaderHandle);
                const char *remap = Godot_Renderer_GetShaderRemap(sn);
                const char *lookup = (remap && remap[0]) ? remap : sn;
                if (lookup && lookup[0]) {
                    const GodotShaderProps *sp = Godot_ShaderProps_Find(lookup);
                    if (sp) {
                        switch (sp->transparency) {
                            case SHADER_ALPHA_TEST:
                                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
                                mat->set_alpha_scissor_threshold(sp->alpha_threshold);
                                break;
                            case SHADER_ALPHA_BLEND:
                                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                                break;
                            case SHADER_ADDITIVE:
                                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                                mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                                break;
                            case SHADER_MULTIPLICATIVE:
                            case SHADER_MULTIPLICATIVE_INV:
                                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                                mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                                break;
                            default:
                                /* SHADER_OPAQUE: unusual for a sprite effect.
                                 * Default to additive (fire/flash/corona) since
                                 * sprite quads need transparency to hide edges. */
                                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                                mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                                break;
                        }
                        /* autosprite/autosprite2 deform — already billboard, but
                         * autosprite2 should use fixed-Y billboard mode */
                        if (sp->has_deform && sp->deform_type == 4) {
                            mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_FIXED_Y);
                        }
                    }
                }

                /* Re-enforce sprite-specific settings: cull mode must stay
                 * CULL_DISABLED for billboard sprites (shader default
                 * SHADER_CULL_BACK would hide back faces). */
                mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
            }
            vfx_mat_cache[mkey] = mat;
        }

        mi->set_surface_override_material(0, mat);

        /* Build transform: position + per-axis scale (width × height) */
        Basis basis;
        basis.scale(Vector3(sprite_w, sprite_h, 1.0f));
        mi->set_global_transform(Transform3D(basis, pos));

        mi->set_visible(true);
    }

    /* Per-frame aggregate diagnostics — log max sprite extent and scale
     * for the first 120 frames that have sprites (after that, quiet). */
    {
        static int diag_frame_count = 0;
        if (count > 0 && diag_frame_count < 120) {
            diag_frame_count++;
            float max_extent = 0.0f, max_scale = 0.0f, min_scale = 999.0f;
            int   max_extent_idx = -1;
            for (int j = 0; j < count && j < VFX_SPRITE_POOL_SIZE; j++) {
                float oj[3] = {0}, rj = 0, rotj = 0, sj = 1.0f;
                int shj = 0;
                unsigned char cj[4] = {255,255,255,255};
                Godot_VFX_GetSprite(j, oj, &rj, &shj, &rotj, cj, &sj);
                if (shj > 0 && sj > 0.0001f) {
                    VfxSpriteSize szj = vfx_get_sprite_size(shj);
                    float ext = (float)szj.width * sj * szj.sprite_scale * MOHAA_UNIT_SCALE;
                    if (ext > max_extent) { max_extent = ext; max_extent_idx = j; }
                    if (sj > max_scale) max_scale = sj;
                    if (sj < min_scale) min_scale = sj;
                }
            }
            if (max_extent > 0.5f) {
                /* Only log when sprites are > 0.5m (potential "huge" sprites) */
                UtilityFunctions::print(String("[VFX-FRAME] n=") + String::num_int64(count) +
                    " max_extent=" + String::num(max_extent, 3) + "m" +
                    " min_entityScale=" + String::num(min_scale, 4) +
                    " max_entityScale=" + String::num(max_scale, 4) +
                    " biggest_sprite_idx=" + String::num_int64(max_extent_idx));
            }
        }
    }
}

void Godot_VFX_Shutdown(void)
{
    if (!vfx_initialised) return;

    for (int i = 0; i < VFX_SPRITE_POOL_SIZE; i++) {
        if (vfx_pool[i]) {
            vfx_pool[i]->queue_free();
            vfx_pool[i] = nullptr;
        }
    }

    vfx_tex_cache.clear();
    vfx_mat_cache.clear();
    vfx_size_cache.clear();
    vfx_unit_quad.unref();
    vfx_parent      = nullptr;
    vfx_initialised = false;
}

void Godot_VFX_Clear(void)
{
    if (!vfx_initialised) return;

    for (int i = 0; i < VFX_SPRITE_POOL_SIZE; i++) {
        if (vfx_pool[i]) {
            vfx_pool[i]->set_visible(false);
        }
    }

    /* Flush caches — shaders/textures may differ on the next map */
    vfx_tex_cache.clear();
    vfx_mat_cache.clear();
    vfx_size_cache.clear();
    vfx_logged_shaders.clear();
}
