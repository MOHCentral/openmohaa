/*
 * tr_godot_gl_stubs.c — No-op GL function pointer definitions.
 *
 * Under GODOT_GDEXTENSION, the renderer data-loading modules
 * (tr_shader.c, tr_image.c, tr_bsp.c, etc.) are compiled into the
 * main GDExtension .so.  They reference GL function pointers (qglXxx)
 * declared in qgl.h.  Those pointers are normally set by GLimp_Init
 * which loads the real GL library.
 *
 * Here we define all the qgl* function pointers as NULL (no-ops).
 * The few places that actually call them (image upload, texture
 * parameter setup) will check for NULL or simply do nothing — the
 * image_t metadata (width, height, imgName, format) is populated
 * before the GL calls, which is all the Godot side needs.
 */

#ifdef GODOT_GDEXTENSION

#include "../renderercommon/qgl.h"

/* ── qgl function pointers — all NULL ── */
void   (APIENTRYP qglActiveTextureARB)(GLenum) = 0;
void   (APIENTRYP qglClientActiveTextureARB)(GLenum) = 0;
void   (APIENTRYP qglMultiTexCoord2fARB)(GLenum, GLfloat, GLfloat) = 0;
void   (APIENTRYP qglLockArraysEXT)(GLint, GLsizei) = 0;
void   (APIENTRYP qglUnlockArraysEXT)(void) = 0;

/* Expand all QGL_*_PROCS macros to define the function pointers as NULL */
#define GLE(ret, name, ...) name##proc * qgl##name = 0;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_3_0_PROCS;
#undef GLE

/* Version globals */
int qglMajorVersion = 0;
int qglMinorVersion = 0;
int qglesMajorVersion = 0;
int qglesMinorVersion = 0;

/* ── Globals used by tr_local.h that are NOT provided by
 *    tr_init.c (since we ARE compiling tr_init.c) or tr_image.c.
 *    Globals from tr_main.c, tr_backend.c, tr_shade.c are defined
 *    here because those files are NOT compiled. ── */
#include "../renderercommon/tr_common.h"
#include "tr_local.h"

/* From tr_main.c (not compiled) */
trGlobals_t tr;

/* From tr_backend.c (not compiled) */
backEndState_t backEnd;
backEndData_t *backEndData = NULL;

volatile renderCommandList_t *renderCommandList = NULL;
volatile qboolean renderThreadActive = qfalse;

suninfo_t s_sun;

/* ── Renderer helper functions that are called but don't need GL ── */

/* GL_Bind — called extensively by image loading code.
 * Under Godot, this is a no-op since there's no GL context.
 * The image_t metadata (dims, name, format) is what we need. */
void GL_Bind(image_t *image) {
    (void)image;
}

void GL_SelectTexture(int unit) {
    (void)unit;
}

void GL_State(unsigned long stateVector) {
    (void)stateVector;
}

void GL_TexEnv(int env) {
    (void)env;
}

void GL_Cull(int cullType) {
    (void)cullType;
}

void GL_SetDefaultState(void) {
}

void GL_CheckErrors(void) {
}

void GL_SetFogColor(const vec4_t fColor) {
    (void)fColor;
}

void R_IssuePendingRenderCommands(void) {
}

void R_SyncRenderThread(void) {
}

void *R_GetCommandBuffer(int bytes) {
    (void)bytes;
    return NULL;
}

void R_AddDrawSurfCmd(drawSurf_t *drawSurfs, int numDrawSurfs) {
    (void)drawSurfs; (void)numDrawSurfs;
}

void R_AddSpriteSurfCmd(drawSurf_t *drawSurfs, int numDrawSurfs) {
    (void)drawSurfs; (void)numDrawSurfs;
}

/* ── GLimp stubs — no GL context to init ── */

void GLimp_Init(qboolean fixedFunction) {
    (void)fixedFunction;
    /* Populate glConfig with sensible defaults so renderer code doesn't crash. */
    glConfig.vidWidth = 1280;
    glConfig.vidHeight = 720;
    glConfig.colorBits = 32;
    glConfig.depthBits = 24;
    glConfig.stencilBits = 8;
    glConfig.textureCompression = TC_NONE;
    glConfig.maxTextureSize = 4096;
    glConfig.numTextureUnits = 2;
    glConfig.textureEnvAddAvailable = qtrue;
    glConfig.isFullscreen = qfalse;
    glConfig.windowAspect = 1280.0f / 720.0f;
}

void GLimp_Shutdown(void) {
}

void GLimp_EndFrame(void) {
}

void GLimp_LogComment(char *comment) {
    (void)comment;
}

void GLimp_Minimize(void) {
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256]) {
    (void)red; (void)green; (void)blue;
}

qboolean GLimp_SpawnRenderThread(void (*function)(void)) {
    (void)function;
    return qfalse;
}

void *GLimp_RendererSleep(void) {
    return NULL;
}

void GLimp_FrontEndSleep(void) {
}

void GLimp_WakeRenderer(void *data) {
    (void)data;
}

/* ── Render backend stubs — these are GL draw path functions
 * that data-loading code may reference but never needs to execute ── */

void RB_BeginSurface(shader_t *shader) { (void)shader; }
void RB_EndSurface(void) {}
void RB_CheckOverflow(int verts, int indexes) { (void)verts; (void)indexes; }

void RB_StageIteratorGeneric(void) {}
void RB_StageIteratorSky(void) {}
void RB_StageIteratorVertexLitTextureUnfogged(void) {}
void RB_StageIteratorLightmappedMultitextureUnfogged(void) {}

void RB_ExecuteRenderCommands(const void *data) { (void)data; }

void RB_AddFlare(void *surface, int fogNum, vec3_t point, vec3_t color, vec3_t normal) {
    (void)surface; (void)fogNum; (void)point; (void)color; (void)normal;
}

void RB_AddDlightFlares(void) {}
void RB_RenderFlares(void) {}

void RB_RenderThread(void) {}

void RB_DrawSprite(const refSprite_t *spr) { (void)spr; }
void RB_DrawSwipeSurface(surfaceType_t *pswipe) { (void)pswipe; }

void RB_SkelMesh(skelSurfaceGame_t *sf) { (void)sf; }
void RB_StaticMesh(staticSurface_t *staticSurf) { (void)staticSurf; }
void RB_Static_BuildDLights(void) {}

void RB_AddQuadStamp(vec3_t origin, vec3_t left, vec3_t up, byte *color) {
    (void)origin; (void)left; (void)up; (void)color;
}
void RB_AddQuadStampExt(vec3_t origin, vec3_t left, vec3_t up, byte *color,
                        float s1, float t1, float s2, float t2) {
    (void)origin; (void)left; (void)up; (void)color;
    (void)s1; (void)t1; (void)s2; (void)t2;
}

/* ── Shade/tess calc stubs ── */
void RB_DeformTessGeometry(void) {}
void RB_CalcEnvironmentTexCoords(float *st) { (void)st; }
void RB_CalcEnvironmentTexCoords2(float *st) { (void)st; }
void RB_CalcSunReflectionTexCoords(float *st) { (void)st; }
void RB_CalcScrollTexCoords(const float scroll[2], float *st) { (void)scroll; (void)st; }
void RB_CalcRotateTexCoords(float speed, float coef, float *st, float start) { (void)speed; (void)coef; (void)st; (void)start; }
void RB_CalcScaleTexCoords(const float scale[2], float *st) { (void)scale; (void)st; }
void RB_CalcTurbulentTexCoords(const waveForm_t *wf, float *st) { (void)wf; (void)st; }
void RB_CalcTransformTexCoords(const texModInfo_t *tmi, float *st) { (void)tmi; (void)st; }
void RB_CalcStretchTexCoords(const waveForm_t *wf, float *st) { (void)wf; (void)st; }
void RB_CalcTransWaveTexCoords(const waveForm_t *wf, float *st) { (void)wf; (void)st; }
void RB_CalcTransWaveTexCoordsT(const waveForm_t *wf, float *st) { (void)wf; (void)st; }
void RB_CalcBulgeTexCoords(const waveForm_t *wf, float *st) { (void)wf; (void)st; }
void RB_CalcOffsetTexCoords(const float *offset, float *st) { (void)offset; (void)st; }
void RB_CalcParallaxTexCoords(const float *rate, float *st) { (void)rate; (void)st; }
void RB_CalcMacroTexCoords(const float *rate, float *st) { (void)rate; (void)st; }

void RB_CalcWaveAlpha(const waveForm_t *wf, unsigned char *colors) { (void)wf; (void)colors; }
void RB_CalcWaveColor(const waveForm_t *wf, unsigned char *colors, unsigned char *cc) { (void)wf; (void)colors; (void)cc; }
void RB_CalcAlphaFromEntity(unsigned char *colors) { (void)colors; }
void RB_CalcAlphaFromOneMinusEntity(unsigned char *colors) { (void)colors; }
void RB_CalcColorFromEntity(unsigned char *colors) { (void)colors; }
void RB_CalcColorFromOneMinusEntity(unsigned char *colors) { (void)colors; }
void RB_CalcColorFromConstant(unsigned char *colors, unsigned char *cc) { (void)colors; (void)cc; }
void RB_CalcAlphaFromConstant(unsigned char *colors, int ca) { (void)colors; (void)ca; }
void RB_CalcAlphaFromDot(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcAlphaFromOneMinusDot(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcAlphaFromDotView(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcAlphaFromOneMinusDotView(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcAlphaFromHeightFade(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcAlphaFromTexCoords(unsigned char *colors, float amin, float amax, int aminCap, int acap, float sw, float tw, float *st) {
    (void)colors; (void)amin; (void)amax; (void)aminCap; (void)acap; (void)sw; (void)tw; (void)st;
}
void RB_CalcRGBFromTexCoords(unsigned char *colors, float amin, float amax, int aminCap, int acap, float sw, float tw, float *st) {
    (void)colors; (void)amin; (void)amax; (void)aminCap; (void)acap; (void)sw; (void)tw; (void)st;
}
void RB_CalcRGBFromDot(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcRGBFromOneMinusDot(unsigned char *colors, float min, float max) { (void)colors; (void)min; (void)max; }
void RB_CalcSpecularAlpha(unsigned char *alphas, float max, vec3_t origin) { (void)alphas; (void)max; (void)origin; }
void RB_CalcLightGridColor(unsigned char *colors) { (void)colors; }
void RB_CalcDiffuseColor(unsigned char *colors) { (void)colors; }

void RB_TextureAxisFromPlane(const vec3_t normal, vec3_t xv, vec3_t yv) { (void)normal; (void)xv; (void)yv; }
void RB_QuakeTextureVecs(const vec3_t normal, const vec2_t scale, vec3_t vecs[2]) { (void)normal; (void)scale; (void)vecs; }

/* Render stream stubs */
void RB_StreamBegin(shader_t *shader) { (void)shader; }
void RB_StreamEnd(void) {}
void RB_StreamBeginDrawSurf(void) {}
void RB_StreamEndDrawSurf(void) {}
void RB_Vertex3fv(vec3_t v) { (void)v; }
void RB_Vertex3f(vec_t x, vec_t y, vec_t z) { (void)x; (void)y; (void)z; }
void RB_Vertex2f(vec_t x, vec_t y) { (void)x; (void)y; }
void RB_Color4f(vec_t r, vec_t g, vec_t b, vec_t a) { (void)r; (void)g; (void)b; (void)a; }
void RB_Color3f(vec_t r, vec_t g, vec_t b) { (void)r; (void)g; (void)b; }
void RB_Color3fv(vec3_t col) { (void)col; }
void RB_Color4bv(unsigned char *colors) { (void)colors; }
void RB_Texcoord2f(float s, float t) { (void)s; (void)t; }
void RB_Texcoord2fv(vec2_t st) { (void)st; }
void RB_ShowImages(qboolean quiet) { (void)quiet; }

/* Additional rendering stubs */
void R_RenderView(viewParms_t *parms) { (void)parms; }
void R_AddWorldSurfaces(void) {}
void R_AddBrushModelSurfaces(trRefEntity_t *e) { (void)e; }
void R_AddMD3Surfaces(trRefEntity_t *e) { (void)e; }
void R_AddNullModelSurfaces(trRefEntity_t *e) { (void)e; }
void R_AddBeamSurfaces(trRefEntity_t *e) { (void)e; }
void R_AddRailSurfaces(trRefEntity_t *e, qboolean underwater) { (void)e; (void)underwater; }
void R_AddLightningBoltSurfaces(trRefEntity_t *e) { (void)e; }
void R_AddPolygonSurfaces(void) {}
void R_AddSwipeSurfaces(void) {}
void R_AddSkelSurfaces(trRefEntity_t *e) { (void)e; }
void R_AddStaticModelSurfaces(void) {}
void R_AddTerrainSurfaces(void) {}
void R_AddTerrainMarkSurfaces(void) {}
void R_AddAnimSurfaces(trRefEntity_t *e) { (void)e; }

void R_RotateForEntity(const trRefEntity_t *ent, const viewParms_t *vp, orientationr_t *ori) { (void)ent; (void)vp; (void)ori; }
void R_RotateForStaticModel(cStaticModelUnpacked_t *sm, const viewParms_t *vp, orientationr_t *ori) { (void)sm; (void)vp; (void)ori; }
void R_RotateForViewer(void) {}
void R_SetupFrustum(void) {}

/* Transform stubs */
void R_TransformModelToClip(const vec3_t src, const float *mm, const float *pm, vec4_t eye, vec4_t dst) {
    (void)src; (void)mm; (void)pm; (void)eye; (void)dst;
}
void R_TransformClipToWindow(const vec4_t clip, const viewParms_t *view, vec4_t norm, vec4_t window) {
    (void)clip; (void)view; (void)norm; (void)window;
}

/* Light stubs */
void R_SetupEntityLighting(const trRefdef_t *refdef, trRefEntity_t *ent) { (void)refdef; (void)ent; }
void R_DlightBmodel(bmodel_t *bmodel) { (void)bmodel; }
void R_TransformDlights(int count, dlight_t *dl, orientationr_t *ori) { (void)count; (void)dl; (void)ori; }
void RB_Sphere_BuildDLights(void) {}
void RB_Sphere_SetupEntity(void) {}
void RB_Grid_SetupEntity(void) {}
void RB_Grid_SetupStaticModel(void) {}
void RB_Light_Real(unsigned char *colors) { (void)colors; }
void RB_Light_Fullbright(unsigned char *colors) { (void)colors; }
void R_Sphere_InitLights(void) {}
void RB_SetupEntityGridLighting(void) {}
void RB_SetupStaticModelGridLighting(trRefdef_t *refdef, cStaticModelUnpacked_t *ent, const vec3_t origin) { (void)refdef; (void)ent; (void)origin; }
int R_RealDlightPatch(srfGridMesh_t *srf, int bit) { (void)srf; return bit; }
int R_RealDlightFace(srfSurfaceFace_t *srf, int bits) { (void)srf; return bits; }
int R_RealDlightTerrain(cTerraPatchUnpacked_t *srf, int bits) { (void)srf; return bits; }
void R_ClearRealDlights(void) {}
void R_UploadDlights(void) {}

/* Shadow stubs */
void RB_ShadowTessEnd(void) {}
void RB_ComputeShadowVolume(void) {}
void RB_ShadowFinish(void) {}
void RB_ProjectionShadowDeform(void) {}

/* Sky stubs */
void R_BuildCloudData(shaderCommands_t *shader) { (void)shader; }
void R_InitSkyTexCoords(float height) { (void)height; }
void R_DrawSkyBox(shaderCommands_t *shader) { (void)shader; }
void RB_DrawSun(void) {}
void RB_ClipSkyPolygons(shaderCommands_t *shader) { (void)shader; }

/* Flare stubs */
void R_ClearFlares(void) {}

/* Mark stubs */
void R_LevelMarksLoad(const char *name) { (void)name; }
void R_LevelMarksInit(void) {}
void R_LevelMarksFree(void) {}
void R_UpdateLevelMarksSystem(void) {}
void R_AddPermanentMarkFragmentSurfaces(void **frag, int num) { (void)frag; (void)num; }

/* Sky portal stubs */
void R_Sky_Init(void) {}
void R_Sky_Reset(void) {}
void R_Sky_AddSurf(msurface_t *surf) { (void)surf; }
void R_Sky_Render(void) {}

/* Sun flare stubs */
void R_InitLensFlare(void) {}
void R_DrawLensFlares(void) {}

/* Swipe stubs */
void RE_SwipeBegin(float thistime, float life, qhandle_t shader) { (void)thistime; (void)life; (void)shader; }
void RE_SwipeEnd(void) {}

/* Ghost stubs */
void R_UpdateGhostTextures(void) {}
void R_SetGhostImage(const char *name, image_t *image) { (void)name; (void)image; }
void LoadGHOST(const char *name, byte **pic, int *width, int *height) {
    (void)name; if(pic) *pic = NULL; if(width) *width = 0; if(height) *height = 0;
}

/* Terrain stubs */
void R_InitTerrain(void) {}
void R_ShutdownTerrain(void) {}
void R_TerrainFree(void) {}
void R_TerrainPrepareFrame(void) {}
qboolean R_TerrainHeightForPoly(cTerraPatchUnpacked_t *p, polyVert_t *v, int n) { (void)p; (void)v; (void)n; return qfalse; }
void R_SwapTerraPatch(cTerraPatch_t *p) { (void)p; }
void R_MarkTerrainPatch(cTerraPatchUnpacked_t *p) { (void)p; }
void R_TerrainCrater_f(void) {}
int R_DlightTerrain(cTerraPatchUnpacked_t *s, int bits) { (void)s; return bits; }
int R_CheckDlightTerrain(cTerraPatchUnpacked_t *s, int bits) { (void)s; return bits; }

/* Debug line stubs */
void R_DrawDebugNumber(const vec3_t org, float number, float scale, float r, float g, float b, int precision) {
    (void)org; (void)number; (void)scale; (void)r; (void)g; (void)b; (void)precision;
}
void R_DebugRotatedBBox(const vec3_t org, const vec3_t ang, const vec3_t mins, const vec3_t maxs, float r, float g, float b, float alpha) {
    (void)org; (void)ang; (void)mins; (void)maxs; (void)r; (void)g; (void)b; (void)alpha;
}
void R_DebugCircle(const vec3_t org, float radius, float r, float g, float b, float alpha, qboolean horizontal) {
    (void)org; (void)radius; (void)r; (void)g; (void)b; (void)alpha; (void)horizontal;
}
void R_DebugLine(const vec3_t start, const vec3_t end, float r, float g, float b, float alpha) {
    (void)start; (void)end; (void)r; (void)g; (void)b; (void)alpha;
}
void R_DebugSkeleton(void) {}

/* 2D drawing stubs */
void Draw_SetColor(const vec4_t rgba) { (void)rgba; }
void Draw_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) {
    (void)x; (void)y; (void)w; (void)h; (void)s1; (void)t1; (void)s2; (void)t2; (void)hShader;
}
void Draw_StretchPic2(float x, float y, float w, float h, float s1, float t1, float s2, float t2, float sx, float sy, qhandle_t hShader) {
    (void)x; (void)y; (void)w; (void)h; (void)s1; (void)t1; (void)s2; (void)t2; (void)sx; (void)sy; (void)hShader;
}
void Draw_TilePic(float x, float y, float w, float h, qhandle_t hShader) {
    (void)x; (void)y; (void)w; (void)h; (void)hShader;
}
void Draw_TilePicOffset(float x, float y, float w, float h, qhandle_t hShader, int ox, int oy) {
    (void)x; (void)y; (void)w; (void)h; (void)hShader; (void)ox; (void)oy;
}
void Draw_TrianglePic(const vec2_t vPoints[3], const vec2_t vTexCoords[3], qhandle_t hShader) {
    (void)vPoints; (void)vTexCoords; (void)hShader;
}
void DrawBox(float x, float y, float w, float h) { (void)x; (void)y; (void)w; (void)h; }
void AddBox(float x, float y, float w, float h) { (void)x; (void)y; (void)w; (void)h; }
void Set2DWindow(int x, int y, int w, int h, float left, float right, float bottom, float top, float n, float f) {
    (void)x; (void)y; (void)w; (void)h; (void)left; (void)right; (void)bottom; (void)top; (void)n; (void)f;
}
void Set2DInitialShaderTime(float startTime) { (void)startTime; }
void RE_Scissor(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void DrawLineLoop(const vec2_t *points, int count, int factor, int mask) {
    (void)points; (void)count; (void)factor; (void)mask;
}
void RE_StretchRaw(int x, int y, int w, int h, int cols, int rows, int comp, const byte *data) {
    (void)x; (void)y; (void)w; (void)h; (void)cols; (void)rows; (void)comp; (void)data;
}
void RE_UploadCinematic(int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty) {
    (void)w; (void)h; (void)cols; (void)rows; (void)data; (void)client; (void)dirty;
}

/* Screenshot/video stubs */
void RE_SaveJPG(char *fn, int q, int w, int h, unsigned char *buf, int pad) {
    (void)fn; (void)q; (void)w; (void)h; (void)buf; (void)pad;
}
size_t RE_SaveJPGToBuffer(byte *buf, size_t sz, int q, int w, int h, byte *img, int pad) {
    (void)buf; (void)sz; (void)q; (void)w; (void)h; (void)img; (void)pad; return 0;
}
void RE_TakeVideoFrame(int w, int h, byte *cap, byte *enc, qboolean mjpeg) {
    (void)w; (void)h; (void)cap; (void)enc; (void)mjpeg;
}
void SaveJPG(char *fn, int q, int w, int h, unsigned char *buf) {
    (void)fn; (void)q; (void)w; (void)h; (void)buf;
}

const void *RB_TakeScreenshotCmd(const void *data) { return data; }
const void *RB_TakeVideoFrameCmd(const void *data) { return data; }
void R_ScreenShot_f(void) {}

/* Performance counter stub */
void R_SavePerformanceCounters(void) {}

/* UI stub */
void UI_LoadResource(const char *name) { (void)name; }

/* ── Tess commands global ── */
shaderCommands_t tess;

/* ── Renderer surface table — data-loading code references but never calls ── */
void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])(void *);

#endif /* GODOT_GDEXTENSION */
