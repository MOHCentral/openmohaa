/*
 * godot_vfx.cpp — VFX Manager: billboard sprite rendering with MultiMesh batching.
 *
 * Reads RT_SPRITE entities from the renderer entity buffer (via the
 * C accessor in godot_vfx_accessors.c) and renders them as camera-facing
 * billboard quads in the Godot 3D scene.
 *
 * Sprites are grouped by shader handle and rendered using MultiMeshInstance3D
 * for efficient batching.  Each unique shader gets one MultiMeshInstance3D
 * with a shared material and per-instance transforms + vertex colours.
 *
 * VFX Manager Foundation
 * Rewritten for MultiMesh batching, vertex colour parity, and
 *           correct blend mode detection.
 */

#include "godot_vfx.h"
#include "godot_shader_props.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <unordered_map>
#include <cstdio>
#include <unordered_set>
#include <vector>
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

    /* Engine-side sprite dimension accessor (godot_shader_accessors.c). */
    int  Godot_Sprite_GetEngineSize(const char *shader_name,
                                    int *out_width, int *out_height,
                                    float *out_sprite_scale);

    /* Engine pipeline capture (godot_render_capture.c) */
    int  Godot_RealModel_GetSpriteDims(int realHandle,
                                       float *out_width, float *out_height,
                                       float *out_sprite_scale);
    int  Godot_ComputeSpriteQuad(int realModelHandle,
                                 const float origin[3], float entityScale,
                                 const float entityAxis[3][3],
                                 const unsigned char rgba[4],
                                 float out_xyz[4][3], float out_uv[4][2]);
    int  Godot_VFX_GetSpriteModelHandle(int idx);
    int  Godot_Model_GetRealHandle(int hModel);
    void Godot_VFX_GetSpriteAxis(int idx, float *out_axis);

    /* Engine-side raw image loader for texture loading */
    int  R_LoadRawImage(const char *name, unsigned char **pic,
                        int *width, int *height);
    void R_FreeRawImage(unsigned char *pic);

    int  GR_IsRealRendererInited(void);
}

/* ── Constants ── */
static constexpr int   VFX_MAX_SPRITES_PER_GROUP = 256;
static constexpr float MOHAA_UNIT_SCALE           = 1.0f / 39.37f;

/* ── id Tech 3 → Godot coordinate conversion ── */
static inline Vector3 id_to_godot(float ix, float iy, float iz) {
    return Vector3(-iy * MOHAA_UNIT_SCALE,
                    iz * MOHAA_UNIT_SCALE,
                   -ix * MOHAA_UNIT_SCALE);
}

/* ── Module state ── */
static Node3D *vfx_parent     = nullptr;
static bool    vfx_initialised = false;

/* ── Texture cache ── */
static std::unordered_map<int, Ref<ImageTexture>> vfx_tex_cache;

/* Cached sprite size info: image pixel dimensions + shader spritescale */
struct VfxSpriteSize {
    int   width        = 0;
    int   height       = 0;
    float sprite_scale = 1.0f;
};
static std::unordered_map<int, VfxSpriteSize> vfx_size_cache;

/* Per-shader render group: one MultiMeshInstance3D per unique shader.
 * Each group can render up to VFX_MAX_SPRITES_PER_GROUP billboard quads
 * in a single draw call. */
struct VfxShaderGroup {
    MultiMeshInstance3D    *mminstance = nullptr;
    Ref<MultiMesh>          multimesh;
    Ref<StandardMaterial3D>  material;
    Ref<ImageTexture>        texture;
    int                      blend_type = 0;  /* 0=alpha, 1=additive, 2=multiplicative */
    int                      active_count = 0;
};

static std::unordered_map<int, VfxShaderGroup> vfx_groups;

/* ── Texture loading (mirrors MoHAARunner::get_shader_texture — resolves shader stage maps) ── */

static Ref<ImageTexture> vfx_load_from_qpath(const char *qpath)
{
    if (!qpath || !qpath[0]) return Ref<ImageTexture>();

    /* Primary path: use the engine's R_LoadRawImage for correct RGBA byte
     * order and TGA/JPG handling parity with the real renderer. */
    {
        unsigned char *raw_pic = nullptr;
        int raw_w = 0, raw_h = 0;
        if (R_LoadRawImage(qpath, &raw_pic, &raw_w, &raw_h) && raw_pic && raw_w > 0 && raw_h > 0) {
            int raw_size = raw_w * raw_h * 4;
            PackedByteArray pba;
            pba.resize(raw_size);
            memcpy(pba.ptrw(), raw_pic, raw_size);
            R_FreeRawImage(raw_pic);

            Ref<Image> img = Image::create_from_data(raw_w, raw_h, false, Image::FORMAT_RGBA8, pba);
            if (!img.is_null() && !img->is_empty()) {
                /* Dead-alpha fix: textures with all-zero or all-255 alpha
                 * have no meaningful alpha → convert to RGB8 so alpha blend
                 * doesn't make them invisible or waste fill rate. */
                if (img->get_format() == Image::FORMAT_RGBA8) {
                    PackedByteArray imgdata = img->get_data();
                    int pixel_count = raw_w * raw_h;
                    const uint8_t *pix = imgdata.ptr();
                    bool all_zero = true, all_opaque = true;
                    for (int p = 0; p < pixel_count; p++) {
                        uint8_t a = pix[p * 4 + 3];
                        if (a > 0) all_zero = false;
                        if (a < 255) all_opaque = false;
                        if (!all_zero && !all_opaque) break;
                    }
                    if (all_zero || all_opaque) {
                        img->convert(Image::FORMAT_RGB8);
                    }
                }
                img->generate_mipmaps();
                return ImageTexture::create_from_image(img);
            }
        }
    }

    /* Fallback: VFS + Godot image decoders + extension probing */
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
        }
    } else {
        /* No shader props or no stages — not unusual for sprite effects
         * that use implicit shaders (texture file loaded directly). */
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
            /* Found via directory search — expected for implicit sprite shaders */
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

/* ── Blend-mode detection from shader properties ──
 * Returns: 0=alpha, 1=additive, 2=multiplicative.
 *
 * The engine determines transparency from the first non-lightmap stage's
 * blendFunc.  For VFX sprites this is crucial: smoke uses alpha blend
 * while fire/flash uses additive.  Getting this wrong makes smoke glow
 * (additive on a textured-alpha sprite) and appear larger than it should.
 *
 * For SHADER_OPAQUE sprites (no explicit blendFunc in the shader
 * definition), we probe the loaded texture's alpha channel.  Textures
 * with meaningful alpha (smoke, fog) get alpha blend; textures that are
 * fully opaque or have dead alpha (fire, flash, corona with black
 * background) get additive blend so the black edges disappear. */
static int vfx_detect_blend_mode(int shaderHandle)
{
    const char *sn     = Godot_Renderer_GetShaderName(shaderHandle);
    const char *remap  = Godot_Renderer_GetShaderRemap(sn);
    const char *lookup = (remap && remap[0]) ? remap : sn;

    if (lookup && lookup[0]) {
        const GodotShaderProps *sp = Godot_ShaderProps_Find(lookup);
        if (sp) {
            switch (sp->transparency) {
                case SHADER_ADDITIVE:      return 1;
                case SHADER_MULTIPLICATIVE:
                case SHADER_MULTIPLICATIVE_INV: return 2;
                case SHADER_ALPHA_BLEND:
                case SHADER_ALPHA_TEST:
                case SHADER_ALPHA_BLEND_INV: return 0;
                case SHADER_OPAQUE:
                default:
                    /* Opaque sprite — probe texture alpha to decide. */
                    break;
            }
        }
    }

    /* Texture-based detection: meaningful alpha → alpha blend (smoke).
     * No alpha / dead alpha → additive (fire/flash with black bg). */
    Ref<ImageTexture> tex = vfx_load_texture(shaderHandle);
    if (tex.is_valid()) {
        Ref<Image> img = tex->get_image();
        if (img.is_valid()) {
            Image::AlphaMode am = img->detect_alpha();
            if (am == Image::ALPHA_BLEND || am == Image::ALPHA_BIT) {
                return 0;  /* Has meaningful alpha → alpha blend */
            }
        }
    }
    return 1;  /* No alpha → additive (hides black background) */
}


/* ──────────────────────────────────────────── */
/*  Public API                                  */
/* ──────────────────────────────────────────── */

void Godot_VFX_Init(Node3D *parent)
{
    if (vfx_initialised || !parent) return;
    vfx_parent      = parent;
    vfx_initialised = true;
}

void Godot_VFX_Update(float delta)
{
    (void)delta;
    if (!vfx_initialised) return;

    /* ── Reset all group active counts ── */
    for (auto &kv : vfx_groups) {
        kv.second.active_count = 0;
    }

    /* ── Read sprite count from entity buffer ── */
    int count = Godot_VFX_GetSpriteCount();

    /* ── Temporary per-group sprite collection ── */
    struct SpriteInst {
        Vector3 position;
        float   width_m;
        float   height_m;
        Color   tint;
        bool    engine_verts;
        Vector3 engine_right;
        Vector3 engine_up;
    };
    std::unordered_map<int, std::vector<SpriteInst>> group_sprites;

    /* ── Collect and size sprites ── */
    for (int i = 0; i < count; i++) {
        float origin[3]       = {0};
        float radius           = 0.0f;
        float rotation         = 0.0f;
        float entityScale      = 1.0f;
        int   shaderHandle     = 0;
        unsigned char rgba[4]  = {255, 255, 255, 255};

        Godot_VFX_GetSprite(i, origin, &radius, &shaderHandle,
                            &rotation, rgba, &entityScale);
        if (shaderHandle <= 0) continue;

        SpriteInst inst;
        inst.tint = Color(rgba[0] / 255.0f, rgba[1] / 255.0f,
                          rgba[2] / 255.0f, rgba[3] / 255.0f);
        inst.engine_verts = false;
        inst.width_m  = 0.0f;
        inst.height_m = 0.0f;

        /* ── Try engine-vert path (parity with RB_DrawSprite) ── */
        {
            int hModel = Godot_VFX_GetSpriteModelHandle(i);
            int realH  = (hModel > 0) ? Godot_Model_GetRealHandle(hModel) : 0;
            if (realH > 0) {
                float entAxis[9];
                Godot_VFX_GetSpriteAxis(i, entAxis);
                float axis33[3][3];
                memcpy(axis33, entAxis, sizeof(axis33));

                float out_xyz[4][3], out_uv[4][2];
                if (Godot_ComputeSpriteQuad(realH, origin, entityScale,
                                            axis33, rgba, out_xyz, out_uv))
                {
                    float right_id[3], up_id[3];
                    for (int c = 0; c < 3; c++) {
                        right_id[c] = out_xyz[1][c] - out_xyz[0][c];
                        up_id[c]    = out_xyz[0][c] - out_xyz[2][c];
                    }
                    inst.engine_right = Vector3(
                        -right_id[1] * MOHAA_UNIT_SCALE,
                         right_id[2] * MOHAA_UNIT_SCALE,
                        -right_id[0] * MOHAA_UNIT_SCALE);
                    inst.engine_up = Vector3(
                        -up_id[1] * MOHAA_UNIT_SCALE,
                         up_id[2] * MOHAA_UNIT_SCALE,
                        -up_id[0] * MOHAA_UNIT_SCALE);
                    inst.position = id_to_godot(origin[0], origin[1], origin[2]);
                    inst.width_m  = inst.engine_right.length();
                    inst.height_m = inst.engine_up.length();
                    inst.engine_verts = true;
                }
            }
        }

        /* ── Fallback billboard sizing ── */
        if (!inst.engine_verts) {
            VfxSpriteSize sz = vfx_get_sprite_size(shaderHandle);
            if (sz.width > 0 && sz.height > 0) {
                inst.width_m  = (float)sz.width  * entityScale * sz.sprite_scale * MOHAA_UNIT_SCALE;
                inst.height_m = (float)sz.height * entityScale * sz.sprite_scale * MOHAA_UNIT_SCALE;
            }
            inst.position = id_to_godot(origin[0], origin[1], origin[2]);
        }

        if (inst.width_m < 0.0001f || inst.height_m < 0.0001f) continue;

        group_sprites[shaderHandle].push_back(inst);
    }

    /* ── Update MultiMesh groups ── */
    for (auto &kv : group_sprites) {
        int shaderHandle   = kv.first;
        auto &sprites      = kv.second;
        int n              = (int)sprites.size();
        if (n > VFX_MAX_SPRITES_PER_GROUP) n = VFX_MAX_SPRITES_PER_GROUP;

        /* Get or create group */
        auto git = vfx_groups.find(shaderHandle);
        if (git == vfx_groups.end()) {
            /* Create new shader group */
            VfxShaderGroup grp;
            grp.mminstance = memnew(MultiMeshInstance3D);
            grp.mminstance->set_name(String("VFX_") + String::num_int64(shaderHandle));
            grp.mminstance->set_visible(false);
            grp.mminstance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            vfx_parent->add_child(grp.mminstance);

            /* Create MultiMesh with instance colour support */
            grp.multimesh.instantiate();
            grp.multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            grp.multimesh->set_use_colors(true);
            grp.multimesh->set_use_custom_data(false);
            grp.multimesh->set_mesh(vfx_get_unit_quad());
            grp.multimesh->set_instance_count(VFX_MAX_SPRITES_PER_GROUP);
            grp.mminstance->set_multimesh(grp.multimesh);

            /* Detect blend mode */
            grp.blend_type = vfx_detect_blend_mode(shaderHandle);

            /* Create material with vertex(instance) colour support.
             * MultiMesh instance colours are exposed as COLOR in the shader,
             * so FLAG_ALBEDO_FROM_VERTEX_COLOR multiplies albedo × instance_color.
             * This replaces the old per-RGBA material cache. */
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
            mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);

            switch (grp.blend_type) {
                case 1: /* Additive */
                    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                    mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                    break;
                case 2: /* Multiplicative */
                    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                    mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                    break;
                default: /* Alpha (0) */
                    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                    break;
            }

            /* Load and attach texture */
            grp.texture = vfx_load_texture(shaderHandle);
            if (grp.texture.is_valid()) {
                mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, grp.texture);
            }
            grp.material = mat;
            grp.mminstance->set_material_override(mat);

            vfx_groups[shaderHandle] = grp;
            git = vfx_groups.find(shaderHandle);
        }

        VfxShaderGroup &grp = git->second;
        grp.active_count = n;
        grp.multimesh->set_visible_instance_count(n);

        for (int j = 0; j < n; j++) {
            const SpriteInst &sp = sprites[j];

            Transform3D xform;
            if (sp.engine_verts) {
                /* Engine-vert path: use pre-computed right/up as basis.
                 * Disable billboard for this material on first engine-vert. */
                Vector3 normal = sp.engine_right.cross(sp.engine_up);
                float nlen = normal.length();
                if (nlen > 0.0001f) normal /= nlen;
                else normal = Vector3(0, 0, 1);
                xform = Transform3D(Basis(sp.engine_right, sp.engine_up, normal), sp.position);

                /* Switch off billboard if this group adopted billboard */
                if (j == 0) {
                    grp.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
                }
            } else {
                /* Fallback billboard: scale the 1×1 unit quad. */
                Basis basis;
                basis.scale(Vector3(sp.width_m, sp.height_m, 1.0f));
                xform = Transform3D(basis, sp.position);

                /* Ensure billboard is on for fallback sprites */
                if (j == 0) {
                    grp.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
                }
            }

            grp.multimesh->set_instance_transform(j, xform);
            grp.multimesh->set_instance_color(j, sp.tint);
        }

        grp.mminstance->set_visible(true);
    }

    /* ── Hide groups that have no sprites this frame ── */
    for (auto &kv : vfx_groups) {
        if (kv.second.active_count == 0) {
            kv.second.multimesh->set_visible_instance_count(0);
            kv.second.mminstance->set_visible(false);
        }
    }
}


void Godot_VFX_Shutdown(void)
{
    if (!vfx_initialised) return;

    for (auto &kv : vfx_groups) {
        if (kv.second.mminstance) {
            kv.second.mminstance->queue_free();
        }
    }
    vfx_groups.clear();

    vfx_tex_cache.clear();
    vfx_size_cache.clear();
    vfx_unit_quad.unref();
    vfx_parent      = nullptr;
    vfx_initialised = false;
}

void Godot_VFX_Clear(void)
{
    if (!vfx_initialised) return;

    for (auto &kv : vfx_groups) {
        kv.second.multimesh->set_visible_instance_count(0);
        kv.second.mminstance->set_visible(false);
        kv.second.active_count = 0;
    }

    /* Flush caches — shaders/textures may differ on the next map */
    vfx_tex_cache.clear();
    vfx_size_cache.clear();

}
