/*
 * godot_skel_model_accessors.cpp — C accessors for TIKI skeletal model data.
 *
 * This C++ file bridges the engine's TIKI/skeletal model data structures
 * (which contain C++ members like skelChannelList_c in dtiki_t) to the
 * Godot-side C++ code that builds ArrayMesh instances.
 *
 * Must be a .cpp file because dtiki_t contains C++ members
 * (skelChannelList_c).  All exported functions are extern "C" for
 * easy consumption from the Godot module.
 *
 * Phase 9 — Skeletal model rendering.
 */

#include "../qcommon/q_shared.h"
#include "../tiki/tiki_shared.h"
#include "../tiki/tiki_skel.h"

/* ── Static helper: walk the pSurfaces linked list to surface N ── */
static skelSurfaceGame_t *GetSkelSurface(skelHeaderGame_t *skelmodel, int surfIndex)
{
    if (!skelmodel || surfIndex < 0 || surfIndex >= skelmodel->numSurfaces)
        return NULL;

    skelSurfaceGame_t *surf = skelmodel->pSurfaces;
    for (int i = 0; i < surfIndex && surf; i++) {
        surf = surf->pNext;
    }
    return surf;
}

/* ── Static helper: get the dtikisurface_t for a (meshIndex, surfIndex) pair ── */
static dtikisurface_t *GetTikiSurfaceShader(dtiki_t *tiki, int meshIndex, int surfIndex)
{
    if (!tiki) return NULL;

    /* dtiki_t::surfaces is a flat array spanning all meshes sequentially.
       Compute the flat index by summing numSurfaces for meshes before meshIndex. */
    int flatIndex = 0;
    for (int m = 0; m < meshIndex && m < tiki->numMeshes; m++) {
        skelHeaderGame_t *sk = TIKI_GetSkel(tiki->mesh[m]);
        if (sk) flatIndex += sk->numSurfaces;
    }
    flatIndex += surfIndex;

    if (flatIndex < 0 || flatIndex >= tiki->num_surfaces)
        return NULL;

    return &tiki->surfaces[flatIndex];
}

/* ===================================================================
 *  Exported accessors — called from godot_skel_model.cpp
 * ================================================================ */

extern "C" {

/* ri wrapper forward declarations (implemented in godot_renderer.c).
 * Needed by Godot_Skel_GetSurfaceVertices for bind-pose computation
 * when pStaticXyz is NULL. */
void *Godot_RI_GetSkeletor(void *tiki, int entityNumber);
void  Godot_RI_SetPoseInternal(void *skeletor, const frameInfo_t *frameInfo,
                                const int *bone_tag, const vec4_t *bone_quat,
                                float actionWeight);
void  Godot_RI_GetFrameInternal(void *tiki, int entityNumber, void *newFrame);
int   Godot_RI_GetNumChannels(void *tiki);
int   Godot_RI_GetLocalChannel(void *tiki, int globalChannel);
int   Godot_RI_GetSkelAnimFrame(void *tiki, void *bonesOut, float *radiusOut);

/* Model type: 0=bad, 1=brush, 2=tiki, 3=sprite
 * Returns -1 if handle is out of range.
 * NOTE: gr_models[] is defined in godot_renderer.c. We access the
 * TIKI model data through the dtiki_t pointer stored there.
 * Instead of reaching into godot_renderer.c globals, we accept a
 * dtiki_t pointer directly from the caller (MoHAARunner passes it
 * via the model accessor API). */

/* ── Core queries that take a dtiki_t pointer ── */

int Godot_Skel_GetMeshCount(void *tikiPtr)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki) return 0;
    return tiki->numMeshes;
}

float Godot_Skel_GetScale(void *tikiPtr)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki) return 1.0f;
    return tiki->load_scale;
}

void Godot_Skel_GetOrigin(void *tikiPtr, float *out)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || !out) return;
    out[0] = tiki->load_origin[0];
    out[1] = tiki->load_origin[1];
    out[2] = tiki->load_origin[2];
}

int Godot_Skel_GetSurfaceCount(void *tikiPtr, int meshIndex)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;
    return skelmodel->numSurfaces;
}

int Godot_Skel_GetSurfaceInfo(void *tikiPtr, int meshIndex, int surfIndex,
                               int *numVerts, int *numTriangles,
                               char *surfName, int surfNameLen,
                               char *shaderName, int shaderNameLen)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    skelSurfaceGame_t *surf = GetSkelSurface(skelmodel, surfIndex);
    if (!surf) return 0;

    if (numVerts)     *numVerts     = surf->numVerts;
    if (numTriangles) *numTriangles = surf->numTriangles;

    if (surfName && surfNameLen > 0) {
        Q_strncpyz(surfName, surf->name, surfNameLen);
    }

    if (shaderName && shaderNameLen > 0) {
        dtikisurface_t *dsurf = GetTikiSurfaceShader(tiki, meshIndex, surfIndex);
        if (dsurf && dsurf->numskins > 0) {
            Q_strncpyz(shaderName, dsurf->shader[0], shaderNameLen);
        } else {
            shaderName[0] = '\0';
        }
    }

    return 1;
}

/* Copy bind-pose vertex data into caller-provided flat arrays.
 * positions: [numVerts * 3]  (x,y,z per vertex)
 * normals:   [numVerts * 3]  (nx,ny,nz per vertex)
 * texcoords: [numVerts * 2]  (u,v per vertex) — uses UV set 0
 *
 * When pStaticXyz is NULL (the GL renderer's R_InitStaticModels never
 * ran), we compute bind-pose vertices on-the-fly from the bone-weight
 * data (pVerts) plus the skeleton's default animation frame — this
 * mirrors R_InitStaticModels in tr_staticmodels.cpp.
 *
 * Returns 1 on success, 0 on failure. */
int Godot_Skel_GetSurfaceVertices(void *tikiPtr, int meshIndex, int surfIndex,
                                   float *positions, float *normals, float *texcoords)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    skelSurfaceGame_t *surf = GetSkelSurface(skelmodel, surfIndex);
    if (!surf) return 0;

    int nv = surf->numVerts;

    /* Fast path: pre-computed static data exists (or GL renderer ran). */
    if (surf->pStaticXyz) {
        if (positions) {
            for (int i = 0; i < nv; i++) {
                positions[i * 3 + 0] = surf->pStaticXyz[i][0];
                positions[i * 3 + 1] = surf->pStaticXyz[i][1];
                positions[i * 3 + 2] = surf->pStaticXyz[i][2];
            }
        }
        if (normals && surf->pStaticNormal) {
            for (int i = 0; i < nv; i++) {
                normals[i * 3 + 0] = surf->pStaticNormal[i][0];
                normals[i * 3 + 1] = surf->pStaticNormal[i][1];
                normals[i * 3 + 2] = surf->pStaticNormal[i][2];
            }
        }
        if (texcoords && surf->pStaticTexCoords) {
            for (int i = 0; i < nv; i++) {
                texcoords[i * 2 + 0] = surf->pStaticTexCoords[i][0][0];
                texcoords[i * 2 + 1] = surf->pStaticTexCoords[i][0][1];
            }
        }
        return 1;
    }

    /* Slow path: compute bind-pose from bone weights (pVerts).
     * This mirrors the inner loop of R_InitStaticModels in
     * renderergl1/tr_staticmodels.cpp. */
    if (!surf->pVerts) {
        return 0;  /* No vertex data at all — genuine failure. */
    }

    /* Get bind-pose bone transforms via TIKI_GetSkelAnimFrame. */
    skelBoneCache_t bones[128];
    memset(bones, 0, sizeof(bones));
    if (!Godot_RI_GetSkelAnimFrame(tikiPtr, bones, NULL)) {
        return 0;  /* Skeleton not ready. */
    }

    /* Walk the variable-stride pVerts and skin each vertex. */
    skeletorVertex_t *vert = surf->pVerts;
    for (int v = 0; v < nv; v++) {
        skelWeight_t *weight = (skelWeight_t *)((byte *)vert
            + sizeof(skeletorVertex_t)
            + sizeof(skeletorMorph_t) * vert->numMorphs);

        /* Resolve bone channel for this vertex's first weight. */
        int channel;
        if (meshIndex > 0) {
            channel = Godot_RI_GetLocalChannel(tikiPtr,
                          skelmodel->pBones[weight->boneIndex].channel);
        } else {
            channel = weight->boneIndex;
        }

        /* Compute position: single-weight only (matches R_InitStaticModels
         * which uses only the first weight for each vertex). */
        if (positions && channel >= 0 && channel < 128) {
            skelBoneCache_t *bone = &bones[channel];
            positions[v * 3 + 0] =
                ((weight->offset[0] * bone->matrix[0][0]
                + weight->offset[1] * bone->matrix[1][0]
                + weight->offset[2] * bone->matrix[2][0])
                + bone->offset[0]) * weight->boneWeight;
            positions[v * 3 + 1] =
                ((weight->offset[0] * bone->matrix[0][1]
                + weight->offset[1] * bone->matrix[1][1]
                + weight->offset[2] * bone->matrix[2][1])
                + bone->offset[1]) * weight->boneWeight;
            positions[v * 3 + 2] =
                ((weight->offset[0] * bone->matrix[0][2]
                + weight->offset[1] * bone->matrix[1][2]
                + weight->offset[2] * bone->matrix[2][2])
                + bone->offset[2]) * weight->boneWeight;
        } else if (positions) {
            positions[v * 3 + 0] = 0.0f;
            positions[v * 3 + 1] = 0.0f;
            positions[v * 3 + 2] = 0.0f;
        }

        /* Compute normal: rotate by the bone matrix. */
        if (normals && channel >= 0 && channel < 128) {
            skelBoneCache_t *bone = &bones[channel];
            normals[v * 3 + 0] = vert->normal[0] * bone->matrix[0][0]
                               + vert->normal[1] * bone->matrix[1][0]
                               + vert->normal[2] * bone->matrix[2][0];
            normals[v * 3 + 1] = vert->normal[0] * bone->matrix[0][1]
                               + vert->normal[1] * bone->matrix[1][1]
                               + vert->normal[2] * bone->matrix[2][1];
            normals[v * 3 + 2] = vert->normal[0] * bone->matrix[0][2]
                               + vert->normal[1] * bone->matrix[1][2]
                               + vert->normal[2] * bone->matrix[2][2];
        } else if (normals) {
            normals[v * 3 + 0] = vert->normal[0];
            normals[v * 3 + 1] = vert->normal[1];
            normals[v * 3 + 2] = vert->normal[2];
        }

        /* Texcoords come from the vertex directly. */
        if (texcoords) {
            texcoords[v * 2 + 0] = vert->texCoords[0];
            texcoords[v * 2 + 1] = vert->texCoords[1];
        }

        /* Advance to next variable-stride vertex. */
        vert = (skeletorVertex_t *)((byte *)vert
            + sizeof(skeletorVertex_t)
            + sizeof(skeletorMorph_t) * vert->numMorphs
            + sizeof(skelWeight_t) * vert->numWeights);
    }

    return 1;
}

/* Copy triangle index data into a caller-provided int array.
 * indices: [numTriangles * 3] — three int indices per triangle.
 * Returns 1 on success, 0 on failure. */
int Godot_Skel_GetSurfaceIndices(void *tikiPtr, int meshIndex, int surfIndex,
                                  int *indices)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    skelSurfaceGame_t *surf = GetSkelSurface(skelmodel, surfIndex);
    if (!surf || !surf->pTriangles) return 0;

    int count = surf->numTriangles * 3;

    /* pTriangles: skelIndex_t* (short int) — widen to int */
    for (int i = 0; i < count; i++) {
        indices[i] = (int)surf->pTriangles[i];
    }

    return 1;
}

/* Get the dtiki_t pointer from the model table in godot_renderer.c.
 * The actual lookup is done via a function in godot_renderer.c since
 * gr_models[] is file-scoped there. */

/* Get bone count for a mesh */
int Godot_Skel_GetBoneCount(void *tikiPtr, int meshIndex)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;
    return skelmodel->numBones;
}

/* Get bone parent index (-1 for root) */
int Godot_Skel_GetBoneParent(void *tikiPtr, int meshIndex, int boneIndex)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return -1;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel || boneIndex < 0 || boneIndex >= skelmodel->numBones)
        return -1;

    return (int)skelmodel->pBones[boneIndex].parent;
}

/* Get the model name */
const char *Godot_Skel_GetName(void *tikiPtr)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki) return "";
    return tiki->name;
}

/* ===================================================================
 *  Skeletal animation — CPU skinning (Phase 13)
 *
 *  Implements the same bone computation and vertex skinning as the
 *  GL1 renderer's R_AddSkelSurfaces + RB_SkelMesh, but outputs
 *  skinned vertex positions/normals for Godot ArrayMesh consumption.
 *
 *  Architecture: ri.* callbacks are only available in godot_renderer.c
 *  (C file).  Thin wrapper functions are exported from there and
 *  called here to set pose / get bone matrices without including
 *  engine renderer headers.
 * ================================================================ */

/* Lightweight POD structs matching SkelMat4 / skelAnimFrame_t layout.
 * Using local definitions avoids pulling in the full skeletor header
 * chain (which includes Container<T>, skeletor_c, etc.). */
typedef struct {
    float val[4][3];   /* 48 bytes — matches SkelMat4 */
} godot_SkelMat4_t;

typedef struct {
    float            radius;       /* 4 bytes */
    float            bounds[2][3]; /* 24 bytes — matches SkelVec3[2] */
    godot_SkelMat4_t bones[1];    /* 48 bytes — variable-length */
} godot_skelAnimFrame_t;

/*
 * Godot_Skel_PrepareBones — Set animation pose and compute bone cache.
 *
 * Mirrors R_AddSkelSurfaces: SetPoseInternal → GetFrameInternal →
 * convert SkelMat4 to skelBoneCache_t.
 *
 * Returns a malloc'd skelBoneCache_t array (caller must free()).
 * outBoneCount is filled with the number of bones.
 * Returns NULL on failure.
 */
void *Godot_Skel_PrepareBones(void *tikiPtr, int entityNumber,
                               const frameInfo_t *frameInfo,
                               const int *bone_tag,
                               const float *bone_quat,
                               float actionWeight,
                               int *outBoneCount)
{
    if (!tikiPtr || !frameInfo) return NULL;

    /* 1. Set animation pose on the skeletor */
    void *skeletor = Godot_RI_GetSkeletor(tikiPtr, entityNumber);
    if (!skeletor) return NULL;

    Godot_RI_SetPoseInternal(skeletor, frameInfo, bone_tag,
                              (const vec4_t *)bone_quat, actionWeight);

    /* 2. Allocate frame buffer for bone output */
    int numChannels = Godot_RI_GetNumChannels(tikiPtr);
    if (numChannels <= 0) return NULL;

    size_t frameSize = sizeof(godot_skelAnimFrame_t)
                     + numChannels * sizeof(godot_SkelMat4_t);
    godot_skelAnimFrame_t *newFrame =
        (godot_skelAnimFrame_t *)malloc(frameSize);
    if (!newFrame) return NULL;

    /* 3. Compute bone matrices into the frame buffer */
    Godot_RI_GetFrameInternal(tikiPtr, entityNumber, newFrame);

    /* 4. Convert SkelMat4 → skelBoneCache_t (same as R_AddSkelSurfaces) */
    skelBoneCache_t *bones =
        (skelBoneCache_t *)malloc(numChannels * sizeof(skelBoneCache_t));
    if (!bones) {
        free(newFrame);
        return NULL;
    }

    for (int i = 0; i < numChannels; i++) {
        /* SkelMat4 row 3 = translation → skelBoneCache_t offset */
        bones[i].offset[0] = newFrame->bones[i].val[3][0];
        bones[i].offset[1] = newFrame->bones[i].val[3][1];
        bones[i].offset[2] = newFrame->bones[i].val[3][2];
        bones[i].offset[3] = 0.0f;
        /* SkelMat4 rows 0–2 = rotation → skelBoneCache_t matrix
         * with padding column set to 0 */
        bones[i].matrix[0][0] = newFrame->bones[i].val[0][0];
        bones[i].matrix[0][1] = newFrame->bones[i].val[0][1];
        bones[i].matrix[0][2] = newFrame->bones[i].val[0][2];
        bones[i].matrix[0][3] = 0.0f;
        bones[i].matrix[1][0] = newFrame->bones[i].val[1][0];
        bones[i].matrix[1][1] = newFrame->bones[i].val[1][1];
        bones[i].matrix[1][2] = newFrame->bones[i].val[1][2];
        bones[i].matrix[1][3] = 0.0f;
        bones[i].matrix[2][0] = newFrame->bones[i].val[2][0];
        bones[i].matrix[2][1] = newFrame->bones[i].val[2][1];
        bones[i].matrix[2][2] = newFrame->bones[i].val[2][2];
        bones[i].matrix[2][3] = 0.0f;
    }

    free(newFrame);
    if (outBoneCount) *outBoneCount = numChannels;
    return bones;
}

/*
 * Godot_Skel_SkinSurface — CPU-skin a single surface.
 *
 * Mirrors RB_SkelMesh: walk the variable-stride pVerts buffer,
 * apply SkelWeightGetXyz per weight per vertex using the bone cache.
 *
 * For mesh > 0, bone indices are remapped through the channel table
 * (pBones[boneIndex].channel → TIKI_GetLocalChannel).
 * For mesh 0, boneIndex maps directly.
 *
 * Writes outPositions[numVerts*3] and outNormals[numVerts*3]
 * in id-space coordinates (caller applies scale + coord conversion).
 *
 * Returns 1 on success, 0 on failure.
 */
int Godot_Skel_SkinSurface(void *tikiPtr, int meshIndex, int surfIndex,
                             const void *boneCachePtr, int boneCount,
                             float *outPositions, float *outNormals)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    skelSurfaceGame_t *surf = GetSkelSurface(skelmodel, surfIndex);
    if (!surf || !surf->pVerts) return 0;

    const skelBoneCache_t *bones = (const skelBoneCache_t *)boneCachePtr;
    if (!bones || boneCount <= 0) return 0;

    int numVerts = surf->numVerts;
    skeletorVertex_t *vert = surf->pVerts;

    for (int v = 0; v < numVerts; v++) {
        vec3_t pos = {0, 0, 0};

        /* Skip past vertex header + morphs to reach weights */
        skelWeight_t *weight = (skelWeight_t *)((byte *)vert
            + sizeof(skeletorVertex_t)
            + sizeof(skeletorMorph_t) * vert->numMorphs);

        /* Resolve bone index for the normal (uses first weight's bone) */
        int normalBoneIdx;
        if (meshIndex > 0) {
            int ch = skelmodel->pBones[weight->boneIndex].channel;
            normalBoneIdx = Godot_RI_GetLocalChannel(tikiPtr, ch);
        } else {
            normalBoneIdx = weight->boneIndex;
        }

        /* Transform normal by bone rotation matrix */
        if (outNormals && normalBoneIdx >= 0 && normalBoneIdx < boneCount) {
            const skelBoneCache_t *nb = &bones[normalBoneIdx];
            float nx = vert->normal[0];
            float ny = vert->normal[1];
            float nz = vert->normal[2];
            outNormals[v*3+0] = nx*nb->matrix[0][0]
                              + ny*nb->matrix[1][0]
                              + nz*nb->matrix[2][0];
            outNormals[v*3+1] = nx*nb->matrix[0][1]
                              + ny*nb->matrix[1][1]
                              + nz*nb->matrix[2][1];
            outNormals[v*3+2] = nx*nb->matrix[0][2]
                              + ny*nb->matrix[1][2]
                              + nz*nb->matrix[2][2];
        } else if (outNormals) {
            outNormals[v*3+0] = vert->normal[0];
            outNormals[v*3+1] = vert->normal[1];
            outNormals[v*3+2] = vert->normal[2];
        }

        /* Accumulate weighted bone transforms (SkelWeightGetXyz) */
        for (int w = 0; w < vert->numWeights; w++) {
            int boneIdx;
            if (meshIndex > 0) {
                int ch = skelmodel->pBones[weight->boneIndex].channel;
                boneIdx = Godot_RI_GetLocalChannel(tikiPtr, ch);
            } else {
                boneIdx = weight->boneIndex;
            }

            if (boneIdx >= 0 && boneIdx < boneCount) {
                const skelBoneCache_t *bone = &bones[boneIdx];
                pos[0] += ((weight->offset[0] * bone->matrix[0][0]
                          + weight->offset[1] * bone->matrix[1][0]
                          + weight->offset[2] * bone->matrix[2][0])
                          + bone->offset[0]) * weight->boneWeight;
                pos[1] += ((weight->offset[0] * bone->matrix[0][1]
                          + weight->offset[1] * bone->matrix[1][1]
                          + weight->offset[2] * bone->matrix[2][1])
                          + bone->offset[1]) * weight->boneWeight;
                pos[2] += ((weight->offset[0] * bone->matrix[0][2]
                          + weight->offset[1] * bone->matrix[1][2]
                          + weight->offset[2] * bone->matrix[2][2])
                          + bone->offset[2]) * weight->boneWeight;
            }

            weight++;
        }

        outPositions[v*3+0] = pos[0];
        outPositions[v*3+1] = pos[1];
        outPositions[v*3+2] = pos[2];

        /* Advance to next variable-stride vertex */
        vert = (skeletorVertex_t *)((byte *)vert
            + sizeof(skeletorVertex_t)
            + sizeof(skeletorMorph_t) * vert->numMorphs
            + sizeof(skelWeight_t) * vert->numWeights);
    }

    return 1;
}

/* ===================================================================
 *  LOD data accessors (Phase 59)
 *
 *  Expose skelHeaderGame_t.lodIndex[], skelSurfaceGame_t.pCollapse[],
 *  and pCollapseIndex[] to the Godot-side LOD system.
 * ================================================================ */

/*
 * Godot_Skel_GetLodIndexCount — return the number of LOD index entries.
 * Always TIKI_SKEL_LOD_INDEXES (10).
 */
int Godot_Skel_GetLodIndexCount(void)
{
    return TIKI_SKEL_LOD_INDEXES;
}

/*
 * Godot_Skel_GetLodIndex — copy the lodIndex[10] array for a mesh.
 *
 * lodIndex[i] stores the maximum vertex count for LOD level i.
 * The engine uses this to select LOD based on camera distance.
 * Returns 1 on success, 0 on failure.
 */
int Godot_Skel_GetLodIndex(void *tikiPtr, int meshIndex, int *outLodIndex)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes || !outLodIndex)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    for (int i = 0; i < TIKI_SKEL_LOD_INDEXES; i++) {
        outLodIndex[i] = skelmodel->lodIndex[i];
    }
    return 1;
}

/*
 * Godot_Skel_GetCollapseData — get progressive mesh collapse data.
 *
 * pCollapse[v] = the vertex that vertex v collapses to at the next
 *                LOD level (-1 or self means it's a root vertex).
 * pCollapseIndex[v] = remapped triangle index for collapsed mesh.
 *
 * outCollapse and outCollapseIndex must each have numVerts entries.
 * Returns 1 on success, 0 on failure (or if collapse data is missing).
 */
int Godot_Skel_GetCollapseData(void *tikiPtr, int meshIndex, int surfIndex,
                                int *outCollapse, int *outCollapseIndex)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || meshIndex < 0 || meshIndex >= tiki->numMeshes)
        return 0;

    skelHeaderGame_t *skelmodel = TIKI_GetSkel(tiki->mesh[meshIndex]);
    if (!skelmodel) return 0;

    skelSurfaceGame_t *surf = GetSkelSurface(skelmodel, surfIndex);
    if (!surf) return 0;

    /* Collapse data may not exist for all models */
    if (!surf->pCollapse || !surf->pCollapseIndex)
        return 0;

    int nv = surf->numVerts;
    for (int i = 0; i < nv; i++) {
        if (outCollapse)      outCollapse[i]      = (int)surf->pCollapse[i];
        if (outCollapseIndex) outCollapseIndex[i]  = (int)surf->pCollapseIndex[i];
    }
    return 1;
}

} /* extern "C" */
