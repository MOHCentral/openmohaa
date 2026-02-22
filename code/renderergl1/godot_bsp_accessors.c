/* godot_bsp_accessors.c — Bridge from engine BSP data to Godot-side queries.
 *
 * This file lives in code/renderergl1/ so it can #include tr_local.h and
 * access tr.world (the parsed BSP world data), plus call real engine
 * functions (R_PointInLeaf, R_inPVS, R_MarkFragments, R_LightForPoint,
 * R_GetLightingGridValue, CM_EntityString, CM_ModelBounds, etc.).
 *
 * It replaces the custom BSP re-parsing in godot_bsp_mesh.cpp for all
 * C-API query functions: PVS, lightgrid, mark fragments, entity tokens,
 * inline model bounds, map version, surface flags, static models, terrain,
 * and fog volumes.
 *
 * The godot_bsp_mesh.cpp C++ code (LoadWorld, Unload, GetBrushModelMesh,
 * GetClusterMesh) still handles mesh construction because it creates
 * Godot ArrayMesh objects — but it can read surface/vertex data via
 * accessors here instead of re-parsing the BSP file from scratch.
 */

#include "tr_local.h"
#include <string.h>
#include <math.h>

#define LIGHTMAP_SIZE 128

/* ===================================================================
 *  Forward declarations for engine functions we call
 * ================================================================ */

/* tr_world.c */
extern mnode_t *R_PointInLeaf(const vec3_t p);
extern qboolean R_inPVS(const vec3_t p1, const vec3_t p2);

/* tr_marks.c */
extern int R_MarkFragments(int numPoints, const vec3_t *points,
    const vec3_t projection, int maxPoints, vec3_t pointBuffer,
    int maxFragments, markFragment_t *fragmentBuffer, float fRadiusSquared);
extern int R_MarkFragmentsForInlineModel(clipHandle_t bmodel,
    const vec3_t vAngles, const vec3_t vOrigin,
    int numPoints, const vec3_t *points, const vec3_t projection,
    int maxPoints, vec3_t pointBuffer,
    int maxFragments, markFragment_t *fragmentBuffer, float fRadiusSquared);

/* tr_light.c */
extern void R_GetLightingGridValue(const vec3_t vPos, vec3_t vLight);
extern int R_LightForPoint(vec3_t point, vec3_t ambientLight,
    vec3_t directedLight, vec3_t lightDir);

/* tr_bsp.c */
extern int RE_MapVersion(void);

/* qcommon/cm_load.c — available at link time */
extern char *CM_EntityString(void);
extern void CM_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs);
extern clipHandle_t CM_InlineModel(int index);
extern int CM_NumInlineModels(void);

/* qcommon/q_shared.c — tokeniser */
extern char *COM_ParseExt(char **data_p, qboolean allowLineBreak);

/* ===================================================================
 *  Entity token parser — uses engine's CM_EntityString + COM_ParseExt
 * ================================================================ */

static const char *s_entityParsePoint = NULL;

int Godot_BSP_GetEntityToken(char *buffer, int bufferSize)
{
    const char *s;
    char *token;

    if (!buffer || bufferSize <= 0) return 0;
    buffer[0] = '\0';

    if (!s_entityParsePoint) {
        s_entityParsePoint = CM_EntityString();
    }
    if (!s_entityParsePoint || !*s_entityParsePoint) {
        return 0;
    }

    /* COM_ParseExt takes a char** — cast away const since it only
     * advances the pointer, does not modify the string content. */
    s = s_entityParsePoint;
    token = COM_ParseExt((char **)&s, qtrue);
    if (!token[0]) {
        s_entityParsePoint = s;
        return 0;
    }

    Q_strncpyz(buffer, token, bufferSize);
    s_entityParsePoint = s;
    return 1;
}

void Godot_BSP_ResetEntityTokenParse(void)
{
    s_entityParsePoint = CM_EntityString();
}

const char *Godot_BSP_GetEntityString(void)
{
    return CM_EntityString();
}

/* ===================================================================
 *  Inline model bounds — via CM_ModelBounds
 * ================================================================ */

void Godot_BSP_GetInlineModelBounds(int index, float *mins, float *maxs)
{
    if (index < 0 || index >= CM_NumInlineModels()) {
        if (mins) { mins[0] = -128; mins[1] = -128; mins[2] = -128; }
        if (maxs) { maxs[0] = 128;  maxs[1] = 128;  maxs[2] = 128; }
        return;
    }

    {
        clipHandle_t h = CM_InlineModel(index);
        vec3_t cmMins, cmMaxs;
        CM_ModelBounds(h, cmMins, cmMaxs);
        if (mins) VectorCopy(cmMins, mins);
        if (maxs) VectorCopy(cmMaxs, maxs);
    }
}

/* ===================================================================
 *  Map version — via RE_MapVersion
 * ================================================================ */

int Godot_BSP_GetMapVersion(void)
{
    return RE_MapVersion();
}

/* ===================================================================
 *  Lightgrid sampling — via R_GetLightingGridValue
 *
 *  The engine's R_GetLightingGridValue returns a combined light colour.
 *  For the Godot side we return ambient, directed, and lightDir as
 *  separate components for more flexible lighting.  We use the engine's
 *  real lightgrid data via tr.world directly.
 * ================================================================ */

int Godot_BSP_LightForPoint(const float point[3], float ambientLight[3],
                            float directedLight[3], float lightDir[3])
{
    vec3_t vLight;
    vec3_t pt;

    /* Defaults */
    if (ambientLight)  { ambientLight[0] = 0.5f; ambientLight[1] = 0.5f; ambientLight[2] = 0.5f; }
    if (directedLight) { directedLight[0] = 0.5f; directedLight[1] = 0.5f; directedLight[2] = 0.5f; }
    if (lightDir)      { lightDir[0] = 0.0f; lightDir[1] = 0.0f; lightDir[2] = 1.0f; }

    if (!tr.world || !tr.world->lightGridData || !tr.world->lightGridOffsets)
        return 0;

    VectorCopy(point, pt);

    /* Use the engine's full lightgrid sampling for the combined colour */
    R_GetLightingGridValue(pt, vLight);

    /* The engine's R_GetLightingGridValue returns a single combined
     * colour.  For ambient/directed split, we read the raw grid data.
     * Use the same grid lookup logic as the engine (tr_light.c). */
    {
        int i;
        int iGridPos[3];
        float fV;
        vec3_t vLightOrigin;

        VectorSubtract(pt, tr.world->lightGridMins, vLightOrigin);

        for (i = 0; i < 3; i++) {
            fV = vLightOrigin[i] * tr.world->lightGridOOSize[i];
            iGridPos[i] = (int)floor(fV);
            if (iGridPos[i] < 0) iGridPos[i] = 0;
            else if (iGridPos[i] > tr.world->lightGridBounds[i] - 2)
                iGridPos[i] = tr.world->lightGridBounds[i] - 2;
        }

        {
            int iArrayXStep = tr.world->lightGridBounds[1];
            int iBaseOffset = tr.world->lightGridBounds[0] + iGridPos[1]
                            + iArrayXStep * iGridPos[0];
            int iOffset;
            byte *pCurData, *pColor;

            iOffset = tr.world->lightGridOffsets[iBaseOffset]
                    + (tr.world->lightGridOffsets[iGridPos[0]] << 8);
            pCurData = &tr.world->lightGridData[iOffset];

            /* MOHAA lightgrid format:
             * byte[0] = ambient palette index
             * byte[1] = directed palette index
             * byte[2] = lat (light direction)
             * byte[3] = lon (light direction)
             */
            if (ambientLight) {
                pColor = &tr.world->lightGridPalette[pCurData[0] * 3];
                ambientLight[0] = pColor[0] / 255.0f;
                ambientLight[1] = pColor[1] / 255.0f;
                ambientLight[2] = pColor[2] / 255.0f;
            }
            if (directedLight) {
                pColor = &tr.world->lightGridPalette[pCurData[1] * 3];
                directedLight[0] = pColor[0] / 255.0f;
                directedLight[1] = pColor[1] / 255.0f;
                directedLight[2] = pColor[2] / 255.0f;
            }
            if (lightDir) {
                float theta, phi;
                theta = pCurData[2] * (M_PI / 128.0f);
                phi   = pCurData[3] * (M_PI / 128.0f);
                lightDir[0] = cos(phi) * sin(theta);
                lightDir[1] = sin(phi) * sin(theta);
                lightDir[2] = cos(theta);
            }
        }
    }

    return 1;
}

/* ===================================================================
 *  PVS queries — via engine's BSP tree (tr.world->nodes)
 * ================================================================ */

int Godot_BSP_PointLeaf(const float pt[3])
{
    mnode_t *leaf;
    if (!tr.world) return -1;

    leaf = R_PointInLeaf(pt);
    if (!leaf) return -1;

    /* Return the leaf index (offset from start of nodes array) */
    return (int)(leaf - tr.world->nodes);
}

int Godot_BSP_PointCluster(const float pt[3])
{
    mnode_t *leaf;
    if (!tr.world) return -1;

    leaf = R_PointInLeaf(pt);
    if (!leaf) return -1;

    return leaf->cluster;
}

int Godot_BSP_ClusterVisible(int source, int target)
{
    if (!tr.world || !tr.world->vis)
        return 1;  /* no PVS: assume visible */
    if (source < 0 || target < 0)
        return 1;
    if (source == target)
        return 1;
    if (source >= tr.world->numClusters || target >= tr.world->numClusters)
        return 1;

    {
        const byte *vis = tr.world->vis + source * tr.world->clusterBytes;
        return (vis[target >> 3] & (1 << (target & 7))) ? 1 : 0;
    }
}

int Godot_BSP_GetNumClusters(void)
{
    if (!tr.world) return 0;
    return tr.world->numClusters;
}

int Godot_BSP_InPVS(const float p1[3], const float p2[3])
{
    if (!tr.world) return 1;
    return R_inPVS(p1, p2) ? 1 : 0;
}

/* ===================================================================
 *  Surface flag queries
 * ================================================================ */

int Godot_BSP_SurfaceHasLightmap(int surface_index)
{
    if (!tr.world) return 1;
    if (surface_index < 0 || surface_index >= tr.world->numsurfaces) return 1;

    {
        msurface_t *surf = &tr.world->surfaces[surface_index];
        if (!surf->shader) return 1;
        /* Check SURF_NOLIGHTMAP in shader's surfaceFlags */
        return (surf->shader->surfaceFlags & 0x100) ? 0 : 1;
    }
}

/* ===================================================================
 *  Mark fragments — delegate to engine's R_MarkFragments
 * ================================================================ */

int Godot_BSP_MarkFragments(
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared)
{
    markFragment_t fragments[256];
    int numFragments;
    int i;

    if (!tr.world) return 0;
    if (numPoints <= 0) return 0;
    if (maxFragments > 256) maxFragments = 256;

    numFragments = R_MarkFragments(numPoints, (const vec3_t *)points,
        projection, maxPoints, (vec3_t *)pointBuffer,
        maxFragments, fragments, fRadiusSquared);

    /* Convert markFragment_t array to the separate arrays the caller expects */
    for (i = 0; i < numFragments; i++) {
        fragFirstPoint[i] = fragments[i].firstPoint;
        fragNumPoints[i]  = fragments[i].numPoints;
        fragIIndex[i]     = fragments[i].iIndex;
    }

    return numFragments;
}

int Godot_BSP_MarkFragmentsForInlineModel(
    int bmodelIndex,
    const float vAngles[3], const float vOrigin[3],
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared)
{
    markFragment_t fragments[256];
    int numFragments;
    int i;

    if (!tr.world) return 0;
    if (numPoints <= 0) return 0;
    if (maxFragments > 256) maxFragments = 256;

    numFragments = R_MarkFragmentsForInlineModel(
        (clipHandle_t)bmodelIndex, vAngles, vOrigin,
        numPoints, (const vec3_t *)points, projection,
        maxPoints, (vec3_t *)pointBuffer,
        maxFragments, fragments, fRadiusSquared);

    for (i = 0; i < numFragments; i++) {
        fragFirstPoint[i] = fragments[i].firstPoint;
        fragNumPoints[i]  = fragments[i].numPoints;
        fragIIndex[i]     = fragments[i].iIndex;
    }

    return numFragments;
}

/* ===================================================================
 *  Static model accessors — from tr.world->staticModels[]
 * ================================================================ */

int Godot_BSP_GetStaticModelCount(void)
{
    if (!tr.world) return 0;
    return tr.world->numStaticModels;
}

/* Fill a caller-provided struct with static model data.
 * Returns 1 on success, 0 if index out of range. */
int Godot_BSP_GetStaticModelData(int index, char *model, int modelBufSize,
    float origin[3], float angles[3], float *scale)
{
    cStaticModelUnpacked_t *sm;

    if (!tr.world || index < 0 || index >= tr.world->numStaticModels)
        return 0;

    sm = &tr.world->staticModels[index];
    Q_strncpyz(model, sm->model, modelBufSize);
    VectorCopy(sm->origin, origin);
    VectorCopy(sm->angles, angles);
    *scale = sm->scale;
    return 1;
}

/* ===================================================================
 *  Terrain patch accessors — from tr.world->terraPatches[]
 * ================================================================ */

int Godot_BSP_GetTerrainPatchCount(void)
{
    if (!tr.world) return 0;
    return tr.world->numTerraPatches;
}

/* Fill caller-provided buffers with terrain patch data.
 * Returns 1 on success, 0 if index out of range. */
int Godot_BSP_GetTerrainPatchData(int index,
    float *x0, float *y0, float *z0,
    float texCoord[2][2][2],
    unsigned char heightmap[81],
    unsigned short *iShader,
    unsigned short *iLightMap,
    unsigned char *lmapScale,
    unsigned char *lm_s, unsigned char *lm_t,
    unsigned char *flags)
{
    cTerraPatchUnpacked_t *patch;
    int i, j, k;

    if (!tr.world || index < 0 || index >= tr.world->numTerraPatches)
        return 0;

    patch = &tr.world->terraPatches[index];

    *x0 = patch->x0;
    *y0 = patch->y0;
    *z0 = patch->z0;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++)
                texCoord[i][j][k] = patch->texCoord[i][j][k];

    memcpy(heightmap, patch->heightmap, 81);

    /* Shader index: shader_t has an 'index' field */
    if (patch->shader)
        *iShader = (unsigned short)patch->shader->index;
    else
        *iShader = 0;

    /* Lightmap data: extract from drawinfo */
    *iLightMap = 0;  /* Computed from lmData pointer offset */
    *lmapScale = (unsigned char)patch->drawinfo.lmapStep;
    *lm_s = (unsigned char)patch->drawinfo.lmapX;
    *lm_t = (unsigned char)patch->drawinfo.lmapY;
    *flags = patch->flags;

    return 1;
}

/* Get terrain patch shader name by index */
const char *Godot_BSP_GetTerrainPatchShaderName(int index)
{
    cTerraPatchUnpacked_t *patch;
    if (!tr.world || index < 0 || index >= tr.world->numTerraPatches)
        return "";

    patch = &tr.world->terraPatches[index];
    if (patch->shader)
        return patch->shader->name;
    return "";
}

/* ===================================================================
 *  Fog volume accessors — from tr.world fog shaders
 * ================================================================ */

int Godot_BSP_GetFogVolumeCount(void)
{
    /* Fog data is stored in tr.world shaders that have fogParms.
     * For now, return 0 — fog volumes need the fog lump which is
     * parsed into tr.world->surfaces with fogIndex. */
    /* TODO: expose fog data when fog rendering is implemented */
    return 0;
}

/* ===================================================================
 *  Flare surface accessors
 * ================================================================ */

int Godot_BSP_GetFlareCount(void)
{
    int count = 0;
    int i;

    if (!tr.world) return 0;

    for (i = 0; i < tr.world->numsurfaces; i++) {
        if (tr.world->surfaces[i].data &&
            *(surfaceType_t *)tr.world->surfaces[i].data == SF_FLARE) {
            count++;
        }
    }
    return count;
}

int Godot_BSP_GetFlareData(int flareIndex,
    float origin[3], float color[3], char *shaderName, int shaderBufSize)
{
    int count = 0;
    int i;

    if (!tr.world) return 0;

    for (i = 0; i < tr.world->numsurfaces; i++) {
        if (tr.world->surfaces[i].data &&
            *(surfaceType_t *)tr.world->surfaces[i].data == SF_FLARE) {
            if (count == flareIndex) {
                srfFlare_t *flare = (srfFlare_t *)tr.world->surfaces[i].data;
                VectorCopy(flare->origin, origin);
                VectorCopy(flare->color, color);
                if (tr.world->surfaces[i].shader)
                    Q_strncpyz(shaderName, tr.world->surfaces[i].shader->name, shaderBufSize);
                else
                    shaderName[0] = '\0';
                return 1;
            }
            count++;
        }
    }
    return 0;
}

/* ===================================================================
 *  Brush model (sub-model) accessors — from tr.world->bmodels[]
 * ================================================================ */

int Godot_BSP_GetBrushModelCount(void)
{
    if (!tr.world) return 0;
    /* Subtract 1 because bmodels[0] is the world model itself */
    return tr.world->numBmodels > 0 ? tr.world->numBmodels - 1 : 0;
}

/* Get surface range for a brush sub-model (1-based index).
 * Returns number of surfaces, fills firstSurface with the offset
 * into tr.world->surfaces[]. */
int Godot_BSP_GetBrushModelSurfaceRange(int submodelIndex,
    int *firstSurfaceIndex, int *numSurfaces)
{
    bmodel_t *bm;

    if (!tr.world || submodelIndex < 1 || submodelIndex >= tr.world->numBmodels)
        return 0;

    bm = &tr.world->bmodels[submodelIndex];
    *firstSurfaceIndex = (int)(bm->firstSurface - tr.world->surfaces);
    *numSurfaces = bm->numSurfaces;
    return 1;
}

/* ===================================================================
 *  World surface accessors — for mesh building
 *
 *  These expose tr.world->surfaces[] data so that godot_bsp_mesh.cpp
 *  can build meshes without re-parsing the BSP file.
 * ================================================================ */

int Godot_BSP_GetNumSurfaces(void)
{
    if (!tr.world) return 0;
    return tr.world->numsurfaces;
}

/* Get the surface type for a given surface index.
 * Returns the surfaceType_t enum value. */
int Godot_BSP_GetSurfaceType(int surfaceIndex)
{
    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return SF_BAD;

    if (!tr.world->surfaces[surfaceIndex].data)
        return SF_BAD;

    return *(surfaceType_t *)tr.world->surfaces[surfaceIndex].data;
}

/* Get the shader name for a surface */
const char *Godot_BSP_GetSurfaceShaderName(int surfaceIndex)
{
    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return "";
    if (!tr.world->surfaces[surfaceIndex].shader)
        return "";
    return tr.world->surfaces[surfaceIndex].shader->name;
}

/* Get surface shader flags (surfaceFlags from the dshader_t) */
int Godot_BSP_GetSurfaceShaderFlags(int surfaceIndex)
{
    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return 0;
    if (!tr.world->surfaces[surfaceIndex].shader)
        return 0;
    return tr.world->surfaces[surfaceIndex].shader->surfaceFlags;
}

/* Get surface fog index */
int Godot_BSP_GetSurfaceFogIndex(int surfaceIndex)
{
    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return -1;
    return tr.world->surfaces[surfaceIndex].fogIndex;
}

/* Get face surface data (SF_FACE / srfSurfaceFace_t).
 * Returns vertex count and writes vertex data to caller's arrays.
 * Returns 0 if surface is wrong type or out of range. */
int Godot_BSP_GetFaceSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals, float *texcoords, float *lmcoords,
    unsigned char *colors,
    int maxIndices, int *indices, int *outNumIndices,
    float planeNormal[3], float *planeDist,
    int *lmX, int *lmY, int *lmWidth, int *lmHeight)
{
    msurface_t *surf;
    srfSurfaceFace_t *face;
    int i, numVerts, numIdx;
    float *point;

    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return 0;

    surf = &tr.world->surfaces[surfaceIndex];
    if (!surf->data || *(surfaceType_t *)surf->data != SF_FACE)
        return 0;

    face = (srfSurfaceFace_t *)surf->data;
    numVerts = face->numPoints;
    numIdx = face->numIndices;

    if (numVerts > maxVerts) numVerts = maxVerts;

    for (i = 0; i < numVerts; i++) {
        point = face->points[i];
        /* points[][VERTEXSIZE] = { x, y, z, s, t, lm_s, lm_t, ? } */
        if (positions) {
            positions[i*3+0] = point[0];
            positions[i*3+1] = point[1];
            positions[i*3+2] = point[2];
        }
        if (texcoords) {
            texcoords[i*2+0] = point[3];
            texcoords[i*2+1] = point[4];
        }
        if (lmcoords) {
            lmcoords[i*2+0] = point[5];
            lmcoords[i*2+1] = point[6];
        }
    }

    /* Face plane */
    if (planeNormal) VectorCopy(face->plane.normal, planeNormal);
    if (planeDist) *planeDist = face->plane.dist;

    /* Face normals come from the plane */
    if (normals) {
        for (i = 0; i < numVerts; i++) {
            normals[i*3+0] = face->plane.normal[0];
            normals[i*3+1] = face->plane.normal[1];
            normals[i*3+2] = face->plane.normal[2];
        }
    }

    /* Colors: faces don't carry per-vertex colour in srfSurfaceFace_t */
    if (colors) {
        for (i = 0; i < numVerts * 4; i++) colors[i] = 255;
    }

    /* Indices */
    if (indices && outNumIndices) {
        int *faceIdx = (int *)((byte *)face + face->ofsIndices);
        int n = numIdx < maxIndices ? numIdx : maxIndices;
        for (i = 0; i < n; i++) {
            indices[i] = faceIdx[i];
        }
        *outNumIndices = n;
    }

    /* Lightmap extents */
    if (lmX) *lmX = face->lmX;
    if (lmY) *lmY = face->lmY;
    if (lmWidth) *lmWidth = face->lmWidth;
    if (lmHeight) *lmHeight = face->lmHeight;

    return numVerts;
}

/* Get grid mesh (patch) surface data (SF_GRID / srfGridMesh_t).
 * Returns number of verts. */
int Godot_BSP_GetGridSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals, float *texcoords,
    float *lmcoords, unsigned char *colors,
    int *outWidth, int *outHeight,
    int *lmX, int *lmY, int *lmWidth, int *lmHeight)
{
    msurface_t *surf;
    srfGridMesh_t *grid;
    int i, numVerts;

    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return 0;

    surf = &tr.world->surfaces[surfaceIndex];
    if (!surf->data || *(surfaceType_t *)surf->data != SF_GRID)
        return 0;

    grid = (srfGridMesh_t *)surf->data;
    numVerts = grid->width * grid->height;
    if (numVerts > maxVerts) numVerts = maxVerts;

    for (i = 0; i < numVerts; i++) {
        drawVert_t *v = &grid->verts[i];
        if (positions) {
            positions[i*3+0] = v->xyz[0];
            positions[i*3+1] = v->xyz[1];
            positions[i*3+2] = v->xyz[2];
        }
        if (normals) {
            normals[i*3+0] = v->normal[0];
            normals[i*3+1] = v->normal[1];
            normals[i*3+2] = v->normal[2];
        }
        if (texcoords) {
            texcoords[i*2+0] = v->st[0];
            texcoords[i*2+1] = v->st[1];
        }
        if (lmcoords) {
            lmcoords[i*2+0] = v->lightmap[0];
            lmcoords[i*2+1] = v->lightmap[1];
        }
        if (colors) {
            colors[i*4+0] = v->color[0];
            colors[i*4+1] = v->color[1];
            colors[i*4+2] = v->color[2];
            colors[i*4+3] = v->color[3];
        }
    }

    if (outWidth) *outWidth = grid->width;
    if (outHeight) *outHeight = grid->height;

    if (lmX) *lmX = grid->lmX;
    if (lmY) *lmY = grid->lmY;
    if (lmWidth) *lmWidth = grid->lmWidth;
    if (lmHeight) *lmHeight = grid->lmHeight;

    return numVerts;
}

/* Get triangle soup surface data (SF_TRIANGLES / srfTriangles_t). */
int Godot_BSP_GetTriangleSurfaceData(int surfaceIndex,
    int maxVerts, float *positions, float *normals,
    float *texcoords, float *lmcoords, unsigned char *colors,
    int maxIndices, int *indices, int *outNumIndices)
{
    msurface_t *surf;
    srfTriangles_t *tri;
    int i, numVerts, numIdx;

    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return 0;

    surf = &tr.world->surfaces[surfaceIndex];
    if (!surf->data || *(surfaceType_t *)surf->data != SF_TRIANGLES)
        return 0;

    tri = (srfTriangles_t *)surf->data;
    numVerts = tri->numVerts;
    numIdx = tri->numIndexes;
    if (numVerts > maxVerts) numVerts = maxVerts;

    for (i = 0; i < numVerts; i++) {
        drawVert_t *v = &tri->verts[i];
        if (positions) {
            positions[i*3+0] = v->xyz[0];
            positions[i*3+1] = v->xyz[1];
            positions[i*3+2] = v->xyz[2];
        }
        if (normals) {
            normals[i*3+0] = v->normal[0];
            normals[i*3+1] = v->normal[1];
            normals[i*3+2] = v->normal[2];
        }
        if (texcoords) {
            texcoords[i*2+0] = v->st[0];
            texcoords[i*2+1] = v->st[1];
        }
        if (lmcoords) {
            lmcoords[i*2+0] = v->lightmap[0];
            lmcoords[i*2+1] = v->lightmap[1];
        }
        if (colors) {
            colors[i*4+0] = v->color[0];
            colors[i*4+1] = v->color[1];
            colors[i*4+2] = v->color[2];
            colors[i*4+3] = v->color[3];
        }
    }

    if (indices && outNumIndices) {
        int n = numIdx < maxIndices ? numIdx : maxIndices;
        for (i = 0; i < n; i++) indices[i] = tri->indexes[i];
        *outNumIndices = n;
    }

    return numVerts;
}

/* Get surface cluster assignment for PVS mesh grouping.
 * Returns the cluster index for a surface, or -1 if unknown.
 * We determine this by checking which leaf the surface's midpoint falls in. */
int Godot_BSP_GetSurfaceCluster(int surfaceIndex)
{
    /* For world model surfaces, we check which leaf references them
     * via marksurfaces.  This is more reliable than midpoint lookup. */
    int i;

    if (!tr.world || surfaceIndex < 0 || surfaceIndex >= tr.world->numsurfaces)
        return -1;

    /* Walk all leaves to find which one contains this surface.
     * Leaves are nodes[numDecisionNodes..numnodes-1]. */
    for (i = tr.world->numDecisionNodes; i < tr.world->numnodes; i++) {
        mnode_t *leaf = &tr.world->nodes[i];
        int j;
        for (j = 0; j < leaf->nummarksurfaces; j++) {
            if (leaf->firstmarksurface[j] == &tr.world->surfaces[surfaceIndex]) {
                return leaf->cluster;
            }
        }
    }

    return -1;
}

/* Build a complete surface→cluster mapping in one pass.
 * Walks all leaves once and assigns each surface to the first cluster
 * that references it.  Much faster than per-surface GetSurfaceCluster()
 * calls for large maps. */
void Godot_BSP_BuildSurfaceClusterMap(int *surfaceClusterOut, int numSurfaces)
{
    int i, j;

    for (i = 0; i < numSurfaces; i++)
        surfaceClusterOut[i] = -1;

    if (!tr.world) return;

    /* Walk all leaves (nodes[numDecisionNodes..numnodes-1]) */
    for (i = tr.world->numDecisionNodes; i < tr.world->numnodes; i++) {
        mnode_t *leaf = &tr.world->nodes[i];
        if (leaf->cluster < 0) continue;

        for (j = 0; j < leaf->nummarksurfaces; j++) {
            int surfIdx = (int)(leaf->firstmarksurface[j] - tr.world->surfaces);
            if (surfIdx >= 0 && surfIdx < numSurfaces &&
                surfaceClusterOut[surfIdx] < 0) {
                surfaceClusterOut[surfIdx] = leaf->cluster;
            }
        }
    }
}

/* ===================================================================
 *  World lightmap data accessor
 *
 *  MOHAA BSPs store lightmaps in a single large lighting lump.
 *  Each lightmap "page" is LIGHTMAP_SIZE × LIGHTMAP_SIZE × 3 bytes.
 *  Surfaces reference pages by index and have sub-rect (lmX, lmY, lmW, lmH).
 * ================================================================ */

int Godot_BSP_GetLightmapPageCount(void)
{
    if (!tr.world || !tr.world->lighting)
        return 0;

    return tr.numLightmaps;
}

/* Get raw lightmap pixel data for a page.
 * Returns pointer to LIGHTMAP_SIZE×LIGHTMAP_SIZE×3 RGB bytes, or NULL. */
const unsigned char *Godot_BSP_GetLightmapPageData(int pageIndex)
{
    if (!tr.world || !tr.world->lighting || pageIndex < 0)
        return NULL;

    /* Each page is LIGHTMAP_SIZE² × 3 bytes */
    return tr.world->lighting + pageIndex * (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3);
}

/* ===================================================================
 *  World shader (dshader_t) accessors
 * ================================================================ */

int Godot_BSP_GetNumShaders(void)
{
    if (!tr.world) return 0;
    return tr.world->numShaders;
}

const char *Godot_BSP_GetShaderName(int shaderIndex)
{
    if (!tr.world || shaderIndex < 0 || shaderIndex >= tr.world->numShaders)
        return "";
    return tr.world->shaders[shaderIndex].shader;
}

int Godot_BSP_GetShaderSurfaceFlags(int shaderIndex)
{
    if (!tr.world || shaderIndex < 0 || shaderIndex >= tr.world->numShaders)
        return 0;
    return tr.world->shaders[shaderIndex].surfaceFlags;
}

int Godot_BSP_GetShaderContentFlags(int shaderIndex)
{
    if (!tr.world || shaderIndex < 0 || shaderIndex >= tr.world->numShaders)
        return 0;
    return tr.world->shaders[shaderIndex].contentFlags;
}
