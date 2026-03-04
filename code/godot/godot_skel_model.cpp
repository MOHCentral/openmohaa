/*
 * godot_skel_model.cpp — Skeletal model cache for the Godot GDExtension.
 *
 * Reads engine TIKI/SKD skeletal model data via C accessors
 * (godot_skel_model_accessors.cpp) and builds Godot ArrayMesh instances
 * with correct geometry, normals, and UV coordinates.
 *
 * Coordinate conversion:
 *   Godot.x = -id.Y    Godot.y = id.Z    Godot.z = -id.X
 *   Scale: id inches → Godot metres (÷ 39.37)
 *
 * Phase 9 — Skeletal model rendering.
 */

#include "godot_skel_model.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/surface_tool.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <cstring>
#include <cstdlib>

/* ── C accessor declarations (godot_skel_model_accessors.cpp + godot_renderer.c) ── */
extern "C" {
    /* From godot_renderer.c */
    void *Godot_Model_GetTikiPtr(int hModel);
    int   Godot_Model_GetType(int hModel);

    /* From godot_skel_model_accessors.cpp */
    int         Godot_Skel_GetMeshCount(void *tikiPtr);
    float       Godot_Skel_GetScale(void *tikiPtr);
    void        Godot_Skel_GetOrigin(void *tikiPtr, float *out);
    int         Godot_Skel_GetSurfaceCount(void *tikiPtr, int meshIndex);
    int         Godot_Skel_GetSurfaceInfo(void *tikiPtr, int meshIndex, int surfIndex,
                                           int *numVerts, int *numTriangles,
                                           char *surfName, int surfNameLen,
                                           char *shaderName, int shaderNameLen);
    int         Godot_Skel_GetSurfaceVertices(void *tikiPtr, int meshIndex, int surfIndex,
                                               float *positions, float *normals, float *texcoords);
    int         Godot_Skel_GetSurfaceIndices(void *tikiPtr, int meshIndex, int surfIndex,
                                              int *indices);
    int         Godot_Skel_GetBoneCount(void *tikiPtr, int meshIndex);
    int         Godot_Skel_GetBoneParent(void *tikiPtr, int meshIndex, int boneIndex);
    const char *Godot_Skel_GetName(void *tikiPtr);

    /* Phase 59: LOD data accessors */
    int  Godot_Skel_GetLodIndexCount(void);
    int  Godot_Skel_GetLodIndex(void *tikiPtr, int meshIndex, int *outLodIndex);
    int  Godot_Skel_GetCollapseData(void *tikiPtr, int meshIndex, int surfIndex,
                                     int *outCollapse, int *outCollapseIndex);
}

/* ── Coordinate conversion (id → Godot) ── */
static const float MOHAA_UNIT_SCALE = 1.0f / 39.37f;  /* inches to metres */

static inline Vector3 id_to_godot_point(float ix, float iy, float iz)
{
    return Vector3(-iy, iz, -ix);
}

static inline Vector3 id_to_godot_normal(float nx, float ny, float nz)
{
    /* Normals use the same axis re-mapping but no scale */
    return Vector3(-ny, nz, -nx);
}

/* ── Singleton ── */
GodotSkelModelCache &GodotSkelModelCache::get()
{
    static GodotSkelModelCache instance;
    return instance;
}

void GodotSkelModelCache::clear()
{
    cache_.clear();
}

const GodotSkelModelCache::CachedModel *GodotSkelModelCache::get_model(int hModel)
{
    /* Check cache first */
    auto it = cache_.find(hModel);
    if (it != cache_.end()) {
        return &it->second;
    }

    /* Build and cache */
    CachedModel *built = build_model(hModel);
    if (!built) return nullptr;

    return &cache_[hModel];
}

GodotSkelModelCache::CachedModel *GodotSkelModelCache::build_model(int hModel)
{
    /* Validate model type */
    int modType = Godot_Model_GetType(hModel);
    if (modType != 2 /* GR_MOD_TIKI */) return nullptr;

    void *tikiPtr = Godot_Model_GetTikiPtr(hModel);
    if (!tikiPtr) return nullptr;

    int meshCount = Godot_Skel_GetMeshCount(tikiPtr);
    if (meshCount <= 0) return nullptr;

    float tikiScale = Godot_Skel_GetScale(tikiPtr);
    float loadOrigin[3] = {0, 0, 0};
    Godot_Skel_GetOrigin(tikiPtr, loadOrigin);

    /* Pre-compute the load origin in Godot space */
    Vector3 originOffset = id_to_godot_point(loadOrigin[0], loadOrigin[1], loadOrigin[2])
                           * tikiScale * MOHAA_UNIT_SCALE;

    CachedModel &model = cache_[hModel];
    model.tiki_scale = tikiScale;

    int totalVerts = 0;
    int totalTris  = 0;
    static bool logged_first = false;

    /* Get the number of expected LOD levels */
    int lodCount = Godot_Skel_GetLodIndexCount();
    if (lodCount <= 0) lodCount = 1;

    /* Build a mesh for each LOD level */
    for (int lod = 0; lod < lodCount; lod++) {
        bool all_surfaces_identical = true;

        if (lod > 0) {
            for (int m = 0; m < meshCount; m++) {
                int sCount = Godot_Skel_GetSurfaceCount(tikiPtr, m);
                for (int s = 0; s < sCount; s++) {
                    int lodVertLimit = Godot_Skel_GetLodVertexLimit(tikiPtr, m, s, lod);
                    int prevLimit = Godot_Skel_GetLodVertexLimit(tikiPtr, m, s, lod - 1);
                    if (lodVertLimit != prevLimit) {
                        all_surfaces_identical = false;
                        break;
                    }
                }
                if (!all_surfaces_identical) break;
            }
        } else {
            all_surfaces_identical = false; // Always build LOD 0
        }

        if (all_surfaces_identical && model.lod_meshes.size() > 0) {
            model.lod_meshes.push_back(model.lod_meshes.back());
            continue;
        }

        Ref<ArrayMesh> arrayMesh;
        arrayMesh.instantiate();

        bool valid_mesh = false;

        /* Iterate all meshes and all surfaces */
        for (int mesh = 0; mesh < meshCount; mesh++) {
            int surfCount = Godot_Skel_GetSurfaceCount(tikiPtr, mesh);

            for (int surf = 0; surf < surfCount; surf++) {
                int lodVertLimit = Godot_Skel_GetLodVertexLimit(tikiPtr, mesh, surf, lod);

                int numVerts = 0, numTris = 0;
                char surfName[64]   = {0};
                char shaderName[64] = {0};

                if (!Godot_Skel_GetSurfaceInfo(tikiPtr, mesh, surf,
                                                &numVerts, &numTris,
                                                surfName, sizeof(surfName),
                                                shaderName, sizeof(shaderName))) {
                    continue;
                }

                if (numVerts <= 0 || numTris <= 0) continue;
                valid_mesh = true;

                /* Allocate temp buffers for vertex data */
                float *positions = (float *)malloc(numVerts * 3 * sizeof(float));
                float *normals   = (float *)malloc(numVerts * 3 * sizeof(float));
                float *texcoords = (float *)malloc(numVerts * 2 * sizeof(float));
                int   *indices   = (int *)malloc(numTris * 3 * sizeof(int));

                if (!positions || !normals || !texcoords || !indices) {
                    free(positions); free(normals); free(texcoords); free(indices);
                    continue;
                }

                if (!Godot_Skel_GetSurfaceVertices(tikiPtr, mesh, surf,
                                                    positions, normals, texcoords)) {
                    free(positions); free(normals); free(texcoords); free(indices);
                    continue;
                }

                if (!Godot_Skel_GetSurfaceIndices(tikiPtr, mesh, surf, indices)) {
                    free(positions); free(normals); free(texcoords); free(indices);
                    continue;
                }

                int outNumVerts = numVerts;
                int outNumTris  = numTris;
                int *outIndices = indices;

                /* Phase 59: LOD Collapse */
                if (lod > 0 && lodVertLimit >= 0 && lodVertLimit < numVerts) {
                    int *collapsedIndices = (int *)malloc(numTris * 3 * sizeof(int));
                    if (collapsedIndices) {
                        if (Godot_Skel_BuildLodMesh(tikiPtr, mesh, surf, lodVertLimit,
                                                     positions, normals, texcoords, numVerts,
                                                     indices, numTris, tikiScale,
                                                     collapsedIndices, &outNumTris)) {
                            outIndices = collapsedIndices;
                            outNumVerts = lodVertLimit;
                        } else {
                            free(collapsedIndices);
                        }
                    }
                }

                /* Skip surfaces that collapsed to nothing at this LOD */
                if (outNumVerts <= 0 || outNumTris <= 0) {
                    free(positions);
                    free(normals);
                    free(texcoords);
                    if (outIndices != indices) free(outIndices);
                    free(indices);
                    continue;
                }

                /* Build Godot PackedArrays with coordinate conversion */
                PackedVector3Array godotPositions;
                PackedVector3Array godotNormals;
                PackedVector2Array godotUVs;
                PackedInt32Array   godotIndices;

                godotPositions.resize(outNumVerts);
                godotNormals.resize(outNumVerts);
                godotUVs.resize(outNumVerts);
                godotIndices.resize(outNumTris * 3);

                for (int v = 0; v < outNumVerts; v++) {
                    float px = positions[v * 3 + 0];
                    float py = positions[v * 3 + 1];
                    float pz = positions[v * 3 + 2];

                    /* Apply tiki load_scale then convert id→Godot.
                     * load_origin is handled at the entity level via transform,
                     * since the mesh is in model-local space. */
                    Vector3 pos = id_to_godot_point(px, py, pz) * tikiScale * MOHAA_UNIT_SCALE;

                    float nx = normals[v * 3 + 0];
                    float ny = normals[v * 3 + 1];
                    float nz = normals[v * 3 + 2];
                    Vector3 nrm = id_to_godot_normal(nx, ny, nz);
                    /* Normalise to handle any engine quirks */
                    if (nrm.length_squared() > 0.001f) {
                        nrm = nrm.normalized();
                    }

                    float u = texcoords[v * 2 + 0];
                    float vt = texcoords[v * 2 + 1];

                    godotPositions.set(v, pos);
                    godotNormals.set(v, nrm);
                    godotUVs.set(v, Vector2(u, vt));
                }

                /* Copy indices as-is.  The id_to_godot_point conversion
                 * has det = +1 (proper rotation), so winding is preserved.
                 * Q3/MOHAA model triangles are already CCW from the visible
                 * side, matching Godot's default front-face convention. */
                bool bad_indices = false;
                for (int t = 0; t < outNumTris; t++) {
                    int i0 = outIndices[t * 3 + 0];
                    int i1 = outIndices[t * 3 + 1];
                    int i2 = outIndices[t * 3 + 2];
                    if (i0 < 0 || i0 >= outNumVerts ||
                        i1 < 0 || i1 >= outNumVerts ||
                        i2 < 0 || i2 >= outNumVerts) {
                        bad_indices = true;
                        break;
                    }
                    godotIndices.set(t * 3 + 0, i0);
                    godotIndices.set(t * 3 + 1, i1);
                    godotIndices.set(t * 3 + 2, i2);
                }

                if (bad_indices) {
                    free(positions); free(normals); free(texcoords);
                    if (outIndices != indices) free(outIndices);
                    free(indices);
                    continue;
                }

                /* Validate arrays before passing to Godot — skip degenerate surfaces
                 * that slipped through the earlier numVerts/numTris checks. */
                if (godotPositions.size() <= 0 || godotIndices.size() < 3) {
                    free(positions); free(normals); free(texcoords);
                    if (outIndices != indices) free(outIndices);
                    free(indices);
                    continue;
                }

                /* Check for NaN/Inf in vertex positions — corrupt bone data
                 * or degenerate weights can produce these, which Godot will
                 * reject or render as garbage. */
                bool has_bad_float = false;
                for (int v = 0; v < godotPositions.size(); v++) {
                    Vector3 p = godotPositions[v];
                    if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z) ||
                        std::isinf(p.x) || std::isinf(p.y) || std::isinf(p.z)) {
                        has_bad_float = true;
                        break;
                    }
                }
                if (has_bad_float) {
                    free(positions); free(normals); free(texcoords);
                    if (outIndices != indices) free(outIndices);
                    free(indices);
                    continue;
                }

                /* Build ArrayMesh surface */
                Array arrays;
                arrays.resize(Mesh::ARRAY_MAX);
                arrays[Mesh::ARRAY_VERTEX] = godotPositions;
                arrays[Mesh::ARRAY_NORMAL] = godotNormals;
                arrays[Mesh::ARRAY_TEX_UV] = godotUVs;
                arrays[Mesh::ARRAY_INDEX]  = godotIndices;

                /* Track surface count to detect if Godot rejects the data.
                 * add_surface_from_arrays returns void — we must compare
                 * the surface count before/after to know if it succeeded. */
                int scBefore = arrayMesh->get_surface_count();
                arrayMesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

                if (arrayMesh->get_surface_count() > scBefore) {
                    /* Surface was accepted — track shader info (LOD 0 only). */
                    if (lod == 0) {
                        SurfaceInfo sinfo;
                        sinfo.shader_name = String(shaderName);
                        model.surfaces.push_back(sinfo);

                        totalVerts += numVerts;
                        totalTris  += numTris;
                    }
                } else {
                    /* Godot rejected the surface — log diagnostics. */
                    const char *tikiName = Godot_Skel_GetName(tikiPtr);
                    UtilityFunctions::print(
                        String("[SKEL] Surface rejected: ") + String(tikiName ? tikiName : "?") +
                        " mesh=" + String::num_int64(mesh) + " surf=" + String::num_int64(surf) +
                        " verts=" + String::num_int64(godotPositions.size()) +
                        " idx=" + String::num_int64(godotIndices.size()) +
                        " numV=" + String::num_int64(outNumVerts) +
                        " numT=" + String::num_int64(outNumTris));
                }

                free(positions);
                free(normals);
                free(texcoords);
                free(indices);
                if (outIndices != indices) {
                    free(outIndices);
                }
            }
        }

        if (arrayMesh->get_surface_count() > 0) {
            model.lod_meshes.push_back(arrayMesh);
        } else if (lod == 0) {
            /* If LOD 0 has no surfaces, the model is invalid */
            cache_.erase(hModel);
            return nullptr;
        } else {
            /* If a lower LOD fails but LOD 0 exists, just duplicate the previous LOD */
            model.lod_meshes.push_back(model.lod_meshes.back());
        }
    }

    /* Log first successful model build */
    if (!logged_first && model.lod_meshes.size() > 0) {
        const char *name = Godot_Skel_GetName(tikiPtr);
        UtilityFunctions::print(
            String("[MoHAA] First skeletal model built: ") + String(name) +
            String(" — ") + String::num_int64(totalVerts) + String(" verts, ") +
            String::num_int64(totalTris) + String(" tris, ") +
            String::num_int64(model.lod_meshes[0]->get_surface_count()) + String(" surfaces, ") +
            String::num_int64(model.lod_meshes.size()) + String(" LODs")
        );
        logged_first = true;
    }

    return &model;
}

/* ===================================================================
 *  Phase 59: Entity LOD System
 *
 *  Distance-based LOD selection using skelHeaderGame_t::lodIndex[]
 *  and progressive mesh vertex collapse via pCollapse/pCollapseIndex.
 * ================================================================ */

/*
 * LOD distance thresholds (in id Tech 3 inches).
 * lodIndex[i] gives the maximum vertex count at LOD level i.
 * The engine uses these approximate distance bands:
 *   LOD 0: 0–256 inches (closest, full detail)
 *   LOD 1: 256–512
 *   LOD 2: 512–1024
 *   LOD 3: 1024–2048
 *   LOD 4–9: progressively farther
 */
static const float LOD_DISTANCE_THRESHOLDS[10] = {
    256.0f, 512.0f, 1024.0f, 2048.0f, 3072.0f,
    4096.0f, 5120.0f, 6144.0f, 7168.0f, 8192.0f
};

int Godot_Skel_SelectLodLevel(void *tikiPtr, int meshIndex, float distance)
{
    if (!tikiPtr || distance < 0.0f) return 0;

    int lodIndex[10];
    if (!Godot_Skel_GetLodIndex(tikiPtr, meshIndex, lodIndex)) {
        return 0;  /* No LOD data — use full detail */
    }

    /* If all lodIndex entries are 0 or identical, LOD is not meaningful */
    if (lodIndex[0] <= 0) return 0;

    /* Find the first LOD level whose distance threshold exceeds `distance` */
    int lodCount = Godot_Skel_GetLodIndexCount();
    for (int i = 0; i < lodCount; i++) {
        if (distance < LOD_DISTANCE_THRESHOLDS[i]) {
            return i;
        }
    }
    return lodCount - 1;  /* Maximum LOD (lowest detail) */
}

int Godot_Skel_GetLodVertexLimit(void *tikiPtr, int meshIndex, int surfIndex, int lodLevel)
{
    if (!tikiPtr) return -1;

    int lodIndex[10];
    if (!Godot_Skel_GetLodIndex(tikiPtr, meshIndex, lodIndex)) {
        return -1;  /* No LOD data */
    }

    int lodCount = Godot_Skel_GetLodIndexCount();
    if (lodLevel <= 0) {
        return -1; // LOD 0 is ALWAYS full detail (LOD->curve[0].val = 0.0f)
    }
    if (lodLevel >= lodCount) lodLevel = lodCount - 1;

    int lod_cutoff = lodIndex[lodLevel];
    if (lod_cutoff <= 0) {
        return -1; // No restriction
    }

    // Evaluate against pCollapseIndex to find the actual vertex limit
    int numVerts = 0, numTris = 0;
    if (!Godot_Skel_GetSurfaceInfo(tikiPtr, meshIndex, surfIndex, &numVerts, &numTris, nullptr, 0, nullptr, 0)) {
        return -1;
    }

    if (numVerts <= 3) {
        return numVerts;
    }

    int *collapseIndex = (int *)malloc(numVerts * sizeof(int));
    if (!collapseIndex) return -1;

    if (!Godot_Skel_GetCollapseData(tikiPtr, meshIndex, surfIndex, nullptr, collapseIndex)) {
        free(collapseIndex);
        return -1;
    }

    if (collapseIndex[2] < lod_cutoff) {
        free(collapseIndex);
        return 0; // Surface fully collapsed away
    }

    int low = 3;
    int mid = 3;
    int high = numVerts;

    while (high >= low) {
        mid = (low + high) >> 1;
        if (collapseIndex[mid] < lod_cutoff) {
            high = mid - 1;
            if (collapseIndex[mid - 1] >= lod_cutoff) {
                break;
            }
        } else {
            mid++;
            low = mid;
            if (high == mid || collapseIndex[mid] < lod_cutoff) {
                break;
            }
        }
    }

    free(collapseIndex);
    return mid;
}

int Godot_Skel_BuildLodMesh(void *tikiPtr, int meshIndex, int surfIndex,
                             int maxVerts,
                             const float * /*positions*/, const float * /*normals*/,
                             const float * /*texcoords*/, int numVerts,
                             const int *indices, int numTris,
                             float /*tikiScale*/,
                             int *outIndices, int *outNumTris)
{
    if (!tikiPtr || !indices || !outIndices || !outNumTris)
        return 0;

    /* If maxVerts >= numVerts, no collapse needed */
    if (maxVerts < 0 || maxVerts >= numVerts) {
        return 0;
    }

    /* Get collapse data */
    int *collapse      = (int *)malloc(numVerts * sizeof(int));
    int *collapseIndex = (int *)malloc(numVerts * sizeof(int));

    if (!collapse || !collapseIndex) {
        free(collapse);
        free(collapseIndex);
        return 0;
    }

    if (!Godot_Skel_GetCollapseData(tikiPtr, meshIndex, surfIndex,
                                     collapse, collapseIndex)) {
        free(collapse);
        free(collapseIndex);
        return 0;  /* No collapse data — use full detail */
    }

    /*
     * Progressive mesh collapse:
     *
     * For each triangle, remap each vertex index through the collapse
     * chain until the vertex index is < maxVerts.  If any two vertices
     * of a triangle collapse to the same vertex, the triangle is
     * degenerate and should be skipped.
     */
    int outTris = 0;

    for (int t = 0; t < numTris; t++) {
        int i0 = indices[t * 3 + 0];
        int i1 = indices[t * 3 + 1];
        int i2 = indices[t * 3 + 2];

        /* Walk collapse chain for each vertex */
        while (i0 >= maxVerts && i0 < numVerts) {
            int next = collapse[i0];
            if (next == i0 || next < 0) break;  /* root vertex */
            i0 = next;
        }
        while (i1 >= maxVerts && i1 < numVerts) {
            int next = collapse[i1];
            if (next == i1 || next < 0) break;
            i1 = next;
        }
        while (i2 >= maxVerts && i2 < numVerts) {
            int next = collapse[i2];
            if (next == i2 || next < 0) break;
            i2 = next;
        }

        /* Skip degenerate triangles */
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;

        /* Skip if any vertex is still out of range */
        if (i0 >= maxVerts || i1 >= maxVerts || i2 >= maxVerts) continue;

        outIndices[outTris * 3 + 0] = i0;
        outIndices[outTris * 3 + 1] = i1;
        outIndices[outTris * 3 + 2] = i2;
        outTris++;
    }

    *outNumTris = outTris;

    free(collapse);
    free(collapseIndex);

    return 1;
}
