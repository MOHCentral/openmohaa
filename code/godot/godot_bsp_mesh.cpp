/*
 * godot_bsp_mesh.cpp — MOHAA BSP world geometry loader for Godot.
 *
 * Reads a BSP file from the engine VFS, parses vertex/index/surface/
 * shader lumps, and constructs Godot ArrayMesh nodes with per-shader
 * surfaces.  Handles MST_PLANAR, MST_TRIANGLE_SOUP, and MST_PATCH
 * (Bézier) surface types.  Textures and lightmaps are loaded from
 * the engine VFS and applied as StandardMaterial3D.
 *
 * The engine's VFS (Godot_VFS_ReadFile) loads the whole file into
 * memory.  We parse in-place, avoiding separate FS_Seek calls.
 *
 * Coordinate system: id Tech 3 (X=Forward, Y=Left, Z=Up, inches)
 *   → Godot (X=Right, Y=Up, -Z=Forward, metres).
 * Scale: 1 id unit ≈ 1 inch → 1/39.37 metres.
 */

#include "godot_bsp_mesh.h"
#include "godot_bsp_accessors.h"
#include "godot_shader_props.h"
#include "godot_shader_material.h"
#include "godot_terrain_normal.h"

#if __has_include("godot_pbr.h")
#include "godot_pbr.h"
#define HAS_PBR_MODULE 1
#endif

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <string>

using namespace godot;

/* ===================================================================
 *  VFS accessor — extern "C" linkage to engine
 * ================================================================ */

extern "C" {
    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer);
    void Godot_VFS_FreeFile(void *buffer);
}

/* ===================================================================
 *  BSP on-disc structures (must match qfiles.h layout exactly)
 *
 *  We define them locally rather than including engine headers to
 *  avoid the godot-cpp / engine header collision.
 * ================================================================ */

// "2015" in little-endian
static constexpr int32_t BSP_IDENT = (('5' << 24) + ('1' << 16) + ('0' << 8) + '2');
static constexpr int32_t BSP_MIN_VERSION = 17;
static constexpr int32_t BSP_MAX_VERSION = 21;

// Lump indices (MOHAA-specific numbering)
static constexpr int LUMP_SHADERS       = 0;
static constexpr int LUMP_PLANES        = 1;
static constexpr int LUMP_LIGHTMAPS     = 2;
static constexpr int LUMP_SURFACES      = 3;
static constexpr int LUMP_DRAWVERTS     = 4;
static constexpr int LUMP_DRAWINDEXES   = 5;
static constexpr int LUMP_LEAFBRUSHES   = 6;
static constexpr int LUMP_LEAFSURFACES  = 7;
static constexpr int LUMP_LEAFS         = 8;
static constexpr int LUMP_NODES         = 9;
static constexpr int LUMP_BRUSHES       = 12;
static constexpr int LUMP_MODELS        = 13;
static constexpr int LUMP_ENTITIES      = 14;
static constexpr int HEADER_LUMPS       = 28;

// Additional lump indices for terrain
static constexpr int LUMP_TERRAIN       = 22;
static constexpr int LUMP_TERRAININDEXES = 23;

// Static model and brush model lumps
static constexpr int LUMP_STATICMODELDEF  = 25;
static constexpr int LUMP_STATICMODELDATA = 24;

// Surface types
static constexpr int MST_BAD            = 0;
static constexpr int MST_PLANAR         = 1;
static constexpr int MST_PATCH          = 2;
static constexpr int MST_TRIANGLE_SOUP  = 3;
static constexpr int MST_FLARE          = 4;
static constexpr int MST_TERRAIN        = 5;

// Surface flags (from surfaceflags.h)
static constexpr int SURF_NODRAW        = 0x80;
static constexpr int SURF_SKY           = 0x4;
static constexpr int SURF_NOLIGHTMAP    = 0x100;
static constexpr int SURF_HINT          = 0x40000000;

struct bsp_lump_t {
    int32_t fileofs;
    int32_t filelen;
};

struct bsp_header_t {
    int32_t    ident;
    int32_t    version;
    int32_t    checksum;
    bsp_lump_t lumps[HEADER_LUMPS];
};

struct bsp_shader_t {
    char    shader[64];
    int32_t surfaceFlags;
    int32_t contentFlags;
    int32_t subdivisions;
    char    fenceMaskImage[64];
};

struct bsp_drawvert_t {
    float   xyz[3];
    float   st[2];         // texture coordinates
    float   lightmap[2];   // lightmap UV
    float   normal[3];
    uint8_t color[4];      // RGBA
};

struct bsp_surface_t {
    int32_t shaderNum;
    int32_t fogNum;
    int32_t surfaceType;
    int32_t firstVert;
    int32_t numVerts;
    int32_t firstIndex;
    int32_t numIndexes;
    int32_t lightmapNum;
    int32_t lightmapX, lightmapY;
    int32_t lightmapWidth, lightmapHeight;
    float   lightmapOrigin[3];
    float   lightmapVecs[3][3];
    int32_t patchWidth;
    int32_t patchHeight;
    float   subdivisions;
};

// Compile-time struct size checks
static_assert(sizeof(bsp_shader_t)  == 140, "bsp_shader_t must be 140 bytes");
static_assert(sizeof(bsp_drawvert_t) == 44, "bsp_drawvert_t must be 44 bytes");
static_assert(sizeof(bsp_surface_t) == 108, "bsp_surface_t must be 108 bytes");

/* ── On-disc terrain patch (cTerraPatch_t / dterPatch_t from qfiles.h)
 *
 *  Each patch covers a 512×512 world-unit square with a 9×9 heightmap
 *  (8×8 cells, 64 units per cell).  Height = iBaseHeight + heightmap[i]*2.
 *
 *  Layout uses natural alignment — 385 bytes of fields + 3 bytes tail
 *  padding for 4-byte alignment = 388 bytes total (matches engine sizeof).
 * ─────────────────────────────────────────────────────────────────── */

struct bsp_terrain_patch_t {
    uint8_t     flags;              // TERPATCH_FLIP (0x40), TERPATCH_NEIGHBOR (0x80)
    uint8_t     lmapScale;          // lightmap texels per 64 world-units (must be > 0)
    uint8_t     lm_s;              // lightmap S offset in pixels within page
    uint8_t     lm_t;              // lightmap T offset in pixels within page
    float       texCoord[2][2][2]; // corner diffuse UVs: [x_dir][y_dir][uv_component]
                                    //   [0][0] = SW, [0][1] = NW, [1][0] = SE, [1][1] = NE
    int8_t      x;                 // patch X grid position (world X = x << 6)
    int8_t      y;                 // patch Y grid position (world Y = y << 6)
    int16_t     iBaseHeight;       // base Z altitude (signed)
    uint16_t    iShader;           // index into BSP shader lump
    uint16_t    iLightMap;         // lightmap page index
    int16_t     iNorth;            // neighbour patch indices (-1 = none)
    int16_t     iEast;
    int16_t     iSouth;
    int16_t     iWest;
    uint16_t    varTree[2][63];    // ROAM binary-triangle-tree variance (unused for static mesh)
    uint8_t     heightmap[81];     // 9×9 height values (unsigned byte, scaled ×2)
};

static_assert(sizeof(bsp_terrain_patch_t) == 388, "terrain patch must be 388 bytes");

/* ── On-disc static model definition (cStaticModel_t from qfiles.h) ── */
struct bsp_static_model_t {
    char    model[128];
    float   origin[3];
    float   angles[3];
    float   scale;
    int32_t firstVertexData;
    int32_t numVertexData;
};

static_assert(sizeof(bsp_static_model_t) == 164, "static model def must be 164 bytes");

/* ── On-disc sub-model definition (dmodel_t from qfiles.h) ── */
struct bsp_model_t {
    float   mins[3], maxs[3];
    int32_t firstSurface, numSurfaces;
    int32_t firstBrush, numBrushes;
};

static_assert(sizeof(bsp_model_t) == 40, "dmodel_t must be 40 bytes");

/* ── On-disc BSP plane (dplane_t) ── */
struct bsp_plane_t {
    float normal[3];
    float dist;
};
static_assert(sizeof(bsp_plane_t) == 16, "dplane_t must be 16 bytes");

/* ── On-disc BSP node (dnode_t) ── */
struct bsp_node_t {
    int32_t planeNum;
    int32_t children[2];    // negative = -(leaf+1)
    int32_t mins[3];
    int32_t maxs[3];
};
static_assert(sizeof(bsp_node_t) == 36, "dnode_t must be 36 bytes");

/* ── On-disc BSP leaf (dleaf_t, version ≥ 18) ── */
struct bsp_leaf_t {
    int32_t cluster;
    int32_t area;
    int32_t mins[3];
    int32_t maxs[3];
    int32_t firstLeafSurface;
    int32_t numLeafSurfaces;
    int32_t firstLeafBrush;
    int32_t numLeafBrushes;
    int32_t firstTerraPatch;
    int32_t numTerraPatches;
    int32_t firstStaticModel;
    int32_t numStaticModels;
};
static_assert(sizeof(bsp_leaf_t) == 64, "dleaf_t must be 64 bytes");

/* ── On-disc BSP leaf (dleaf_t_ver17 — smaller) ── */
struct bsp_leaf_t_v17 {
    int32_t cluster;
    int32_t area;
    int32_t mins[3];
    int32_t maxs[3];
    int32_t firstLeafSurface;
    int32_t numLeafSurfaces;
    int32_t firstLeafBrush;
    int32_t numLeafBrushes;
    int32_t firstTerraPatch;
    int32_t numTerraPatches;
};
static_assert(sizeof(bsp_leaf_t_v17) == 56, "dleaf_t_ver17 must be 56 bytes");

/* ── Parsed static model and brush model caches ── */
static std::vector<BSPStaticModelDef> s_static_models;
static std::vector<Ref<ArrayMesh>>    s_brush_models;  /* 1-based: s_brush_models[0] = submodel *1 */

/* ── Phase 78: Fog volume cache ── */

/* On-disc fog definition (dfog_t from qfiles.h, 72 bytes) */
struct bsp_fog_t {
    char    shader[64];
    int32_t brushNum;
    int32_t visibleSide;
};
static_assert(sizeof(bsp_fog_t) == 72, "bsp_fog_t must be 72 bytes");

static std::vector<BSPFogVolume> s_fog_volumes;

/* ── Phase 74: Flare cache ── */
static std::vector<BSPFlare> s_flares;

/* ── PVS cluster mesh tracking ──
 * Maps cluster index → MeshInstance3D node for per-cluster visibility toggling.
 * Built during Godot_BSP_LoadWorld(), queried each frame by MoHAARunner. */
static std::vector<godot::MeshInstance3D *> s_cluster_meshes;  /* indexed by cluster */
static int s_pvs_num_clusters = 0;


/* ===================================================================
 *  Coordinate conversion
 * ================================================================ */

static constexpr float MOHAA_UNIT_SCALE = 1.0f / 39.37f;

static inline Vector3 id_to_godot_pos(const float *v) {
    return Vector3(
        -v[1] * MOHAA_UNIT_SCALE,
         v[2] * MOHAA_UNIT_SCALE,
        -v[0] * MOHAA_UNIT_SCALE
    );
}

/* ===================================================================
 *  Lump access helper (mirrors Q_GetLumpByVersion in qfiles.h)
 * ================================================================ */

static const bsp_lump_t *get_lump(const bsp_header_t *hdr, int lump_id) {
    if (hdr->version <= 18 && lump_id > LUMP_BRUSHES) {
        // BSP ≤ 18 had LUMP_FOGS at slot 13; later versions removed it
        // and shifted everything above LUMP_BRUSHES down by one.
        return &hdr->lumps[lump_id + 1];
    }
    return &hdr->lumps[lump_id];
}

/* ===================================================================
 *  Per-shader vertex/index accumulator
 *
 *  Batches are keyed by (shaderNum, lightmapNum) so that surfaces
 *  sharing the same shader but referencing different lightmap tiles
 *  get separate materials with the correct lightmap bound.
 * ================================================================ */

struct ShaderBatch {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<Vector2> lm_uvs;     // lightmap UVs (UV2)
    std::vector<Color>   colors;
    std::vector<int32_t> indices;
    int32_t              vertex_offset = 0;
    const char          *shader_name   = nullptr;
    int                  lightmap_num  = -1;
    bool                 nolightmap    = false;
    int32_t              surface_flags = 0;
};

/// Encode (shaderNum, lightmapNum) into a single int64_t batch key.
static inline int64_t make_batch_key(int shader_num, int lightmap_num) {
    return ((int64_t)shader_num << 32) | ((int64_t)(uint32_t)lightmap_num);
}

/* ===================================================================
 *  Linear interpolation helper
 * ================================================================ */

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}


/* ===================================================================
 *  Bézier patch tessellation
 *
 *  BSP MST_PATCH surfaces store an odd×odd grid of control points
 *  (patchWidth × patchHeight).  Each 3×3 sub-grid defines a
 *  bi-quadratic Bézier patch.  We tessellate parametrically into
 *  a regular vertex grid and emit triangles.
 *
 *  Basis: B0(u)=(1-u)², B1(u)=2u(1-u), B2(u)=u²
 *  P(s,t) = Σ Bi(s)·Bj(t)·C[i][j]   for i,j ∈ {0,1,2}
 * ================================================================ */

/// Interpolated vertex attributes from Bézier evaluation.
struct BezierVert {
    Vector3 pos;
    Vector3 normal;
    Vector2 uv;
    Vector2 lm_uv;
    Color   col;
};

/// Evaluate a bi-quadratic Bézier patch at parameter (s, t) ∈ [0,1]².
/// ctrl[row][col] is indexed [0..2][0..2].
static BezierVert bezier_eval(const bsp_drawvert_t *ctrl[3][3], float s, float t) {
    // Quadratic basis functions
    float bs[3] = {
        (1.0f - s) * (1.0f - s),
        2.0f * s * (1.0f - s),
        s * s
    };
    float bt[3] = {
        (1.0f - t) * (1.0f - t),
        2.0f * t * (1.0f - t),
        t * t
    };

    BezierVert result;
    result.pos    = Vector3(0, 0, 0);
    result.normal = Vector3(0, 0, 0);
    result.uv     = Vector2(0, 0);
    result.lm_uv  = Vector2(0, 0);
    result.col    = Color(0, 0, 0, 0);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float w = bs[j] * bt[i];  // j=column(s), i=row(t)
            const bsp_drawvert_t *cv = ctrl[i][j];

            // Position: convert id→Godot
            result.pos += id_to_godot_pos(cv->xyz) * w;
            // Normal: convert direction id→Godot
            result.normal += id_to_godot_dir(cv->normal) * w;
            // Texture UVs
            result.uv  += Vector2(cv->st[0], cv->st[1]) * w;
            result.lm_uv += Vector2(cv->lightmap[0], cv->lightmap[1]) * w;
            // Vertex colour
            result.col += Color(
                cv->color[0] / 255.0f,
                cv->color[1] / 255.0f,
                cv->color[2] / 255.0f,
                cv->color[3] / 255.0f
            ) * w;
        }
    }

    // Re-normalise the interpolated normal
    float nl = result.normal.length();
    if (nl > 1e-6f) result.normal /= nl;

    return result;
}

/// Default tessellation level per Bézier sub-patch edge.
static constexpr int PATCH_TESS_LEVEL = 8;

/// Tessellate an MST_PATCH surface and append to a ShaderBatch.
/// @param surf     BSP surface header
/// @param verts    Global drawvert array
/// @param batch    Target batch to append geometry to
static void tessellate_patch(const bsp_surface_t *surf,
                             const bsp_drawvert_t *verts,
                             ShaderBatch &batch) {
    int pw = surf->patchWidth;
    int ph = surf->patchHeight;

    // patchWidth and patchHeight must be odd and ≥ 3
    if (pw < 3 || ph < 3 || (pw & 1) == 0 || (ph & 1) == 0) return;

    int num_patches_x = (pw - 1) / 2;
    int num_patches_y = (ph - 1) / 2;
    int tess = PATCH_TESS_LEVEL;

    // Total output grid size (stitched across all sub-patches)
    int grid_w = num_patches_x * tess + 1;
    int grid_h = num_patches_y * tess + 1;

    // Tessellate into a flat vertex grid
    std::vector<BezierVert> grid(grid_w * grid_h);

    for (int py = 0; py < num_patches_y; py++) {
        for (int px = 0; px < num_patches_x; px++) {
            // Build pointers to the 3×3 control points for this sub-patch
            const bsp_drawvert_t *ctrl[3][3];
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    int vert_row = py * 2 + r;
                    int vert_col = px * 2 + c;
                    ctrl[r][c] = &verts[surf->firstVert + vert_row * pw + vert_col];
                }
            }

            // Evaluate sub-patch vertices
            int start_col = px * tess;
            int start_row = py * tess;

            // Include the last row/col only for the final sub-patch
            int end_s = (px == num_patches_x - 1) ? tess : tess - 1;
            int end_t = (py == num_patches_y - 1) ? tess : tess - 1;

            for (int ti = 0; ti <= end_t; ti++) {
                for (int si = 0; si <= end_s; si++) {
                    float s = (float)si / (float)tess;
                    float t = (float)ti / (float)tess;

                    int grid_col = start_col + si;
                    int grid_row = start_row + ti;
                    grid[grid_row * grid_w + grid_col] = bezier_eval(ctrl, s, t);
                }
            }
        }
    }

    // Append tessellated vertices to the batch
    int base = batch.vertex_offset;

    for (int i = 0; i < grid_w * grid_h; i++) {
        const BezierVert &bv = grid[i];
        batch.positions.push_back(bv.pos);
        batch.normals.push_back(bv.normal);
        batch.uvs.push_back(bv.uv);
        batch.lm_uvs.push_back(bv.lm_uv);
        batch.colors.push_back(bv.col);
    }

    // Generate triangle indices: two triangles per grid quad
    for (int row = 0; row < grid_h - 1; row++) {
        for (int col = 0; col < grid_w - 1; col++) {
            int v0 = base + row * grid_w + col;
            int v1 = base + row * grid_w + col + 1;
            int v2 = base + (row + 1) * grid_w + col;
            int v3 = base + (row + 1) * grid_w + col + 1;

            // Two triangles per quad (matching engine winding)
            batch.indices.push_back(v0);
            batch.indices.push_back(v2);
            batch.indices.push_back(v1);

            batch.indices.push_back(v1);
            batch.indices.push_back(v2);
            batch.indices.push_back(v3);
        }
    }

    batch.vertex_offset += grid_w * grid_h;
}

/* ===================================================================
 *  Texture cache and loader
 *
 *  Loads images from the engine VFS (pk3 archives).  Tries .jpg
 *  first, then .tga, matching the engine's search order.
 *  Results are cached by shader name for the duration of a map load.
 * ================================================================ */

static std::unordered_map<std::string, Ref<ImageTexture>> s_texture_cache;

/// Return a cached 1x1 white texture — used as lightmap fallback for
/// nolightmap surfaces so that GLSL lightmap samplers produce white
/// (identity modulation) instead of black (unbound sampler default).
/// This mirrors the real renderer's tr.whiteImage substitution.
static Ref<ImageTexture> get_white_texture() {
    static Ref<ImageTexture> s_white;
    if (s_white.is_null()) {
        PackedByteArray wdata;
        wdata.resize(4);
        wdata.ptrw()[0] = 255; wdata.ptrw()[1] = 255;
        wdata.ptrw()[2] = 255; wdata.ptrw()[3] = 255;
        Ref<Image> wimg = Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, wdata);
        s_white = ImageTexture::create_from_image(wimg);
    }
    return s_white;
}

/// Load an image from the VFS and return a Godot ImageTexture.
/// @param shader_name  e.g. "textures/mohmain/brick01" (no extension)
/// @return  ImageTexture or empty Ref on failure.
static Ref<ImageTexture> load_texture(const char *shader_name) {
    if (!shader_name || !shader_name[0]) return Ref<ImageTexture>();

    // Check cache first
    std::string key(shader_name);
    auto it = s_texture_cache.find(key);
    if (it != s_texture_cache.end()) return it->second;

    // Strip any existing extension from the shader name
    char base[256];
    strncpy(base, shader_name, sizeof(base) - 5);
    base[sizeof(base) - 5] = '\0';
    int blen = (int)strlen(base);

    // Remove extension if present (.tga, .jpg, .bmp, etc.)
    if (blen > 4 && base[blen - 4] == '.') {
        base[blen - 4] = '\0';
        blen -= 4;
    }

    // Extensions to try in order (matching engine behaviour)
    static const char *extensions[] = { ".jpg", ".tga", nullptr };

    for (int e = 0; extensions[e]; e++) {
        char path[264];
        snprintf(path, sizeof(path), "%s%s", base, extensions[e]);

        void *raw = nullptr;
        long len = Godot_VFS_ReadFile(path, &raw);
        if (len <= 0 || !raw) continue;

        // Build PackedByteArray from raw data
        PackedByteArray buf;
        buf.resize(len);
        memcpy(buf.ptrw(), raw, len);
        Godot_VFS_FreeFile(raw);

        // Try loading via Godot's Image class
        Ref<Image> img;
        img.instantiate();
        Error err;

        if (extensions[e][1] == 'j') {
            err = img->load_jpg_from_buffer(buf);
        } else {
            err = img->load_tga_from_buffer(buf);
        }

        if (err == OK && img->get_width() > 0 && img->get_height() > 0) {
            // Generate mipmaps for better rendering quality
            img->generate_mipmaps();

            Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
            s_texture_cache[key] = tex;
            return tex;
        }
    }

    // Cache failure as empty ref to avoid repeated disk reads
    s_texture_cache[key] = Ref<ImageTexture>();
    return Ref<ImageTexture>();
}

/* ===================================================================
 *  Lightmap loader
 *
 *  Each BSP lightmap is 128×128×3 (RGB).  We create one Godot Image
 *  per lightmap, stored in a flat array indexed by lightmapNum.
 * ================================================================ */

static constexpr int LIGHTMAP_SIZE = 128;
static constexpr int LIGHTMAP_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3;

static std::vector<Ref<ImageTexture>> s_lightmaps;

/* DEBUG: set to true to replace all lightmaps with white — helps diagnose
 * whether "black marks" are caused by lightmap data.  Remove after testing. */
static bool s_debug_white_lightmaps = false;

/// Parse the LUMP_LIGHTMAPS data and create Godot ImageTexture objects.
static void load_lightmaps(const uint8_t *lm_data, int lm_len) {
    s_lightmaps.clear();
    if (!lm_data || lm_len <= 0) return;

    int count = lm_len / LIGHTMAP_BYTES;
    s_lightmaps.reserve(count);

    for (int i = 0; i < count; i++) {
        const uint8_t *src = lm_data + i * LIGHTMAP_BYTES;

        // Expand RGB→RGBA (Godot's FORMAT_RGB8 or FORMAT_RGBA8)
        PackedByteArray rgba;
        rgba.resize(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);
        uint8_t *dst = rgba.ptrw();

        for (int p = 0; p < LIGHTMAP_SIZE * LIGHTMAP_SIZE; p++) {
            // The real renderer applies R_ColorShiftLightingBytes (<< overbrightShift)
            // here.  On modern systems (no hardware gamma, windowed):
            //   overbrightBits = 0
            //   overbrightShift = r_mapOverBrightBits - overbrightBits = 1 - 0 = 1
            // So the shift is << 1 (×2) with hue-preserving clamping.
            // This bakes the overbright scaling directly into the lightmap texture.
            int r = src[p * 3 + 0] << 1;
            int g = src[p * 3 + 1] << 1;
            int b = src[p * 3 + 2] << 1;

            // Hue-preserving clamp (same as R_ColorShiftLightingBytes in tr_bsp.c)
            int max_val = r;
            if (g > max_val) max_val = g;
            if (b > max_val) max_val = b;

            if (max_val > 255) {
                r = (r * 255) / max_val;
                g = (g * 255) / max_val;
                b = (b * 255) / max_val;
            }

            dst[p * 4 + 0] = r;
            dst[p * 4 + 1] = g;
            dst[p * 4 + 2] = b;
            dst[p * 4 + 3] = 255;
        }

        Ref<Image> img = Image::create_from_data(
            LIGHTMAP_SIZE, LIGHTMAP_SIZE, false, Image::FORMAT_RGBA8, rgba
        );
        img->generate_mipmaps();

        Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
        s_lightmaps.push_back(tex);
    }

    UtilityFunctions::print(String("[BSP] Loaded ") +
                            String::num_int64(count) + " lightmaps.");
}

/* ===================================================================
 *  BSP loader implementation
 * ================================================================ */

/// Returns true if the shader name is a tool texture that should not
/// be rendered (clip planes, trigger volumes, hints, etc.).
static bool is_tool_shader(const char *name) {
    if (!name || !name[0]) return true;

    // Common tool textures in MOHAA
    static const char *skip_prefixes[] = {
        "clip",
        "textures/common/clip",
        "textures/common/trigger",
        "textures/common/hint",
        "textures/common/skip",
        "textures/common/caulk",
        "textures/common/nodraw",
        "textures/common/weaponclip",
        "textures/common/playerclip",
        "textures/common/full_clip",
        "textures/common/ai_nosight",
        nullptr
    };

    for (int i = 0; skip_prefixes[i]; i++) {
        if (strncmp(name, skip_prefixes[i], strlen(skip_prefixes[i])) == 0) {
            return true;
        }
    }

    return false;
}

/* ===================================================================
 *  Helper: convert ShaderBatch map into a Godot ArrayMesh.
 *
 *  This is shared between the world mesh and brush sub-model builders.
 *  Each ShaderBatch becomes one ArrayMesh surface with its own material.
 * ================================================================ */

static Ref<ArrayMesh> batches_to_array_mesh(
    std::unordered_map<int64_t, ShaderBatch> &batches,
    int *out_tex_loaded = nullptr,
    int *out_tex_failed = nullptr)
{
    Ref<ArrayMesh> mesh;
    mesh.instantiate();

    int surface_idx = 0;
    int tex_ok  = 0;
    int tex_bad = 0;
    for (auto &pair : batches) {
        ShaderBatch &batch = pair.second;
        if (batch.positions.empty() || batch.indices.empty()) continue;

        int nv = (int)batch.positions.size();
        int ni = (int)batch.indices.size();

        PackedVector3Array pv3_pos;
        PackedVector3Array pv3_nrm;
        PackedVector2Array pv2_uv;
        PackedVector2Array pv2_uv2;
        PackedColorArray   pc_col;
        PackedInt32Array   pi_idx;

        pv3_pos.resize(nv);
        pv3_nrm.resize(nv);
        pv2_uv.resize(nv);
        pv2_uv2.resize(nv);
        pc_col.resize(nv);
        pi_idx.resize(ni);

        memcpy(pv3_pos.ptrw(), batch.positions.data(), nv * sizeof(Vector3));
        memcpy(pv3_nrm.ptrw(), batch.normals.data(),   nv * sizeof(Vector3));
        memcpy(pv2_uv.ptrw(),  batch.uvs.data(),       nv * sizeof(Vector2));
        memcpy(pv2_uv2.ptrw(), batch.lm_uvs.data(),    nv * sizeof(Vector2));
        memcpy(pc_col.ptrw(),  batch.colors.data(),     nv * sizeof(Color));
        memcpy(pi_idx.ptrw(),  batch.indices.data(),    ni * sizeof(int32_t));

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = pv3_pos;
        arrays[Mesh::ARRAY_NORMAL] = pv3_nrm;
        arrays[Mesh::ARRAY_TEX_UV] = pv2_uv;
        arrays[Mesh::ARRAY_TEX_UV2] = pv2_uv2;
        arrays[Mesh::ARRAY_COLOR]  = pc_col;
        arrays[Mesh::ARRAY_INDEX]  = pi_idx;

        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

        // Material with texture and shader properties.
        // Multi-stage shaders use ShaderMaterial; single-stage / unknown
        // shaders fall back to StandardMaterial3D.
        Ref<Material> surface_mat;

        const GodotShaderProps *sp = batch.shader_name
            ? Godot_ShaderProps_Find(batch.shader_name) : nullptr;

        /* Check whether the shader definition includes a lightmap stage.
         * MOHAA terrain (and some surfaces) rely on the engine's rendering
         * code to apply lightmap modulation via TMU1, so their .shader
         * definitions don't include $lightmap or nextBundle $lightmap.
         * If the batch has a valid lightmap but the shader doesn't reference
         * one, skip the ShaderMaterial path and fall through to the
         * StandardMaterial3D fallback which handles lightmaps via the
         * detail texture mechanism. */
        bool shader_has_lm = false;
        if (sp) {
            for (int si = 0; si < sp->stage_count; si++) {
                if (sp->stages[si].isLightmap || sp->stages[si].hasNextBundleLightmap) {
                    shader_has_lm = true;
                    break;
                }
            }
        }
        bool batch_has_lm = !batch.nolightmap &&
                            batch.lightmap_num >= 0 &&
                            batch.lightmap_num < (int)s_lightmaps.size() &&
                            s_lightmaps[batch.lightmap_num].is_valid();
        bool skip_shader_for_lm = (sp && sp->stage_count > 0 &&
                                   batch_has_lm && !shader_has_lm);

        if (sp && sp->stage_count > 0 && !skip_shader_for_lm) {
            /* ── ShaderMaterial path (multi-stage .shader) ── */
            Ref<ShaderMaterial> smat = Godot_Shader_BuildMaterial(sp);
            if (smat.is_valid()) {
                // Bind per-stage texture uniforms.
                // Stage types are mutually exclusive: a stage is either an
                // animMap sequence, a $lightmap, or a regular texture.
                for (int si = 0; si < sp->stage_count; si++) {
                    const MohaaShaderStage *stage = &sp->stages[si];
                    if (!stage->active) continue;
                    String idx = String::num_int64(si);

                    if (stage->animMapFrameCount > 0) {
                        // animMap: bind each frame texture
                        for (int f = 0; f < stage->animMapFrameCount; f++) {
                            Ref<ImageTexture> ftex = load_texture(stage->animMapFrames[f]);
                            if (ftex.is_valid()) {
                                smat->set_shader_parameter(
                                    String("stage") + idx + "_frame" + String::num_int64(f), ftex);
                            }
                        }
                    } else if (stage->isLightmap) {
                        // $lightmap stage — bind the batch's lightmap texture,
                        // or a white fallback if the surface has no lightmap
                        // (mirrors the real renderer's tr.whiteImage).
                        if (s_debug_white_lightmaps) {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_tex",
                                get_white_texture());
                        } else if (!batch.nolightmap &&
                            batch.lightmap_num >= 0 &&
                            batch.lightmap_num < (int)s_lightmaps.size() &&
                            s_lightmaps[batch.lightmap_num].is_valid()) {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_tex",
                                s_lightmaps[batch.lightmap_num]);
                        } else {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_tex",
                                get_white_texture());
                        }
                    } else if (stage->map[0] != '\0') {
                        // Regular texture stage
                        Ref<ImageTexture> stex = load_texture(stage->map);
                        if (stex.is_valid()) {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_tex", stex);
                            tex_ok++;
                        } else {
                            tex_bad++;
                            if (batch.shader_name) {
                                printf("[BSP-TEX-FAIL] shader='%s' stage=%d map='%s'\n",
                                       batch.shader_name, si, stage->map);
                            }
                        }
                    }

                    /* nextBundle $lightmap: bind lightmap to stage<i>_lm,
                     * or white fallback for nolightmap surfaces. */
                    if (stage->hasNextBundleLightmap) {
                        if (s_debug_white_lightmaps) {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_lm",
                                get_white_texture());
                        } else if (!batch.nolightmap &&
                            batch.lightmap_num >= 0 &&
                            batch.lightmap_num < (int)s_lightmaps.size() &&
                            s_lightmaps[batch.lightmap_num].is_valid()) {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_lm",
                                s_lightmaps[batch.lightmap_num]);
                        } else {
                            smat->set_shader_parameter(
                                String("stage") + idx + "_lm",
                                get_white_texture());
                        }
                    }
                }

                smat->set_meta("shader_name", String(batch.shader_name));
                surface_mat = smat;
            }
        } else if (!sp) {
        }

        if (surface_mat.is_null()) {
            /* ── StandardMaterial3D fallback ── */
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            /* MOHAA default is CT_FRONT_SIDED (cull back faces).
             * apply_shader_props overrides to CULL_DISABLED only if the
             * shader definition explicitly says "cull none". */
            mat->set_cull_mode(BaseMaterial3D::CULL_BACK);

            /* BSP world surfaces are lit exclusively by their baked lightmap
             * (or vertex colours for nolightmap surfaces).  The real
             * OpenMOHAA renderer never applies dynamic scene lights to BSP
             * geometry.  UNSHADED prevents Godot from adding any dynamic
             * illumination (which would double-light the scene). */
            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

            bool has_texture = false;
            if (batch.shader_name) {
                Ref<ImageTexture> tex = load_texture(batch.shader_name);
                if (tex.is_valid()) {
                    mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    has_texture = true;
                    tex_ok++;
                } else {
                    tex_bad++;

                }

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
                            mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                            mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                            break;
                        default:
                            break;
                    }
                    switch (sp->cull) {
                        case SHADER_CULL_BACK:
                            mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
                            break;
                        case SHADER_CULL_FRONT:
                            mat->set_cull_mode(BaseMaterial3D::CULL_FRONT);
                            break;
                        case SHADER_CULL_NONE:
                            mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                            break;
                    }

                    // Phase 142: clampMap — disable texture repeat
                    for (int st = 0; st < sp->stage_count; st++) {
                        if (!sp->stages[st].active) continue;
                        if (sp->stages[st].isLightmap) continue;
                        if (sp->stages[st].isClampMap) {
                            mat->set_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT, false);
                        }
                        break;
                    }
                }

                mat->set_meta("shader_name", String(batch.shader_name));
            }

            if (!has_texture) {
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            }

            if (batch.nolightmap) {
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                /* Nolightmap surfaces are fullbright (sky, lava, special FX).
                 * Revert to UNSHADED so they don't receive dynamic shadows
                 * or respond to the directional light. */
                mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            }

            if (!s_debug_white_lightmaps &&
                !batch.nolightmap &&
                batch.lightmap_num >= 0 &&
                batch.lightmap_num < (int)s_lightmaps.size() &&
                s_lightmaps[batch.lightmap_num].is_valid()) {

                mat->set_flag(BaseMaterial3D::FLAG_UV2_USE_TRIPLANAR, false);
                mat->set_detail_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                mat->set_detail_uv(BaseMaterial3D::DETAIL_UV_2);
                mat->set_texture(BaseMaterial3D::TEXTURE_DETAIL_ALBEDO,
                                 s_lightmaps[batch.lightmap_num]);
                mat->set_feature(BaseMaterial3D::FEATURE_DETAIL, true);
            }

#ifdef HAS_PBR_MODULE
            // PBR enhancement: if HD PBR textures exist for this surface's shader,
            // apply normal map, roughness, and switch to lit rendering.
            if (Godot_PBR_IsEnabled() && batch.shader_name) {
                Godot_PBR_ApplyToMaterial(mat, batch.shader_name);
            }
#endif

            surface_mat = mat;
        }

        mesh->surface_set_material(surface_idx, surface_mat);
        surface_idx++;
    }

    if (out_tex_loaded) *out_tex_loaded = tex_ok;
    if (out_tex_failed) *out_tex_failed = tex_bad;

    return mesh;
}

/* ===================================================================
 *  Helper: process a single BSP surface into a ShaderBatch.
 *
 *  Handles MST_PLANAR, MST_TRIANGLE_SOUP, and MST_PATCH surfaces.
 *  Returns true if the surface was processed, false if skipped.
 * ================================================================ */

static bool process_surface(const bsp_surface_t *surf, int surf_idx,
                             const bsp_drawvert_t *verts, int num_verts,
                             const int32_t *indices, int num_indices,
                             const bsp_shader_t *shaders, int num_shaders,
                             std::unordered_map<int64_t, ShaderBatch> &batches,
                             int &skipped_nodraw, int &skipped_sky)
{
    // Skip unsupported surface types
    if (surf->surfaceType != MST_PLANAR &&
        surf->surfaceType != MST_TRIANGLE_SOUP &&
        surf->surfaceType != MST_PATCH) {
        return false;
    }

    // Filter tool/invisible shaders
    if (surf->shaderNum >= 0 && surf->shaderNum < num_shaders) {
        const bsp_shader_t *sh = &shaders[surf->shaderNum];
        if (sh->surfaceFlags & SURF_SKY)    { skipped_sky++;    return false; }
        if (sh->surfaceFlags & SURF_NODRAW) { skipped_nodraw++; return false; }
        if (sh->surfaceFlags & SURF_HINT)   { skipped_nodraw++; return false; }
        if (is_tool_shader(sh->shader))      { skipped_nodraw++; return false; }
    }

    // Bounds checks
    if (surf->firstVert < 0 || surf->firstVert + surf->numVerts > num_verts) return false;
    if (surf->numVerts <= 0) return false;
    if (surf->surfaceType != MST_PATCH) {
        if (surf->firstIndex < 0 || surf->firstIndex + surf->numIndexes > num_indices) return false;
        if (surf->numIndexes <= 0) return false;
    }

    // Get or create batch — keyed by (shaderNum, lightmapNum) so that
    // surfaces sharing the same shader but referencing different lightmap
    // tiles get separate materials with the correct lightmap bound.
    int64_t bkey = make_batch_key(surf->shaderNum, surf->lightmapNum);
    ShaderBatch &batch = batches[bkey];
    if (!batch.shader_name && surf->shaderNum >= 0 && surf->shaderNum < num_shaders) {
        batch.shader_name = shaders[surf->shaderNum].shader;
        batch.surface_flags = shaders[surf->shaderNum].surfaceFlags;
        if (shaders[surf->shaderNum].surfaceFlags & SURF_NOLIGHTMAP) {
            batch.nolightmap = true;
        }
    }
    batch.lightmap_num = surf->lightmapNum;

    // Handle Bézier patches
    if (surf->surfaceType == MST_PATCH) {
        tessellate_patch(surf, verts, batch);
        return true;
    }

    // Handle planar faces and triangle soups
    int base = batch.vertex_offset;

    for (int v = 0; v < surf->numVerts; v++) {
        const bsp_drawvert_t *dv = &verts[surf->firstVert + v];
        batch.positions.push_back(id_to_godot_pos(dv->xyz));
        batch.normals.push_back(id_to_godot_dir(dv->normal));
        batch.uvs.push_back(Vector2(dv->st[0], dv->st[1]));
        batch.lm_uvs.push_back(Vector2(dv->lightmap[0], dv->lightmap[1]));
        batch.colors.push_back(Color(
            dv->color[0] / 255.0f, dv->color[1] / 255.0f,
            dv->color[2] / 255.0f, dv->color[3] / 255.0f));
    }

    for (int i = 0; i < surf->numIndexes; i++) {
        int idx = indices[surf->firstIndex + i];
        batch.indices.push_back(base + idx);
    }

    batch.vertex_offset += surf->numVerts;
    return true;
}

godot::Node3D *Godot_BSP_LoadWorld(const char *bsp_path) {
    if (!bsp_path || !bsp_path[0]) {
        UtilityFunctions::printerr("[BSP] No BSP path provided.");
        return nullptr;
    }

    // ── 0. Load shader transparency properties ──
    Godot_ShaderProps_Load();

    // ── 1. Read BSP file via engine VFS ──
    void *raw = nullptr;
    long file_len = Godot_VFS_ReadFile(bsp_path, &raw);
    if (file_len <= 0 || !raw) {
        UtilityFunctions::printerr(String("[BSP] Failed to read: ") + bsp_path);
        return nullptr;
    }

    uint8_t *data = static_cast<uint8_t *>(raw);

    UtilityFunctions::print(String("[BSP] Loaded ") + bsp_path +
                            " (" + String::num_int64(file_len) + " bytes)");

    // ── 2. Parse and validate header ──
    if (file_len < (long)sizeof(bsp_header_t)) {
        UtilityFunctions::printerr("[BSP] File too small for header.");
        Godot_VFS_FreeFile(raw);
        return nullptr;
    }

    const bsp_header_t *hdr = reinterpret_cast<const bsp_header_t *>(data);

    if (hdr->ident != BSP_IDENT) {
        UtilityFunctions::printerr(String("[BSP] Bad ident: ") +
                                   String::num_int64(hdr->ident) +
                                   " (expected 'MOHAA 2015')");
        Godot_VFS_FreeFile(raw);
        return nullptr;
    }

    if (hdr->version < BSP_MIN_VERSION || hdr->version > BSP_MAX_VERSION) {
        UtilityFunctions::printerr(String("[BSP] Unsupported version: ") +
                                   String::num_int64(hdr->version));
        Godot_VFS_FreeFile(raw);
        return nullptr;
    }

    UtilityFunctions::print(String("[BSP] Version ") + String::num_int64(hdr->version) +
                            ", checksum " + String::num_int64(hdr->checksum));

    // ── 3. Locate lumps ──
    const bsp_lump_t *l_shaders  = get_lump(hdr, LUMP_SHADERS);
    const bsp_lump_t *l_verts    = get_lump(hdr, LUMP_DRAWVERTS);
    const bsp_lump_t *l_indices  = get_lump(hdr, LUMP_DRAWINDEXES);
    const bsp_lump_t *l_surfaces = get_lump(hdr, LUMP_SURFACES);
    const bsp_lump_t *l_lmaps   = get_lump(hdr, LUMP_LIGHTMAPS);

    // Validate lump bounds
    auto lump_ok = [&](const bsp_lump_t *l) -> bool {
        return l->fileofs >= 0 && l->filelen >= 0 &&
               (l->fileofs + l->filelen) <= file_len;
    };

    if (!lump_ok(l_shaders) || !lump_ok(l_verts) ||
        !lump_ok(l_indices) || !lump_ok(l_surfaces)) {
        UtilityFunctions::printerr("[BSP] Lump bounds out of range.");
        Godot_VFS_FreeFile(raw);
        return nullptr;
    }

    // ── 4. Access raw lump data ──
    const bsp_shader_t  *shaders  = reinterpret_cast<const bsp_shader_t *>(data + l_shaders->fileofs);
    int num_shaders  = l_shaders->filelen / (int)sizeof(bsp_shader_t);

    const bsp_drawvert_t *verts   = reinterpret_cast<const bsp_drawvert_t *>(data + l_verts->fileofs);
    int num_verts    = l_verts->filelen / (int)sizeof(bsp_drawvert_t);

    const int32_t *indices = reinterpret_cast<const int32_t *>(data + l_indices->fileofs);
    int num_indices  = l_indices->filelen / (int)sizeof(int32_t);

    const bsp_surface_t *surfaces = reinterpret_cast<const bsp_surface_t *>(data + l_surfaces->fileofs);
    int num_surfaces = l_surfaces->filelen / (int)sizeof(bsp_surface_t);

    UtilityFunctions::print(String("[BSP] Shaders: ") + String::num_int64(num_shaders) +
                            ", Verts: " + String::num_int64(num_verts) +
                            ", Indices: " + String::num_int64(num_indices) +
                            ", Surfaces: " + String::num_int64(num_surfaces));

    // ── 4b. Load lightmaps ──
    if (lump_ok(l_lmaps) && l_lmaps->filelen > 0) {
        load_lightmaps(data + l_lmaps->fileofs, l_lmaps->filelen);
    }

    // ── 4c. Parse LUMP_MODELS — identify brush sub-model surface ranges ──
    //
    // dmodel_t[0] = world model (all surfaces).
    // dmodel_t[1..N] = inline brush models (*1, *2, ...) used by doors/movers.
    // We track which surfaces belong to sub-models so we can:
    //   (a) exclude them from the world mesh (they render at entity positions)
    //   (b) build separate ArrayMesh objects for each sub-model.
    const bsp_lump_t *l_models = get_lump(hdr, LUMP_MODELS);
    const bsp_model_t *bsp_models = nullptr;
    int num_models = 0;
    std::vector<bool> is_submodel_surface(num_surfaces, false);

    s_brush_models.clear();

    if (lump_ok(l_models) && l_models->filelen >= (int)sizeof(bsp_model_t)) {
        bsp_models = reinterpret_cast<const bsp_model_t *>(data + l_models->fileofs);
        num_models = l_models->filelen / (int)sizeof(bsp_model_t);

        // Mark surfaces belonging to sub-models 1..N
        for (int m = 1; m < num_models; m++) {
            int first = bsp_models[m].firstSurface;
            int count = bsp_models[m].numSurfaces;
            for (int si = first; si < first + count && si < num_surfaces; si++) {
                if (si >= 0) is_submodel_surface[si] = true;
            }
        }

        UtilityFunctions::print(String("[BSP] Models: ") +
                                String::num_int64(num_models) +
                                " (1 world + " +
                                String::num_int64(num_models - 1) +
                                " brush sub-models)");
    }

    // 4d/4e. Mark data and world metadata are now provided by
    //    renderergl1/godot_bsp_accessors.c reading from tr.world directly.
    //    No BSP raw data retention is needed here.
    // ── 4f. Phase 78: Parse fog volumes (LUMP_FOGS, BSP ≤ 18 only) ──
    //
    // LUMP_FOGS was at slot 13 in BSP versions ≤ 18 and was removed in
    // version 19+.  Each fog entry references a brush and a shader that
    // defines the fog colour and density.
    static constexpr int BSP_LAST_VERSION_WITH_FOGS = 18;
    static constexpr int LUMP_FOGS_SLOT = 13;  /* raw lump slot for fog in BSP ≤ 18 */

    s_fog_volumes.clear();
    if (hdr->version <= BSP_LAST_VERSION_WITH_FOGS) {
        const bsp_lump_t *l_fogs = &hdr->lumps[LUMP_FOGS_SLOT];
        if (l_fogs->fileofs >= 0 && l_fogs->filelen >= (int)sizeof(bsp_fog_t) &&
            (l_fogs->fileofs + l_fogs->filelen) <= file_len) {
            const bsp_fog_t *fog_data =
                reinterpret_cast<const bsp_fog_t *>(data + l_fogs->fileofs);
            int num_fogs = l_fogs->filelen / (int)sizeof(bsp_fog_t);

            for (int fi = 0; fi < num_fogs; fi++) {
                BSPFogVolume fv;
                memset(&fv, 0, sizeof(fv));
                memcpy(fv.shader, fog_data[fi].shader, sizeof(fv.shader));
                fv.shader[sizeof(fv.shader) - 1] = '\0';
                fv.brushNum = fog_data[fi].brushNum;
                fv.visibleSide = fog_data[fi].visibleSide;

                /* Look up fog colour and distance from shader props */
                const GodotShaderProps *fsp = Godot_ShaderProps_Find(fv.shader);
                if (fsp && fsp->has_fog) {
                    fv.color[0] = fsp->fog_color[0];
                    fv.color[1] = fsp->fog_color[1];
                    fv.color[2] = fsp->fog_color[2];
                    fv.depthForOpaque = fsp->fog_distance;
                } else {
                    /* Default grey fog */
                    fv.color[0] = fv.color[1] = fv.color[2] = 0.5f;
                    fv.depthForOpaque = 500.0f;
                }

                s_fog_volumes.push_back(fv);
            }

            if (!s_fog_volumes.empty()) {
                UtilityFunctions::print(String("[BSP] Parsed ") +
                    String::num_int64((int64_t)s_fog_volumes.size()) +
                    " fog volumes from LUMP_FOGS.");
            }
        }
    }

    // ── 5. Build surface-to-cluster mapping from leaf data ──
    //
    // Walk all BSP leaves and build a mapping from surface index to the
    // PVS cluster that "owns" it. A surface may be referenced by multiple
    // leaves; we assign it to the first cluster encountered. Surfaces with
    // no cluster assignment (cluster -1 or not referenced) go into a
    // special "always-visible" group (cluster -1).
    std::vector<int> surface_cluster(num_surfaces, -1);  // -1 = unassigned

    Godot_BSP_BuildSurfaceClusterMap(surface_cluster.data(), num_surfaces);
    // ── 5b. Accumulate world surfaces into per-cluster, per-shader batches ──
    // Key: cluster → {(shaderNum,lightmapNum) → ShaderBatch}
    // Cluster -1 is the "always visible" fallback group.
    std::unordered_map<int, std::unordered_map<int64_t, ShaderBatch>> cluster_batches;

    int skipped_nodraw = 0;
    int skipped_type   = 0;
    int skipped_sky    = 0;
    int skipped_bmodel = 0;
    int processed      = 0;
    int processed_patches = 0;

    /* Phase 74: Collect flare surface positions */
    s_flares.clear();

    for (int s = 0; s < num_surfaces; s++) {
        const bsp_surface_t *surf = &surfaces[s];

        // Skip surfaces belonging to brush sub-models — they render at
        // entity positions, not at the world origin.
        if (is_submodel_surface[s]) {
            skipped_bmodel++;
            continue;
        }

        /* Phase 74: Collect MST_FLARE surfaces as flare definitions */
        if (surf->surfaceType == MST_FLARE) {
            if (surf->shaderNum >= 0 && surf->shaderNum < num_shaders &&
                surf->numVerts > 0 && surf->firstVert >= 0 &&
                surf->firstVert < num_verts) {
                const bsp_drawvert_t *fv = &verts[surf->firstVert];
                BSPFlare flare;
                memset(&flare, 0, sizeof(flare));
                /* Convert position to Godot coordinates */
                Vector3 gpos = id_to_godot_pos(fv->xyz);
                flare.origin[0] = gpos.x;
                flare.origin[1] = gpos.y;
                flare.origin[2] = gpos.z;
                flare.color[0] = fv->color[0] / 255.0f;
                flare.color[1] = fv->color[1] / 255.0f;
                flare.color[2] = fv->color[2] / 255.0f;
                memcpy(flare.shader, shaders[surf->shaderNum].shader, sizeof(flare.shader));
                flare.shader[sizeof(flare.shader) - 1] = '\0';
                s_flares.push_back(flare);
            }
            skipped_type++;
            continue;
        }

        // Skip unsupported surface types
        if (surf->surfaceType != MST_PLANAR &&
            surf->surfaceType != MST_TRIANGLE_SOUP &&
            surf->surfaceType != MST_PATCH) {
            skipped_type++;
            continue;
        }

        /* Phase 73: Skip portal surfaces — render distinctly elsewhere */
        if (surf->shaderNum >= 0 && surf->shaderNum < num_shaders) {
            const GodotShaderProps *sp = Godot_ShaderProps_Find(
                shaders[surf->shaderNum].shader);
            if (sp && sp->is_portal) {
                /* Portal surfaces are skipped from world geometry;
                 * Agent 10 renders them as flat reflective surfaces. */
                skipped_type++;
                continue;
            }
        }

        if (process_surface(surf, s, verts, num_verts, indices, num_indices,
                            shaders, num_shaders,
                            cluster_batches[surface_cluster[s]],
                            skipped_nodraw, skipped_sky)) {
            if (surf->surfaceType == MST_PATCH) processed_patches++;
            processed++;
        }
    }

    if (!s_flares.empty()) {
        UtilityFunctions::print(String("[BSP] Collected ") +
            String::num_int64((int64_t)s_flares.size()) + " flare surfaces.");
    }

    // ── 5c. Load and process terrain patches (LUMP_TERRAIN) ──
    //
    // MOHAA terrain is a separate system from BSP surfaces. Each patch
    // covers 512×512 world units with a 9×9 heightmap grid.  We render
    // at maximum detail (8×8 quads = 128 triangles per patch), skipping
    // the ROAM LOD tessellation.
    int terrain_processed = 0;

    const bsp_lump_t *l_terrain = get_lump(hdr, LUMP_TERRAIN);
    if (lump_ok(l_terrain) && l_terrain->filelen > 0) {
        const uint8_t *terrain_data = data + l_terrain->fileofs;
        int num_patches = l_terrain->filelen / (int)sizeof(bsp_terrain_patch_t);

        UtilityFunctions::print(String("[BSP] Terrain lump: ") +
                                String::num_int64(l_terrain->filelen) +
                                " bytes, " + String::num_int64(num_patches) +
                                " patches (" + String::num_int64((int64_t)sizeof(bsp_terrain_patch_t)) +
                                " bytes each)");

        for (int p = 0; p < num_patches; p++) {
            const bsp_terrain_patch_t *patch =
                reinterpret_cast<const bsp_terrain_patch_t *>(
                    terrain_data + p * sizeof(bsp_terrain_patch_t));

            // Validate shader index
            if (patch->iShader >= (uint16_t)num_shaders) continue;

            // Filter tool/invisible shaders
            const bsp_shader_t *sh = &shaders[patch->iShader];
            if (sh->surfaceFlags & SURF_NODRAW) continue;
            if (sh->surfaceFlags & SURF_SKY) continue;
            if (is_tool_shader(sh->shader)) continue;

            // Skip patches with invalid lightmap scale
            if (patch->lmapScale == 0) continue;

            // Compute world-space origin
            float x0 = (float)((int)patch->x << 6);
            float y0 = (float)((int)patch->y << 6);
            float z0 = (float)patch->iBaseHeight;

            // Determine PVS cluster for this terrain patch from its centre
            float terrain_centre[3] = { x0 + 256.0f, y0 + 256.0f, z0 + 128.0f };
            int terrain_cluster = Godot_BSP_PointCluster(terrain_centre);

            // Get or create shader batch for this cluster — keyed by
            // (shaderNum, lightmapNum) to avoid lightmap tile mismatches.
            int terrain_lm = (int)patch->iLightMap;
            int64_t tkey = make_batch_key(patch->iShader, terrain_lm);
            ShaderBatch &batch = cluster_batches[terrain_cluster][tkey];
            if (!batch.shader_name) {
                batch.shader_name = sh->shader;
            }
            batch.lightmap_num = terrain_lm;
            // Corner diffuse texture coordinates
            //   [0][0] = SW (col=0,row=0)
            //   [0][1] = NW (col=0,row=8)
            //   [1][0] = SE (col=8,row=0)
            //   [1][1] = NE (col=8,row=8)
            //
            // The original engine (tr_terrain.c) normalises these by
            // subtracting floor(min(corners)) so values start near zero.
            // This prevents floating-point precision loss at large UV
            // offsets.  texture repeat handles the integer wrap.
            float raw_s00 = patch->texCoord[0][0][0], raw_t00 = patch->texCoord[0][0][1];
            float raw_s01 = patch->texCoord[0][1][0], raw_t01 = patch->texCoord[0][1][1];
            float raw_s10 = patch->texCoord[1][0][0], raw_t10 = patch->texCoord[1][0][1];
            float raw_s11 = patch->texCoord[1][1][0], raw_t11 = patch->texCoord[1][1][1];

            // Compute sMin: floor of minimum S across all four corners
            float sMin_a = (raw_s00 < raw_s01) ? raw_s00 : raw_s01;
            float sMin_b = (raw_s10 < raw_s11) ? raw_s10 : raw_s11;
            float sMin = floorf((sMin_a < sMin_b) ? sMin_a : sMin_b);

            // Compute tMin: floor of minimum T across all four corners
            float tMin_a = (raw_t00 < raw_t01) ? raw_t00 : raw_t01;
            float tMin_b = (raw_t10 < raw_t11) ? raw_t10 : raw_t11;
            float tMin = floorf((tMin_a < tMin_b) ? tMin_a : tMin_b);

            float s00 = raw_s00 - sMin, t00 = raw_t00 - tMin;
            float s01 = raw_s01 - sMin, t01 = raw_t01 - tMin;
            float s10 = raw_s10 - sMin, t10 = raw_t10 - tMin;
            float s11 = raw_s11 - sMin, t11 = raw_t11 - tMin;

            // Lightmap UV parameters
            float lm_s_norm = ((float)patch->lm_s + 0.5f) / 128.0f;
            float lm_t_norm = ((float)patch->lm_t + 0.5f) / 128.0f;
            int lmapSize = patch->lmapScale * 8 + 1;
            float lm_ext = (float)(lmapSize - 1) / 128.0f;

            int base = batch.vertex_offset;

            // Generate 9×9 = 81 vertices
            for (int row = 0; row < 9; row++) {
                for (int col = 0; col < 9; col++) {
                    // World-space position
                    float vx = x0 + col * 64.0f;
                    float vy = y0 + row * 64.0f;
                    float vz = z0 + patch->heightmap[row * 9 + col] * 2.0f;

                    float engine_pos[3] = {vx, vy, vz};
                    batch.positions.push_back(id_to_godot_pos(engine_pos));

                    // Smooth normal from heightmap
                    batch.normals.push_back(
                        compute_terrain_normal(patch->heightmap, col, row));

                    // Bilinear interpolation of diffuse UVs from corners
                    float u_frac = col / 8.0f;
                    float v_frac = row / 8.0f;
                    float u = lerpf(lerpf(s00, s10, u_frac),
                                    lerpf(s01, s11, u_frac), v_frac);
                    float v = lerpf(lerpf(t00, t10, u_frac),
                                    lerpf(t01, t11, u_frac), v_frac);
                    batch.uvs.push_back(Vector2(u, v));

                    // Lightmap UVs — linear mapping within the page region
                    float lmu = lm_s_norm + u_frac * lm_ext;
                    float lmv = lm_t_norm + v_frac * lm_ext;
                    batch.lm_uvs.push_back(Vector2(lmu, lmv));

                    // Vertex colour (terrain uses white)
                    batch.colors.push_back(Color(1.0f, 1.0f, 1.0f, 1.0f));
                }
            }

            // Generate 8×8 quads = 128 triangles (256 index entries)
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    int i0 = base + row * 9 + col;
                    int i1 = i0 + 1;
                    int i2 = i0 + 9;
                    int i3 = i2 + 1;

                    // Two triangles per quad (CCW winding for Godot)
                    batch.indices.push_back(i0);
                    batch.indices.push_back(i2);
                    batch.indices.push_back(i1);

                    batch.indices.push_back(i1);
                    batch.indices.push_back(i2);
                    batch.indices.push_back(i3);
                }
            }

            batch.vertex_offset += 81;
            terrain_processed++;
        }
    }

    // Count total shader batches across all clusters
    int total_shader_batches = 0;
    for (auto &cp : cluster_batches)
        total_shader_batches += (int)cp.second.size();

    UtilityFunctions::print(String("[BSP] Processed ") + String::num_int64(processed) +
                            " surfaces (" + String::num_int64(processed_patches) +
                            " patches tessellated), " +
                            String::num_int64(terrain_processed) +
                            " terrain patches, skipped " +
                            String::num_int64(skipped_nodraw) +
                            " (nodraw/tool), " + String::num_int64(skipped_sky) +
                            " (sky), " + String::num_int64(skipped_type) +
                            " (flare), " + String::num_int64(skipped_bmodel) +
                            " (brushmodel). " +
                            String::num_int64((int64_t)cluster_batches.size()) +
                            " clusters, " +
                            String::num_int64(total_shader_batches) + " shader batches.");

    if (cluster_batches.empty()) {
        UtilityFunctions::printerr("[BSP] No renderable surfaces found.");
        Godot_VFS_FreeFile(raw);
        return nullptr;
    }

    // ── 6. Create per-cluster Godot ArrayMesh nodes for PVS culling ──
    //
    // Each PVS cluster gets its own MeshInstance3D so that MoHAARunner
    // can toggle visibility per-cluster each frame based on the camera's
    // PVS bitset. Cluster -1 is the "always-visible" fallback group.
    int tex_loaded = 0, tex_failed = 0;
    int clusters_with_geometry = 0;

    // Determine the highest cluster index for the lookup table
    int max_cluster = -1;
    for (auto &cp : cluster_batches) {
        if (cp.first > max_cluster) max_cluster = cp.first;
    }
    s_pvs_num_clusters = (max_cluster >= 0) ? (max_cluster + 1) : 0;
    s_cluster_meshes.clear();
    s_cluster_meshes.resize(s_pvs_num_clusters, nullptr);

    // ── 6b. Build brush sub-model meshes ──
    //
    // For each sub-model *1..*N, collect its surfaces into per-shader
    // batches and build a separate ArrayMesh.  These meshes are positioned
    // at entity origins by MoHAARunner::update_entities().
    if (num_models > 1) {
        s_brush_models.resize(num_models - 1);
        int bmodels_built = 0;

        for (int m = 1; m < num_models; m++) {
            int first = bsp_models[m].firstSurface;
            int count = bsp_models[m].numSurfaces;

            std::unordered_map<int64_t, ShaderBatch> bm_batches;
            int bm_skip_nd = 0, bm_skip_sky = 0;

            for (int si = first; si < first + count && si < num_surfaces; si++) {
                if (si < 0) continue;
                const bsp_surface_t *surf = &surfaces[si];
                process_surface(surf, si, verts, num_verts, indices, num_indices,
                                shaders, num_shaders, bm_batches,
                                bm_skip_nd, bm_skip_sky);
            }

            if (!bm_batches.empty()) {
                s_brush_models[m - 1] = batches_to_array_mesh(bm_batches);
                bmodels_built++;
            }
        }

        UtilityFunctions::print(String("[BSP] Built ") +
                                String::num_int64(bmodels_built) +
                                " brush sub-model meshes.");
    }

    // ── 6c. Parse LUMP_STATICMODELDEF — static TIKI model placements ──
    //
    // Static models are pre-placed instances of TIKI skeletal models
    // (trees, furniture, debris, etc.) baked into the BSP.  We parse
    // their definitions here; MoHAARunner instantiates them after the
    // BSP is added to the scene.
    s_static_models.clear();

    const bsp_lump_t *l_staticdefs = get_lump(hdr, LUMP_STATICMODELDEF);
    if (lump_ok(l_staticdefs) && l_staticdefs->filelen >= (int)sizeof(bsp_static_model_t)) {
        const bsp_static_model_t *sm_data =
            reinterpret_cast<const bsp_static_model_t *>(data + l_staticdefs->fileofs);
        int num_static = l_staticdefs->filelen / (int)sizeof(bsp_static_model_t);

        s_static_models.reserve(num_static);

        for (int i = 0; i < num_static; i++) {
            BSPStaticModelDef def;
            memcpy(def.model, sm_data[i].model, 128);
            def.model[127] = '\0';  // ensure null termination

            // Byte-swap from little-endian (matches R_CopyStaticModel)
            def.origin[0] = sm_data[i].origin[0];
            def.origin[1] = sm_data[i].origin[1];
            def.origin[2] = sm_data[i].origin[2];
            def.angles[0] = sm_data[i].angles[0];
            def.angles[1] = sm_data[i].angles[1];
            def.angles[2] = sm_data[i].angles[2];
            def.scale      = sm_data[i].scale;

            s_static_models.push_back(def);
        }

        UtilityFunctions::print(String("[BSP] Parsed ") +
                                String::num_int64(num_static) +
                                " static model definitions.");
    }

    // ── 7. Create scene tree with per-cluster MeshInstance3D nodes ──
    Node3D *root = memnew(Node3D);
    root->set_name("BSPMap");

    Node3D *cluster_root = memnew(Node3D);
    cluster_root->set_name("ClusterGeometry");
    root->add_child(cluster_root);

    // Always-visible group (cluster -1): surfaces not in any PVS cluster
    MeshInstance3D *always_visible_mi = nullptr;
    auto it_fallback = cluster_batches.find(-1);
    if (it_fallback != cluster_batches.end() && !it_fallback->second.empty()) {
        int tl = 0, tf = 0;
        Ref<ArrayMesh> fb_mesh = batches_to_array_mesh(it_fallback->second, &tl, &tf);
        tex_loaded += tl; tex_failed += tf;
        if (fb_mesh.is_valid() && fb_mesh->get_surface_count() > 0) {
            always_visible_mi = memnew(MeshInstance3D);
            always_visible_mi->set_name("Cluster_AlwaysVisible");
            always_visible_mi->set_mesh(fb_mesh);
            cluster_root->add_child(always_visible_mi);
            clusters_with_geometry++;
        }
    }

    // Per-cluster meshes
    for (auto &cp : cluster_batches) {
        int cluster = cp.first;
        if (cluster < 0) continue;  // already handled above

        auto &shader_batches = cp.second;
        if (shader_batches.empty()) continue;

        int tl = 0, tf = 0;
        Ref<ArrayMesh> cmesh = batches_to_array_mesh(shader_batches, &tl, &tf);
        tex_loaded += tl; tex_failed += tf;
        if (!cmesh.is_valid() || cmesh->get_surface_count() == 0) continue;

        MeshInstance3D *cmi = memnew(MeshInstance3D);
        cmi->set_name(String("Cluster_") + String::num_int64(cluster));
        cmi->set_mesh(cmesh);
        cluster_root->add_child(cmi);

        if (cluster < s_pvs_num_clusters)
            s_cluster_meshes[cluster] = cmi;

        clusters_with_geometry++;
    }

    UtilityFunctions::print(String("[BSP] Created ") +
                            String::num_int64(clusters_with_geometry) +
                            " cluster mesh nodes. " +
                            "Textures loaded: " + String::num_int64(tex_loaded) +
                            ", missing: " + String::num_int64(tex_failed));

    // ── 8. Free BSP data ──
    Godot_VFS_FreeFile(raw);

    UtilityFunctions::print("[BSP] World geometry loaded.");
    return root;
}

void Godot_BSP_Unload() {
    s_texture_cache.clear();
    s_lightmaps.clear();
    s_static_models.clear();
    s_brush_models.clear();
    s_fog_volumes.clear();
    s_flares.clear();
    s_cluster_meshes.clear();  // PVS cluster references (nodes owned by scene tree)
    s_pvs_num_clusters = 0;
    Godot_ShaderProps_Unload();
}

/* ===================================================================
 *  Static model accessors
 * ================================================================ */

int Godot_BSP_GetStaticModelCount() {
    return (int)s_static_models.size();
}

const BSPStaticModelDef *Godot_BSP_GetStaticModelDef(int index) {
    if (index < 0 || index >= (int)s_static_models.size()) return nullptr;
    return &s_static_models[index];
}

/* ===================================================================
 *  Brush sub-model accessors
 * ================================================================ */

int Godot_BSP_GetBrushModelCount() {
    return (int)s_brush_models.size();
}

Ref<ArrayMesh> Godot_BSP_GetBrushModelMesh(int submodelIndex) {
    /* submodelIndex is 1-based (matching *1, *2, etc.) */
    int arrIdx = submodelIndex - 1;
    if (arrIdx < 0 || arrIdx >= (int)s_brush_models.size()) return Ref<ArrayMesh>();
    return s_brush_models[arrIdx];
}

/* ===================================================================
 *  Phase 78: Fog volume accessors
 * ================================================================ */

int Godot_BSP_GetFogVolumeCount() {
    return (int)s_fog_volumes.size();
}

const BSPFogVolume *Godot_BSP_GetFogVolume(int index) {
    if (index < 0 || index >= (int)s_fog_volumes.size()) return nullptr;
    return &s_fog_volumes[index];
}

/* ===================================================================
 *  Phase 74: Flare accessors
 * ================================================================ */

int Godot_BSP_GetFlareCount() {
    return (int)s_flares.size();
}

const BSPFlare *Godot_BSP_GetFlare(int index) {
    if (index < 0 || index >= (int)s_flares.size()) return nullptr;
    return &s_flares[index];
}

/* ===================================================================
 *  PVS cluster mesh accessors
 *
 *  Called by MoHAARunner each frame to toggle per-cluster visibility
 *  based on the camera's PVS bitset.
 * ================================================================ */

int Godot_BSP_GetPVSNumClusters() {
    return s_pvs_num_clusters;
}

godot::MeshInstance3D *Godot_BSP_GetClusterMesh(int cluster) {
    if (cluster < 0 || cluster >= (int)s_cluster_meshes.size()) return nullptr;
    return s_cluster_meshes[cluster];
}
/* C API functions and mark fragment implementation moved to
 * renderergl1/godot_bsp_accessors.c which reads from tr.world directly. */
