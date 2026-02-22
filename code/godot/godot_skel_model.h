/*
 * godot_skel_model.h — Skeletal model cache for the Godot GDExtension.
 *
 * Builds Godot ArrayMesh instances from engine TIKI/SKD skeletal model
 * data.  Meshes are cached per hModel so geometry is only extracted once.
 *
 * Phase 9  — Skeletal model rendering.
 * Phase 59 — Entity LOD system (distance-based vertex collapse).
 */

#ifndef GODOT_SKEL_MODEL_H
#define GODOT_SKEL_MODEL_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/string.hpp>

#include <unordered_map>
#include <vector>

using namespace godot;

class GodotSkelModelCache {
public:
    /* Per-surface shader name + material state */
    struct SurfaceInfo {
        String shader_name;   /* e.g. "models/weapons/m1_garand/gun" */
    };

    /* Cached model = mesh + per-surface shader names + scale */
    struct CachedModel {
        Ref<ArrayMesh>            mesh;
        std::vector<SurfaceInfo>  surfaces;
        float                     tiki_scale;    /* dtiki_t::load_scale */
    };

    /* Singleton access */
    static GodotSkelModelCache &get();

    /* Build (or return cached) mesh for a TIKI model handle.
     * Returns nullptr if hModel is invalid or not a TIKI model. */
    const CachedModel *get_model(int hModel);

    /* Clear the cache (call on map change / shutdown). */
    void clear();

private:
    std::unordered_map<int, CachedModel> cache_;

    /* Build a new CachedModel from engine data.  Returns nullptr on failure. */
    CachedModel *build_model(int hModel);
};

/* ── Phase 59: LOD utility functions ── */

/*
 * Godot_Skel_SelectLodLevel — select LOD level based on distance.
 *
 * @param tikiPtr   dtiki_t pointer for the model.
 * @param meshIndex Which mesh in the TIKI (usually 0).
 * @param distance  Distance from camera to entity in id Tech 3 units (inches).
 * @return LOD level (0 = highest detail, higher = less detail).
 *
 * Reads skelHeaderGame_t::lodIndex[] to determine the appropriate
 * vertex count limit.  Returns 0 (full detail) if LOD data is absent.
 */
int Godot_Skel_SelectLodLevel(void *tikiPtr, int meshIndex, float distance);

/*
 * Godot_Skel_GetLodVertexLimit — get vertex count limit for a LOD level.
 *
 * @param tikiPtr   dtiki_t pointer.
 * @param meshIndex Mesh index.
 * @param lodLevel  LOD level from Godot_Skel_SelectLodLevel().
 * @return Maximum vertex count, or -1 if LOD data is absent (use all verts).
 */
int Godot_Skel_GetLodVertexLimit(void *tikiPtr, int meshIndex, int lodLevel);

/*
 * Godot_Skel_BuildLodMesh — build an ArrayMesh with LOD vertex collapse.
 *
 * Creates a reduced-detail mesh by applying the progressive mesh collapse
 * tables (pCollapse/pCollapseIndex) up to the specified vertex limit.
 *
 * @param tikiPtr       dtiki_t pointer.
 * @param meshIndex     Mesh index.
 * @param surfIndex     Surface index within the mesh.
 * @param maxVerts      Maximum vertex count from LOD selection.
 * @param positions     Vertex positions (id-space, numVerts*3 floats).
 * @param normals       Vertex normals (id-space, numVerts*3 floats).
 * @param texcoords     UVs (numVerts*2 floats).
 * @param numVerts      Total vertex count of full-detail surface.
 * @param indices       Triangle indices (numTris*3 ints).
 * @param numTris       Total triangle count.
 * @param tikiScale     Model scale factor.
 * @param outIndices    Output: collapsed triangle indices.
 * @param outNumTris    Output: number of triangles after collapse.
 * @return 1 on success, 0 if collapse data unavailable (use full detail).
 */
int Godot_Skel_BuildLodMesh(void *tikiPtr, int meshIndex, int surfIndex,
                             int maxVerts,
                             const float *positions, const float *normals,
                             const float *texcoords, int numVerts,
                             const int *indices, int numTris,
                             float tikiScale,
                             int *outIndices, int *outNumTris);

#endif /* GODOT_SKEL_MODEL_H */
