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
