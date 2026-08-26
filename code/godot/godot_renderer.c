/*
 * godot_renderer.c — Godot renderer backend for the OpenMoHAA GDExtension.
 *
 * Implements the refexport_t interface (GetRefAPI) using stub functions.
 * The engine's client code calls these through the `re` function table.
 *
 * In the final implementation, 2D drawing will go through Godot's
 * CanvasItem / RenderingServer, and 3D rendering through Godot nodes.
 * For now, all functions are no-ops that return valid (but dummy) data
 * so the engine boots into client mode without crashing.
 *
 * This file replaces renderergl1/tr_init.c's GetRefAPI.
 */

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"
#include "godot_shader_props.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

/* Forward declarations used before their definitions. */
const char *Godot_Renderer_GetShaderRemap( const char *shaderName );

/* -------------------------------------------------------------------
 *  Globals
 * ---------------------------------------------------------------- */

/* Engine → renderer callbacks.
 * Non-static so the real renderer data modules (tr_shader.c, tr_image.c,
 * tr_bsp.c, etc.) can use ri.Printf, ri.FS_ReadFile, ri.TIKI_* through
 * the extern declaration in tr_local.h. */
refimport_t ri;                /* engine → renderer callbacks */

/* -------------------------------------------------------------------
 * Model table (TIKI model registration)
 *
 *  Mirrors the renderer's tr.models[] to provide valid qhandle_t →
 *  dtiki_t* mapping.  Without this, GR_Model_GetHandle returns NULL
 *  and cgame skips all entity rendering.
 * ---------------------------------------------------------------- */

#define GR_MAX_MODELS  1024

static qhandle_t GR_RegisterShader( const char *name );

typedef enum {
    GR_MOD_BAD,
    GR_MOD_BRUSH,
    GR_MOD_TIKI,
    GR_MOD_SPRITE
} gr_modtype_t;

typedef struct {
    char          name[MAX_QPATH];  /* matches model_t.name in the real renderer */
    gr_modtype_t  type;
    int           index;
    qboolean      serveronly;
    dtiki_t      *tiki;       /* only set for GR_MOD_TIKI */
    int           shaderHandle;
    /* Sprite model dimensions (GR_MOD_SPRITE only) — from image width/height */
    float         spriteWidth;     /* source image pixel width  */
    float         spriteHeight;    /* source image pixel height */
    float         spriteScale;     /* shader spritescale keyword (default 1.0) */
    int           realModelHandle; /* Handle in tr.models[] for engine pipeline use */
} gr_model_t;

static gr_model_t gr_models[GR_MAX_MODELS];
static int        gr_numModels = 0;

static void GR_ModelInit( void )
{
    memset( gr_models, 0, sizeof( gr_models ) );
    gr_numModels = 1;   /* slot 0 = sentinel bad model */
    Q_strncpyz( gr_models[0].name, "** BAD MODEL **", sizeof( gr_models[0].name ) );
    gr_models[0].type = GR_MOD_BAD;
}

/* Internal registration — handles TIKI, brush inline models, and sprites. */
static qhandle_t GR_RegisterModelInternal( const char *name, qboolean bBeginTiki, qboolean use )
{
    int         i;
    const char *ext;
    gr_model_t *mod;

    if ( !name || !name[0] ) return 0;
    if ( strlen( name ) >= MAX_QPATH ) return 0;

    /* ── Search existing models ── */
    /* GR_MOD_BAD slots arise when CG_ExecuteNewServerCommands' first pass
     * (modelOnly=qtrue) calls GR_UnregisterServerModel() on every CS_MODELS
     * entry that was registered by CG_PrepRefresh().  The second pass then
     * calls GR_RegisterServerModel() with the same name.  If we returned 0
     * on a BAD hit, cgs.model_draw[] would stay 0 → model.tiki = NULL →
     * entity invisible.  Re-using (re-loading) the bad slot fixes this. */
    int bad_slot = 0;
    for ( i = 1; i < gr_numModels; i++ ) {
        if ( !Q_stricmp( gr_models[i].name, name ) ) {
            if ( gr_models[i].type == GR_MOD_BAD ) {
                bad_slot = i;   /* Will re-initialise this slot below */
                break;
            }
            return i;   /* Already loaded and valid */
        }
    }

    /* ── Allocate new slot (or re-use a previously unregistered BAD slot) ── */
    int new_slot;
    if ( bad_slot > 0 ) {
        new_slot = bad_slot;
    } else {
        if ( gr_numModels >= GR_MAX_MODELS ) {
            ri.Printf( PRINT_WARNING, "[GodotRenderer] Model table full, cannot register '%s'\n", name );
            return 0;
        }
        new_slot = gr_numModels;
        gr_numModels++;
    }

    mod = &gr_models[new_slot];
    memset( mod, 0, sizeof( *mod ) );
    Q_strncpyz( mod->name, name, sizeof( mod->name ) );
    mod->index = new_slot;
    mod->serveronly = qtrue;

    /* ── BSP inline models (*N) ── */
    if ( name[0] == '*' ) {
        mod->type = GR_MOD_BRUSH;
        return mod->index;
    }

    /* ── Dispatch by extension ── */
    ext = strrchr( name, '.' );
    if ( ext ) {
        ext++;

        if ( !Q_stricmp( ext, "tik" ) ) {
            mod->tiki = ri.TIKI_RegisterTikiFlags( name, use );
            if ( mod->tiki ) {
                mod->type = GR_MOD_TIKI;

                /* Process init commands (spawn effects, sounds) */
                if ( bBeginTiki && ri.CG_ProcessInitCommands ) {
                    ri.CG_ProcessInitCommands( mod->tiki, NULL );
                }

                ri.Printf( PRINT_DEVELOPER,
                    "[GodotRenderer] RegisterModel TIKI: %s → %d\n",
                    name, mod->index );
                return mod->index;
            }
            ri.Printf( PRINT_ALL,
                "[GodotRenderer] TIKI load failed: %s\n", name );
        } else if ( !Q_stricmp( ext, "spr" ) ) {
            /* Sprites: The shader name is the sprite filename without the .spr extension. */
            char shadername[MAX_QPATH];
            COM_StripExtension( name, shadername, sizeof(shadername) );

            mod->type = GR_MOD_SPRITE;
            mod->shaderHandle = GR_RegisterShader( shadername );

            /* Also register in the real tr.models[] table so that the
             * engine's RB_DrawSprite can be called for verification and
             * eventual pipeline capture. */
            {
                extern int Godot_RealModel_RegisterSprite(const char *name);
                mod->realModelHandle = Godot_RealModel_RegisterSprite( name );
            }

            /* Retrieve source image dimensions and spritescale from the real
             * shader_t data.  This mirrors SPR_RegisterSprite (tr_sprite.c):
             * the quad half-extents are (width*0.5*scale, height*0.5*scale)
             * in engine inches. */
            {
                int iw = 0, ih = 0;
                float ss = 1.0f;
                extern int Godot_Sprite_GetEngineSize(const char *shader_name,
                    int *out_width, int *out_height, float *out_sprite_scale);
                if ( Godot_Sprite_GetEngineSize( shadername, &iw, &ih, &ss ) ) {
                    mod->spriteWidth  = (float)iw;
                    mod->spriteHeight = (float)ih;
                    mod->spriteScale  = (ss > 0.0f) ? ss : 1.0f;
                } else {
                    /* Fallback: assume a reasonable default */
                    mod->spriteWidth  = 64.0f;
                    mod->spriteHeight = 64.0f;
                    mod->spriteScale  = 1.0f;
                }
            }

            ri.Printf( PRINT_DEVELOPER,
                "[GodotRenderer] RegisterModel SPRITE: %s -> shader '%s' (handle #%d) dims=%dx%d spriteScale=%.2f realH=%d\n",
                name, shadername, mod->shaderHandle,
                (int)mod->spriteWidth, (int)mod->spriteHeight, mod->spriteScale,
                mod->realModelHandle );
            return mod->index;
        }
    }

    /* Unknown type or load failure */
    ri.Printf( PRINT_ALL,
        "[GodotRenderer] RegisterModel FAILED: %s\n", name );
    mod->type = GR_MOD_BAD;
    return 0;
}

static int next_shader_handle = 1;
static int next_skin_handle   = 1;
static int gr_fontHandlesDirty = 0;

/* -------------------------------------------------------------------
 * Shader name table (2D overlay rendering)
 *
 *  Maps qhandle_t → shader name so MoHAARunner can load textures
 *  via the engine VFS and display 2D HUD elements in Godot.
 * ---------------------------------------------------------------- */

#define GR_MAX_SHADERS 2048

typedef struct {
    char name[MAX_QPATH];
    int  nomip;      /* 1 if registered with RegisterShaderNoMip */
} gr_shader_t;

static gr_shader_t gr_shaders[GR_MAX_SHADERS];
static int         gr_numShaders = 1;  /* slot 0 = sentinel */

/* -------------------------------------------------------------------
 * 2D draw command buffer
 *
 *  Each frame, GR_DrawStretchPic / GR_DrawBox / GR_SetColor calls
 *  append commands here.  GR_ClearScene resets the count.
 *  MoHAARunner reads via Godot_Renderer_Get2DCmd* accessors.
 * ---------------------------------------------------------------- */

#define GR_MAX_2D_CMDS 8192

typedef enum {
    GR_2D_STRETCHPIC,  /* textured quad */
    GR_2D_BOX,         /* solid colour box */
 GR_2D_SCISSOR, /* scissor region change */
    GR_2D_TRIANGLE,    /* textured triangle (compass, needle, circle, spinner) */
} gr_2d_type_t;

typedef struct {
    gr_2d_type_t type;
    float x, y, w, h;        /* screen-space rectangle */
    float s1, t1, s2, t2;    /* texture coordinates (for STRETCHPIC) */
    float color[4];           /* RGBA at time of draw */
    int   shader;             /* qhandle_t for texture lookup */
    /* Triangle vertex data (for GR_2D_TRIANGLE only) */
    float tri_verts[3][2];    /* 3 vertex positions in screen space */
    float tri_uvs[3][2];      /* 3 vertex UV coordinates */
} gr_2d_cmd_t;

static gr_2d_cmd_t gr_2d_cmds[GR_MAX_2D_CMDS];
static int         gr_num2DCmds = 0;

static float current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

/* -------------------------------------------------------------------
 * Cinematic frame buffer
 *
 *  GR_DrawStretchRaw stores decoded RoQ frames here.
 *  MoHAARunner reads via Godot_Renderer_GetCinematic* accessors
 *  and displays on a TextureRect.
 * ---------------------------------------------------------------- */

#define GR_CIN_MAX_WIDTH  1024
#define GR_CIN_MAX_HEIGHT 1024

static byte  gr_cin_buffer[GR_CIN_MAX_WIDTH * GR_CIN_MAX_HEIGHT * 4];
static int   gr_cin_width   = 0;
static int   gr_cin_height  = 0;
static int   gr_cin_dirty   = 0;   /* set to 1 each time a new frame arrives */
static int   gr_cin_active  = 0;   /* 1 while a cinematic is playing */

/* A scratch refEntity_t returned by GetRenderEntity */
static refEntity_t scratch_entity;
static int   scratch_bone_tag[5];
static float scratch_bone_quat[5][4];

/* ── Loading screen force-render callback ──
 * During map loading, SCR_UpdateScreen() is called multiple times within a
 * single Com_Frame().  Each call goes through BeginFrame → draw → EndFrame.
 * Under Godot the stub EndFrame is a no-op, so none of these intermediate
 * frames are ever presented.  We detect this by counting EndFrame calls per
 * Com_Frame() cycle.  On the 2nd+ EndFrame, we invoke a callback that triggers
 * an actual Godot frame render (update_2d_overlay + force_draw) so the loading
 * screen is visible instead of a black screen. */
static int  gr_endframe_count = 0;
static void (*gr_force_render_callback)(void) = NULL;

void Godot_Renderer_SetForceRenderCallback(void (*cb)(void))
{
    gr_force_render_callback = cb;
}

void Godot_Renderer_ResetEndFrameCount(void)
{
    gr_endframe_count = 0;
}

/* Per-entity skeleton pose tracking — mirrors tr.skel_index[] / tr.frame_skel_index
 * in the real renderer.  Prevents redundant SetPose calls within a single frame
 * while ensuring TIKI_Orientation always has a current pose to query. */
static int gr_frame_skel_index = 0;
static int gr_skel_index[MAX_GENTITIES];

/* Scratch glconfig filled during BeginRegistration */
static glconfig_t stored_glconfig;

/* vid_restart detection — set in GR_BeginRegistration, consumed by MoHAARunner */
static int gr_vidRestarted = 0;
static int gr_vidRestart_fullscreen = 0;    /* r_fullscreen value at restart time */
static int gr_vidRestart_width  = 0;        /* resolved window width */
static int gr_vidRestart_height = 0;        /* resolved window height */

/* Video mode table — expanded with widescreen and ultrawide resolutions.
 * Modes 0-11 match the original renderer (tr_init.c) for backwards
 * compatibility with saved r_mode values.  Modes 12+ add common
 * modern resolutions including 16:9, 16:10, and 21:9 aspect ratios. */
typedef struct {
    int width, height;
} gr_vidmode_t;

static const gr_vidmode_t gr_vidModes[] = {
    /* Original modes 0-11 (unchanged for saved-config compat) */
    {  320,  240 },  /* Mode  0: 4:3   */
    {  400,  300 },  /* Mode  1: 4:3   */
    {  512,  384 },  /* Mode  2: 4:3   */
    {  640,  480 },  /* Mode  3: 4:3   */
    {  800,  600 },  /* Mode  4: 4:3   */
    {  960,  720 },  /* Mode  5: 4:3   */
    { 1024,  768 },  /* Mode  6: 4:3   */
    { 1152,  864 },  /* Mode  7: 4:3   */
    { 1280, 1024 },  /* Mode  8: 5:4   */
    { 1600, 1200 },  /* Mode  9: 4:3   */
    { 2048, 1536 },  /* Mode 10: 4:3   */
    {  856,  480 },  /* Mode 11: ~16:9 (legacy wide) */
    /* 16:9 widescreen */
    { 1280,  720 },  /* Mode 12: 16:9  (720p)  */
    { 1366,  768 },  /* Mode 13: ~16:9         */
    { 1600,  900 },  /* Mode 14: 16:9          */
    { 1920, 1080 },  /* Mode 15: 16:9  (1080p) */
    { 2560, 1440 },  /* Mode 16: 16:9  (1440p) */
    { 3840, 2160 },  /* Mode 17: 16:9  (4K)    */
    /* 16:10 widescreen */
    { 1280,  800 },  /* Mode 18: 16:10         */
    { 1440,  900 },  /* Mode 19: 16:10         */
    { 1680, 1050 },  /* Mode 20: 16:10         */
    { 1920, 1200 },  /* Mode 21: 16:10         */
    { 2560, 1600 },  /* Mode 22: 16:10         */
    /* Ultrawide 21:9 */
    { 2560, 1080 },  /* Mode 23: 21:9          */
    { 3440, 1440 },  /* Mode 24: 21:9          */
};
static const int gr_numVidModes = (int)( sizeof(gr_vidModes) / sizeof(gr_vidModes[0]) );

/* Desktop resolution — set by MoHAARunner before Com_Init so
 * GR_BeginRegistration can resolve r_mode -2 correctly. */
static int gr_desktopWidth  = 1920;
static int gr_desktopHeight = 1080;

/* Physical screen/monitor resolution — set by MoHAARunner at startup.
 * Used for widescreen aspect ratio adjustment when going fullscreen:
 * the engine width is expanded to match the screen's aspect ratio
 * so the cgame's hor+ FOV calculation widens the view instead of
 * stretching.  Separate from gr_desktopWidth/Height which tracks
 * the Godot window size for r_mode -2. */
static int gr_screenWidth   = 0;
static int gr_screenHeight  = 0;

/* Actual Godot viewport size — tracked separately from stored_glconfig
 * so that glConfig (engine resolution) is never clobbered by per-frame
 * viewport sync.  MoHAARunner calls SetViewportSize each frame; the
 * Godot-side ui_scale divides viewport by engine resolution. */
static int gr_viewport_width  = 0;
static int gr_viewport_height = 0;

/* -------------------------------------------------------------------
 * Camera bridge — captured refdef_t data
 *
 *  GR_RenderScene stores the latest viewpoint here each frame.
 *  MoHAARunner.cpp reads it via the Godot_Renderer_* C accessors
 *  to update the Godot Camera3D.
 * ---------------------------------------------------------------- */

static float   gr_viewOrigin[3]    = { 0.0f, 0.0f, 0.0f };
static float   gr_viewAxis[3][3]   = { {1,0,0}, {0,1,0}, {0,0,1} };
static float   gr_fov_x            = 90.0f;
static float   gr_fov_y            = 73.74f;
static int     gr_renderWidth      = 1280;
static int     gr_renderHeight     = 720;
static float   gr_farplane_distance = 0.0f;
static float   gr_farplane_bias     = 0.0f;
static float   gr_farplane_color[3] = { 0.0f, 0.0f, 0.0f };
static qboolean gr_farplane_cull    = qfalse;
static qboolean gr_skybox_farplane  = qfalse;
static qboolean gr_renderTerrain    = qtrue;
static qboolean gr_hasNewFrame     = qfalse;
static int     gr_frameCount       = 0;

/* World map name captured by GR_LoadWorld for BSP loader */
static char     gr_worldMapName[MAX_QPATH] = {0};
static qboolean gr_worldMapLoaded   = qfalse;

/* -------------------------------------------------------------------
 * Entity capture — ring buffer for scene entities
 *
 *  GR_AddRefEntityToScene copies entity transform data into a buffer
 *  each frame.  MoHAARunner.cpp reads it via Godot_Renderer_* accessors.
 * ---------------------------------------------------------------- */

/* Compact entity data — value types only, safe engine pointers */
typedef struct {
    int     reType;         /* refEntityType_t (RT_MODEL, RT_SPRITE, etc.) */
    int     hModel;         /* qhandle_t — opaque model handle */
    int     entityNumber;   /* game entity number */
    float   origin[3];
    float   axis[3][3];
    float   scale;
    float   oldorigin[3];   /* RT_BEAM "to", or previous pos for lerp */
    int     frame;          /* RT_BEAM diameter, or current frame */
    int     oldframe;
    float   backlerp;
    byte    shaderRGBA[4];
    int     customShader;   /* qhandle_t */
    int     lightmapNum;    /* renderfx */
    int     parentEntity;
    float   radius;          /* RT_SPRITE size */
    float   rotation;        /* RT_SPRITE rotation angle (degrees) */
    float   lightingOrigin[3]; /* RF_LIGHTING_ORIGIN override point */
    float   shadowPlane;     /* shadow projection plane height (id Z) */

 /* Skeletal animation data */
    frameInfo_t frameInfo[MAX_FRAMEINFOS];
    float       actionWeight;
    int         bone_tag[5];         /* controller bone indices */
    float       bone_quat[5][4];     /* controller bone quaternions */
    void       *tiki;                /* dtiki_t pointer */

    /* Per-surface state flags (32 bytes, matches MAX_MODEL_SURFACES).
     * Bit 2 (MDL_SURFACE_NODRAW = 4) = hidden surface.
     * Bits 0-1 = per-surface skin variant offset (added to skinNum). */
    byte        surfaces[32];
    int         skinNum;             /* inline skin index for shader slot selection */
} gr_entity_t;

/* Dynamic light data */
typedef struct {
    float   origin[3];
    float   intensity;
    float   r, g, b;
    int     type;           /* dlighttype_t */
} gr_dlight_t;

#define GR_MAX_ENTITIES  1024
#define GR_MAX_DLIGHTS   64

static gr_entity_t gr_entities[GR_MAX_ENTITIES];
static int         gr_numEntities = 0;

static gr_dlight_t gr_dlights[GR_MAX_DLIGHTS];
static int         gr_numDlights  = 0;

/* -------------------------------------------------------------------
 * HUD model render requests
 *
 *  CL_Draw3DModel() calls ClearScene + AddEntity + RenderScene with
 *  RDF_HUD | RDF_NOWORLDMODEL to render a 3D model preview into a
 *  UI widget rect.  We capture each such request into a separate
 *  buffer so that world entities and camera are preserved.
 * ---------------------------------------------------------------- */

#define GR_MAX_HUD_MODELS 8

typedef struct {
    gr_entity_t entity;
    float       vieworg[3];
    float       viewaxis[3][3];
    float       fov_x, fov_y;
    float       rect_x, rect_y, rect_w, rect_h;
    int         draw_order;  /* 2D command index at time of capture */
} gr_hud_model_t;

static gr_hud_model_t gr_hudModels[GR_MAX_HUD_MODELS];
static int            gr_numHudModels = 0;
static int            gr_mainSceneRendered = 0;
static int            gr_hudEntityStart = 0;

/* -------------------------------------------------------------------
 * Poly capture — scene polys for particles, decals, effects
 *
 *  GR_AddPolyToScene stores quads/tris here each frame.
 *  MoHAARunner.cpp reads via Godot_Renderer_GetPoly* accessors.
 * ---------------------------------------------------------------- */

#define GR_MAX_POLYS       2048
#define GR_MAX_POLY_VERTS  (GR_MAX_POLYS * 4)   /* typically quads */

typedef struct {
    float   xyz[3];
    float   st[2];
    unsigned char rgba[4];
} gr_polyVert_t;

typedef struct {
    int     hShader;
    int     numVerts;
    int     firstVert;    /* index into gr_polyVerts[] */
} gr_poly_t;

static gr_poly_t     gr_polys[GR_MAX_POLYS];
static gr_polyVert_t gr_polyVerts[GR_MAX_POLY_VERTS];
static int           gr_numPolys     = 0;
static int           gr_numPolyVerts = 0;

/* -------------------------------------------------------------------
 * Shader remapping table
 * ---------------------------------------------------------------- */
#define GR_MAX_SHADER_REMAPS 128
typedef struct {
    char oldName[MAX_QPATH];
    char newName[MAX_QPATH];
    int  timeOffset;
} gr_shaderRemap_t;
static gr_shaderRemap_t gr_shaderRemaps[GR_MAX_SHADER_REMAPS];
static int              gr_numShaderRemaps = 0;

/* -------------------------------------------------------------------
 * Swipe effect capture
 * ---------------------------------------------------------------- */
#define GR_MAX_SWIPE_POINTS 64
typedef struct {
    float point1[3];
    float point2[3];
    float time;
} gr_swipePoint_t;
typedef struct {
    int         hShader;
    float       thisTime;
    float       life;
    gr_swipePoint_t points[GR_MAX_SWIPE_POINTS];
    int         numPoints;
    int         active;
} gr_swipe_t;
static gr_swipe_t gr_currentSwipe;

/* -------------------------------------------------------------------
 * Terrain mark capture
 * ---------------------------------------------------------------- */
#define GR_MAX_TERRAIN_MARKS 256
#define GR_MAX_TERRAIN_MARK_VERTS 4096
typedef struct {
    int hShader;
    int numVerts;
    int firstVert;
    int terrainIndex;
    int renderfx;
} gr_terrainMark_t;
typedef struct {
    float xyz[3];
    float st[2];
    unsigned char rgba[4];
} gr_terrainMarkVert_t;
static gr_terrainMark_t     gr_terrainMarks[GR_MAX_TERRAIN_MARKS];
static gr_terrainMarkVert_t gr_terrainMarkVerts[GR_MAX_TERRAIN_MARK_VERTS];
static int                  gr_numTerrainMarks     = 0;
static int                  gr_numTerrainMarkVerts = 0;

/* -------------------------------------------------------------------
 * Scissor state
 * ---------------------------------------------------------------- */
static int gr_scissorX      = 0;
static int gr_scissorY      = 0;
static int gr_scissorWidth  = 0;
static int gr_scissorHeight = 0;

/* -------------------------------------------------------------------
 * Shader dimension cache — declarations
 *  The full implementation is further down near GetShaderWidth/Height.
 * ---------------------------------------------------------------- */
#define GR_DIM_UNKNOWN  (-1)
static int gr_shaderWidths[GR_MAX_SHADERS];
static int gr_shaderHeights[GR_MAX_SHADERS];
static int GR_GetShaderWidth( qhandle_t hShader );
static int GR_GetShaderHeight( qhandle_t hShader );

/* -------------------------------------------------------------------
 * Background image capture
 * ---------------------------------------------------------------- */
#define GR_MAX_BG_PIXELS (1024 * 1024 * 4)
static unsigned char gr_bgData[GR_MAX_BG_PIXELS];
static int gr_bgCols   = 0;
static int gr_bgRows   = 0;
static int gr_bgBgr      = 0;
static int gr_bgActive   = 0;
static int gr_bgCmdIndex = 0;   /* 2D cmd count when GR_DrawBackground was called */

/* -------------------------------------------------------------------
 * Frame/render timing (perf)
 * ---------------------------------------------------------------- */
static int   gr_frameNumber = 0;
static int   gr_renderTime  = 0;
static float gr_2dShaderStartTime = 0.0f;

/* -------------------------------------------------------------------
 *  Renderer function stubs — lifecycle
 * ---------------------------------------------------------------- */

/* Defined in tr_init.c — bootstraps the real renderer's shader parser,
 * image system, model tables, font loader, and function tables.
 * GL calls inside R_Init are guarded with #ifndef GODOT_GDEXTENSION.
 * Sky/marks init are no-ops via -z muldefs stub priority. */
extern void R_Init( void );

/* Real renderer shutdown — defined in tr_init.c.  Cleans up shader/image/
 * model/font/terrain state so R_Init() can safely be called again. */
extern void RE_Shutdown( qboolean destroyWindow );

static int gr_realRendererInited = 0;

/* Forward declarations for 2D window state (defined in 2D drawing section). */
static float gr_2d_left;
static float gr_2d_right;
static float gr_2d_top;
static float gr_2d_bottom;
static int   gr_2d_vp_x;
static int   gr_2d_vp_y;
static int   gr_2d_vp_w;
static int   gr_2d_vp_h;

/* Accessor so Godot-side C++ code can tell whether R_Init() has been called yet */
int GR_IsRealRendererInited( void ) { return gr_realRendererInited; }

/* Forward declaration — implemented after font data declarations */
static void GR_ClearFontCaches( void );

static void GR_Shutdown( qboolean destroyWindow )
{
    ri.Printf( PRINT_ALL, "[GodotRenderer] Shutdown\n" );

    /* Under Godot we intentionally keep the real renderer subsystems alive
     * across vid_restart.  The shader text database, image system, model
     * table, and font data are all resolution-independent — tearing them
     * down and rebuilding via RE_Shutdown() + R_Init() is unnecessary and
     * harmful:
     *   - Leaks memory: old s_shaderText, shadertext_t hash entries,
     *     backEndData, and image allocations are never freed before the
     *     second R_Init() allocates replacements.
     *   - Re-parses all .shader files from VFS (hundreds of FS_ReadFile
     *     calls) — expensive and risky on web/Emscripten where memory is
     *     limited and malloc failure in Z_TagMalloc is a NULL-deref crash.
     *   - Causes crashes on the web platform specifically because the
     *     full teardown + rebuild cycle exhausts or corrupts zone memory.
     *
     * By keeping gr_realRendererInited = 1, the next GR_BeginRegistration
     * skips R_Init() entirely and just resolves the new resolution.  The
     * real renderer's R_FindShader / R_FindShaderByName / R_FindModel
     * continue to work because their data structures (tr.shaders[],
     * s_shaderText, hashTable) are still intact.
     *
     * During final library unload, Com_Shutdown() → Cmd_Shutdown() removes
     * renderer commands, and process exit reclaims all malloc'd memory.
     * No explicit RE_Shutdown() call is needed. */

    GR_ModelInit();
    next_shader_handle = 1;
    next_skin_handle   = 1;
    gr_hasNewFrame     = qfalse;
    gr_frameCount      = 0;
    gr_worldMapName[0] = '\0';
    gr_worldMapLoaded  = qfalse;
    gr_numEntities     = 0;
    gr_numDlights      = 0;
    gr_numPolys         = 0;
    gr_numPolyVerts    = 0;
    gr_numShaderRemaps = 0;
    gr_numTerrainMarks = 0;
    gr_numTerrainMarkVerts = 0;
    memset( &gr_currentSwipe, 0, sizeof( gr_currentSwipe ) );
    gr_bgActive        = 0;
    gr_frameNumber     = 0;

    /* Clear font caches so fonts are re-loaded from the current VFS search
     * path after a vid_restart or game directory change (AA→SH→BT).
     * Without this, cached font data (UV coords, height, aspect) from the
     * previous game would persist despite the new game's fonts having
     * different dimensions (e.g. base facfont-20 is 256×128 / aspect 2.0
     * while Spearhead's is 256×256 / aspect 1.0). */
    GR_ClearFontCaches();
}

static void GR_BeginRegistration( glconfig_t *config )
{
    int i;
    ri.Printf( PRINT_ALL, "[GodotRenderer] BeginRegistration\n" );

    /* Bootstrap the real renderer subsystems (shader text parser, image
     * system, model tables, function tables, fonts).  Only needed once —
     * subsequent map loads reuse the parsed shader text database.
     * R_Init() clears tr/backEnd/tess, registers cvars, allocates
     * backEndData, parses all .shader files, and loads built-in images.
     * All GL calls are guarded (#ifndef GODOT_GDEXTENSION) or stubbed. */
    if ( !gr_realRendererInited ) {
        ri.Printf( PRINT_ALL, "[GodotRenderer] Calling R_Init() to bootstrap real shader/image/model subsystems\n" );
        R_Init();
        gr_realRendererInited = 1;
    }

    /* (Re-)initialise the model table */
    GR_ModelInit();

    /* (Re-)initialise the shader table */
    memset( gr_shaders, 0, sizeof( gr_shaders ) );
    gr_numShaders = 1;  /* slot 0 = sentinel */

 /* Reset shader dimension cache */
    for ( int i = 0; i < GR_MAX_SHADERS; i++ ) {
        gr_shaderWidths[i]  = GR_DIM_UNKNOWN;
        gr_shaderHeights[i] = GR_DIM_UNKNOWN;
    }

    /* Mark world as unloaded so check_world_load() invalidates Godot-side
     * caches (BSP mesh, textures, skel models) even if the same map name
     * is reloaded.  TIKI data and shader/model handles are all reset by
     * BeginRegistration, so stale Godot-side references must be dropped. */
    gr_worldMapLoaded  = qfalse;
    gr_worldMapName[0] = '\0';
    gr_numEntities     = 0;

     /* Fonts persist across map loads; shader handles must be rebound into
         the rebuilt gr_shaders table.  Mark dirty and refresh lazily when
         text is drawn (after font tables/functions are in scope). */
     gr_fontHandlesDirty = 1;

    memset( config, 0, sizeof( *config ) );

    config->colorBits         = 32;
    config->depthBits         = 24;
    config->stencilBits       = 8;
    config->isFullscreen      = qfalse;
    config->stereoEnabled     = qfalse;
    config->displayFrequency  = 60;
    config->textureEnvAddAvailable = qtrue;
    config->maxTextureSize    = 4096;

    /* Read video cvars and resolve actual resolution for glConfig.
     * This sets config->vidWidth/vidHeight to the REAL window resolution
     * (matching OPM/ioquake3 behaviour).  The UI framework reads
     * cls.glconfig.vidWidth/vidHeight via CL_FillUIDef() and positions
     * all widgets in that coordinate space.  SCR_AdjustFrom640() scales
     * HUD elements from 640×480 virtual space to this real resolution. */
    {
        cvar_t *r_mode_cv       = ri.Cvar_Get( "r_mode",         "-2", 0 );
        cvar_t *r_fullscreen_cv = ri.Cvar_Get( "r_fullscreen",   "1",    0 );
        cvar_t *r_customw_cv    = ri.Cvar_Get( "r_customwidth",  "1600",  0 );
        cvar_t *r_customh_cv    = ri.Cvar_Get( "r_customheight", "1024",  0 );

        /* Register r_gamma so the config value is loaded and accessible
         * from the Godot side for full-screen gamma correction. */
        ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE );

        /* Register r_shadows:
         *  0 = classic MOHAA shadow blobs (default)
         *  1 = modern Godot GPU shadows: sun DirectionalLight (from map sundirection)
         *      casts shadows for all RF_SHADOW entities onto PER_PIXEL BSP surfaces.
         *      Use r_dlight_shadows to also cast shadows from muzzle/explosion lights. */
        ri.Cvar_Get( "r_shadows", "0", CVAR_ARCHIVE );

        /* Register r_dlight_shadows:
         *  0 = dynamic point lights illuminate but do NOT cast shadows (default, fast)
         *  1 = dynamic point lights cast shadows (expensive: 6 depth passes per light).
         *      Requires r_shadows 1. */
        ri.Cvar_Get( "r_dlight_shadows", "0", CVAR_ARCHIVE );

        /* Register r_godot_rain:
         *  0 = use original MOHAA rain (line streaks from cgame)
         *  1 = use Godot physics rain with collision (default) */
        ri.Cvar_Get( "r_godot_rain", "1", CVAR_ARCHIVE );

        int mode = r_mode_cv ? r_mode_cv->integer : -2;
        int fs   = r_fullscreen_cv ? r_fullscreen_cv->integer : 0;
        int w = 640, h = 480;  /* fallback */

        if ( mode >= 0 && mode < gr_numVidModes ) {
            w = gr_vidModes[mode].width;
            h = gr_vidModes[mode].height;
        } else if ( mode == -1 ) {
            /* Custom mode */
            w = r_customw_cv ? r_customw_cv->integer : 1600;
            h = r_customh_cv ? r_customh_cv->integer : 1024;
        } else if ( mode == -2 ) {
            /* Desktop resolution — use value set by MoHAARunner */
            w = gr_desktopWidth;
            h = gr_desktopHeight;
        }

        /* Widescreen support: when fullscreen, adjust the width to match
         * the physical screen's aspect ratio.  This preserves the user's
         * vertical resolution (render quality) while widening the view.
         * The cgame's CG_CalcFov() hor+ calculation in cg_view.c uses
         * refdef.width/height (derived from glConfig.vidWidth/Height) to
         * compute a wider fov_x for widescreen displays. */
        if ( fs && gr_screenWidth > 0 && gr_screenHeight > 0 ) {
            float screen_aspect = (float)gr_screenWidth / (float)gr_screenHeight;
            float engine_aspect = (float)w / (float)h;
            if ( screen_aspect - engine_aspect > 0.01f ||
                 engine_aspect - screen_aspect > 0.01f ) {
                w = (int)((float)h * screen_aspect + 0.5f);
                w &= ~1;  /* ensure even */
                ri.Printf( PRINT_ALL,
                    "[GodotRenderer] Widescreen adjust: %dx%d (screen %.0fx%.0f aspect %.3f)\n",
                    w, h, (float)gr_screenWidth, (float)gr_screenHeight, screen_aspect );
            }
        }

        /* Set glConfig to the (possibly widescreen-adjusted) resolution.
         * The engine UI framework, SCR_AdjustFrom640(), and all draw code
         * use these values for coordinate scaling and widget layout. */
        config->vidWidth     = w;
        config->vidHeight    = h;
        config->windowAspect = (float)w / (float)h;
        config->isFullscreen = (qboolean)( fs != 0 );

        gr_vidRestart_fullscreen = fs;
        gr_vidRestart_width      = w;
        gr_vidRestart_height     = h;
        gr_vidRestarted          = 1;

        ri.Printf( PRINT_ALL, "[GodotRenderer] BeginRegistration: r_mode=%d fs=%d => %dx%d\n",
                   mode, fs, w, h );
    }

    /* Copy for later queries */
    stored_glconfig = *config;

    /* Initialise viewport tracking to the engine resolution.
     * MoHAARunner will overwrite with the real Godot viewport size
     * on the first frame. */
    if ( gr_viewport_width == 0 || gr_viewport_height == 0 ) {
        gr_viewport_width  = config->vidWidth;
        gr_viewport_height = config->vidHeight;
    }
}

static void GR_EndRegistration( void )
{
    ri.Printf( PRINT_DEVELOPER, "[GodotRenderer] EndRegistration\n" );
}

/* -------------------------------------------------------------------
 *  Asset registration — return sequential dummy handles
 * ---------------------------------------------------------------- */

static qhandle_t GR_RegisterModel( const char *name )
{
    qhandle_t handle = GR_RegisterModelInternal( name, qtrue, qtrue );
    if ( handle ) {
        gr_models[handle].serveronly = qfalse;
    }
    return handle;
}

static qhandle_t GR_RegisterSkin( const char *name )
{
    return next_skin_handle++;
}

static qhandle_t GR_RegisterShader( const char *name )
{
    int i;
    if ( !name || !name[0] ) return 0;

    /* Deduplicate */
    for ( i = 1; i < gr_numShaders; i++ ) {
        if ( !Q_stricmp( gr_shaders[i].name, name ) ) {
            return i;
        }
    }

    if ( gr_numShaders >= GR_MAX_SHADERS ) return 0;

    i = gr_numShaders++;
    Q_strncpyz( gr_shaders[i].name, name, sizeof( gr_shaders[i].name ) );
    gr_shaders[i].nomip = 0;
    ri.Printf( PRINT_DEVELOPER, "[GodotRenderer] RegisterShader: %s → %d\n", name, i );
    return i;
}

static qhandle_t GR_RegisterShaderNoMip( const char *name )
{
    int i;
    if ( !name || !name[0] ) return 0;

    /* Deduplicate */
    for ( i = 1; i < gr_numShaders; i++ ) {
        if ( !Q_stricmp( gr_shaders[i].name, name ) ) {
            return i;
        }
    }

    if ( gr_numShaders >= GR_MAX_SHADERS ) return 0;

    /* Match OpenMOHAA RE_RegisterShaderNoMip: if the image doesn't exist
     * and there's no .shader definition, return 0 (defaultShader).
     * This is critical for UIBindButton which checks GetMaterial() to
     * decide whether to show a texture image or fall back to text.
     *
     * Use Godot_ShaderProps_Find_2D() (LIGHTMAP_2D) to match the real
     * RE_RegisterShaderNoMip which calls R_FindShader(name, LIGHTMAP_2D).
     * This ensures the shader_t gets correct 2D stage properties
     * (CGEN_GLOBAL_COLOR, AGEN_GLOBAL_ALPHA, SRC_ALPHA blend). */
    if ( !Godot_ShaderProps_Find_2D( name ) ) {
        return 0;
    }

    i = gr_numShaders++;
    Q_strncpyz( gr_shaders[i].name, name, sizeof( gr_shaders[i].name ) );
    gr_shaders[i].nomip = 1;
    return i;
}

static qhandle_t GR_RefreshShaderNoMip( const char *name )
{
    /* Re-registration: find existing or create new */
    return GR_RegisterShaderNoMip( name );
}

static qhandle_t GR_SpawnEffectModel( const char *name, vec3_t pos, vec3_t axis[3] )
{
    qhandle_t handle = GR_RegisterModelInternal( name, qfalse, qtrue );
    if ( handle && gr_models[handle].type == GR_MOD_TIKI && gr_models[handle].tiki ) {
        gr_models[handle].serveronly = qfalse;
        /* Process init commands with spawn position */
        if ( ri.CG_ProcessInitCommands ) {
            refEntity_t ent;
            memset( &ent, 0, sizeof( ent ) );
            memset( ent.shaderRGBA, 255, sizeof( ent.shaderRGBA ) );
            VectorCopy( pos, ent.origin );
            ent.scale = 1.0f;
            if ( axis ) {
                AxisCopy( axis, ent.axis );
            }
            ent.hModel = handle;
            ri.CG_ProcessInitCommands( gr_models[handle].tiki, &ent );
        }
    }
    return handle;
}

static qhandle_t GR_RegisterServerModel( const char *name )
{
    return GR_RegisterModelInternal( name, qtrue, qfalse );
}

static void GR_UnregisterServerModel( qhandle_t model )
{
    if ( model >= 1 && model < gr_numModels ) {
        gr_models[model].type = GR_MOD_BAD;
        gr_models[model].tiki = NULL;
    }
}

/* -------------------------------------------------------------------
 *  World loading
 * ---------------------------------------------------------------- */

static void GR_LoadWorld( const char *name )
{
    ri.Printf( PRINT_ALL, "[GodotRenderer] LoadWorld: %s\n", name );

    /* Store the map path for the BSP loader to pick up */
    if ( name ) {
        strncpy( gr_worldMapName, name, sizeof( gr_worldMapName ) - 1 );
        gr_worldMapName[sizeof( gr_worldMapName ) - 1] = '\0';
    } else {
        gr_worldMapName[0] = '\0';
    }
    gr_worldMapLoaded = ( name && name[0] ) ? qtrue : qfalse;

    /* Load the BSP into the real renderer's tr.world so that
     * R_MarkFragments (decals/bullet holes) can walk the BSP tree.
     * All GL calls in the load path are stubbed via tr_godot_gl_stubs.c.
     * The Godot-side BSP mesh builder (godot_bsp_mesh.cpp) separately
     * creates Godot MeshInstance3D nodes — this call populates the
     * internal renderer data structures only. */
    if ( name && name[0] ) {
        extern void RE_LoadWorldMap( const char *name );
        RE_LoadWorldMap( name );
    }
}

static void GR_SetWorldVisData( const byte *vis )
{
    (void)vis;
}

/* -------------------------------------------------------------------
 *  Frame management
 * ---------------------------------------------------------------- */

static void GR_BeginFrame( stereoFrame_t stereoFrame )
{
    (void)stereoFrame;
    /* Reset 2D transform state to default full-screen window each frame.
     * Loading/connect draws can run before UI code sets a custom 2D window,
     * so stale state from a previous frame must not leak forward. */
    {
        int vw = stored_glconfig.vidWidth  > 0 ? stored_glconfig.vidWidth  : 640;
        int vh = stored_glconfig.vidHeight > 0 ? stored_glconfig.vidHeight : 480;
        gr_2d_vp_x   = 0;
        gr_2d_vp_y   = 0;
        gr_2d_vp_w   = vw;
        gr_2d_vp_h   = vh;
        gr_2d_left   = 0.0f;
        gr_2d_right  = (float)vw;
        gr_2d_top    = 0.0f;
        gr_2d_bottom = (float)vh;

        gr_scissorX      = 0;
        gr_scissorY      = 0;
        gr_scissorWidth  = vw;
        gr_scissorHeight = vh;
    }

    /* Reset 2D command buffer for this frame */
    gr_num2DCmds = 0;
    /* Reset HUD model state so the first ClearScene does a full clear */
    gr_mainSceneRendered = 0;
    gr_numHudModels = 0;
    /* Reset HUD entity start so menu-only frames (no world scene)
     * don't carry a stale offset from a previous world render.
     * Without this, GR_RenderScene's gr_numEntities > gr_hudEntityStart
     * check would fail when transitioning from in-game to a menu. */
    gr_hudEntityStart = 0;
}

static void GR_EndFrame( int *frontEndMsec, int *backEndMsec )
{
    if ( frontEndMsec ) *frontEndMsec = 0;
    if ( backEndMsec )  *backEndMsec  = 0;

    gr_endframe_count++;

    /* If this is the 2nd+ EndFrame within a single Com_Frame() call,
     * we are inside a loading loop (SCR_UpdateScreen called repeatedly
     * by UI_LoadResource / UI_TestUpdateScreen).  Invoke the callback
     * so Godot actually presents the loading screen content. */
    if ( gr_endframe_count > 1 && gr_force_render_callback ) {
        gr_force_render_callback();
    }
}

/* -------------------------------------------------------------------
 *  Scene building
 * ---------------------------------------------------------------- */

static void GR_ClearScene( void )
{
    if ( gr_mainSceneRendered ) {
 /* World scene already rendered this frame.
         * This ClearScene is for a HUD model sub-render (CL_Draw3DModel).
         * Don't clear world entities — note where HUD entities start. */
        gr_hudEntityStart = gr_numEntities;
        return;
    }

    gr_numEntities  = 0;
    gr_numDlights   = 0;
    gr_numPolys     = 0;
    gr_numPolyVerts = 0;
    gr_numTerrainMarks     = 0;
    gr_numTerrainMarkVerts = 0;
    gr_bgActive     = 0;
    gr_bgCmdIndex   = 0;
    gr_mainSceneRendered = 0;

    /* Advance per-frame skeleton pose index so GR_UpdatePoseInternal
     * re-poses each entity's skeleton exactly once per frame. */
    gr_frame_skel_index++;
}

static void GR_AddRefEntityToScene( const refEntity_t *re, int parentEntityNumber )
{
    if ( !re || gr_numEntities >= GR_MAX_ENTITIES ) return;

    gr_entity_t *ge = &gr_entities[gr_numEntities++];

    ge->reType        = (int)re->reType;
    ge->hModel        = (int)re->hModel;
    ge->entityNumber  = re->entityNumber;
    ge->scale         = re->scale;
    ge->frame         = re->frame;
    ge->oldframe      = re->oldframe;
    ge->backlerp      = re->backlerp;
    ge->customShader  = (int)re->customShader;
    ge->lightmapNum   = re->renderfx;
    ge->parentEntity  = parentEntityNumber;
    ge->radius        = re->radius;
    ge->rotation      = re->rotation;
    ge->shadowPlane   = re->shadowPlane;

    VectorCopy( re->lightingOrigin, ge->lightingOrigin );

    VectorCopy( re->origin, ge->origin );
    VectorCopy( re->oldorigin, ge->oldorigin );
    VectorCopy( re->axis[0], ge->axis[0] );
    VectorCopy( re->axis[1], ge->axis[1] );
    VectorCopy( re->axis[2], ge->axis[2] );

    ge->shaderRGBA[0] = re->shaderRGBA[0];
    ge->shaderRGBA[1] = re->shaderRGBA[1];
    ge->shaderRGBA[2] = re->shaderRGBA[2];
    ge->shaderRGBA[3] = re->shaderRGBA[3];

    /* Per-surface state flags and skin selector */
    ge->skinNum = re->skinNum;
    memcpy( ge->surfaces, re->surfaces, sizeof( ge->surfaces ) );

 /* Skeletal animation data */
    memcpy( ge->frameInfo, re->frameInfo, sizeof(ge->frameInfo) );
    ge->actionWeight = re->actionWeight;
    ge->tiki         = (void *)re->tiki;

    /* Always resolve ge->tiki to the real dtiki_t* from our model table.
     * cgame stores a qhandle_t integer in re->tiki (e.g. cgs.model_draw[idx])
     * which is a small integer like 3 cast to dtiki_t*.  That bogus pointer
     * would crash Godot_RI_GetSkeletor / Godot_Skel_PrepareBones.
     * The correct TIKI pointer is always in gr_models[hModel].tiki. */
    if ( ge->hModel > 0 && ge->hModel < gr_numModels &&
         gr_models[ge->hModel].type == GR_MOD_TIKI ) {
        ge->tiki = (void *)gr_models[ge->hModel].tiki;
    } else if ( !ge->tiki ) {
        /* Non-TIKI model with no tiki set — leave as NULL (sprites, etc.) */
    }

    if ( re->bone_tag && re->bone_quat ) {
        for ( int i = 0; i < 5; i++ ) {
            ge->bone_tag[i]     = re->bone_tag[i];
            ge->bone_quat[i][0] = re->bone_quat[i][0];
            ge->bone_quat[i][1] = re->bone_quat[i][1];
            ge->bone_quat[i][2] = re->bone_quat[i][2];
            ge->bone_quat[i][3] = re->bone_quat[i][3];
        }
    } else {
        memset( ge->bone_tag, -1, sizeof(ge->bone_tag) );
        memset( ge->bone_quat, 0, sizeof(ge->bone_quat) );
    }

}


static qboolean GR_AddPolyToScene( qhandle_t hShader, int numVerts,
                                   const polyVert_t *verts, int renderfx )
{
    /* The 4th parameter is renderfx (render flags), NOT a poly count.
     * The cgame always submits one polygon per call with renderfx
     * for that polygon (often 0).  Add exactly one poly. */
    if ( gr_numPolys >= GR_MAX_POLYS ) return qfalse;
    if ( gr_numPolyVerts + numVerts > GR_MAX_POLY_VERTS ) return qfalse;

    gr_poly_t *p = &gr_polys[gr_numPolys++];
    p->hShader   = (int)hShader;
    p->numVerts  = numVerts;
    p->firstVert = gr_numPolyVerts;

    int v;
    for ( v = 0; v < numVerts; v++ ) {
        gr_polyVert_t *gv = &gr_polyVerts[gr_numPolyVerts++];
        VectorCopy( verts[v].xyz, gv->xyz );
        gv->st[0] = verts[v].st[0];
        gv->st[1] = verts[v].st[1];
        gv->rgba[0] = verts[v].modulate[0];
        gv->rgba[1] = verts[v].modulate[1];
        gv->rgba[2] = verts[v].modulate[2];
        gv->rgba[3] = verts[v].modulate[3];
    }

    return qtrue;
}

static int GR_LightForPoint( vec3_t point, vec3_t ambientLight,
                             vec3_t directedLight, vec3_t lightDir )
{
    if ( ambientLight )   VectorSet( ambientLight, 0.5f, 0.5f, 0.5f );
    if ( directedLight )  VectorSet( directedLight, 0.5f, 0.5f, 0.5f );
    if ( lightDir )       VectorSet( lightDir, 0.0f, 0.0f, 1.0f );
    return qtrue;
}

static void GR_AddLightToScene( const vec3_t org, float intensity,
                                float r, float g, float b, int type )
{
    if ( gr_numDlights >= GR_MAX_DLIGHTS ) return;

    gr_dlight_t *dl = &gr_dlights[gr_numDlights++];
    VectorCopy( org, dl->origin );
    dl->intensity = intensity;
    dl->r = r;
    dl->g = g;
    dl->b = b;
    dl->type = type;
}

static void GR_AddAdditiveLightToScene( const vec3_t org, float intensity,
                                        float r, float g, float b )
{
 /* Treat additive lights the same as regular lights */
    if ( gr_numDlights >= GR_MAX_DLIGHTS ) return;
    gr_dlight_t *dl = &gr_dlights[gr_numDlights++];
    VectorCopy( org, dl->origin );
    dl->intensity = intensity;
    dl->r = r;
    dl->g = g;
    dl->b = b;
    dl->type = 1;  /* additive type marker */
}

static void GR_RenderScene( const refdef_t *fd )
{
    if ( !fd ) return;

 /* HUD model render (CL_Draw3DModel) — capture into
     * separate buffer, don't overwrite world camera/entities. */
    if ( fd->rdflags & 0x0001 /* RDF_NOWORLDMODEL */ ) {
        if ( gr_numHudModels < GR_MAX_HUD_MODELS ) {
            gr_hud_model_t *hm = &gr_hudModels[gr_numHudModels];

            /* Copy entity data from the temporary slot(s) added after
             * the world scene's entity list. */
            if ( gr_numEntities > gr_hudEntityStart ) {
                memcpy( &hm->entity, &gr_entities[gr_hudEntityStart],
                        sizeof(gr_entity_t) );
            } else {
                memset( &hm->entity, 0, sizeof(gr_entity_t) );
            }

            /* Copy viewport parameters */
            VectorCopy( fd->vieworg, hm->vieworg );
            VectorCopy( fd->viewaxis[0], hm->viewaxis[0] );
            VectorCopy( fd->viewaxis[1], hm->viewaxis[1] );
            VectorCopy( fd->viewaxis[2], hm->viewaxis[2] );
            hm->fov_x  = fd->fov_x;
            hm->fov_y  = fd->fov_y;
            hm->rect_x = (float)fd->x;
            hm->rect_y = (float)fd->y;
            hm->rect_w = (float)fd->width;
            hm->rect_h = (float)fd->height;
            hm->draw_order = gr_num2DCmds;  /* record position in 2D stream */

            gr_numHudModels++;
        }

        /* Remove HUD entities from world entity buffer */
        gr_numEntities = gr_hudEntityStart;
        return;
    }

    /* Normal world render — capture viewpoint for Godot Camera3D bridge */
    gr_mainSceneRendered = 1;

    VectorCopy( fd->vieworg, gr_viewOrigin );
    VectorCopy( fd->viewaxis[0], gr_viewAxis[0] );
    VectorCopy( fd->viewaxis[1], gr_viewAxis[1] );
    VectorCopy( fd->viewaxis[2], gr_viewAxis[2] );

    gr_fov_x = fd->fov_x;
    gr_fov_y = fd->fov_y;
    gr_renderWidth  = fd->width;
    gr_renderHeight = fd->height;

    gr_farplane_distance = fd->farplane_distance;
    gr_farplane_bias = fd->farplane_bias;
    VectorCopy( fd->farplane_color, gr_farplane_color );
    gr_farplane_cull = fd->farplane_cull;
    gr_skybox_farplane = fd->skybox_farplane;
    gr_renderTerrain = fd->renderTerrain;

    /*
     * Replicate the farclipOverride logic from RE_RenderScene()
     * (tr_scene.c L599–645).  This post-processes the raw refdef
     * values into the final fog parameters that the real renderer
     * would use.
     */
    {
        int farclip = 0;

        if (fd->farclipOverride >= 15900.0f || fd->farclipOverride <= -0.99f) {
            farclip = 0;
        } else {
            cvar_t *r_farclip_cv  = ri.Cvar_Get("r_farclip", "0", 0);
            cvar_t *r_picmip_cv   = ri.Cvar_Get("r_picmip", "0", 0);
            cvar_t *r_colorbits_cv = ri.Cvar_Get("r_colorbits", "0", 0);
            farclip = r_farclip_cv->integer;
            if (!farclip && (r_picmip_cv->integer > 1 || r_colorbits_cv->integer == 16)) {
                farclip = 2800;
            }
        }

        if (farclip) {
            if (fd->farclipOverride != 0.0f) {
                gr_farplane_distance = fd->farclipOverride;
            } else {
                gr_farplane_distance = (float)farclip;
            }

            if (fd->farplane_color[0] >= 0.0f && fd->farplane_color[1] >= 0.0f && fd->farplane_color[2] >= 0.0f) {
                VectorCopy(fd->farplane_color, gr_farplane_color);
            }

            if (fd->farplaneColorOverride[0] >= 0.0f && fd->farplaneColorOverride[1] >= 0.0f && fd->farplaneColorOverride[2] >= 0.0f) {
                VectorCopy(fd->farplaneColorOverride, gr_farplane_color);
            }

            gr_farplane_cull = qtrue;

            if (fd->farplane_distance > 0.0f && fd->farplane_distance < gr_farplane_distance) {
                gr_farplane_distance = fd->farplane_distance;
            } else {
                if (fd->farplane_bias == 0.0f) {
                    gr_farplane_bias = gr_farplane_distance * 0.18f;
                } else if (fd->farplane_distance <= 500.0f) {
                    gr_farplane_bias = gr_farplane_distance * 0.18f;
                } else {
                    gr_farplane_bias = gr_farplane_distance / fd->farplane_distance;
                }
            }
        } else if (gr_farplane_bias == 0.0f) {
            /* Default bias auto-calculation when no farclip override */
            gr_farplane_bias = gr_farplane_distance * 0.18f;
        }
    }

    /* Set up the real renderer's backend view params so that
     * Godot_ComputeSpriteQuad() (and future engine pipeline
     * capture functions) produce correct camera-facing geometry. */
    {
        extern void Godot_Backend_SetupView(const float vieworg[3],
            const float viewaxis[3][3]);
        Godot_Backend_SetupView( fd->vieworg, fd->viewaxis );
    }

    gr_hasNewFrame = qtrue;
    gr_frameCount++;
}

static void GR_AddRefSpriteToScene( const refEntity_t *ent )
{
 /* Capture sprites the same way as entities — they have
       a shader handle and transform.  MoHAARunner renders them as
       camera-facing billboard quads. */
    if ( !ent ) return;
    GR_AddRefEntityToScene( ent, ENTITYNUM_NONE );
}

static void GR_AddTerrainMarkToScene( int terrainIndex, qhandle_t hShader,
                                      int numVerts, const polyVert_t *verts,
                                      int renderfx )
{
    if ( gr_numTerrainMarks >= GR_MAX_TERRAIN_MARKS ) return;
    if ( gr_numTerrainMarkVerts + numVerts > GR_MAX_TERRAIN_MARK_VERTS ) return;

    gr_terrainMark_t *tm = &gr_terrainMarks[gr_numTerrainMarks++];
    tm->hShader      = (int)hShader;
    tm->numVerts     = numVerts;
    tm->firstVert    = gr_numTerrainMarkVerts;
    tm->terrainIndex = terrainIndex;
    tm->renderfx     = renderfx;

    for ( int i = 0; i < numVerts; i++ ) {
        gr_terrainMarkVert_t *v = &gr_terrainMarkVerts[gr_numTerrainMarkVerts++];
        v->xyz[0] = verts[i].xyz[0];
        v->xyz[1] = verts[i].xyz[1];
        v->xyz[2] = verts[i].xyz[2];
        v->st[0]  = verts[i].st[0];
        v->st[1]  = verts[i].st[1];
        v->rgba[0] = verts[i].modulate[0];
        v->rgba[1] = verts[i].modulate[1];
        v->rgba[2] = verts[i].modulate[2];
        v->rgba[3] = verts[i].modulate[3];
    }
}

/* -------------------------------------------------------------------
 *  2D drawing
 * ---------------------------------------------------------------- */

static void GR_SetColor( const float *rgba )
{
    if ( rgba ) {
        current_color[0] = rgba[0];
        current_color[1] = rgba[1];
        current_color[2] = rgba[2];
        current_color[3] = rgba[3];
    } else {
        current_color[0] = current_color[1] = current_color[2] = current_color[3] = 1.0f;
    }
}

/* Track the 2D window state so MoHAARunner can map engine coordinates
 * to the Godot viewport correctly.  The engine calls this before drawing
 * UI widgets to set up a custom orthographic projection. */
static float gr_2d_left   = 0.0f;
static float gr_2d_right  = 640.0f;
static float gr_2d_top    = 0.0f;
static float gr_2d_bottom = 480.0f;
static int   gr_2d_vp_x   = 0;
static int   gr_2d_vp_y   = 0;
static int   gr_2d_vp_w   = 640;
static int   gr_2d_vp_h   = 480;

/* Transform draw coordinates from widget ortho space (Set2DWindow) to
 * absolute screen space (640x480).  The engine's GL renderer does this
 * automatically via glViewport + glOrtho; we must replicate it so the
 * Godot-side 2D overlay gets absolute positions. */
static void GR_Transform2D( float in_x, float in_y, float in_w, float in_h,
                            float *out_x, float *out_y, float *out_w, float *out_h )
{
    float range_x = gr_2d_right  - gr_2d_left;
    float range_y = gr_2d_bottom - gr_2d_top;

    if ( range_x <= 0.0f ) range_x = (float)SCREEN_WIDTH;
    if ( range_y <= 0.0f ) range_y = (float)SCREEN_HEIGHT;

    /* Map from ortho space to viewport pixel space */
    float sx = (float)gr_2d_vp_w / range_x;
    float sy = (float)gr_2d_vp_h / range_y;

    *out_x = (float)gr_2d_vp_x + ( in_x - gr_2d_left ) * sx;
    /* GL viewport Y is bottom-up; convert to top-down by flipping.
     * The GL ortho has top at the top and bottom at the bottom (MOHAA
     * passes clippedorigin.y+h as bottom, clippedorigin.y as top), so
     * the Y mapping is: screen_y = vp_y + (draw_y - top) * vp_h / (bottom - top).
     * But vp_y is GL bottom-up; convert to top-down:
     *   topdown_y = vidHeight - (vp_y + vp_h) + (draw_y - top) * sy */
    *out_y = (float)( stored_glconfig.vidHeight - gr_2d_vp_y - gr_2d_vp_h )
             + ( in_y - gr_2d_top ) * sy;
    *out_w = in_w * sx;
    *out_h = in_h * sy;
}

static void GR_DrawStretchPic( float x, float y, float w, float h,
                               float s1, float t1, float s2, float t2,
                               qhandle_t hShader )
{
    if ( gr_num2DCmds >= GR_MAX_2D_CMDS ) return;
    gr_2d_cmd_t *cmd = &gr_2d_cmds[gr_num2DCmds++];
    cmd->type   = GR_2D_STRETCHPIC;
    GR_Transform2D( x, y, w, h, &cmd->x, &cmd->y, &cmd->w, &cmd->h );
    cmd->s1 = s1; cmd->t1 = t1; cmd->s2 = s2; cmd->t2 = t2;
    cmd->shader = (int)hShader;
    cmd->color[0] = current_color[0];
    cmd->color[1] = current_color[1];
    cmd->color[2] = current_color[2];
    cmd->color[3] = current_color[3];
}

static void GR_DrawStretchPic2( float x, float y, float w, float h,
                                float s1, float t1, float s2, float t2,
                                float sx, float sy, qhandle_t hShader )
{
    /* Treat sx/sy as additional scale — fall through to normal stretch pic */
    GR_DrawStretchPic( x, y, w * sx, h * sy, s1, t1, s2, t2, hShader );
}

static void GR_DrawStretchRaw( int x, int y, int w, int h,
                               int cols, int rows, int components,
                               const byte *data )
{
    /* RoQ decoder sends 32-bit RGBA data (components=0 from cinematic path,
     * actual format is always 4 bytes/pixel set by initRoQ). */
    if ( !data || cols <= 0 || rows <= 0 ) return;
    if ( cols > GR_CIN_MAX_WIDTH || rows > GR_CIN_MAX_HEIGHT ) return;

    memcpy( gr_cin_buffer, data, cols * rows * 4 );
    gr_cin_width  = cols;
    gr_cin_height = rows;
    gr_cin_dirty  = 1;
    gr_cin_active = 1;
}

static void GR_UploadCinematic( int w, int h, int cols, int rows,
                                const byte *data, int client, qboolean dirty )
{
    /* Shader-embedded cinematics (e.g. animated textures) use this path.
     * For now, treat same as DrawStretchRaw. */
    if ( !data || cols <= 0 || rows <= 0 ) return;
    if ( cols > GR_CIN_MAX_WIDTH || rows > GR_CIN_MAX_HEIGHT ) return;

    memcpy( gr_cin_buffer, data, cols * rows * 4 );
    gr_cin_width  = cols;
    gr_cin_height = rows;
    gr_cin_dirty  = 1;
    gr_cin_active = 1;
}

static void GR_DrawTilePic( float x, float y, float w, float h,
                            qhandle_t hShader )
{
 /* Tile UV based on real texture dimensions */
    int tw = GR_GetShaderWidth( hShader );
    int th = GR_GetShaderHeight( hShader );
    float s1 = (tw > 0) ? w / (float)tw : 1.0f;
    float t1 = (th > 0) ? h / (float)th : 1.0f;
    GR_DrawStretchPic( x, y, w, h, 0.0f, 0.0f, s1, t1, hShader );
}

static void GR_DrawTilePicOffset( float x, float y, float w, float h,
                                  qhandle_t hShader, int offsetX, int offsetY )
{
 /* Tile UV with offset */
    int tw = GR_GetShaderWidth( hShader );
    int th = GR_GetShaderHeight( hShader );
    float s0 = (tw > 0) ? (float)offsetX / (float)tw : 0.0f;
    float t0 = (th > 0) ? (float)offsetY / (float)th : 0.0f;
    float s1 = (tw > 0) ? s0 + w / (float)tw : 1.0f;
    float t1 = (th > 0) ? t0 + h / (float)th : 1.0f;
    GR_DrawStretchPic( x, y, w, h, s0, t0, s1, t1, hShader );
}

/* Transform a single 2D point from widget ortho space to absolute screen space.
 * Same maths as GR_Transform2D but for individual vertices. */
static void GR_TransformPoint2D( float in_x, float in_y,
                                 float *out_x, float *out_y )
{
    float range_x = gr_2d_right  - gr_2d_left;
    float range_y = gr_2d_bottom - gr_2d_top;

    if ( range_x <= 0.0f ) range_x = (float)SCREEN_WIDTH;
    if ( range_y <= 0.0f ) range_y = (float)SCREEN_HEIGHT;

    float sx = (float)gr_2d_vp_w / range_x;
    float sy = (float)gr_2d_vp_h / range_y;

    *out_x = (float)gr_2d_vp_x + ( in_x - gr_2d_left ) * sx;
    *out_y = (float)( stored_glconfig.vidHeight - gr_2d_vp_y - gr_2d_vp_h )
             + ( in_y - gr_2d_top ) * sy;
}

static void GR_DrawTrianglePic( const vec2_t vPoints[3],
                                const vec2_t vTexCoords[3],
                                qhandle_t hShader )
{
    if ( !vPoints ) return;
    if ( gr_num2DCmds >= GR_MAX_2D_CMDS ) return;

    gr_2d_cmd_t *cmd = &gr_2d_cmds[gr_num2DCmds++];
    cmd->type   = GR_2D_TRIANGLE;
    cmd->shader = (int)hShader;
    cmd->color[0] = current_color[0];
    cmd->color[1] = current_color[1];
    cmd->color[2] = current_color[2];
    cmd->color[3] = current_color[3];

    /* Transform each vertex from widget ortho space to screen space */
    for ( int i = 0; i < 3; i++ ) {
        GR_TransformPoint2D( vPoints[i][0], vPoints[i][1],
                             &cmd->tri_verts[i][0], &cmd->tri_verts[i][1] );
        if ( vTexCoords ) {
            cmd->tri_uvs[i][0] = vTexCoords[i][0];
            cmd->tri_uvs[i][1] = vTexCoords[i][1];
        } else {
            cmd->tri_uvs[i][0] = 0.0f;
            cmd->tri_uvs[i][1] = 0.0f;
        }
    }

    /* Fill rect fields with bounding box (for scissor/diagnostics) */
    cmd->x = cmd->tri_verts[0][0];
    cmd->y = cmd->tri_verts[0][1];
    float maxX = cmd->x, maxY = cmd->y;
    for ( int i = 1; i < 3; i++ ) {
        if ( cmd->tri_verts[i][0] < cmd->x ) cmd->x = cmd->tri_verts[i][0];
        if ( cmd->tri_verts[i][1] < cmd->y ) cmd->y = cmd->tri_verts[i][1];
        if ( cmd->tri_verts[i][0] > maxX ) maxX = cmd->tri_verts[i][0];
        if ( cmd->tri_verts[i][1] > maxY ) maxY = cmd->tri_verts[i][1];
    }
    cmd->w = maxX - cmd->x;
    cmd->h = maxY - cmd->y;
    cmd->s1 = cmd->t1 = 0.0f;
    cmd->s2 = cmd->t2 = 1.0f;
}

static void GR_DrawBackground( int cols, int rows, int bgr, uint8_t *data )
{
    if ( !data || cols <= 0 || rows <= 0 ) {
        gr_bgActive = 0;
        return;
    }
    int totalBytes = cols * rows * 3;  /* RGB data */
    if ( totalBytes > GR_MAX_BG_PIXELS ) {
        gr_bgActive = 0;
        return;
    }
    memcpy( gr_bgData, data, totalBytes );
    gr_bgCols     = cols;
    gr_bgRows     = rows;
    gr_bgBgr      = bgr;
    gr_bgActive   = 1;
    gr_bgCmdIndex = gr_num2DCmds;  /* position in 2D stream where bg should render */
}

static void GR_DrawBox( float x, float y, float w, float h )
{
    if ( gr_num2DCmds >= GR_MAX_2D_CMDS ) return;
    gr_2d_cmd_t *cmd = &gr_2d_cmds[gr_num2DCmds++];
    cmd->type   = GR_2D_BOX;
    GR_Transform2D( x, y, w, h, &cmd->x, &cmd->y, &cmd->w, &cmd->h );
    cmd->s1 = cmd->t1 = 0.0f;
    cmd->s2 = cmd->t2 = 1.0f;
    cmd->shader = 0;
    cmd->color[0] = current_color[0];
    cmd->color[1] = current_color[1];
    cmd->color[2] = current_color[2];
    cmd->color[3] = current_color[3];
}

static void GR_AddBox( float x, float y, float w, float h )
{
 /* Treat AddBox same as DrawBox — accumulated box drawing */
    GR_DrawBox( x, y, w, h );
}

static void GR_Scissor( int x, int y, int width, int height )
{
    gr_scissorX      = x;
    gr_scissorY      = y;
    gr_scissorWidth  = width;
    gr_scissorHeight = height;

 /* Emit a scissor-change command into the 2D stream so
       MoHAARunner can clip subsequent draws to this region.

       Note: The x,y from the engine are in GL pixel space (Y=0 at bottom).
       Convert to top-down screen space for the Godot-side overlay. */
    if ( gr_num2DCmds < GR_MAX_2D_CMDS ) {
        gr_2d_cmd_t *cmd = &gr_2d_cmds[gr_num2DCmds++];
        cmd->type = GR_2D_SCISSOR;
        cmd->x = (float)x;
        cmd->y = (float)( stored_glconfig.vidHeight - y - height );
        cmd->w = (float)width;
        cmd->h = (float)height;
        cmd->shader = 0;
    }
}

static void GR_DrawLineLoop( const vec2_t *points, int count,
                             int stippleFactor, int stippleMask )
{
 /* Approximate line loop with box commands for the segments.
     * Each segment is drawn as a thin 1px rectangle.  This is used
     * sparingly (e.g., selection outlines in UI editors). */
    if ( !points || count < 2 ) return;
    for ( int i = 0; i < count; i++ ) {
        int j = (i + 1) % count;
        float x0 = points[i][0], y0 = points[i][1];
        float x1 = points[j][0], y1 = points[j][1];
        float dx = x1 - x0, dy = y1 - y0;
        float len = sqrtf( dx*dx + dy*dy );
        if ( len < 0.5f ) continue;
        float minX = x0 < x1 ? x0 : x1;
        float minY = y0 < y1 ? y0 : y1;
        float w = fabsf(dx) > 1.0f ? fabsf(dx) : 1.0f;
        float h = fabsf(dy) > 1.0f ? fabsf(dy) : 1.0f;
        GR_DrawBox( minX, minY, w, h );
    }
}

static void GR_Set2DWindow( int x, int y, int w, int h,
                            float left, float right, float bottom, float top,
                            float n, float f )
{
    gr_2d_vp_x   = x;
    gr_2d_vp_y   = y;
    gr_2d_vp_w   = w > 0 ? w : 640;
    gr_2d_vp_h   = h > 0 ? h : 480;
    gr_2d_left   = left;
    gr_2d_right  = right;
    gr_2d_top    = top;
    gr_2d_bottom = bottom;
}

static void GR_DebugLine( const vec3_t start, const vec3_t end,
                          float r, float g, float b, float alpha )
{
 /* Store debug lines for 3D rendering by MoHAARunner.
     * These are viewable via cg_debuglines or developer cheats.
     * For now, log at developer level — proper ImmediateMesh
     * rendering is added when we build the debug overlay system. */
    (void)start; (void)end;
    (void)r; (void)g; (void)b; (void)alpha;
}

/* -------------------------------------------------------------------
 * Fonts & text
 *
 *  Parses .RitualFont files from the VFS, stores glyph metrics,
 *  and emits GR_DrawStretchPic calls for each character so all text
 *  flows through the existing 2D command pipeline.
 * ---------------------------------------------------------------- */

#define MAX_GR_FONTS     32
#define MAX_GR_FONTS_SGL 32

static fontheader_sgl_t  gr_fontsgl[MAX_GR_FONTS_SGL];
static int               gr_numFontsSgl = 0;
static fontheader_t      gr_fonts[MAX_GR_FONTS];
static int               gr_numFonts = 0;
static float             gr_fontHeightScale  = 1.0f;
static float             gr_fontGeneralScale = 1.0f;

/* ----------------------------------------------------------------
 *  Font scale accessors — allow per-widget text scaling.
 *  Used by UI widgets (UIDMBox, UIGMBox, UIConsole) under
 *  GODOT_GDEXTENSION to apply cvar-driven text scale.
 * ---------------------------------------------------------------- */
void Godot_Renderer_SetFontScale( float scale )
{
    gr_fontGeneralScale = scale;
}

float Godot_Renderer_GetFontScale( void )
{
    return gr_fontGeneralScale;
}

/* Clear all cached font data.  Called from GR_Shutdown so that a
 * vid_restart or game directory change (AA→SH→BT) forces fonts to be
 * re-loaded from the current VFS search path. */
static void GR_ClearFontCaches( void )
{
    int fi;
    for ( fi = 0; fi < gr_numFonts; fi++ ) {
        if ( gr_fonts[fi].charTable ) {
            ri.Free( gr_fonts[fi].charTable );
            gr_fonts[fi].charTable = NULL;
        }
    }
    memset( gr_fontsgl, 0, sizeof( gr_fontsgl ) );
    memset( gr_fonts, 0, sizeof( gr_fonts ) );
    gr_numFonts    = 0;
    gr_numFontsSgl = 0;
    gr_fontHandlesDirty = 0;
}

static void GR_RegisterFont( const char *fontName, int pointSize,
                             fontInfo_t *font )
{
    if ( font ) memset( font, 0, sizeof( *font ) );
}

static void GR_RefreshFontHandlesIfNeeded( void )
{
    int i;

    if ( !gr_fontHandlesDirty ) return;

    for ( i = 0; i < gr_numFontsSgl; i++ ) {
        char shaderName[MAX_QPATH];
        Com_sprintf( shaderName, sizeof( shaderName ), "gfx/fonts/%s", gr_fontsgl[i].name );
        gr_fontsgl[i].trhandle = GR_RegisterShaderNoMip( shaderName );
        /* Force CGEN_GLOBAL_COLOR / AGEN_GLOBAL_ALPHA so SetColor controls text colour */
        Godot_ShaderAccessor_OverrideFontShader( shaderName );
    }

    gr_fontHandlesDirty = 0;
}

/* ---- load a single-page .RitualFont ---- */

static fontheader_sgl_t *GR_LoadFont_sgl( const char *name )
{
    int i;
    char *theFile;
    fontheader_sgl_t *header;
    char *ref;
    const char *token;
    int error = 0;

    /* Check for already-loaded */
    for ( i = 0; i < gr_numFontsSgl; i++ ) {
        if ( !Q_stricmp( name, gr_fontsgl[i].name ) )
            return &gr_fontsgl[i];
    }

    if ( gr_numFontsSgl >= MAX_GR_FONTS_SGL ) {
        ri.Printf( PRINT_WARNING, "GR_LoadFont_sgl: too many fonts, returning first\n" );
        return &gr_fontsgl[0];
    }

    header = &gr_fontsgl[gr_numFontsSgl];
    memset( header, 0, sizeof( *header ) );
    Q_strncpyz( header->name, name, sizeof( header->name ) );

    if ( ri.FS_ReadFile( va( "fonts/%s.RitualFont", name ), (void**)&theFile ) == -1 ) {
        ri.Printf( PRINT_WARNING, "GR_LoadFont_sgl: couldn't load font '%s', using fallback\n", name );
        goto make_fallback_in_slot;
    }

    header->height = 0.0f;
    header->aspectRatio = 0.0f;

    ref = theFile;
    while ( ref && !error ) {
        token = COM_Parse( &ref );
        if ( !token[0] ) break;

        if ( !Q_stricmp( token, "RitFont" ) ) {
            continue;
        } else if ( !Q_stricmp( token, "indirections" ) ) {
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
            for ( i = 0; i < 256; i++ ) {
                token = COM_Parse( &ref );
                if ( !token[0] ) { error = 1; break; }
                header->indirection[i] = atoi( token );
            }
            if ( error ) break;
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
        } else if ( !Q_stricmp( token, "locations" ) ) {
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
            for ( i = 0; i < 256; i++ ) {
                token = COM_Parse( &ref );
                if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
                if ( header->aspectRatio == 0.0f ) {
                    ri.Printf( PRINT_WARNING, "GR_LoadFont_sgl: aspect must precede locations in '%s'\n", name );
                    error = 1; break;
                }
                header->locations[i].pos[0]  = atof( COM_Parse( &ref ) ) / 256.0f;
                header->locations[i].pos[1]  = atof( COM_Parse( &ref ) ) * header->aspectRatio / 256.0f;
                header->locations[i].size[0] = atof( COM_Parse( &ref ) ) / 256.0f;
                header->locations[i].size[1] = atof( COM_Parse( &ref ) ) * header->aspectRatio / 256.0f;
                token = COM_Parse( &ref );
                if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
            }
            if ( error ) break;
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
        } else if ( !Q_stricmp( token, "height" ) ) {
            header->height = atof( COM_Parse( &ref ) );
        } else if ( !Q_stricmp( token, "aspect" ) ) {
            header->aspectRatio = atof( COM_Parse( &ref ) );
        } else {
            break;
        }
    }

    ri.FS_FreeFile( theFile );

    if ( error || !header->height || !header->aspectRatio ) {
        ri.Printf( PRINT_WARNING, "GR_LoadFont_sgl: error parsing font '%s', using fallback\n", name );
        goto make_fallback_in_slot;
    }

    /* Register the font texture as a shader (path: gfx/fonts/<name>) */
    {
        char shaderName[MAX_QPATH];
        Com_sprintf( shaderName, sizeof(shaderName), "gfx/fonts/%s", name );
        header->trhandle = GR_RegisterShaderNoMip( shaderName );
        /* Replicate R_LoadFontShader: force CGEN_GLOBAL_COLOR / AGEN_GLOBAL_ALPHA
         * so that re.SetColor() controls text colour (critical for SH/BT fonts
         * whose .shader has explicit "rgbGen identity"). */
        Godot_ShaderAccessor_OverrideFontShader( shaderName );
    }

    gr_numFontsSgl++;
    return header;

make_fallback_in_slot:
    header->height      = 16.0f;
    header->aspectRatio = 1.0f;
    for ( i = 0; i < 256; i++ ) {
        header->indirection[i] = ( i < 128 ) ? i : -1;
        header->locations[i].pos[0]  = 0.0f;
        header->locations[i].pos[1]  = 0.0f;
        header->locations[i].size[0] = 8.0f / 256.0f;
        header->locations[i].size[1] = 16.0f / 256.0f;
    }
    header->trhandle = 0;
    gr_numFontsSgl++;
    return header;
}

/* ---- load a (possibly multi-page) font ---- */


/* ---- load a (possibly multi-page) font ---- */

static fontheader_t *GR_LoadFont( const char *name )
{
    int i;
    char *theFile;
    fontheader_t *header;
    char *ref;
    const char *token;
    int error = 0;
    char *ritFontNames[32];

    /* Check for already-loaded */
    for ( i = 0; i < gr_numFonts; i++ ) {
        if ( !Q_stricmp( name, gr_fonts[i].name ) )
            return &gr_fonts[i];
    }

    if ( gr_numFonts >= MAX_GR_FONTS ) {
        ri.Printf( PRINT_WARNING, "GR_LoadFont: too many fonts, returning first\n" );
        return &gr_fonts[0];
    }

    header = &gr_fonts[gr_numFonts];
    memset( header, 0, sizeof( *header ) );
    Q_strncpyz( header->name, name, sizeof( header->name ) );

    if ( ri.FS_ReadFile( va( "fonts/%s.RitualFont", name ), (void**)&theFile ) == -1 ) {
        ri.Printf( PRINT_WARNING, "GR_LoadFont: couldn't load '%s', using sgl fallback\n", name );
        header->sgl[0] = GR_LoadFont_sgl( name );
        gr_numFonts++;
        return header;
    }

    memset( ritFontNames, 0, sizeof( ritFontNames ) );
    ref = theFile;
    token = COM_Parse( &ref );

    if ( Q_stricmp( token, "RitFontList" ) ) {
        /* Single-page font */
        ri.FS_FreeFile( theFile );
        header->sgl[0] = GR_LoadFont_sgl( name );
        gr_numFonts++;
        return header;
    }

    /* ---- Multi-page font (RitFontList) ---- */
    while ( ref && !error ) {
        token = COM_Parse( &ref );
        if ( !token[0] ) break;

        if ( !Q_stricmp( token, "CodePage" ) ) {
            header->codePage = (short)atoi( COM_Parse( &ref ) );
        } else if ( !Q_stricmp( token, "Chars" ) ) {
            header->charTableLength = atoi( COM_Parse( &ref ) );
            header->charTable = (fontchartable_t *)ri.Malloc(
                header->charTableLength * (int)sizeof( fontchartable_t ) );
            if ( !header->charTable ) { error = 1; break; }
        } else if ( !Q_stricmp( token, "Pages" ) ) {
            header->numPages = atoi( COM_Parse( &ref ) );
        } else if ( !Q_stricmp( token, "RitFontName" ) ) {
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
            for ( i = 0; i < header->numPages; i++ ) {
                token = COM_Parse( &ref );
                if ( !token[0] ) { error = 1; break; }
                ritFontNames[i] = (char *)ri.Malloc( (int)strlen( token ) + 1 );
                if ( !ritFontNames[i] ) { error = 1; break; }
                strcpy( ritFontNames[i], token );
            }
            if ( error ) break;
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
        } else if ( !Q_stricmp( token, "CharTable" ) ) {
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
            for ( i = 0; i < header->charTableLength; i++ ) {
                token = COM_Parse( &ref );
                if ( Q_stricmp( token, "{" ) ) { error = 1; break; }
                header->charTable[i].cp    = (unsigned short)atoi( COM_Parse( &ref ) );
                header->charTable[i].index = (unsigned char)atoi( COM_Parse( &ref ) );
                header->charTable[i].loc   = (unsigned char)atoi( COM_Parse( &ref ) );
                atoi( COM_Parse( &ref ) ); /* skip 4th field */
                token = COM_Parse( &ref );
                if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
            }
            if ( error ) break;
            token = COM_Parse( &ref );
            if ( Q_stricmp( token, "}" ) ) { error = 1; break; }
        } else {
            error = 1;
            break;
        }
    }

    ri.FS_FreeFile( theFile );

    if ( !error ) {
        if ( !header->numPages ) {
            header->sgl[0] = GR_LoadFont_sgl( name );
        } else {
            for ( i = 0; i < header->numPages; i++ ) {
                header->sgl[i] = GR_LoadFont_sgl( ritFontNames[i] );
            }
        }
    }

    /* Free allocated name strings */
    for ( i = 0; i < 32 && ritFontNames[i]; i++ ) {
        ri.Free( ritFontNames[i] );
    }

    if ( error ) {
        if ( header->charTable ) {
            ri.Free( header->charTable );
            header->charTable = NULL;
        }
        ri.Printf( PRINT_WARNING, "GR_LoadFont: error in multi-page font '%s', using fallback\n", name );
        header->numPages = 0;
        header->charTableLength = 0;
        header->sgl[0] = GR_LoadFont_sgl( name );
    }

    gr_numFonts++;
    return header;
}

/* ---- draw a string using single-page glyph data ---- */

static void GR_DrawString_sgl( fontheader_sgl_t *font, const char *text,
                               float x, float y, int maxLen,
                               const float *pvVirtualScreen )
{
    float charHeight;
    float startx, starty;
    int i;
    float fWidthScale = 1.0f, fHeightScale = 1.0f;
    qhandle_t shader;

    if ( !font || !text || !text[0] ) return;

    GR_RefreshFontHandlesIfNeeded();

    /* pvVirtualScreen scaling — matches renderergl1/tr_font.cpp.
     * The UI system calls Set2DWindow with vidWidth×vidHeight ortho,
     * then passes pvVirtualScreen to scale from 640×480 virtual
     * coordinates to actual screen space.  Without this, font glyphs
     * are emitted in 640×480 space against a larger ortho projection,
     * making text too small and mispositioned. */
    if ( pvVirtualScreen ) {
        if ( pvVirtualScreen[0] ) {
            fWidthScale = pvVirtualScreen[0];
        } else {
            fWidthScale = (float)stored_glconfig.vidWidth / 640.0f;
        }

        if ( pvVirtualScreen[1] ) {
            fHeightScale = pvVirtualScreen[1];
        } else {
            fHeightScale = (float)stored_glconfig.vidHeight / 480.0f;
        }
    }

    shader = font->trhandle;
    charHeight = gr_fontHeightScale * font->height * gr_fontGeneralScale;
    startx = x;
    starty = y;

    for ( i = 0; text[i]; i++ ) {
        unsigned char c = (unsigned char)text[i];
        int idx;
        letterloc_t *loc;
        float gx, gy, gw, gh;
        float s1, t1, s2, t2;

        if ( maxLen != -1 && i >= maxLen ) break;

        switch ( c ) {
        case '\t':
            idx = font->indirection[32];
            if ( idx != -1 ) {
                x += gr_fontGeneralScale * font->locations[idx].size[0] * 256.0f * 3.0f;
            }
            break;
        case '\n':
            starty += charHeight;
            x = startx;
            y = starty;
            break;
        case '\r':
            x = startx;
            break;
        default:
            idx = font->indirection[c];
            if ( idx == -1 ) {
                idx = font->indirection['?'];
                if ( idx == -1 ) break;
                font->indirection[c] = idx;
            }
            loc = &font->locations[idx];

            /* Glyph position in 640×480 virtual coords */
            gw = gr_fontGeneralScale * loc->size[0] * 256.0f;
            gh = charHeight;
            gx = x;
            gy = y;

            /* Scale glyph vertices when pvVirtualScreen is set.
             * Matches renderergl1/tr_font.cpp: emitted vertices are
             * scaled to screen space, but cursor advance stays in
             * virtual 640×480 space. */
            if ( pvVirtualScreen ) {
                gx *= fWidthScale;
                gy *= fHeightScale;
                gw *= fWidthScale;
                gh *= fHeightScale;
            }

            /* UV coordinates (already normalised to 0–1) */
            s1 = loc->pos[0];
            t1 = loc->pos[1];
            s2 = loc->pos[0] + loc->size[0];
            t2 = loc->pos[1] + loc->size[1];

            /* Emit as a DrawStretchPic command */
            GR_DrawStretchPic( gx, gy, gw, gh, s1, t1, s2, t2, shader );

            /* Cursor advance in virtual 640×480 space (NOT scaled).
             * Matches tr_font.cpp: x += s_fontGeneralScale * loc->size[0] * 256.0 */
            x += gr_fontGeneralScale * loc->size[0] * 256.0f;
            break;
        }
    }
}

static void GR_DrawString( fontheader_t *font, const char *text,
                           float x, float y, int maxLen,
                           const float *pvVirtualScreen )
{
    if ( !font || !text || !text[0] ) return;

    /* Single-page fast path */
    if ( !font->charTableLength || font->numPages <= 1 ) {
        GR_DrawString_sgl( font->sgl[0], text, x, y, maxLen, pvVirtualScreen );
        return;
    }

    /* Multi-page: route each character to its page.
       For simplicity, we only handle single-page for now —
       multi-page (CJK, etc.) is rare in MOHAA.  Fall back to page 0. */
    GR_DrawString_sgl( font->sgl[0], text, x, y, maxLen, pvVirtualScreen );
}

static float GR_GetFontHeight( const fontheader_t *font )
{
    if ( !font || !font->sgl[0] ) return 16.0f;
    return font->sgl[0]->height * gr_fontGeneralScale * gr_fontHeightScale;
}

static float GR_GetFontStringWidth( const fontheader_t *font,
                                    const char *string )
{
    int i;
    float w = 0.0f;
    fontheader_sgl_t *sgl;

    if ( !font || !string || !font->sgl[0] ) return 0.0f;
    sgl = font->sgl[0];

    for ( i = 0; string[i]; i++ ) {
        unsigned char c = (unsigned char)string[i];
        int idx = sgl->indirection[c];
        if ( idx == -1 ) {
            idx = sgl->indirection['?'];
            if ( idx == -1 ) continue;
        }
        w += gr_fontGeneralScale * sgl->locations[idx].size[0] * 256.0f;
    }
    return w;
}

/* -------------------------------------------------------------------
 *  Marks / fragments
 * ---------------------------------------------------------------- */

/* Forward declarations — implemented in godot_bsp_mesh.cpp */
extern int Godot_BSP_GetEntityToken(char *buffer, int bufferSize);
extern void Godot_BSP_ResetEntityTokenParse(void);
extern void Godot_BSP_GetInlineModelBounds(int index, float *mins, float *maxs);
extern int Godot_BSP_GetMapVersion(void);
extern int Godot_BSP_LightForPoint(const float point[3], float ambientLight[3],
                                   float directedLight[3], float lightDir[3]);
extern int Godot_BSP_InPVS(const float p1[3], const float p2[3]);
extern int Godot_BSP_MarkFragments(
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared);

extern int Godot_BSP_MarkFragmentsForInlineModel(
    int bmodelIndex,
    const float vAngles[3], const float vOrigin[3],
    int numPoints, const float points[][3], const float projection[3],
    int maxPoints, float *pointBuffer,
    int maxFragments,
    int *fragFirstPoint, int *fragNumPoints, int *fragIIndex,
    float fRadiusSquared);

static int GR_MarkFragments( int numPoints, const vec3_t *points,
                             const vec3_t projection,
                             int maxPoints, vec3_t pointBuffer,
                             int maxFragments, markFragment_t *fragmentBuffer,
                             float fRadiusSquared )
{
    /* Temporary arrays to receive Fragment struct fields */
    int fragFirstPoint[MAX_MARK_FRAGMENTS];
    int fragNumPoints[MAX_MARK_FRAGMENTS];
    int fragIIndex[MAX_MARK_FRAGMENTS];

    int clampedMax = maxFragments;
    if (clampedMax > MAX_MARK_FRAGMENTS)
        clampedMax = MAX_MARK_FRAGMENTS;

    int n = Godot_BSP_MarkFragments(
        numPoints, (const float (*)[3])points, projection,
        maxPoints, (float *)pointBuffer,
        clampedMax,
        fragFirstPoint, fragNumPoints, fragIIndex,
        fRadiusSquared);

    /* Copy results into engine markFragment_t array */
    for (int i = 0; i < n; i++) {
        fragmentBuffer[i].firstPoint = fragFirstPoint[i];
        fragmentBuffer[i].numPoints  = fragNumPoints[i];
        fragmentBuffer[i].iIndex     = fragIIndex[i];
    }
    return n;
}

static int GR_MarkFragmentsForInlineModel( clipHandle_t bmodel,
        const vec3_t vAngles, const vec3_t vOrigin,
        int numPoints, const vec3_t *points, const vec3_t projection,
        int maxPoints, vec3_t pointBuffer,
        int maxFragments, markFragment_t *fragmentBuffer,
        float fRadiusSquared )
{
    /* bmodel handle is 1-based brush model index from the engine */
    int fragFirstPoint[MAX_MARK_FRAGMENTS];
    int fragNumPoints[MAX_MARK_FRAGMENTS];
    int fragIIndex[MAX_MARK_FRAGMENTS];

    int clampedMax = maxFragments;
    if (clampedMax > MAX_MARK_FRAGMENTS)
        clampedMax = MAX_MARK_FRAGMENTS;

    int n = Godot_BSP_MarkFragmentsForInlineModel(
        (int)bmodel,
        vAngles, vOrigin,
        numPoints, (const float (*)[3])points, projection,
        maxPoints, (float *)pointBuffer,
        clampedMax,
        fragFirstPoint, fragNumPoints, fragIIndex,
        fRadiusSquared);

    for (int i = 0; i < n; i++) {
        fragmentBuffer[i].firstPoint = fragFirstPoint[i];
        fragmentBuffer[i].numPoints  = fragNumPoints[i];
        fragmentBuffer[i].iIndex     = fragIIndex[i];
    }
    return n;
}

/* -------------------------------------------------------------------
 *  Model queries
 * ---------------------------------------------------------------- */

static int GR_LerpTag( orientation_t *tag, qhandle_t model,
                       int startFrame, int endFrame, float frac,
                       const char *tagName )
{
    if ( tag ) memset( tag, 0, sizeof( *tag ) );
    return -1;
}

static void GR_ModelBounds( qhandle_t model, vec3_t mins, vec3_t maxs )
{
    if ( model >= 1 && model < gr_numModels ) {
        if ( gr_models[model].type == GR_MOD_TIKI && gr_models[model].tiki ) {
            ri.TIKI_CalculateBounds( gr_models[model].tiki, 1.0f, mins, maxs );
            return;
        }
        if ( gr_models[model].type == GR_MOD_BRUSH && gr_models[model].name[0] == '*' ) {
            int subIdx = atoi( gr_models[model].name + 1 );
            Godot_BSP_GetInlineModelBounds( subIdx, mins, maxs );
            return;
        }
    }
    if ( mins ) VectorSet( mins, -16, -16, -16 );
    if ( maxs ) VectorSet( maxs, 16, 16, 16 );
}

static float GR_ModelRadius( qhandle_t handle )
{
    if ( handle >= 1 && handle < gr_numModels &&
         gr_models[handle].type == GR_MOD_TIKI && gr_models[handle].tiki ) {
        return ri.TIKI_GlobalRadius( gr_models[handle].tiki );
    }
    return 32.0f;
}

static dtiki_t *GR_Model_GetHandle( qhandle_t handle )
{
    if ( handle < 1 || handle >= gr_numModels ) return NULL;
    if ( gr_models[handle].type == GR_MOD_TIKI ) return gr_models[handle].tiki;
    return NULL;
}

static refEntity_t *GR_GetRenderEntity( int entityNumber )
{
 /* Return captured entity data if available */
    memset( &scratch_entity, 0, sizeof( scratch_entity ) );
    for ( int i = 0; i < gr_numEntities; i++ ) {
        if ( gr_entities[i].entityNumber == entityNumber ) {
            scratch_entity.reType       = (refEntityType_t)gr_entities[i].reType;
            scratch_entity.hModel       = (qhandle_t)gr_entities[i].hModel;
            scratch_entity.entityNumber = gr_entities[i].entityNumber;
            scratch_entity.scale        = gr_entities[i].scale;
            scratch_entity.renderfx     = gr_entities[i].lightmapNum;
            scratch_entity.customShader = (qhandle_t)gr_entities[i].customShader;
            VectorCopy( gr_entities[i].origin, scratch_entity.origin );
            VectorCopy( gr_entities[i].oldorigin, scratch_entity.oldorigin );
            VectorCopy( gr_entities[i].axis[0], scratch_entity.axis[0] );
            VectorCopy( gr_entities[i].axis[1], scratch_entity.axis[1] );
            VectorCopy( gr_entities[i].axis[2], scratch_entity.axis[2] );
            memcpy( scratch_entity.shaderRGBA, gr_entities[i].shaderRGBA, 4 );
            scratch_entity.radius   = gr_entities[i].radius;
            scratch_entity.rotation = gr_entities[i].rotation;
            scratch_entity.frame    = gr_entities[i].frame;
            scratch_entity.oldframe = gr_entities[i].oldframe;
            scratch_entity.backlerp = gr_entities[i].backlerp;
            scratch_entity.tiki     = (dtiki_t *)gr_entities[i].tiki;
            memcpy( scratch_entity.frameInfo, gr_entities[i].frameInfo, sizeof(scratch_entity.frameInfo) );
            scratch_entity.actionWeight = gr_entities[i].actionWeight;

            /* Copy bone controller data into static buffers so ForceUpdatePose
             * and TIKI_Orientation can use them for skeleton posing. */
            memcpy( scratch_bone_tag, gr_entities[i].bone_tag, sizeof(scratch_bone_tag) );
            memcpy( scratch_bone_quat, gr_entities[i].bone_quat, sizeof(scratch_bone_quat) );
            scratch_entity.bone_tag  = scratch_bone_tag;
            scratch_entity.bone_quat = (vec4_t *)scratch_bone_quat;

            break;
        }
    }
    return &scratch_entity;
}

/* -------------------------------------------------------------------
 *  Shader queries
 * ---------------------------------------------------------------- */

/* -------------------------------------------------------------------
 * Shader dimension cache
 *
 *  First call for a given shader loads the image from VFS, reads
 *  the dimensions, caches them, and frees the data.  Subsequent
 *  calls return the cached value.  Defaults to 64×64 if load fails.
 * ---------------------------------------------------------------- */

/* ── Helper: try to read image dimensions from a buffer of known length.
 *    Returns 1 if dimensions were extracted, 0 otherwise.
 *    Checks TGA format marker (h[2]==2 or 10) before reading TGA fields,
 *    and JPEG SOI (FF D8) + SOF0/SOF2 for JPEG.  This prevents mis-reading
 *    a JPEG file's JFIF header bytes as TGA width/height (which would give
 *    garbage values like width=1). */
static int GR_TryParseDimensions( const unsigned char *h, int len,
                                  int *out_w, int *out_h )
{
    if ( len < 18 || !h ) return 0;

    /* TGA: first validate the image type byte */
    if ( h[2] == 2 || h[2] == 10 ) {
        int w  = h[12] | (h[13] << 8);
        int ht = h[14] | (h[15] << 8);
        if ( w > 0 && w < 8192 && ht > 0 && ht < 8192 ) {
            *out_w = w;
            *out_h = ht;
            return 1;
        }
    }

    /* JPEG: SOI marker (FF D8), then scan for SOF0 (FF C0) or SOF2 (FF C2) */
    if ( h[0] == 0xFF && h[1] == 0xD8 && len > 10 ) {
        for ( int i = 2; i < len - 9; i++ ) {
            if ( h[i] == 0xFF && (h[i+1] == 0xC0 || h[i+1] == 0xC2) ) {
                int ht = (h[i+5] << 8) | h[i+6];
                int w  = (h[i+7] << 8) | h[i+8];
                if ( w > 0 && w < 8192 && ht > 0 && ht < 8192 ) {
                    *out_w = w;
                    *out_h = ht;
                    return 1;
                }
                break;
            }
        }
    }

    return 0;
}

/* ── Helper: strip file extension if present (.tga, .jpg, .png, etc.)
 *    Returns 1 if an extension was stripped, 0 otherwise. */
static int GR_StripImageExtension( const char *in, char *out, int out_size )
{
    int len;
    Q_strncpyz( out, in, out_size );
    len = (int)strlen( out );
    if ( len > 4 && out[len-4] == '.' ) {
        out[len-4] = '\0';
        return 1;
    }
    return 0;
}

/* ── Try to load an image file and extract its dimensions.
 *    Probes .tga then .jpg (matching engine's R_LoadImage order).
 *    Returns 1 if dimensions were found, 0 otherwise. */
static int GR_ProbeImageDimensions( const char *basepath, int *out_w, int *out_h )
{
    void *buf = NULL;
    int len;
    char stripped[MAX_QPATH];
    int had_ext;

    /* Strip any existing extension so we can probe cleanly */
    had_ext = GR_StripImageExtension( basepath, stripped, sizeof(stripped) );

    /* Probe 1: .tga */
    len = ri.FS_ReadFile( va( "%s.tga", stripped ), &buf );
    if ( len > 18 && buf ) {
        if ( GR_TryParseDimensions( (const unsigned char *)buf, len, out_w, out_h ) ) {
            ri.FS_FreeFile( buf );
            return 1;
        }
        ri.FS_FreeFile( buf );
        buf = NULL;
    }
    if ( buf ) { ri.FS_FreeFile( buf ); buf = NULL; }

    /* Probe 2: .jpg (matches engine's R_LoadImage fallback order) */
    len = ri.FS_ReadFile( va( "%s.jpg", stripped ), &buf );
    if ( len > 18 && buf ) {
        if ( GR_TryParseDimensions( (const unsigned char *)buf, len, out_w, out_h ) ) {
            ri.FS_FreeFile( buf );
            return 1;
        }
        ri.FS_FreeFile( buf );
        buf = NULL;
    }
    if ( buf ) { ri.FS_FreeFile( buf ); buf = NULL; }

    /* Probe 3: bare path (may already have correct extension) */
    if ( had_ext ) {
        len = ri.FS_ReadFile( basepath, &buf );
        if ( len > 18 && buf ) {
            if ( GR_TryParseDimensions( (const unsigned char *)buf, len, out_w, out_h ) ) {
                ri.FS_FreeFile( buf );
                return 1;
            }
            ri.FS_FreeFile( buf );
            buf = NULL;
        }
        if ( buf ) { ri.FS_FreeFile( buf ); buf = NULL; }
    }

    return 0;
}

static void GR_CacheShaderDimensions( qhandle_t hShader )
{
    const char *name;
    const char *remapped;
    char resolved[MAX_QPATH];
    int w = 0, h = 0;

    if ( hShader < 1 || hShader >= gr_numShaders ) return;
    if ( gr_shaderWidths[hShader] != GR_DIM_UNKNOWN ) return;

    name = gr_shaders[hShader].name;

    /* Resolve through shader remap table so that the dimensions match
     * the texture that get_shader_texture() will actually load. */
    remapped = Godot_Renderer_GetShaderRemap( name );
    if ( remapped && remapped[0] ) {
        name = remapped;
    }

    /* Step 1: Resolve through .shader definition — the stage `map` directive
     * holds the actual texture path, which may differ from the shader name.
     * This mirrors the lookup that get_shader_texture() does in MoHAARunner. */
    extern int Godot_ShaderProps_GetTextureMap(const char *shader_name, char *out_path, int out_size);
    int sp_found = Godot_ShaderProps_GetTextureMap( name, resolved, sizeof(resolved) );
    if ( sp_found ) {
        if ( GR_ProbeImageDimensions( resolved, &w, &h ) ) {
            gr_shaderWidths[hShader]  = w;
            gr_shaderHeights[hShader] = h;
            return;
        }
    }

    /* Step 2: Fall back to shader name directly (implicit shader, no .shader def).
     * Probe .tga and .jpg — mirrors engine's R_LoadImage which tries both. */
    if ( GR_ProbeImageDimensions( name, &w, &h ) ) {
        gr_shaderWidths[hShader]  = w;
        gr_shaderHeights[hShader] = h;
        return;
    }

    /* Fallback if nothing loaded */
    gr_shaderWidths[hShader]  = 64;
    gr_shaderHeights[hShader] = 64;
}

static int GR_GetShaderWidth( qhandle_t hShader )
{
    if ( hShader < 1 || hShader >= gr_numShaders ) return 64;
    if ( gr_shaderWidths[hShader] == GR_DIM_UNKNOWN )
        GR_CacheShaderDimensions( hShader );
    return gr_shaderWidths[hShader];
}

static int GR_GetShaderHeight( qhandle_t hShader )
{
    if ( hShader < 1 || hShader >= gr_numShaders ) return 64;
    if ( gr_shaderHeights[hShader] == GR_DIM_UNKNOWN )
        GR_CacheShaderDimensions( hShader );
    return gr_shaderHeights[hShader];
}

static const char *GR_GetShaderName( qhandle_t hShader )
{
    if ( hShader >= 1 && hShader < gr_numShaders )
        return gr_shaders[hShader].name;
    return "";
}

static const char *GR_GetModelName( qhandle_t hModel )
{
    if ( hModel >= 1 && hModel < gr_numModels ) return gr_models[hModel].name;
    return "godot_stub_model";
}

qboolean GR_ImageExists( const char *name )
{
    int i;

    if ( !name || !name[0] ) return qfalse;

    /* Check if it's a registered shader name first.  Menu elements
     * often use abstract shader names (e.g. "textures/menu/main_a")
     * that don't correspond to literal file paths but have .shader
     * definitions or were previously registered. */
    for ( i = 1; i < gr_numShaders; i++ ) {
        if ( gr_shaders[i].name[0] && !Q_stricmp( gr_shaders[i].name, name ) ) {
            return qtrue;
        }
    }

    /* Check VFS for actual image file existence (full search path incl. pk3).
     * Use local buffers instead of va() to avoid re-entrance corruption
     * (va() has only 2 rotating slots and FS_ReadFile may call va() internally). */
    if ( ri.FS_ReadFile( name, NULL ) >= 0 ) return qtrue;
    {
        char path[MAX_QPATH];
        Com_sprintf( path, sizeof( path ), "%s.tga", name );
        if ( ri.FS_ReadFile( path, NULL ) >= 0 ) return qtrue;
        Com_sprintf( path, sizeof( path ), "%s.jpg", name );
        if ( ri.FS_ReadFile( path, NULL ) >= 0 ) return qtrue;
        Com_sprintf( path, sizeof( path ), "%s.png", name );
        if ( ri.FS_ReadFile( path, NULL ) >= 0 ) return qtrue;
    }
    return qfalse;
}

static int GR_CountTextureMemory( void )
{
    return 0;
}

/* -------------------------------------------------------------------
 *  Misc
 * ---------------------------------------------------------------- */

static void GR_InvalidateDimensionsForShader( const char *shaderName )
{
    /* Invalidate cached dimensions for any handle whose name matches
     * so that GR_GetShaderWidth/Height will re-probe the file. */
    for ( int i = 1; i < gr_numShaders; i++ ) {
        if ( Q_stricmp( gr_shaders[i].name, shaderName ) == 0 ) {
            gr_shaderWidths[i]  = GR_DIM_UNKNOWN;
            gr_shaderHeights[i] = GR_DIM_UNKNOWN;
        }
    }
}

static void GR_RemapShader( const char *oldShader, const char *newShader,
                            const char *offsetTime )
{
    if ( !oldShader || !newShader ) return;

    for ( int i = 0; i < gr_numShaderRemaps; i++ ) {
        if ( Q_stricmp( gr_shaderRemaps[i].oldName, oldShader ) == 0 ) {
            strncpy( gr_shaderRemaps[i].newName, newShader, MAX_QPATH - 1 );
            gr_shaderRemaps[i].newName[MAX_QPATH - 1] = '\0';
            gr_shaderRemaps[i].timeOffset = offsetTime ? atoi( offsetTime ) : 0;
            GR_InvalidateDimensionsForShader( oldShader );
            return;
        }
    }
    if ( gr_numShaderRemaps >= GR_MAX_SHADER_REMAPS ) return;

    gr_shaderRemap_t *r = &gr_shaderRemaps[gr_numShaderRemaps++];
    strncpy( r->oldName, oldShader, MAX_QPATH - 1 );
    r->oldName[MAX_QPATH - 1] = '\0';
    strncpy( r->newName, newShader, MAX_QPATH - 1 );
    r->newName[MAX_QPATH - 1] = '\0';
    r->timeOffset = offsetTime ? atoi( offsetTime ) : 0;
    GR_InvalidateDimensionsForShader( oldShader );
}

static qboolean GR_GetEntityToken( char *buffer, int size )
{
    return Godot_BSP_GetEntityToken( buffer, size ) ? qtrue : qfalse;
}

static qboolean GR_inPVS( const vec3_t p1, const vec3_t p2 )
{
    return Godot_BSP_InPVS( p1, p2 ) ? qtrue : qfalse;
}

static void GR_TakeVideoFrame( int h, int w, byte *captureBuffer,
                               byte *encodeBuffer, qboolean motionJpeg )
{
}

static void GR_FreeModels( void )
{
    GR_ModelInit();
}

static void GR_PrintBSPFileSizes( void )
{
}

static int GR_MapVersion( void )
{
    return Godot_BSP_GetMapVersion();
}

static const char *GR_GetGraphicsInfo( void )
{
    return "Godot Renderer Backend (stub)";
}

/* Pose the skeleton from the entity's animation channels, with
 * per-frame deduplication matching the real renderer's R_UpdatePoseInternal.
 * ENTITYNUM_NONE entities always re-pose (no tracking). */
static void GR_UpdatePoseInternal( refEntity_t *model )
{
    void *skeletor;

    if ( !model || !model->tiki ) return;

    if ( model->entityNumber != ENTITYNUM_NONE ) {
        if ( model->entityNumber >= 0 && model->entityNumber < MAX_GENTITIES ) {
            if ( gr_skel_index[model->entityNumber] == gr_frame_skel_index ) {
                return; /* Already posed this frame */
            }
            gr_skel_index[model->entityNumber] = gr_frame_skel_index;
        }
    }

    skeletor = ri.TIKI_GetSkeletor( model->tiki, model->entityNumber );
    if ( skeletor ) {
        ri.TIKI_SetPoseInternal(
            skeletor,
            model->frameInfo,
            model->bone_tag,
            model->bone_quat,
            model->actionWeight
        );
    }
}

static void GR_ForceUpdatePose( refEntity_t *model )
{
    void *skeletor;

    /* ForceUpdatePose ALWAYS re-poses (no frame-skip early return),
     * but marks the entity as posed so subsequent UpdatePoseInternal
     * calls within the same frame can skip. */
    if ( !model || !model->tiki ) return;

    if ( model->entityNumber != ENTITYNUM_NONE
         && model->entityNumber >= 0
         && model->entityNumber < MAX_GENTITIES ) {
        gr_skel_index[model->entityNumber] = gr_frame_skel_index;
    }

    skeletor = ri.TIKI_GetSkeletor( model->tiki, model->entityNumber );
    if ( skeletor ) {
        ri.TIKI_SetPoseInternal(
            skeletor,
            model->frameInfo,
            model->bone_tag,
            model->bone_quat,
            model->actionWeight
        );
    }
}

static orientation_t GR_TIKI_Orientation( refEntity_t *model, int tagNum )
{
    orientation_t o;
    if ( model && model->tiki ) {
        /* Match the real renderer's RE_TIKI_Orientation: always ensure
         * the skeleton is in the current pose before querying a bone tag. */
        GR_UpdatePoseInternal( model );
        o = ri.TIKI_OrientationInternal( model->tiki, model->entityNumber, tagNum, model->scale );
        return o;
    }
    memset( &o, 0, sizeof( o ) );
    o.axis[0][0] = o.axis[1][1] = o.axis[2][2] = 1.0f;
    return o;
}

static qboolean GR_TIKI_IsOnGround( refEntity_t *model, int tagNum,
                                    float threshold )
{
    if ( model && model->tiki ) {
        return ri.TIKI_IsOnGroundInternal( model->tiki, model->entityNumber, tagNum, threshold );
    }
    return qtrue;
}

static void GR_SetFrameNumber( int frameNumber )
{
    gr_frameNumber = frameNumber;
}

static void GR_SavePerformanceCounters( void )
{
}

static void GR_GetInlineModelBounds( int index, vec3_t mins, vec3_t maxs )
{
    Godot_BSP_GetInlineModelBounds( index, mins, maxs );
}

static void GR_GetLightingForDecal( vec3_t light, const vec3_t facing,
                                    const vec3_t origin )
{
    if ( !light ) return;
    float ambient[3], directed[3], dir[3];
    if ( Godot_BSP_LightForPoint( origin, ambient, directed, dir ) ) {
        /* Blend ambient + directed modulated by facing·dir */
        float dot = facing[0]*dir[0] + facing[1]*dir[1] + facing[2]*dir[2];
        if ( dot < 0.0f ) dot = 0.0f;
        light[0] = ambient[0] + directed[0] * dot;
        light[1] = ambient[1] + directed[1] * dot;
        light[2] = ambient[2] + directed[2] * dot;
        /* Clamp */
        for (int i = 0; i < 3; i++) {
            if (light[i] > 1.0f) light[i] = 1.0f;
        }
    } else {
        VectorSet( light, 1.0f, 1.0f, 1.0f );
    }
}

static void GR_GetLightingForSmoke( vec3_t light, const vec3_t origin )
{
    if ( !light ) return;
    float ambient[3], directed[3], dir[3];
    if ( Godot_BSP_LightForPoint( origin, ambient, directed, dir ) ) {
        /* Smoke uses full omnidirectional lighting */
        light[0] = ambient[0] + directed[0] * 0.5f;
        light[1] = ambient[1] + directed[1] * 0.5f;
        light[2] = ambient[2] + directed[2] * 0.5f;
        for (int i = 0; i < 3; i++) {
            if (light[i] > 1.0f) light[i] = 1.0f;
        }
    } else {
        VectorSet( light, 1.0f, 1.0f, 1.0f );
    }
}

static int GR_GatherLightSources( const vec3_t pos, vec3_t *lightPos,
                                  vec3_t *lightIntensity, int maxLights )
{
    /* Return active dynamic lights that are close to pos */
    int count = 0;
    for ( int i = 0; i < gr_numDlights && count < maxLights; i++ ) {
        float dx = gr_dlights[i].origin[0] - pos[0];
        float dy = gr_dlights[i].origin[1] - pos[1];
        float dz = gr_dlights[i].origin[2] - pos[2];
        float dist2 = dx*dx + dy*dy + dz*dz;
        float r = gr_dlights[i].intensity;
        if ( r <= 0.0f ) continue;
        if ( dist2 < r * r ) {
            if ( lightPos ) {
                lightPos[count][0] = gr_dlights[i].origin[0];
                lightPos[count][1] = gr_dlights[i].origin[1];
                lightPos[count][2] = gr_dlights[i].origin[2];
            }
            if ( lightIntensity ) {
                float atten = 1.0f - sqrtf(dist2) / r;
                lightIntensity[count][0] = gr_dlights[i].r * atten;
                lightIntensity[count][1] = gr_dlights[i].g * atten;
                lightIntensity[count][2] = gr_dlights[i].b * atten;
            }
            count++;
        }
    }
    return count;
}

/* -------------------------------------------------------------------
 *  Swipe effects
 * ---------------------------------------------------------------- */

static void GR_SwipeBegin( float thisTime, float life, qhandle_t hShader )
{
    memset( &gr_currentSwipe, 0, sizeof( gr_currentSwipe ) );
    gr_currentSwipe.thisTime  = thisTime;
    gr_currentSwipe.life      = life;
    gr_currentSwipe.hShader   = (int)hShader;
    gr_currentSwipe.numPoints = 0;
    gr_currentSwipe.active    = 1;
}

static void GR_SwipePoint( vec3_t point1, vec3_t point2, float time )
{
    if ( !gr_currentSwipe.active ) return;
    if ( gr_currentSwipe.numPoints >= GR_MAX_SWIPE_POINTS ) return;
    gr_swipePoint_t *sp = &gr_currentSwipe.points[gr_currentSwipe.numPoints++];
    VectorCopy( point1, sp->point1 );
    VectorCopy( point2, sp->point2 );
    sp->time = time;
}

static void GR_SwipeEnd( void )
{
    gr_currentSwipe.active = 0;
    /* Swipe data is now ready for rendering by MoHAARunner */
}

/* -------------------------------------------------------------------
 *  Mode / fullscreen
 * ---------------------------------------------------------------- */

static qboolean GR_SetMode( int mode, const glconfig_t *glConfig )
{
    if ( glConfig ) stored_glconfig = *glConfig;
    return qtrue;
}

static void GR_SetFullscreen( qboolean fullScreen )
{
    (void)fullScreen;  /* Godot manages window mode */
}

/* ter_restart command handler — terrain quality changes are
 * applied on next map load, so this is effectively a no-op. */
static void GR_TerrainRestart_f( void )
{
    ri.Printf( PRINT_ALL, "[GodotRenderer] ter_restart: terrain quality changes will apply on next map load.\\n" );
}

static void GR_SetRenderTime( int t )
{
    gr_renderTime = t;
}

/* Perlin noise — 3D+time noise for shader effects */
static float GR_Noise( float x, float y, float z, double t )
{
    /* Simple pseudo-random hash-based noise.
     * Uses a combination of sin-based hashing to approximate Perlin noise.
     * Good enough for shader wobble/turbulence effects. */
    float ft = (float)t;
    float n = x * 12.9898f + y * 78.233f + z * 37.719f + ft * 43.758f;
    /* sin-hash trick — fast, visually acceptable pseudo-noise */
    float s = sinf(n) * 43758.5453f;
    s = s - floorf(s);  /* fract */
    /* Make it centred on 0 (-0.5 to 0.5 range) */
    return s - 0.5f;
}

/* -------------------------------------------------------------------
 * Raw image loading
 *
 *  Loads TGA/JPG images through the engine VFS for cgame/UI use.
 *  Tries multiple extensions (.tga, .jpg) as the engine normally does.
 * ---------------------------------------------------------------- */

static qboolean GR_LoadTGA( const byte *fileData, int fileLen,
                            byte **pic, int *width, int *height )
{
    /* Minimal uncompressed/RLE TGA reader for 24/32-bit images */
    if ( fileLen < 18 ) return qfalse;
    const byte *hdr = fileData;
    int idLen      = hdr[0];
    int cmapType   = hdr[1];
    int imgType    = hdr[2];   /* 2=uncompressed, 10=RLE */
    int w          = hdr[12] | (hdr[13] << 8);
    int h          = hdr[14] | (hdr[15] << 8);
    int bpp        = hdr[16];
    int descriptor = hdr[17];

    if ( cmapType != 0 ) return qfalse;
    if ( imgType != 2 && imgType != 10 ) return qfalse;
    if ( bpp != 24 && bpp != 32 ) return qfalse;
    if ( w <= 0 || h <= 0 || w > 8192 || h > 8192 ) return qfalse;

    int pixelSize = bpp / 8;
    int numPixels = w * h;
    byte *out = (byte *)ri.Malloc( numPixels * 4 );
    if ( !out ) return qfalse;

    const byte *src = fileData + 18 + idLen;
    int srcLeft = fileLen - 18 - idLen;
    int flipVert = !(descriptor & 0x20);  /* bit 5 = top-to-bottom */

    if ( imgType == 2 ) {
        /* Uncompressed */
        for ( int i = 0; i < numPixels && srcLeft >= pixelSize; i++ ) {
            int row = flipVert ? (h - 1 - i / w) : (i / w);
            int col = i % w;
            int di  = (row * w + col) * 4;
            out[di + 2] = src[0];   /* B → R */
            out[di + 1] = src[1];   /* G */
            out[di + 0] = src[2];   /* R → B (BGRA → RGBA) */
            out[di + 3] = (bpp == 32) ? src[3] : 255;
            src     += pixelSize;
            srcLeft -= pixelSize;
        }
    } else {
        /* RLE */
        int pixel = 0;
        while ( pixel < numPixels && srcLeft > 0 ) {
            byte runHdr = *src++; srcLeft--;
            int count = (runHdr & 0x7F) + 1;
            if ( runHdr & 0x80 ) {
                /* Run-length packet */
                if ( srcLeft < pixelSize ) break;
                byte b = src[0], g = src[1], r = src[2];
                byte a = (bpp == 32 && srcLeft >= 4) ? src[3] : 255;
                src += pixelSize; srcLeft -= pixelSize;
                for ( int j = 0; j < count && pixel < numPixels; j++, pixel++ ) {
                    int row = flipVert ? (h - 1 - pixel / w) : (pixel / w);
                    int col = pixel % w;
                    int di  = (row * w + col) * 4;
                    out[di + 0] = r; out[di + 1] = g; out[di + 2] = b; out[di + 3] = a;
                }
            } else {
                /* Raw packet */
                for ( int j = 0; j < count && pixel < numPixels && srcLeft >= pixelSize; j++, pixel++ ) {
                    int row = flipVert ? (h - 1 - pixel / w) : (pixel / w);
                    int col = pixel % w;
                    int di  = (row * w + col) * 4;
                    out[di + 2] = src[0];
                    out[di + 1] = src[1];
                    out[di + 0] = src[2];
                    out[di + 3] = (bpp == 32) ? src[3] : 255;
                    src += pixelSize; srcLeft -= pixelSize;
                }
            }
        }
    }

    *pic    = out;
    *width  = w;
    *height = h;
    return qtrue;
}

static qboolean GR_LoadRawImage( const char *name, byte **pic,
                                 int *width, int *height )
{
    if ( pic )    *pic    = NULL;
    if ( width )  *width  = 0;
    if ( height ) *height = 0;
    if ( !name || !pic ) return qfalse;

    /* Try the exact name first, then .tga extension */
    const char *extensions[] = { "", ".tga", NULL };
    char tryPath[MAX_QPATH];

    for ( int i = 0; extensions[i]; i++ ) {
        void *buf = NULL;
        int len;

        if ( extensions[i][0] == '\0' ) {
            len = ri.FS_ReadFile( name, &buf );
        } else {
            snprintf( tryPath, sizeof(tryPath), "%s%s", name, extensions[i] );
            len = ri.FS_ReadFile( tryPath, &buf );
        }

        if ( buf && len > 0 ) {
            qboolean ok = GR_LoadTGA( (byte *)buf, len, pic, width, height );
            ri.FS_FreeFile( buf );
            if ( ok ) return qtrue;
        }
        if ( buf ) ri.FS_FreeFile( buf );
    }

    return qfalse;
}

static void GR_FreeRawImage( byte *pic )
{
    if ( pic ) ri.Free( pic );
}

static void GR_Set2DInitialShaderTime( float startTime )
{
    gr_2dShaderStartTime = startTime;
}

/* -------------------------------------------------------------------
 * Weapon effects
 * ---------------------------------------------------------------- */

extern void Godot_MuzzleFlash_Spawn_C(float *pos, float *dir, float intensity);
extern void Godot_ShellCasing_Eject_C(float *pos, float *vel, int type);

static void GR_AddMuzzleFlash( const vec3_t origin, const vec3_t dir, float intensity )
{
    Godot_MuzzleFlash_Spawn_C( (float*)origin, (float*)dir, intensity );
}

static void GR_AddShellCasing( const vec3_t origin, const vec3_t velocity, int type )
{
    Godot_ShellCasing_Eject_C( (float*)origin, (float*)velocity, type );
}

/* ===================================================================
 * Camera bridge C accessors
 *
 *  Called from MoHAARunner.cpp (via extern "C") after each Com_Frame()
 *  to read the latest viewpoint data captured by GR_RenderScene.
 * ================================================================ */

int Godot_Renderer_HasNewFrame( void )
{
    return (int)gr_hasNewFrame;
}

void Godot_Renderer_ClearNewFrame( void )
{
    gr_hasNewFrame = qfalse;
}

int Godot_Renderer_GetFrameCount( void )
{
    return gr_frameCount;
}

void Godot_Renderer_GetViewOrigin( float *out )
{
    out[0] = gr_viewOrigin[0];
    out[1] = gr_viewOrigin[1];
    out[2] = gr_viewOrigin[2];
}

void Godot_Renderer_GetViewAxis( float *out )
{
    /* 9 floats: viewaxis[0][0..2], viewaxis[1][0..2], viewaxis[2][0..2] */
    memcpy( out, gr_viewAxis, sizeof( gr_viewAxis ) );
}

void Godot_Renderer_GetFov( float *fov_x, float *fov_y )
{
    if ( fov_x ) *fov_x = gr_fov_x;
    if ( fov_y ) *fov_y = gr_fov_y;
}

void Godot_Renderer_GetRenderSize( int *w, int *h )
{
    if ( w ) *w = gr_renderWidth;
    if ( h ) *h = gr_renderHeight;
}

void Godot_Renderer_GetFarplane( float *distance, float *bias, float *color, int *cull )
{
    if ( distance ) *distance = gr_farplane_distance;
    if ( bias ) *bias = gr_farplane_bias;
    if ( color ) {
        color[0] = gr_farplane_color[0];
        color[1] = gr_farplane_color[1];
        color[2] = gr_farplane_color[2];
    }
    if ( cull ) *cull = (int)gr_farplane_cull;
}

int Godot_Renderer_GetSkyboxFarplane( void )
{
    return (int)gr_skybox_farplane;
}

/* Also expose the world map path so MoHAARunner knows what BSP to load */

const char *Godot_Renderer_GetWorldMapName( void )
{
    return gr_worldMapName;
}

int Godot_Renderer_IsWorldMapLoaded( void )
{
    return (int)gr_worldMapLoaded;
}

int Godot_Renderer_GetRenderTerrain( void )
{
    return (int)gr_renderTerrain;
}

/* ===================================================================
 * Entity bridge C accessors
 *
 *  Give MoHAARunner.cpp read access to the entities, sprites, and
 *  dynamic lights captured during the current frame.
 * ================================================================ */

int Godot_Renderer_GetEntityCount( void )
{
    return gr_numEntities;
}

/* Fill flat arrays with transform data for entity i.
 * Returns the entity type (reType), or -1 if out of range. */
int Godot_Renderer_GetEntity( int index,
                              float *origin,      /* [3] */
                              float *axis,        /* [9] */
                              float *out_scale,   /* [1] */
                              int   *hModel,      /* [1] */
                              int   *entityNumber,/* [1] */
                              unsigned char *rgba, /* [4] */
                              int   *renderfx )   /* [1] */
{
    if ( index < 0 || index >= gr_numEntities ) return -1;

    const gr_entity_t *ge = &gr_entities[index];

    if ( origin ) {
        origin[0] = ge->origin[0];
        origin[1] = ge->origin[1];
        origin[2] = ge->origin[2];
    }
    if ( axis ) {
        memcpy( axis, ge->axis, 9 * sizeof(float) );
    }
    if ( out_scale )    *out_scale    = ge->scale;
    if ( hModel )       *hModel       = ge->hModel;
    if ( entityNumber ) *entityNumber = ge->entityNumber;
    if ( rgba ) {
        rgba[0] = ge->shaderRGBA[0];
        rgba[1] = ge->shaderRGBA[1];
        rgba[2] = ge->shaderRGBA[2];
        rgba[3] = ge->shaderRGBA[3];
    }
    if ( renderfx )     *renderfx     = ge->lightmapNum;

    return ge->reType;
}

/* Beam-specific accessors (origin2 / diameter) */
void Godot_Renderer_GetEntityBeam( int index,
                                   float *from,    /* [3] — origin */
                                   float *to,      /* [3] — oldorigin */
                                   float *diameter )
{
    if ( index < 0 || index >= gr_numEntities ) return;
    const gr_entity_t *ge = &gr_entities[index];
    if ( from )     VectorCopy( ge->origin, from );
    if ( to )       VectorCopy( ge->oldorigin, to );
    if ( diameter ) *diameter = (float)ge->frame;
}

/* Sprite-specific accessors (radius, rotation, customShader) */
void Godot_Renderer_GetEntitySprite( int index,
                                     float *radius,
                                     float *rotation,
                                     int   *customShader )
{
    if ( index < 0 || index >= gr_numEntities ) return;
    const gr_entity_t *ge = &gr_entities[index];
    if ( radius )       *radius       = ge->radius;
    if ( rotation )     *rotation     = ge->rotation;
    if ( customShader ) *customShader = ge->customShader;
}

/* Entity parenting accessor */
int Godot_Renderer_GetEntityParent( int index )
{
    if ( index < 0 || index >= gr_numEntities ) return -1;
    return gr_entities[index].parentEntity;
}

/* Entity lighting origin accessor */
void Godot_Renderer_GetEntityLightingOrigin( int index, float *out )
{
    if ( index < 0 || index >= gr_numEntities || !out ) return;
    out[0] = gr_entities[index].lightingOrigin[0];
    out[1] = gr_entities[index].lightingOrigin[1];
    out[2] = gr_entities[index].lightingOrigin[2];
}

/* Per-surface state flags and skin selector.
 * out_surfaces must point to a 32-byte buffer (MAX_MODEL_SURFACES).
 * Bit 2 (MDL_SURFACE_NODRAW=4) indicates the surface should not be drawn.
 * Bits 0-1 give the per-surface skin variant offset (added to skinNum).
 * Mirrors the surfaces[] / skinNum fields of refEntity_t used in
 * tr_model.cpp::R_AddSkelSurfaces for correct surface rendering. */
void Godot_Renderer_GetEntitySurfaces( int index,
                                       unsigned char *out_surfaces,
                                       int *out_skinNum )
{
    if ( index < 0 || index >= gr_numEntities ) {
        if ( out_skinNum )   *out_skinNum = 0;
        if ( out_surfaces )  memset( out_surfaces, 0, 32 );
        return;
    }
    if ( out_surfaces )
        memcpy( out_surfaces, gr_entities[index].surfaces, 32 );
    if ( out_skinNum )
        *out_skinNum = gr_entities[index].skinNum;
}

/* Shadow blob accessors: shadow plane height and model radius */
float Godot_Renderer_GetEntityShadowPlane( int index )
{
    if ( index < 0 || index >= gr_numEntities ) return 0.0f;
    return gr_entities[index].shadowPlane;
}

float Godot_Model_GetRadius( int hModel )
{
    return GR_ModelRadius( (qhandle_t)hModel );
}

int Godot_Model_GetSpriteShader( int hModel )
{
    if ( hModel < 0 || hModel >= gr_numModels ) return 0;
    if ( gr_models[hModel].type == GR_MOD_SPRITE ) {
        return gr_models[hModel].shaderHandle;
    }
    return 0;
}

/* Get sprite model dimensions (image-based sizing) for GR_MOD_SPRITE models.
 * Returns 1 if model is a sprite with valid dimensions, 0 otherwise. */
int Godot_Model_GetSpriteDims( int hModel,
                               float *outWidth, float *outHeight,
                               float *outSpriteScale )
{
    if ( hModel < 0 || hModel >= gr_numModels ) return 0;
    if ( gr_models[hModel].type != GR_MOD_SPRITE ) return 0;
    if ( gr_models[hModel].spriteWidth <= 0.0f ) return 0;
    if ( outWidth )       *outWidth       = gr_models[hModel].spriteWidth;
    if ( outHeight )      *outHeight      = gr_models[hModel].spriteHeight;
    if ( outSpriteScale ) *outSpriteScale = gr_models[hModel].spriteScale;
    return 1;
}

/* Shader remap query — check if a shader name has been remapped */
const char *Godot_Renderer_GetShaderRemap( const char *shaderName )
{
    if ( !shaderName ) return NULL;
    for ( int i = 0; i < gr_numShaderRemaps; i++ ) {
        if ( Q_stricmp( gr_shaderRemaps[i].oldName, shaderName ) == 0 ) {
            return gr_shaderRemaps[i].newName;
        }
    }
    return NULL;
}

/* Swipe effect accessor */
int Godot_Renderer_GetSwipeData( float *thisTime, float *life,
                                 int *hShader, int *numPoints )
{
    if ( gr_currentSwipe.numPoints <= 0 ) return 0;
    if ( thisTime )  *thisTime  = gr_currentSwipe.thisTime;
    if ( life )      *life      = gr_currentSwipe.life;
    if ( hShader )   *hShader   = gr_currentSwipe.hShader;
    if ( numPoints ) *numPoints = gr_currentSwipe.numPoints;
    return 1;
}

void Godot_Renderer_GetSwipePoint( int index, float *point1, float *point2,
                                   float *time )
{
    if ( index < 0 || index >= gr_currentSwipe.numPoints ) return;
    const gr_swipePoint_t *sp = &gr_currentSwipe.points[index];
    if ( point1 ) VectorCopy( sp->point1, point1 );
    if ( point2 ) VectorCopy( sp->point2, point2 );
    if ( time )   *time = sp->time;
}

/* Terrain mark accessor */
int Godot_Renderer_GetTerrainMarkCount( void )
{
    return gr_numTerrainMarks;
}

void Godot_Renderer_GetTerrainMark( int index, int *hShader, int *numVerts,
                                    int *terrainIndex, int *renderfx )
{
    if ( index < 0 || index >= gr_numTerrainMarks ) return;
    const gr_terrainMark_t *tm = &gr_terrainMarks[index];
    if ( hShader )      *hShader      = tm->hShader;
    if ( numVerts )     *numVerts     = tm->numVerts;
    if ( terrainIndex ) *terrainIndex = tm->terrainIndex;
    if ( renderfx )     *renderfx     = tm->renderfx;
}

void Godot_Renderer_GetTerrainMarkVert( int markIndex, int vertIndex,
                                        float *xyz, float *st,
                                        unsigned char *rgba )
{
    if ( markIndex < 0 || markIndex >= gr_numTerrainMarks ) return;
    const gr_terrainMark_t *tm = &gr_terrainMarks[markIndex];
    if ( vertIndex < 0 || vertIndex >= tm->numVerts ) return;
    int vi = tm->firstVert + vertIndex;
    if ( vi < 0 || vi >= gr_numTerrainMarkVerts ) return;
    const gr_terrainMarkVert_t *v = &gr_terrainMarkVerts[vi];
    if ( xyz )  { xyz[0] = v->xyz[0]; xyz[1] = v->xyz[1]; xyz[2] = v->xyz[2]; }
    if ( st )   { st[0] = v->st[0]; st[1] = v->st[1]; }
    if ( rgba ) { rgba[0] = v->rgba[0]; rgba[1] = v->rgba[1]; rgba[2] = v->rgba[2]; rgba[3] = v->rgba[3]; }
}

/* Scissor state accessor */
void Godot_Renderer_GetScissor( int *x, int *y, int *width, int *height )
{
    if ( x )      *x      = gr_scissorX;
    if ( y )      *y      = gr_scissorY;
    if ( width )  *width  = gr_scissorWidth;
    if ( height ) *height = gr_scissorHeight;
}

/* Background image accessor */
int Godot_Renderer_GetBackground( int *cols, int *rows, int *bgr,
                                  const unsigned char **data )
{
    if ( !gr_bgActive ) return 0;
    if ( cols ) *cols = gr_bgCols;
    if ( rows ) *rows = gr_bgRows;
    if ( bgr )  *bgr  = gr_bgBgr;
    if ( data ) *data  = gr_bgData;
    return 1;
}

int Godot_Renderer_GetBackgroundCmdIndex( void )
{
    return gr_bgCmdIndex;
}

int Godot_Cvar_VariableIntegerValue( const char *var_name )
{
    return ri.Cvar_VariableIntegerValue( var_name );
}

int Godot_Renderer_GetDlightCount( void )
{
    return gr_numDlights;
}

void Godot_Renderer_GetDlight( int index,
                               float *origin,    /* [3] */
                               float *intensity,
                               float *r, float *g, float *b,
                               int   *type )
{
    if ( index < 0 || index >= gr_numDlights ) return;
    const gr_dlight_t *dl = &gr_dlights[index];
    if ( origin )    VectorCopy( dl->origin, origin );
    if ( intensity ) *intensity = dl->intensity;
    if ( r )         *r = dl->r;
    if ( g )         *g = dl->g;
    if ( b )         *b = dl->b;
    if ( type )      *type = dl->type;
}

/* ===================================================================
 * Poly C accessors
 *
 *  Give MoHAARunner.cpp read access to captured polys for particle/
 *  smoke/explosion/decal rendering.
 * ================================================================ */

int Godot_Renderer_GetPolyCount( void )
{
    return gr_numPolys;
}

/* Returns numVerts for the poly.  Writes shader handle and vertex data. */
int Godot_Renderer_GetPoly( int index,
                            int   *hShader,
                            float *positions,     /* [numVerts * 3] */
                            float *texcoords,     /* [numVerts * 2] */
                            unsigned char *colors, /* [numVerts * 4] */
                            int maxVerts )
{
    if ( index < 0 || index >= gr_numPolys ) return 0;
    const gr_poly_t *p = &gr_polys[index];
    if ( hShader ) *hShader = p->hShader;

    int count = p->numVerts;
    if ( count > maxVerts ) count = maxVerts;

    int v;
    for ( v = 0; v < count; v++ ) {
        const gr_polyVert_t *pv = &gr_polyVerts[p->firstVert + v];
        if ( positions ) {
            positions[v*3+0] = pv->xyz[0];
            positions[v*3+1] = pv->xyz[1];
            positions[v*3+2] = pv->xyz[2];
        }
        if ( texcoords ) {
            texcoords[v*2+0] = pv->st[0];
            texcoords[v*2+1] = pv->st[1];
        }
        if ( colors ) {
            colors[v*4+0] = pv->rgba[0];
            colors[v*4+1] = pv->rgba[1];
            colors[v*4+2] = pv->rgba[2];
            colors[v*4+3] = pv->rgba[3];
        }
    }

    return p->numVerts;
}

/* ===================================================================
 * 2D overlay C accessors
 *
 *  Give MoHAARunner.cpp read access to the 2D draw command buffer
 *  and shader name table for HUD overlay rendering.
 * ================================================================ */

int Godot_Renderer_Get2DCmdCount( void )
{
    return gr_num2DCmds;
}

uint64_t Godot_Renderer_Get2DCmdHash( void )
{
    uint64_t hash = 14695981039346656037ULL;
    const unsigned char *b = (const unsigned char *)gr_2d_cmds;
    size_t n = gr_num2DCmds * sizeof(gr_2d_cmd_t);
    size_t i;
    for ( i = 0; i < n; i++ ) {
        hash ^= b[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int Godot_Renderer_Get2DCmd( int index,
                             int   *type,     /* gr_2d_type_t */
                             float *x, float *y, float *w, float *h,
                             float *s1, float *t1, float *s2, float *t2,
                             float *color,    /* [4] */
                             int   *shader )
{
    if ( index < 0 || index >= gr_num2DCmds ) return 0;
    const gr_2d_cmd_t *cmd = &gr_2d_cmds[index];
    if ( type )   *type   = (int)cmd->type;
    if ( x )      *x      = cmd->x;
    if ( y )      *y      = cmd->y;
    if ( w )      *w      = cmd->w;
    if ( h )      *h      = cmd->h;
    if ( s1 )     *s1     = cmd->s1;
    if ( t1 )     *t1     = cmd->t1;
    if ( s2 )     *s2     = cmd->s2;
    if ( t2 )     *t2     = cmd->t2;
    if ( color ) {
        color[0] = cmd->color[0];
        color[1] = cmd->color[1];
        color[2] = cmd->color[2];
        color[3] = cmd->color[3];
    }
    if ( shader ) *shader = cmd->shader;
    return 1;
}

int Godot_Renderer_Get2DCmdTriVerts( int index,
                                     float *verts,   /* [3][2] = 6 floats: screen-space positions */
                                     float *uvs )    /* [3][2] = 6 floats: texture coordinates */
{
    if ( index < 0 || index >= gr_num2DCmds ) return 0;
    const gr_2d_cmd_t *cmd = &gr_2d_cmds[index];
    if ( (int)cmd->type != (int)GR_2D_TRIANGLE ) return 0;
    if ( verts ) {
        for ( int i = 0; i < 3; i++ ) {
            verts[i*2+0] = cmd->tri_verts[i][0];
            verts[i*2+1] = cmd->tri_verts[i][1];
        }
    }
    if ( uvs ) {
        for ( int i = 0; i < 3; i++ ) {
            uvs[i*2+0] = cmd->tri_uvs[i][0];
            uvs[i*2+1] = cmd->tri_uvs[i][1];
        }
    }
    return 1;
}

const char *Godot_Renderer_GetShaderName( int handle )
{
    if ( handle < 0 || handle >= gr_numShaders ) return "";
    return gr_shaders[handle].name;
}

int Godot_Renderer_IsShaderNoMip( int handle )
{
    if ( handle < 0 || handle >= gr_numShaders ) return 0;
    return gr_shaders[handle].nomip;
}

int Godot_Renderer_GetShaderCount( void )
{
    return gr_numShaders;
}

/* Public shader registration — callable from MoHAARunner.cpp (C++ side).
 * Wraps the internal GR_RegisterShader so that Godot code can register
 * shader names into the shader table and receive a valid handle for use
 * with get_shader_texture().  Mirrors R_FindShader usage in the real
 * renderer (e.g. R_InitStaticModels registers each static model surface
 * shader via R_FindShader). */
int Godot_Renderer_RegisterShader( const char *name )
{
    return (int)GR_RegisterShader( name );
}

/* ── Invalidate shader dimension cache ──
 * Called after Godot_ShaderProps_Load() so that shaders whose dimensions
 * were cached with a 64×64 fallback (because shader props weren't loaded
 * yet at first query) get re-resolved on next access. */
void Godot_Renderer_InvalidateShaderDimCache( void )
{
    int i;
    for ( i = 0; i < GR_MAX_SHADERS; i++ ) {
        gr_shaderWidths[i]  = GR_DIM_UNKNOWN;
        gr_shaderHeights[i] = GR_DIM_UNKNOWN;
    }
}

void Godot_Renderer_GetVidSize( int *w, int *h )
{
    if ( w ) *w = stored_glconfig.vidWidth;
    if ( h ) *h = stored_glconfig.vidHeight;
}

/* Sync stored_glconfig resolution with the actual Godot viewport.
 * Called by MoHAARunner each frame so that GR_Transform2D's Y-flip
 * and update_ui_transform's scaling always match the real viewport.
 *
 * DEPRECATED: Do NOT call this per-frame. stored_glconfig must reflect
 * the engine's selected resolution (from r_mode), not the viewport.
 * Use Godot_Renderer_SetViewportSize() instead for viewport tracking.
 * Kept only for vid_restart windowed mode where glConfig must match
 * the window size. */
void Godot_Renderer_SyncVidSize( int w, int h )
{
    if ( w > 0 && h > 0 ) {
        stored_glconfig.vidWidth  = w;
        stored_glconfig.vidHeight = h;
        stored_glconfig.windowAspect = (float)w / (float)h;
    }
}

/* Set the actual Godot viewport size.  Called by MoHAARunner each frame.
 * This does NOT touch stored_glconfig (engine resolution). */
void Godot_Renderer_SetViewportSize( int w, int h )
{
    if ( w > 0 && h > 0 ) {
        gr_viewport_width  = w;
        gr_viewport_height = h;
    }
}

/* Read the actual Godot viewport size. */
void Godot_Renderer_GetViewportSize( int *w, int *h )
{
    if ( w ) *w = gr_viewport_width;
    if ( h ) *h = gr_viewport_height;
}

void Godot_Renderer_Get2DWindow( float *left, float *right,
                                  float *top, float *bottom,
                                  int *vp_x, int *vp_y,
                                  int *vp_w, int *vp_h )
{
    if ( left )   *left   = gr_2d_left;
    if ( right )  *right  = gr_2d_right;
    if ( top )    *top    = gr_2d_top;
    if ( bottom ) *bottom = gr_2d_bottom;
    if ( vp_x )   *vp_x   = gr_2d_vp_x;
    if ( vp_y )   *vp_y   = gr_2d_vp_y;
    if ( vp_w )   *vp_w   = gr_2d_vp_w;
    if ( vp_h )   *vp_h   = gr_2d_vp_h;
}

/* ===================================================================
 * Skeletal model accessors
 *
 *  Expose the dtiki_t pointer and model type so the skeletal model
 *  module (godot_skel_model_accessors.cpp) can extract mesh data.
 * ================================================================ */

/* Return the dtiki_t pointer for a TIKI model handle, or NULL. */
void *Godot_Model_GetTikiPtr( int hModel )
{
    if ( hModel < 1 || hModel >= gr_numModels ) return NULL;
    if ( gr_models[hModel].type != GR_MOD_TIKI ) return NULL;
    return (void *)gr_models[hModel].tiki;
}

/* Return model type: 0=bad, 1=brush, 2=tiki, 3=sprite */
int Godot_Model_GetType( int hModel )
{
    if ( hModel < 1 || hModel >= gr_numModels ) return 0;
    return (int)gr_models[hModel].type;
}

/* Return the model name string for a model handle, or NULL. */
const char *Godot_Model_GetName( int hModel )
{
    if ( hModel < 0 || hModel >= gr_numModels ) return NULL;
    return gr_models[hModel].name;
}

/* Return the shader handle associated with a model (GR_MOD_SPRITE stores
 * a shader handle at registration time).  Returns 0 if not found. */
int Godot_Model_GetShaderHandle( int hModel )
{
    if ( hModel < 1 || hModel >= gr_numModels ) return 0;
    return gr_models[hModel].shaderHandle;
}

/* Return the real tr.models[] handle for engine pipeline capture.
 * Currently populated for GR_MOD_SPRITE only (via Godot_RealModel_RegisterSprite).
 * Returns 0 if no real handle was registered. */
int Godot_Model_GetRealHandle( int hModel )
{
    if ( hModel < 1 || hModel >= gr_numModels ) return 0;
    return gr_models[hModel].realModelHandle;
}

/* Register a model by name (TIKI, brush, or sprite) and return its handle.
 * Used by the BSP loader to register static model TIKIs. */
int Godot_Model_Register( const char *name )
{
    return (int)GR_RegisterModelInternal( name, qfalse, qtrue );
}

/* ===================================================================
 * Cinematic accessors
 *
 *  Allow MoHAARunner to read decoded RoQ frames for display.
 * ================================================================ */

/* Cross-platform symbol visibility macro for accessor functions */
#ifdef _MSC_VER
#  define GODOT_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define GODOT_EXPORT __attribute__((visibility("default")))
#else
#  define GODOT_EXPORT
#endif

GODOT_EXPORT
int Godot_Renderer_IsCinematicActive( void )
{
    return gr_cin_active;
}

GODOT_EXPORT
int Godot_Renderer_GetCinematicFrame( const byte **out_data,
                                      int *out_width, int *out_height )
{
    if ( !gr_cin_dirty || !gr_cin_active ) return 0;
    if ( out_data )   *out_data   = gr_cin_buffer;
    if ( out_width )  *out_width  = gr_cin_width;
    if ( out_height ) *out_height = gr_cin_height;
    gr_cin_dirty = 0;
    return 1;
}

GODOT_EXPORT
void Godot_Renderer_SetCinematicInactive( void )
{
    gr_cin_active = 0;
    gr_cin_dirty  = 0;
}

/* ===================================================================
 * Skeletal animation ri wrappers
 *
 *  Thin C functions that forward to engine ri.* callbacks.
 *  Called from godot_skel_model_accessors.cpp (C++) which cannot
 *  include renderer headers due to C++/C conflicts.
 * ================================================================ */

void *Godot_RI_GetSkeletor( void *tiki, int entityNumber )
{
    if ( !tiki ) return NULL;
    return ri.TIKI_GetSkeletor( (dtiki_t *)tiki, entityNumber );
}

void Godot_RI_SetPoseInternal( void *skeletor, const frameInfo_t *frameInfo,
                                const int *bone_tag, const vec4_t *bone_quat,
                                float actionWeight )
{
    if ( !skeletor ) return;
    ri.TIKI_SetPoseInternal( skeletor, frameInfo, bone_tag, bone_quat, actionWeight );
}

void Godot_RI_GetFrameInternal( void *tiki, int entityNumber, void *newFrame )
{
    if ( !tiki || !newFrame ) return;
    ri.GetFrameInternal( (dtiki_t *)tiki, entityNumber, (skelAnimFrame_t *)newFrame );
}

int Godot_RI_GetNumChannels( void *tiki )
{
    if ( !tiki ) return 0;
    return ri.TIKI_GetNumChannels( (dtiki_t *)tiki );
}

int Godot_RI_GetLocalChannel( void *tiki, int globalChannel )
{
    if ( !tiki ) return -1;
    return ri.TIKI_GetLocalChannel( (dtiki_t *)tiki, globalChannel );
}

/* ── Morph weight frame accessor ──
 * Wraps ri.SKEL_GetMorphWeightFrame so the skel model accessor can
 * retrieve morph target weights without including skeletor headers. */
int Godot_RI_GetMorphWeightFrame( void *skeletor, int index, float time, int *data )
{
    if ( !skeletor || !data ) return 0;
    if ( !ri.SKEL_GetMorphWeightFrame ) return 0;
    return ri.SKEL_GetMorphWeightFrame( skeletor, index, time, data );
}

/* ── Bind-pose bone computation for static models ──
 * Wraps ri.TIKI_GetSkelAnimFrame so the skel model accessor can
 * compute pStaticXyz on-the-fly when the GL renderer hasn't done it. */
int Godot_RI_GetSkelAnimFrame( void *tiki, void *bonesOut, float *radiusOut )
{
    if ( !tiki || !bonesOut ) return 0;
    if ( !ri.TIKI_GetSkelAnimFrame ) return 0;
    vec3_t mins, maxs;
    float  radius = 0.0f;
    ri.TIKI_GetSkelAnimFrame( (dtiki_t *)tiki, (skelBoneCache_t *)bonesOut,
                               &radius, &mins, &maxs );
    if ( radiusOut ) *radiusOut = radius;
    return 1;
}

/* ── Entity animation data accessor ── */
int Godot_Renderer_GetEntityAnim( int index,
                                   void **outTiki,
                                   int *outEntityNumber,
                                   void *outFrameInfo,     /* frameInfo_t[MAX_FRAMEINFOS] */
                                   int *outBoneTag,        /* int[5] */
                                   float *outBoneQuat,     /* float[5][4] */
                                   float *outActionWeight,
                                   float *outScale )
{
    if ( index < 0 || index >= gr_numEntities ) return 0;
    const gr_entity_t *ge = &gr_entities[index];
    if ( !ge->tiki ) return 0;

    if ( outTiki )         *outTiki         = ge->tiki;
    if ( outEntityNumber ) *outEntityNumber = ge->entityNumber;
    if ( outFrameInfo )    memcpy( outFrameInfo, ge->frameInfo, sizeof(ge->frameInfo) );
    if ( outBoneTag )      memcpy( outBoneTag, ge->bone_tag, sizeof(ge->bone_tag) );
    if ( outBoneQuat )     memcpy( outBoneQuat, ge->bone_quat, sizeof(ge->bone_quat) );
    if ( outActionWeight ) *outActionWeight = ge->actionWeight;
    if ( outScale )        *outScale        = ge->scale;

    return 1;
}

/* ===================================================================
 * HUD model render request accessors
 *
 *  Give MoHAARunner.cpp read access to the HUD model render requests
 *  captured from CL_Draw3DModel() calls during UI rendering.
 * ================================================================ */

int Godot_Renderer_GetHudModelCount( void )
{
    return gr_numHudModels;
}

int Godot_Renderer_GetHudModel( int index,
                                float *origin,      /* [3] */
                                float *axis,        /* [9] */
                                float *out_scale,   /* [1] */
                                int   *hModel,      /* [1] */
                                unsigned char *rgba, /* [4] */
                                void **tiki,        /* [1] */
                                float *rect,        /* [4] x,y,w,h */
                                float *vieworg,     /* [3] */
                                float *viewaxis,    /* [9] */
                                float *fov )        /* [2] fov_x, fov_y */
{
    if ( index < 0 || index >= gr_numHudModels ) return 0;

    const gr_hud_model_t *hm = &gr_hudModels[index];
    const gr_entity_t *ge = &hm->entity;

    if ( origin ) {
        origin[0] = ge->origin[0];
        origin[1] = ge->origin[1];
        origin[2] = ge->origin[2];
    }
    if ( axis ) {
        memcpy( axis, ge->axis, 9 * sizeof(float) );
    }
    if ( out_scale )  *out_scale = ge->scale;
    if ( hModel )     *hModel    = ge->hModel;
    if ( rgba ) {
        rgba[0] = ge->shaderRGBA[0];
        rgba[1] = ge->shaderRGBA[1];
        rgba[2] = ge->shaderRGBA[2];
        rgba[3] = ge->shaderRGBA[3];
    }
    if ( tiki )       *tiki = ge->tiki;
    if ( rect ) {
        rect[0] = hm->rect_x;
        rect[1] = hm->rect_y;
        rect[2] = hm->rect_w;
        rect[3] = hm->rect_h;
    }
    if ( vieworg ) {
        vieworg[0] = hm->vieworg[0];
        vieworg[1] = hm->vieworg[1];
        vieworg[2] = hm->vieworg[2];
    }
    if ( viewaxis ) {
        memcpy( viewaxis, hm->viewaxis, 9 * sizeof(float) );
    }
    if ( fov ) {
        fov[0] = hm->fov_x;
        fov[1] = hm->fov_y;
    }

    return 1;
}

/* Return the 2D command index at which a HUD model was submitted.
 * This tells the Godot side where in the 2D draw stream to inject
 * the viewport texture so it appears at the correct z-position
 * (between background fills and foreground widgets like dropdowns). */
int Godot_Renderer_GetHudModelDrawOrder( int index )
{
    if ( index < 0 || index >= gr_numHudModels ) return 0;
    return gr_hudModels[index].draw_order;
}

/* Vid_restart state — consumed by MoHAARunner after Com_Frame().
 * Returns 1 if a vid_restart happened this frame, and fills output params.
 * Resets the flag so it only fires once. */
int Godot_Renderer_ConsumeVidRestart( int *out_fullscreen, int *out_width, int *out_height )
{
    if ( !gr_vidRestarted ) return 0;
    gr_vidRestarted = 0;
    if ( out_fullscreen ) *out_fullscreen = gr_vidRestart_fullscreen;
    if ( out_width )      *out_width      = gr_vidRestart_width;
    if ( out_height )     *out_height     = gr_vidRestart_height;
    return 1;
}

/* Set desktop resolution from Godot (called by MoHAARunner before Com_Init
 * and before vid_restart so r_mode -2 resolves correctly). */
void Godot_Renderer_SetDesktopResolution( int w, int h )
{
    if ( w > 0 && h > 0 ) {
        gr_desktopWidth  = w;
        gr_desktopHeight = h;
    }
}

/* Set the physical screen/monitor resolution.  Called by MoHAARunner at
 * startup.  GR_BeginRegistration uses this to adjust the engine width for
 * widescreen aspect ratio when fullscreen is active. */
void Godot_Renderer_SetScreenResolution( int w, int h )
{
    if ( w > 0 && h > 0 ) {
        gr_screenWidth  = w;
        gr_screenHeight = h;
    }
}

/* Mode table query for UI hooks and external code. */
int Godot_Renderer_GetNumVidModes( void )
{
    return gr_numVidModes;
}

int Godot_Renderer_GetVidMode( int mode, int *w, int *h )
{
    if ( mode < 0 || mode >= gr_numVidModes ) return 0;
    if ( w ) *w = gr_vidModes[mode].width;
    if ( h ) *h = gr_vidModes[mode].height;
    return 1;
}

/* Return animation data for a HUD model entity */
int Godot_Renderer_GetHudModelAnim( int index,
                                    void *outFrameInfo,     /* frameInfo_t[MAX_FRAMEINFOS] */
                                    int *outBoneTag,        /* [5] */
                                    float *outBoneQuat,     /* [5][4] = [20] */
                                    float *outActionWeight, /* [1] */
                                    float *outScale )       /* [1] */
{
    if ( index < 0 || index >= gr_numHudModels ) return 0;

    const gr_entity_t *ge = &gr_hudModels[index].entity;

    if ( outFrameInfo )    memcpy( outFrameInfo, ge->frameInfo, sizeof(ge->frameInfo) );
    if ( outBoneTag )      memcpy( outBoneTag, ge->bone_tag, sizeof(ge->bone_tag) );
    if ( outBoneQuat )     memcpy( outBoneQuat, ge->bone_quat, sizeof(ge->bone_quat) );
    if ( outActionWeight ) *outActionWeight = ge->actionWeight;
    if ( outScale )        *outScale        = ge->scale;

    return 1;
}

/* ===================================================================
 *  GetRefAPI — the only exported entry point
 *
 *  Called by CL_InitRef() in cl_main.cpp.  We populate a static
 *  refexport_t with our Godot-backend function pointers.
 * ================================================================ */

refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp )
{
    static refexport_t re;

    ri = *rimp;

    if ( apiVersion != REF_API_VERSION ) {
        ri.Printf( PRINT_ALL,
            "[GodotRenderer] Mismatched REF_API_VERSION: expected %i, got %i\n",
            REF_API_VERSION, apiVersion );
        return NULL;
    }

    ri.Printf( PRINT_ALL, "[GodotRenderer] Initialising Godot renderer backend\n" );

    memset( &re, 0, sizeof( re ) );

    /* Lifecycle */
    re.Shutdown            = GR_Shutdown;
    re.BeginRegistration   = GR_BeginRegistration;
    re.EndRegistration     = GR_EndRegistration;

    /* Asset registration */
    re.RegisterModel       = GR_RegisterModel;
    re.RegisterSkin        = GR_RegisterSkin;
    re.RegisterShader      = GR_RegisterShader;
    re.RegisterShaderNoMip = GR_RegisterShaderNoMip;
    re.RefreshShaderNoMip  = GR_RefreshShaderNoMip;
    re.SpawnEffectModel    = GR_SpawnEffectModel;
    re.RegisterServerModel = GR_RegisterServerModel;
    re.UnregisterServerModel = GR_UnregisterServerModel;
    re.RegisterFont        = GR_RegisterFont;
    re.LoadFont            = GR_LoadFont;

    /* World */
    re.LoadWorld           = GR_LoadWorld;
    re.SetWorldVisData     = GR_SetWorldVisData;

    /* Scene building */
    re.ClearScene          = GR_ClearScene;
    re.AddRefEntityToScene = GR_AddRefEntityToScene;
    re.AddPolyToScene      = GR_AddPolyToScene;
    re.LightForPoint       = GR_LightForPoint;
    re.AddLightToScene     = GR_AddLightToScene;
    re.AddAdditiveLightToScene = GR_AddAdditiveLightToScene;
    re.RenderScene         = GR_RenderScene;
    re.AddRefSpriteToScene = GR_AddRefSpriteToScene;
    re.AddTerrainMarkToScene = GR_AddTerrainMarkToScene;

    /* Frame management */
    re.BeginFrame          = GR_BeginFrame;
    re.EndFrame            = GR_EndFrame;

    /* 2D drawing */
    re.SetColor            = GR_SetColor;
    re.DrawStretchPic      = GR_DrawStretchPic;
    re.DrawStretchPic2     = GR_DrawStretchPic2;
    re.DrawStretchRaw      = GR_DrawStretchRaw;
    re.UploadCinematic     = GR_UploadCinematic;
    re.DrawTilePic         = GR_DrawTilePic;
    re.DrawTilePicOffset   = GR_DrawTilePicOffset;
    re.DrawTrianglePic     = GR_DrawTrianglePic;
    re.DrawBackground      = GR_DrawBackground;
    re.DrawBox             = GR_DrawBox;
    re.AddBox              = GR_AddBox;
    re.Set2DWindow         = GR_Set2DWindow;
    re.Scissor             = GR_Scissor;
    re.DrawLineLoop        = GR_DrawLineLoop;
    re.DebugLine           = GR_DebugLine;

    /* Fonts / text */
    re.DrawString          = GR_DrawString;
    re.GetFontHeight       = GR_GetFontHeight;
    re.GetFontStringWidth  = GR_GetFontStringWidth;

    /* Marks / fragments */
    re.MarkFragments       = GR_MarkFragments;
    re.MarkFragmentsForInlineModel = GR_MarkFragmentsForInlineModel;

    /* Model queries */
    re.LerpTag             = GR_LerpTag;
    re.ModelBounds         = GR_ModelBounds;
    re.ModelRadius         = GR_ModelRadius;
    re.R_Model_GetHandle   = GR_Model_GetHandle;
    re.GetRenderEntity     = GR_GetRenderEntity;

    /* Shader queries */
    re.GetShaderWidth      = GR_GetShaderWidth;
    re.GetShaderHeight     = GR_GetShaderHeight;
    re.GetShaderName       = GR_GetShaderName;
    re.GetModelName        = GR_GetModelName;
    re.ImageExists         = GR_ImageExists;
    re.CountTextureMemory  = GR_CountTextureMemory;

    /* Misc */
    re.RemapShader         = GR_RemapShader;
    re.GetEntityToken      = GR_GetEntityToken;
    re.inPVS               = GR_inPVS;
    re.TakeVideoFrame      = GR_TakeVideoFrame;
    re.FreeModels          = GR_FreeModels;
    re.PrintBSPFileSizes   = GR_PrintBSPFileSizes;
    re.MapVersion          = GR_MapVersion;
    re.GetGraphicsInfo     = GR_GetGraphicsInfo;
    re.ForceUpdatePose     = GR_ForceUpdatePose;
    re.TIKI_Orientation    = GR_TIKI_Orientation;
    re.TIKI_IsOnGround     = GR_TIKI_IsOnGround;
    re.SetFrameNumber      = GR_SetFrameNumber;
    re.SavePerformanceCounters = GR_SavePerformanceCounters;
    re.GetInlineModelBounds = GR_GetInlineModelBounds;
    re.GetLightingForDecal = GR_GetLightingForDecal;
    re.GetLightingForSmoke = GR_GetLightingForSmoke;
    re.R_GatherLightSources = GR_GatherLightSources;

    /* Swipe effects */
    re.SwipeBegin          = GR_SwipeBegin;
    re.SwipePoint          = GR_SwipePoint;
    re.SwipeEnd            = GR_SwipeEnd;

    /* Mode / fullscreen */
    re.SetMode             = GR_SetMode;
    re.SetFullscreen       = GR_SetFullscreen;
    re.SetRenderTime       = GR_SetRenderTime;
    re.Noise               = GR_Noise;

    /* Raw image loading */
    re.LoadRawImage        = GR_LoadRawImage;
    re.FreeRawImage        = GR_FreeRawImage;
    re.Set2DInitialShaderTime = GR_Set2DInitialShaderTime;

#ifdef GODOT_GDEXTENSION
    re.AddMuzzleFlash      = GR_AddMuzzleFlash;
    re.AddShellCasing      = GR_AddShellCasing;
#endif

 /* Register ter_restart command — the original renderer registers this
     * in tr_terrain.c which we don't compile. The menu's CheckRestart() issues
     * "ter_restart" when CVAR_TERRAIN_LATCH cvars change. We handle it as a no-op
     * since terrain quality changes apply automatically on next map load. */
    ri.Cmd_AddCommand( "ter_restart", GR_TerrainRestart_f );

    return &re;
}

void Godot_Renderer_CM_BoxTrace(void *results, const float *start, const float *end, const float *mins, const float *maxs, int model, int brushMask, int cylinder) {
    if (ri.CM_BoxTrace) {
        ri.CM_BoxTrace((trace_t *)results, start, end, mins, maxs, model, brushMask, cylinder);
    }
}

