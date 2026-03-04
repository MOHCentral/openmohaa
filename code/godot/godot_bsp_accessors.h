/* godot_bsp_accessors.h — C API for reading BSP world data from the engine.
 *
 * These functions are implemented in renderergl1/godot_bsp_accessors.c
 * and read from the engine's parsed tr.world data instead of re-parsing
 * the BSP file.  They replace the custom BSP parsing in godot_bsp_mesh.cpp
 * for all query functions (PVS, lightgrid, marks, entities, etc.).
 *
 * For use from godot-cpp C++ code, wrap includes in extern "C" { }.
 */

#ifndef GODOT_BSP_ACCESSORS_H
#define GODOT_BSP_ACCESSORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Entity token parser ── */
int Godot_BSP_GetEntityToken(char *buffer, int bufferSize);
void Godot_BSP_ResetEntityTokenParse(void);
const char *Godot_BSP_GetEntityString(void);

/* ── Inline model bounds ── */
void Godot_BSP_GetInlineModelBounds(int index, float *mins, float *maxs);

/* ── Map version ── */
int Godot_BSP_GetMapVersion(void);

/* ── Lightgrid sampling ── */
int Godot_BSP_LightForPoint(const float point[3], float ambientLight[3],
                            float directedLight[3], float lightDir[3]);

/* ── PVS queries ── */
int Godot_BSP_PointLeaf(const float pt[3]);
int Godot_BSP_PointCluster(const float pt[3]);
int Godot_BSP_ClusterVisible(int source, int target);
int Godot_BSP_GetNumClusters(void);
int Godot_BSP_InPVS(const float p1[3], const float p2[3]);

/* ── Surface flag queries ── */
int Godot_BSP_SurfaceHasLightmap(int surface_index);

/* ── Mark fragments ── */
int Godot_BSP_MarkFragments(
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared);

int Godot_BSP_MarkFragmentsForInlineModel(
    int bmodelIndex,
    const float vAngles[3], const float vOrigin[3],
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared);

/* ── Static model accessors ── */
int Godot_BSP_GetStaticModelData(int index, char *model, int modelBufSize,
    float origin[3], float angles[3], float *scale);

/* Get the firstVertexData (byte offset into staticModelData RGBA array)
 * for a static model instance.  Returns -1 on failure. */
int Godot_BSP_GetStaticModelFirstVertexData(int index);

/* Get the per-vertex RGBA lighting colours for a static model.
 * Copies up to maxVerts × 4 bytes into outRGBA.
 * vertexOffset is the surface vertex offset within this model
 * (accumulated numVerts across preceding surfaces).
 * Returns the number of vertices actually copied (≤ maxVerts). */
int Godot_BSP_GetStaticModelVertexColors(int index, int vertexOffset,
    int maxVerts, unsigned char *outRGBA);

/* ── Terrain patch accessors ── */
int Godot_BSP_GetTerrainPatchCount(void);
int Godot_BSP_GetTerrainPatchData(int index,
    float *x0, float *y0, float *z0,
    float texCoord[2][2][2],
    unsigned char heightmap[81],
    unsigned short *iShader,
    unsigned short *iLightMap,
    unsigned char *lmapScale,
    unsigned char *lm_s, unsigned char *lm_t,
    unsigned char *flags);
const char *Godot_BSP_GetTerrainPatchShaderName(int index);

/* ── Fog volume accessors ── */

/* ── Flare surface accessors ── */
int Godot_BSP_GetFlareData(int flareIndex,
    float origin[3], float color[3], char *shaderName, int shaderBufSize);

/* ── Brush model (sub-model) accessors ── */
int Godot_BSP_GetBrushModelSurfaceRange(int submodelIndex,
    int *firstSurfaceIndex, int *numSurfaces);

/* ── World surface accessors for mesh building ── */
int Godot_BSP_GetNumSurfaces(void);
int Godot_BSP_GetSurfaceType(int surfaceIndex);
const char *Godot_BSP_GetSurfaceShaderName(int surfaceIndex);
int Godot_BSP_GetSurfaceShaderFlags(int surfaceIndex);
int Godot_BSP_GetSurfaceFogIndex(int surfaceIndex);

int Godot_BSP_GetFaceSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals, float *texcoords, float *lmcoords,
    unsigned char *colors,
    int maxIndices, int *indices, int *outNumIndices,
    float planeNormal[3], float *planeDist,
    int *lmX, int *lmY, int *lmWidth, int *lmHeight);

int Godot_BSP_GetGridSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals, float *texcoords,
    float *lmcoords, unsigned char *colors,
    int *outWidth, int *outHeight,
    int *lmX, int *lmY, int *lmWidth, int *lmHeight);

int Godot_BSP_GetTriangleSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals,
    float *texcoords, float *lmcoords, unsigned char *colors,
    int maxIndices, int *indices, int *outNumIndices);

int Godot_BSP_GetSurfaceCluster(int surfaceIndex);

/* Build a complete surface→cluster mapping in one pass (batch API). */
void Godot_BSP_BuildSurfaceClusterMap(int *surfaceClusterOut, int numSurfaces);

/* Build flat array of (surfaceIdx, cluster) pairs for ALL surface
 * ↔ cluster relationships. Multi-cluster surfaces produce multiple
 * entries. Returns the number of pairs written.
 * Each pair is two consecutive ints: [surfaceIdx, cluster]. */
int Godot_BSP_BuildSurfaceClusterPairs(int *pairsOut, int maxPairs, int numSurfaces);

/* Build terrain patch → cluster mapping from leaf ownership data.
 * Uses LUMP_TERRAININDEXES indirection (visTerraPatches) to map each
 * terrain patch to the first BSP cluster that references it.  Patches
 * not referenced by any leaf get cluster -1 (always visible). */
void Godot_BSP_BuildTerrainClusterMap(int *patchClusterOut, int numPatches);

/* Build flat array of (patchIdx, cluster) pairs for ALL terrain patch
 * ↔ cluster relationships.  Multi-cluster patches produce multiple
 * entries.  Returns the number of pairs written.  Each pair is two
 * consecutive ints: [patchIdx, cluster].  maxPairs is the capacity. */
int Godot_BSP_BuildTerrainClusterPairs(int *pairsOut, int maxPairs, int numPatches);

/* Build static model → cluster mapping from leaf ownership data.
 * Uses LUMP_STATICMODELINDEXES indirection (visStaticModels) to map
 * each static model to the first BSP cluster that references it.
 * Models not referenced by any leaf get cluster -1 (always visible). */
void Godot_BSP_BuildStaticModelClusterMap(int *modelClusterOut, int numModels);

/* ── Lightmap page data ── */
int Godot_BSP_GetLightmapPageCount(void);
const unsigned char *Godot_BSP_GetLightmapPageData(int pageIndex);

/* ── BSP shader lump accessors ── */
int Godot_BSP_GetNumShaders(void);
const char *Godot_BSP_GetShaderName(int shaderIndex);
int Godot_BSP_GetShaderSurfaceFlags(int shaderIndex);
int Godot_BSP_GetShaderContentFlags(int shaderIndex);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_BSP_ACCESSORS_H */
