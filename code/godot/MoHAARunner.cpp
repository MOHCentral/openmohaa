#include "MoHAARunner.h"
#include "godot_bsp_mesh.h"
#include "godot_skel_model.h"
#include "godot_shader_props.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/cubemap.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_effect_reverb.hpp>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <setjmp.h>

// From register_types.cpp — track engine lifecycle across module boundary
extern void Godot_SetEngineInitialized(bool v);

extern "C" {
    // Core engine entry points
    void Com_Init(char *commandLine);
    void Com_Frame(void);
    void Com_Shutdown(void);
    void Z_MarkShutdown(void);

    // Pre-init steps from sys_main.c / sys_unix.c that main() normally calls
    void Sys_PlatformInit(void);
    void Sys_SetBinaryPath(const char *path);
    void Sys_SetDefaultInstallPath(const char *path);
    void CON_Init(void);
    void NET_Init(void);
    int  Sys_Milliseconds(void);

    // Command buffer
    void Cbuf_AddText(const char *text);
    void Cbuf_ExecuteText(int exec_when, const char *text);

    // Console variable access
    float Cvar_VariableValue(const char *var_name);
    int   Cvar_VariableIntegerValue(const char *var_name);
    void  Cvar_VariableStringBuffer(const char *var_name, char *buffer, int bufsize);

    // Server state — from server.h (we only read these, never write)
    // serverState_t enum: SS_DEAD=0, SS_LOADING=1, SS_LOADING2=2, SS_GAME=3
    typedef struct {
        int state;   // serverState_t — first field of server_t
        // rest of struct omitted; we only need 'state'
    } server_t_partial;

    // serverStatic_t — we access mapName and iNumClients
    // These are at known offsets in the struct. To avoid depending on
    // the full struct layout, we declare extern accessor functions instead.
    int  Godot_GetServerState(void);
    const char *Godot_GetMapName(void);
    int  Godot_GetPlayerCount(void);
    int  Godot_GetMaxClients(void);
    int  Godot_GetScoreboardPlayer(int i,
                                   char *out_name, int out_name_len,
                                   int *out_kills, int *out_deaths,
                                   int *out_ping);

    // Engine image loader — from renderergl1/tr_image.c
    // Uses R_LoadImage internally: tries JPG before TGA, handles RLE,
    // correct BGRA→RGBA conversion, all MOHAA TGA variants.
    int  R_LoadRawImage(const char *name, unsigned char **pic, int *width, int *height);
    void R_FreeRawImage(unsigned char *pic);

    // VFS accessors (Task 4.1) — from godot_vfs_accessors.c
    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer);
    long Godot_VFS_FileOpenRead(const char *qpath, int *out_handle);
    long Godot_VFS_FileRead(int handle, void *buffer, long len);
    void Godot_VFS_FileClose(int handle);
    void Godot_VFS_FreeFile(void *buffer);
    int  Godot_VFS_FileExists(const char *qpath);
    char **Godot_VFS_ListFiles(const char *directory, const char *extension, int *out_count);
    void Godot_VFS_FreeFileList(char **list);
    const char *Godot_VFS_GetGamedir(void);

    // Input bridge (Phase 6) — from godot_input_bridge.c
    int  Godot_InjectKeyEvent(int godot_key, int down);
    void Godot_InjectCharEvent(int unicode);
    void Godot_InjectMouseMotion(int dx, int dy);
    void Godot_InjectMouseButton(int godot_button, int down);
    void Godot_InjectMousePosition(int x, int y);
    void Godot_ResetMousePosition(void);

    // Renderer / camera bridge (Phase 7a) — from godot_renderer.c
    int  Godot_Renderer_HasNewFrame(void);
    void Godot_Renderer_ClearNewFrame(void);
    int  Godot_Renderer_GetFrameCount(void);
    void Godot_Renderer_GetViewOrigin(float *out);
    void Godot_Renderer_GetViewAxis(float *out);
    void Godot_Renderer_GetFov(float *fov_x, float *fov_y);
    void Godot_Renderer_GetRenderSize(int *w, int *h);
    void Godot_Renderer_GetFarplane(float *distance, float *bias, float *color, int *cull);
    const char *Godot_Renderer_GetWorldMapName(void);
    int  Godot_Renderer_IsWorldMapLoaded(void);

    // Phase 39 parity: additional refdef_t and entity field accessors
    int  Godot_Renderer_GetRefdefTime(void);
    int  Godot_Renderer_GetRefdefFlags(void);
    void Godot_Renderer_GetAreamask(unsigned char *out, int maxBytes);
    int  Godot_Renderer_GetSkyPortal(float *origin, float *axis, float *alpha);
    int  Godot_Renderer_GetRenderTerrain(void);
    int  Godot_Renderer_GetSkyboxFarplane(void);
    void Godot_Renderer_GetFarplaneOverrides(float *farclip, float *colorOverride);
    void Godot_Renderer_GetEntityShaderData(int index, float *shaderTime,
                                            float *shaderTexCoord, int *customSkin,
                                            int *nonNormalizedAxes);

    // Terrain visibility — from tr_world.c and tr_terrain.c
    void R_MarkTerrainForGodot(const float vieworg[3], const float viewaxis[3][3],
                                float fov_x, float fov_y, float farplane_dist);
    void Godot_Terrain_TessellateForGodot(void);
    int  Godot_Terrain_IsPatchMarked(int patchIdx);
    int  Godot_Terrain_GetRendererPatchCount(void);

    // Entity/light bridge (Phase 7e) — from godot_renderer.c
    int  Godot_Renderer_GetEntityCount(void);
    int  Godot_Renderer_GetEntity(int index,
                                  float *origin, float *axis, float *out_scale,
                                  int *hModel, int *entityNumber,
                                  unsigned char *rgba, int *renderfx);
    void Godot_Renderer_GetEntityBeam(int index,
                                      float *from, float *to, float *diameter);
    int  Godot_Renderer_GetDlightCount(void);
    void Godot_Renderer_GetDlight(int index,
                                  float *origin, float *intensity,
                                  float *r, float *g, float *b, int *type);

    // Poly bridge (Phase 16) — from godot_renderer.c
    int  Godot_Renderer_GetPolyCount(void);
    int  Godot_Renderer_GetPoly(int index, int *hShader,
                                float *positions, float *texcoords,
                                unsigned char *colors, int maxVerts);

    // Sprite entity data (Phase 16) — from godot_renderer.c
    void Godot_Renderer_GetEntitySprite(int index, float *radius,
                                        float *rotation, int *customShader);

    // 2D overlay bridge (Phase 7h) — from godot_renderer.c
    int  Godot_Renderer_Get2DCmdCount(void);
    int  Godot_Renderer_Get2DCmd(int index,
                                 int *type,
                                 float *x, float *y, float *w, float *h,
                                 float *s1, float *t1, float *s2, float *t2,
                                 float *color, int *shader);
    int  Godot_Renderer_Get2DCmdTriVerts(int index,
                                         float *verts,  /* [3][2] */
                                         float *uvs);   /* [3][2] */
    const char *Godot_Renderer_GetShaderName(int handle);
    int  Godot_Renderer_IsShaderNoMip(int handle);
    int  Godot_Renderer_GetShaderCount(void);
    int  Godot_Renderer_RegisterShader(const char *name);
    void Godot_Renderer_GetVidSize(int *w, int *h);
    /* Phase 52 remap uses Godot_Renderer_GetShaderRemap declared below */

    // Sound bridge (Phase 8) — from godot_sound.c
    int  Godot_Sound_GetSfxCount(void);
    const char *Godot_Sound_GetSfxName(int index);
    int  Godot_Sound_GetSfxHandle(int index);
    int  Godot_Sound_FindSfxIndex(int handle);
    int  Godot_Sound_GetEventCount(void);
    int  Godot_Sound_GetEvent(int index, float *origin, int *entnum,
                              int *channel, int *sfxHandle, float *volume,
                              float *minDist, float *maxDist, float *pitch,
                              int *streamed, char *nameOut, int nameLen);
    void Godot_Sound_ClearEvents(void);
    int  Godot_Sound_GetLoopCount(void);
    void Godot_Sound_GetLoop(int index, float *origin, float *velocity,
                             int *sfxHandle, float *volume, float *minDist,
                             float *maxDist, float *pitch, int *flags);
    void Godot_Sound_GetListener(float *origin, float *axis, int *entnum);
    int  Godot_Sound_GetMusicAction(void);
    const char *Godot_Sound_GetMusicName(void);
    float Godot_Sound_GetMusicVolume(void);
    float Godot_Sound_GetMusicFadeTime(void);
    void Godot_Sound_ClearMusicAction(void);

    // Phase 49-51: New sound accessors
    int   Godot_Sound_GetEntityPosition(int entnum, float *origin, float *velocity);
    float Godot_Sound_GetFadeTime(void);
    float Godot_Sound_GetFadeTarget(void);
    int   Godot_Sound_GetFadeActive(void);
    void  Godot_Sound_ClearFade(void);
    int   Godot_Sound_GetReverbType(void);
    float Godot_Sound_GetReverbLevel(void);
    int   Godot_Sound_GetPlayingCount(void);
    int   Godot_Sound_GetPlaying(int index, int *channel, int *sfxHandle,
                                  char *name, int nameLen);
    void  Godot_Sound_MarkStopped(int channel);
    int   Godot_Sound_GetMusicMood(int *current, int *fallback);
    int   Godot_Sound_GetTriggeredAction(void);
    const char *Godot_Sound_GetTriggeredName(void);
    int   Godot_Sound_GetTriggeredLoopCount(void);
    int   Godot_Sound_GetTriggeredOffset(void);
    void  Godot_Sound_ClearTriggeredAction(void);
    int   Godot_Sound_FindSfxIndex(int handle);

    // Sound occlusion (Phase 48) — from godot_sound_occlusion.c
    float Godot_SoundOcclusion_Check(float listener_x, float listener_y,
                                     float listener_z,
                                     float origin_x, float origin_y,
                                     float origin_z);

    // Phase 5: Ambient volume and clear buffer
    float Godot_Sound_GetAmbientVolume(void);
    int   Godot_Sound_GetClearRequested(void);
    void  Godot_Sound_ClearClearRequest(void);
    void  Godot_SoundOcclusion_SetEnabled(int enabled);
    int   Godot_SoundOcclusion_IsEnabled(void);

    // Model bridge (Phase 9) — from godot_renderer.c + godot_skel_model_accessors.cpp
    void *Godot_Model_GetTikiPtr(int hModel);
    int   Godot_Model_GetType(int hModel);
    int   Godot_Model_Register(const char *name);
    const char *Godot_Model_GetName(int hModel);

    // Client diagnostics (Phase 6 debug) — from godot_client_accessors.cpp
    int  Godot_Client_GetState(void);
    int  Godot_Client_GetKeyCatchers(void);
    int  Godot_Client_GetGuiMouse(void);
    int  Godot_Client_GetStartStage(void);
    void Godot_Client_GetMousePos(int *mx, int *my);
    void Godot_Client_SetGameInputMode(void);
    void Godot_Client_SetKeyCatchers(int catchers);
    int  Godot_Client_GetPaused(void);
    void Godot_Client_ForceUnpause(void);
    int  Godot_Client_IsAnyOverlayActive(void);
    void Godot_Client_SyncGuiMouseToOverlayState(void);
    void Godot_Client_SetMousePos(int x, int y);
    int  Godot_Client_IsUIStarted(void);
    int  Godot_Client_IsMenuUp(void);
    const char *Godot_Client_GetKeyBinding(int keynum);
    int  Godot_Client_GetPlayerZoom(void);
    int  Godot_Client_GetMouseButtons(void);
    void Godot_Client_DumpInputState(void);
    int  Godot_Client_GetPlayerHealth(void);
    void Godot_Client_GetScreenBlend(float *r, float *g, float *b, float *a);

    // Save/load bridge — from godot_save_accessors.c
    void Godot_Save_QuickSave(void);
    void Godot_Save_QuickLoad(void);
    void Godot_Save_SaveToSlot(int slot);
    void Godot_Save_LoadFromSlot(int slot);
    int  Godot_Save_SlotExists(int slot);

    // Cinematic bridge (Phase 11) — from godot_renderer.c
    int  Godot_Renderer_IsCinematicActive(void);
    int  Godot_Renderer_GetCinematicFrame(const unsigned char **out_data,
                                          int *out_width, int *out_height);
    void Godot_Renderer_SetCinematicInactive(void);

    // Skeletal animation bridge (Phase 13) — from godot_renderer.c + godot_skel_model_accessors.cpp
    int   Godot_Renderer_GetEntityAnim(int index,
                                        void **outTiki, int *outEntityNumber,
                                        void *outFrameInfo, int *outBoneTag,
                                        float *outBoneQuat, float *outActionWeight,
                                        float *outScale);
    void *Godot_Skel_PrepareBones(void *tikiPtr, int entityNumber,
                                   const void *frameInfo, const int *bone_tag,
                                   const float *bone_quat, float actionWeight,
                                   int *outBoneCount,
                                   int **outMorphCache, int *outMorphCount);
    int   Godot_Skel_SkinSurface(void *tikiPtr, int meshIndex, int surfIndex,
                                  const void *boneCache, int boneCount,
                                  float *outPositions, float *outNormals,
                                  int maxVerts,
                                  const int *morphCache, int morphCount);
    int   Godot_Skel_GetMeshCount(void *tikiPtr);
    float Godot_Skel_GetScale(void *tikiPtr);
    void  Godot_Skel_GetOrigin(void *tikiPtr, float *out);
    int   Godot_Skel_GetSurfaceCount(void *tikiPtr, int meshIndex);
    int   Godot_Skel_GetSurfaceInfo(void *tikiPtr, int meshIndex, int surfIndex,
                                     int *numVerts, int *numTriangles,
                                     char *surfName, int surfNameLen,
                                     char *shaderName, int shaderNameLen);

    int Godot_Cvar_VariableIntegerValue(const char *var_name);
    // Skin-aware shader name lookup: mirrors tr_model.cpp hShader[skinNum + (bsurf & 3)] selection.
    int   Godot_Skel_GetSurfaceShaderForSkin(void *tikiPtr, int meshIndex, int surfIndex,
                                              int iShaderNum,
                                              char *shaderName, int shaderNameLen);
    int   Godot_Skel_GetSurfaceVertices(void *tikiPtr, int meshIndex, int surfIndex,
                                         float *positions, float *normals, float *texcoords);
    int   Godot_Skel_GetSurfaceIndices(void *tikiPtr, int meshIndex, int surfIndex,
                                        int *indices);

    // Phase 35: Entity parenting — from godot_renderer.c
    int   Godot_Renderer_GetEntityParent(int index);

    // Phase 268: Entity lighting origin — from godot_renderer.c
    void  Godot_Renderer_GetEntityLightingOrigin(int index, float *out);

    // Per-surface state flags and skin slot — from godot_renderer.c
    // Mirrors refEntity_t::surfaces[] / skinNum used in tr_model.cpp::R_AddSkelSurfaces.
    void  Godot_Renderer_GetEntitySurfaces(int index,
                                           unsigned char *out_surfaces,
                                           int *out_skinNum);

    // Model/Shadow accessors
    float Godot_Renderer_GetEntityShadowPlane(int index);
    float Godot_Model_GetRadius(int hModel);
    int   Godot_Model_GetSpriteShader(int hModel);
    int   Godot_Model_GetSpriteDims(int hModel, float *outWidth,
                                     float *outHeight, float *outSpriteScale);

    // Render commands and polygons
    // Phase 148: HUD model render request accessors — from godot_renderer.c
    int   Godot_Renderer_GetHudModelCount(void);
    int   Godot_Renderer_GetHudModel(int index,
                                     float *origin, float *axis, float *out_scale,
                                     int *hModel, unsigned char *rgba, void **tiki,
                                     float *rect, float *vieworg, float *viewaxis,
                                     float *fov);
    int   Godot_Renderer_GetHudModelAnim(int index,
                                         void *outFrameInfo, int *outBoneTag,
                                         float *outBoneQuat, float *outActionWeight,
                                         float *outScale);
    int   Godot_Renderer_GetHudModelDrawOrder(int index);

    // Phase 149: Vid_restart detection + settings accessors
    int   Godot_Renderer_ConsumeVidRestart(int *out_fullscreen, int *out_width, int *out_height);
    void  Godot_Renderer_SetDesktopResolution(int w, int h);
    void  Godot_Renderer_SyncVidSize(int w, int h);

    // Client accessor: sync cls.glconfig with actual Godot viewport
    void  Godot_Client_SyncGlConfigVidSize(int w, int h);
    void  Godot_Client_RefreshUIDef(void);
    void  Godot_Client_ResolutionChange(void);

    // Cvar event bridge (event-driven fullscreen updates)
    int   Godot_ConsumeFullscreenCvarChanged(int *out_fullscreen);

    // Phase 26: Shader remap query — from godot_renderer.c
    const char *Godot_Renderer_GetShaderRemap(const char *shaderName);

    // Phase 24: Swipe effect accessor — from godot_renderer.c
    int   Godot_Renderer_GetSwipeData(float *thisTime, float *life,
                                      int *hShader, int *numPoints);
    void  Godot_Renderer_GetSwipePoint(int index, float *point1, float *point2,
                                       float *time);

    // Phase 25: Terrain mark accessor — from godot_renderer.c
    int   Godot_Renderer_GetTerrainMarkCount(void);
    void  Godot_Renderer_GetTerrainMark(int index, int *hShader, int *numVerts,
                                        int *terrainIndex, int *renderfx);
    void  Godot_Renderer_GetTerrainMarkVert(int markIndex, int vertIndex,
                                            float *xyz, float *st,
                                            unsigned char *rgba);

    // Phase 32: Scissor state — from godot_renderer.c
    void  Godot_Renderer_GetScissor(int *x, int *y, int *width, int *height);

    // Phase 33: Background image accessor — from godot_renderer.c
    int   Godot_Renderer_GetBackground(int *cols, int *rows, int *bgr,
                                       const unsigned char **data);
    int   Godot_Renderer_GetBackgroundCmdIndex(void);

    // Phase 59: UI system — from godot_ui_system.cpp / godot_ui_input.cpp
    // Fallback declarations in case headers are absent:
#ifndef HAS_UI_SYSTEM_MODULE
    int   Godot_UI_Update(void);
    int   Godot_UI_IsActive(void);
    int   Godot_UI_IsMenuActive(void);
    int   Godot_UI_ShouldShowCursor(void);
    void  Godot_UI_OnMapLoad(void);
    int   Godot_UI_IsLoading(void);
#endif
#ifndef HAS_UI_INPUT_MODULE
    int   Godot_UI_HandleKeyEvent(int godot_key, int down);
    int   Godot_UI_HandleCharEvent(int unicode);
    int   Godot_UI_HandleMouseButton(int godot_button, int down);
    int   Godot_UI_HandleMouseMotion(int dx, int dy);
    int   Godot_UI_ShouldCaptureInput(void);
#endif

    // Phase 59: Mouse reset — from godot_input_bridge.c
    void  Godot_ResetMousePosition(void);

    // Cursor image accessor — from stubs.cpp
    int   Godot_GetPendingCursorImage(const unsigned char **out_pixels, int *out_w, int *out_h);
    void  Godot_ClearPendingCursorImage(void);

    // Scoreboard capture buffer — from godot_scoreboard.c
    int         Godot_SB_IsVisible(void);
    int         Godot_SB_GetItemCount(void);
    int         Godot_SB_GetColumnCount(void);
    const char *Godot_SB_GetItemString(int item, int field);
    void        Godot_SB_GetItemTextColor(int item, float *r, float *g, float *b, float *a);
    void        Godot_SB_GetItemBackColor(int item, float *r, float *g, float *b, float *a);
    int         Godot_SB_GetItemIsHeader(int item);
    const char *Godot_SB_GetColumnName(int col);
    int         Godot_SB_GetColumnWidth(int col);
    void        Godot_SB_GetPosition(float *x, float *y, float *w, float *h);
    void        Godot_SB_GetBGColor(float *r, float *g, float *b, float *a);
    void        Godot_SB_GetFontColor(float *r, float *g, float *b, float *a);
    int         Godot_SB_GetDrawHeader(void);
    const char *Godot_SB_GetMenuName(void);

    // Quake3 common parsing utilities (from qcommon/q_shared.c / q_parse.cpp)
    char *COM_Parse(char **data_p);
    char *COM_ParseExt(char **data_p, int allowLineBreak);
    int   Q_stricmp(const char *s1, const char *s2);

    // BSP entity string accessor — from godot_bsp_mesh.cpp
    const char *Godot_BSP_GetEntityString(void);

    // Game switch completed flag — from common.c (GODOT_GDEXTENSION)
    int  Godot_GetGameSwitchCompleted(void);
    void Godot_ClearGameSwitchCompleted(void);
}

// ──────────────────────────────────────────────
//  Error / quit interception (Task 2.5.2)
// ──────────────────────────────────────────────

// Jump buffer for recovering from Sys_Error / Sys_Quit under Godot
static jmp_buf godot_error_jmpbuf;
static bool    godot_jmpbuf_valid = false;

static bool    godot_has_fatal_error = false;
static char    godot_error_message[1024] = {0};
static bool    godot_quit_requested = false;

// Tracks which effective shader name was used for each cached texture entry.
// Keyed by shader handle.  Cleared alongside shader_textures on map/vid_restart.
static std::unordered_map<int, std::string> s_shader_texture_loaded_names;

// Per-surface animation cache for update_shader_animations: avoids per-frame
// string conversion and hash lookups.  Cleared on map change.
struct SurfAnimCache {
    const GodotShaderProps *sp;
    int shader_handle;
    bool needs_animation;
};
static std::unordered_map<uint32_t, SurfAnimCache> s_surf_anim_cache;

// Sprite/beam material caches: avoid per-frame material+texture creation.
// Cleared on map change (shader handles are re-registered).
static std::unordered_map<int, Ref<StandardMaterial3D>> s_sprite_mat_cache;
static std::unordered_map<int, Ref<StandardMaterial3D>> s_beam_mat_cache;

// Poly/terrain-mark material caches: keyed on (shader_handle << 2 | blend_type)
// where blend_type: 0=alpha, 1=inv_mul (ShaderMaterial), 2=multiplicative.
// Avoids per-frame material instantiation for effects that reuse the same shader.
static std::unordered_map<int64_t, Ref<Material>> s_poly_mat_cache;
static std::unordered_map<int64_t, Ref<Material>> s_terrain_mark_mat_cache;

// Sprite/beam per-instance tint cache: keyed on (shader_handle << 16 | rgba_quantized).
// Avoids duplicate() per sprite/beam per frame when tint is unchanged.
static std::unordered_map<uint64_t, Ref<StandardMaterial3D>> s_sprite_tint_cache;
static std::unordered_map<uint64_t, Ref<StandardMaterial3D>> s_beam_tint_cache;

// ALPHA_INV pre-composited texture cache: keyed on shader_handle.
// These textures have the two-stage ALPHA_INV blend baked in on the CPU
// in sRGB space, bypassing GPU sRGB decode/encode mismatches.
static std::unordered_map<int, Ref<ImageTexture>> s_alpha_inv_tex_cache;

static MoHAARunner* s_mohaa_runner_instance = nullptr;

godot::Ref<godot::ImageTexture> Godot_GetShaderTexture(int shader_handle) {
    if (s_mohaa_runner_instance) {
        return s_mohaa_runner_instance->get_shader_texture(shader_handle);
    }
    return godot::Ref<godot::ImageTexture>();
}

// Called from patched Sys_Error in sys_main.c
extern "C" void Godot_SysError(const char *error) {
    strncpy(godot_error_message, error, sizeof(godot_error_message) - 1);
    godot_error_message[sizeof(godot_error_message) - 1] = '\0';
    godot_has_fatal_error = true;

    if (godot_jmpbuf_valid) {
        longjmp(godot_error_jmpbuf, 1);
    }
    // If no jmpbuf is set up, we can't recover — log and hope for the best
    fprintf(stderr, "[MoHAA] FATAL ERROR (no recovery point): %s\n", error);
}

// Called from patched Sys_Quit in sys_main.c
extern "C" void Godot_SysQuit(void) {
    godot_quit_requested = true;

    if (godot_jmpbuf_valid) {
        longjmp(godot_error_jmpbuf, 2);
    }
    fprintf(stderr, "[MoHAA] Quit requested (no recovery point)\n");
}

// ──────────────────────────────────────────────
//  Godot print redirect
// ──────────────────────────────────────────────

static bool g_godot_ready = false;

extern "C" void Godot_SysPrint(const char *msg) {
    if (g_godot_ready) {
        // Strip trailing newline for cleaner Godot output
        godot::String s(msg);
        if (s.ends_with("\n")) {
            s = s.substr(0, s.length() - 1);
        }
        godot::UtilityFunctions::print(s);
    } else {
        // Before Godot is fully ready, fall back to stdout
        fputs(msg, stdout);
    }
}

// ──────────────────────────────────────────────
//  Clipboard accessors — called from stubs.cpp
// ──────────────────────────────────────────────
extern "C" int Godot_Clipboard_Get(char *buf, int bufSize) {
    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds || bufSize <= 0) return 0;
    godot::String clip = ds->clipboard_get();
    if (clip.is_empty()) return 0;
    godot::CharString utf8 = clip.utf8();
    int len = utf8.length();
    if (len >= bufSize) len = bufSize - 1;
    memcpy(buf, utf8.get_data(), len);
    buf[len] = '\0';
    return 1;
}

extern "C" void Godot_Clipboard_Set(const char *text) {
    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds || !text) return;
    ds->clipboard_set(godot::String(text));
}

// ──────────────────────────────────────────────
//  MoHAARunner implementation
// ──────────────────────────────────────────────

MoHAARunner::MoHAARunner() {
    initialized = false;
    basepath = "";
    game_flow_state = GameFlowState::BOOT;
    mouse_captured = false;
    hud_visible = true;

    s_mohaa_runner_instance = this;
}

// ── Coordinate conversion helpers (Phase 7a) ──
//
// id Tech 3 (MOHAA): X = Forward, Y = Left, Z = Up  (right-handed, Z-up)
// Godot 4:           X = Right,   Y = Up,   -Z = Forward  (right-handed, Y-up)
//
// Point conversion:
//   Godot.x = -idTech.y
//   Godot.y =  idTech.z
//   Godot.z = -idTech.x
//
// The same formula applies to direction vectors.

static inline Vector3 id_to_godot_point(float ix, float iy, float iz) {
    return Vector3(-iy, iz, -ix);
}

// MOHAA uses Q3-era map units where 1 unit ≈ 1 inch.
// Godot's default units are metres.  We scale by 1/39.37 (inches → metres)
// so geometry and player speed feel correct.
static constexpr float MOHAA_UNIT_SCALE = 1.0f / 39.37f;

static inline Vector3 id_to_godot_position(float ix, float iy, float iz) {
    return Vector3(-iy * MOHAA_UNIT_SCALE, iz * MOHAA_UNIT_SCALE, -ix * MOHAA_UNIT_SCALE);
}

static inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

MoHAARunner::~MoHAARunner() {
    if (s_mohaa_runner_instance == this) {
        s_mohaa_runner_instance = nullptr;
    }

#ifdef HAS_WEAPON_VIEWPORT_MODULE
    Godot_WeaponViewport::get().destroy();
#endif
#ifdef HAS_VFX_MODULE
    Godot_VFX_Shutdown();
#endif
#ifdef HAS_SCREEN_EFFECTS_MODULE
    Godot_ScreenFX_Shutdown();
#endif
#ifdef HAS_WEAPON_EFFECTS_MODULE
    Godot_WeaponEffects_Cleanup();
#endif
#ifdef HAS_IMPACT_EFFECTS_MODULE
    Godot_Impact_Shutdown();
#endif
#ifdef HAS_EXPLOSION_EFFECTS_MODULE
    Godot_Explosion_Shutdown();
    Godot_CameraShake_Clear();
#endif
#ifdef HAS_MUSIC_MODULE
    Godot_Music_Shutdown();
#endif

    if (initialized) {
#ifdef HAS_WEATHER_MODULE
        Godot_Weather_Shutdown();
#endif
        Com_Shutdown();
        /* Mark zone allocator as shut down.  Global C++ destructors
           (e.g. ~con_arrayset for Event::commandList) run after this
           during exit() and must not call Z_Free on stale zone data. */
        Z_MarkShutdown();
        initialized = false;
        Godot_SetEngineInitialized(false);
    } else {
        /* Even if not initialized, mark shutdown as safety net */
        Z_MarkShutdown();
    }

    // ── Module shutdown hooks (defensive) ──
#ifdef HAS_UBERSOUND_MODULE
    Godot_Ubersound_Shutdown();
#endif
#ifdef HAS_MESH_CACHE_MODULE
    Godot_MeshCache::get().clear();
    Godot_MaterialCache::get().clear();
#endif
#ifdef HAS_SHADER_MATERIAL_MODULE
    Godot_Shader_ClearCache();
#endif
#ifdef HAS_FRUSTUM_CULL_MODULE
    Godot_FrustumCull_Shutdown();
#endif

    /* Free scoreboard map preview child canvas item. */
    if (sb_map_preview_ci.is_valid()) {
        RenderingServer::get_singleton()->free_rid(sb_map_preview_ci);
        sb_map_preview_ci = RID();
    }

    if (s_mohaa_runner_instance == this) {
        s_mohaa_runner_instance = nullptr;
    }

    g_godot_ready = false;
}

void MoHAARunner::_bind_methods() {
    // Engine lifecycle
    godot::ClassDB::bind_method(godot::D_METHOD("is_engine_initialized"), &MoHAARunner::is_engine_initialized);
    godot::ClassDB::bind_method(godot::D_METHOD("get_basepath"), &MoHAARunner::get_basepath);
    godot::ClassDB::bind_method(godot::D_METHOD("set_basepath", "path"), &MoHAARunner::set_basepath);
    godot::ClassDB::bind_method(godot::D_METHOD("get_startup_args"), &MoHAARunner::get_startup_args);
    godot::ClassDB::bind_method(godot::D_METHOD("set_startup_args", "args"), &MoHAARunner::set_startup_args);

    // Commands
    godot::ClassDB::bind_method(godot::D_METHOD("execute_command", "command"), &MoHAARunner::execute_command);
    godot::ClassDB::bind_method(godot::D_METHOD("load_map", "map_name"), &MoHAARunner::load_map);

    // Server status (Task 2.5.3)
    godot::ClassDB::bind_method(godot::D_METHOD("is_map_loaded"), &MoHAARunner::is_map_loaded);
    godot::ClassDB::bind_method(godot::D_METHOD("get_current_map"), &MoHAARunner::get_current_map);
    godot::ClassDB::bind_method(godot::D_METHOD("get_player_count"), &MoHAARunner::get_player_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_server_state"), &MoHAARunner::get_server_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_server_state_string"), &MoHAARunner::get_server_state_string);
    godot::ClassDB::bind_method(godot::D_METHOD("get_cvar_string", "name"), &MoHAARunner::get_cvar_string);

    // VFS access (Task 4.1)
    godot::ClassDB::bind_method(godot::D_METHOD("vfs_read_file", "qpath"), &MoHAARunner::vfs_read_file);
    godot::ClassDB::bind_method(godot::D_METHOD("vfs_file_exists", "qpath"), &MoHAARunner::vfs_file_exists);
    godot::ClassDB::bind_method(godot::D_METHOD("vfs_list_files", "directory", "extension"), &MoHAARunner::vfs_list_files);
    godot::ClassDB::bind_method(godot::D_METHOD("vfs_get_gamedir"), &MoHAARunner::vfs_get_gamedir);

    // Input control (Phase 6)
    godot::ClassDB::bind_method(godot::D_METHOD("set_mouse_captured", "captured"), &MoHAARunner::set_mouse_captured);
    godot::ClassDB::bind_method(godot::D_METHOD("is_mouse_captured"), &MoHAARunner::is_mouse_captured);
    godot::ClassDB::bind_method(godot::D_METHOD("set_hud_visible", "visible"), &MoHAARunner::set_hud_visible);
    godot::ClassDB::bind_method(godot::D_METHOD("is_hud_visible"), &MoHAARunner::is_hud_visible);

    // Game flow state (Phase 261)
    godot::ClassDB::bind_method(godot::D_METHOD("get_game_flow_state"), &MoHAARunner::get_game_flow_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_game_flow_state_string"), &MoHAARunner::get_game_flow_state_string);

    // New game flow (Phase 262)
    godot::ClassDB::bind_method(godot::D_METHOD("start_new_game", "difficulty"), &MoHAARunner::start_new_game);
    godot::ClassDB::bind_method(godot::D_METHOD("set_difficulty", "difficulty"), &MoHAARunner::set_difficulty);

    // Save / load game (Phase 264)
    godot::ClassDB::bind_method(godot::D_METHOD("quick_save"), &MoHAARunner::quick_save);
    godot::ClassDB::bind_method(godot::D_METHOD("quick_load"), &MoHAARunner::quick_load);
    godot::ClassDB::bind_method(godot::D_METHOD("save_game", "slot_name"), &MoHAARunner::save_game);
    godot::ClassDB::bind_method(godot::D_METHOD("load_game", "slot_name"), &MoHAARunner::load_game);
    godot::ClassDB::bind_method(godot::D_METHOD("get_save_list"), &MoHAARunner::get_save_list);

    // Multiplayer helpers (Phases 265-266)
    godot::ClassDB::bind_method(godot::D_METHOD("list_available_maps"), &MoHAARunner::list_available_maps);
    godot::ClassDB::bind_method(godot::D_METHOD("start_server", "map", "gametype", "max_clients"), &MoHAARunner::start_server);
    godot::ClassDB::bind_method(godot::D_METHOD("connect_to_server", "address"), &MoHAARunner::connect_to_server);
    godot::ClassDB::bind_method(godot::D_METHOD("disconnect_from_server"), &MoHAARunner::disconnect_from_server);

    // Multiplayer server browser + hosting (Phase 263)
    godot::ClassDB::bind_method(godot::D_METHOD("host_server", "map", "maxplayers", "gametype"), &MoHAARunner::host_server);
    godot::ClassDB::bind_method(godot::D_METHOD("refresh_server_list"), &MoHAARunner::refresh_server_list);
    godot::ClassDB::bind_method(godot::D_METHOD("refresh_lan"), &MoHAARunner::refresh_lan);
    godot::ClassDB::bind_method(godot::D_METHOD("get_server_count"), &MoHAARunner::get_server_count);

    // Settings helpers (Phases 267-270)
    godot::ClassDB::bind_method(godot::D_METHOD("set_audio_volume", "master", "music", "dialog"), &MoHAARunner::set_audio_volume);
    godot::ClassDB::bind_method(godot::D_METHOD("set_video_fullscreen", "fullscreen"), &MoHAARunner::set_video_fullscreen);
    godot::ClassDB::bind_method(godot::D_METHOD("set_video_resolution", "width", "height"), &MoHAARunner::set_video_resolution);
    godot::ClassDB::bind_method(godot::D_METHOD("set_network_rate", "preset"), &MoHAARunner::set_network_rate);

    // Render quality settings
    godot::ClassDB::bind_method(godot::D_METHOD("set_render_quality", "preset"), &MoHAARunner::set_render_quality);

    godot::ClassDB::bind_method(godot::D_METHOD("set_texture_quality", "level"), &MoHAARunner::set_texture_quality);
    godot::ClassDB::bind_method(godot::D_METHOD("get_texture_quality"), &MoHAARunner::get_texture_quality);

    godot::ClassDB::bind_method(godot::D_METHOD("set_shadow_quality", "level"), &MoHAARunner::set_shadow_quality);
    godot::ClassDB::bind_method(godot::D_METHOD("get_shadow_quality"), &MoHAARunner::get_shadow_quality);

    godot::ClassDB::bind_method(godot::D_METHOD("set_geometry_quality", "level"), &MoHAARunner::set_geometry_quality);
    godot::ClassDB::bind_method(godot::D_METHOD("get_geometry_quality"), &MoHAARunner::get_geometry_quality);

    godot::ClassDB::bind_method(godot::D_METHOD("set_effects_quality", "level"), &MoHAARunner::set_effects_quality);
    godot::ClassDB::bind_method(godot::D_METHOD("get_effects_quality"), &MoHAARunner::get_effects_quality);

    godot::ClassDB::bind_method(godot::D_METHOD("set_msaa", "level"), &MoHAARunner::set_msaa);
    godot::ClassDB::bind_method(godot::D_METHOD("get_msaa"), &MoHAARunner::get_msaa);

    godot::ClassDB::bind_method(godot::D_METHOD("set_fxaa_enabled", "enabled"), &MoHAARunner::set_fxaa_enabled);
    godot::ClassDB::bind_method(godot::D_METHOD("is_fxaa_enabled"), &MoHAARunner::is_fxaa_enabled);

    godot::ClassDB::bind_method(godot::D_METHOD("set_vsync_mode", "mode"), &MoHAARunner::set_vsync_mode);
    godot::ClassDB::bind_method(godot::D_METHOD("get_vsync_mode"), &MoHAARunner::get_vsync_mode);

    // Menu control (Phase 261)
    godot::ClassDB::bind_method(godot::D_METHOD("open_main_menu"), &MoHAARunner::open_main_menu);
    godot::ClassDB::bind_method(godot::D_METHOD("close_menu"), &MoHAARunner::close_menu);
    godot::ClassDB::bind_method(godot::D_METHOD("push_menu", "menu_name"), &MoHAARunner::push_menu);
    godot::ClassDB::bind_method(godot::D_METHOD("show_menu", "menu_name", "force"), &MoHAARunner::show_menu, false);
    godot::ClassDB::bind_method(godot::D_METHOD("toggle_menu", "menu_name"), &MoHAARunner::toggle_menu);
    godot::ClassDB::bind_method(godot::D_METHOD("pop_menu", "restore_cvars"), &MoHAARunner::pop_menu, false);
    godot::ClassDB::bind_method(godot::D_METHOD("hide_menu", "menu_name"), &MoHAARunner::hide_menu);
    godot::ClassDB::bind_method(godot::D_METHOD("is_menu_active"), &MoHAARunner::is_menu_active);

    // Properties
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "basepath"), "set_basepath", "get_basepath");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "startup_args"), "set_startup_args", "get_startup_args");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::BOOL, "hud_visible"), "set_hud_visible", "is_hud_visible");

    // Render quality properties (0=Low, 1=Medium, 2=High, 3=Ultra)
    ADD_GROUP("Render Quality", "render_");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_texture_quality", godot::PROPERTY_HINT_ENUM, "Low,Medium,High,Ultra"), "set_texture_quality", "get_texture_quality");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_shadow_quality", godot::PROPERTY_HINT_ENUM, "Off,Low,Medium,High"), "set_shadow_quality", "get_shadow_quality");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_geometry_quality", godot::PROPERTY_HINT_ENUM, "Low,Medium,High,Ultra"), "set_geometry_quality", "get_geometry_quality");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_effects_quality", godot::PROPERTY_HINT_ENUM, "Low,Medium,High,Ultra"), "set_effects_quality", "get_effects_quality");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_msaa", godot::PROPERTY_HINT_ENUM, "Disabled,2x,4x,8x"), "set_msaa", "get_msaa");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::BOOL, "render_fxaa"), "set_fxaa_enabled", "is_fxaa_enabled");
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::INT, "render_vsync_mode", godot::PROPERTY_HINT_ENUM, "Disabled,Enabled,Adaptive,Mailbox"), "set_vsync_mode", "get_vsync_mode");

    // Signals (Task 2.5.4)
    ADD_SIGNAL(godot::MethodInfo("engine_error", godot::PropertyInfo(godot::Variant::STRING, "message")));
    ADD_SIGNAL(godot::MethodInfo("map_loaded", godot::PropertyInfo(godot::Variant::STRING, "map_name")));
    ADD_SIGNAL(godot::MethodInfo("map_unloaded"));
    ADD_SIGNAL(godot::MethodInfo("engine_shutdown_requested"));
    ADD_SIGNAL(godot::MethodInfo("game_flow_state_changed", godot::PropertyInfo(godot::Variant::INT, "new_state")));
    ADD_SIGNAL(godot::MethodInfo("render_quality_changed", godot::PropertyInfo(godot::Variant::STRING, "setting"), godot::PropertyInfo(godot::Variant::INT, "level")));
}

bool MoHAARunner::is_engine_initialized() const {
    return initialized;
}

godot::String MoHAARunner::get_basepath() const {
    return basepath;
}

void MoHAARunner::set_basepath(const godot::String &p_path) {
    basepath = p_path;
}

void MoHAARunner::set_startup_args(const godot::String &p_args) {
    if (initialized) {
        UtilityFunctions::printerr("[MoHAA] set_startup_args ignored after init.");
        return;
    }
    startup_args = p_args;
}

godot::String MoHAARunner::get_startup_args() const {
    return startup_args;
}

// ──────────────────────────────────────────────
//  Godot-side cache clearing after game switch (AA/SH/BT)
//  Called after Com_GameRestart() has already restarted the engine
//  (SV_Shutdown, CL_Shutdown, FS_Restart, Cvar_Restart, CL_Init).
//  We just need to invalidate Godot caches so assets reload from
//  the new pk3s.
// ──────────────────────────────────────────────
void MoHAARunner::clear_godot_caches_for_game_switch(int target_game) {
    UtilityFunctions::print(String("[MoHAA] === GAME SWITCH: clearing Godot caches for game ") +
        String::num_int64(target_game) + String(" ==="));

    // BSP world — force full reload
    if (bsp_map_node) {
        bsp_map_node->queue_free();
        bsp_map_node = nullptr;
        static_model_root = nullptr;
    }
    static_model_pvs.clear();
    loaded_bsp_name = "";

    // Model/mesh/material caches — all stale after FS_Restart
    GodotSkelModelCache::get().clear();
    skel_mesh_cache.clear();
    tinted_mat_cache.clear();
    tiki_mat_cache.clear();
    shader_textures.clear();
    s_shader_texture_loaded_names.clear();
    animmap_info.clear();
    animmap_frames.clear();
    s_surf_anim_cache.clear();
    s_sprite_mat_cache.clear();
    s_beam_mat_cache.clear();
    s_poly_mat_cache.clear();
    s_terrain_mark_mat_cache.clear();
    s_sprite_tint_cache.clear();
    s_beam_tint_cache.clear();
    s_alpha_inv_tex_cache.clear();

#ifdef HAS_MESH_CACHE_MODULE
    Godot_MeshCache::get().clear();
    Godot_MaterialCache::get().clear();
#endif
#ifdef HAS_SHADER_MATERIAL_MODULE
    Godot_Shader_ClearCache();
    Godot_Shader_ClearMaterialRegistry();
#endif

    // Entity pool — free all MeshInstance3D nodes
    for (auto *inst : entity_meshes) {
        if (inst && inst->is_inside_tree()) {
            inst->queue_free();
        }
    }
    entity_meshes.clear();

    // Dynamic lights
    for (auto *light : dlight_nodes) {
        if (light && light->is_inside_tree()) {
            light->queue_free();
        }
    }
    dlight_nodes.clear();

    // Skybox state
    sky_cloud_material.unref();
    sky_cloud_scroll_s = 0.0f;
    sky_cloud_scroll_t = 0.0f;
    sky_cloud_time = 0.0;

    // Sun flare
    sun_flare_initialized = false;
    if (sun_flare_canvas) {
        sun_flare_canvas->queue_free();
        sun_flare_canvas = nullptr;
        sun_flare_control = nullptr;
    }
    sun_flare_sprites.clear();

    // Audio — stop all players (new game may have different sounds)
    for (auto *p : sfx_players_3d) {
        if (p && p->is_playing()) p->stop();
    }
    for (auto *p : sfx_players_2d) {
        if (p && p->is_playing()) p->stop();
    }
    if (music_player && music_player->is_playing()) {
        music_player->stop();
    }

    // PVS state
    pvs_current_cluster = -1;
    pvs_log_count = 0;

    // Cinematic
    if (cin_texture.is_valid()) cin_texture.unref();
    cin_was_active = false;

    // Scoreboard
    scoreboard_visible = false;

    // Reset state tracking so check_world_load() detects the new map
    last_server_state = 0;
    last_map_name = "";
    game_flow_state = GameFlowState::BOOT;

    // Rebuild shader props cache from the newly parsed tr.shaders[]
    // (R_Init was re-called during GR_BeginRegistration)
    Godot_ShaderProps_Load();

    UtilityFunctions::print(String("[MoHAA] === Godot caches cleared for game ") +
        String::num_int64(target_game) + String(" ==="));
}

// ──────────────────────────────────────────────
//  3D scene setup (Phase 7a)
// ──────────────────────────────────────────────

void MoHAARunner::setup_3d_scene() {
    // Create a Node3D root for all 3D content
    game_world = memnew(Node3D);
    game_world->set_name("GameWorld");
    add_child(game_world);

    // Camera3D — driven by engine refdef_t each frame
    camera = memnew(Camera3D);
    camera->set_name("EngineCamera");
    camera->set_fov(80.0);   // will be overridden by engine fov_y
    camera->set_near(0.05);  // 2 inches in id units → ~5cm
    camera->set_far(8000.0 * MOHAA_UNIT_SCALE);  // ~200 metres
    camera->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
    camera->set_current(true);
    game_world->add_child(camera);

    // We intentionally do NOT spawn a Godot DirectionalLight3D here.
    // OpenMOHAA (id Tech 3) bakes all sunlight directly into the BSP lightmaps
    // and the 3D Light Grid. A real-time Godot sun breaks mathematical parity
    // by double-lighting the scene and overriding the baked shadows.

    // WorldEnvironment with basic ambient light
    world_env = memnew(WorldEnvironment);
    world_env->set_name("WorldEnv");
    Ref<Environment> env;
    env.instantiate();
    env->set_background(Environment::BG_COLOR);
    env->set_bg_color(Color(0.4, 0.5, 0.6));   // light grey-blue sky
    env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
    /* Ambient = 0.5 because the lightmap textures are intrinsically baked
     * at 2.0x brightness (<< 1 in godot_bsp_mesh). If ambient is 1.0,
     * the textures blow out to white. */
    env->set_ambient_light_color(Color(1.0, 1.0, 1.0));
    env->set_ambient_light_energy(0.5);

    // Phase 81: Tonemap and exposure to match MOHAA's overbright/gamma
    // Set to linear. The real overbright math is baked directly into the
    // lightmap build pipeline (godot_bsp_mesh load_lightmaps)
    env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
    env->set_tonemap_exposure(1.0);
    env->set_tonemap_white(1.0);
    world_env->set_environment(env);
    game_world->add_child(world_env);

    // ── Dynamic entity shadow light (r_shadows cvar) ──
    // Directional light casting GPU shadows for all RF_SHADOW entities onto PER_PIXEL BSP surfaces.
    // Hidden until r_shadows 1. Direction set per-map from worldspawn sundirection/suncolor.
    // Energy = 0.01: just enough to ensure Godot generates the shadow map.
    // BSP surfaces use a custom light() shader that reads ATTENUATION directly
    // (ignoring LIGHT_COLOR), and entities stay UNSHADED, so this value does
    // not affect the visible output — only shadow map generation matters.
    entity_shadow_light = memnew(DirectionalLight3D);
    entity_shadow_light->set_name("EntityShadowLight");
    // Direction is set per-map by apply_sun_light_direction() after BSP entity string is parsed.
    // Fallback: nearly-overhead sun to minimise wall bleed until map loads.
    entity_shadow_light->set_rotation_degrees(Vector3(-75.0f, 45.0f, 0.0f));
    entity_shadow_light->set_param(Light3D::PARAM_ENERGY, 0.01f);
    entity_shadow_light->set_color(Color(1.0f, 1.0f, 1.0f));
    entity_shadow_light->set_shadow(true);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_BIAS, 0.02f);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_NORMAL_BIAS, 2.0f);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_BLUR, 1.5f);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_FADE_START, 0.8f);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 80.0f);
    entity_shadow_light->set_param(Light3D::PARAM_SHADOW_PANCAKE_SIZE, 20.0f);
    entity_shadow_light->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
    entity_shadow_light->set_visible(false);  // Off until r_shadows 1
    game_world->add_child(entity_shadow_light);

    UtilityFunctions::print("[MoHAA] 3D scene created (Camera3D + light + environment).");

    // ── Weapon viewport (Phase 62) ──
    // Render first-person weapon entities in a separate SubViewport with
    // its own depth buffer, then composite on top of the main scene.
    // This replicates id Tech 3's RF_DEPTHHACK (depth range 0–0.3).
    bool overlay_active_now = false;
    {
        Vector2i win_size = DisplayServer::get_singleton()->window_get_size();
        if (win_size.x < 1 || win_size.y < 1) {
            win_size = Vector2i(1280, 720);
        }

        // SubViewport with transparent background + own World3D
        weapon_viewport = memnew(SubViewport);
        weapon_viewport->set_size(win_size);
        weapon_viewport->set_transparent_background(true);
        weapon_viewport->set_world_3d(Ref<World3D>(memnew(World3D)));
        weapon_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
        // Disable own 3D rendering until we populate it — UPDATE_ALWAYS
        // ensures the viewport runs each frame.
        add_child(weapon_viewport);

        // Camera inside weapon viewport — will mirror main camera each frame
        weapon_camera = memnew(Camera3D);
        weapon_camera->set_near(0.01);
        weapon_camera->set_far(100.0);
        weapon_camera->set_current(true);   // must be current for SubViewport to render
        weapon_viewport->add_child(weapon_camera);

        // Root node for FPS entity meshes inside the weapon viewport
        weapon_root = memnew(Node3D);
        weapon_viewport->add_child(weapon_root);

        // CanvasLayer to overlay weapon texture on top of main scene
        weapon_canvas_layer = memnew(CanvasLayer);
        weapon_canvas_layer->set_layer(10);  // above world, below HUD
        add_child(weapon_canvas_layer);

        weapon_overlay = memnew(TextureRect);
        weapon_overlay->set_texture(weapon_viewport->get_texture());
        weapon_overlay->set_anchors_preset(Control::PRESET_FULL_RECT);
        weapon_overlay->set_stretch_mode(TextureRect::STRETCH_SCALE);
        weapon_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        weapon_canvas_layer->add_child(weapon_overlay);

        UtilityFunctions::print("[MoHAA] Weapon SubViewport created (", win_size.x, "x", win_size.y, ")");
    }
}

// ──────────────────────────────────────────────
//  Camera update (Phase 7a)
// ──────────────────────────────────────────────

void MoHAARunner::update_camera() {
    if (!camera) return;
    if (!Godot_Renderer_HasNewFrame()) return;

    // Read viewpoint from the engine's last RenderScene call
    float origin[3];
    float axis[9];  // viewaxis[0..2], each 3 floats
    float fov_x, fov_y;

    Godot_Renderer_GetViewOrigin(origin);
    Godot_Renderer_GetViewAxis(axis);
    Godot_Renderer_GetFov(&fov_x, &fov_y);
    Godot_Renderer_ClearNewFrame();

    // ── Position ──
    // Convert from id Tech 3 coords (inches) to Godot coords (metres)
    Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
    camera->set_global_position(pos);

    // ── Orientation ──
    // Engine viewaxis[0] = forward, [1] = left, [2] = up  (in id coords)
    // Convert each axis direction vector to Godot coordinates
    // (direction vectors: no scale, just coordinate swap)
    float *fwd = &axis[0];  // viewaxis[0]
    float *lft = &axis[3];  // viewaxis[1]
    float *up  = &axis[6];  // viewaxis[2]

    Vector3 forward_g = id_to_godot_point(fwd[0], fwd[1], fwd[2]);
    Vector3 left_g    = id_to_godot_point(lft[0], lft[1], lft[2]);
    Vector3 up_g      = id_to_godot_point(up[0],  up[1],  up[2]);

    // Godot Basis columns: x = right, y = up, z = back
    // right = -left,  back = -forward
    Vector3 right_g = -left_g;
    Vector3 back_g  = -forward_g;

    Basis basis(right_g, up_g, back_g);
    camera->set_global_transform(Transform3D(basis, pos));

    // ── FOV ──
    // Engine provides vertical fov_y in degrees; Godot's Camera3D.fov
    // is vertical FOV when keep_aspect == KEEP_HEIGHT
    if (fov_y > 1.0f && fov_y < 170.0f) {
        camera->set_fov((double)fov_y);
    }

    // ── Far plane (fog distance) ──
    float fp_dist = 0.0f;
    float fp_bias = 0.0f;
    float fp_color[3] = {0, 0, 0};
    int   fp_cull = 0;
    Godot_Renderer_GetFarplane(&fp_dist, &fp_bias, fp_color, &fp_cull);

    // Phase 39 parity: farclipOverride and farplaneColorOverride from refdef_t.
    // When the engine sets these per-frame (e.g. via script or entity trigger),
    // they take priority over the standard farplane_distance / farplane_color cvars.
    float farclip_override = 0.0f;
    float fp_color_override[3] = {0, 0, 0};
    Godot_Renderer_GetFarplaneOverrides(&farclip_override, fp_color_override);
    if (farclip_override > 0.0f) {
        fp_dist = farclip_override;
    }
    if (fp_color_override[0] > 0.0f || fp_color_override[1] > 0.0f || fp_color_override[2] > 0.0f) {
        fp_color[0] = fp_color_override[0];
        fp_color[1] = fp_color_override[1];
        fp_color[2] = fp_color_override[2];
    }

    if (fp_dist > 0.0f) {
        camera->set_far((double)(fp_dist * MOHAA_UNIT_SCALE));

        // ── Fog rendering ──
        // MOHAA uses GL_LINEAR fog: FOG_START = farplane_bias,
        // FOG_END = farplane_distance.  Godot 4.2 only has exponential
        // density fog (no depth_begin/depth_end).  We approximate the
        // linear [bias, distance] range with a conservative density:
        //   density = 1.0 / (distance_metres)
        // This gives ~63% fog at the far plane and ~16% at the bias
        // point.  The trade-off (some fog before the bias point) is
        // unavoidable with exponential-only fog, but far better than
        // the old 2.3/dist formula which produced 90% fog at the far
        // plane and ~34% at the bias point — making distant geometry
        // appear fully white.
        Ref<Environment> env = world_env->get_environment();
        if (env.is_valid()) {
            float fog_dist = fp_dist * MOHAA_UNIT_SCALE;
            if (fog_dist < 1.0f) fog_dist = 1.0f;
            // Conservative density: ~63% fog at far plane, ~16% at bias
            float density = 1.0f / fog_dist;
            if (!debug_fog_off) {
                env->set_fog_enabled(true);
                env->set_fog_light_color(Color(fp_color[0], fp_color[1], fp_color[2]));
                env->set_fog_density(density);
                env->set_fog_sky_affect(1.0f);
            }
        }
    } else {
        // No fog configured — disable and use default far plane
        Ref<Environment> env = world_env->get_environment();
        if (env.is_valid() && env->is_fog_enabled()) {
            env->set_fog_enabled(false);
        }
    }

    // ── Weapon viewport camera sync ──
    // Mirror the main camera's transform and FOV into the weapon
    // SubViewport's camera so FPS entities render from the same viewpoint.
    if (weapon_camera) {
        weapon_camera->set_global_transform(camera->get_global_transform());
        weapon_camera->set_fov(camera->get_fov());
    }

    // ── Weapon viewport resize ──
    // Keep the weapon SubViewport size in sync with the window.
    if (weapon_viewport) {
        Vector2i win_size = DisplayServer::get_singleton()->window_get_size();
        if (win_size.x > 0 && win_size.y > 0 && weapon_viewport->get_size() != win_size) {
            weapon_viewport->set_size(win_size);
        }
    }
}

// ──────────────────────────────────────────────
//  BSP world loading (Phase 7b)
// ──────────────────────────────────────────────

void MoHAARunner::check_world_load() {
    if (!game_world) return;

    // Check if the renderer has flagged a new world map
    if (!Godot_Renderer_IsWorldMapLoaded()) {
        // World unloaded — remove existing BSP mesh
        if (bsp_map_node) {
            bsp_map_node->queue_free();
            bsp_map_node = nullptr;
            static_model_root = nullptr;  // child of bsp_map_node, freed with it
            static_model_pvs.clear();
            loaded_bsp_name = "";
            GodotSkelModelCache::get().clear();  // Invalidate model cache
            skel_mesh_cache.clear();              // Phase 60: Clear skinned mesh cache
            tinted_mat_cache.clear();             // Phase 61: Clear tinted material cache
            tiki_mat_cache.clear();               // Clear TIKI entity material cache
            shader_textures.clear();              // Shader handles are re-registered on next map
            s_shader_texture_loaded_names.clear();  // Clear name tracking alongside texture cache
            animmap_info.clear();
            animmap_frames.clear();
            s_surf_anim_cache.clear();
            s_sprite_mat_cache.clear();
            s_beam_mat_cache.clear();
            s_poly_mat_cache.clear();
            s_terrain_mark_mat_cache.clear();
            s_sprite_tint_cache.clear();
            s_beam_tint_cache.clear();
            s_alpha_inv_tex_cache.clear();
            sfx_cache.clear();                    // Free cached audio streams for previous map
            // Free mirror SubViewports from previous map
            for (auto &m : active_mirrors) {
                if (m.viewport) {
                    m.viewport->queue_free();
                }
            }
            active_mirrors.clear();
#ifdef HAS_MESH_CACHE_MODULE
            Godot_MeshCache::get().clear();
            Godot_MaterialCache::get().clear();
#endif
#ifdef HAS_SHADER_MATERIAL_MODULE
            Godot_Shader_ClearCache();
            Godot_Shader_ClearMaterialRegistry();
#endif
#ifdef HAS_WEATHER_MODULE
            Godot_Weather_Shutdown();
#endif
#ifdef HAS_PBR_MODULE
            Godot_PBR_Shutdown();
#endif
            Godot_SoundOcclusion_SetEnabled(0);   // Disable occlusion when BSP unloaded
            pvs_current_cluster = -1;             // Reset PVS state
            pvs_log_count = 0;
            sky_cloud_material.unref();           // Reset sky cloud animation state
            sky_cloud_scroll_s = 0.0f;
            sky_cloud_scroll_t = 0.0f;
            sky_cloud_time = 0.0;
            Godot_VFX_Clear();  // Flush VFX caches — shader handles are invalidated by BeginRegistration
#ifdef HAS_WEAPON_EFFECTS_MODULE
            Godot_ShellCasing_Clear();
#endif
#ifdef HAS_EXPLOSION_EFFECTS_MODULE
            Godot_Explosion_Clear();
            Godot_CameraShake_Clear();
#endif
            prev_health = -1;  // Reset health tracking for screen effects
            UtilityFunctions::print("[MoHAA] BSP world unloaded.");
        }
        return;
    }

    const char *map_path = Godot_Renderer_GetWorldMapName();
    if (!map_path || !map_path[0]) return;

    String new_bsp(map_path);
    if (new_bsp == loaded_bsp_name) return;  // Same map already loaded

    // Remove old BSP mesh if any
    if (bsp_map_node) {
        bsp_map_node->queue_free();
        bsp_map_node = nullptr;
        Godot_BSP_Unload();
    }

    // Load new BSP geometry
    UtilityFunctions::print(String("[MoHAA] Loading BSP world: ") + new_bsp);

    // Phase 59: Notify UI system that a map load has started
    Godot_UI_OnMapLoad();

    // Clear texture caches before loading — BeginRegistration resets shader
    // handles in godot_renderer.c, so stale entries from menu/previous map
    // at the same handle numbers would return wrong textures.
    shader_textures.clear();
    s_shader_texture_loaded_names.clear();
    animmap_info.clear();
    animmap_frames.clear();
    tinted_mat_cache.clear();
    tiki_mat_cache.clear();               // Clear TIKI entity material cache
    s_surf_anim_cache.clear();
    s_sprite_mat_cache.clear();
    s_beam_mat_cache.clear();
    s_poly_mat_cache.clear();
    s_terrain_mark_mat_cache.clear();
    s_sprite_tint_cache.clear();
    s_beam_tint_cache.clear();
    s_alpha_inv_tex_cache.clear();

#ifdef HAS_PBR_MODULE
    // Initialise PBR texture discovery BEFORE BSP loading so that
    // surface materials can apply PBR textures during mesh creation.
    Godot_PBR_Init();
    if (Godot_PBR_IsEnabled()) {
        UtilityFunctions::print(String("[PBR] PBR rendering enabled with ") +
                                String::num_int64(Godot_PBR_GetCount()) +
                                String(" texture sets."));
    }
#endif

    Node3D *map_node = Godot_BSP_LoadWorld(map_path);
    if (map_node) {
        game_world->add_child(map_node);
        bsp_map_node = map_node;
        loaded_bsp_name = new_bsp;
        pvs_current_cluster = -1;  // Force PVS recalculation for new map
        pvs_log_count = 0;
        UtilityFunctions::print("[MoHAA] BSP world added to scene.");

        // Instantiate static TIKI models from BSP data
        load_static_models();
        UtilityFunctions::print(String("[MoHAA] Static models loaded for: ") + new_bsp);

        // Load skybox cubemap from sky shader (Phase 12)
        load_skybox();
        UtilityFunctions::print(String("[MoHAA] Loading load_skybox: ") + new_bsp);

        // Load sun flare data from entity string + lensflaredefs.txt
        load_sun_flare();

        // ── Module hooks for world load (defensive) ──
#ifdef HAS_WEATHER_MODULE
        Godot_Weather_Init(game_world);
#endif
        // Enable sound occlusion now that BSP collision data is available
        Godot_SoundOcclusion_SetEnabled(1);
        UtilityFunctions::print("[MoHAA] Sound occlusion enabled.");

        // Orient the shadow DirectionalLight from the new map's sundirection.
        // Then re-apply entity shadow mode so any cached TIKI materials and
        // freshly built BSP ShaderMaterials receive the correct shadow darkness.
        apply_sun_light_direction();
        apply_player_shadow_mode(cached_entity_shadow_mode);

#ifdef HAS_PBR_MODULE
        // ── Next-gen rendering pipeline (requires PBR) ──
        if (Godot_PBR_IsEnabled()) {
            if (world_env && world_env->get_environment().is_valid()) {
                Ref<Environment> env = world_env->get_environment();

                // ── Tonemapping ──
                // ACES filmic with neutral exposure — the baked lightmaps
                // already contain correct lighting levels so we don't boost.
                env->set_tonemapper(Environment::TONE_MAPPER_ACES);
                env->set_tonemap_exposure(1.0);
                env->set_tonemap_white(4.0);

                // ── Ambient light ── (lightmap pass-through, set in setup)
                // Keep ambient at 1.0 white — the lightmap detail-MUL texture
                // IS the lighting.  Don't override to a lower value here.

                // ── SSAO (Screen-Space Ambient Occlusion) ──
                env->set_ssao_enabled(true);
                env->set_ssao_radius(1.5);
                env->set_ssao_intensity(2.0);

                // ── SSR (Screen-Space Reflections) ──
                // Adds real-time reflections on wet/polished surfaces
                env->set_ssr_enabled(true);
                env->set_ssr_max_steps(64);
                env->set_ssr_fade_in(0.15);
                env->set_ssr_fade_out(2.0);
                env->set_ssr_depth_tolerance(0.2);

                // ── Bloom / Glow ──
                // Cinematic glow on bright lights, explosions, fire
                env->set_glow_enabled(true);
                env->set_glow_intensity(0.8);
                env->set_glow_strength(1.2);
                env->set_glow_bloom(0.1);
                env->set_glow_blend_mode(Environment::GLOW_BLEND_MODE_SOFTLIGHT);
                env->set_glow_hdr_bleed_threshold(1.0);
                env->set_glow_hdr_bleed_scale(2.0);
                env->set_glow_hdr_luminance_cap(12.0);
                // Multi-level glow pyramid for natural falloff
                env->set_glow_level(0, 0.0);  // skip finest level
                env->set_glow_level(1, 0.4);
                env->set_glow_level(2, 0.7);
                env->set_glow_level(3, 1.0);
                env->set_glow_level(4, 0.6);
                env->set_glow_level(5, 0.3);
                env->set_glow_level(6, 0.1);

                // ── Volumetric Fog ──
                // Atmospheric depth, god rays from windows and lights
                env->set_volumetric_fog_enabled(true);
                env->set_volumetric_fog_density(0.01);
                env->set_volumetric_fog_albedo(Color(0.9, 0.9, 0.95));
                env->set_volumetric_fog_emission(Color(0.0, 0.0, 0.0));
                env->set_volumetric_fog_emission_energy(0.0);
                env->set_volumetric_fog_anisotropy(0.6);
                env->set_volumetric_fog_length(100.0);
                env->set_volumetric_fog_detail_spread(2.0);
                env->set_volumetric_fog_gi_inject(1.0);
                env->set_volumetric_fog_ambient_inject(0.0);
                env->set_volumetric_fog_sky_affect(0.5);
                env->set_volumetric_fog_temporal_reprojection_enabled(true);
                env->set_volumetric_fog_temporal_reprojection_amount(0.9);

                // ── Depth fog (exponential distance fog fallback) ──
                env->set_fog_enabled(true);
                env->set_fog_light_color(Color(0.7, 0.75, 0.85));
                env->set_fog_light_energy(0.5);
                env->set_fog_sun_scatter(0.3);
                env->set_fog_density(0.001);
                env->set_fog_aerial_perspective(0.5);
                env->set_fog_sky_affect(0.3);

                // ── Colour grading ──
                // Subtle: contrast for depth, slight desaturation for WW2 feel
                env->set_adjustment_enabled(true);
                env->set_adjustment_brightness(1.0);
                env->set_adjustment_contrast(1.05);
                env->set_adjustment_saturation(0.9);

                UtilityFunctions::print("[PBR] Next-gen environment: ACES, SSR, bloom, volumetric fog, SSAO, colour grading.");
            }

            // Sun / directional light removed — OpenMOHAA uses baked lighting only.

            // ── Anti-aliasing ── MSAA 4x for geometry edges
            Viewport *vp = get_viewport();
            if (vp) {
                vp->set_msaa_3d(Viewport::MSAA_4X);
                vp->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_FXAA);
            }
        }
#endif

    } else {
        UtilityFunctions::printerr("[MoHAA] Failed to load BSP world.");
    }
}

// ──────────────────────────────────────────────
//  PVS cluster visibility culling
// ──────────────────────────────────────────────

void MoHAARunner::update_pvs_visibility() {
    int num_clusters = Godot_BSP_GetPVSNumClusters();
    if (num_clusters <= 0) return;

    // Get camera position in id Tech 3 coordinates (already read by update_camera)
    float origin[3];
    Godot_Renderer_GetViewOrigin(origin);

    int new_cluster = Godot_BSP_PointCluster(origin);

    // Only update visibility when the camera changes cluster
    if (new_cluster == pvs_current_cluster) return;
    pvs_current_cluster = new_cluster;

    // If camera is outside the world (cluster -1), show everything
    if (new_cluster < 0) {
        for (int c = 0; c < num_clusters; c++) {
            MeshInstance3D *mi = Godot_BSP_GetClusterMesh(c);
            if (mi) mi->set_visible(true);
        }
        for (const auto &sm : static_model_pvs) {
            if (sm.mesh) sm.mesh->set_visible(true);
        }
        int tp_count = Godot_BSP_GetTerrainPatchCount();
        for (int i = 0; i < tp_count; i++) {
            MeshInstance3D *tmi = Godot_BSP_GetTerrainPatchMesh(i);
            if (tmi) tmi->set_visible(true);
        }
        return;
    }

    // Toggle per-cluster visibility based on PVS + distance override
    int visible_count = 0;
    int hidden_count = 0;
    for (int c = 0; c < num_clusters; c++) {
        MeshInstance3D *mi = Godot_BSP_GetClusterMesh(c);
        if (!mi) continue;

        bool vis = (Godot_BSP_ClusterVisible(new_cluster, c) != 0);

        mi->set_visible(vis);
        if (vis) visible_count++;
        else     hidden_count++;
    }

    // Toggle static model visibility based on their PVS cluster
    int sm_visible = 0, sm_hidden = 0;
    for (const auto &sm : static_model_pvs) {
        if (!sm.mesh) continue;
        if (sm.cluster < 0) {
            sm.mesh->set_visible(true);  // no cluster = always visible
            sm_visible++;
        } else {
            bool vis = (Godot_BSP_ClusterVisible(new_cluster, sm.cluster) != 0);
            sm.mesh->set_visible(vis);
            if (vis) sm_visible++;
            else     sm_hidden++;
        }
    }

    // Terrain visibility is now handled per-frame by update_terrain_visibility()
    // using the engine's real BSP tree walk + PVS + frustum + ter_cull pipeline.

    if (pvs_log_count < 5) {
        UtilityFunctions::print(String("[PVS] Cluster ") +
                                String::num_int64(new_cluster) +
                                ": " + String::num_int64(visible_count) +
                                " visible, " + String::num_int64(hidden_count) +
                                " hidden of " + String::num_int64(num_clusters) +
                                " total. Static models: " +
                                String::num_int64(sm_visible) +
                                " visible, " + String::num_int64(sm_hidden) +
                                " hidden.");
        pvs_log_count++;
    }
}

// ──────────────────────────────────────────────
//  Per-frame terrain visibility (engine-parity)
// ──────────────────────────────────────────────
//  Uses the engine's real BSP tree walk, PVS, frustum culling, and
//  ter_cull view-cone test via R_MarkTerrainForGodot(). This replicates
//  the exact code path that R_AddWorldSurfaces() → R_RecursiveWorldNode()
//  uses in the real renderer to mark terrain patches as visible.

void MoHAARunner::update_terrain_visibility() {
    int tp_count = Godot_BSP_GetTerrainPatchCount();
    if (tp_count <= 0) return;

    // Phase 39 parity: respect renderTerrain flag from refdef_t.
    // When false (e.g. during certain cinematics or UI screens), hide all terrain.
    if (!Godot_Renderer_GetRenderTerrain()) {
        for (int pi = 0; pi < tp_count; pi++) {
            MeshInstance3D *patch_mi = Godot_BSP_GetTerrainPatchMesh(pi);
            if (patch_mi) patch_mi->set_visible(false);
        }
        return;
    }

    // Read current view parameters (already captured by GR_RenderScene)
    float origin[3];
    Godot_Renderer_GetViewOrigin(origin);

    float axis_flat[9];
    Godot_Renderer_GetViewAxis(axis_flat);

    // Reshape flat float[9] → float[3][3] for the engine function
    float axis[3][3];
    axis[0][0] = axis_flat[0]; axis[0][1] = axis_flat[1]; axis[0][2] = axis_flat[2];
    axis[1][0] = axis_flat[3]; axis[1][1] = axis_flat[4]; axis[1][2] = axis_flat[5];
    axis[2][0] = axis_flat[6]; axis[2][1] = axis_flat[7]; axis[2][2] = axis_flat[8];

    float fov_x = 0.0f, fov_y = 0.0f;
    Godot_Renderer_GetFov(&fov_x, &fov_y);

    float farplane_dist = 0.0f, farplane_bias = 0.0f;
    float farplane_color[3] = {0};
    int   farplane_cull = 0;
    Godot_Renderer_GetFarplane(&farplane_dist, &farplane_bias, farplane_color, &farplane_cull);

    // Run the engine's terrain marking pipeline:
    // R_SetupFrustum → R_MarkLeaves → R_TerrainPrepareFrame → R_TerrainOnlyWorldNode
    R_MarkTerrainForGodot(origin, axis, fov_x, fov_y, farplane_dist);
    Godot_Terrain_TessellateForGodot();

    // Apply visibility results to Godot MeshInstance3D nodes
    for (int i = 0; i < tp_count; i++) {
        MeshInstance3D *tmi = Godot_BSP_GetTerrainPatchMesh(i);
        if (!tmi) continue;
        int bsp_idx = Godot_BSP_GetTerrainPatchBSPIndex(i);
        const bool marked = (bsp_idx >= 0 && Godot_Terrain_IsPatchMarked(bsp_idx) != 0);
        if (!marked) {
            tmi->set_visible(false);
            continue;
        }

        const bool updated = (Godot_BSP_UpdateTerrainPatchFromRenderer(i) != 0);
        tmi->set_visible(updated);
    }
}

// ──────────────────────────────────────────────
//  Static BSP model loading (Phase 10)
// ──────────────────────────────────────────────

/// Check whether a shader handle uses additive blending.
/// Used to keep additive entities in the main scene instead of the weapon
/// SubViewport, where alpha compositing breaks additive transparency
/// (black pixels that should be invisible become opaque).
static bool is_shader_additive(int shaderHandle)
{
    if (shaderHandle <= 0) return false;
    const char *sn = Godot_Renderer_GetShaderName(shaderHandle);
    if (!sn || !sn[0]) return false;
    const GodotShaderProps *sp = Godot_ShaderProps_Find(sn);
    return (sp && sp->transparency == SHADER_ADDITIVE);
}

/// Apply shader transparency / cull properties to a StandardMaterial3D.
/// Call after setting the albedo texture.  shader_name is the C-string
/// name used for the Godot_ShaderProps_Find lookup (e.g. "textures/foo/bar").
static void apply_shader_props_to_material(Ref<StandardMaterial3D> &mat,
                                            const char *shader_name)
{
    if (!shader_name || !shader_name[0]) return;

    const GodotShaderProps *sp = Godot_ShaderProps_Find(shader_name);
    if (!sp) {
        return;
    }

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
        case SHADER_MULTIPLICATIVE_INV:
            // dst*(1-src) can't be expressed exactly in StandardMaterial3D.
            // BLEND_MODE_MUL (dst*src) is the closest approximation — at least
            // darkens instead of rendering a solid grey quad.  Callers that need
            // exact inv-mul (polys, terrain marks) use a custom ShaderMaterial.
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

    // Phase 36: tcMod scale — apply UV scale if non-default
    if (sp->has_tcmod) {
        if (sp->tcmod_scale_s != 1.0f || sp->tcmod_scale_t != 1.0f) {
            mat->set_uv1_scale(Vector3(sp->tcmod_scale_s, sp->tcmod_scale_t, 1.0f));
        }
        // tcMod scroll — UV offset is animated per-frame by update_shader_animations()
    }

    // Phase 56/57: rgbGen/alphaGen baseline material state
    if (sp->rgbgen_type == 4) { // const
        Color a = mat->get_albedo();
        mat->set_albedo(Color(clamp01(sp->rgbgen_const[0]),
                              clamp01(sp->rgbgen_const[1]),
                              clamp01(sp->rgbgen_const[2]), a.a));
    } else if (sp->rgbgen_type == 0) { // identity
        Color a = mat->get_albedo();
        mat->set_albedo(Color(1.0f, 1.0f, 1.0f, a.a));
    }

    if (sp->alphagen_type == 4) { // const
        Color a = mat->get_albedo();
        float alpha = clamp01(sp->alphagen_const);
        mat->set_albedo(Color(a.r, a.g, a.b, alpha));
        if (alpha < 0.999f) {
            mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        }
    }

    // Phase 65: Fullbright rendering for nolightmap surfaces
    if (sp->no_lightmap) {
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    }

    // Phase 136: deformVertexes autosprite/autosprite2 billboard mode
    if (sp->has_deform) {
        if (sp->deform_type == 3) { // autosprite
            mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        } else if (sp->deform_type == 4) { // autosprite2
            mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_FIXED_Y);
        }
    }

    // Phase 142: clampMap — disable texture repeat if the first diffuse stage
    // uses clampMap instead of map.  Mirrors R_FindShader() image loading which
    // passes GL_CLAMP for clampMap stages.
    if (sp->stage_count > 0) {
        for (int st = 0; st < sp->stage_count; st++) {
            if (!sp->stages[st].active) continue;
            if (sp->stages[st].isLightmap) continue;
            if (sp->stages[st].isClampMap) {
                mat->set_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT, false);
            }
            // Phase 143: depthWrite / nodepthwrite / noDepthTest
            if (sp->stages[st].depthWriteExplicit) {
                if (sp->stages[st].depthWriteEnabled) {
                    mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_ALWAYS);
                } else {
                    mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
                }
            }
            if (sp->stages[st].noDepthTest) {
                mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
                mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
            }
            break;  // only check the first non-lightmap stage
        }
    }

#ifdef HAS_PBR_MODULE
    // PBR texture enhancement: if HD PBR textures exist for this shader,
    // apply normal map, roughness map, and switch to lit rendering.
    // This is the core of the modern graphics upgrade path.
    if (Godot_PBR_IsEnabled() && shader_name) {
        Godot_PBR_ApplyToMaterial(mat, shader_name);
    }
#endif
}

/// id Tech 3 AngleVectorsLeft — computes forward/left/up vectors from
/// Euler angles [pitch, yaw, roll] in degrees.  Uses MOHAA convention:
/// PITCH=0, YAW=1, ROLL=2.
static void id_angle_vectors_left(const float *angles,
                                  float *forward, float *left, float *up)
{
    float sp, cp, sy, cy, sr, cr;
    float ang;

    ang = angles[1] * (3.14159265358979f / 180.0f);  // YAW
    sy = sinf(ang); cy = cosf(ang);
    ang = angles[0] * (3.14159265358979f / 180.0f);  // PITCH
    sp = sinf(ang); cp = cosf(ang);
    ang = angles[2] * (3.14159265358979f / 180.0f);  // ROLL
    sr = sinf(ang); cr = cosf(ang);

    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (left) {
        // Matches AngleVectorsLeft() in q_math.c
        left[0] = (sr * sp * cy + cr * -sy);
        left[1] = (sr * sp * sy + cr * cy);
        left[2] = sr * cp;
    }
    if (up) {
        up[0] = (cr * sp * cy + -sr * -sy);
        up[1] = (cr * sp * sy + -sr * cy);
        up[2] = cr * cp;
    }
}

void MoHAARunner::load_static_models() {
    int count = Godot_BSP_GetStaticModelCount();
    if (count <= 0) return;

    // Clean up any previous static models
    if (static_model_root) {
        static_model_root->queue_free();
        static_model_root = nullptr;
    }
    static_model_pvs.clear();

    static_model_root = memnew(Node3D);
    static_model_root->set_name("StaticModels");
    if (bsp_map_node) {
        bsp_map_node->add_child(static_model_root);
    } else {
        game_world->add_child(static_model_root);
    }

    int placed = 0, failed = 0;

    for (int i = 0; i < count; i++) {
        const BSPStaticModelDef *def = Godot_BSP_GetStaticModelDef(i);
        if (!def || !def->model[0]) continue;

        // Build full model path — BSP stores paths relative to models/
        // (mirrors R_InitStaticModels in tr_staticmodels.cpp)
        char full_path[256];
        if (strncasecmp(def->model, "models", 6) != 0) {
            snprintf(full_path, sizeof(full_path), "models/%s", def->model);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", def->model);
        }
        // Canonicalise: collapse double slashes
        {
            char *r = full_path, *w = full_path;
            while (*r) {
                if (*r == '/' && *(r + 1) == '/') { r++; continue; }
                *w++ = *r++;
            }
            *w = '\0';
        }

        // Register the TIKI model with the renderer
        int hModel = Godot_Model_Register(full_path);
        if (hModel <= 0) {
            failed++;
            continue;
        }

        // Build or retrieve the cached mesh
        const GodotSkelModelCache::CachedModel *cached =
            GodotSkelModelCache::get().get_model(hModel);

        if (!cached || cached->lod_meshes.empty() || !cached->lod_meshes[0].is_valid()) {
            failed++;
            continue;
        }

        // Create MeshInstance3D
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String("SM_") + String::num_int64(i));
        mi->set_extra_cull_margin(4.0f);

        // ── Build per-instance mesh with baked vertex colours ──
        // The BSP stores per-vertex RGBA lighting data for each static
        // model instance (LUMP_STATICMODELDATA, overbright-processed by
        // R_InitStaticModels).  We duplicate the cached mesh's surface
        // arrays and inject the per-vertex colours so that each instance
        // gets its own baked lighting, matching OpenMoHAA exactly.
        int firstVtxData = Godot_BSP_GetStaticModelFirstVertexData(i);
        Ref<ArrayMesh> cachedMesh = cached->lod_meshes[0];
        int surfCount = cachedMesh->get_surface_count();
        bool has_vtx_colors = (firstVtxData >= 0);

        Ref<ArrayMesh> instanceMesh;
        if (has_vtx_colors) {
            instanceMesh.instantiate();
            int vtxOffset = 0;  // accumulated vertex offset within this model

            for (int s = 0; s < surfCount; s++) {
                Array arrays = cachedMesh->surface_get_arrays(s);
                if (arrays.size() < Mesh::ARRAY_MAX) {
                    vtxOffset += ((PackedVector3Array)arrays[Mesh::ARRAY_VERTEX]).size();
                    continue;
                }

                PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
                int numVerts = verts.size();

                // Read per-vertex RGBA from the engine's staticModelData
                PackedColorArray colors;
                colors.resize(numVerts);

                if (numVerts > 0) {
                    unsigned char *rgba = (unsigned char *)malloc(numVerts * 4);
                    if (rgba) {
                        int got = Godot_BSP_GetStaticModelVertexColors(
                            i, vtxOffset, numVerts, rgba);
                        for (int v = 0; v < numVerts; v++) {
                            if (v < got) {
                                colors.set(v, Color(
                                    rgba[v * 4 + 0] / 255.0f,
                                    rgba[v * 4 + 1] / 255.0f,
                                    rgba[v * 4 + 2] / 255.0f,
                                    rgba[v * 4 + 3] / 255.0f));
                            } else {
                                colors.set(v, Color(1, 1, 1, 1));
                            }
                        }
                        std::free(rgba);
                    } else {
                        for (int v = 0; v < numVerts; v++)
                            colors.set(v, Color(1, 1, 1, 1));
                    }
                }

                arrays[Mesh::ARRAY_COLOR] = colors;

                // Validate arrays before passing to Godot
                PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
                if (numVerts <= 0 || idx.size() < 3) {
                    vtxOffset += numVerts;
                    continue;
                }

                instanceMesh->add_surface_from_arrays(
                    Mesh::PRIMITIVE_TRIANGLES, arrays);
                vtxOffset += numVerts;
            }
            mi->set_mesh(instanceMesh);
        } else {
            mi->set_mesh(cachedMesh);
        }

        // Apply shader textures to each surface.
        // Mirrors R_InitStaticModels: register each surface shader via
        // RegisterShader, then use the returned handle for texture lookup
        // through the standard get_shader_texture pipeline.
        Ref<Mesh> meshRef = mi->get_mesh();
        int matCount = meshRef.is_valid() ? meshRef->get_surface_count() : 0;
        if (matCount > (int)cached->surfaces.size())
            matCount = (int)cached->surfaces.size();
        for (int s = 0; s < matCount; s++) {
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            // MOHAA default is CT_FRONT_SIDED (back-face cull).
            // apply_shader_props_to_material() overrides to CULL_DISABLED
            // only if the shader says "cull none".
            mat->set_cull_mode(BaseMaterial3D::CULL_BACK);

            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            mat->set_specular(0.2f);
            mat->set_roughness(0.9f);

            // Enable vertex colour modulation so baked static lighting
            // from the BSP's LUMP_STATICMODELDATA is applied.
            if (has_vtx_colors) {
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            }

            const String &shader_name = cached->surfaces[s].shader_name;
            bool found_tex = false;

            if (!shader_name.is_empty()) {
                // Register the shader into the renderer's shader table
                // (same as R_FindShader in R_InitStaticModels), then use the
                // handle with get_shader_texture() for the standard texture
                // loading path.
                CharString cs = shader_name.ascii();
                int shaderHandle = Godot_Renderer_RegisterShader(cs.get_data());
                if (shaderHandle > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(shaderHandle);
                    if (tex.is_valid()) {
                        mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                        found_tex = true;
                    }
                }
            }

            if (!found_tex) {
                mat->set_albedo(Color(0.5, 0.6, 0.4, 1.0));
            }

            // Apply shader transparency / cull properties (Phase 11)
            if (!shader_name.is_empty()) {
                CharString cs = shader_name.ascii();
                apply_shader_props_to_material(mat, cs.get_data());

/*
                if (shader_name.to_lower().contains("vanity")) {
                    UtilityFunctions::print(String("[MoHAA][MIRROR-DEBUG-STATIC] TIKI shader name contains vanity: '") + shader_name + "'");
                }

                if (shader_name == "static_Vanity" || shader_name.to_lower() == "static_vanity" || shader_name.to_lower().contains("vanity")) {
                    UtilityFunctions::print(String("[MoHAA][MIRROR-DEBUG-STATIC] Vanity mirror hack triggered for surface ") + String::num_int64(s));

                    MirrorViewport *mv = nullptr;
                    for (auto &m : active_mirrors) {
                        if (m.mesh_instance == nullptr || m.mesh_instance == mi) {
                            mv = &m;
                            break;
                        }
                    }
                    if (!mv) {
                        active_mirrors.push_back(MirrorViewport());
                        mv = &active_mirrors.back();
                    }

                    if (!mv->viewport) {
                        mv->viewport = memnew(SubViewport);
                        mv->viewport->set_size(get_viewport()->get_visible_rect().size);
                        mv->viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
                        mv->viewport->set_world_3d(get_viewport()->find_world_3d());
                        add_child(mv->viewport);
                    }

                    if (!mv->camera) {
                        mv->camera = memnew(Camera3D);
                        mv->viewport->add_child(mv->camera);
                        mv->camera->set_cull_mask(~(1 << 20)); // Exclude if we used layer 20
                    }

                    mv->mesh_instance = mi;
                    mv->surface_idx = s;

                    mv->normal = Vector3(0, 0, 1);
                    mv->center = Vector3(0, 0, 0);

                    Ref<ViewportTexture> vtex = mv->viewport->get_texture();

                    Ref<Shader> mshader;
                    mshader.instantiate();
                    mshader->set_code(
                        "shader_type spatial;\n"
                        "render_mode unshaded, cull_disabled;\n"
                        "uniform sampler2D base_texture : source_color, filter_linear_mipmap;\n"
                        "uniform sampler2D cutout_texture : source_color, filter_linear_mipmap;\n"
                        "uniform sampler2D mirror_texture : source_color, filter_linear;\n"
                        "void fragment() {\n"
                        "    vec4 base_color = texture(base_texture, UV);\n"
                        "    vec4 cutout = texture(cutout_texture, UV);\n"
                        "    vec4 reflection = texture(mirror_texture, SCREEN_UV);\n"
                        "    \n"
                        "    if (base_color.a < 0.5) {\n"
                        "        discard; // Cut out the feet of the vanity\n"
                        "    }\n"
                        "    \n"
                        "    // The cutout texture likely uses its alpha (or rgb) to mask the mirror.\n"
                        "    // We'll use alpha, but if it's solid white we fallback to color luminance.\n"
                        "    float mask = cutout.a;\n"
                        "    if (mask > 0.99 && cutout.r < 0.1) mask = cutout.r; // heuristic fallback\n"
                        "    \n"
                        "    ALBEDO = mix(base_color.rgb, reflection.rgb, mask);\n"
                        "}\n"
                    );

                    Ref<ShaderMaterial> smat;
                    smat.instantiate();
                    smat->set_shader(mshader);

                    Ref<Texture2D> base_tex = mat->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
                    smat->set_shader_parameter("base_texture", base_tex);
                    smat->set_shader_parameter("mirror_texture", vtex);

                    int cutout_handle = Godot_Renderer_RegisterShader("textures/models/items/vanity_cutout.tga");
                    if (cutout_handle > 0) {
                        Ref<ImageTexture> cutout_tex = get_shader_texture(cutout_handle);
                        if (cutout_tex.is_valid()) {
                            smat->set_shader_parameter("cutout_texture", cutout_tex);
                        }
                    }

                    mv->override_mat = smat;
                    mi->set_surface_override_material(s, smat);
                    continue;
                }
                */
            }

            mi->set_surface_override_material(s, mat);
        }

        // ── Compute transform from origin + angles + scale ──
        // Mirrors renderergl1/tr_main.c:R_RotateForStaticModel +
        // renderergl1/tr_staticmodels.cpp:R_AddStaticModelSurfaces.
        float fwd[3], left[3], up[3];
        id_angle_vectors_left(def->angles, fwd, left, up);

        // Static-model origin is offset by tiki->load_origin * (load_scale * def.scale)
        // in model space, then rotated by the static-model axis.
        float scaled_local[3] = {0.0f, 0.0f, 0.0f};
        {
            float s = (def->scale > 0.001f) ? def->scale : 1.0f;
            float tiki_scale = cached->tiki_scale * s;

            void *tiki_ptr = Godot_Model_GetTikiPtr(hModel);
            if (tiki_ptr) {
                float load_origin[3] = {0.0f, 0.0f, 0.0f};
                Godot_Skel_GetOrigin(tiki_ptr, load_origin);
                scaled_local[0] = load_origin[0] * tiki_scale;
                scaled_local[1] = load_origin[1] * tiki_scale;
                scaled_local[2] = load_origin[2] * tiki_scale;
            }
        }

        // World offset in id-space: fwd*x + left*y + up*z
        float world_off_id[3] = {
            fwd[0] * scaled_local[0] + left[0] * scaled_local[1] + up[0] * scaled_local[2],
            fwd[1] * scaled_local[0] + left[1] * scaled_local[1] + up[1] * scaled_local[2],
            fwd[2] * scaled_local[0] + left[2] * scaled_local[1] + up[2] * scaled_local[2],
        };

        Vector3 pos = id_to_godot_position(def->origin[0] + world_off_id[0],
                                            def->origin[1] + world_off_id[1],
                                            def->origin[2] + world_off_id[2]);

        // Convert id axis to Godot basis
        Vector3 forward_g = id_to_godot_point(fwd[0],  fwd[1],  fwd[2]);
        Vector3 left_g    = id_to_godot_point(left[0], left[1], left[2]);
        Vector3 up_g      = id_to_godot_point(up[0],   up[1],   up[2]);

        // Godot basis: X=right, Y=up, Z=back
        Vector3 godot_right = -left_g;
        Vector3 godot_up    = up_g;
        Vector3 godot_back  = -forward_g;

        float s = (def->scale > 0.001f) ? def->scale : 1.0f;
        Basis basis(godot_right * s, godot_up * s, godot_back * s);
        mi->set_global_transform(Transform3D(basis, pos));

        static_model_root->add_child(mi);

        // Record PVS cluster for this static model
        StaticModelPVS pvs_entry;
        pvs_entry.mesh = mi;
        pvs_entry.cluster = Godot_BSP_GetStaticModelCluster(i);
        static_model_pvs.push_back(pvs_entry);

        placed++;
    }

    int sm_pvs_assigned = 0;
    for (const auto &sm : static_model_pvs) {
        if (sm.cluster >= 0) sm_pvs_assigned++;
    }
    UtilityFunctions::print(String("[MoHAA] Static models: ") +
                            String::num_int64(placed) + " placed, " +
                            String::num_int64(failed) + " failed, " +
                            String::num_int64(sm_pvs_assigned) + " PVS-culled.");
}

// ──────────────────────────────────────────────
//  Skybox loading (Phase 12)
// ──────────────────────────────────────────────

void MoHAARunner::load_skybox() {
    if (!world_env) return;

    const char *sky_env = Godot_ShaderProps_GetSkyEnv();
    if (!sky_env || !sky_env[0]) {
        UtilityFunctions::print("[MoHAA] No sky shader found — keeping default background.");
        return;
    }

    UtilityFunctions::print(String("[MoHAA] Loading skybox: ") + sky_env);

    // id Tech 3 sky uses manual quads (MakeSkyVec in tr_sky.c), NOT GL cubemaps.
    // The face images must be mapped to Godot cubemap layers accounting for both
    // the id Tech 3 → Godot coordinate transform AND the difference between
    // id Tech 3's quad UV layout and OpenGL cubemap conventions.
    //
    // Coordinate mapping: id Tech 3 (X=fwd, Y=left, Z=up) → Godot (X=right, Y=up, -Z=fwd)
    //   Godot +X (right)  = id Tech 3 -Y → face suffix "_ft" via sky_texorder
    //   Godot -X (left)   = id Tech 3 +Y → "_bk"
    //   Godot +Y (up)     = id Tech 3 +Z → "_up"
    //   Godot -Y (down)   = id Tech 3 -Z → "_dn"
    //   Godot +Z (back)   = id Tech 3 -X → "_lf"
    //   Godot -Z (front)  = id Tech 3 +X → "_rt"
    //
    // Per-face orientation fix (MakeSkyVec UV vs OpenGL cubemap sc/tc):
    //   Layers 0,1 (+X,-X): horizontal flip
    //   Layers 2,3 (+Y,-Y): vertical flip
    //   Layers 4,5 (+Z,-Z): no transform
    static const char *suffixes[6] = { "_ft", "_bk", "_up", "_dn", "_lf", "_rt" };
    static const char *extensions[] = { ".jpg", ".tga", nullptr };

    TypedArray<Image> face_images;
    face_images.resize(6);
    int loaded = 0;

    for (int i = 0; i < 6; i++) {
        Ref<Image> img;
        bool found = false;

        for (int e = 0; extensions[e]; e++) {
            char path[256];
            snprintf(path, sizeof(path), "%s%s%s", sky_env, suffixes[i], extensions[e]);

            void *raw = nullptr;
            long len = Godot_VFS_ReadFile(path, &raw);
            if (len <= 0 || !raw) continue;

            PackedByteArray buf;
            buf.resize(len);
            memcpy(buf.ptrw(), raw, len);
            Godot_VFS_FreeFile(raw);

            img.instantiate();
            Error err;
            if (extensions[e][1] == 'j') {
                err = img->load_jpg_from_buffer(buf);
            } else {
                err = img->load_tga_from_buffer(buf);
            }

            if (err == OK && img->get_width() > 0) {
                found = true;
                break;
            }
            img.unref();
        }

        if (!found) {
            UtilityFunctions::printerr(
                String("[MoHAA] Sky face missing: ") + sky_env + suffixes[i]);
            return;
        }

        // Ensure consistent format for cubemap creation
        if (img->get_format() != Image::FORMAT_RGBA8) {
            img->convert(Image::FORMAT_RGBA8);
        }

        // Fix orientation: id Tech 3 sky quads (MakeSkyVec st_to_vec) have
        // different UV layout than OpenGL cubemap face conventions (sc/tc).
        // Derived by tracing each Q3 axis through st_to_vec → Q3 coords →
        // Godot coords (Gx=-Qy, Gy=Qz, Gz=-Qx) → OpenGL cubemap (sc,tc):
        //   Layers 0,1,4,5 (+X,-X,+Z,-Z): horizontal flip
        //   Layers 2,3 (+Y,-Y): vertical flip
        if (i == 2 || i == 3) {
            img->flip_y();
        } else {
            img->flip_x();
        }

        face_images[i] = img;
        loaded++;
    }

    if (loaded != 6) return;

    // Find the largest face dimension — all faces must match for Cubemap::create_from_images
    int max_size = 0;
    for (int i = 0; i < 6; i++) {
        Ref<Image> face = face_images[i];
        if (face->get_width() > max_size) max_size = face->get_width();
        if (face->get_height() > max_size) max_size = face->get_height();
    }
    // Resize any mismatched faces to the largest dimension
    for (int i = 0; i < 6; i++) {
        Ref<Image> face = face_images[i];
        if (face->get_width() != max_size || face->get_height() != max_size) {
            face->resize(max_size, max_size);
        }
    }

    // Create Cubemap from the 6 face images
    Ref<Cubemap> cubemap;
    cubemap.instantiate();
    Error err = cubemap->create_from_images(face_images);
    if (err != OK) {
        UtilityFunctions::printerr(String("[MoHAA] Failed to create sky cubemap, error=") + String::num_int64((int)err));
        return;
    }

    // ── Check for cloud layer ──
    float cloud_height = 0.0f;
    char cloud_map_path[256] = {0};
    float cloud_scroll_s = 0.0f, cloud_scroll_t = 0.0f;
    int cloud_is_additive = 0;
    bool has_clouds = (Godot_ShaderProps_GetSkyCloudData(
        &cloud_height, cloud_map_path, sizeof(cloud_map_path),
        &cloud_scroll_s, &cloud_scroll_t, &cloud_is_additive) != 0);

    Ref<ImageTexture> cloud_tex;
    if (has_clouds && cloud_map_path[0]) {
        // Try loading the cloud texture (with common extensions)
        static const char *cloud_exts[] = { "", ".tga", ".jpg", nullptr };
        for (int e = 0; cloud_exts[e]; e++) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s%s", cloud_map_path, cloud_exts[e]);

            void *raw = nullptr;
            long len = Godot_VFS_ReadFile(full_path, &raw);
            if (len <= 0 || !raw) continue;

            PackedByteArray buf;
            buf.resize(len);
            memcpy(buf.ptrw(), raw, len);
            Godot_VFS_FreeFile(raw);

            Ref<Image> cloud_img;
            cloud_img.instantiate();
            Error cerr;
            // Detect format from extension
            const char *ext = strrchr(full_path, '.');
            if (ext && (ext[1] == 'j' || ext[1] == 'J')) {
                cerr = cloud_img->load_jpg_from_buffer(buf);
            } else {
                cerr = cloud_img->load_tga_from_buffer(buf);
            }

            if (cerr == OK && cloud_img->get_width() > 0) {
                if (cloud_img->get_format() != Image::FORMAT_RGBA8) {
                    cloud_img->convert(Image::FORMAT_RGBA8);
                }
                cloud_tex = ImageTexture::create_from_image(cloud_img);
                UtilityFunctions::print(
                    String("[MoHAA] Sky cloud texture loaded: ") + full_path +
                    " (cloudHeight=" + String::num(cloud_height, 0) +
                    ", scroll=" + String::num(cloud_scroll_s, 4) + "/" + String::num(cloud_scroll_t, 4) +
                    ", additive=" + String::num(cloud_is_additive) + ")");
                break;
            }
        }
        if (!cloud_tex.is_valid()) {
            UtilityFunctions::print(
                String("[MoHAA] Sky cloud texture not found: ") + cloud_map_path +
                " — skybox without clouds.");
            has_clouds = false;
        }
    }

    // ── Build Sky shader ──
    Ref<Shader> sky_shader;
    sky_shader.instantiate();

    if (has_clouds && cloud_tex.is_valid()) {
        // Sky shader with cubemap + cloud dome overlay.
        // Cloud UV uses spherical projection matching id Tech 3's R_InitSkyTexCoords:
        // the cloud is projected onto a dome at cloudHeight above a sphere of
        // radius 4096 units.  UV coords use acos() of the normalised intersection
        // point's horizontal components, matching the engine's sRad/tRad.
        //
        // Coordinate mapping: Engine (X=forward, Y=left, Z=up) → Godot (X=right, Y=up, -Z=forward).
        // Engine v[0] (forward) = Godot -v.z;  engine v[1] (left) = Godot -v.x.
        //
        // Blend mode: additive (blendFunc add) or alpha (blendFunc blend).
        // tcMod scroll: cloud UVs scroll over time.
        sky_shader->set_code(
            "shader_type sky;\n"
            "uniform samplerCube sky_cubemap : source_color;\n"
            "uniform sampler2D cloud_texture : source_color, filter_linear, repeat_enable;\n"
            "uniform float cloud_height = 512.0;\n"
            "uniform float cloud_time = 0.0;\n"
            "uniform float scroll_s = 0.0;\n"
            "uniform float scroll_t = 0.0;\n"
            "uniform bool is_additive = true;\n"
            "\n"
            "void sky() {\n"
            "    vec3 sky_color = texture(sky_cubemap, EYEDIR).rgb;\n"
            "\n"
            "    // Spherical cloud projection (R_InitSkyTexCoords)\n"
            "    vec3 dir = normalize(EYEDIR);\n"
            "    float R = 4096.0;\n"
            "    float H = cloud_height;\n"
            "\n"
            "    // Only render clouds above the horizon\n"
            "    if (dir.y > 0.01) {\n"
            "        // Parametric dome intersection — simplified for |dir|=1\n"
            "        float p = -dir.y * R + sqrt(dir.y * dir.y * R * R + 2.0 * R * H + H * H);\n"
            "\n"
            "        vec3 v = dir * p;\n"
            "        v.y += R;\n"
            "        v = normalize(v);\n"
            "\n"
            "        // Engine: sRad = acos(v[0]), tRad = acos(v[1])\n"
            "        // Engine X (forward) = Godot -Z,  Engine Y (left) = Godot -X\n"
            "        float s_coord = acos(clamp(-v.z, -1.0, 1.0));\n"
            "        float t_coord = acos(clamp(-v.x, -1.0, 1.0));\n"
            "\n"
            "        // Apply tcMod scroll animation\n"
            "        s_coord += scroll_s * cloud_time;\n"
            "        t_coord += scroll_t * cloud_time;\n"
            "\n"
            "        vec4 cloud_sample = texture(cloud_texture, vec2(s_coord, t_coord));\n"
            "\n"
            "        // Fade out near horizon to avoid seam\n"
            "        float horizon_mask = smoothstep(0.01, 0.15, dir.y);\n"
            "\n"
            "        if (is_additive) {\n"
            "            // blendFunc add: GL_ONE GL_ONE — additive blend\n"
            "            COLOR = sky_color + cloud_sample.rgb * horizon_mask;\n"
            "        } else {\n"
            "            // blendFunc blend: GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n"
            "            float cloud_alpha = clamp(cloud_sample.a, 0.0, 1.0) * horizon_mask;\n"
            "            COLOR = mix(sky_color, cloud_sample.rgb, cloud_alpha);\n"
            "        }\n"
            "    } else {\n"
            "        COLOR = sky_color;\n"
            "    }\n"
            "}\n"
        );
    } else {
        // Simple cubemap-only sky shader (no clouds)
        sky_shader->set_code(
            "shader_type sky;\n"
            "uniform samplerCube sky_cubemap : source_color;\n"
            "void sky() {\n"
            "    COLOR = texture(sky_cubemap, EYEDIR).rgb;\n"
            "}\n"
        );
    }

    // Create ShaderMaterial and assign cubemap
    Ref<ShaderMaterial> sky_mat;
    sky_mat.instantiate();
    sky_mat->set_shader(sky_shader);
    sky_mat->set_shader_parameter("sky_cubemap", cubemap);

    if (has_clouds && cloud_tex.is_valid()) {
        sky_mat->set_shader_parameter("cloud_texture", cloud_tex);
        sky_mat->set_shader_parameter("cloud_height", cloud_height);
        sky_mat->set_shader_parameter("scroll_s", cloud_scroll_s);
        sky_mat->set_shader_parameter("scroll_t", cloud_scroll_t);
        sky_mat->set_shader_parameter("is_additive", (bool)cloud_is_additive);
        sky_mat->set_shader_parameter("cloud_time", 0.0f);

        // Store for per-frame animation
        sky_cloud_material = sky_mat;
        sky_cloud_scroll_s = cloud_scroll_s;
        sky_cloud_scroll_t = cloud_scroll_t;
        sky_cloud_time = 0.0;
    } else {
        sky_cloud_material.unref();
        sky_cloud_scroll_s = 0.0f;
        sky_cloud_scroll_t = 0.0f;
        sky_cloud_time = 0.0;
    }

    // Create Sky resource
    Ref<Sky> sky;
    sky.instantiate();
    sky->set_material(sky_mat);
    sky->set_radiance_size(Sky::RADIANCE_SIZE_256);
    // Process mode: always re-render the sky when cloud_time changes
    if (has_clouds && cloud_tex.is_valid()) {
        sky->set_process_mode(Sky::PROCESS_MODE_REALTIME);
    }

    // Update environment to use the skybox
    Ref<Environment> env = world_env->get_environment();
    if (env.is_valid()) {
        env->set_background(Environment::BG_SKY);
        env->set_sky(sky);
        UtilityFunctions::print(
            String("[MoHAA] Skybox loaded: ") + sky_env +
            (has_clouds ? " (6 faces + cloud layer)." : " (6 faces)."));
    }
}

// ──────────────────────────────────────────────
//  Sun flare loading + rendering
// ──────────────────────────────────────────────

void MoHAARunner::load_sun_flare() {
    sun_exists = false;
    sun_flare_initialized = false;
    sun_flare_sprites.clear();
    sun_flare_blend_alpha = 0.0f;

    // ── Parse entity string for sun properties ──
    const char *ents = Godot_BSP_GetEntityString();
    if (!ents || !ents[0]) {
        UtilityFunctions::print("[MoHAA] No entity string — no sun flare.");
        return;
    }

    bool flare_dir_set = false;
    bool world_processed = false;
    bool is_world = false;

    sun_direction[0] = sun_direction[1] = sun_direction[2] = 0.0f;
    sun_color[0] = sun_color[1] = sun_color[2] = 1.0f;
    sun_flare_name[0] = 0;

    // Make a mutable copy since COM_Parse modifies the pointer
    size_t ent_len = strlen(ents);
    char *ent_buf = (char *)malloc(ent_len + 1);
    memcpy(ent_buf, ents, ent_len + 1);
    char *p = ent_buf;

    while (p && *p) {
        char *ret = COM_Parse(&p);
        if (!ret || !ret[0]) break;

        if (ret[0] == '{' || ret[0] == '}') continue;

        if (!world_processed && !strcmp(ret, "classname")) {
            ret = COM_Parse(&p);
            if (!strcmp(ret, "worldspawn")) {
                is_world = true;
            } else {
                world_processed = true;
            }
            continue;
        }

        if (!strcmp(ret, "suncolor") || !strcmp(ret, "sunlight")) {
            if (world_processed) { COM_Parse(&p); continue; }
            ret = COM_Parse(&p);
            sscanf(ret, "%f %f %f", &sun_color[0], &sun_color[1], &sun_color[2]);
            // Normalize to 0-1 range (original multiplies by overbrightMult)
            float max_c = fmaxf(fmaxf(sun_color[0], sun_color[1]), sun_color[2]);
            if (max_c > 1.0f) {
                sun_color[0] /= max_c;
                sun_color[1] /= max_c;
                sun_color[2] /= max_c;
            }
            sun_exists = true;
        } else if (!strcmp(ret, "sundirection")) {
            if (world_processed) { COM_Parse(&p); continue; }
            float dir[3];
            ret = COM_Parse(&p);
            sscanf(ret, "%f %f %f", &dir[0], &dir[1], &dir[2]);
            // AngleVectorsLeft: get forward vector from euler angles
            id_angle_vectors_left(dir, sun_direction, nullptr, nullptr);
            sun_exists = true;
            if (!flare_dir_set) {
                sun_flare_direction[0] = sun_direction[0];
                sun_flare_direction[1] = sun_direction[1];
                sun_flare_direction[2] = sun_direction[2];
            }
        } else if (!strcmp(ret, "sunflaredirection")) {
            if (world_processed) { COM_Parse(&p); continue; }
            float dir[3];
            ret = COM_Parse(&p);
            sscanf(ret, "%f %f %f", &dir[0], &dir[1], &dir[2]);
            id_angle_vectors_left(dir, sun_flare_direction, nullptr, nullptr);
            flare_dir_set = true;
        } else if (!strcmp(ret, "sunflarename")) {
            if (world_processed) { COM_Parse(&p); continue; }
            ret = COM_Parse(&p);
            snprintf(sun_flare_name, sizeof(sun_flare_name), "%s", ret);
        } else {
            COM_Parse(&p);  // Skip value
        }
    }
    ::free(ent_buf);

    if (sun_exists && !sun_flare_name[0]) {
        snprintf(sun_flare_name, sizeof(sun_flare_name), "sun");
    }

    if (!sun_exists) {
        UtilityFunctions::print("[MoHAA] No sun defined in map — no sun flare.");
        return;
    }

    UtilityFunctions::print(
        String("[MoHAA] Sun found: dir=(") + String::num(sun_direction[0], 2) + "," +
        String::num(sun_direction[1], 2) + "," + String::num(sun_direction[2], 2) +
        ") color=(" + String::num(sun_color[0], 2) + "," +
        String::num(sun_color[1], 2) + "," + String::num(sun_color[2], 2) +
        ") flare=" + sun_flare_name);

    // ── Parse global/lensflaredefs.txt for flare definition ──
    if (!strcmp(sun_flare_name, "none")) {
        UtilityFunctions::print("[MoHAA] Sun flare name is 'none' — no flare rendered.");
        return;
    }

    void *flare_raw = nullptr;
    long flare_len = Godot_VFS_ReadFile("global/lensflaredefs.txt", &flare_raw);
    if (flare_len <= 0 || !flare_raw) {
        UtilityFunctions::print("[MoHAA] Could not open global/lensflaredefs.txt — no sun flare.");
        return;
    }

    char *flare_buf = (char *)malloc(flare_len + 1);
    memcpy(flare_buf, flare_raw, flare_len);
    flare_buf[flare_len] = '\0';
    Godot_VFS_FreeFile(flare_raw);

    char *fp = flare_buf;
    bool found_section = false;

    // Find the matching "begin <name>" section
    while (fp && *fp) {
        char *token = COM_ParseExt(&fp, 1);
        if (!token[0]) break;

        if (!Q_stricmp(token, "begin")) {
            token = COM_ParseExt(&fp, 0);
            if (!Q_stricmp(token, sun_flare_name)) {
                found_section = true;
                break;
            }
        }
    }

    if (!found_section) {
        UtilityFunctions::print(
            String("[MoHAA] Flare section '") + sun_flare_name +
            "' not found in lensflaredefs.txt.");
        ::free(flare_buf);
        return;
    }

    // Parse the section
    sun_flare_dot_min = 0.8f;
    sun_flare_fullscale = 0.7f;
    sun_flare_fullfade = 0;
    sun_flare_fullscreen_shader = 0;

    while (fp && *fp) {
        char *token = COM_ParseExt(&fp, 1);
        if (!token[0]) break;

        if (!Q_stricmp(token, "end")) break;

        if (!Q_stricmp(token, "dot_min")) {
            token = COM_ParseExt(&fp, 0);
            if (token[0]) sun_flare_dot_min = (float)atof(token);
        } else if (!Q_stricmp(token, "flare")) {
            SunFlareSprite sprite = {};
            sprite.alphascale = 1.0f;

            token = COM_ParseExt(&fp, 0);
            if (token[0]) sprite.size = (float)atof(token);

            token = COM_ParseExt(&fp, 0);
            if (token[0]) sprite.where = (float)atof(token);

            token = COM_ParseExt(&fp, 0);
            if (token[0]) {
                sprite.shader_handle = Godot_Renderer_RegisterShader(token);
            }

            token = COM_ParseExt(&fp, 0);
            if (token[0]) sprite.alphascale = (float)atof(token);

            sun_flare_sprites.push_back(sprite);
        } else if (!Q_stricmp(token, "fullscale")) {
            token = COM_ParseExt(&fp, 0);
            if (token[0]) sun_flare_fullscale = (float)atof(token);
        } else if (!Q_stricmp(token, "fullscreen")) {
            token = COM_ParseExt(&fp, 0);
            if (token[0]) sun_flare_fullscreen_shader = Godot_Renderer_RegisterShader(token);
        } else if (!Q_stricmp(token, "fullfade")) {
            token = COM_ParseExt(&fp, 0);
            if (token[0]) sun_flare_fullfade = (int)atof(token);
        }
    }

    ::free(flare_buf);

    if (sun_flare_sprites.empty()) {
        UtilityFunctions::print("[MoHAA] No flare sprites defined — no sun flare rendered.");
        return;
    }

    sun_flare_initialized = true;
    UtilityFunctions::print(
        String("[MoHAA] Sun flare loaded: ") + String::num_int64(sun_flare_sprites.size()) +
        " sprites, dot_min=" + String::num(sun_flare_dot_min, 2) +
        ", fullscale=" + String::num(sun_flare_fullscale, 2));

    // ── Create canvas layer for sun flare overlay ──
    if (!sun_flare_canvas) {
        sun_flare_canvas = memnew(CanvasLayer);
        sun_flare_canvas->set_layer(95); // Above world, below HUD (100)
        add_child(sun_flare_canvas);

        sun_flare_control = memnew(Control);
        sun_flare_control->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
        sun_flare_control->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        // Flare sprites use additive blend (GL_ONE GL_ONE) in the original
        // renderer — without this, the rectangular texture edges become
        // visible as hard lines against the sky.
        if (add_canvas_material.is_null()) {
            add_canvas_material.instantiate();
            add_canvas_material->set_blend_mode(CanvasItemMaterial::BLEND_MODE_ADD);
        }
        sun_flare_control->set_material(add_canvas_material);
        sun_flare_canvas->add_child(sun_flare_control);
    }
}

extern "C" {
    void Godot_Renderer_CM_BoxTrace(void *results, const float *start, const float *end, const float *mins, const float *maxs, int model, int brushMask, int cylinder);
}

struct GodotTraceResult {
    int allsolid;
    int startsolid;
    float fraction;
    float endpos[3];
    struct {
        float normal[3];
        float dist;
        uint8_t type;
        uint8_t signbits;
        uint8_t pad[2];
    } plane;
    int surfaceFlags;
    int shaderNum;
    int contents;
    int entityNum;
    int location;
    void *ent;
};

void MoHAARunner::update_sun_flare() {
    if (!sun_exists || !sun_flare_initialized || !camera || !sun_flare_control) return;

    RenderingServer *rs = RenderingServer::get_singleton();
    RID ci = sun_flare_control->get_canvas_item();

    // Clear previous frame's draws
    rs->canvas_item_clear(ci);

    // ── Project sun direction to screen space ──
    // Sun is at camera_pos + sun_flare_direction * large_distance (in id coords)
    float cam_id[3];
    Godot_Renderer_GetViewOrigin(cam_id);

    // Convert sun direction to Godot coordinates
    Vector3 sun_dir_godot = id_to_godot_point(
        sun_flare_direction[0], sun_flare_direction[1], sun_flare_direction[2]);
    sun_dir_godot = sun_dir_godot.normalized();

    // Sun position far away in Godot space
    Vector3 cam_pos = camera->get_global_position();
    Vector3 sun_world_pos = cam_pos + sun_dir_godot * 1000.0f;

    // Check if sun is in front of camera
    Vector3 cam_forward = -camera->get_global_transform().basis.get_column(2);
    float dot = cam_forward.dot(sun_dir_godot);

    if (dot < sun_flare_dot_min) {
        return; // Sun behind camera or below dot threshold
    }

    // Project sun position to screen coordinates
    if (!camera->is_position_behind(sun_world_pos)) {
        Vector2 screen_pos = camera->unproject_position(sun_world_pos);
        Vector2 viewport_size = get_viewport()->get_visible_rect().size;

        // ── Occlusion test via id Tech BSP CM_BoxTrace ──
        // Uses the original quake 3 collision system rather than Godot physics
        bool sun_visible = true;
        GodotTraceResult trace;
        float trace_start[3];
        Godot_Renderer_GetViewOrigin(trace_start);

        float trace_end[3] = {
            trace_start[0] + sun_flare_direction[0] * 10000.0f,
            trace_start[1] + sun_flare_direction[1] * 10000.0f,
            trace_start[2] + sun_flare_direction[2] * 10000.0f
        };
        float mins[3] = {0,0,0};
        float maxs[3] = {0,0,0};

        // CONTENTS_SOLID = 1
        Godot_Renderer_CM_BoxTrace(&trace, trace_start, trace_end, mins, maxs, 0, 1, 0);

        // The original renderer checks for sky surface flag (4). If it hits sky, it's visible.
        // trace.surfaceFlags & 4 (SURF_SKY)
        if (trace.fraction < 1.0f) {
            if ((trace.surfaceFlags & 4) == 0) {
                sun_visible = false;
            }
        }

        if (!sun_visible) {
            return;
        }

        // ── Compute flare alpha based on dot product ──
        float alpha = (dot - sun_flare_dot_min) / (1.0f - sun_flare_dot_min);
        alpha = CLAMP(alpha, 0.0f, 1.0f);

        // Normalize screen position to -1..1 range (matching original renderer)
        float nx = (screen_pos.x / viewport_size.x) * 2.0f - 1.0f;
        float ny = (screen_pos.y / viewport_size.y) * 2.0f - 1.0f;

        // Flare axis direction: from screen center to sun position (and beyond)
        float diff_x = -2.0f * nx;
        float diff_y = -2.0f * ny;

        // ── Draw each flare sprite ──
        for (int i = 0; i < (int)sun_flare_sprites.size(); i++) {
            const SunFlareSprite &sprite = sun_flare_sprites[i];

            // Position along flare axis
            float fx = nx + sprite.where * diff_x;
            float fy = ny + sprite.where * diff_y;

            // Convert from normalized coords back to pixel coords
            float px = (fx + 1.0f) * 0.5f * viewport_size.x;
            float py = (fy + 1.0f) * 0.5f * viewport_size.y;

            // Size in pixels (sprite.size is in normalized coords)
            float size_px = sprite.size * viewport_size.x * 0.5f;

            float sprite_alpha = alpha * sprite.alphascale;

            // Get flare texture
            if (sprite.shader_handle > 0) {
                Ref<ImageTexture> tex = get_shader_texture(sprite.shader_handle);
                if (tex.is_valid()) {
                    Rect2 dst_rect(px - size_px, py - size_px,
                                   size_px * 2.0f, size_px * 2.0f);
                    Color modulate(sun_color[0], sun_color[1], sun_color[2], sprite_alpha);

                    rs->canvas_item_add_texture_rect(ci, dst_rect, tex->get_rid(), false, modulate);
                }
            }
        }

        // ── Fullscreen bloom/blend effect ──
        if (sun_flare_fullscreen_shader > 0) {
            float blend_alpha = alpha * alpha * alpha * alpha * sun_flare_fullscale;
            if (blend_alpha > 0.001f) {
                Ref<ImageTexture> fs_tex = get_shader_texture(sun_flare_fullscreen_shader);
                if (fs_tex.is_valid()) {
                    Rect2 fs_rect(0, 0, viewport_size.x, viewport_size.y);
                    Color fs_mod(1.0f, 1.0f, 1.0f, blend_alpha);
                    rs->canvas_item_add_texture_rect(ci, fs_rect, fs_tex->get_rid(), false, fs_mod);
                }
            }
        }

        sun_flare_last_visible_time = shader_anim_time;
    }
}

// ──────────────────────────────────────────────
//  Wave function evaluation (Phase 141)
// ──────────────────────────────────────────────

// Evaluate a wave function (mirrors renderergl1 EvalWaveForm).
// Returns the wave value for the given function type at the specified time.
static float eval_wave(MohaaWaveFunc func, float base, float amp,
                       float phase, float freq, double time) {
    float t = fmodf((float)(phase + time * freq), 1.0f);
    if (t < 0.0f) t += 1.0f;
    float wave = 0.0f;
    switch (func) {
    case WAVE_SIN:
        wave = sinf(t * 2.0f * (float)M_PI);
        break;
    case WAVE_TRIANGLE:
        wave = (t < 0.5f) ? (4.0f * t - 1.0f) : (-4.0f * t + 3.0f);
        break;
    case WAVE_SQUARE:
        wave = (t < 0.5f) ? 1.0f : -1.0f;
        break;
    case WAVE_SAWTOOTH:
        wave = t;
        break;
    case WAVE_INVERSE_SAWTOOTH:
        wave = 1.0f - t;
        break;
    default:
        wave = sinf(t * 2.0f * (float)M_PI);
        break;
    }
    return base + amp * wave;
}

// ──────────────────────────────────────────────
//  Entity rendering (Phase 7e)
// ──────────────────────────────────────────────

// Entity type constants matching refEntityType_t (tr_types.h)
// RT_MODEL=0, RT_POLY=1, RT_SPRITE=2, RT_BEAM=3, RT_RAIL_CORE=4, etc.
static constexpr int RT_MODEL        = 0;
static constexpr int RT_POLY         = 1;  /* Q3A only — not used by MOHAA cgame */
static constexpr int RT_SPRITE       = 2;
static constexpr int RT_BEAM         = 3;
static constexpr int RT_RAIL_CORE    = 4;  /* Q3A only */
static constexpr int RT_RAIL_RINGS   = 5;  /* Q3A only */
static constexpr int RT_LIGHTNING    = 6;  /* Q3A only */
static constexpr int RT_PORTALSURFACE = 7;

void MoHAARunner::update_entities() {
    if (!game_world) return;

    int ent_count = Godot_Renderer_GetEntityCount();

    // Create entity container node on first use
    if (!entity_root) {
        entity_root = memnew(Node3D);
        entity_root->set_name("Entities");
        game_world->add_child(entity_root);
    }

    // Grow the mesh pool if needed
    while ((int)entity_meshes.size() < ent_count) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String("Entity_") + String::num_int64((int64_t)entity_meshes.size()));
        mi->set_visible(false);
        mi->set_extra_cull_margin(4.0f);
        entity_root->add_child(mi);
        entity_meshes.push_back(mi);
    }

    if ((int)entity_cache_keys.size() < ent_count) {
        entity_cache_keys.resize(ent_count);
    }

    // Cache camera origin for PVS entity culling (id Tech 3 coordinates)
    float pvs_cam_origin[3] = {0, 0, 0};
    if (pvs_current_cluster >= 0) {
        Godot_Renderer_GetViewOrigin(pvs_cam_origin);
    }

    // Update positions for active entities this frame
    for (int i = 0; i < ent_count; i++) {
        float origin[3], axis[9], scale = 1.0f;
        int hModel = 0, entityNumber = 0, renderfx = 0;
        unsigned char rgba[4] = {255, 255, 255, 255};

        int reType = Godot_Renderer_GetEntity(i, origin, axis, &scale,
                                               &hModel, &entityNumber,
                                               rgba, &renderfx);

        MeshInstance3D *mi = entity_meshes[i];

        // Skip non-renderable entities (portals, etc.)
        // RT_SPRITE is handled by the VFX module (Godot_VFX_Update) when available
#ifdef HAS_VFX_MODULE
        if (reType != RT_MODEL && reType != RT_BEAM) {
#else
        if (reType != RT_MODEL && reType != RT_SPRITE && reType != RT_BEAM) {
#endif
            mi->set_visible(false);
            continue;
        }
        // RT_MODEL needs a valid model handle
        if (reType == RT_MODEL && hModel <= 0) {
            mi->set_visible(false);
            continue;
        }



        // RF_THIRD_PERSON    (0x0001): local player body — not culled here (no mirrors in our renderer)
        // RF_FIRST_PERSON   (0x0002): view weapon — route to weapon SubViewport
        // RF_DEPTHHACK      (0x0004): view weapon depth hack — route to weapon SubViewport
        // RF_LIGHTING_ORIGIN (0x0080): use refEntity->lightingOrigin for light sampling (q_shared.h)
        //   NOTE: RF_DONTDRAW (q_shared.h, (1<<7)=0x80) lives in entityState_t.renderfx, NOT
        //   refEntity_t.renderfx.  The cgame filters RF_DONTDRAW entities before calling
        //   R_AddRefEntityToScene, so they never reach this buffer.  Do NOT hide on bit 0x80.

        // NOTE: Entity PVS culling is intentionally NOT performed here.
        // The original MOHAA renderer (tr_main.c::R_AddRefEntityToScene) does
        // NOT PVS-cull entities — PVS is applied only to BSP cluster meshes
        // via update_pvs_visibility().  The server already PVS-culls snapshots
        // before sending them, so double-culling on the client incorrectly hides
        // entities in multiplayer (players at spawn points that are in different
        // PVS leaves from the camera position).

        // Phase 133: Draw distance culling — skip entities beyond the far-plane cull distance.
        // First-person entities (RF_FIRST_PERSON / RF_DEPTHHACK) are always visible.
        // NOTE: Entity frustum culling is handled automatically by Godot's scene tree AABB
        // culling per MeshInstance3D — no manual sphere test is needed or correct here.
        // The original MOHAA renderer (tr_main.c) does NOT manually frustum-cull entities.
#ifdef HAS_DRAW_DISTANCE_MODULE
        if (!(renderfx & 0x06)) {  // Skip for RF_FIRST_PERSON(0x02)|RF_DEPTHHACK(0x04)
            Vector3 ent_pos = id_to_godot_position(origin[0], origin[1], origin[2]);
            float cull_dist = Godot_DrawDistance_GetCullDistance();
            if (cull_dist > 0.0f) {
                Vector3 cam_pos = camera ? camera->get_global_position() : Vector3();
                if (ent_pos.distance_to(cam_pos) > cull_dist) {
                    mi->set_visible(false);
                    continue;
                }
            }
        }
#endif

        // Phase 39 parity: fetch per-entity shader data (shaderTime, shaderTexCoord)
        // In the original renderer, tess.shaderTime = refdef.floatTime - entity.shaderTime.
        // This makes effect animations start at zero when the entity is spawned.
        float ent_shaderTime = 0.0f;
        float ent_shaderTexCoord[2] = {0.0f, 0.0f};
        int   ent_customSkin = 0;
        int   ent_nonNormalizedAxes = 0;
        Godot_Renderer_GetEntityShaderData(i, &ent_shaderTime,
                                           ent_shaderTexCoord, &ent_customSkin,
                                           &ent_nonNormalizedAxes);

        // Compute per-entity effective shader time (in seconds).
        // Mirrors RB_StageIteratorGeneric: tess.shaderTime = floatTime - shaderStartTime.
        double ent_effective_time = shader_anim_time;
        if (ent_shaderTime > 0.0f) {
            ent_effective_time = shader_anim_time - (double)ent_shaderTime;
            if (ent_effective_time < 0.0) ent_effective_time = 0.0;
        }

        // RT_SPRITE: billboard quad at entity origin (Phase 16)
        // MOHAA .spr sprites are sized by image dimensions × scale, NOT by
        // refEntity_t.radius (which is only used for frustum culling).
        // See RB_DrawSprite in tr_sprite.c for the original sizing logic.
        if (reType == RT_SPRITE) {
            float radius = 0.0f, rotation = 0.0f;
            int customShader = 0;
            Godot_Renderer_GetEntitySprite(i, &radius, &rotation, &customShader);

            // Use customShader if set, else extract the registered shader from the sprite model handle
            int spriteShader = (customShader > 0) ? customShader : Godot_Model_GetSpriteShader(hModel);

            // Compute sprite quad half-extents using the MOHAA sizing model:
            //   halfW = (image_width  * 0.5) × entity.scale × shader.spritescale
            //   halfH = (image_height * 0.5) × entity.scale × shader.spritescale
            // All in engine inches, then converted to Godot metres.
            float halfW = 0.0f, halfH = 0.0f;
            float spriteW = 0.0f, spriteH = 0.0f, spriteScale = 1.0f;

            if (hModel > 0 && Godot_Model_GetSpriteDims(hModel, &spriteW, &spriteH, &spriteScale)) {
                // MOHAA .spr model: size from image dimensions
                float combinedScale = (scale > 0.001f ? scale : 1.0f) * spriteScale;
                halfW = (spriteW * 0.5f) * combinedScale * MOHAA_UNIT_SCALE;
                halfH = (spriteH * 0.5f) * combinedScale * MOHAA_UNIT_SCALE;
            } else if (radius > 0.001f) {
                // Q3-style RT_SPRITE fallback: radius IS the half-extent
                halfW = halfH = radius * MOHAA_UNIT_SCALE;
            } else {
                // No model, no radius — skip
                mi->set_visible(false);
                continue;
            }

            if (halfW < 0.001f || halfH < 0.001f) {
                mi->set_visible(false);
                continue;
            }

            // Build a simple quad (2 triangles) — billboard handled by material
            PackedVector3Array gPos;
            PackedVector2Array gUV;
            PackedColorArray   gCol;
            PackedInt32Array   gIdx;
            gPos.resize(4);
            gUV.resize(4);
            gCol.resize(4);
            gIdx.resize(6);

            gPos.set(0, Vector3(-halfW, -halfH, 0.0f));
            gPos.set(1, Vector3( halfW, -halfH, 0.0f));
            gPos.set(2, Vector3( halfW,  halfH, 0.0f));
            gPos.set(3, Vector3(-halfW,  halfH, 0.0f));
            gUV.set(0, Vector2(0, 1));
            gUV.set(1, Vector2(1, 1));
            gUV.set(2, Vector2(1, 0));
            gUV.set(3, Vector2(0, 0));

            Color entCol(rgba[0] / 255.0f, rgba[1] / 255.0f,
                         rgba[2] / 255.0f, rgba[3] / 255.0f);
            gCol.set(0, entCol);
            gCol.set(1, entCol);
            gCol.set(2, entCol);
            gCol.set(3, entCol);

            gIdx.set(0, 0); gIdx.set(1, 1); gIdx.set(2, 2);
            gIdx.set(3, 0); gIdx.set(4, 2); gIdx.set(5, 3);

            Array arrays;
            arrays.resize(Mesh::ARRAY_MAX);
            arrays[Mesh::ARRAY_VERTEX] = gPos;
            arrays[Mesh::ARRAY_TEX_UV] = gUV;
            arrays[Mesh::ARRAY_COLOR]  = gCol;
            arrays[Mesh::ARRAY_INDEX]  = gIdx;

            Ref<ArrayMesh> smesh;
            smesh.instantiate();
            smesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
            mi->set_mesh(smesh);

            // Cached billboard material per shader handle — avoids creating
            // a new StandardMaterial3D + shader props lookup every frame.
            auto sp_it = s_sprite_mat_cache.find(spriteShader);
            if (sp_it == s_sprite_mat_cache.end()) {
                Ref<StandardMaterial3D> smat;
                smat.instantiate();
                smat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
                smat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                smat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                smat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                smat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                smat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);

                // Apply the shader's own blend mode — the engine already
                // parsed the .shader definition correctly.  tr_sprite.c
                // does NOT set any blend mode; GL_State(pStage->stateBits)
                // applies whatever the shader says (additive, alpha, etc.).
                if (spriteShader > 0) {
                    const char *sn = Godot_Renderer_GetShaderName(spriteShader);
                    if (sn && sn[0]) {
                        apply_shader_props_to_material(smat, sn);
                    }
                    // Load texture
                    Ref<ImageTexture> tex = get_shader_texture(spriteShader);
                    if (tex.is_valid()) {
                        smat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                }

                // Re-enforce sprite-specific settings that
                // apply_shader_props_to_material may have overridden:
                smat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
                smat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                smat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                smat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);

                // If the shader was classified OPAQUE but this is a sprite,
                // we still need transparency enabled (at minimum alpha blend)
                // so that vertex colour alpha works.
                if (smat->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED) {
                    smat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                }

                static std::unordered_set<int> logged_active_sprites;
                if (logged_active_sprites.find(spriteShader) == logged_active_sprites.end()) {
                    const char *sn = (spriteShader > 0) ? Godot_Renderer_GetShaderName(spriteShader) : "(none)";
                    const GodotShaderProps *sp_diag = (sn && sn[0]) ? Godot_ShaderProps_Find(sn) : nullptr;
                    auto ha_it = shader_texture_has_alpha.find(spriteShader);
                    bool tex_alpha = (ha_it != shader_texture_has_alpha.end()) && ha_it->second;
                    const char *transp_names[] = {"OPAQUE","ALPHA_TEST","ALPHA_BLEND","ADDITIVE","MULTIPLICATIVE","MULT_INV","ALPHA_INV"};
                    const char *blend_names[] = {"MIX","ADD","SUB","MUL"};
                    int tn = sp_diag ? sp_diag->transparency : -1;
                    int bn = (int)smat->get_blend_mode();
                    UtilityFunctions::print(String("[MoHAA][SPRITE-MAT] shader=#") + String::num_int64(spriteShader) +
                        String(" name='") + String(sn ? sn : "(null)") + String("'") +
                        String(" props=") + String(sp_diag ? "YES" : "NO") +
                        String(" shader_transp=") + String(tn >= 0 && tn < 7 ? transp_names[tn] : "?") +
                        String(" godot_blend=") + String(bn >= 0 && bn < 4 ? blend_names[bn] : "?") +
                        String(" tex_alpha=") + String(tex_alpha ? "yes" : "no"));
                    logged_active_sprites.insert(spriteShader);
                }

                s_sprite_mat_cache[spriteShader] = smat;
                sp_it = s_sprite_mat_cache.find(spriteShader);
            }

            // Apply texture per-frame to support RemapShader and animMap
            if (spriteShader > 0) {
                Ref<ImageTexture> tex = get_shader_texture(spriteShader);
                if (tex.is_valid()) {
                    sp_it->second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                } else {
                    static std::unordered_set<int> logged_missing_sprites;
                    if (logged_missing_sprites.find(spriteShader) == logged_missing_sprites.end()) {
                        const char *sn = Godot_Renderer_GetShaderName(spriteShader);
                        UtilityFunctions::print(String("[MoHAA][SPRITE-TEX-MISS] Sprite shader missing texture! Name: ") + String(sn ? sn : ""));
                        logged_missing_sprites.insert(spriteShader);
                    }
                }
            }

            // Use tint cache to avoid duplicate() per sprite per frame.
            // If RGBA is white (255,255,255,255), reuse the template directly.
            if (rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[3] == 255) {
                mi->set_surface_override_material(0, sp_it->second);
            } else {
                // Quantise RGBA to 4-bit per channel for cache key
                uint16_t rq = (uint16_t)(rgba[0] >> 4);
                uint16_t gq = (uint16_t)(rgba[1] >> 4);
                uint16_t bq = (uint16_t)(rgba[2] >> 4);
                uint16_t aq = (uint16_t)(rgba[3] >> 4);
                uint64_t tint_key = ((uint64_t)spriteShader << 16) |
                    ((uint64_t)rq << 12) | ((uint64_t)gq << 8) |
                    ((uint64_t)bq << 4) | (uint64_t)aq;
                auto stc_it = s_sprite_tint_cache.find(tint_key);
                if (stc_it != s_sprite_tint_cache.end()) {
                    if (spriteShader > 0) {
                        Ref<ImageTexture> tex = get_shader_texture(spriteShader);
                        if (tex.is_valid()) stc_it->second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                    mi->set_surface_override_material(0, stc_it->second);
                } else {
                    Ref<StandardMaterial3D> inst_mat = sp_it->second->duplicate();
                    inst_mat->set_albedo(Color(rgba[0] / 255.0f, rgba[1] / 255.0f,
                                               rgba[2] / 255.0f, rgba[3] / 255.0f));
                    s_sprite_tint_cache[tint_key] = inst_mat;
                    mi->set_surface_override_material(0, inst_mat);
                }
            }

            // Position sprite at entity origin
            Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
            mi->set_global_transform(Transform3D(Basis(), pos));

            // ── Parent management for sprites ──
            // Sprites must be in the correct parent (entity_root vs weapon_root)
            // based on their renderfx flags.  Without this, a MeshInstance that
            // was previously in weapon_root (from a prior frame's RT_MODEL) stays
            // there when reused as a sprite, causing SubViewport compositing issues.
            // ALL first-person sprites must stay in entity_root.  The weapon
            // SubViewport's alpha compositing breaks both additive and alpha
            // blending: dark pixels get alpha=1.0, and the TextureRect
            // compositing treats them as opaque black.  Sprites are always
            // visual effects and rarely clip into world geometry, so losing
            // the depth hack is acceptable.
            if (weapon_root) {
                Node *target = entity_root;
                Node *cur = mi->get_parent();
                if (cur && cur != target) {
                    cur->remove_child(mi);
                    target->add_child(mi);
                }
            }

            mi->set_visible(true);
            continue;
        }

        // ── Phase 23: RT_BEAM — line between two points (e.g. tracers, lasers) ──
        if (reType == RT_BEAM) {
            float from[3], to[3], diameter = 1.0f;
            Godot_Renderer_GetEntityBeam(i, from, to, &diameter);

            Vector3 p1 = id_to_godot_position(from[0], from[1], from[2]);
            Vector3 p2 = id_to_godot_position(to[0], to[1], to[2]);
            Vector3 dir = p2 - p1;
            float len = dir.length();
            if (len < 0.001f) {
                mi->set_visible(false);
                continue;
            }

            // Camera-facing beam quad: the side vector is computed from the
            // cross product of beam direction and camera-to-beam vector,
            // matching the original engine's beam rendering approach.
            float halfW = (diameter > 0 ? diameter : 1.0f) * MOHAA_UNIT_SCALE * 0.5f;
            Vector3 beam_mid = (p1 + p2) * 0.5f;
            Vector3 cam_to_beam = beam_mid - camera->get_global_position();
            Vector3 side = dir.normalized().cross(cam_to_beam.normalized());
            if (side.length_squared() < 0.001f) {
                // Fallback when camera is aligned with beam direction
                side = dir.normalized().cross(Vector3(0, 1, 0));
                if (side.length_squared() < 0.001f) {
                    side = dir.normalized().cross(Vector3(1, 0, 0));
                }
            }
            side = side.normalized() * halfW;

            PackedVector3Array gPos;
            PackedVector2Array gUV;
            PackedInt32Array   gIdx;
            gPos.resize(4);
            gUV.resize(4);
            gIdx.resize(6);
            gPos.set(0, p1 - side);
            gPos.set(1, p1 + side);
            gPos.set(2, p2 + side);
            gPos.set(3, p2 - side);
            gUV.set(0, Vector2(0, 0)); gUV.set(1, Vector2(1, 0));
            gUV.set(2, Vector2(1, 1)); gUV.set(3, Vector2(0, 1));
            gIdx.set(0, 0); gIdx.set(1, 1); gIdx.set(2, 2);
            gIdx.set(3, 0); gIdx.set(4, 2); gIdx.set(5, 3);

            Array arrays;
            arrays.resize(Mesh::ARRAY_MAX);
            arrays[Mesh::ARRAY_VERTEX] = gPos;
            arrays[Mesh::ARRAY_TEX_UV] = gUV;
            arrays[Mesh::ARRAY_INDEX]  = gIdx;

            Ref<ArrayMesh> bmesh;
            bmesh.instantiate();
            bmesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
            mi->set_mesh(bmesh);

            // Cached beam material per shader handle — avoids creating
            // a new StandardMaterial3D + texture load + shader props lookup every frame.
            int customShader = 0;
            Godot_Renderer_GetEntitySprite(i, nullptr, nullptr, &customShader);
            int beamShader = (customShader > 0) ? customShader : hModel;

            auto bm_it = s_beam_mat_cache.find(beamShader);
            if (bm_it == s_beam_mat_cache.end()) {
                Ref<StandardMaterial3D> bmat;
                bmat.instantiate();
                bmat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                bmat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                bmat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                bmat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);

                bool beam_props_found = false;
                if (beamShader > 0) {
                    const char *sn = Godot_Renderer_GetShaderName(beamShader);
                    if (sn && sn[0]) {
                        const GodotShaderProps *sp = Godot_ShaderProps_Find(sn);
                        if (sp) {
                            beam_props_found = true;
                        }
                        apply_shader_props_to_material(bmat, sn);
                    }
                }

                // Beams without shader definitions: check texture alpha
                // for additive blend fallback (same logic as sprites).
                if (!beam_props_found && beamShader > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(beamShader);
                    if (tex.is_valid()) {
                        bmat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                    auto ha_it = shader_texture_has_alpha.find(beamShader);
                    bool tex_has_alpha = (ha_it != shader_texture_has_alpha.end()) && ha_it->second;
                    if (!tex_has_alpha) {
                        bmat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                    }
                }
                s_beam_mat_cache[beamShader] = bmat;
                bm_it = s_beam_mat_cache.find(beamShader);
            }

            // Apply texture per-frame to support RemapShader and animMap
            if (beamShader > 0) {
                Ref<ImageTexture> tex = get_shader_texture(beamShader);
                if (tex.is_valid()) {
                    bm_it->second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                }
            }

            // Use tint cache to avoid duplicate() per beam per frame.
            if (rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[3] == 255) {
                mi->set_surface_override_material(0, bm_it->second);
            } else {
                uint16_t rq = (uint16_t)(rgba[0] >> 4);
                uint16_t gq = (uint16_t)(rgba[1] >> 4);
                uint16_t bq = (uint16_t)(rgba[2] >> 4);
                uint16_t aq = (uint16_t)(rgba[3] >> 4);
                uint64_t tint_key = ((uint64_t)beamShader << 16) |
                    ((uint64_t)rq << 12) | ((uint64_t)gq << 8) |
                    ((uint64_t)bq << 4) | (uint64_t)aq;
                auto btc_it = s_beam_tint_cache.find(tint_key);
                if (btc_it != s_beam_tint_cache.end()) {
                    if (beamShader > 0) {
                        Ref<ImageTexture> tex = get_shader_texture(beamShader);
                        if (tex.is_valid()) btc_it->second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                    mi->set_surface_override_material(0, btc_it->second);
                } else {
                    Ref<StandardMaterial3D> inst_bmat = bm_it->second->duplicate();
                    inst_bmat->set_albedo(Color(rgba[0] / 255.0f, rgba[1] / 255.0f,
                                                rgba[2] / 255.0f, rgba[3] / 255.0f));
                    s_beam_tint_cache[tint_key] = inst_bmat;
                    mi->set_surface_override_material(0, inst_bmat);
                }
            }
            // Beam vertices are already in world space — use identity transform
            mi->set_global_transform(Transform3D());

            // ── Parent management for beams ──
            // ALL first-person beams go to entity_root — same reasoning as
            // sprites: the SubViewport alpha compositing breaks additive/
            // alpha blending.  Beams are visual effects that rarely clip.
            if (weapon_root) {
                Node *target = entity_root;
                Node *cur = mi->get_parent();
                if (cur && cur != target) {
                    cur->remove_child(mi);
                    target->add_child(mi);
                }
            }

            mi->set_visible(true);
            continue;
        }

        bool is_first_person = (renderfx & 0x02) != 0;  // RF_FIRST_PERSON
        bool is_depthhack    = (renderfx & 0x04) != 0;  // RF_DEPTHHACK

        EntityCacheKey key { hModel, reType, 0, renderfx };
        bool same_key = (i < (int)entity_cache_keys.size() && entity_cache_keys[i] == key);

        // When this entity slot changes model/type, clear stale surface
        // override materials.  Otherwise a TIKI model's overrides persist
        // when the slot is reused for a brush entity (or vice versa),
        // causing wrong textures / flickering.
        if (!same_key && mi->get_mesh().is_valid()) {
            int prev_sc = mi->get_mesh()->get_surface_count();
            for (int s = 0; s < prev_sc; s++) {
                mi->set_surface_override_material(s, Ref<Material>());
            }
        }

        // Try to get the actual skeletal model mesh from cache
        int modType = Godot_Model_GetType(hModel);

        if (modType == 1 /* GR_MOD_BRUSH */) {
            // ── Brush model (door, mover, platform, etc.) ──
            // Extract submodel number from name (e.g. "*5" → 5)
            const char *modName = Godot_Model_GetName(hModel);
            int subIdx = 0;
            if (modName && modName[0] == '*') {
                subIdx = atoi(modName + 1);
            }

            Ref<ArrayMesh> bmesh = Godot_BSP_GetBrushModelMesh(subIdx);
            if (bmesh.is_valid() && mi->get_mesh() != bmesh) {
                mi->set_mesh(bmesh);
                // Materials are already baked into the ArrayMesh surfaces
                // by batches_to_array_mesh() — no override needed.
            } else if (!bmesh.is_valid()) {
                // Brush model mesh not available — skip display
                static std::unordered_set<int> logged_missing_bmodels;
                if (logged_missing_bmodels.find(subIdx) == logged_missing_bmodels.end()) {
                    logged_missing_bmodels.insert(subIdx);
                    UtilityFunctions::print(String("[MoHAA] Entity brush model *") +
                        String::num_int64(subIdx) + " has no mesh — hiding entity");
                }
                mi->set_visible(false);
                continue;
            }
        } else {
            // ── Skeletal (TIKI) model ──
            // Try to get the cached bind-pose model (may be null for
            // dynamically registered models like FPS weapon TIKIs).
            const GodotSkelModelCache::CachedModel *cached =
                GodotSkelModelCache::get().get_model(hModel);

            // ── Build / cache materials for this model (one-time per skinNum) ──
            // Materials are built from the cached model's surface shader names.
            // If the cache has no model, build materials from TIKI data directly.
            // Cache key = hModel | (skinNum << 20) to match tr_model.cpp's
            // hShader[skinNum + (bsurf & 3)] skin-variant selection logic.
            unsigned char ent_surfaces[32] = {};
            int ent_skinNum = 0;
            Godot_Renderer_GetEntitySurfaces(i, ent_surfaces, &ent_skinNum);
            int mat_key = hModel | (ent_skinNum << 20);

            auto &mat_cache = tiki_mat_cache;
            if (mat_cache.find(mat_key) == mat_cache.end()) {
                auto &entry = mat_cache[mat_key];
                auto &mats = entry.mats;
                auto &flat_indices = entry.flat_surf_idx;

                // Enumerate surfaces from TIKI with skinNum-aware shader selection.
                // Mirrors tr_model.cpp::R_AddSkelSurfaces: shader slot = skinNum + (bsurf & 3).
                // flat_indices tracks the raw TIKI surface index per godot surface so that
                // per-entity surface hide flags (MDL_SURFACE_NODRAW bit 2) can be applied.
                int surf_total = 0;
                std::vector<String> surf_shader_names;

                {
                    void *tiki_for_mats = Godot_Model_GetTikiPtr(hModel);
                    if (tiki_for_mats) {
                        int meshCount = Godot_Skel_GetMeshCount(tiki_for_mats);
                        int flat_idx = 0;
                        for (int m = 0; m < meshCount; m++) {
                            int sc = Godot_Skel_GetSurfaceCount(tiki_for_mats, m);
                            for (int s = 0; s < sc; s++, flat_idx++) {
                                int nv = 0, nt = 0;
                                char sh[64] = {0};
                                Godot_Skel_GetSurfaceInfo(tiki_for_mats, m, s,
                                    &nv, &nt, nullptr, 0, nullptr, 0);
                                if (nv > 0 && nt > 0) {
                                    // Resolve skin slot: skinNum + per-surface variant bits (0-3)
                                    int bsurf_bits = (flat_idx < 32) ? (ent_surfaces[flat_idx] & 3) : 0;
                                    int iShaderNum = ent_skinNum + bsurf_bits;
                                    Godot_Skel_GetSurfaceShaderForSkin(tiki_for_mats, m, s,
                                        iShaderNum, sh, sizeof(sh));
                                    surf_shader_names.push_back(String(sh));
                                    flat_indices.push_back(flat_idx);
                                    surf_total++;
                                }
                            }
                        }
                    } else if (cached && !cached->lod_meshes.empty()) {
                        // Fallback: TIKI ptr unavailable — use cached shader names (skin 0 only)
                        surf_total = (int)cached->surfaces.size();
                        for (int s = 0; s < surf_total; s++) {
                            surf_shader_names.push_back(cached->surfaces[s].shader_name);
                            flat_indices.push_back(s);
                        }
                    }
                }

                for (int s = 0; s < surf_total; s++) {
                    Ref<StandardMaterial3D> mat;
                    mat.instantiate();
                    mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
                    /* Shading mode depends on the current rendering mode:
                     *  r_shadows 0 (UNSHADED): OpenMOHAA lights entities via the baked
                     *    Light Grid (albedo multiplier). No Godot dynamic lights active.
                     *  r_shadows 1 (PER_PIXEL): entities receive sun + OmniLight3D
                     *    (muzzle flashes, explosions) and cast shadows onto BSP floors. */
                    mat->set_shading_mode(cached_entity_shadow_mode == 1
                        ? BaseMaterial3D::SHADING_MODE_PER_PIXEL
                        : BaseMaterial3D::SHADING_MODE_UNSHADED);

#ifdef GODOT_DEBUG_WHITE_ENTITIES
                    // DEBUG: Force all TIKI entity surfaces to opaque white so models
                    // are visible regardless of texture/shader pipeline issues.
                    mat->set_albedo(Color(1, 1, 1, 1));
                    (void)surf_shader_names[s]; // suppress unused warning
#else
                    const String &shader_name = surf_shader_names[s];
                    bool found_tex = false;

                    if (!shader_name.is_empty()) {
                        CharString cs = shader_name.ascii();
                        int shaderHandle = Godot_Renderer_RegisterShader(cs.get_data());
                        if (shaderHandle > 0) {
                            Ref<ImageTexture> tex = get_shader_texture(shaderHandle);
                            if (tex.is_valid()) {
                                mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                                found_tex = true;
                            }
                        }
                    }

                    if (!found_tex) {
                        mat->set_albedo(Color(0.6, 0.6, 0.6, 1.0));
                        static std::unordered_set<std::string> logged_missing_tiki_tex;
                        if (!shader_name.is_empty() && logged_missing_tiki_tex.find(shader_name.ascii().get_data()) == logged_missing_tiki_tex.end()) {
                            UtilityFunctions::print(String("[MoHAA][TIKI-TEX-MISS] TIKI surface missing texture! Shader: ") + shader_name);
                            logged_missing_tiki_tex.insert(shader_name.ascii().get_data());
                        }
                    }

                    if (!shader_name.is_empty()) {
                        CharString cs = shader_name.ascii();
                        apply_shader_props_to_material(mat, cs.get_data());
                        mat->set_meta("shader_name", Variant(shader_name));
                    }
#endif // GODOT_DEBUG_WHITE_ENTITIES

                    mats.push_back(mat);
                }
            }

            // ── Phase 59: LOD Selection ──
            int lodLevel = 0;
            // First-person weapons/hands (RF_FIRST_PERSON) are always LOD 0.
            if (!is_first_person) {
                // Determine `distance` from camera to entity origin
                Vector3 camPos = camera->get_global_position();
                Vector3 entPos = id_to_godot_position(origin[0], origin[1], origin[2]);
                // Convert Godot meters back to id Tech 3 inches for LOD thresholds
                float distInches = camPos.distance_to(entPos) * (1.0f / MOHAA_UNIT_SCALE);

                // Adjust for FOV. engine uses: distance *= (90.0f / fov_x)
                float fov_x = camera->get_fov();
                distInches *= (90.0f / fov_x);

                // Apply r_lodbias (from CVar)
                int lodbias = Godot_Cvar_VariableIntegerValue("r_lodbias");

                void *tikiForLod = Godot_Model_GetTikiPtr(hModel);
                if (tikiForLod) {
                    lodLevel = Godot_Skel_SelectLodLevel(tikiForLod, 0, distInches);
                    lodLevel += lodbias;
                    if (lodLevel < 0) lodLevel = 0;
                }
            }

            // ── CPU skinning — works independently of bind-pose cache ──
            void *tikiPtr = nullptr;
            int entNum = 0;
            float actionWeight = 0, entScale = 1.0f;
            alignas(8) char frameInfoBuf[256];
            int boneTagBuf[5];
            float boneQuatBuf[20]; /* 5 × 4 floats */

            bool has_anim = Godot_Renderer_GetEntityAnim(
                i, &tikiPtr, &entNum,
                frameInfoBuf, boneTagBuf, boneQuatBuf,
                &actionWeight, &entScale) != 0;

            Ref<ArrayMesh> skinned_mesh;

            if (has_anim && tikiPtr) {
                // Phase 60: Compute FNV-1a hash of animation state to
                // skip mesh rebuild when the pose hasn't changed.
                // Include LOD level in the hash since different LODs need different meshes!
                uint64_t anim_hash = 14695981039346656037ULL;
                auto fnv_bytes = [&anim_hash](const void *p, size_t n) {
                    const unsigned char *b = (const unsigned char *)p;
                    for (size_t j = 0; j < n; j++) {
                        anim_hash ^= b[j];
                        anim_hash *= 1099511628211ULL;
                    }
                };
                fnv_bytes(frameInfoBuf, sizeof(frameInfoBuf));
                fnv_bytes(boneTagBuf, sizeof(boneTagBuf));
                fnv_bytes(boneQuatBuf, sizeof(boneQuatBuf));
                fnv_bytes(&actionWeight, sizeof(actionWeight));
                fnv_bytes(&hModel, sizeof(hModel));
                fnv_bytes(&lodLevel, sizeof(lodLevel));

                // ENTITYNUM_NONE (1023) is shared by many unrelated temp
                // entities (debris, particles, effects).  Caching by entNum
                // would thrash — always rebuild these.
                bool skip_cache = (entNum == 1023 || entNum < 0);

                // Check cache: if animation state & LOD unchanged, reuse mesh
                if (!skip_cache) {
                    auto cache_it = skel_mesh_cache.find(entNum);
                    if (cache_it != skel_mesh_cache.end() &&
                        cache_it->second.anim_hash == anim_hash &&
                        cache_it->second.mesh != nullptr) {
                        skinned_mesh = cache_it->second.mesh;
                    }
                }
                if (!skinned_mesh.is_valid()) {
                    int boneCount = 0;
                    int *morphCache = nullptr;
                    int morphCount = 0;
                    void *boneCache = Godot_Skel_PrepareBones(
                        tikiPtr, entNum,
                        (const void *)frameInfoBuf, boneTagBuf,
                        (const float *)boneQuatBuf,
                        actionWeight, &boneCount,
                        &morphCache, &morphCount);

                    if (boneCache && boneCount > 0) {
                        skinned_mesh.instantiate();
                        int meshCount = Godot_Skel_GetMeshCount(tikiPtr);
                        float tikiScale = Godot_Skel_GetScale(tikiPtr);

                        for (int mesh = 0; mesh < meshCount; mesh++) {
                            int surfCount = Godot_Skel_GetSurfaceCount(tikiPtr, mesh);
                            for (int surf = 0; surf < surfCount; surf++) {
                                int lodVertLimit = Godot_Skel_GetLodVertexLimit(tikiPtr, mesh, surf, lodLevel);

                                int numVerts = 0, numTris = 0;
                                Godot_Skel_GetSurfaceInfo(tikiPtr, mesh, surf,
                                    &numVerts, &numTris,
                                    nullptr, 0, nullptr, 0);
                                if (numVerts <= 0 || numTris <= 0) continue;

                                float *positions = (float *)malloc(numVerts * 3 * sizeof(float));
                                float *normals   = (float *)malloc(numVerts * 3 * sizeof(float));
                                float *texcoords = (float *)malloc(numVerts * 2 * sizeof(float));
                                int   *indices   = (int *)malloc(numTris * 3 * sizeof(int));

                                if (!positions || !normals || !texcoords || !indices) {
                                    ::free(positions); ::free(normals);
                                    ::free(texcoords); ::free(indices);
                                    continue;
                                }

                                // Get skinned positions + normals (cap at LOD vertex limit)
                                if (!Godot_Skel_SkinSurface(tikiPtr, mesh, surf,
                                        boneCache, boneCount,
                                        positions, normals, lodVertLimit,
                                        morphCache, morphCount)) {
                                    ::free(positions); ::free(normals);
                                    ::free(texcoords); ::free(indices);
                                    continue;
                                }

                                // Get UVs + indices from static data
                                Godot_Skel_GetSurfaceVertices(tikiPtr, mesh, surf,
                                    nullptr, nullptr, texcoords);
                                Godot_Skel_GetSurfaceIndices(tikiPtr, mesh, surf,
                                    indices);

                                int outNumVerts = numVerts;
                                int outNumTris  = numTris;
                                int *outIndices = indices;

                                // Phase 59: Apply LOD index collapse if not LOD 0
                                if (lodLevel > 0 && lodVertLimit >= 0 && lodVertLimit < numVerts) {
                                    int *collapsedIndices = (int *)malloc(numTris * 3 * sizeof(int));
                                    if (collapsedIndices) {
                                        if (Godot_Skel_BuildLodMesh(tikiPtr, mesh, surf, lodVertLimit,
                                                                     positions, normals, texcoords, numVerts,
                                                                     indices, numTris, tikiScale,
                                                                     collapsedIndices, &outNumTris)) {
                                            outIndices = collapsedIndices;
                                            outNumVerts = lodVertLimit;
                                        } else {
                                            ::free(collapsedIndices);
                                        }
                                    }
                                }

                                // Skip surfaces that collapsed to nothing at this LOD
                                if (outNumVerts <= 0 || outNumTris <= 0) {
                                    ::free(positions);
                                    ::free(normals);
                                    ::free(texcoords);
                                    if (outIndices != indices) ::free(outIndices);
                                    ::free(indices);
                                    continue;
                                }

                                // Build Godot arrays with coord conversion
                                PackedVector3Array gPos, gNrm;
                                PackedVector2Array gUVs;
                                PackedInt32Array   gIdx;
                                gPos.resize(outNumVerts);
                                gNrm.resize(outNumVerts);
                                gUVs.resize(outNumVerts);
                                gIdx.resize(outNumTris * 3);

                                for (int v = 0; v < outNumVerts; v++) {
                                    Vector3 p = id_to_godot_point(
                                        positions[v*3+0],
                                        positions[v*3+1],
                                        positions[v*3+2])
                                        * tikiScale * MOHAA_UNIT_SCALE;
                                    Vector3 n = id_to_godot_point(
                                        normals[v*3+0],
                                        normals[v*3+1],
                                        normals[v*3+2]);
                                    if (n.length_squared() > 0.001f)
                                        n = n.normalized();

                                    gPos.set(v, p);
                                    gNrm.set(v, n);
                                    gUVs.set(v, Vector2(
                                        texcoords[v*2+0],
                                        texcoords[v*2+1]));
                                }

                                // Indices as-is — det(id_to_godot_point) = +1, winding preserved
                                for (int t = 0; t < outNumTris; t++) {
                                    gIdx.set(t*3+0, outIndices[t*3+0]);
                                    gIdx.set(t*3+1, outIndices[t*3+1]);
                                    gIdx.set(t*3+2, outIndices[t*3+2]);
                                }

                                Array arrays;
                                arrays.resize(Mesh::ARRAY_MAX);
                                arrays[Mesh::ARRAY_VERTEX] = gPos;
                                arrays[Mesh::ARRAY_NORMAL] = gNrm;
                                arrays[Mesh::ARRAY_TEX_UV] = gUVs;
                                arrays[Mesh::ARRAY_INDEX]  = gIdx;

                                if (gPos.size() > 0 && gIdx.size() >= 3) {
                                    skinned_mesh->add_surface_from_arrays(
                                        Mesh::PRIMITIVE_TRIANGLES, arrays);
                                }

                                ::free(positions);
                                ::free(normals);
                                ::free(texcoords);
                                ::free(indices);
                                if (outIndices != indices) {
                                    ::free(outIndices);
                                }
                            }
                        }

                        ::free(boneCache);
                        ::free(morphCache);
                    }

                    // Phase 60: Cache the newly built skinned mesh
                    if (!skip_cache && skinned_mesh.is_valid() && skinned_mesh->get_surface_count() > 0) {
                        auto &entry = skel_mesh_cache[entNum];
                        entry.anim_hash = anim_hash;
                        entry.mesh = skinned_mesh;
                        entry.mesh_surfaces = skinned_mesh->get_surface_count();
                    } else if (!skip_cache && (!skinned_mesh.is_valid() || skinned_mesh->get_surface_count() == 0)) {
                        // PrepareBones failed or produced empty mesh — reuse
                        // the stale cache entry rather than hiding the entity.
                        auto stale_it = skel_mesh_cache.find(entNum);
                        if (stale_it != skel_mesh_cache.end() && stale_it->second.mesh != nullptr) {
                            skinned_mesh = stale_it->second.mesh;
                        }
                    }
                }  // end cache miss rebuild
            }

            // Use skinned mesh if available, else cached bind pose, else hide
            bool mesh_changed = false;

            if (skinned_mesh.is_valid() &&
                skinned_mesh->get_surface_count() > 0) {
                mi->set_mesh(skinned_mesh);
                mesh_changed = true;

                static bool logged_skin = false;
                if (!logged_skin) {
                    UtilityFunctions::print(
                        String("[MoHAA] First CPU-skinned entity rendered (") +
                        String::num_int64(skinned_mesh->get_surface_count()) +
                        String(" surfaces)."));
                    logged_skin = true;
                }
            } else if (cached && !cached->lod_meshes.empty()) {
                // Determine clamped LOD index for the static array
                int cacheLodIdx = lodLevel;
                if (cacheLodIdx >= (int)cached->lod_meshes.size()) {
                    cacheLodIdx = (int)cached->lod_meshes.size() - 1;
                }

                Ref<ArrayMesh> lod_mesh = cached->lod_meshes[cacheLodIdx];
                if (lod_mesh.is_valid() && mi->get_mesh() != lod_mesh) {
                    mi->set_mesh(lod_mesh);
                    mesh_changed = true;
                }
            } else {
                // No mesh available — parity with OpenMOHAA: do not render
                {
                    static std::unordered_map<int, int> s_no_mesh_log;
                    if (s_no_mesh_log[hModel] < 3) {
                        s_no_mesh_log[hModel]++;
                        const char *nm = Godot_Model_GetName(hModel);
                        void *tp = Godot_Model_GetTikiPtr(hModel);
                        UtilityFunctions::print(
                            String("[MoHAA][NO-MESH] hModel=") + String::num_int64(hModel) +
                            " name=" + String(nm ? nm : "?") +
                            " hasAnim=" + String(has_anim ? "Y" : "N") +
                            " tikiPtr=" + String(tp ? "Y" : "N") +
                            " modType=" + String::num_int64(modType) +
                            " entNum=" + String::num_int64(entityNumber));
                    }
                }

#ifdef GODOT_DEBUG_WHITE_ENTITIES
                // DEBUG: show a tiny orange box at NO-MESH entity positions
                // so we can see the entity is present even with no TIKI mesh.
                {
                    Ref<ArrayMesh> dbg_box;
                    dbg_box.instantiate();
                    float hs = 0.15f;  // half-size in metres
                    PackedVector3Array bv;
                    bv.resize(8);
                    bv.set(0, Vector3(-hs,-hs,-hs)); bv.set(1, Vector3( hs,-hs,-hs));
                    bv.set(2, Vector3(-hs, hs,-hs)); bv.set(3, Vector3( hs, hs,-hs));
                    bv.set(4, Vector3(-hs,-hs, hs)); bv.set(5, Vector3( hs,-hs, hs));
                    bv.set(6, Vector3(-hs, hs, hs)); bv.set(7, Vector3( hs, hs, hs));
                    PackedInt32Array bi;
                    bi.resize(36);
                    int faces[36] = {0,1,3,0,3,2, 4,6,7,4,7,5,
                                     0,4,5,0,5,1, 2,3,7,2,7,6,
                                     0,2,6,0,6,4, 1,5,7,1,7,3};
                    for (int bi2=0;bi2<36;bi2++) bi.set(bi2, faces[bi2]);
                    Array barr; barr.resize(Mesh::ARRAY_MAX);
                    barr[Mesh::ARRAY_VERTEX] = bv;
                    barr[Mesh::ARRAY_INDEX]  = bi;
                    dbg_box->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, barr);
                    Ref<StandardMaterial3D> bmat; bmat.instantiate();
                    bmat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                    bmat->set_albedo(Color(1.0f, 0.4f, 0.0f, 1.0f));  // orange
                    bmat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                    dbg_box->surface_set_material(0, bmat);
                    mi->set_mesh(dbg_box);
                    // Don't continue — fall through to apply transform and set_visible(true)
                    mesh_changed = true;
                }
#else
                mi->set_visible(false);
                continue; // Skip material application and drawing
#endif
            }

            // Apply cached materials (after set_mesh which clears overrides).
            // If customShader is set, it overrides all surface shaders
            // (matches MOHAA renderer: refEntity_t.customShader in tr_local.h).
            int entCustomShader = 0;
            Godot_Renderer_GetEntitySprite(i, nullptr, nullptr, &entCustomShader);
            if (entCustomShader > 0) {
                auto cs_it = s_sprite_mat_cache.find(entCustomShader);
                if (cs_it == s_sprite_mat_cache.end()) {
                    Ref<StandardMaterial3D> csmat;
                    csmat.instantiate();
                    csmat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                    csmat->set_cull_mode(BaseMaterial3D::CULL_BACK);
                    Ref<ImageTexture> tex = get_shader_texture(entCustomShader);
                    if (tex.is_valid()) {
                        csmat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                    // Apply the shader's real blend mode from the engine data.
                    // The engine's R_FindShader() already parsed the .shader
                    // definition; just use that — no custom heuristics needed.
                    const char *csn = Godot_Renderer_GetShaderName(entCustomShader);
                    if (csn && csn[0]) {
                        apply_shader_props_to_material(csmat, csn);
                        csmat->set_meta("shader_name", Variant(String(csn)));
                    }
                    s_sprite_mat_cache[entCustomShader] = csmat;
                    cs_it = s_sprite_mat_cache.find(entCustomShader);
                }
                int sc = mi->get_mesh().is_valid() ? mi->get_mesh()->get_surface_count() : 0;
                for (int s = 0; s < sc; s++) {
                    mi->set_surface_override_material(s, cs_it->second);
                }
            } else if (mesh_changed && mat_cache.find(mat_key) != mat_cache.end()) {
                auto &entry = mat_cache[mat_key];
                int sc = mi->get_mesh().is_valid()
                       ? mi->get_mesh()->get_surface_count() : 0;
                for (int s = 0; s < (int)entry.mats.size() && s < sc; s++) {
                    mi->set_surface_override_material(s, entry.mats[s]);
                }
                // Apply MDL_SURFACE_NODRAW (TIKI_SURF_NODRAW = bit 2) per-entity hide flag.
                // Mirrors tr_model.cpp::R_AddSkelSurfaces: if (*bsurf & 4) continue.
                // Skip for RF_FIRST_PERSON entities: the cgame may incorrectly set
                // NODRAW on all FPS surfaces due to a state sync issue in our
                // GDExtension environment (EF_UNARMED or STAT flags stuck).
                // Zoom-based hiding is handled below via Godot_Client_GetPlayerZoom().
                if (!is_first_person) {
                    static Ref<StandardMaterial3D> s_nodraw_mat;
                    if (!s_nodraw_mat.is_valid()) {
                        s_nodraw_mat.instantiate();
                        s_nodraw_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                        s_nodraw_mat->set_albedo(Color(0.0f, 0.0f, 0.0f, 0.0f));
                        s_nodraw_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                        s_nodraw_mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                    }
                    for (int s = 0; s < (int)entry.flat_surf_idx.size() && s < sc; s++) {
                        int fi = entry.flat_surf_idx[s];
                        if (fi >= 0 && fi < 32 && (ent_surfaces[fi] & 4)) {
                            mi->set_surface_override_material(s, s_nodraw_mat);
                        }
                    }
                }
                // applying MDL_SURFACE_NODRAW.
                for (auto &m : active_mirrors) {
                    if (m.mesh_instance == mi && m.override_mat.is_valid()) {
                        mi->set_surface_override_material(m.surface_idx, m.override_mat);
                    }
                }
            }
        }  // end else (TIKI model)

        // Position: convert id→Godot
        Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);

        if (modType == 1 /* GR_MOD_BRUSH */) {
            // Brush model vertices are at absolute BSP world coordinates.
            // The entity's origin is an offset from the default position.
            // Use identity basis (no scale/rotation) + position offset.
            float *fwd = &axis[0];
            float *lft = &axis[3];
            float *up  = &axis[6];

            Vector3 forward_g = id_to_godot_point(fwd[0], fwd[1], fwd[2]);
            Vector3 left_g    = id_to_godot_point(lft[0], lft[1], lft[2]);
            Vector3 up_g      = id_to_godot_point(up[0],  up[1],  up[2]);

            Vector3 right_g = -left_g;
            Vector3 back_g  = -forward_g;

            Basis basis(right_g, up_g, back_g);
            mi->set_global_transform(Transform3D(basis, pos));
        } else {
            // Orientation: convert axis vectors
            float *fwd = &axis[0];
            float *lft = &axis[3];
            float *up  = &axis[6];

            Vector3 forward_g = id_to_godot_point(fwd[0], fwd[1], fwd[2]);
            Vector3 left_g    = id_to_godot_point(lft[0], lft[1], lft[2]);
            Vector3 up_g      = id_to_godot_point(up[0],  up[1],  up[2]);

            Vector3 right_g = -left_g;
            Vector3 back_g  = -forward_g;

            // Apply entity scale (entity-level only — MOHAA_UNIT_SCALE is
            // already baked into the mesh vertices by godot_skel_model.cpp)
            float s = (scale > 0.001f ? scale : 1.0f);

            Basis basis(right_g * s, up_g * s, back_g * s);
            mi->set_global_transform(Transform3D(basis, pos));
        }

        // ── Phase 62: Weapon viewport for first-person entities (depth-hack) ──
        // RF_FIRST_PERSON (0x02) or RF_DEPTHHACK (0x04) → weapon_root (separate
        // SubViewport with its own depth buffer, composited on top of the main
        // scene).  This prevents the viewmodel from clipping into walls.
        // All other entities stay in entity_root (main scene).
        // Exception: entities with customShader AND RF_FIRST_PERSON/RF_DEPTHHACK
        // must render in entity_root.  The weapon SubViewport composites via a
        // TextureRect with alpha blending; any non-opaque content there (additive
        // muzzle flashes, alpha-blended effects) produces opaque-black pixels
        // where the texture is dark, because the alpha channel in the SubViewport
        // is 1.0 even though RGB adds nothing (dark+additive=dark).  The
        // TextureRect compositing then treats alpha=1 as fully opaque → black.
        // Moving customShader entities to entity_root lets them blend directly
        // against the world, which matches the original engine (no separate
        // compositing pass for first-person effects).  The depth hack is lost
        // for these entities, but muzzle flashes are brief and rarely clip.
        if (weapon_root) {
            bool force_main_scene = false;
            if (is_first_person || is_depthhack) {
                int cs_check = 0;
                Godot_Renderer_GetEntitySprite(i, nullptr, nullptr, &cs_check);
                if (cs_check > 0) {
                    force_main_scene = true;  // ANY customShader → main scene
                }
            }
            Node *target_parent = (!force_main_scene && (is_first_person || is_depthhack))
                                ? weapon_root : entity_root;
            Node *cur_parent = mi->get_parent();
            if (cur_parent && cur_parent != target_parent) {
                cur_parent->remove_child(mi);
                target_parent->add_child(mi);
            }
        }

        // ── Phase 21+22+268: Entity colour tinting + alpha + entity lighting ──
        // Phase 134: Correct rgbGen/alphaGen entity application per shader directive
        // Only apply shaderRGBA when the shader explicitly requests it via rgbGen/alphaGen entity.
        Color light_mul(1.0f, 1.0f, 1.0f, 1.0f);
#ifdef HAS_ENTITY_LIGHTING_MODULE
        {
            // Determine lighting sample position in id Tech 3 coordinates
            float light_pos[3] = { origin[0], origin[1], origin[2] };
            // RF_LIGHTING_ORIGIN (0x0080 per tr_types.h): sample at lightingOrigin
            if (renderfx & 0x0080) {
                Godot_Renderer_GetEntityLightingOrigin(i, light_pos);
            }
            float lr, lg, lb;
            // Godot_EntityLight_Combined now calls Godot_EntityGridLighting()
            // which is an exact replica of RB_GetEntityGridLighting():
            // R_GetLightingGridValue + dlights + overbright + normalise + clamp.
            // Returns [0,1] — no further processing needed.
            Godot_EntityLight_Combined(light_pos, 4, &lr, &lg, &lb);
            light_mul = Color(lr, lg, lb, 1.0f);
        }
#else
        {
            float point[3] = { origin[0], origin[1], origin[2] };
            float lr = 1.0f, lg = 1.0f, lb = 1.0f;
            extern "C" void Godot_EntityGridLighting(const float origin[3],
                float *out_r, float *out_g, float *out_b);
            Godot_EntityGridLighting(point, &lr, &lg, &lb);
            light_mul = Color(lr, lg, lb, 1.0f);
        }
#endif
        bool has_light_tint = fabsf(light_mul.r - 1.0f) > 0.02f ||
                              fabsf(light_mul.g - 1.0f) > 0.02f ||
                              fabsf(light_mul.b - 1.0f) > 0.02f;

        // Phase 134: Check if we need per-surface shader analysis for rgbGen/alphaGen entity
        bool needs_material_update = has_light_tint;
        if (!needs_material_update) {
            // Quick pre-check: do we have any non-default shaderRGBA or RF_ALPHAFADE?
            bool has_shader_rgba = (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255 || rgba[3] < 255 || (renderfx & 0x0400));
            needs_material_update = has_shader_rgba;
        }

        if (needs_material_update) {
            Ref<Mesh> mesh = mi->get_mesh();
            if (mesh.is_valid()) {
                // Phase 61: Quantise light to 4-bit for cache key
                uint8_t lr = (uint8_t)(light_mul.r * 15.0f + 0.5f);
                uint8_t lg = (uint8_t)(light_mul.g * 15.0f + 0.5f);
                uint8_t lb = (uint8_t)(light_mul.b * 15.0f + 0.5f);
                uint32_t light_q = ((uint32_t)lr << 8) | ((uint32_t)lg << 4) | lb;

                int sc = mesh->get_surface_count();
                for (int s = 0; s < sc; s++) {
                    Ref<Material> base_mat = mi->get_surface_override_material(s);
                    if (base_mat.is_null())
                        base_mat = mesh->surface_get_material(s);

                    Ref<StandardMaterial3D> smat = base_mat;
                    if (!smat.is_valid()) continue;

                    // Phase 134: Determine per-surface shader rgbGen/alphaGen directives
                    String shader_name = smat->get_meta("shader_name", "");
                    const GodotShaderProps *sp = nullptr;
                    if (!shader_name.is_empty()) {
                        CharString cs = shader_name.ascii();
                        sp = Godot_ShaderProps_Find(cs.get_data());
                    }

                    // Determine what modulation to apply based on shader directives.
                    // We need to check per-stage rgbGen/alphaGen from the first non-lightmap stage
                    // because the global props->rgbgen_type doesn't distinguish entity vs oneMinusEntity.
                    // Per-stage enums: STAGE_RGBGEN_ENTITY=4, STAGE_RGBGEN_ONE_MINUS_ENTITY=5
                    //                  STAGE_ALPHAGEN_ENTITY=3, STAGE_ALPHAGEN_ONE_MINUS_ENTITY=4
                    //
                    // IMPORTANT: For RT_MODEL entities, "vertex" colour IS the entity's
                    // shaderRGBA — the engine copies refEntity_t.shaderRGBA into
                    // tess.svars.colors[] (see RB_CalcDiffuseColor / RB_CalcColorFromEntity).
                    // So rgbGen vertex (2) and alphaGen vertex (1) must be treated
                    // identically to rgbGen entity (4) / alphaGen entity (3) here.
                    // This is critical for effect models like muzflash.tik that use
                    // "alphagen vertex" + tagspawn alpha 0.30 + fade.
                    bool apply_rgb_entity = false;
                    bool apply_rgb_one_minus_entity = false;
                    bool apply_alpha_entity = false;
                    bool apply_alpha_one_minus_entity = false;

                    if (sp && sp->stage_count > 0) {
                        // Find first non-lightmap stage (same logic as get_shader_texture)
                        for (int st = 0; st < sp->stage_count; st++) {
                            if (!sp->stages[st].active) continue;
                            if (sp->stages[st].isLightmap) continue;
                            // MohaaStageRgbGen: IDENTITY=0, IDENTITY_LIGHTING=1, VERTEX=2, WAVE=3, ENTITY=4, ONE_MINUS_ENTITY=5, LIGHTING_DIFFUSE=6, CONST=7
                            if (sp->stages[st].rgbGen == 4 || sp->stages[st].rgbGen == 2) {
                                // ENTITY(4) or VERTEX(2) — for entities, vertex == entity
                                apply_rgb_entity = true;
                            } else if (sp->stages[st].rgbGen == 5) { // STAGE_RGBGEN_ONE_MINUS_ENTITY
                                apply_rgb_one_minus_entity = true;
                            }
                            // MohaaStageAlphaGen: IDENTITY=0, VERTEX=1, WAVE=2, ENTITY=3, ONE_MINUS_ENTITY=4, PORTAL=5, CONST=6
                            if (sp->stages[st].alphaGen == 3 || sp->stages[st].alphaGen == 1) {
                                // ENTITY(3) or VERTEX(1) — for entities, vertex == entity
                                apply_alpha_entity = true;
                            } else if (sp->stages[st].alphaGen == 4) { // STAGE_ALPHAGEN_ONE_MINUS_ENTITY
                                apply_alpha_one_minus_entity = true;
                            }
                            break; // Only check first non-lightmap stage
                        }
                    }

                    // Build modulation color (entity color + wave animation)
                    Color entity_tint(1.0f, 1.0f, 1.0f, 1.0f);
                    bool has_entity_tint = false;

                    if (apply_rgb_entity) {
                        entity_tint.r = rgba[0] / 255.0f;
                        entity_tint.g = rgba[1] / 255.0f;
                        entity_tint.b = rgba[2] / 255.0f;
                        has_entity_tint = true;
                    } else if (apply_rgb_one_minus_entity) {
                        entity_tint.r = (255 - rgba[0]) / 255.0f;
                        entity_tint.g = (255 - rgba[1]) / 255.0f;
                        entity_tint.b = (255 - rgba[2]) / 255.0f;
                        has_entity_tint = true;
                    }

                    if (apply_alpha_entity) {
                        entity_tint.a = rgba[3] / 255.0f;
                        has_entity_tint = true;
                    } else if (apply_alpha_one_minus_entity) {
                        entity_tint.a = (255 - rgba[3]) / 255.0f;
                        has_entity_tint = true;
                    }

                    // Phase 135: Apply rgbGen/alphaGen wave animation (pulsing color/alpha effects)
                    // Mirrors update_shader_animations() wave logic but applied per-entity.
                    // Uses ent_effective_time so per-entity shaderTime offsets are respected
                    // (e.g. muzzle flashes animate from spawn time, not map load time).
                    if (sp) {
                        if (sp->rgbgen_type == 2) { // wave
                            float wave_val = eval_wave(sp->rgbgen_wave_func,
                                sp->rgbgen_wave_base, sp->rgbgen_wave_amp,
                                sp->rgbgen_wave_phase, sp->rgbgen_wave_freq,
                                ent_effective_time);
                            wave_val = clamp01(wave_val);
                            entity_tint.r *= wave_val;
                            entity_tint.g *= wave_val;
                            entity_tint.b *= wave_val;
                            has_entity_tint = true;
                        }
                        if (sp->alphagen_type == 2) { // wave
                            float alpha_wave = eval_wave(sp->alphagen_wave_func,
                                sp->alphagen_wave_base, sp->alphagen_wave_amp,
                                sp->alphagen_wave_phase, sp->alphagen_wave_freq,
                                ent_effective_time);
                            entity_tint.a *= clamp01(alpha_wave);
                            has_entity_tint = true;
                        }
                    }

                    // Skip if no modulation needed
                    if (!has_light_tint && !has_entity_tint)
                        continue;

                    // Build tinted material cache key:
                    //   hModel(16b) | surfIdx(4b) | rgba_q(16b=4×4b) | light_q(12b) = 48 bits
                    uint8_t rq = rgba[0] >> 4, gq = rgba[1] >> 4;
                    uint8_t bq = rgba[2] >> 4, aq = rgba[3] >> 4;
                    uint64_t tint_key = ((uint64_t)(hModel & 0xFFFF) << 32) |
                                        ((uint64_t)(s & 0xF) << 28) |
                                        ((uint64_t)rq << 24) |
                                        ((uint64_t)gq << 20) |
                                        ((uint64_t)bq << 16) |
                                        ((uint64_t)aq << 12) |
                                        (uint64_t)(light_q & 0xFFF);

                    auto tint_it = tinted_mat_cache.find(tint_key);
                    if (tint_it != tinted_mat_cache.end()) {
                        mi->set_surface_override_material(s, tint_it->second);
                    } else {
                        Ref<StandardMaterial3D> dup = smat->duplicate();
                        Color existing = dup->get_albedo();
                        dup->set_albedo(Color(existing.r * entity_tint.r * light_mul.r,
                                               existing.g * entity_tint.g * light_mul.g,
                                               existing.b * entity_tint.b * light_mul.b,
                                               existing.a * entity_tint.a));
                        if (entity_tint.a < 0.999f || (apply_alpha_entity && rgba[3] < 255)) {
                            dup->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                        }

                        // Phase 136: deformVertexes autosprite/autosprite2 billboard mode
                        if (sp && sp->has_deform) {
                            if (sp->deform_type == 3) { // autosprite
                                dup->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
                            } else if (sp->deform_type == 4) { // autosprite2
                                dup->set_billboard_mode(BaseMaterial3D::BILLBOARD_FIXED_Y);
                            }
                        }

                        tinted_mat_cache[tint_key] = dup;
                        mi->set_surface_override_material(s, dup);
                    }
                }
            }
        }

        // ── FPS viewmodel zoom hiding ──
        // When the player is zooming (e.g. sniper scope), the first-person
        // viewmodel should be hidden.  The cgame normally signals this via
        // MDL_SURFACE_NODRAW in the surfaces[] array, but in our GDExtension
        // the cgame may set NODRAW unconditionally due to a state sync issue.
        // Instead, we check STAT_INZOOM directly from the player state.
        if (is_first_person && Godot_Client_GetPlayerZoom() > 0) {
            mi->set_visible(false);
            entity_cache_keys[i] = key;
            continue;
        }

        // ── Dynamic shadow casting (r_shadows 1) ──
        // In mode 1, ALL visible non-firstperson entities cast GPU shadows onto
        // PER_PIXEL BSP floors regardless of RF_SHADOW flag.
        // (RF_SHADOW is rarely set by cgame; using it as guard means no casters.)
        {
            bool wants_shadow = (cached_entity_shadow_mode == 1) &&
                                 !is_first_person && !is_depthhack;
            mi->set_cast_shadows_setting(wants_shadow
                ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON
                : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        }

        mi->set_visible(true);

        entity_cache_keys[i] = key;
    }

    // Hide excess pool meshes from previous frame
    for (int i = ent_count; i < active_entity_count; i++) {
        if (i < (int)entity_meshes.size()) {
            entity_meshes[i]->set_visible(false);
        }
    }

    active_entity_count = ent_count;

    // ── Phase 35: Entity parenting — DISABLED ──
    // CG_AttachEntity (in cgame) already computes world-space positions
    // for child entities before submitting them via R_AddRefEntityToScene.
    // The parentEntity field is metadata for lighting origin inheritance,
    // NOT a rendering hierarchy directive.  Applying the parent transform
    // here would double-offset child entities (weapons, attachments),
    // causing them to appear at wildly wrong positions.
    // See: cg_modelanim.c CG_AttachEntity → VectorMA for origin,
    //      then cgi.R_AddRefEntityToScene(&model, parent) submits
    //      the entity at its final world position.
}

void MoHAARunner::update_dlights() {
    if (!game_world) return;

    int dl_count = Godot_Renderer_GetDlightCount();

    // Log dlight count once when first lights appear
    static bool logged_dlight_count = false;
    if (!logged_dlight_count && dl_count > 0) {
        UtilityFunctions::print(String("[MoHAA] Dynamic lights in frame: ") +
                                String::num_int64(dl_count));
        logged_dlight_count = true;
    }

    // Grow the dynamic light pool if needed
    while ((int)dlight_nodes.size() < dl_count) {
        OmniLight3D *light = memnew(OmniLight3D);
        light->set_name(String("DLight_") + String::num_int64((int64_t)dlight_nodes.size()));
        light->set_visible(false);
        light->set_param(Light3D::PARAM_ATTENUATION, 1.5);  // smoother falloff
        // Inherit current r_dlight_shadows setting for newly pooled lights
        light->set_shadow(cached_dlight_shadows == 1);
        game_world->add_child(light);
        dlight_nodes.push_back(light);
    }

    for (int i = 0; i < dl_count; i++) {
        float origin[3], intensity = 0.0f;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        int type = 0;

        Godot_Renderer_GetDlight(i, origin, &intensity, &r, &g, &b, &type);

        OmniLight3D *light = dlight_nodes[i];
        Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
        light->set_global_position(pos);
        light->set_color(Color(r, g, b));
        // Convert intensity from id units to Godot energy + range
        float range_metres = intensity * MOHAA_UNIT_SCALE;
        light->set_param(Light3D::PARAM_RANGE, range_metres);
        light->set_param(Light3D::PARAM_ENERGY, 2.0);
        // r_dlight_shadows: each omni shadow = 6 depth cube-map renders/frame
        light->set_shadow(cached_dlight_shadows == 1);
        light->set_visible(true);
    }

    // Hide excess lights from previous frame
    for (int i = dl_count; i < active_dlight_count; i++) {
        if (i < (int)dlight_nodes.size()) {
            dlight_nodes[i]->set_visible(false);
        }
    }

    active_dlight_count = dl_count;
}

// ──────────────────────────────────────────────
//  Poly/particle rendering (Phase 16)
// ──────────────────────────────────────────────

void MoHAARunner::update_polys() {
    if (!game_world) return;

    int poly_count = Godot_Renderer_GetPolyCount();

    if (poly_count == 0 && active_poly_count == 0) return;

    // Create poly container on first use
    if (!poly_root) {
        poly_root = memnew(Node3D);
        poly_root->set_name("Polys");
        game_world->add_child(poly_root);
    }

    // Grow mesh pool if needed
    while ((int)poly_meshes.size() < poly_count) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String("Poly_") + String::num_int64((int64_t)poly_meshes.size()));
        mi->set_visible(false);
        poly_root->add_child(mi);
        poly_meshes.push_back(mi);
    }

    for (int i = 0; i < poly_count; i++) {
        int hShader = 0;
        // Mark fragments from BSP clipping can produce up to 8 verts
        // (MAX_VERTS_ON_POLY in cg_local.h)
        float positions[8 * 3];
        float texcoords[8 * 2];
        unsigned char colors[8 * 4];

        int numVerts = Godot_Renderer_GetPoly(i, &hShader,
                                               positions, texcoords,
                                               colors, 8);

        MeshInstance3D *mi = poly_meshes[i];

        if (numVerts < 3) {
            mi->set_visible(false);
            continue;
        }

        if (numVerts > 8) numVerts = 8;

        // Build an ArrayMesh triangle fan from the poly vertices
        PackedVector3Array gPos;
        PackedVector2Array gUV;
        PackedColorArray   gCol;
        PackedInt32Array   gIdx;

        gPos.resize(numVerts);
        gUV.resize(numVerts);
        gCol.resize(numVerts);

        for (int v = 0; v < numVerts; v++) {
            gPos.set(v, id_to_godot_position(
                positions[v*3+0], positions[v*3+1], positions[v*3+2]));
            gUV.set(v, Vector2(texcoords[v*2+0], texcoords[v*2+1]));
            gCol.set(v, Color(colors[v*4+0] / 255.0f,
                              colors[v*4+1] / 255.0f,
                              colors[v*4+2] / 255.0f,
                              colors[v*4+3] / 255.0f));
        }

        // Nudge mark/decal polys slightly outward along their face normal
        // to prevent Z-fighting with the BSP surface they sit on.
        // This replicates GL polygonOffset which the real renderer uses.
        if (numVerts >= 3) {
            Vector3 e1 = gPos[1] - gPos[0];
            Vector3 e2 = gPos[2] - gPos[0];
            Vector3 normal = e1.cross(e2);
            if (normal.length_squared() > 1e-12f) {
                normal = normal.normalized();
                // 0.005 m ~ 0.2 id units — invisible but prevents Z-fight
                // Subtract normal because the vertices are wound clockwise, meaning normal points INTO the surface.
                for (int v = 0; v < numVerts; v++) {
                    gPos.set(v, gPos[v] - normal * 0.005f);
                }
            }
        }

        // Triangle fan: 0‒1‒2, 0‒2‒3, ...
        int numTris = numVerts - 2;
        gIdx.resize(numTris * 3);
        for (int t = 0; t < numTris; t++) {
            gIdx.set(t*3+0, 0);
            gIdx.set(t*3+1, t + 1);
            gIdx.set(t*3+2, t + 2);
        }

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = gPos;
        arrays[Mesh::ARRAY_TEX_UV] = gUV;
        arrays[Mesh::ARRAY_COLOR]  = gCol;
        arrays[Mesh::ARRAY_INDEX]  = gIdx;

        Ref<ArrayMesh> mesh = mi->get_mesh();
        if (mesh.is_valid()) {
            mesh->clear_surfaces();
        } else {
            mesh.instantiate();
            mi->set_mesh(mesh);
        }
        if (gPos.size() > 0 && gIdx.size() >= 3) {
            mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        }

        // Material: cached per (shader_handle, blend_type) — avoids per-frame
        // instantiation, property resets, and shader/texture lookups.
        // blend_type: 0=alpha (default), 1=inv_mul, 2=multiplicative
        int blend_type = 0;
        if (hShader > 0) {
            const char *sn = Godot_Renderer_GetShaderName(hShader);
            const GodotShaderProps *poly_sp = (sn && sn[0]) ? Godot_ShaderProps_Find(sn) : nullptr;
            if (poly_sp && poly_sp->transparency == SHADER_MULTIPLICATIVE_INV) blend_type = 1;
            else if (poly_sp && poly_sp->transparency == SHADER_MULTIPLICATIVE) blend_type = 2;
        }

        int64_t poly_mat_key = ((int64_t)hShader << 2) | blend_type;
        auto pm_it = s_poly_mat_cache.find(poly_mat_key);
        if (pm_it == s_poly_mat_cache.end()) {
            const char *sn = Godot_Renderer_GetShaderName(hShader);
            UtilityFunctions::print(String("[MoHAA][Poly] Caching new poly material: shader='") + String(sn ? sn : "none") +
                                    String("', blend_type=") + String::num_int64(blend_type));

            // First time seeing this (shader, blend) combo — create and cache
            if (blend_type == 1) {
                // Inverse-multiplicative: result = dst * (1 - src*vertex_color)
                static Ref<Shader> inv_mul_poly_shader;
                if (inv_mul_poly_shader.is_null()) {
                    inv_mul_poly_shader.instantiate();
                    inv_mul_poly_shader->set_code(
                        "shader_type spatial;\n"
                        "render_mode blend_mul, unshaded, cull_disabled, "
                        "depth_draw_opaque;\n"
                        "uniform sampler2D albedo_texture : hint_default_black, "
                        "filter_linear;\n"
                        "void fragment() {\n"
                        "    vec4 tex = texture(albedo_texture, UV);\n"
                        "    ALBEDO = vec3(1.0) - tex.rgb * COLOR.rgb;\n"
                        "    ALPHA = 1.0;\n"
                        "}\n"
                    );
                }
                Ref<ShaderMaterial> smat;
                smat.instantiate();
                smat->set_shader(inv_mul_poly_shader);
                smat->set_render_priority(-1);  // Draw after BSP opaque but before transparent (polygonOffset)

                // Load and set the albedo texture for this shader
                if (hShader > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(hShader);
                    if (tex.is_valid()) {
                        smat->set_shader_parameter("albedo_texture", tex);
                    }
                }

                s_poly_mat_cache[poly_mat_key] = smat;
            } else {
                // Standard or multiplicative — StandardMaterial3D
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
                mat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
                mat->set_albedo(Color(1, 1, 1, 1));
                mat->set_uv1_scale(Vector3(1, 1, 1));
                mat->set_uv1_offset(Vector3(0, 0, 0));

                if (blend_type == 2) {
                    mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                }

                bool poly_shader_props_found = false;
                if (hShader > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(hShader);
                    if (!tex.is_valid()) {
                        const char *sn = Godot_Renderer_GetShaderName(hShader);
                        static std::unordered_set<std::string> logged_missing_tex;
                        if (sn && logged_missing_tex.find(sn) == logged_missing_tex.end()) {
                            UtilityFunctions::print(String("[MoHAA][CAUTION] Poly shader missing texture! Name: ") + String(sn) + " blend: " + String::num_int64(blend_type));
                            logged_missing_tex.insert(sn);
                        }
                    }

                    if (blend_type == 0) {
                        const char *sn = Godot_Renderer_GetShaderName(hShader);
                        if (sn && sn[0]) {
                            const GodotShaderProps *sp_poly = Godot_ShaderProps_Find(sn);
                            if (sp_poly) {
                                poly_shader_props_found = true;
                            }
                            apply_shader_props_to_material(mat, sn);
                        }
                    }
                }

                // When no .shader definition is found for a poly, check
                // texture alpha to determine blend mode. Polys without
                // alpha (fire, flash, sparks) should use additive
                // blending so black areas are invisible.
                if (!poly_shader_props_found && blend_type == 0 && hShader > 0) {
                    auto ha_it = shader_texture_has_alpha.find(hShader);
                    bool tex_has_alpha = (ha_it != shader_texture_has_alpha.end()) && ha_it->second;
                    if (!tex_has_alpha) {
                        mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                    }
                }

                // CRITICAL: Polys are generated by the C engine with absolute world-space
                // coordinates and added to a Node3D at origin(0,0,0). If the shader definition
                // specified an autosprite deform, apply_shader_props_to_material will
                // enable Godot's BILLBOARD_ENABLED. This causes Godot to rotate the far-away
                // vertices around (0,0,0), creating giant, screen-spanning distorted geometry.
                // Since the C engine ALREADY handled the billboarding math, we MUST force
                // it disabled here.
                mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
                mat->set_render_priority(-1);  // Draw after BSP opaque but before transparent (polygonOffset)

                s_poly_mat_cache[poly_mat_key] = mat;
            }
            pm_it = s_poly_mat_cache.find(poly_mat_key);
        }

        // Apply texture per-frame to support RemapShader and animMap
        if (hShader > 0) {
            Ref<ImageTexture> tex = get_shader_texture(hShader);
            if (tex.is_valid()) {
                if (blend_type == 1) {
                    Ref<ShaderMaterial> smat = pm_it->second;
                    smat->set_shader_parameter("albedo_texture", tex);
                } else {
                    Ref<StandardMaterial3D> std_mat = pm_it->second;
                    std_mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                }
            } else {
                // Diagnostic: log once per shader handle that has no texture
                static std::unordered_set<int> logged_missing_poly_tex;
                if (logged_missing_poly_tex.find(hShader) == logged_missing_poly_tex.end()) {
                    const char *sn = Godot_Renderer_GetShaderName(hShader);
                    UtilityFunctions::print(
                        String("[MoHAA][POLY-TEX-MISS] Poly shader has NO texture! handle=")
                        + String::num_int64(hShader)
                        + String(" name='") + String(sn ? sn : "(null)") + String("'")
                        + String(" blend=") + String::num_int64(blend_type));
                    logged_missing_poly_tex.insert(hShader);
                }
            }
        }

        mi->set_surface_override_material(0, pm_it->second);
        mi->set_visible(true);

        static std::unordered_set<int> logged_active_poly_shaders;
        if (hShader > 0) {
            if (logged_active_poly_shaders.find(hShader) == logged_active_poly_shaders.end()) {
                const char *sn = Godot_Renderer_GetShaderName(hShader);
                UtilityFunctions::print(String("[MoHAA][POLY-ACTIVE] Poly rendering with shader handle: ") + String::num_int64(hShader) + " name: '" + String(sn ? sn : "(null)") + "'");
                logged_active_poly_shaders.insert(hShader);
            }
        } else {
            static bool logged_zero_shader = false;
            if (!logged_zero_shader) {
                UtilityFunctions::print(String("[MoHAA][POLY-ZERO] Poly rendering with hShader == ") + String::num_int64(hShader));
                logged_zero_shader = true;
            }
        }
    }

    // Hide excess polys from previous frame
    for (int i = poly_count; i < active_poly_count; i++) {
        if (i < (int)poly_meshes.size()) {
            poly_meshes[i]->set_visible(false);
        }
    }

    active_poly_count = poly_count;
}

// ──────────────────────────────────────────────
//  Swipe effects (Phase 24)
// ──────────────────────────────────────────────

void MoHAARunner::update_swipe_effects() {
    if (!game_world) return;

    float thisTime, life;
    int hShader, numPoints;
    if (!Godot_Renderer_GetSwipeData(&thisTime, &life, &hShader, &numPoints) || numPoints < 2) {
        if (swipe_mesh) swipe_mesh->set_visible(false);
        return;
    }

    // Create container on first use
    if (!swipe_root) {
        swipe_root = memnew(Node3D);
        swipe_root->set_name("SwipeEffects");
        game_world->add_child(swipe_root);
    }
    if (!swipe_mesh) {
        swipe_mesh = memnew(MeshInstance3D);
        swipe_mesh->set_name("SwipeTrail");
        swipe_root->add_child(swipe_mesh);
    }

    // Build a triangle strip mesh from swipe points
    PackedVector3Array gPos;
    PackedVector2Array gUV;
    PackedInt32Array   gIdx;
    gPos.resize(numPoints * 2);
    gUV.resize(numPoints * 2);

    for (int i = 0; i < numPoints; i++) {
        float p1[3], p2[3], time;
        Godot_Renderer_GetSwipePoint(i, p1, p2, &time);

        Vector3 gp1 = id_to_godot_position(p1[0], p1[1], p1[2]);
        Vector3 gp2 = id_to_godot_position(p2[0], p2[1], p2[2]);
        float t = (float)i / (float)(numPoints - 1);

        gPos.set(i * 2,     gp1);
        gPos.set(i * 2 + 1, gp2);
        gUV.set(i * 2,     Vector2(t, 0));
        gUV.set(i * 2 + 1, Vector2(t, 1));
    }

    // Triangle indices for the strip
    for (int i = 0; i < numPoints - 1; i++) {
        int base = i * 2;
        gIdx.push_back(base);
        gIdx.push_back(base + 1);
        gIdx.push_back(base + 2);
        gIdx.push_back(base + 1);
        gIdx.push_back(base + 3);
        gIdx.push_back(base + 2);
    }

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = gPos;
    arrays[Mesh::ARRAY_TEX_UV] = gUV;
    arrays[Mesh::ARRAY_INDEX]  = gIdx;

    Ref<ArrayMesh> smesh = swipe_mesh->get_mesh();
    if (smesh.is_null()) {
        smesh.instantiate();
        swipe_mesh->set_mesh(smesh);
    }
    smesh->clear_surfaces();
    if (gPos.size() > 0 && gIdx.size() >= 3) {
        smesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    }

    // Material: alpha-blended, unshaded, double-sided
    Ref<StandardMaterial3D> mat;
    Ref<Material> override_mat = swipe_mesh->get_surface_override_material(0);
    if (override_mat.is_valid()) {
        mat = override_mat;
    } else {
        mat.instantiate();
        mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        swipe_mesh->set_surface_override_material(0, mat);
    }

    if (hShader > 0) {
        Ref<ImageTexture> tex = get_shader_texture(hShader);
        if (tex.is_valid()) {
            mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
        } else {
            mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, Ref<Texture2D>());
        }
    } else {
        mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, Ref<Texture2D>());
    }
    swipe_mesh->set_global_transform(Transform3D());
    swipe_mesh->set_visible(true);
}

// ──────────────────────────────────────────────
//  Terrain mark decals (Phase 25)
// ──────────────────────────────────────────────

void MoHAARunner::update_terrain_marks() {
    if (!game_world) return;

    // Phase 150: honour r_drawmarks cvar — when 0, hide all marks
    if (Cvar_VariableIntegerValue("r_drawmarks") == 0) {
        for (int i = 0; i < active_terrain_mark_count; i++) {
            if (i < (int)terrain_mark_meshes.size())
                terrain_mark_meshes[i]->set_visible(false);
        }
        active_terrain_mark_count = 0;
        return;
    }

    int markCount = Godot_Renderer_GetTerrainMarkCount();
    if (markCount <= 0) {
        for (int i = 0; i < active_terrain_mark_count; i++) {
            if (i < (int)terrain_mark_meshes.size())
                terrain_mark_meshes[i]->set_visible(false);
        }
        active_terrain_mark_count = 0;
        return;
    }

    // Create container on first use
    if (!terrain_mark_root) {
        terrain_mark_root = memnew(Node3D);
        terrain_mark_root->set_name("TerrainMarks");
        game_world->add_child(terrain_mark_root);
    }

    // Grow pool
    while ((int)terrain_mark_meshes.size() < markCount) {
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String("TerrainMark_") + String::num_int64((int64_t)terrain_mark_meshes.size()));
        mi->set_visible(false);
        terrain_mark_root->add_child(mi);
        terrain_mark_meshes.push_back(mi);
    }

    for (int m = 0; m < markCount; m++) {
        int hShader = 0, numVerts = 0, terrainIndex = 0, renderfx = 0;
        Godot_Renderer_GetTerrainMark(m, &hShader, &numVerts, &terrainIndex, &renderfx);

        MeshInstance3D *mi = terrain_mark_meshes[m];
        if (numVerts < 3) {
            mi->set_visible(false);
            continue;
        }

        // Build polygon mesh from terrain mark vertices
        PackedVector3Array gPos;
        PackedVector2Array gUV;
        PackedColorArray   gCol;
        PackedInt32Array   gIdx;
        gPos.resize(numVerts);
        gUV.resize(numVerts);
        gCol.resize(numVerts);

        for (int v = 0; v < numVerts; v++) {
            float xyz[3], st[2];
            unsigned char rgba[4];
            Godot_Renderer_GetTerrainMarkVert(m, v, xyz, st, rgba);
            gPos.set(v, id_to_godot_position(xyz[0], xyz[1], xyz[2]));
            gUV.set(v, Vector2(st[0], st[1]));
            gCol.set(v, Color(rgba[0] / 255.0f, rgba[1] / 255.0f,
                              rgba[2] / 255.0f, rgba[3] / 255.0f));
        }

        // Nudge terrain mark polys outward to prevent Z-fighting
        if (numVerts >= 3) {
            Vector3 e1 = gPos[1] - gPos[0];
            Vector3 e2 = gPos[2] - gPos[0];
            Vector3 normal = e1.cross(e2);
            if (normal.length_squared() > 1e-12f) {
                normal = normal.normalized();
                // Subtract normal because the vertices are wound clockwise, meaning normal points INTO the surface.
                for (int v = 0; v < numVerts; v++) {
                    gPos.set(v, gPos[v] - normal * 0.005f);
                }
            }
        }

        // Fan triangulation
        for (int v = 1; v < numVerts - 1; v++) {
            gIdx.push_back(0);
            gIdx.push_back(v);
            gIdx.push_back(v + 1);
        }

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = gPos;
        arrays[Mesh::ARRAY_TEX_UV] = gUV;
        arrays[Mesh::ARRAY_COLOR]  = gCol;
        arrays[Mesh::ARRAY_INDEX]  = gIdx;

        Ref<ArrayMesh> tmesh;
        tmesh.instantiate();
        if (gPos.size() > 0 && gIdx.size() >= 3) {
            tmesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        }
        mi->set_mesh(tmesh);

        // Material: cached per (shader_handle, blend_type) — avoids per-frame
        // instantiation and shader/texture lookups for terrain marks.
        int mark_blend_type = 0;
        if (hShader > 0) {
            const char *mark_shader_name = Godot_Renderer_GetShaderName(hShader);
            const GodotShaderProps *mark_sp = (mark_shader_name && mark_shader_name[0])
                ? Godot_ShaderProps_Find(mark_shader_name) : nullptr;
            if (mark_sp && mark_sp->transparency == SHADER_MULTIPLICATIVE_INV) mark_blend_type = 1;
            else if (mark_sp && mark_sp->transparency == SHADER_MULTIPLICATIVE) mark_blend_type = 2;
        }

        int64_t tm_mat_key = ((int64_t)hShader << 2) | mark_blend_type;
        auto tm_it = s_terrain_mark_mat_cache.find(tm_mat_key);
        if (tm_it == s_terrain_mark_mat_cache.end()) {
            if (mark_blend_type == 1) {
                // Inverse-multiplicative blend: result = dst * (1 - texture*vertex_color)
                static Ref<Shader> inv_mul_3d_shader;
                if (inv_mul_3d_shader.is_null()) {
                    inv_mul_3d_shader.instantiate();
                    inv_mul_3d_shader->set_code(
                        "shader_type spatial;\n"
                        "render_mode blend_mul, unshaded, cull_disabled, "
                        "depth_draw_never, depth_test_disabled;\n"
                        "uniform sampler2D albedo_texture : source_color, "
                        "filter_linear;\n"
                        "void fragment() {\n"
                        "    vec4 tex = texture(albedo_texture, UV);\n"
                        "    ALBEDO = vec3(1.0) - tex.rgb * COLOR.rgb;\n"
                        "    ALPHA = 1.0;\n"
                        "}\n"
                    );
                }
                Ref<ShaderMaterial> smat;
                smat.instantiate();
                smat->set_shader(inv_mul_3d_shader);
                if (hShader > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(hShader);
                    if (tex.is_valid()) {
                        smat->set_shader_parameter("albedo_texture", tex);
                    }
                }
                s_terrain_mark_mat_cache[tm_mat_key] = smat;
            } else if (mark_blend_type == 2) {
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_MUL);
                mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                s_terrain_mark_mat_cache[tm_mat_key] = mat;
            } else {
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                if (hShader > 0) {
                    const char *mark_shader_name = Godot_Renderer_GetShaderName(hShader);
                    if (mark_shader_name && mark_shader_name[0]) {
                        apply_shader_props_to_material(mat, mark_shader_name);
                    }
                }
                s_terrain_mark_mat_cache[tm_mat_key] = mat;
            }
            tm_it = s_terrain_mark_mat_cache.find(tm_mat_key);
        }

        // Apply texture per-frame to support RemapShader and animMap
        if (hShader > 0) {
            Ref<ImageTexture> tex = get_shader_texture(hShader);
            if (tex.is_valid()) {
                if (mark_blend_type == 1) {
                    Ref<ShaderMaterial> smat = tm_it->second;
                    smat->set_shader_parameter("albedo_texture", tex);
                } else {
                    Ref<StandardMaterial3D> std_mat = tm_it->second;
                    std_mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                }
            }
        }

        mi->set_surface_override_material(0, tm_it->second);
        mi->set_global_transform(Transform3D());
        mi->set_visible(true);
    }

    for (int i = markCount; i < active_terrain_mark_count; i++) {
        if (i < (int)terrain_mark_meshes.size())
            terrain_mark_meshes[i]->set_visible(false);
    }
    active_terrain_mark_count = markCount;
}

// ──────────────────────────────────────────────
//  Dynamic shadow mode switching  (r_shadows / r_dlight_shadows cvars)
//  r_shadows 0 = classic MOHAA shadow blobs (default)
//  r_shadows 1 = modern GPU shadows: sun DirectionalLight (from map sundirection)
//               + optional dlight shadows (r_dlight_shadows 1)
// ──────────────────────────────────────────────

// Orient entity_shadow_light from the map's worldspawn sundirection/suncolor.
// Called after every map load (load_sun_flare populates sun_direction/sun_color),
// and when switching to r_shadows 1.  Safe to call even when mode == 0.
void MoHAARunner::apply_sun_light_direction() {
    if (!entity_shadow_light) return;

    // ── Derive Godot-space direction from map sundirection ──
    // id Tech 3 → Godot coordinate mapping: X=-idY, Y=idZ, Z=-idX
    // sun_direction[3] points FROM scene TOWARD the sun (set by id_angle_vectors_left).
    // We place the light AT the sun position and look toward origin.
    Vector3 sun_dir_godot;
    if (sun_exists &&
        (sun_direction[0] != 0.0f || sun_direction[1] != 0.0f || sun_direction[2] != 0.0f)) {
        sun_dir_godot = Vector3(
            -sun_direction[1],
             sun_direction[2],
            -sun_direction[0]
        ).normalized();
    } else {
        // No sundirection in worldspawn — use a natural-looking fallback angle.
        // ~65° elevation, from the south-east.
        sun_dir_godot = Vector3(0.35f, 0.87f, -0.35f).normalized();
    }

    // ── Orient the DirectionalLight3D ──
    // look_at_from_position: -Z axis points from p_position toward p_target.
    // Place light in the "toward sun" direction; look at origin → -Z = rays toward scene.
    Vector3 light_pos = sun_dir_godot * 500.0f;  // Far enough to be beyond any scene geometry
    Vector3 look_target = Vector3(0.0f, 0.0f, 0.0f);
    // Avoid degenerate look_at when sun_dir is parallel to the default up axis.
    Vector3 up = (Math::abs(sun_dir_godot.dot(Vector3(0.0f, 1.0f, 0.0f))) > 0.99f)
        ? Vector3(1.0f, 0.0f, 0.0f)
        : Vector3(0.0f, 1.0f, 0.0f);
    entity_shadow_light->look_at_from_position(light_pos, look_target, up);

    // ── Sun colour from worldspawn suncolor / sunlight ──
    if (sun_exists) {
        entity_shadow_light->set_color(Color(sun_color[0], sun_color[1], sun_color[2]));
    } else {
        entity_shadow_light->set_color(Color(1.0f, 0.98f, 0.9f));  // warm white fallback
    }

    UtilityFunctions::print(
        String("[MoHAA] Sun light: dir_godot=(") +
        String::num(sun_dir_godot.x, 2) + "," +
        String::num(sun_dir_godot.y, 2) + "," +
        String::num(sun_dir_godot.z, 2) + ")" +
        (sun_exists ? " (from map)" : " (fallback)"));
}

// Apply r_shadows mode transition.  Called when the cvar changes and
// after every map load.
void MoHAARunner::apply_player_shadow_mode(int mode) {
    cached_entity_shadow_mode = mode;

    // ── Toggle the dedicated shadow DirectionalLight ──
    if (entity_shadow_light) {
        entity_shadow_light->set_visible(mode == 1);
    }

    // ── Adjust environment ambient energy ──
    // Ambient stays at the baseline 0.5 in ALL modes.  The 0.5 value exists
    // because BSP lightmap textures are baked at 2× overbright — not because
    // of shadow contrast.  In mode 1 the DirectionalLight only generates a
    // shadow map; it does not contribute visible illumination (energy 0.01).
    // Entity materials remain UNSHADED (100 % parity with OpenMoHAA) so
    // ambient doesn't affect them.  BSP uses a custom ShaderMaterial whose
    // fragment() emits lightmapped colour directly and is unaffected by
    // ambient.  Therefore no ambient change is needed for either mode.
    // (Left as explicit no-op for clarity.)

    // ── Entity materials stay UNSHADED in all modes ──
    // 100 % visual parity with OpenMoHAA: entities use their albedo colour
    // directly (lightgrid-style).  They still CAST shadows onto BSP surfaces
    // via MeshInstance3D::cast_shadow, but their own appearance is unchanged.
    // No material shading mode switch needed.

    // ── Update BSP ShaderMaterial shadow darkness uniform ──
    // Set bsp_shadow_darkness on every registered ShaderMaterial.  At 0.0
    // the shader behaves identically to the old render_mode unshaded (no
    // visible shadow).  At 0.3 the DirectionalLight's ATTENUATION (shadow
    // factor) is applied: BSP floors in shadow are 70 % of their lightmap
    // colour, giving subtle shadow shapes matching MOHAA's original look.
#ifdef HAS_SHADER_MATERIAL_MODULE
    Godot_Shader_SetShadowDarkness(mode == 1 ? 0.3f : 0.0f);
#endif

    UtilityFunctions::print(
        String("[MoHAA] r_shadows = ") + String::num_int64(mode) +
        (mode == 1 ? " (GPU shadows ON)" : " (classic shadow blobs)"));
}

// ──────────────────────────────────────────────
//  Shadow blob projection for RF_SHADOW entities
// ──────────────────────────────────────────────

void MoHAARunner::update_shadow_blobs() {
    if (!game_world) return;

    // In mode 1 (dynamic Godot shadows), RF_SHADOW entities cast real GPU
    // shadows onto the world.  Skip the shadow blob projection entirely so
    // blobs don't double-up with the accurate shadows.
    if (cached_entity_shadow_mode == 1) {
        // Hide any previously created blobs
        for (MeshInstance3D *mi : shadow_blob_meshes) {
            if (mi) mi->set_visible(false);
        }
        active_shadow_blob_count = 0;
        return;
    }

    // RF_ flag constants used for shadow filtering
    static const int RF_DONTDRAW = 0x80;   // (1<<7)
    static const int RF_SHADOW   = 0x800;  // (1<<11)
    static const float SHADOW_DISTANCE = 96.0f;  // id units — max downward trace
    static const float SHADOW_Z_OFFSET = 0.5f;   // id units — lift above ground to avoid z-fighting
    static const int SHADOW_CIRCLE_SEGMENTS = 8;

    int ent_count = Godot_Renderer_GetEntityCount();

    // First pass: count entities that need shadow blobs
    int shadow_count = 0;
    for (int i = 0; i < ent_count; i++) {
        float origin[3];
        int renderfx = 0, hModel = 0, entityNumber = 0;
        unsigned char rgba[4];
        int reType = Godot_Renderer_GetEntity(i, origin, nullptr, nullptr,
                                               &hModel, &entityNumber, rgba, &renderfx);
        if (reType != 0 /* RT_MODEL */ || !(renderfx & RF_SHADOW) || (renderfx & RF_DONTDRAW))
            continue;
        shadow_count++;
    }

    // Create container on first use
    if (!shadow_blob_root && shadow_count > 0) {
        shadow_blob_root = memnew(Node3D);
        shadow_blob_root->set_name("ShadowBlobs");
        game_world->add_child(shadow_blob_root);
    }

    // Create shared shadow material on first use
    if (shadow_blob_material.is_null() && shadow_count > 0) {
        shadow_blob_material.instantiate();
        shadow_blob_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        shadow_blob_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        shadow_blob_material->set_blend_mode(BaseMaterial3D::BLEND_MODE_MIX);
        shadow_blob_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        shadow_blob_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        shadow_blob_material->set_albedo(Color(1.0f, 1.0f, 1.0f, 1.0f));
        // Render above ground to avoid z-fighting
        shadow_blob_material->set_render_priority(1);
    }

    // Grow pool if needed
    if (shadow_blob_root) {
        while ((int)shadow_blob_meshes.size() < shadow_count) {
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_name(String("ShadowBlob_") + String::num_int64((int64_t)shadow_blob_meshes.size()));
            mi->set_visible(false);
            shadow_blob_root->add_child(mi);
            shadow_blob_meshes.push_back(mi);
        }
    }

    // Second pass: project shadow blobs
    int shadow_idx = 0;

    for (int i = 0; i < ent_count && shadow_idx < shadow_count; i++) {
        float origin[3], axis[9], scale = 1.0f;
        int renderfx = 0, hModel = 0, entityNumber = 0;
        unsigned char rgba[4];
        int reType = Godot_Renderer_GetEntity(i, origin, axis, &scale,
                                               &hModel, &entityNumber, rgba, &renderfx);
        if (reType != 0 /* RT_MODEL */ || !(renderfx & RF_SHADOW) || (renderfx & RF_DONTDRAW))
            continue;

        MeshInstance3D *mi = shadow_blob_meshes[shadow_idx];

        // Determine shadow blob radius from model
        float modelRadius = Godot_Model_GetRadius(hModel);
        float blobRadius = modelRadius * scale * 0.6f;
        if (blobRadius < 4.0f) blobRadius = 4.0f;
        if (blobRadius > 64.0f) blobRadius = 64.0f;

        // Build a circular polygon in id space centred at entity origin,
        // oriented horizontally (in XY plane, Z is up in id space)
        float points[SHADOW_CIRCLE_SEGMENTS][3];
        for (int s = 0; s < SHADOW_CIRCLE_SEGMENTS; s++) {
            float angle = (float)s / (float)SHADOW_CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
            points[s][0] = origin[0] + cosf(angle) * blobRadius;
            points[s][1] = origin[1] + sinf(angle) * blobRadius;
            points[s][2] = origin[2];
        }

        // Projection vector: straight down in id space
        float projection[3] = { 0.0f, 0.0f, -SHADOW_DISTANCE };

        // Use BSP mark fragments to clip shadow polygon against world geometry
        static const int MAX_FRAG_POINTS = 384;
        static const int MAX_FRAGMENTS = 32;
        float pointBuffer[MAX_FRAG_POINTS * 3];
        int fragFirstPoint[MAX_FRAGMENTS];
        int fragNumPoints[MAX_FRAGMENTS];
        int fragIIndex[MAX_FRAGMENTS];

        int numFragments = Godot_BSP_MarkFragments(
            SHADOW_CIRCLE_SEGMENTS, (const float (*)[3])points, projection,
            MAX_FRAG_POINTS, pointBuffer,
            MAX_FRAGMENTS,
            fragFirstPoint, fragNumPoints, fragIIndex,
            blobRadius * blobRadius);

        if (numFragments <= 0) {
            mi->set_visible(false);
            shadow_idx++;
            continue;
        }

        // Compute alpha fade based on distance to ground:
        // Use the first fragment point to estimate ground height
        float groundZ = pointBuffer[fragFirstPoint[0] * 3 + 2];
        float heightAboveGround = origin[2] - groundZ;
        float fade = 1.0f - (heightAboveGround / SHADOW_DISTANCE);
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        float alpha = fade * 0.5f;

        // Build mesh from all fragments
        PackedVector3Array gPos;
        PackedColorArray   gCol;
        PackedInt32Array   gIdx;

        int totalVerts = 0;
        for (int f = 0; f < numFragments; f++)
            totalVerts += fragNumPoints[f];

        gPos.resize(totalVerts);
        gCol.resize(totalVerts);

        int vertOffset = 0;
        for (int f = 0; f < numFragments; f++) {
            int first = fragFirstPoint[f];
            int count = fragNumPoints[f];
            for (int v = 0; v < count; v++) {
                float *pt = &pointBuffer[(first + v) * 3];
                // Offset slightly upward to avoid z-fighting
                gPos.set(vertOffset + v, id_to_godot_position(pt[0], pt[1], pt[2] + SHADOW_Z_OFFSET));
                gCol.set(vertOffset + v, Color(0.0f, 0.0f, 0.0f, alpha));
            }
            // Fan triangulation for this fragment
            for (int v = 1; v < count - 1; v++) {
                gIdx.push_back(vertOffset);
                gIdx.push_back(vertOffset + v);
                gIdx.push_back(vertOffset + v + 1);
            }
            vertOffset += count;
        }

        if (gIdx.size() < 3) {
            mi->set_visible(false);
            shadow_idx++;
            continue;
        }

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = gPos;
        arrays[Mesh::ARRAY_COLOR]  = gCol;
        arrays[Mesh::ARRAY_INDEX]  = gIdx;

        Ref<ArrayMesh> smesh = Object::cast_to<ArrayMesh>(mi->get_mesh().ptr());
        if (smesh.is_valid()) {
            smesh->clear_surfaces();
        } else {
            smesh.instantiate();
            mi->set_mesh(smesh);
        }
        smesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mi->set_surface_override_material(0, shadow_blob_material);

        mi->set_global_transform(Transform3D());
        mi->set_visible(true);
        shadow_idx++;
    }

    // Hide excess pool meshes
    for (int i = shadow_idx; i < active_shadow_blob_count; i++) {
        if (i < (int)shadow_blob_meshes.size())
            shadow_blob_meshes[i]->set_visible(false);
    }
    active_shadow_blob_count = shadow_idx;
}

// ──────────────────────────────────────────────
//  Shader UV animation (Phase 36)
// ──────────────────────────────────────────────

void MoHAARunner::update_shader_animations(double delta) {
    // Use engine's refdef.time (milliseconds) for shader animations.
    // This keeps shader timing in sync with the engine clock and prevents
    // drift from Godot's variable delta accumulation.
    // Fallback to delta accumulation if refdef time is zero (pre-init).
    int refdef_ms = Godot_Renderer_GetRefdefTime();
    if (refdef_ms > 0) {
        shader_anim_time = (double)refdef_ms / 1000.0;
    } else {
        shader_anim_time += delta;
    }

    // Iterate over BSP world mesh surfaces and apply tcMod scroll offset
    if (!bsp_map_node) return;

    // Per-surface cache: avoids per-frame string→C-string conversion,
    // GodotShaderProps hash lookup, and O(N) linear shader scan.
    // Built on first access; cleared on map change alongside animmap caches.
    // Key = (child_index << 16) | surface_index — unique per BSP surface.

    // Walk MeshInstance3D children of bsp_map_node
    for (int c = 0; c < bsp_map_node->get_child_count(); c++) {
        MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(bsp_map_node->get_child(c));
        if (!mi) continue;

        Ref<Mesh> mesh = mi->get_mesh();
        if (!mesh.is_valid()) continue;

        for (int s = 0; s < mesh->get_surface_count(); s++) {
            // Look up or populate per-surface cache
            uint32_t cache_key = ((uint32_t)c << 16) | (uint32_t)s;
            auto cache_it = s_surf_anim_cache.find(cache_key);
            if (cache_it == s_surf_anim_cache.end()) {
                SurfAnimCache entry;
                entry.sp = nullptr;
                entry.shader_handle = -1;
                entry.needs_animation = false;

                Ref<Material> base = mi->get_surface_override_material(s);
                if (base.is_null()) base = mesh->surface_get_material(s);
                Ref<StandardMaterial3D> smat_init = base;
                if (smat_init.is_valid()) {
                    String shader_name = smat_init->get_meta("shader_name", "");
                    if (!shader_name.is_empty()) {
                        CharString cs = shader_name.ascii();
                        entry.sp = Godot_ShaderProps_Find(cs.get_data());
                        if (entry.sp) {
                            // Register shader handle via O(1) hash insert (not linear scan)
                            entry.shader_handle = Godot_Renderer_RegisterShader(cs.get_data());
                            entry.needs_animation = entry.sp->has_tcmod
                                || (entry.sp->has_animmap && entry.sp->animmap_num_frames > 0 && entry.sp->animmap_freq > 0.0f)
                                || entry.sp->rgbgen_type == 2
                                || entry.sp->alphagen_type == 2;
                        }
                    }
                }
                s_surf_anim_cache[cache_key] = entry;
                cache_it = s_surf_anim_cache.find(cache_key);
            }

            const SurfAnimCache &sc = cache_it->second;
            if (!sc.needs_animation) continue;  // Early exit: skip static surfaces

            const GodotShaderProps *sp = sc.sp;

            Ref<Material> base = mi->get_surface_override_material(s);
            if (base.is_null()) base = mesh->surface_get_material(s);
            Ref<StandardMaterial3D> smat = base;
            if (!smat.is_valid()) continue;

            // Apply UV tcMod animation: scroll + turb
            if (sp->has_tcmod) {
                float offS = 0.0f;
                float offT = 0.0f;
                /* Phase 144: tcMod offset — static UV shift */
                offS += sp->tcmod_offset_s;
                offT += sp->tcmod_offset_t;
                if (sp->tcmod_scroll_s != 0.0f || sp->tcmod_scroll_t != 0.0f) {
                    offS += fmodf((float)(sp->tcmod_scroll_s * shader_anim_time), 1.0f);
                    offT += fmodf((float)(sp->tcmod_scroll_t * shader_anim_time), 1.0f);
                }
                if (sp->tcmod_turb_amp != 0.0f && sp->tcmod_turb_freq != 0.0f) {
                    float t = (float)(shader_anim_time * sp->tcmod_turb_freq);
                    offS += sinf(t) * sp->tcmod_turb_amp;
                    offT += cosf(t) * sp->tcmod_turb_amp;
                }
                smat->set_uv1_offset(Vector3(offS, offT, 0.0f));

                if (sp->tcmod_rotate != 0.0f) {
                    smat->set_uv1_offset(Vector3(offS, offT, Math::deg_to_rad((float)(sp->tcmod_rotate * shader_anim_time))));
                }
            }

            // Phase 55: animMap frame swap — uses cached shader_handle (O(1))
            if (sp->has_animmap && sp->animmap_num_frames > 0 && sp->animmap_freq > 0.0f) {
                int shader_handle = sc.shader_handle;

                if (shader_handle > 0) {
                    auto it_info = animmap_info.find(shader_handle);
                    if (it_info == animmap_info.end()) {
                        AnimMapInfo info;
                        info.freq = sp->animmap_freq;
                        info.num_frames = sp->animmap_num_frames;
                        animmap_info[shader_handle] = info;

                        std::vector<Ref<ImageTexture>> frames;
                        for (int fi = 0; fi < sp->animmap_num_frames; fi++) {
                            Ref<ImageTexture> frame_tex;
                            // Use RegisterShader for O(1) lookup instead of linear scan
                            if (sp->animmap_frames[fi][0]) {
                                int frame_handle = Godot_Renderer_RegisterShader(sp->animmap_frames[fi]);
                                if (frame_handle > 0) {
                                    frame_tex = get_shader_texture(frame_handle);
                                }
                            }
                            frames.push_back(frame_tex);
                        }
                        animmap_frames[shader_handle] = frames;
                    }

                    auto it_frames = animmap_frames.find(shader_handle);
                    auto it_anim   = animmap_info.find(shader_handle);
                    if (it_frames != animmap_frames.end() && it_anim != animmap_info.end()) {
                        const AnimMapInfo &ai = it_anim->second;
                        if (ai.num_frames > 0 && (int)it_frames->second.size() >= ai.num_frames) {
                            int frame_idx = (int)floor(shader_anim_time * ai.freq) % ai.num_frames;
                            if (frame_idx < 0) frame_idx += ai.num_frames;
                            Ref<ImageTexture> ftex = it_frames->second[frame_idx];
                            if (ftex.is_valid()) {
                                smat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ftex);
                            }
                        }
                    }
                }
            }

            // Phase 56/57 + Phase 141: runtime rgbGen/alphaGen wave animation
            // Uses eval_wave() to support all wave function types (sin, triangle,
            // square, sawtooth, inverse_sawtooth) — not just sine.
            if (sp->rgbgen_type == 2 || sp->alphagen_type == 2) {
                Color a = smat->get_albedo();
                if (sp->rgbgen_type == 2) {
                    float v = eval_wave(sp->rgbgen_wave_func,
                        sp->rgbgen_wave_base, sp->rgbgen_wave_amp,
                        sp->rgbgen_wave_phase, sp->rgbgen_wave_freq,
                        shader_anim_time);
                    v = clamp01(v);
                    a.r = v; a.g = v; a.b = v;
                }
                if (sp->alphagen_type == 2) {
                    float alpha = eval_wave(sp->alphagen_wave_func,
                        sp->alphagen_wave_base, sp->alphagen_wave_amp,
                        sp->alphagen_wave_phase, sp->alphagen_wave_freq,
                        shader_anim_time);
                    a.a = clamp01(alpha);
                    if (a.a < 0.999f) {
                        smat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                    }
                }
                smat->set_albedo(a);
            }
        }
    }
}

// ──────────────────────────────────────────────
//  2D HUD overlay (Phase 7h)
// ──────────────────────────────────────────────

Ref<ImageTexture> MoHAARunner::get_shader_texture(int shader_handle) {
    static std::unordered_map<int, bool> logged_missing;
    static std::unordered_map<int, bool> logged_empty_name;

    // Look up shader name (Phase 52: apply shader remap if active)
    const char *raw_name = Godot_Renderer_GetShaderName(shader_handle);
    const char *remapped = Godot_Renderer_GetShaderRemap(raw_name);
    const char *name = (remapped && remapped[0]) ? remapped : raw_name;
    if (!name || !name[0]) {
        if (logged_empty_name.find(shader_handle) == logged_empty_name.end()) {
            logged_empty_name[shader_handle] = true;
            UtilityFunctions::print(String("[MoHAA][2D] Shader has no name yet: #") + String::num_int64(shader_handle));
        }
        return Ref<ImageTexture>();
    }

    const char *lookup_name = name;
    const GodotShaderProps *sp = Godot_ShaderProps_Find(name);

    // UI scripts may provide namespaced aliases (e.g. "MENU/multiarrow")
    // while the .shader definition key is the leaf token ("multiarrow").
    // Retry lookups using basename to match renderer behaviour.
    if (!sp) {
        const char *slash = strrchr(name, '/');
        if (slash && slash[1]) {
            const GodotShaderProps *sp_base = Godot_ShaderProps_Find(slash + 1);
            if (sp_base) {
                sp = sp_base;
                lookup_name = slash + 1;
            }
        }
    }

    auto load_texture_from_qpath = [&](const char *qpath, bool *out_has_alpha) -> Ref<ImageTexture> {
        if (!qpath || !qpath[0]) {
            return Ref<ImageTexture>();
        }
        if (out_has_alpha) *out_has_alpha = false;

        // Helper: post-process a loaded Godot Image (dead-alpha fix, alpha detection, mipmaps)
        auto finalise_image = [&](Ref<Image> img, const char *loaded_path) -> Ref<ImageTexture> {
            if (!img.is_valid() || img->is_empty()) return Ref<ImageTexture>();

            // Strip useless alpha: all-zero or all-255 → convert to RGB8.
            // Matches the real renderer's Upload32 which selects GL_RGB
            // (no alpha) when every alpha byte is 0xFF, and handles the
            // degenerate all-zero case (broken TGA with empty alpha).
            if (img->get_format() == Image::FORMAT_RGBA8) {
                PackedByteArray imgdata = img->get_data();
                int pixel_count = img->get_width() * img->get_height();
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

            if (out_has_alpha) {
                *out_has_alpha = (img->detect_alpha() != Image::ALPHA_NONE);
            }
            img->generate_mipmaps();
            return ImageTexture::create_from_image(img);
        };

        // ── Primary path: engine's R_LoadRawImage ──
        // Uses the real renderer's image pipeline: tries JPG before TGA
        // (matching r_loadjpg behaviour), handles RLE compression, correct
        // BGRA→RGBA byte swapping, and bottom-up flipping.  This avoids
        // Godot's TGA parser which misinterprets some MOHAA TGA files.
        {
            unsigned char *raw_pic = nullptr;
            int raw_w = 0, raw_h = 0;
            if (R_LoadRawImage(qpath, &raw_pic, &raw_w, &raw_h) && raw_pic && raw_w > 0 && raw_h > 0) {
                int raw_size = raw_w * raw_h * 4;

                // One-shot diagnostic: compute alpha stats before finalise_image
                {
                    static std::unordered_set<std::string> logged_raw;
                    if (logged_raw.find(qpath) == logged_raw.end()) {
                        logged_raw.insert(qpath);
                        uint8_t min_a = 255, max_a = 0;
                        for (int p = 0; p < raw_w * raw_h; p++) {
                            uint8_t a = raw_pic[p * 4 + 3];
                            if (a < min_a) min_a = a;
                            if (a > max_a) max_a = a;
                        }
                        UtilityFunctions::print(String("[TEX-LOAD] R_LoadRawImage OK: '") + String(qpath) +
                            String("' ") + String::num_int64(raw_w) + String("x") + String::num_int64(raw_h) +
                            String(" alpha_min=") + String::num_int64(min_a) +
                            String(" alpha_max=") + String::num_int64(max_a));
                    }
                }

                PackedByteArray pba;
                pba.resize(raw_size);
                memcpy(pba.ptrw(), raw_pic, raw_size);
                R_FreeRawImage(raw_pic);

                Ref<Image> img = Image::create_from_data(raw_w, raw_h, false, Image::FORMAT_RGBA8, pba);
                Ref<ImageTexture> tex = finalise_image(img, qpath);
                if (tex.is_valid()) return tex;
            }
        }

        // ── Fallback: VFS + Godot decoders (for extensionless paths, etc.) ──
        // Try .jpg before .tga to match the engine's r_loadjpg=1 preference:
        // JPG has no alpha channel, avoiding unwanted transparency from
        // 32-bit TGA files where the alpha is irrelevant (e.g. serverback).
        const char *extensions[] = { "", ".jpg", ".tga", ".png", NULL };
        for (int ext_i = 0; extensions[ext_i]; ext_i++) {
            char path[256];
            snprintf(path, sizeof(path), "%s%s", qpath, extensions[ext_i]);

            void *buf = NULL;
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
                if (err != OK) err = img->load_jpg_from_buffer(pba);
            }

            if (err == OK && !img->is_empty()) {
                Ref<ImageTexture> tex = finalise_image(img, path);
                if (tex.is_valid()) return tex;
            }
        }

        return Ref<ImageTexture>();
    };

    // animMap UI shaders must return a frame based on time; never return a
    // frozen cached texture for these (e.g. fan_anim1 in the main menu).
    if (sp && sp->has_animmap && sp->animmap_num_frames > 0 && sp->animmap_freq > 0.0f) {
        shader_textures.erase(shader_handle);
        shader_texture_has_alpha.erase(shader_handle);

        auto it_anim = animmap_info.find(shader_handle);
        auto it_frames = animmap_frames.find(shader_handle);

        bool need_init = (it_anim == animmap_info.end() ||
                          it_frames == animmap_frames.end() ||
                          (int)it_frames->second.size() < sp->animmap_num_frames);

        if (need_init) {
            AnimMapInfo info;
            info.freq = sp->animmap_freq;
            info.num_frames = sp->animmap_num_frames;
            animmap_info[shader_handle] = info;

            std::vector<Ref<ImageTexture>> frames;
            frames.resize(sp->animmap_num_frames);
            for (int fi = 0; fi < sp->animmap_num_frames; fi++) {
                if (!sp->animmap_frames[fi][0]) {
                    continue;
                }
                frames[fi] = load_texture_from_qpath(sp->animmap_frames[fi], nullptr);
            }
            animmap_frames[shader_handle] = frames;

            it_anim = animmap_info.find(shader_handle);
            it_frames = animmap_frames.find(shader_handle);
        }

        if (it_anim != animmap_info.end() && it_frames != animmap_frames.end()) {
            const AnimMapInfo &ai = it_anim->second;
            if (ai.num_frames > 0 && (int)it_frames->second.size() >= ai.num_frames) {
                int frame_idx = (int)floor(shader_anim_time * ai.freq) % ai.num_frames;
                if (frame_idx < 0) frame_idx += ai.num_frames;
                Ref<ImageTexture> frame_tex = it_frames->second[frame_idx];
                if (frame_tex.is_valid()) {
                    return frame_tex;
                }
            }
        }
    }

    // Non-animated shaders use static texture caching.
    // Track which effective name was used to load each cached texture so that
    // shader remapping (RemapShader) correctly invalidates stale entries.
    auto it = shader_textures.find(shader_handle);
    if (it != shader_textures.end()) {
        auto it_name = s_shader_texture_loaded_names.find(shader_handle);
        if (it_name != s_shader_texture_loaded_names.end() && it_name->second == name) {
            return it->second;  // Cache valid — same effective name
        }
        // Name changed (shader remap active or handle reused) — invalidate
        shader_textures.erase(it);
        shader_texture_has_alpha.erase(shader_handle);
        s_shader_texture_loaded_names.erase(shader_handle);
    }

    // ── Determine the actual texture image path(s) to try ──
    // Mirrors R_FindShader: first look up the shader definition in parsed
    // .shader script files.  If a definition exists, the first non-lightmap
    // stage's "map" directive gives the real texture path.  If no definition
    // is found, try using the shader name itself as a texture file path
    // (the fallback R_FindShader uses for implicit shaders).

    const char *texture_paths[4] = { NULL, NULL, NULL, NULL };
    int num_texture_paths = 0;

    if (sp && sp->stage_count > 0) {
        /* Select the best diffuse texture: skip lightmap, $whiteimage,
         * and environment map stages (tcGen environment = reflections).
         * Fall back to the first non-lightmap stage if only env stages exist.
         * Also handle animMap stages: use the first frame as the texture. */
        const char *fallback = NULL;
        for (int st = 0; st < sp->stage_count && num_texture_paths == 0; st++) {
            // Note: do NOT skip inactive stages for texture resolution —
            // ifCvar-disabled stages still define the correct texture path.
            if (sp->stages[st].isLightmap) continue;

            /* Determine the candidate texture path for this stage:
             * prefer map[], fall back to animMapFrames[0] for animated stages. */
            const char *stage_map = NULL;
            if (sp->stages[st].map[0]) {
                stage_map = sp->stages[st].map;
            } else if (sp->stages[st].animMapFrameCount > 0
                       && sp->stages[st].animMapFrames[0][0]) {
                stage_map = sp->stages[st].animMapFrames[0];
            }

            if (!stage_map) continue;
            if (strcmp(stage_map, "$lightmap") == 0) continue;
            if (strcmp(stage_map, "$whiteimage") == 0) continue;
            // Internal image names from the shader accessor start with '*'
            // (e.g. *white = tr.whiteImage, *lightmap, *default). These are
            // never real file paths — skip them to reach the actual texture stage.
            if (stage_map[0] == '*') continue;
            if (!fallback) fallback = stage_map;
            if (sp->stages[st].tcGen == STAGE_TCGEN_ENVIRONMENT) continue;
            texture_paths[num_texture_paths++] = stage_map;
        }
        if (num_texture_paths == 0 && fallback) {
            texture_paths[num_texture_paths++] = fallback;
        }
    }

    // Always also try the shader name itself (works for implicit shaders
    // where the name IS the texture path without extension)
    if (num_texture_paths == 0 || strcmp(texture_paths[0], name) != 0) {
        texture_paths[num_texture_paths++] = name;
    }

    // If a namespaced alias resolved via basename, also try the basename as a
    // direct image path fallback (for implicit shaders without .shader blocks).
    if (lookup_name != name && num_texture_paths < 4) {
        bool exists = false;
        for (int i = 0; i < num_texture_paths; i++) {
            if (texture_paths[i] && strcmp(texture_paths[i], lookup_name) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            texture_paths[num_texture_paths++] = lookup_name;
        }
    }

    // Hardcoded aliases for implicit shaders in scripts/common.shader that lack 'textures/' prefixes
    if (!sp && num_texture_paths <= 1) {
        if (strcmp(lookup_name, "markShadow") == 0) {
            texture_paths[num_texture_paths++] = "textures/common/shadow";
        } else if (strcasecmp(lookup_name, "footShadow") == 0 || strcasecmp(lookup_name, "footshadow") == 0) {
            texture_paths[num_texture_paths++] = "textures/decals/footshadow";
        } else if (strcmp(lookup_name, "projectionShadow") == 0) {
            texture_paths[num_texture_paths++] = "*white";
        } else if (strcmp(lookup_name, "flare") == 0 || strcmp(lookup_name, "flareshader") == 0) {
            texture_paths[num_texture_paths++] = "textures/sprites/flare";
        }
    }

    // ── Handle $whiteimage shaders (e.g. menu_button_trans) ──
    // If the shader definition exists but uses only $whiteimage stages
    // (no real texture paths), create a 1x1 white texture.
    if (num_texture_paths <= 1 && sp && sp->stage_count > 0) {
        bool all_white = true;
        for (int st = 0; st < sp->stage_count; st++) {
            if (sp->stages[st].isLightmap) continue;
            const char *sm = sp->stages[st].map;
            if (sm[0] && strcmp(sm, "$whiteimage") != 0 && strcmp(sm, "$lightmap") != 0 && sm[0] != '*') {
                all_white = false;
                break;
            }
            if (sp->stages[st].animMapFrameCount > 0) {
                all_white = false;
                break;
            }
        }
        if (all_white && num_texture_paths == 1) {
            // All stages are $whiteimage — return a 1x1 white texture
            static Ref<ImageTexture> *white_tex = new Ref<ImageTexture>();
            if (white_tex->is_null()) {
                PackedByteArray wdata;
                wdata.resize(4);
                wdata.ptrw()[0] = 255; wdata.ptrw()[1] = 255;
                wdata.ptrw()[2] = 255; wdata.ptrw()[3] = 255;
                Ref<Image> wimg = Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, wdata);
                *white_tex = ImageTexture::create_from_image(wimg);
            }
            shader_textures[shader_handle] = *white_tex;
            shader_texture_has_alpha[shader_handle] = false; // $whiteimage is fully opaque
            s_shader_texture_loaded_names[shader_handle] = name ? name : "";
            return *white_tex;
        }
    }

    // ── Try loading each candidate path via VFS ──
    Ref<ImageTexture> tex;
    bool loaded_has_alpha = false;
    for (int tp = 0; tp < num_texture_paths && tex.is_null(); tp++) {
        bool tex_alpha = false;
        tex = load_texture_from_qpath(texture_paths[tp], &tex_alpha);
        if (!tex.is_null()) {
            loaded_has_alpha = tex_alpha;
        }
    }

    // ── Fallback: engine-internal white texture ──
    // The shader "white" references textures/sprites/white.tga which may
    // not exist in the pk3s.  In the real renderer, R_FindShader("white")
    // falls back to tr.whiteImage (a programmatic 8x8 white texture).
    // Replicate that behaviour: if the texture file can't be found and
    // the shader name is "white" or any candidate path references the
    // sprites/white texture, return a 1x1 white pixel.
    if (tex.is_null() && name) {
        bool is_white_shader = (strcmp(name, "white") == 0 ||
                                strcmp(name, "*white") == 0 ||
                                strcmp(name, "$whiteimage") == 0);
        if (!is_white_shader) {
            for (int i = 0; i < num_texture_paths && !is_white_shader; i++) {
                if (!texture_paths[i]) continue;
                if (strstr(texture_paths[i], "sprites/white") ||
                    strcmp(texture_paths[i], "*white") == 0)
                    is_white_shader = true;
            }
        }
        if (is_white_shader) {
            static Ref<ImageTexture> *white_fallback = new Ref<ImageTexture>();
            if (white_fallback->is_null()) {
                PackedByteArray wdata;
                wdata.resize(4);
                wdata.ptrw()[0] = 255; wdata.ptrw()[1] = 255;
                wdata.ptrw()[2] = 255; wdata.ptrw()[3] = 255;
                Ref<Image> wimg = Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, wdata);
                *white_fallback = ImageTexture::create_from_image(wimg);
            }
            tex = *white_fallback;
        }
    }

    if (tex.is_null()) {
        if (logged_missing.find(shader_handle) == logged_missing.end()) {
            logged_missing[shader_handle] = true;
            String dbg = String("[MoHAA][2D] Missing texture for shader #") +
                         String::num_int64(shader_handle) + String(" name='") +
                         String(name ? name : "NULL") + String("' candidates=");
            for (int i = 0; i < num_texture_paths; i++) {
                if (!texture_paths[i] || !texture_paths[i][0]) continue;
                if (i > 0) dbg += String(",");
                dbg += String(texture_paths[i]);
            }
            UtilityFunctions::print(dbg);
        }
        // Return null — callers already guard with .is_valid(), so missing
        // textures are silently skipped rather than rendering a white box.
        // Do NOT cache the failure: allows the next frame to retry the VFS
        // lookup, so a texture that was unavailable on first access (e.g.
        // due to shader registration ordering) succeeds once the renderer
        // is fully initialised.
        return Ref<ImageTexture>();
    }

    if (!tex.is_null()) {
        shader_textures[shader_handle] = tex;
        shader_texture_has_alpha[shader_handle] = loaded_has_alpha;
        s_shader_texture_loaded_names[shader_handle] = name ? name : "";
    }
    return tex;
}

// ──────────────────────────────────────────────
//  UI Viewport Coordinate Transformation
// ──────────────────────────────────────────────
// Calculate the transformation from engine glConfig resolution to actual
// Godot viewport.  The pre-frame viewport sync (before Com_Frame) keeps
// glConfig in lock-step with the viewport, so ui_scale will normally
// be 1.0.  This function ONLY computes the scale — it does NOT modify
// glConfig (that's done once per frame in the pre-frame sync block).
void MoHAARunner::update_ui_transform() {
    // Use get_visible_rect() as the authoritative viewport size — always current,
    // even immediately after a window resize or fullscreen switch.
    Vector2 viewport_size = get_viewport()->get_visible_rect().size;
    if (viewport_size.x < 1.0f || viewport_size.y < 1.0f) {
        Vector2i win = DisplayServer::get_singleton()->window_get_size();
        viewport_size = Vector2(win);
    }

    // Read current glConfig resolution (set by pre-frame sync or GR_BeginRegistration)
    Godot_Renderer_GetVidSize(&ui_vid_w, &ui_vid_h);
    if (ui_vid_w <= 0) ui_vid_w = 640;
    if (ui_vid_h <= 0) ui_vid_h = 480;

    // Scale from engine glConfig space to actual Godot viewport.
    // Normally 1.0 because the pre-frame sync keeps them equal.
    ui_scale_x = viewport_size.x / (float)ui_vid_w;
    ui_scale_y = viewport_size.y / (float)ui_vid_h;
    ui_offset_x = 0.0f;
    ui_offset_y = 0.0f;
}

/*
void MoHAARunner::update_mirrors() {
    if (!camera || active_mirrors.empty()) {
        return;
    }

    // Mirror debugging hotkeys
    if (Input::get_singleton()) {
        float step = 0.05f;
        for (auto &m : active_mirrors) {
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_8)) m.normal.y += step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_2)) m.normal.y -= step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_6)) m.normal.x += step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_4)) m.normal.x -= step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_9)) m.normal.z += step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_7)) m.normal.z -= step;

            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_ADD)) m.center.z += step;
            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_SUBTRACT)) m.center.z -= step;

            if (Input::get_singleton()->is_key_pressed(Key::KEY_KP_5)) {
                UtilityFunctions::print(String("[MIRROR] normal = (") + String::num(m.normal.x) + ", " + String::num(m.normal.y) + ", " + String::num(m.normal.z) + ") center.z=" + String::num(m.center.z));
            }
            m.normal = m.normal.normalized();
        }
    }

    Transform3D cam_transform = camera->get_global_transform();
    Vector2i vp_size = get_viewport()->get_visible_rect().size;

    for (auto &m : active_mirrors) {
        if (!m.viewport || !m.camera || !m.mesh_instance || !m.mesh_instance->is_visible()) {
            continue;
        }

        // Ensure resolution matches main game window for accurate SCREEN_UV projection
        if (m.viewport->get_size() != vp_size) {
            m.viewport->set_size(vp_size);
        }

        // Get world space mirror info from the model's transform
        Transform3D model_transform = m.mesh_instance->get_global_transform();

        // Let's assume the local normal is looking up +Z for TIKI models typically
        // but it might need tweaking based on vanity.tik's exact orientation.
        // Easiest is to trace or visually debug. Defaulting to +Y local as a plane.
        Vector3 world_normal = model_transform.basis.xform(m.normal).normalized();
        Vector3 world_center = model_transform.xform(m.center);

        Plane mirror_plane(world_normal, world_center);

        // Reflect camera position across the mirror plane
        Vector3 ref_pos = mirror_plane.project(cam_transform.origin);
        ref_pos = ref_pos + (ref_pos - cam_transform.origin);

        // Reflect camera basis
        Basis ref_basis = cam_transform.basis;
        ref_basis.set_column(0, ref_basis.get_column(0) - 2.0 * world_normal * world_normal.dot(ref_basis.get_column(0)));
        ref_basis.set_column(1, ref_basis.get_column(1) - 2.0 * world_normal * world_normal.dot(ref_basis.get_column(1)));
        ref_basis.set_column(2, ref_basis.get_column(2) - 2.0 * world_normal * world_normal.dot(ref_basis.get_column(2)));

        m.camera->set_global_transform(Transform3D(ref_basis, ref_pos));
        m.camera->set_fov(camera->get_fov());

        // In a proper implementation we would set up an oblique near-plane frustum.
        // For this fun quick hack, we'll just let the geometry intersect.

        // We are using a ShaderMaterial that statically binds the viewport texture,
        // so we don't need to reapply the material or texture here anymore.
    }
}
*/

void MoHAARunner::update_2d_overlay() {
    int cmd_count = Godot_Renderer_Get2DCmdCount();
    if (cmd_count == 0 && !hud_layer) return;
    if (!hud_visible) return;  // F9 toggled off

    // Create HUD layer on first use
    if (!hud_layer) {
        hud_layer = memnew(CanvasLayer);
        hud_layer->set_layer(100);
        hud_layer->set_name("HUDLayer");
        add_child(hud_layer);

        hud_control = memnew(Control);
        hud_control->set_name("HUDControl");
        hud_control->set_anchors_preset(Control::PRESET_FULL_RECT);
        // CRITICAL: Allow mouse events to pass through to _unhandled_input().
        // Default MOUSE_FILTER_STOP would eat all mouse clicks/motion,
        // preventing the engine's UI system from receiving input.
        hud_control->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
        hud_layer->add_child(hud_control);

        static bool logged_hud = false;
        if (!logged_hud) {
            UtilityFunctions::print("[MoHAA] HUD overlay created.");
            logged_hud = true;
        }
    }

    // Update viewport transformation (calculates ui_scale_x/y, ui_offset_x/y)
    // Used both for rendering below and for mouse input transformation
    update_ui_transform();

    if (cmd_count == 0) return;

    // Get the Control's canvas item RID for direct RenderingServer drawing
    RID ci = hud_control->get_canvas_item();
    RenderingServer *rs = RenderingServer::get_singleton();
    rs->canvas_item_clear(ci);

    /* ── Blend-mode segment pool ──
     * The engine's 2D command stream interleaves normal (mix), multiplicative
     * (filter/dst*src), and inverse-multiplicative (dst*(1-src)) blend modes.
     * In OpenGL these happen sequentially on a single framebuffer.  In Godot,
     * each blend mode needs a separate canvas item with the right material.
     * We create child canvas items in command-stream order — each "segment"
     * covers a contiguous run of commands with the same blend mode.  This
     * preserves the correct z-ordering across blend mode switches.
     *
     * Segments are pooled: we reuse RIDs across frames to avoid alloc churn. */

    // Lazily create shared materials (once)
    if (mul_canvas_material.is_null()) {
        mul_canvas_material.instantiate();
        mul_canvas_material->set_blend_mode(CanvasItemMaterial::BLEND_MODE_MUL);
    }
    if (mul_inv_material.is_null()) {
        mul_inv_shader.instantiate();
        mul_inv_shader->set_code(
            "shader_type canvas_item;\n"
            "render_mode blend_mul;\n"
            "void fragment() {\n"
            "    vec4 tex = texture(TEXTURE, UV);\n"
            "    float id_light = 0.5;\n"
            "    COLOR = vec4(vec3(1.0) - tex.rgb * COLOR.rgb * id_light, tex.a * COLOR.a);\n"
            "}\n"
        );
        mul_inv_material.instantiate();
        mul_inv_material->set_shader(mul_inv_shader);
    }
    if (opaque_mix_material.is_null()) {
        opaque_mix_shader.instantiate();
        opaque_mix_shader->set_code(
            "shader_type canvas_item;\n"
            "void fragment() {\n"
            "    vec4 tex = texture(TEXTURE, UV);\n"
            "    // Ignore texture alpha (replicates GL_BLEND disabled for\n"
            "    // GLS_DEFAULT/SHADER_OPAQUE), but preserve COLOR.a from\n"
            "    // SetColor so hover/UI highlights with alpha < 1 still\n"
            "    // composite correctly on the canvas.\n"
            "    COLOR = vec4(tex.rgb * COLOR.rgb, COLOR.a);\n"
            "}\n"
        );
        opaque_mix_material.instantiate();
        opaque_mix_material->set_shader(opaque_mix_shader);
    }
    if (add_canvas_material.is_null()) {
        add_canvas_material.instantiate();
        add_canvas_material->set_blend_mode(CanvasItemMaterial::BLEND_MODE_ADD);
    }
    if (alpha_inv_material.is_null()) {
        alpha_inv_shader.instantiate();
        alpha_inv_shader->set_code(
            "shader_type canvas_item;\n"
            "void fragment() {\n"
            "    vec4 tex = texture(TEXTURE, UV);\n"
            "    // Fallback ALPHA_INV: for draws NOT pre-composited on CPU.\n"
            "    // Godot linearises tex.rgb (sRGB decode) but tex.a is raw.\n"
            "    // Undo the decode, composite in sRGB, then re-linearise.\n"
            "    vec3 srgb = pow(max(tex.rgb, vec3(0.0)), vec3(1.0 / 2.2));\n"
            "    vec3 composited = srgb * (1.0 - tex.a) + vec3(1.0) * tex.a;\n"
            "    COLOR = vec4(pow(composited, vec3(2.2)) * COLOR.rgb, COLOR.a);\n"
            "}\n"
        );
        alpha_inv_material.instantiate();
        alpha_inv_material->set_shader(alpha_inv_shader);
    }
    if (mix_material.is_null()) {
        mix_shader.instantiate();
        mix_shader->set_code(
            "shader_type canvas_item;\n"
            "void fragment() {\n"
            "    // Explicit texture * vertex-colour multiply.  Godot's default\n"
            "    // no-material canvas path appears not to apply the per-draw\n"
            "    // modulate colour correctly, causing white font text despite\n"
            "    // black SetColor.  This shader forces the multiplication.\n"
            "    COLOR = texture(TEXTURE, UV) * COLOR;\n"
            "}\n"
        );
        mix_material.instantiate();
        mix_material->set_shader(mix_shader);
    }

    // Clear all existing segments
    for (int si = 0; si < (int)overlay_segments.size(); si++) {
        rs->canvas_item_clear(overlay_segments[si].item);
        // Hide unused segments from previous frame
        rs->canvas_item_set_visible(overlay_segments[si].item, false);
    }
    overlay_segment_count = 0;
    overlay_current_blend = -1;

    // Helper lambda: get or create the current segment canvas item for a blend mode
    auto get_segment_ci = [&](int blend) -> RID {
        if (overlay_segment_count > 0 && overlay_current_blend == blend) {
            return overlay_segments[overlay_segment_count - 1].item;
        }
        // Need a new segment
        if (overlay_segment_count >= (int)overlay_segments.size()) {
            CanvasSegment seg;
            seg.item = rs->canvas_item_create();
            rs->canvas_item_set_parent(seg.item, ci);
            seg.blend_mode = -1;
            overlay_segments.push_back(seg);
        }
        auto &seg = overlay_segments[overlay_segment_count];
        rs->canvas_item_set_visible(seg.item, true);

        // Set material for this segment's blend mode
        if (seg.blend_mode != blend) {
            if (blend == BLEND_MIX) {
                rs->canvas_item_set_material(seg.item, mix_material->get_rid());
            } else if (blend == BLEND_MUL) {
                rs->canvas_item_set_material(seg.item, mul_canvas_material->get_rid());
            } else if (blend == BLEND_MUL_INV) {
                rs->canvas_item_set_material(seg.item, mul_inv_material->get_rid());
            } else if (blend == BLEND_OPAQUE) {
                rs->canvas_item_set_material(seg.item, opaque_mix_material->get_rid());
            } else if (blend == BLEND_ADD) {
                rs->canvas_item_set_material(seg.item, add_canvas_material->get_rid());
            } else if (blend == BLEND_ALPHA_INV) {
                rs->canvas_item_set_material(seg.item, alpha_inv_material->get_rid());
            }
            seg.blend_mode = blend;
        }

        overlay_segment_count++;
        overlay_current_blend = blend;
        return seg.item;
    };

    // Phase 58: Prepare loading background image (map preview from
    // RE_DrawStretchRaw).  We build a texture here but DON'T draw it yet.
    // It will be drawn inside the 2D command loop at the correct Z-position
    // (bg_cmd_index) so that widget backgrounds render UNDER the preview
    // and the photo frame / text / loading bar render ON TOP.
    static Ref<ImageTexture> loading_bg_tex;
    static Ref<Image> loading_bg_img;
    bool has_loading_bg = false;
    int  bg_cmd_index   = 0;
    {
        int cols = 0, rows = 0, bgr = 0;
        const unsigned char *bg_data = nullptr;
        if (Godot_Renderer_GetBackground(&cols, &rows, &bgr, &bg_data) &&
            cols > 0 && rows > 0 && bg_data && !Godot_Renderer_IsWorldMapLoaded()) {

            PackedByteArray pixels;
            pixels.resize(cols * rows * 4);
            unsigned char *dst = pixels.ptrw();
            int src_stride = cols * 3;
            for (int y = 0; y < rows; y++) {
                const unsigned char *src_row = bg_data + y * src_stride;
                unsigned char *dst_row = dst + y * cols * 4;
                for (int x = 0; x < cols; x++) {
                    const unsigned char *s = src_row + x * 3;
                    unsigned char *d = dst_row + x * 4;
                    if (bgr) {
                        d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
                    } else {
                        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    }
                    d[3] = 255;
                }
            }

            loading_bg_img = Image::create_from_data(cols, rows, false, Image::FORMAT_RGBA8, pixels);
            if (loading_bg_img.is_valid()) {
                if (loading_bg_tex.is_null()) {
                    loading_bg_tex = ImageTexture::create_from_image(loading_bg_img);
                } else {
                    loading_bg_tex->update(loading_bg_img);
                }
            }

            if (loading_bg_tex.is_valid()) {
                has_loading_bg = true;
                bg_cmd_index = Godot_Renderer_GetBackgroundCmdIndex();
            }
        }
    }

    // Use cached transformation values calculated by update_ui_transform()
    float vid_area = (float)(ui_vid_w * ui_vid_h);

    bool scissor_enabled = false;

    Rect2 scissor_rect;

    // ── Temporary diagnostic: dump all 2D commands when scoreboard first appears ──
    {
        static bool sb_dump_done = false;
        bool sb_vis = (Godot_SB_IsVisible() != 0);
        if (sb_vis && !sb_dump_done) {
            sb_dump_done = true;
            UtilityFunctions::print(String("[SB-DIAG] Scoreboard visible, dumping ") +
                                    String::num(cmd_count) + String(" 2D commands:"));
            UtilityFunctions::print(String("[SB-DIAG] vid_area=") + String::num(vid_area));
            for (int di = 0; di < cmd_count && di < 200; di++) {
                int dtype, dshader;
                float dx, dy, dw, dh, ds1, dt1, ds2, dt2, dcol[4];
                if (!Godot_Renderer_Get2DCmd(di, &dtype, &dx, &dy, &dw, &dh,
                                              &ds1, &dt1, &ds2, &dt2, dcol, &dshader)) continue;
                const char *tname = (dtype == 0 && dshader > 0) ? Godot_Renderer_GetShaderName(dshader) : "";
                if (!tname) tname = "";
                UtilityFunctions::print(String("[SB-DIAG] cmd[") + String::num(di) + String("] type=") +
                                        String::num(dtype) + String(" pos=(") +
                                        String::num(dx, 1) + String(",") + String::num(dy, 1) +
                                        String(") size=(") + String::num(dw, 1) + String(",") +
                                        String::num(dh, 1) + String(") col=(") +
                                        String::num(dcol[0], 3) + String(",") + String::num(dcol[1], 3) +
                                        String(",") + String::num(dcol[2], 3) + String(",") +
                                        String::num(dcol[3], 3) + String(") shader=") +
                                        String::num(dshader) + String(" '") + String(tname) + String("'"));
            }
        }
    }

    /* Gather HUD model draw orders so we can inject viewport textures
     * at the correct position in the 2D command stream. */
    int hud_model_count = Godot_Renderer_GetHudModelCount();
    int next_hud_model = 0;  /* index of next HUD model to inject */

    for (int i = 0; i < cmd_count; i++) {
        /* ── Inject HUD model viewport textures at their recorded position ──
         * The engine called CL_Draw3DModel (→ GR_RenderScene with RDF_NOWORLDMODEL)
         * at a specific point in the 2D draw stream. We inject the viewport
         * texture here so it appears between the background fills and the
         * foreground UI widgets (dropdowns, etc.). */
        while (next_hud_model < hud_model_count) {
            int draw_order = Godot_Renderer_GetHudModelDrawOrder(next_hud_model);
            if (draw_order > i) break;  /* not yet time for this model */

            if (next_hud_model < (int)hud_model_viewports.size() &&
                hud_model_viewports[next_hud_model]) {
                SubViewport *vp = hud_model_viewports[next_hud_model];
                Ref<ViewportTexture> vp_tex = vp->get_texture();
                if (vp_tex.is_valid()) {
                    float rect[4];
                    Godot_Renderer_GetHudModel(next_hud_model,
                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                        rect, nullptr, nullptr, nullptr);
                    Rect2 screen_rect(
                        ui_offset_x + rect[0] * ui_scale_x,
                        ui_offset_y + rect[1] * ui_scale_y,
                        rect[2] * ui_scale_x,
                        rect[3] * ui_scale_y);
                    RID hm_ci = get_segment_ci(BLEND_MIX);
                    rs->canvas_item_add_texture_rect(hm_ci, screen_rect, vp_tex->get_rid());
                }
            }
            next_hud_model++;
        }

        int type, shader;
        float x, y, w, h, s1, t1, s2, t2, color[4];

        if (!Godot_Renderer_Get2DCmd(i, &type, &x, &y, &w, &h,
                                      &s1, &t1, &s2, &t2, color, &shader)) {
            continue;
        }

        /* Draw loading background exactly at the command-stream index recorded
         * by GR_DrawBackground so layering matches OpenMoHAA.
         * Menu backdrops/clears before this remain underneath;
         * frame/text/loading widgets after this draw above it. */
        if (has_loading_bg && i == bg_cmd_index) {
            RID bg_ci = get_segment_ci(BLEND_MIX);
            Rect2 bg_rect(ui_offset_x, ui_offset_y,
                          (float)ui_vid_w * ui_scale_x,
                          (float)ui_vid_h * ui_scale_y);
            if (scissor_enabled) {
                bg_rect = bg_rect.intersection(scissor_rect);
            }
            if (bg_rect.size.x <= 0.0f || bg_rect.size.y <= 0.0f) {
                continue;
            }
            rs->canvas_item_add_texture_rect(bg_ci, bg_rect, loading_bg_tex->get_rid());
        }

        // no-op: loading bg fill suppression removed — bg now drawn at correct Z position

        // Scale from engine coords to actual viewport (with aspect correction)
        Rect2 rect(ui_offset_x + x * ui_scale_x, ui_offset_y + y * ui_scale_y,
                   w * ui_scale_x, h * ui_scale_y);
        Color col(color[0], color[1], color[2], color[3]);

        if (type == 2) {
            // GR_2D_SCISSOR — clip subsequent draw calls.
            // Coordinates are in engine space; apply the same transform
            // as other 2D commands before clipping.
            if (w <= 0.0f || h <= 0.0f) {
                scissor_enabled = false;
            } else {
                scissor_enabled = true;
                scissor_rect = rect;
            }
            continue;
        }

        Rect2 draw_rect = rect;
        if (scissor_enabled) {
            draw_rect = draw_rect.intersection(scissor_rect);
            if (draw_rect.size.x <= 0.0f || draw_rect.size.y <= 0.0f) {
                continue;
            }
        }

        if (type == 1) {
            // GR_2D_BOX — solid colour rectangle (always mix blend)
            RID box_ci = get_segment_ci(BLEND_MIX);
            rs->canvas_item_add_rect(box_ci, draw_rect, col);
        } else if (type == 0 && shader > 0) {
            // GR_2D_STRETCHPIC — textured quad
            Ref<ImageTexture> tex = get_shader_texture(shader);

            if (tex.is_valid()) {
                RID tex_rid = tex->get_rid();
                float tw = (float)tex->get_width();
                float th = (float)tex->get_height();
                Rect2 src(s1 * tw, t1 * th, (s2 - s1) * tw, (t2 - t1) * th);

                if (scissor_enabled) {
                    if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) {
                        continue;
                    }

                    float u0 = (draw_rect.position.x - rect.position.x) / rect.size.x;
                    float v0 = (draw_rect.position.y - rect.position.y) / rect.size.y;
                    float u1 = (draw_rect.position.x + draw_rect.size.x - rect.position.x) / rect.size.x;
                    float v1 = (draw_rect.position.y + draw_rect.size.y - rect.position.y) / rect.size.y;

                    src.position.x += src.size.x * u0;
                    src.position.y += src.size.y * v0;
                    src.size.x *= (u1 - u0);
                    src.size.y *= (v1 - v0);
                }

                // ── Apply shader stage rgbGen/alphaGen semantics to draw colour ──
                //
                //All 2D overlay shaders originate from RE_RegisterShaderNoMip
                // which uses LIGHTMAP_2D.  FinishShader for LIGHTMAP_2D sets
                // CGEN_GLOBAL_COLOR + AGEN_GLOBAL_ALPHA, so implicit shaders
                // (no .shader definition) respect SetColor automatically.
                //
                // We use Godot_ShaderProps_Find_2D() which resolves with
                // LIGHTMAP_2D — exact parity with RE_RegisterShaderNoMip.
                //
                // Summary:
                //   GLOBAL_COLOR (LIGHTMAP_2D default)  → SetColor
                //   IDENTITY / IDENTITY_LIGHTING        → white (explicit .shader)
                //   CONST                               → explicit constant
                Color draw_col = col;
                const char *sname = Godot_Renderer_GetShaderName(shader);

                // Check nomip flag: 1 = RE_RegisterShaderNoMip (UI decorations) → LIGHTMAP_2D
                //                   0 = RE_RegisterShader (photos, backdrops)   → LIGHTMAP_NONE
                int is_nomip = Godot_Renderer_IsShaderNoMip(shader);

                // DEBUG: Print nomip flag for loading screen shaders
                if (sname && sname[0] && strstr(sname, "loadingbar")) {
                    UtilityFunctions::print("[2D STRETCHPIC] shader='", sname, "' nomip=", is_nomip);
                }

                if (sname && sname[0]) {
                    const GodotShaderProps *sp = is_nomip ? Godot_ShaderProps_Find_2D(sname)
                                                          : Godot_ShaderProps_Find(sname);
                    if (sp && sp->stage_count > 0) {
                        for (int st = 0; st < sp->stage_count; st++) {
                            if (!sp->stages[st].active) continue;
                            if (sp->stages[st].isLightmap) continue;
                            const MohaaShaderStage *stg = &sp->stages[st];

                            // rgbGen
                            if (stg->rgbGen == STAGE_RGBGEN_CONST) {
                                draw_col.r = stg->rgbConst[0];
                                draw_col.g = stg->rgbConst[1];
                                draw_col.b = stg->rgbConst[2];
                            } else if (stg->rgbGen == STAGE_RGBGEN_GLOBAL_COLOR) {
                                // Use SetColor (draw_col = col already)
                            } else if (stg->rgbGen == STAGE_RGBGEN_IDENTITY ||
                                       stg->rgbGen == STAGE_RGBGEN_IDENTITY_LIGHTING) {
                                draw_col.r = 1.0f;
                                draw_col.g = 1.0f;
                                draw_col.b = 1.0f;
                            }

                            // alphaGen
                            if (stg->alphaGen == STAGE_ALPHAGEN_CONST) {
                                draw_col.a = stg->alphaConst;
                            } else if (stg->alphaGen == STAGE_ALPHAGEN_GLOBAL_ALPHA) {
                                // Use SetColor alpha (draw_col.a = col.a already)
                            } else if (stg->alphaGen == STAGE_ALPHAGEN_IDENTITY) {
                                draw_col.a = 1.0f;
                            }
                            break;  // Only process first non-lightmap stage
                        }
                    }
                }

                // Skip fully transparent draws (alphaConst=0 → invisible)
                if (draw_col.a < 0.001f) continue;

                /* Choose blend mode based on shader transparency.
                 * - SHADER_MULTIPLICATIVE: blendFunc filter (dst*src)
                 * - SHADER_MULTIPLICATIVE_INV: dst*(1-src)
                 * - SHADER_ADDITIVE: blendFunc add (src+dst)
                 * - SHADER_OPAQUE: the real renderer disables GL_BLEND
                 *   (GLS_DEFAULT stateBits), making texture alpha
                 *   irrelevant.  BLEND_OPAQUE ignores texture alpha
                 *   but preserves SetColor alpha (COLOR.a).
                 * - SHADER_ALPHA_BLEND: standard alpha blending. */
                int draw_blend = BLEND_MIX;
                if (sname && sname[0]) {
                    const GodotShaderProps *sp2 = is_nomip ? Godot_ShaderProps_Find_2D(sname)
                                                           : Godot_ShaderProps_Find(sname);
                    if (sp2) {
                        if (sp2->transparency == SHADER_MULTIPLICATIVE) {
                            draw_blend = BLEND_MUL;
                        } else if (sp2->transparency == SHADER_MULTIPLICATIVE_INV) {
                            draw_blend = BLEND_MUL_INV;
                        } else if (sp2->transparency == SHADER_ADDITIVE) {
                            draw_blend = BLEND_ADD;
                        } else if (sp2->transparency == SHADER_ALPHA_BLEND_INV) {
                            draw_blend = BLEND_ALPHA_INV;
                        } else if (sp2->transparency == SHADER_OPAQUE) {
                            draw_blend = BLEND_OPAQUE;

                            /* Multi-stage shader: check the actual texture stage
                             * for a custom blendFunc that overrides the opaque
                             * default (e.g. GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA).
                             * Skip internal engine images: the shader accessor
                             * stores $whiteimage as "*white", $lightmap as
                             * "*lightmap", etc.  These are first-pass fill stages
                             * (e.g. mohdm* levelshots write white in stage 0,
                             * then alpha-blend the screenshot in stage 1). */
                            if (sp2->stage_count > 1) {
                                for (int st = 0; st < sp2->stage_count; st++) {
                                    if (sp2->stages[st].isLightmap) continue;
                                    const char *sm = sp2->stages[st].map;
                                    if (!sm[0]) continue;
                                    if (strcmp(sm, "$lightmap") == 0) continue;
                                    if (strcmp(sm, "$whiteimage") == 0) continue;
                                    if (sm[0] == '*') continue;

                                    if (sp2->stages[st].blendSrc == BLEND_ONE_MINUS_SRC_ALPHA &&
                                        sp2->stages[st].blendDst == BLEND_SRC_ALPHA) {
                                        draw_blend = BLEND_ALPHA_INV;
                                    } else if (sp2->stages[st].blendSrc == BLEND_SRC_ALPHA &&
                                               sp2->stages[st].blendDst == BLEND_ONE_MINUS_SRC_ALPHA) {
                                        draw_blend = BLEND_MIX;
                                    } else if (sp2->stages[st].blendSrc == BLEND_ONE &&
                                               sp2->stages[st].blendDst == BLEND_ONE) {
                                        draw_blend = BLEND_ADD;
                                    }
                                    break;
                                }
                            }
                        }

                        /* NOTE: AGEN_GLOBAL_ALPHA does NOT mean "ignore texture alpha".
                         * In the real renderer (tr_shade.c), AGEN_GLOBAL_ALPHA sets the
                         * VERTEX alpha to SetColor.a.  The fragment output is still
                         * texture × vertex (GL_MODULATE), so fragment.a = tex.a × SetColor.a.
                         * Blending then uses GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA, which
                         * means texture alpha IS part of the final compositing.
                         * Using BLEND_OPAQUE here would discard texture alpha, breaking
                         * UI images with meaningful transparency (e.g. loading screen
                         * photo frames, menu backdrops).  Keep BLEND_MIX. */

                        /* Texture-alpha parity for implicit/UI shaders:
                         * - If blend resolved to OPAQUE but the loaded texture has
                         *   meaningful alpha, use BLEND_MIX so cutout/soft edges
                         *   render correctly (e.g. loadingbar_border, overlays).
                         * - If blend is MIX but texture has no alpha, collapse to
                         *   OPAQUE for exact opaque output. */
                        auto ha_it = shader_texture_has_alpha.find(shader);
                        if (ha_it != shader_texture_has_alpha.end()) {
                            const bool tex_has_alpha = ha_it->second;
                            if (draw_blend == BLEND_OPAQUE && tex_has_alpha) {
                                draw_blend = BLEND_MIX;
                            } else if (draw_blend == BLEND_MIX && !tex_has_alpha) {
                                draw_blend = BLEND_OPAQUE;
                            }
                        }

                    }
                }

                // ALPHA_INV: pre-composite on CPU in sRGB space to avoid
                // GPU sRGB decode/encode colour space mismatches.
                // The two-stage pipeline (white fill + inverse alpha blend)
                // is baked into an opaque RGB image, then drawn normally.
                if (draw_blend == BLEND_ALPHA_INV) {
                    auto it = s_alpha_inv_tex_cache.find(shader);
                    if (it == s_alpha_inv_tex_cache.end()) {
                        // Re-load the raw Image from the texture (before GPU upload)
                        Ref<Image> src_img = tex->get_image();
                        if (src_img.is_valid() && !src_img->is_empty()) {
                            src_img = src_img->duplicate();
                            if (src_img->get_format() != Image::FORMAT_RGBA8) {
                                src_img->convert(Image::FORMAT_RGBA8);
                            }
                            int iw = src_img->get_width();
                            int ih = src_img->get_height();
                            // Create pre-composited RGB8 image
                            Ref<Image> comp_img;
                            comp_img.instantiate();
                            PackedByteArray src_data = src_img->get_data();
                            const uint8_t *sp = src_data.ptr();
                            PackedByteArray dst_data;
                            dst_data.resize(iw * ih * 3);
                            uint8_t *dp = dst_data.ptrw();
                            for (int px = 0; px < iw * ih; px++) {
                                uint8_t r = sp[px * 4 + 0];
                                uint8_t g = sp[px * 4 + 1];
                                uint8_t b = sp[px * 4 + 2];
                                uint8_t a = sp[px * 4 + 3];
                                // result = tex*(1-a/255) + 255*(a/255)
                                //        = tex*(255-a)/255 + a
                                dp[px * 3 + 0] = (uint8_t)((r * (255 - a) + 255 * a + 127) / 255);
                                dp[px * 3 + 1] = (uint8_t)((g * (255 - a) + 255 * a + 127) / 255);
                                dp[px * 3 + 2] = (uint8_t)((b * (255 - a) + 255 * a + 127) / 255);
                            }
                            comp_img = Image::create_from_data(iw, ih, false, Image::FORMAT_RGB8, dst_data);
                            comp_img->generate_mipmaps();
                            Ref<ImageTexture> comp_tex = ImageTexture::create_from_image(comp_img);
                            s_alpha_inv_tex_cache[shader] = comp_tex;
                            it = s_alpha_inv_tex_cache.find(shader);
                        }
                    }
                    if (it != s_alpha_inv_tex_cache.end() && it->second.is_valid()) {
                        tex_rid = it->second->get_rid();
                        tw = (float)it->second->get_width();
                        th = (float)it->second->get_height();
                        src = Rect2(s1 * tw, t1 * th, (s2 - s1) * tw, (t2 - t1) * th);
                    }
                    draw_blend = BLEND_OPAQUE;
                }

                // ── One-shot 2D texture diagnostic ──
                {
                    static std::unordered_set<int> logged_2d_shaders;
                    if (logged_2d_shaders.find(shader) == logged_2d_shaders.end() && sname && sname[0]) {
                        logged_2d_shaders.insert(shader);
                        const char *fmt_name = "?";
                        if (tex.is_valid()) {
                            Ref<Image> dbg_img = tex->get_image();
                            if (dbg_img.is_valid()) {
                                int f = (int)dbg_img->get_format();
                                fmt_name = (f == (int)Image::FORMAT_RGB8) ? "RGB8" :
                                           (f == (int)Image::FORMAT_RGBA8) ? "RGBA8" : "other";
                            }
                        }
                        const char *blend_name = (draw_blend == BLEND_OPAQUE) ? "OPAQUE" :
                                                 (draw_blend == BLEND_MIX) ? "MIX" :
                                                 (draw_blend == BLEND_ADD) ? "ADD" :
                                                 (draw_blend == BLEND_MUL) ? "MUL" : "OTHER";
                        const GodotShaderProps *dbg_sp = is_nomip ? Godot_ShaderProps_Find_2D(sname)
                                                                  : Godot_ShaderProps_Find(sname);
                        int dbg_transp = dbg_sp ? dbg_sp->transparency : -1;
                        int dbg_ag = -1;
                        if (dbg_sp && dbg_sp->stage_count > 0) {
                            for (int ds = 0; ds < dbg_sp->stage_count; ds++) {
                                if (dbg_sp->stages[ds].isLightmap) continue;
                                const char *dm = dbg_sp->stages[ds].map;
                                if (!dm[0] || dm[0] == '*') continue;
                                dbg_ag = (int)dbg_sp->stages[ds].alphaGen;
                                break;
                            }
                        }
                        auto ha_it2 = shader_texture_has_alpha.find(shader);
                        bool has_a = (ha_it2 != shader_texture_has_alpha.end()) && ha_it2->second;
                        UtilityFunctions::print(String("[2D-TEX] shader='") + String(sname) +
                            String("' fmt=") + String(fmt_name) +
                            String(" blend=") + String(blend_name) +
                            String(" transp=") + String::num_int64(dbg_transp) +
                            String(" alphaGen=") + String::num_int64(dbg_ag) +
                            String(" has_alpha=") + String(has_a ? "Y" : "N") +
                            String(" col.a=") + String::num(draw_col.a, 3) +
                            String(" tex=") + String::num(tw,0) + String("x") + String::num(th,0));
                    }
                }

                RID target_ci = get_segment_ci(draw_blend);

                // Detect tiling: if the source rect extends beyond the
                // texture dimensions, the engine expects GL_REPEAT wrapping
                // (e.g. statbar_tileshader for ammo bullet icons).
                // Godot's canvas_item_add_texture_rect_region clamps source rects
                // to image bounds, so tiled draws need special handling.
                bool needs_tiling = (src.position.x + src.size.x > tw + 0.5f) ||
                                    (src.position.y + src.size.y > th + 0.5f);

                if (needs_tiling && tw > 0.0f && th > 0.0f &&
                    draw_rect.size.x > 0.0f && draw_rect.size.y > 0.0f) {
                    // Manual tiling: emit one draw per tile copy.
                    // UV range in normalised [0..N] coordinates
                    float u0 = s1;
                    float v0 = t1;
                    float u1 = s2;
                    float v1 = t2;

                    float total_u = u1 - u0;
                    float total_v = v1 - v0;
                    if (total_u <= 0.0f) total_u = 1.0f;
                    if (total_v <= 0.0f) total_v = 1.0f;

                    // Output pixels per UV unit
                    float px_per_u = draw_rect.size.x / total_u;
                    float px_per_v = draw_rect.size.y / total_v;

                    int max_tiles = 256; // safety guard
                    int tile_count = 0;
                    float cur_v = v0;
                    float out_y = draw_rect.position.y;
                    while (cur_v < v1 - 0.0001f && tile_count < max_tiles) {
                        float frac_v = cur_v - floorf(cur_v);
                        float remain_v = v1 - cur_v;
                        float span_v = 1.0f - frac_v;
                        if (span_v > remain_v) span_v = remain_v;
                        float row_h = span_v * px_per_v;

                        float cur_u = u0;
                        float out_x = draw_rect.position.x;
                        while (cur_u < u1 - 0.0001f && tile_count < max_tiles) {
                            float frac_u = cur_u - floorf(cur_u);
                            float remain_u = u1 - cur_u;
                            float span_u = 1.0f - frac_u;
                            if (span_u > remain_u) span_u = remain_u;
                            float col_w = span_u * px_per_u;

                            Rect2 tile_dst(out_x, out_y, col_w, row_h);
                            Rect2 tile_src(frac_u * tw, frac_v * th,
                                           span_u * tw, span_v * th);

                            if (scissor_enabled) {
                                Rect2 clipped = tile_dst.intersection(scissor_rect);
                                if (clipped.size.x > 0.5f && clipped.size.y > 0.5f &&
                                    tile_dst.size.x > 0.0f && tile_dst.size.y > 0.0f) {
                                    float cu0 = (clipped.position.x - tile_dst.position.x) / tile_dst.size.x;
                                    float cv0 = (clipped.position.y - tile_dst.position.y) / tile_dst.size.y;
                                    float cu1 = cu0 + clipped.size.x / tile_dst.size.x;
                                    float cv1 = cv0 + clipped.size.y / tile_dst.size.y;
                                    Rect2 cs(tile_src.position.x + tile_src.size.x * cu0,
                                             tile_src.position.y + tile_src.size.y * cv0,
                                             tile_src.size.x * (cu1 - cu0),
                                             tile_src.size.y * (cv1 - cv0));
                                    rs->canvas_item_add_texture_rect_region(target_ci, clipped, tex_rid, cs, draw_col);
                                }
                            } else {
                                rs->canvas_item_add_texture_rect_region(target_ci, tile_dst, tex_rid, tile_src, draw_col);
                            }

                            tile_count++;
                            cur_u += span_u;
                            out_x += col_w;
                        }
                        cur_v += span_v;
                        out_y += row_h;
                    }
                } else {
                    // Use triangle_array with explicit per-vertex colours for
                    // ALL non-tiled textured 2D draws.  Godot 4.2's
                    // canvas_item_add_texture_rect_region passes draw_col
                    // via "modulate" which does not reliably reach the shader
                    // as COLOR, making font text (white-RGB + alpha mask ×
                    // black SetColor) appear as white instead of black.
                    // triangle_array with a PackedColorArray always works
                    // because the colours are genuine vertex attributes.
                    {
                        float dx = draw_rect.position.x;
                        float dy = draw_rect.position.y;
                        float dw = draw_rect.size.x;
                        float dh = draw_rect.size.y;

                        PackedVector2Array pts;
                        PackedVector2Array fuv;
                        PackedColorArray   cols;
                        PackedInt32Array   idx;

                        pts.resize(4);
                        fuv.resize(4);
                        cols.resize(4);
                        idx.resize(6);

                        pts[0] = Vector2(dx,      dy);
                        pts[1] = Vector2(dx + dw,  dy);
                        pts[2] = Vector2(dx + dw,  dy + dh);
                        pts[3] = Vector2(dx,       dy + dh);

                        bool need_flip = (src.size.x < 0.0f || src.size.y < 0.0f);
                        if (need_flip) {
                            // Raw s1/t1/s2/t2 encode the flip direction
                            fuv[0] = Vector2(s1, t1);
                            fuv[1] = Vector2(s2, t1);
                            fuv[2] = Vector2(s2, t2);
                            fuv[3] = Vector2(s1, t2);
                        } else {
                            // Normal draw: convert scissor-adjusted pixel-space
                            // src rect to normalised UVs
                            float fu0 = src.position.x / tw;
                            float fv0 = src.position.y / th;
                            float fu1 = (src.position.x + src.size.x) / tw;
                            float fv1 = (src.position.y + src.size.y) / th;
                            fuv[0] = Vector2(fu0, fv0);
                            fuv[1] = Vector2(fu1, fv0);
                            fuv[2] = Vector2(fu1, fv1);
                            fuv[3] = Vector2(fu0, fv1);
                        }

                        cols[0] = cols[1] = cols[2] = cols[3] = draw_col;

                        idx[0] = 0; idx[1] = 1; idx[2] = 2;
                        idx[3] = 0; idx[4] = 2; idx[5] = 3;

                        rs->canvas_item_add_triangle_array(target_ci, idx, pts, cols, fuv, PackedInt32Array(), PackedFloat32Array(), tex_rid, -1);
                    }
                }
            }
            // If texture not loaded, skip — don't draw opaque coloured rect fallback
        } else if (type == 0) {
            // StretchPic with no shader — parity path: draw as solid rect.
            RID noshader_ci = get_segment_ci(BLEND_MIX);
            rs->canvas_item_add_rect(noshader_ci, draw_rect, col);
        } else if (type == 3 && shader > 0) {
            // GR_2D_TRIANGLE — textured triangle (compass, needle, circle, spinner)
            float tri_verts[6], tri_uvs[6];
            if (Godot_Renderer_Get2DCmdTriVerts(i, tri_verts, tri_uvs)) {
                Ref<ImageTexture> tex = get_shader_texture(shader);
                if (tex.is_valid()) {
                    // Transform triangle vertices from engine coords to viewport
                    PackedVector2Array points;
                    PackedVector2Array uvs;
                    PackedColorArray colors_arr;
                    points.resize(3);
                    uvs.resize(3);
                    colors_arr.resize(3);

                    Color draw_col = col;
                    // Apply shader stage rgbGen/alphaGen like STRETCHPIC
                    const char *sname = Godot_Renderer_GetShaderName(shader);
                    int is_nomip = Godot_Renderer_IsShaderNoMip(shader);
                    if (sname && sname[0]) {
                        const GodotShaderProps *sp = is_nomip ? Godot_ShaderProps_Find_2D(sname)
                                                              : Godot_ShaderProps_Find(sname);
                        if (sp && sp->stage_count > 0) {
                            for (int st = 0; st < sp->stage_count; st++) {
                                if (!sp->stages[st].active) continue;
                                if (sp->stages[st].isLightmap) continue;
                                const MohaaShaderStage *stg = &sp->stages[st];
                                if (stg->rgbGen == STAGE_RGBGEN_IDENTITY ||
                                    stg->rgbGen == STAGE_RGBGEN_IDENTITY_LIGHTING) {
                                    draw_col.r = draw_col.g = draw_col.b = 1.0f;
                                } else if (stg->rgbGen == STAGE_RGBGEN_CONST) {
                                    draw_col.r = stg->rgbConst[0];
                                    draw_col.g = stg->rgbConst[1];
                                    draw_col.b = stg->rgbConst[2];
                                }
                                if (stg->alphaGen == STAGE_ALPHAGEN_IDENTITY) {
                                    draw_col.a = 1.0f;
                                } else if (stg->alphaGen == STAGE_ALPHAGEN_CONST) {
                                    draw_col.a = stg->alphaConst;
                                }
                                break;
                            }
                        }
                    }
                    if (draw_col.a < 0.001f) continue;

                    for (int v = 0; v < 3; v++) {
                        float vx = ui_offset_x + tri_verts[v*2+0] * ui_scale_x;
                        float vy = ui_offset_y + tri_verts[v*2+1] * ui_scale_y;
                        points.set(v, Vector2(vx, vy));
                        uvs.set(v, Vector2(tri_uvs[v*2+0], tri_uvs[v*2+1]));
                        colors_arr.set(v, draw_col);
                    }

                    // Choose blend mode based on shader transparency
                    int draw_blend = BLEND_MIX;
                    if (sname && sname[0]) {
                        const GodotShaderProps *sp2 = is_nomip ? Godot_ShaderProps_Find_2D(sname)
                                                               : Godot_ShaderProps_Find(sname);
                        if (sp2) {
                            if (sp2->transparency == SHADER_MULTIPLICATIVE) {
                                draw_blend = BLEND_MUL;
                            } else if (sp2->transparency == SHADER_MULTIPLICATIVE_INV) {
                                draw_blend = BLEND_MUL_INV;
                            } else if (sp2->transparency == SHADER_ADDITIVE) {
                                draw_blend = BLEND_ADD;
                            } else if (sp2->transparency == SHADER_ALPHA_BLEND_INV) {
                                draw_blend = BLEND_ALPHA_INV;
                            } else if (sp2->transparency == SHADER_OPAQUE) {
                                draw_blend = BLEND_OPAQUE;
                                if (sp2->stage_count > 1) {
                                    for (int st = 0; st < sp2->stage_count; st++) {
                                        if (sp2->stages[st].isLightmap) continue;
                                        const char *sm = sp2->stages[st].map;
                                        if (!sm[0]) continue;
                                        if (strcmp(sm, "$lightmap") == 0) continue;
                                        if (strcmp(sm, "$whiteimage") == 0) continue;
                                        if (sp2->stages[st].blendSrc == BLEND_ONE_MINUS_SRC_ALPHA &&
                                            sp2->stages[st].blendDst == BLEND_SRC_ALPHA) {
                                            draw_blend = BLEND_ALPHA_INV;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Texture-alpha parity (same rationale as stretch-pic path)
                    auto ha_it = shader_texture_has_alpha.find(shader);
                    if (ha_it != shader_texture_has_alpha.end()) {
                        const bool tex_has_alpha = ha_it->second;
                        if (draw_blend == BLEND_OPAQUE && tex_has_alpha) {
                            draw_blend = BLEND_MIX;
                        } else if (draw_blend == BLEND_MIX && !tex_has_alpha) {
                            draw_blend = BLEND_OPAQUE;
                        }
                    }

                    RID target_ci = get_segment_ci(draw_blend);

                    RID tex_rid = tex->get_rid();
                    rs->canvas_item_add_polygon(target_ci, points, colors_arr, uvs, tex_rid);
                }
            }
        }
    }

    if (has_loading_bg && bg_cmd_index >= cmd_count) {
        RID bg_ci = get_segment_ci(BLEND_MIX);
        Rect2 bg_rect(ui_offset_x, ui_offset_y,
                      (float)ui_vid_w * ui_scale_x,
                      (float)ui_vid_h * ui_scale_y);
        if (scissor_enabled) {
            bg_rect = bg_rect.intersection(scissor_rect);
        }
        if (bg_rect.size.x > 0.0f && bg_rect.size.y > 0.0f) {
            rs->canvas_item_add_texture_rect(bg_ci, bg_rect, loading_bg_tex->get_rid());
        }
    }

    /* Flush any remaining HUD model viewport textures whose draw_order
     * was >= cmd_count (e.g. model draw was the last operation). */
    while (next_hud_model < hud_model_count) {
        if (next_hud_model < (int)hud_model_viewports.size() &&
            hud_model_viewports[next_hud_model]) {
            SubViewport *vp = hud_model_viewports[next_hud_model];
            Ref<ViewportTexture> vp_tex = vp->get_texture();
            if (vp_tex.is_valid()) {
                float rect[4];
                Godot_Renderer_GetHudModel(next_hud_model,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    rect, nullptr, nullptr, nullptr);
                Rect2 screen_rect(
                    ui_offset_x + rect[0] * ui_scale_x,
                    ui_offset_y + rect[1] * ui_scale_y,
                    rect[2] * ui_scale_x,
                    rect[3] * ui_scale_y);
                RID hm_ci2 = get_segment_ci(BLEND_MIX);
                rs->canvas_item_add_texture_rect(hm_ci2, screen_rect, vp_tex->get_rid());
            }
        }
        next_hud_model++;
    }

    static bool logged_2d = false;
    if (!logged_2d && cmd_count > 0) {
        UtilityFunctions::print(String("[MoHAA] 2D overlay: ") +
                                String::num_int64(cmd_count) + String(" draw commands"));
        logged_2d = true;
    }
}

// ──────────────────────────────────────────────
//  Audio bridge (Phase 8)
// ──────────────────────────────────────────────

void MoHAARunner::setup_audio() {
    if (!game_world) return;

    // Audio root container
    audio_root = memnew(Node3D);
    audio_root->set_name("AudioRoot");
    game_world->add_child(audio_root);

    // AudioListener3D — positioned at the camera
    audio_listener = memnew(AudioListener3D);
    audio_listener->set_name("Listener");
    audio_root->add_child(audio_listener);
    audio_listener->make_current();

    // Create 3D player pool
    for (int i = 0; i < MAX_3D_PLAYERS; i++) {
        AudioStreamPlayer3D *p = memnew(AudioStreamPlayer3D);
        p->set_name(String("SFX3D_") + String::num_int64(i));
        p->set_max_distance(2000.0f);  // ~78,000 engine units in metres
        p->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
        p->set_unit_size(1.0f);
        p->set_max_polyphony(1);
        p->set_doppler_tracking(AudioStreamPlayer3D::DOPPLER_TRACKING_IDLE_STEP);
        audio_root->add_child(p);
        sfx_players_3d.push_back(p);
    }

    // Create 2D player pool (UI sounds, local sounds)
    for (int i = 0; i < MAX_2D_PLAYERS; i++) {
        AudioStreamPlayer *p = memnew(AudioStreamPlayer);
        p->set_name(String("SFX2D_") + String::num_int64(i));
        p->set_max_polyphony(1);
        add_child(p);  // 2D players live on this Node, not in 3D tree
        sfx_players_2d.push_back(p);
    }

    UtilityFunctions::print("[MoHAA] Audio bridge initialised: " +
                            String::num_int64(MAX_3D_PLAYERS) + " 3D + " +
                            String::num_int64(MAX_2D_PLAYERS) + " 2D players.");

    // Initialise player slot tracking (Phase 41)
    player_slot_info.resize(MAX_3D_PLAYERS);

    // Music player (Phase 17)
    music_player = memnew(AudioStreamPlayer);
    music_player->set_name("MusicPlayer");
    music_player->set_bus(StringName("Master"));
    add_child(music_player);
    UtilityFunctions::print("[MoHAA] Music player initialised.");

    // ── Phase 45: Initialise ubersound alias system ──
#ifdef HAS_UBERSOUND_MODULE
    Godot_Ubersound_Init();
    UtilityFunctions::print("[MoHAA] Ubersound accessor ready (aliases loaded by cgame at map load).");
#endif

    // ── Phase 48: Enable sound occlusion ──
#ifdef HAS_SOUND_OCCLUSION_MODULE
    Godot_SoundOcclusion_SetEnabled(1);
#endif
}

Ref<AudioStream> MoHAARunner::load_wav_from_vfs(int sfxHandle) {
    // Check cache first
    auto it = sfx_cache.find(sfxHandle);
    if (it != sfx_cache.end()) return it->second;

    // Look up name from the sound registry
    int idx = Godot_Sound_FindSfxIndex(sfxHandle);
    if (idx < 0) return Ref<AudioStream>();

    const char *snd_name = Godot_Sound_GetSfxName(idx);
    if (!snd_name || !snd_name[0]) return Ref<AudioStream>();

    // Try loading via VFS — sound files may or may not have "sound/" prefix
    void *buf = nullptr;
    long len = Godot_VFS_ReadFile(snd_name, &buf);

    // If that failed, try with "sound/" prefix
    if (len <= 0 && strncmp(snd_name, "sound/", 6) != 0) {
        char prefixed[256];
        snprintf(prefixed, sizeof(prefixed), "sound/%s", snd_name);
        len = Godot_VFS_ReadFile(prefixed, &buf);
    }

    // ── Phase 45: Try ubersound alias resolution if direct load failed ──
#ifdef HAS_UBERSOUND_MODULE
    if (len <= 0 && Godot_Ubersound_IsLoaded()) {
        char resolved_path[512];
        float vol, mindist, maxdist, pitch;
        int channel;
        if (Godot_Ubersound_Resolve(snd_name, resolved_path, sizeof(resolved_path),
                                     &vol, &mindist, &maxdist, &pitch, &channel)) {
            len = Godot_VFS_ReadFile(resolved_path, &buf);
            if (len <= 0 && strncmp(resolved_path, "sound/", 6) != 0) {
                char prefixed[512];
                snprintf(prefixed, sizeof(prefixed), "sound/%s", resolved_path);
                len = Godot_VFS_ReadFile(prefixed, &buf);
            }
        }
    }
#endif

    if (len <= 0 || !buf) {
        // Cache a null ref so we don't keep retrying
        sfx_cache[sfxHandle] = Ref<AudioStream>();
        return Ref<AudioStream>();
    }

    const unsigned char *data = (const unsigned char *)buf;

    // Check if this is a raw MP3 file (starts with ID3 tag or MP3 sync word)
    bool is_raw_mp3 = false;
    if (len >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        is_raw_mp3 = true;
    } else if (len >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        is_raw_mp3 = true;
    }

    if (is_raw_mp3) {
        PackedByteArray mp3_data;
        mp3_data.resize(len);
        memcpy(mp3_data.ptrw(), data, len);
        Godot_VFS_FreeFile(buf);

        Ref<AudioStreamMP3> mp3;
        mp3.instantiate();
        mp3->set_data(mp3_data);
        sfx_cache[sfxHandle] = mp3;
        return mp3;
    }

    // Parse WAV header — RIFF/WAVE format
    // Minimum: RIFF(4) + size(4) + WAVE(4) + fmt (8+16) + data(8) = 44 bytes
    if (len < 44 ||
        data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F' ||
        data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') {
        Godot_VFS_FreeFile(buf);
        sfx_cache[sfxHandle] = Ref<AudioStream>();
        return Ref<AudioStream>();
    }

    // Find 'fmt ' chunk
    int fmt_offset = -1;
    int data_offset = -1;
    int data_size = 0;
    int pos = 12;

    while (pos + 8 <= (int)len) {
        int chunk_size = data[pos + 4] | (data[pos + 5] << 8) |
                         (data[pos + 6] << 16) | (data[pos + 7] << 24);

        if (data[pos] == 'f' && data[pos+1] == 'm' && data[pos+2] == 't' && data[pos+3] == ' ') {
            fmt_offset = pos + 8;
        } else if (data[pos] == 'd' && data[pos+1] == 'a' && data[pos+2] == 't' && data[pos+3] == 'a') {
            data_offset = pos + 8;
            data_size = chunk_size;
        }

        // Move to next chunk (align to 2 bytes)
        pos += 8 + ((chunk_size + 1) & ~1);
    }

    if (fmt_offset < 0 || data_offset < 0 || data_size <= 0) {
        Godot_VFS_FreeFile(buf);
        sfx_cache[sfxHandle] = Ref<AudioStream>();
        return Ref<AudioStream>();
    }

    // Read fmt chunk fields
    int audio_format = data[fmt_offset] | (data[fmt_offset + 1] << 8);
    int num_channels = data[fmt_offset + 2] | (data[fmt_offset + 3] << 8);
    int sample_rate  = data[fmt_offset + 4] | (data[fmt_offset + 5] << 8) |
                       (data[fmt_offset + 6] << 16) | (data[fmt_offset + 7] << 24);
    int bits_per_sample = data[fmt_offset + 14] | (data[fmt_offset + 15] << 8);

    // Phase 43: Handle MP3-in-WAV (format tag 0x0055)
    if (audio_format == 0x0055) {
        // Clamp data_size to available data
        if (data_offset + data_size > (int)len) {
            data_size = (int)len - data_offset;
        }
        // The data chunk contains raw MP3 frames
        PackedByteArray mp3_data;
        mp3_data.resize(data_size);
        memcpy(mp3_data.ptrw(), &data[data_offset], data_size);
        Godot_VFS_FreeFile(buf);

        Ref<AudioStreamMP3> mp3;
        mp3.instantiate();
        mp3->set_data(mp3_data);
        sfx_cache[sfxHandle] = mp3;
        return mp3;
    }

    // Support PCM (1) and IMA-ADPCM (17)
    AudioStreamWAV::Format godot_format;
    if (audio_format == 1) {
        // PCM
        if (bits_per_sample == 8)
            godot_format = AudioStreamWAV::FORMAT_8_BITS;
        else if (bits_per_sample == 16)
            godot_format = AudioStreamWAV::FORMAT_16_BITS;
        else {
            Godot_VFS_FreeFile(buf);
            sfx_cache[sfxHandle] = Ref<AudioStream>();
            return Ref<AudioStream>();
        }
    } else if (audio_format == 17) {
        godot_format = AudioStreamWAV::FORMAT_IMA_ADPCM;
    } else {
        // Unsupported format
        Godot_VFS_FreeFile(buf);
        sfx_cache[sfxHandle] = Ref<AudioStream>();
        return Ref<AudioStream>();
    }

    // Clamp data_size to available data
    if (data_offset + data_size > (int)len) {
        data_size = (int)len - data_offset;
    }

    // Build Godot AudioStreamWAV
    Ref<AudioStreamWAV> wav;
    wav.instantiate();
    wav->set_format(godot_format);
    wav->set_mix_rate(sample_rate);
    wav->set_stereo(num_channels >= 2);
    wav->set_loop_mode(AudioStreamWAV::LOOP_DISABLED);

    // Copy PCM data into PackedByteArray
    PackedByteArray pcm_data;
    pcm_data.resize(data_size);
    memcpy(pcm_data.ptrw(), &data[data_offset], data_size);
    wav->set_data(pcm_data);

    Godot_VFS_FreeFile(buf);

    // Cache it
    sfx_cache[sfxHandle] = wav;

    return wav;
}

/* ===================================================================
 *  Scoreboard overlay — previously rendered a custom layer at z=150.
 *
 *  The engine's own UI widget system (UIListCtrl + .urc menu widgets)
 *  already renders the complete scoreboard (DM_Scoreboard.urc etc.)
 *  through the 2D command buffer captured by update_2d_overlay() at
 *  layer 100.  Drawing a second copy on top at layer 150 causes the
 *  "washed out" / double-draw artefact visible as ghost text and
 *  excessive transparency.
 *
 *  The Godot_SB_* capture buffer still collects scoreboard data for
 *  potential future use (custom HUD, debug, etc.) but no custom
 *  rendering is performed here.
 * =================================================================== */

void MoHAARunner::update_scoreboard() {
    /* No custom rendering — the engine's .urc UI handles everything
     * through the 2D overlay system (update_2d_overlay at layer 100).
     * Hide any previously-created custom scoreboard layer. */
    bool show = Godot_SB_IsVisible() || scoreboard_visible;

    (void)show;
    if (scoreboard_layer) {
        scoreboard_layer->set_visible(false);
    }
}

/* ===================================================================
 *  Phase 148: HUD model preview rendering
 *
 *  CL_Draw3DModel() renders 3D model previews into UI widget rects
 *  (e.g., player model selection in multiplayer options).  We capture
 *  these requests in godot_renderer.c and render them here using a
 *  SubViewport + Camera3D + MeshInstance3D.  The viewport texture is
 *  drawn into the 2D HUD overlay at the widget's screen rect.
 * ================================================================ */

void MoHAARunner::update_hud_models() {
    int count = Godot_Renderer_GetHudModelCount();

    /* If no HUD models requested, hide/disable all preview slots. */
    if (count == 0) {
        for (size_t i = 0; i < hud_model_viewports.size(); i++) {
            if (hud_model_viewports[i]) {
                hud_model_viewports[i]->set_update_mode(SubViewport::UPDATE_DISABLED);
            }
            if (i < hud_model_meshes.size() && hud_model_meshes[i]) {
                hud_model_meshes[i]->set_visible(false);
            }
        }
        return;
    }

    /* Create missing SubViewport slots when multiple HUD models are requested. */
    while ((int)hud_model_viewports.size() < count) {
        int idx = (int)hud_model_viewports.size();

        SubViewport *vp = memnew(SubViewport);
        vp->set_name(String("HudModelViewport") + String::num_int64(idx));
        vp->set_transparent_background(true);
        vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
        vp->set_use_own_world_3d(true);
        add_child(vp);

        Camera3D *cam = memnew(Camera3D);
        cam->set_name(String("HudModelCamera") + String::num_int64(idx));
        cam->set_near(0.01);
        cam->set_far(100.0);
        vp->add_child(cam);

        WorldEnvironment *we = memnew(WorldEnvironment);
        we->set_name(String("HudModelWorldEnv") + String::num_int64(idx));
        Ref<Environment> env;
        env.instantiate();
        env->set_ambient_light_color(Color(1.0, 1.0, 1.0, 1.0));
        env->set_ambient_light_energy(1.7f);
        env->set_ambient_light_sky_contribution(0.0f);
        we->set_environment(env);
        vp->add_child(we);

        DirectionalLight3D *key = memnew(DirectionalLight3D);
        key->set_name(String("HudModelKeyLight") + String::num_int64(idx));
        key->set_color(Color(1.0, 1.0, 1.0, 1.0));
        key->set_param(Light3D::PARAM_ENERGY, 2.0f);
        key->set_rotation(Vector3(Math::deg_to_rad(-30.0f), Math::deg_to_rad(45.0f), 0.0f));
        vp->add_child(key);

        OmniLight3D *fill = memnew(OmniLight3D);
        fill->set_name(String("HudModelFillLight") + String::num_int64(idx));
        fill->set_color(Color(1.0, 0.97, 0.92, 1.0));
        fill->set_param(Light3D::PARAM_ENERGY, 2.6f);
        fill->set_param(Light3D::PARAM_RANGE, 6.0f);
        vp->add_child(fill);

        MeshInstance3D *mesh = memnew(MeshInstance3D);
        mesh->set_name(String("HudModelMesh") + String::num_int64(idx));
        mesh->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        vp->add_child(mesh);

        hud_model_viewports.push_back(vp);
        hud_model_cameras.push_back(cam);
        hud_model_world_envs.push_back(we);
        hud_model_key_lights.push_back(key);
        hud_model_fill_lights.push_back(fill);
        hud_model_meshes.push_back(mesh);
        hud_model_last_hmodels.push_back(-1);
        hud_model_last_anim_hashes.push_back(0);

        UtilityFunctions::print(String("[MoHAA] HUD model SubViewport slot created: ") + String::num_int64(idx));
    }

    /* Disable/clear unused slots this frame. */
    for (size_t i = (size_t)count; i < hud_model_viewports.size(); i++) {
        if (hud_model_viewports[i]) {
            hud_model_viewports[i]->set_update_mode(SubViewport::UPDATE_DISABLED);
        }
        if (hud_model_meshes[i]) {
            hud_model_meshes[i]->set_visible(false);
        }
    }

    /* Process every HUD model (e.g. both Allies + Axis previews in mpoptions). */
    for (int hud_idx = 0; hud_idx < count; hud_idx++) {
        float origin[3], axis[9], ent_scale = 1.0f;
        float rect[4], vieworg[3], viewaxis[9], fov[2];
        int hModel = 0;
        unsigned char rgba[4] = {255, 255, 255, 255};
        void *tiki = nullptr;

        if (!Godot_Renderer_GetHudModel(hud_idx, origin, axis, &ent_scale, &hModel, rgba, &tiki,
                                        rect, vieworg, viewaxis, fov)) {
            continue;
        }

        SubViewport *hud_model_viewport = hud_model_viewports[hud_idx];
        Camera3D *hud_model_camera = hud_model_cameras[hud_idx];
        OmniLight3D *hud_model_fill_light = hud_model_fill_lights[hud_idx];
        MeshInstance3D *hud_model_mesh = hud_model_meshes[hud_idx];

        /* Set viewport size proportional to widget rect */
        int vp_w = Math::max((int)rect[2], 64);
        int vp_h = Math::max((int)rect[3], 64);
        hud_model_viewport->set_size(Vector2i(vp_w, vp_h));
        hud_model_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);

        /* ── Camera setup ── */
        Vector3 cam_pos = id_to_godot_position(vieworg[0], vieworg[1], vieworg[2]);

        float *va_fwd = &viewaxis[0];
        float *va_lft = &viewaxis[3];
        float *va_up  = &viewaxis[6];
        Vector3 forward_g = id_to_godot_point(va_fwd[0], va_fwd[1], va_fwd[2]);
        Vector3 left_g    = id_to_godot_point(va_lft[0], va_lft[1], va_lft[2]);
        Vector3 up_g      = id_to_godot_point(va_up[0],  va_up[1],  va_up[2]);

        Vector3 right_g = -left_g;
        Vector3 back_g  = -forward_g;
        Basis cam_basis(right_g, up_g, back_g);
        hud_model_camera->set_global_transform(Transform3D(cam_basis, cam_pos));

        if (hud_model_fill_light) {
            // Add frontal fill so preview models do not fall into near-black silhouettes.
            hud_model_fill_light->set_global_position(cam_pos + (-back_g.normalized()) * 0.65f + up_g.normalized() * 0.1f);
        }

        /* FOV — use fov_y for vertical FOV */
        if (fov[1] > 1.0f && fov[1] < 170.0f) {
            hud_model_camera->set_fov((double)fov[1]);
        } else if (fov[0] > 1.0f && fov[0] < 170.0f) {
            hud_model_camera->set_fov((double)fov[0]);
        }

        /* ── Build skeletal mesh for the entity ── */
        if (tiki) {
        /* Get animation data */
        alignas(8) char frameInfoBuf[256];
        int boneTagBuf[5];
        float boneQuatBuf[20];
        float actionWeight = 0, anim_scale = 1.0f;

        bool has_anim = Godot_Renderer_GetHudModelAnim(
            hud_idx, frameInfoBuf, boneTagBuf, boneQuatBuf,
            &actionWeight, &anim_scale) != 0;

        /* Compute anim hash to skip rebuild when pose unchanged */
        uint64_t anim_hash = 14695981039346656037ULL;
        auto fnv = [&anim_hash](const void *p, size_t n) {
            const unsigned char *b = (const unsigned char *)p;
            for (size_t j = 0; j < n; j++) {
                anim_hash ^= b[j];
                anim_hash *= 1099511628211ULL;
            }
        };
        fnv(frameInfoBuf, sizeof(frameInfoBuf));
        fnv(boneTagBuf, sizeof(boneTagBuf));
        fnv(boneQuatBuf, sizeof(boneQuatBuf));
        fnv(&actionWeight, sizeof(actionWeight));
        fnv(&hModel, sizeof(hModel));

        bool need_rebuild = (hModel != hud_model_last_hmodels[hud_idx]) ||
                    (anim_hash != hud_model_last_anim_hashes[hud_idx]);

        if (need_rebuild && has_anim) {
            int boneCount = 0;
            int *morphCache = nullptr;
            int morphCount = 0;
            void *boneCache = Godot_Skel_PrepareBones(
                tiki, 1023 /* entityNumber from CL_Draw3DModel */,
                (const void *)frameInfoBuf, boneTagBuf,
                (const float *)boneQuatBuf,
                actionWeight, &boneCount,
                &morphCache, &morphCount);

            if (boneCache && boneCount > 0) {
                Ref<ArrayMesh> mesh;
                mesh.instantiate();

                int meshCount = Godot_Skel_GetMeshCount(tiki);
                float tikiScale = Godot_Skel_GetScale(tiki);

                for (int m = 0; m < meshCount; m++) {
                    int surfCount = Godot_Skel_GetSurfaceCount(tiki, m);
                    for (int s = 0; s < surfCount; s++) {
                        int numVerts = 0, numTris = 0;
                        char surfName[128] = {0}, shaderName[128] = {0};
                        Godot_Skel_GetSurfaceInfo(tiki, m, s,
                            &numVerts, &numTris,
                            surfName, sizeof(surfName),
                            shaderName, sizeof(shaderName));
                        if (numVerts <= 0 || numTris <= 0) continue;

                        float *positions = (float *)malloc(numVerts * 3 * sizeof(float));
                        float *normals   = (float *)malloc(numVerts * 3 * sizeof(float));
                        float *texcoords = (float *)malloc(numVerts * 2 * sizeof(float));
                        int   *indices   = (int *)malloc(numTris * 3 * sizeof(int));

                        if (!positions || !normals || !texcoords || !indices) {
                            ::free(positions); ::free(normals);
                            ::free(texcoords); ::free(indices);
                            continue;
                        }

                        if (!Godot_Skel_SkinSurface(tiki, m, s,
                                boneCache, boneCount, positions, normals, -1,
                                morphCache, morphCount)) {
                            ::free(positions); ::free(normals);
                            ::free(texcoords); ::free(indices);
                            continue;
                        }

                        Godot_Skel_GetSurfaceVertices(tiki, m, s,
                            nullptr, nullptr, texcoords);
                        Godot_Skel_GetSurfaceIndices(tiki, m, s, indices);

                        PackedVector3Array gPos, gNrm;
                        PackedVector2Array gUVs;
                        PackedInt32Array   gIdx;
                        gPos.resize(numVerts);
                        gNrm.resize(numVerts);
                        gUVs.resize(numVerts);
                        gIdx.resize(numTris * 3);

                        for (int v = 0; v < numVerts; v++) {
                            Vector3 p = id_to_godot_point(
                                positions[v*3+0], positions[v*3+1], positions[v*3+2])
                                * tikiScale * MOHAA_UNIT_SCALE;
                            Vector3 n = id_to_godot_point(
                                normals[v*3+0], normals[v*3+1], normals[v*3+2]);
                            if (n.length_squared() > 0.001f) n = n.normalized();

                            gPos.set(v, p);
                            gNrm.set(v, n);
                            gUVs.set(v, Vector2(texcoords[v*2+0], texcoords[v*2+1]));
                        }

                        /* Indices as-is — det(id_to_godot_point) = +1, winding preserved */
                        for (int t = 0; t < numTris; t++) {
                            gIdx.set(t*3+0, indices[t*3+0]);
                            gIdx.set(t*3+1, indices[t*3+1]);
                            gIdx.set(t*3+2, indices[t*3+2]);
                        }

                        Array arrays;
                        arrays.resize(Mesh::ARRAY_MAX);
                        arrays[Mesh::ARRAY_VERTEX] = gPos;
                        arrays[Mesh::ARRAY_NORMAL] = gNrm;
                        arrays[Mesh::ARRAY_TEX_UV] = gUVs;
                        arrays[Mesh::ARRAY_INDEX]  = gIdx;

                        if (gPos.size() > 0 && gIdx.size() >= 3) {
                            mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
                        }

                        /* Apply material with texture */
                        if (shaderName[0]) {
                            Ref<StandardMaterial3D> mat;
                            mat.instantiate();
                            mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
                            mat->set_roughness(1.0f);
                            // HUD preview models should be clearly readable and not
                            // depend on dynamic lighting direction.  Use fullbright
                            // shading to match the original menu preview appearance.
                            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                            mat->set_specular(0.0f);
                            mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, false);
                            mat->set_albedo(Color(1.0f, 1.0f, 1.0f, 1.0f));

                            int sh = Godot_Renderer_RegisterShader(shaderName);
                            if (sh > 0) {
                                Ref<ImageTexture> tex = get_shader_texture(sh);
                                if (tex.is_valid()) {
                                    mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                                }
                            }
                            apply_shader_props_to_material(mat, shaderName);
                            mesh->surface_set_material(mesh->get_surface_count() - 1, mat);
                        }

                        ::free(positions); ::free(normals);
                        ::free(texcoords); ::free(indices);
                    }
                }

                ::free(boneCache);
                ::free(morphCache);

                if (mesh.is_valid() && mesh->get_surface_count() > 0) {
                    hud_model_mesh->set_mesh(mesh);
                    hud_model_mesh->set_visible(true);
                    hud_model_last_hmodels[hud_idx] = hModel;
                    hud_model_last_anim_hashes[hud_idx] = anim_hash;
                }
            }
        } else if (!need_rebuild) {
            /* Same model and pose — just ensure visibility */
            hud_model_mesh->set_visible(true);
        }
        } else {
            /* No tiki — hide the mesh */
            if (hud_model_mesh) hud_model_mesh->set_visible(false);
        }

        /* ── Position the mesh in SubViewport space ── */
        if (hud_model_mesh && hud_model_mesh->is_visible()) {
            Vector3 ent_pos = id_to_godot_position(origin[0], origin[1], origin[2]);

            float *e_fwd = &axis[0];
            float *e_lft = &axis[3];
            float *e_up  = &axis[6];
            Vector3 ef = id_to_godot_point(e_fwd[0], e_fwd[1], e_fwd[2]) * ent_scale;
            Vector3 el = id_to_godot_point(e_lft[0], e_lft[1], e_lft[2]) * ent_scale;
            Vector3 eu = id_to_godot_point(e_up[0],  e_up[1],  e_up[2])  * ent_scale;

            Vector3 ent_right = -el;
            Vector3 ent_back  = -ef;
            Basis ent_basis(ent_right, eu, ent_back);

            hud_model_mesh->set_global_transform(Transform3D(ent_basis, ent_pos));
        }

        /* Viewport texture drawing is handled by update_2d_overlay() which
         * injects the viewport texture at the correct position in the 2D
         * command stream (using draw_order from the renderer capture). */
    }
}

void MoHAARunner::update_audio(double delta) {
    if (!audio_root) return;

    // -- 1. Update listener position from engine camera --
    float listener_id_space[3] = {0, 0, 0};  // Listener in id-space for occlusion checks
    {
        float lo[3], la[9];
        int lent;
        Godot_Sound_GetListener(lo, la, &lent);
        listener_id_space[0] = lo[0];
        listener_id_space[1] = lo[1];
        listener_id_space[2] = lo[2];
        Vector3 listener_pos = id_to_godot_position(lo[0], lo[1], lo[2]);
        if (audio_listener) {
            audio_listener->set_global_position(listener_pos);
            Vector3 fwd = id_to_godot_point(la[0], la[1], la[2]);
            Vector3 lft = id_to_godot_point(la[3], la[4], la[5]);
            Vector3 up  = id_to_godot_point(la[6], la[7], la[8]);
            Vector3 right = -lft;
            Vector3 back  = -fwd;
            Basis b(right.normalized(), up.normalized(), back.normalized());
            audio_listener->set_global_transform(Transform3D(b, listener_pos));
        }
    }

    // -- 2. Process one-shot sound events --
    int evt_count = Godot_Sound_GetEventCount();
    for (int i = 0; i < evt_count; i++) {
        float origin[3];
        int entnum, channel, sfxHandle, streamed;
        float volume, minDist, maxDist, pitch;
        char name[256];

        int type = Godot_Sound_GetEvent(i, origin, &entnum, &channel,
                                        &sfxHandle, &volume, &minDist,
                                        &maxDist, &pitch, &streamed,
                                        name, sizeof(name));

        if (type == 3) {
            for (auto *p : sfx_players_3d) { if (p->is_playing()) p->stop(); }
            for (auto *p : sfx_players_2d) { if (p->is_playing()) p->stop(); }
            active_loops.clear();
            continue;
        }
        if (type == 2) continue;
        if (sfxHandle <= 0) continue;

        Ref<AudioStream> wav = load_wav_from_vfs(sfxHandle);
        if (wav.is_null()) continue;

        if (type == 0) {
            // Phase 41: Try to evict a player on the same entity+channel first
            int pi = -1;
            if (entnum > 0 && channel > 0) {
                for (int s = 0; s < MAX_3D_PLAYERS; s++) {
                    if (player_slot_info[s].in_use &&
                        player_slot_info[s].entnum == entnum &&
                        player_slot_info[s].channel == channel) {
                        pi = s;
                        break;
                    }
                }
            }
            if (pi < 0) {
                // Fallback: find an idle player, then round-robin
                for (int s = 0; s < MAX_3D_PLAYERS; s++) {
                    int idx = (next_3d_player + s) % MAX_3D_PLAYERS;
                    if (!sfx_players_3d[idx]->is_playing()) {
                        pi = idx;
                        break;
                    }
                }
                if (pi < 0) {
                    pi = next_3d_player;
                }
                next_3d_player = (pi + 1) % MAX_3D_PLAYERS;
            }
            AudioStreamPlayer3D *p = sfx_players_3d[pi];
            p->set_stream(wav);
            Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
            p->set_global_position(pos);
            float vol_db = (volume > 0.001f) ? (20.0f * log10f(volume)) : -80.0f;
#ifdef HAS_SOUND_OCCLUSION_MODULE
            {
                // Phase 48: Apply sound occlusion attenuation for 3D sounds
                float lo[3], la[9]; int lent;
                Godot_Sound_GetListener(lo, la, &lent);
                float occ = Godot_SoundOcclusion_Check(lo[0], lo[1], lo[2],
                                                        origin[0], origin[1], origin[2]);
                if (occ < 1.0f) {
                    vol_db += 20.0f * log10f(occ > 0.001f ? occ : 0.001f);
                }
            }
#endif
            p->set_volume_db(vol_db);
            p->set_pitch_scale(pitch > 0.01f ? pitch : 1.0f);
            float max_m = (maxDist > 0) ? (maxDist * MOHAA_UNIT_SCALE) : 50.0f;
            p->set_max_distance(max_m);
            float unit_m = (minDist > 0) ? (minDist * MOHAA_UNIT_SCALE) : 1.0f;
            p->set_unit_size(unit_m);
            p->play();
            player_slot_info[pi] = {entnum, channel, true};
        } else if (type == 1) {
            AudioStreamPlayer *p = sfx_players_2d[next_2d_player];
            next_2d_player = (next_2d_player + 1) % MAX_2D_PLAYERS;
            p->set_stream(wav);
            float vol_db = (volume > 0.001f) ? (20.0f * log10f(volume)) : -80.0f;
            p->set_volume_db(vol_db);
            p->set_pitch_scale(pitch > 0.01f ? pitch : 1.0f);
            p->play();
        }
    }
    Godot_Sound_ClearEvents();

    // -- 3. Update looping sounds (Phase 40: position-aware tracking) --
    int loop_count = Godot_Sound_GetLoopCount();
    // Build a composite key = sfxHandle * 65537 + quantised position hash
    // This allows the same sfxHandle to loop at multiple positions
    auto make_loop_key = [](int sfxHandle, float ox, float oy, float oz) -> int64_t {
        // Quantise to 128-unit grid for stable matching across frames
        int qx = (int)(ox / 128.0f);
        int qy = (int)(oy / 128.0f);
        int qz = (int)(oz / 128.0f);
        int64_t posHash = ((int64_t)(qx & 0xFFF)) | ((int64_t)(qy & 0xFFF) << 12) | ((int64_t)(qz & 0xFFF) << 24);
        return ((int64_t)sfxHandle << 36) | posHash;
    };
    std::unordered_map<int64_t, int> new_loops_64;

    for (int i = 0; i < loop_count; i++) {
        float origin[3], velocity[3];
        int sfxHandle, flags;
        float volume, minDist, maxDist, pitch;
        Godot_Sound_GetLoop(i, origin, velocity, &sfxHandle, &volume,
                            &minDist, &maxDist, &pitch, &flags);
        if (sfxHandle <= 0) continue;
        int64_t lkey = make_loop_key(sfxHandle, origin[0], origin[1], origin[2]);
        new_loops_64[lkey] = i;

        auto it = active_loops.find(lkey);
        if (it != active_loops.end()) {
            int pi = it->second;
            if (pi >= 0 && pi < MAX_3D_PLAYERS) {
                AudioStreamPlayer3D *p = sfx_players_3d[pi];
                Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
                p->set_global_position(pos);
                // Apply sound occlusion attenuation for looping sounds
                float occ = Godot_SoundOcclusion_Check(listener_id_space[0], listener_id_space[1],
                                                        listener_id_space[2],
                                                        origin[0], origin[1], origin[2]);
                float adj_vol = volume * occ;
                float vol_db = (adj_vol > 0.001f) ? (20.0f * log10f(adj_vol)) : -80.0f;
                p->set_volume_db(vol_db);
            }
        } else {
            Ref<AudioStream> wav = load_wav_from_vfs(sfxHandle);
            if (wav.is_null()) continue;
            // Phase 43: Handle looping for both WAV and MP3 streams
            Ref<AudioStream> loop_stream;
            Ref<AudioStreamWAV> wav_ref = wav;
            Ref<AudioStreamMP3> mp3_ref = wav;
            if (wav_ref.is_valid()) {
                Ref<AudioStreamWAV> loop_wav = wav_ref->duplicate();
                if (loop_wav.is_valid()) {
                    loop_wav->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
                    loop_wav->set_loop_begin(0);
                    int total_samples = 0;
                    PackedByteArray d = loop_wav->get_data();
                    int bps = (loop_wav->get_format() == AudioStreamWAV::FORMAT_16_BITS) ? 2 : 1;
                    int ch = loop_wav->is_stereo() ? 2 : 1;
                    if (bps > 0 && ch > 0) total_samples = d.size() / (bps * ch);
                    loop_wav->set_loop_end(total_samples);
                    loop_stream = loop_wav;
                }
            } else if (mp3_ref.is_valid()) {
                Ref<AudioStreamMP3> loop_mp3 = mp3_ref->duplicate();
                if (loop_mp3.is_valid()) {
                    loop_mp3->set_loop(true);
                    loop_stream = loop_mp3;
                }
            }
            if (loop_stream.is_null()) loop_stream = wav;
            int pi = next_3d_player;
            next_3d_player = (next_3d_player + 1) % MAX_3D_PLAYERS;
            AudioStreamPlayer3D *p = sfx_players_3d[pi];
            p->set_stream(loop_stream);
            Vector3 pos = id_to_godot_position(origin[0], origin[1], origin[2]);
            p->set_global_position(pos);
            // Apply sound occlusion attenuation for new looping sounds
            float occ = Godot_SoundOcclusion_Check(listener_id_space[0], listener_id_space[1],
                                                    listener_id_space[2],
                                                    origin[0], origin[1], origin[2]);
            float adj_vol = volume * occ;
            float vol_db = (adj_vol > 0.001f) ? (20.0f * log10f(adj_vol)) : -80.0f;
            p->set_volume_db(vol_db);
            p->set_pitch_scale(pitch > 0.01f ? pitch : 1.0f);
            float max_m = (maxDist > 0) ? (maxDist * MOHAA_UNIT_SCALE) : 50.0f;
            p->set_max_distance(max_m);
            float unit_m = (minDist > 0) ? (minDist * MOHAA_UNIT_SCALE) : 1.0f;
            p->set_unit_size(unit_m);
            p->play();
            active_loops[lkey] = pi;
        }
    }

    for (auto it = active_loops.begin(); it != active_loops.end(); ) {
        if (new_loops_64.find(it->first) == new_loops_64.end()) {
            int pi = it->second;
            if (pi >= 0 && pi < MAX_3D_PLAYERS) sfx_players_3d[pi]->stop();
            it = active_loops.erase(it);
        } else {
            ++it;
        }
    }

    // -- 4. Sound fade --
    if (Godot_Sound_GetFadeActive()) {
        float fade_time = Godot_Sound_GetFadeTime();
        if (fade_time > 0.0f) {
            sound_fade_duration = fade_time;
            sound_fade_elapsed = 0.0f;
            sound_fading = true;
        } else {
            // Instant fade
            sound_fade_factor = 0.0f;
            sound_fading = false;
        }
        Godot_Sound_ClearFade();
    }
    if (sound_fading) {
        sound_fade_elapsed += (float)delta;
        if (sound_fade_elapsed >= sound_fade_duration) {
            sound_fade_factor = 0.0f;
            sound_fading = false;
        } else {
            sound_fade_factor = 1.0f - (sound_fade_elapsed / sound_fade_duration);
        }
    } else if (sound_fade_factor < 1.0f) {
        // Recover from fade (engine will send new events to restore)
    }

    // -- 5. Apply global fade + ambient volume to active 3D players --
    {
        float ambient_vol = Godot_Sound_GetAmbientVolume();
        float global_scale = sound_fade_factor * ambient_vol;
        // We apply fade as a master volume modifier; individual player volumes
        // were set at play-time, so we apply fade via AudioServer bus volume
        // if available, or we could adjust per-player. For simplicity, we
        // adjust the Master bus volume.
        if (global_scale < 0.999f || global_scale > 1.001f) {
            // Apply to Master bus: convert linear scale to dB
            float bus_db = (global_scale > 0.001f) ? (20.0f * log10f(global_scale)) : -80.0f;
            AudioServer *as = AudioServer::get_singleton();
            if (as) {
                int master_idx = as->get_bus_index(StringName("Master"));
                if (master_idx >= 0) {
                    as->set_bus_volume_db(master_idx, bus_db);
                }
            }
        } else {
            AudioServer *as = AudioServer::get_singleton();
            if (as) {
                int master_idx = as->get_bus_index(StringName("Master"));
                if (master_idx >= 0) {
                    as->set_bus_volume_db(master_idx, 0.0f);
                }
            }
        }
    }

    // -- 6. Reverb --
    {
        int rev_type = Godot_Sound_GetReverbType();
        float rev_level = Godot_Sound_GetReverbLevel();
        if (rev_type != current_reverb_type) {
            current_reverb_type = rev_type;
            AudioServer *as = AudioServer::get_singleton();
            if (as) {
                // Ensure a "Reverb" bus exists
                if (reverb_bus_idx < 0) {
                    int bus_count = as->get_bus_count();
                    for (int b = 0; b < bus_count; b++) {
                        if (as->get_bus_name(b) == StringName("Reverb")) {
                            reverb_bus_idx = b;
                            break;
                        }
                    }
                    if (reverb_bus_idx < 0) {
                        as->add_bus();
                        reverb_bus_idx = as->get_bus_count() - 1;
                        as->set_bus_name(reverb_bus_idx, StringName("Reverb"));
                        as->set_bus_send(reverb_bus_idx, StringName("Master"));
                        // Add a reverb effect
                        Ref<AudioEffectReverb> reverb;
                        reverb.instantiate();
                        as->add_bus_effect(reverb_bus_idx, reverb);
                    }
                }
                // Map EAX reverb type to Godot reverb parameters
                // EAX types: 0=generic, 1=paddedcell, 2=room, 3=bathroom,
                // 4=livingroom, 5=stoneroom, 6=auditorium, 7=concerthall,
                // 8=cave, 9=arena, 10=hangar, 11=carpetedhallway, 12=hallway,
                // 13=stonecorridor, 14=alley, 15=forest, 16=city, 17=mountains,
                // 18=quarry, 19=plain, 20=parkinglot, 21=sewerpipe, 22=underwater,
                // 23=drugged, 24=dizzy, 25=psychotic
                float room_size = 0.5f;
                float damping = 0.5f;
                float wet = 0.3f;
                float dry = 0.7f;
                switch (rev_type) {
                    case 0:  room_size=0.5f; damping=0.5f; wet=0.3f; break; // generic
                    case 1:  room_size=0.1f; damping=0.9f; wet=0.2f; break; // padded cell
                    case 2:  room_size=0.3f; damping=0.6f; wet=0.3f; break; // room
                    case 3:  room_size=0.4f; damping=0.3f; wet=0.5f; break; // bathroom
                    case 4:  room_size=0.3f; damping=0.7f; wet=0.2f; break; // living room
                    case 5:  room_size=0.6f; damping=0.4f; wet=0.5f; break; // stone room
                    case 6:  room_size=0.8f; damping=0.5f; wet=0.5f; break; // auditorium
                    case 7:  room_size=0.9f; damping=0.4f; wet=0.6f; break; // concert hall
                    case 8:  room_size=0.9f; damping=0.2f; wet=0.7f; break; // cave
                    case 9:  room_size=0.8f; damping=0.3f; wet=0.6f; break; // arena
                    case 10: room_size=1.0f; damping=0.3f; wet=0.6f; break; // hangar
                    case 11: room_size=0.2f; damping=0.8f; wet=0.1f; break; // carpeted hallway
                    case 12: room_size=0.4f; damping=0.5f; wet=0.4f; break; // hallway
                    case 13: room_size=0.5f; damping=0.3f; wet=0.5f; break; // stone corridor
                    case 14: room_size=0.3f; damping=0.4f; wet=0.3f; break; // alley
                    case 15: room_size=0.6f; damping=0.8f; wet=0.2f; break; // forest
                    case 16: room_size=0.4f; damping=0.5f; wet=0.2f; break; // city
                    case 17: room_size=0.7f; damping=0.6f; wet=0.3f; break; // mountains
                    case 18: room_size=0.6f; damping=0.3f; wet=0.4f; break; // quarry
                    case 19: room_size=0.5f; damping=0.7f; wet=0.1f; break; // plain
                    case 20: room_size=0.4f; damping=0.6f; wet=0.2f; break; // parking lot
                    case 21: room_size=0.7f; damping=0.2f; wet=0.7f; break; // sewer pipe
                    case 22: room_size=0.8f; damping=0.1f; wet=0.8f; break; // underwater
                    case 23: room_size=0.6f; damping=0.3f; wet=0.5f; break; // drugged
                    case 24: room_size=0.5f; damping=0.4f; wet=0.4f; break; // dizzy
                    case 25: room_size=0.7f; damping=0.2f; wet=0.6f; break; // psychotic
                    default: room_size=0.5f; damping=0.5f; wet=0.3f; break;
                }
                wet *= rev_level;
                dry = 1.0f - (wet * 0.5f);
                if (as->get_bus_effect_count(reverb_bus_idx) > 0) {
                    Ref<AudioEffectReverb> eff = as->get_bus_effect(reverb_bus_idx, 0);
                    if (eff.is_valid()) {
                        eff->set_room_size(room_size);
                        eff->set_damping(damping);
                        eff->set_wet(wet);
                        eff->set_dry(dry);
                    }
                }
                // Route 3D players through the Reverb bus
                String bus_name = (rev_type > 0 && rev_level > 0.01f) ? "Reverb" : "Master";
                for (auto *p : sfx_players_3d) {
                    p->set_bus(StringName(bus_name));
                }
            }
        }
    }

    // -- 7. Clear sound buffer --
    if (Godot_Sound_GetClearRequested()) {
        for (auto *p : sfx_players_3d) { if (p->is_playing()) p->stop(); }
        for (auto *p : sfx_players_2d) { if (p->is_playing()) p->stop(); }
        active_loops.clear();
        Godot_Sound_ClearClearRequest();
    }

    // -- 8. Music --
    update_music(delta);

    // -- 9. Log sound stats once --
    static bool logged_audio = false;
    if (!logged_audio && evt_count > 0) {
        int sfx_count = Godot_Sound_GetSfxCount();
        UtilityFunctions::print(String("[MoHAA] Audio: ") +
            String::num_int64(sfx_count) + String(" sounds registered, ") +
            String::num_int64(evt_count) + String(" events this frame, ") +
            String::num_int64(loop_count) + String(" loops active."));
        logged_audio = true;
    }
}

// ──────────────────────────────────────────────
//  Music playback
// ──────────────────────────────────────────────

Ref<AudioStream> MoHAARunner::load_music_from_vfs(const char *name) {
    if (!name || !name[0]) return Ref<AudioStream>();

    // Try loading via VFS — music files are typically in sound/music/
    unsigned char *data = nullptr;
    int len = 0;

    // Try as-is first (engine usually provides full path like "sound/music/...")
    len = Godot_VFS_ReadFile(name, (void **)&data);
    if (len <= 0 || !data) {
        // Try with "sound/" prefix
        char prefixed[512];
        snprintf(prefixed, sizeof(prefixed), "sound/%s", name);
        len = Godot_VFS_ReadFile(prefixed, (void **)&data);
    }
    if (len <= 0 || !data) return Ref<AudioStream>();

    // Detect format
    bool is_mp3 = false;
    if (len >= 3) {
        // ID3 tag or MP3 sync word
        if ((data[0] == 'I' && data[1] == 'D' && data[2] == '3') ||
            (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)) {
            is_mp3 = true;
        }
    }
    if (!is_mp3 && len >= 4 && data[0] == 'R' && data[1] == 'I' &&
        data[2] == 'F' && data[3] == 'F') {
        // WAV — check for MP3-in-WAV (format tag 0x0055)
        if (len > 20) {
            // Find fmt chunk and check format tag
            for (int off = 12; off < len - 8; off++) {
                if (data[off] == 'f' && data[off+1] == 'm' &&
                    data[off+2] == 't' && data[off+3] == ' ') {
                    if (off + 12 < len) {
                        uint16_t fmt_tag = data[off+8] | (data[off+9] << 8);
                        if (fmt_tag == 0x0055) is_mp3 = true;
                    }
                    break;
                }
            }
        }
    }

    if (is_mp3) {
        PackedByteArray pba;
        pba.resize(len);
        memcpy(pba.ptrw(), data, len);
        Ref<AudioStreamMP3> mp3;
        mp3.instantiate();
        mp3->set_data(pba);
        Godot_VFS_FreeFile(data);
        return mp3;
    }

    // Try loading as WAV (reuse the WAV loading logic from load_wav_from_vfs)
    // For music, WAV is less common but possible
    Godot_VFS_FreeFile(data);
    return Ref<AudioStream>();
}

void MoHAARunner::update_music(double delta) {
    if (!music_player) return;

    // Process music state changes from the engine
    int action = Godot_Sound_GetMusicAction();
    if (action != 0) { // GR_MUSIC_NONE = 0
        const char *name = Godot_Sound_GetMusicName();
        float volume = Godot_Sound_GetMusicVolume();
        float fade_time = Godot_Sound_GetMusicFadeTime();

        if (action == 1) { // GR_MUSIC_PLAY
            String new_name = name ? String(name) : String();
            if (new_name.length() > 0 && new_name != current_music_name) {
                Ref<AudioStream> stream = load_music_from_vfs(name);
                if (stream.is_valid()) {
                    // Enable looping for music
                    Ref<AudioStreamMP3> mp3 = stream;
                    if (mp3.is_valid()) {
                        mp3->set_loop(true);
                    }
                    music_player->set_stream(stream);
                    music_player->set_volume_db(0.0f);
                    music_player->play();
                    current_music_name = new_name;
                    music_current_volume = (volume > 0.01f) ? volume : 1.0f;
                    music_target_volume = music_current_volume;
                    float vol_db = (music_current_volume > 0.001f)
                        ? (20.0f * log10f(music_current_volume)) : -80.0f;
                    music_player->set_volume_db(vol_db);
                    UtilityFunctions::print(String("[MoHAA] Music: playing ") + new_name);
                }
            }
        } else if (action == 2) { // GR_MUSIC_STOP
            if (music_player->is_playing()) {
                music_player->stop();
                current_music_name = "";
            }
        } else if (action == 3) { // GR_MUSIC_VOLUME
            music_target_volume = volume;
            music_fade_time = fade_time;
            music_fade_elapsed = 0.0f;
        }
        Godot_Sound_ClearMusicAction();
    }

    // Process triggered music
    int trig_action = Godot_Sound_GetTriggeredAction();
    if (trig_action > 0) {
        if (trig_action == 1) { // GR_TRIG_START
            const char *trig_name = Godot_Sound_GetTriggeredName();
            if (trig_name && trig_name[0]) {
                Ref<AudioStream> stream = load_music_from_vfs(trig_name);
                if (stream.is_valid()) {
                    int loop_count = Godot_Sound_GetTriggeredLoopCount();
                    Ref<AudioStreamMP3> mp3 = stream;
                    if (mp3.is_valid()) {
                        mp3->set_loop(loop_count != 0);
                    }
                    music_player->set_stream(stream);
                    music_player->set_volume_db(0.0f);
                    music_player->play();
                    current_music_name = String(trig_name);
                    music_current_volume = 1.0f;
                    music_target_volume = 1.0f;
                }
            }
        } else if (trig_action == 2) { // GR_TRIG_STOP
            if (music_player->is_playing()) {
                music_player->stop();
                current_music_name = "";
            }
        } else if (trig_action == 3) { // GR_TRIG_PAUSE
            // Godot AudioStreamPlayer has no pause — set volume to -80 dB
            music_player->set_volume_db(-80.0f);
        } else if (trig_action == 4) { // GR_TRIG_UNPAUSE
            float vol_db = (music_current_volume > 0.001f)
                ? (20.0f * log10f(music_current_volume)) : -80.0f;
            music_player->set_volume_db(vol_db);
        }
        Godot_Sound_ClearTriggeredAction();
    }

    // Interpolate music volume fade
    if (music_fade_time > 0.0f && music_player->is_playing()) {
        music_fade_elapsed += (float)delta;
        if (music_fade_elapsed >= music_fade_time) {
            music_current_volume = music_target_volume;
            music_fade_time = 0.0f;
        } else {
            float t = music_fade_elapsed / music_fade_time;
            // Lerp from current towards target
            music_current_volume += (music_target_volume - music_current_volume) * t;
        }
        float vol_db = (music_current_volume > 0.001f)
            ? (20.0f * log10f(music_current_volume)) : -80.0f;
        music_player->set_volume_db(vol_db);
    }
}

// ──────────────────────────────────────────────
//  Cinematic bridge (Phase 11)
// ──────────────────────────────────────────────

void MoHAARunner::setup_cinematic() {
    /* Cinematic layer sits above the HUD (layer 11) so it covers everything
     * when a video is playing. */
    cin_layer = memnew(CanvasLayer);
    cin_layer->set_name("CinematicLayer");
    cin_layer->set_layer(11);
    add_child(cin_layer);

    cin_rect = memnew(TextureRect);
    cin_rect->set_name("CinematicRect");
    cin_rect->set_anchors_preset(Control::PRESET_FULL_RECT);
    cin_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    cin_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    cin_layer->add_child(cin_rect);

    // Hidden by default — shown when cinematic is playing
    cin_layer->set_visible(false);

    UtilityFunctions::print("[MoHAA] Cinematic display initialised.");
}

void MoHAARunner::update_cinematic() {
    int active = Godot_Renderer_IsCinematicActive();

    if (active) {
        const unsigned char *data = nullptr;
        int w = 0, h = 0;

        if (Godot_Renderer_GetCinematicFrame(&data, &w, &h) && data && w > 0 && h > 0) {
            /* Build an Image from the raw RGBA data. The engine decoder
             * outputs 32-bit RGBA (samplesPerPixel=4 in cl_cin.cpp). */
            PackedByteArray pba;
            int byte_count = w * h * 4;
            pba.resize(byte_count);
            memcpy(pba.ptrw(), data, byte_count);

            Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, pba);
            if (img.is_valid()) {
                if (cin_texture.is_null()) {
                    cin_texture = ImageTexture::create_from_image(img);
                } else {
                    cin_texture->update(img);
                }
                cin_rect->set_texture(cin_texture);
            }
        }

        if (!cin_was_active) {
            cin_layer->set_visible(true);
            /* Hide the HUD and 3D world while cinematic is playing */
            if (hud_layer) hud_layer->set_visible(false);
            if (game_world) game_world->set_visible(false);
            UtilityFunctions::print("[MoHAA] Cinematic started.");
            cin_was_active = true;
        }
    } else if (cin_was_active) {
        /* Cinematic has ended — hide overlay, restore world + HUD */
        cin_layer->set_visible(false);
        if (game_world) game_world->set_visible(true);
        if (hud_layer && hud_visible) hud_layer->set_visible(true);
        cin_texture.unref();
        cin_was_active = false;
        Godot_Renderer_SetCinematicInactive();
        UtilityFunctions::print("[MoHAA] Cinematic ended.");
    }
}

void MoHAARunner::_ready() {
    if (initialized) {
        return;
    }

    g_godot_ready = true;

    UtilityFunctions::print("[MoHAA] Initialising engine...");

    // ── Pre-init (mirrors what main() does in sys_main.c) ──
    Sys_PlatformInit();
    Sys_Milliseconds();  // establish time base

    // Resolve base path: property > environment > cwd
    godot::String resolved_base = basepath;
    if (resolved_base.is_empty()) {
        // Try MOHAA_BASEPATH environment variable
        const char *env = getenv("MOHAA_BASEPATH");
        if (env && env[0]) {
            resolved_base = godot::String(env);
        } else {
            resolved_base = ".";
        }
    }

    godot::CharString bp = resolved_base.utf8();
    Sys_SetBinaryPath(bp.get_data());
    Sys_SetDefaultInstallPath(bp.get_data());

    CON_Init();

    // Set desktop resolution so GR_BeginRegistration can resolve r_mode -2
    {
        DisplayServer *ds = DisplayServer::get_singleton();
        if (ds) {
            Vector2i screen = ds->screen_get_size();
            if (screen.x > 0 && screen.y > 0) {
                Godot_Renderer_SetDesktopResolution(screen.x, screen.y);
                UtilityFunctions::print(String("[MoHAA] Desktop resolution: ") +
                    String::num_int64(screen.x) + String("x") + String::num_int64(screen.y));
            }
        }
    }

    // Build command line — full client + dedicated/listen server configurable.
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline),
        "%s +set fs_basepath \"%s\"",
        startup_args.utf8().get_data(), bp.get_data());

    // Set up error recovery point before calling into engine
    godot_has_fatal_error = false;
    godot_quit_requested = false;
    godot_jmpbuf_valid = true;

    int jmpval = setjmp(godot_error_jmpbuf);
    if (jmpval != 0) {
        godot_jmpbuf_valid = false;
        if (godot_has_fatal_error) {
            godot::String err_msg(godot_error_message);
            UtilityFunctions::printerr(godot::String("[MoHAA] FATAL ERROR during init: ") + err_msg);
            emit_signal("engine_error", err_msg);
            godot_has_fatal_error = false;
        } else if (godot_quit_requested) {
            UtilityFunctions::print("[MoHAA] Engine quit during init.");
            emit_signal("engine_shutdown_requested");
            godot_quit_requested = false;
        }
        initialized = false;
        return;
    }

    Com_Init(cmdline);
    NET_Init();

    /* Load shader properties early so menu shaders resolve correctly.
     * This is loaded again during Godot_BSP_LoadWorld() for each map,
     * but menu textures need shader definitions before any map loads. */
    Godot_ShaderProps_Load();

    godot_jmpbuf_valid = false;
    initialized = true;
    Godot_SetEngineInitialized(true);

    // Initialize state tracking
    last_server_state = Godot_GetServerState();
    last_map_name = "";

    // Create 3D scene nodes (Phase 7a — camera bridge)
    setup_3d_scene();

    // Create audio player pools (Phase 8 — sound bridge)
    setup_audio();

    // Create cinematic video display (Phase 11 — cinematic bridge)
    setup_cinematic();

    // Initialise game flow state (Phase 261)
    game_flow_state = GameFlowState::BOOT;

    // ── Module init hooks (defensive — only called if module exists) ──
#ifdef HAS_MUSIC_MODULE
    Godot_Music_Init(static_cast<void*>(this));
#endif
#ifdef HAS_VFX_MODULE
    Godot_VFX_Init(game_world);
#endif
#ifdef HAS_SCREEN_EFFECTS_MODULE
    Godot_ScreenFX_Init(this);
#endif
#ifdef HAS_WEAPON_EFFECTS_MODULE
    Godot_WeaponEffects_Init(game_world);
#endif
#ifdef HAS_IMPACT_EFFECTS_MODULE
    Godot_Impact_Init(game_world);
#endif
#ifdef HAS_EXPLOSION_EFFECTS_MODULE
    Godot_Explosion_Init(game_world);
#endif
#ifdef HAS_FRUSTUM_CULL_MODULE
    Godot_FrustumCull_Init();
#endif
#ifdef HAS_DRAW_DISTANCE_MODULE
    Godot_DrawDistance_Init();
#endif

    UtilityFunctions::print("[MoHAA] Engine initialised.");
}

void MoHAARunner::_process(double delta) {
    if (!initialized) {
        return;
    }

    // Set up error recovery point
    godot_has_fatal_error = false;
    godot_quit_requested = false;
    godot_jmpbuf_valid = true;

    int jmpval = setjmp(godot_error_jmpbuf);
    if (jmpval != 0) {
        godot_jmpbuf_valid = false;
        if (godot_has_fatal_error) {
            godot::String err_msg(godot_error_message);
            UtilityFunctions::printerr(godot::String("[MoHAA] FATAL ERROR: ") + err_msg);
            emit_signal("engine_error", err_msg);
            godot_has_fatal_error = false;
        } else if (godot_quit_requested) {
            UtilityFunctions::print("[MoHAA] Engine shutdown requested.");
            emit_signal("engine_shutdown_requested");
            godot_quit_requested = false;
        }
        initialized = false;
        Godot_SetEngineInitialized(false);
        return;
    }

    // ── Pre-frame mouse injection (web only) ──────────────────────────────────
    // MUST happen BEFORE Com_Frame() so that UI_Update() → ServiceEvents() sees
    // the correct cl.mousex/cl.mousey and cl.mouseButtons this frame.
    // We check the CURRENT keyCatcher state (not the cached value) so that the
    // very first frame after a menu opens is handled correctly.
    // SyncGuiMouseToOverlayState() first ensures in_guimouse matches keyCatchers.
    // ── Pre-frame viewport sync ───────────────────────────────────────────────
    // Under Godot there is no physical display-mode switch.  The engine's
    // r_mode cvar is irrelevant — glConfig must always match the actual
    // Godot viewport.  We sync BEFORE Com_Frame so that:
    //  1. GR_Transform2D's Y-flip uses the correct vidHeight
    //  2. SCR_AdjustFrom640 / SetVirtualScale / widget Realign use correct dims
    //  3. If vid_restart triggers inside Com_Frame, GR_BeginRegistration reads
    //     the current viewport from gr_desktopWidth/Height
    //  4. uid.vidWidth/vidHeight stays in sync via CL_FillUIDef wrapper
    {
        // Use get_visible_rect() as the authoritative viewport size — it is always
        // up-to-date even immediately after a window resize or fullscreen switch.
        // hud_control->get_size() can lag by a frame after a resize event.
        Vector2 vp = get_viewport()->get_visible_rect().size;
        if (vp.x < 1.0f || vp.y < 1.0f) {
            Vector2i win = DisplayServer::get_singleton()->window_get_size();
            vp = Vector2(win);
        }
        int vp_w = (int)vp.x;
        int vp_h = (int)vp.y;
        if (vp_w > 0 && vp_h > 0) {
            int cur_w = 0, cur_h = 0;
            Godot_Renderer_GetVidSize(&cur_w, &cur_h);
            if (cur_w != vp_w || cur_h != vp_h) {
                Godot_Renderer_SetDesktopResolution(vp_w, vp_h);
                Godot_Renderer_SyncVidSize(vp_w, vp_h);
                Godot_Client_SyncGlConfigVidSize(vp_w, vp_h);
                Godot_Client_ResolutionChange();
            }
        }
    }

#ifdef __EMSCRIPTEN__
    {
        Godot_Client_SyncGuiMouseToOverlayState();
        bool engine_wants_gui_pre = Godot_Client_GetGuiMouse() != 0;
        bool menu_up_pre = Godot_Client_IsMenuUp() != 0;
        // Only treat as overlay (absolute mouse) when a menu is actually visible.
        bool pre_overlay = engine_wants_gui_pre && menu_up_pre;
        // Fall back to prev-frame value for the very first overlay frame
        if (!pre_overlay) pre_overlay = overlay_prev_frame;
        poll_mouse_input_web(pre_overlay);
    }
#endif

    Com_Frame();
    godot_jmpbuf_valid = false;

    // ── Check if a game switch just completed (switchgame console command) ──
    // Com_SwitchGame_f runs Com_GameRestart() inline during Com_Frame().
    // After it completes, the engine is fully restarted (new FS, renderer,
    // client).  We just need to clear Godot-side caches so assets reload.
    {
        int switched = Godot_GetGameSwitchCompleted();
        if (switched >= 0) {
            Godot_ClearGameSwitchCompleted();
            clear_godot_caches_for_game_switch(switched);
            return;  // Skip rest of this frame; new state takes effect next frame
        }
    }

    bool overlay_active_now = false;

    // ── Cursor management: read engine overlay state to set Godot cursor mode ──
    // The engine manages in_guimouse internally via IN_MouseOn()/IN_MouseOff()
    // when menus open/close (UI_FocusMenuIfExists, UI_MenuEscape, etc.).
    // We simply mirror that state to Godot's cursor mode.
    // This is the ONLY place that sets mouse_captured / Godot mouse mode.
    // Placed AFTER Com_Frame() so state changes during the frame are immediate.
    {
        // Ensure in_guimouse tracks overlay keycatchers (UI/console/message)
        // on platforms where it can become stale.
        Godot_Client_SyncGuiMouseToOverlayState();

        bool overlay_active = Godot_Client_IsAnyOverlayActive() != 0;
        overlay_active_now = overlay_active;
        overlay_prev_frame = overlay_active;  // save for next frame's pre-frame poll
        bool engine_wants_gui = Godot_Client_GetGuiMouse() != 0;
        // Only show the OS cursor when a menu is actually visible on screen.
        // During multiplayer spawn, KEYCATCH_UI / in_guimouse can be set before
        // any menu has rendered — guard against that with UI_MenuUp().
        bool menu_actually_up = Godot_Client_IsMenuUp() != 0;
        bool should_capture = !(engine_wants_gui && menu_actually_up);
        overlay_active_now = engine_wants_gui && menu_actually_up;

        static int last_catchers = -1;
        int cur_catchers = Godot_Client_GetKeyCatchers();
        if (cur_catchers != last_catchers) {
            UtilityFunctions::print(String("[MoHAA] Mouse State Change: catchers=0x") + String::num_int64(cur_catchers, 16) +
                String(" overlay=") + String::num_int64(overlay_active) +
                String(" guimouse=") + String::num_int64(engine_wants_gui) +
                String(" menu_up=") + String::num_int64(menu_actually_up) +
                String(" should_capture=") + String::num_int64(should_capture));
            last_catchers = cur_catchers;
        }

        // Unified mouse-capture logic for ALL platforms (including web).
        // On web, Godot's JS layer defers requestPointerLock() to the next
        // user gesture (click) when MOUSE_MODE_CAPTURED is set.  This is safe
        // to call from _process() — it stores intent, not an immediate lock.
        if (should_capture != mouse_captured) {
            mouse_captured = should_capture;
            Input *input = Input::get_singleton();
            if (input) {
                if (should_capture) {
                    input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
                } else {
                    input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
                }
            }
            Godot_ResetMousePosition();
        }
#ifdef __EMSCRIPTEN__
        // On web, the browser can release pointer lock asynchronously
        // (Escape key, Alt-Tab, focus loss).  When that happens the actual
        // mouse mode reverts to VISIBLE even though mouse_captured is true.
        // Re-assert CAPTURED so Godot's JS layer will re-lock on the next
        // user click.  This is cheap (just stores intent, no DOM call).
        else if (mouse_captured) {
            Input *input = Input::get_singleton();
            if (input && input->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
                input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
            }
        }
#endif
    }

    // Post-frame poll: keeps button transition state in sync for events that
    // arrive between frames (e.g. from Godot's input system on non-web builds).
    // On web this is a no-op duplicate that ensures any lingering state is clean.
    poll_mouse_input_web(overlay_active_now);

    // ── Phase 149: Apply engine cvar settings to Godot systems ──
    // Audio volume: read s_volume / s_musicvolume and apply to Godot AudioServer bus
    {
        AudioServer *as = AudioServer::get_singleton();
        if (as) {
            float master_vol = Cvar_VariableValue("s_volume");
            if (master_vol < 0.0f) master_vol = 0.0f;
            if (master_vol > 1.0f) master_vol = 1.0f;
            float master_db = (master_vol > 0.001f) ? (20.0f * log10f(master_vol)) : -80.0f;
            int bus_idx = as->get_bus_index("Master");
            if (bus_idx >= 0) {
                as->set_bus_volume_db(bus_idx, master_db);
            }
        }
        // Music volume: sync s_musicvolume cvar to Godot music player
        float music_vol = Cvar_VariableValue("s_musicvolume");
        if (music_vol < 0.0f) music_vol = 0.0f;
        if (music_vol > 1.0f) music_vol = 1.0f;
        Godot_Music_SetVolume(music_vol);
    }

    // Gamma: replicate GLimp_SetGamma hardware gamma ramp.
    // The real renderer applies pow(color, 1/gamma) to the entire display via
    // SDL_SetWindowGammaRamp.  We use a full-screen CanvasLayer with a
    // SCREEN_TEXTURE shader to achieve the same effect on both 3D and 2D.
    {
        float gamma = Cvar_VariableValue("r_gamma");
        if (gamma < 0.5f) gamma = 0.5f;
        if (gamma > 3.0f) gamma = 3.0f;

        // Create overlay on first use
        if (!gamma_canvas_layer && gamma != 1.0f) {
            gamma_shader.instantiate();
            gamma_shader->set_code(
                "shader_type canvas_item;\n"
                "uniform float gamma_inv : hint_range(0.1, 2.0) = 1.0;\n"
                "uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;\n"
                "void fragment() {\n"
                "    vec3 col = textureLod(screen_tex, SCREEN_UV, 0.0).rgb;\n"
                "    COLOR = vec4(pow(col, vec3(gamma_inv)), 1.0);\n"
                "}\n"
            );
            gamma_material.instantiate();
            gamma_material->set_shader(gamma_shader);
            gamma_material->set_shader_parameter("gamma_inv", 1.0f / gamma);

            gamma_canvas_layer = memnew(CanvasLayer);
            gamma_canvas_layer->set_layer(200);
            gamma_canvas_layer->set_name("GammaOverlay");
            add_child(gamma_canvas_layer);

            gamma_color_rect = memnew(ColorRect);
            gamma_color_rect->set_name("GammaRect");
            gamma_color_rect->set_anchors_preset(Control::PRESET_FULL_RECT);
            gamma_color_rect->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
            gamma_color_rect->set_material(gamma_material);
            gamma_canvas_layer->add_child(gamma_color_rect);

            gamma_current = gamma;
        }

        // Update gamma uniform when value changes
        if (gamma_canvas_layer) {
            if (gamma == 1.0f) {
                gamma_canvas_layer->set_visible(false);
            } else {
                gamma_canvas_layer->set_visible(true);
                if (gamma != gamma_current) {
                    gamma_material->set_shader_parameter("gamma_inv", 1.0f / gamma);
                    gamma_current = gamma;
                }
            }
        }

        // BSP materials are now unshaded (lightmap is sole illumination),
        // so no Environment brightness compensation is needed.  Disable
        // adjustment to avoid dimming the correctly-lit scene.
        if (world_env) {
            Ref<Environment> env = world_env->get_environment();
            if (env.is_valid()) {
                env->set_adjustment_enabled(false);
            }
        }
    }

    // r_shadows: 0 = classic MOHAA shadow blobs (default)
    //            1 = modern GPU shadows: sun DirectionalLight (from map sundirection)
    //                casts shadows for all RF_SHADOW entities onto BSP surfaces.
    {
        int new_shadow_mode = Cvar_VariableIntegerValue("r_shadows");
        if (new_shadow_mode < 0 || new_shadow_mode > 1) new_shadow_mode = 0;
        if (new_shadow_mode != cached_entity_shadow_mode) {
            apply_player_shadow_mode(new_shadow_mode);
        }
    }

    // r_dlight_shadows: 0 = dlights illuminate only, no shadow casting (default, fast)
    //                   1 = dlights cast shadows (expensive: 6 depth passes per light)
    //                   Takes effect immediately; update_dlights() reads cached_dlight_shadows.
    {
        int new_dlight_shadows = Cvar_VariableIntegerValue("r_dlight_shadows");
        if (new_dlight_shadows < 0 || new_dlight_shadows > 1) new_dlight_shadows = 0;
        cached_dlight_shadows = new_dlight_shadows;
    }

    // r_fastsky: toggle skybox visibility based on cvar
    if (world_env) {
        Ref<Environment> env = world_env->get_environment();
        if (env.is_valid()) {
            int fastsky = Cvar_VariableIntegerValue("r_fastsky");
            Environment::BGMode bg = env->get_background();
            if (fastsky && bg == Environment::BG_SKY) {
                env->set_background(Environment::BG_COLOR);
            } else if (!fastsky && bg == Environment::BG_COLOR && env->get_sky().is_valid()) {
                env->set_background(Environment::BG_SKY);
            }
        }
    }

    // Event-driven r_fullscreen updates (no per-frame cvar polling).
    {
        int new_fullscreen = 0;
        if (Godot_ConsumeFullscreenCvarChanged(&new_fullscreen)) {
            cached_r_fullscreen = new_fullscreen ? 1 : 0;

            DisplayServer *ds = DisplayServer::get_singleton();
            if (ds) {
                DisplayServer::WindowMode target_mode =
                    cached_r_fullscreen ? DisplayServer::WINDOW_MODE_FULLSCREEN
                                        : DisplayServer::WINDOW_MODE_WINDOWED;

                if (ds->window_get_mode() != target_mode) {
                    ds->window_set_mode(target_mode);
                }

                UtilityFunctions::print(String("[MoHAA] r_fullscreen -> ") +
                    String::num_int64(cached_r_fullscreen));
            }
        }
    }

    // Vid_restart: apply fullscreen/resolution changes from menu
    {
        int fs = 0, vw = 0, vh = 0;
        if (Godot_Renderer_ConsumeVidRestart(&fs, &vw, &vh)) {
            /* Renderer shader/model tables are rebuilt on vid_restart.
               Drop handle-keyed caches so 2D/UI textures are re-resolved. */
            shader_textures.clear();
            s_shader_texture_loaded_names.clear();
            animmap_info.clear();
            animmap_frames.clear();
            s_surf_anim_cache.clear();
            s_sprite_mat_cache.clear();
            s_beam_mat_cache.clear();
            s_poly_mat_cache.clear();
            s_terrain_mark_mat_cache.clear();
            s_sprite_tint_cache.clear();
            s_beam_tint_cache.clear();
            s_alpha_inv_tex_cache.clear();

            DisplayServer *ds = DisplayServer::get_singleton();
            if (ds) {
                if (vw <= 0) vw = 640;
                if (vh <= 0) vh = 480;

                /* Match OpenMoHAA vid_restart semantics: honour both
                 * fullscreen and resolved mode dimensions. */
                if (fs) {
                    ds->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
                } else {
                    ds->window_set_mode(DisplayServer::WINDOW_MODE_WINDOWED);
                    ds->window_set_size(Vector2i(vw, vh));
                }

                /* Keep renderer/client video state aligned immediately after
                 * mode changes so UI and 2D scaling use the new dimensions
                 * in the same frame.
                 *
                 * IMPORTANT: use the TARGET dimensions (vw/vh or screen size)
                 * directly — NOT get_visible_rect().  The Godot viewport does
                 * not update until the next frame after window_set_size/mode,
                 * so querying it here returns the OLD size and would re-sync
                 * glConfig to the previous resolution, breaking HUD layout
                 * and mouse coordinate mapping for one or more frames. */
                int sync_w, sync_h;
                if (fs) {
                    Vector2i screen = ds->screen_get_size();
                    sync_w = screen.x;
                    sync_h = screen.y;
                } else {
                    sync_w = vw;
                    sync_h = vh;
                }
                if (sync_w > 0 && sync_h > 0) {
                    Godot_Renderer_SetDesktopResolution(sync_w, sync_h);
                    Godot_Renderer_SyncVidSize(sync_w, sync_h);
                    Godot_Client_SyncGlConfigVidSize(sync_w, sync_h);
                    Godot_Client_ResolutionChange();
                }

                UtilityFunctions::print(String("[MoHAA] vid_restart: fullscreen=") +
                    String::num_int64(fs) + String(" resolved=") +
                    String::num_int64(vw) + String("x") + String::num_int64(vh));
            }
        }
    }

    // ── Phase 85: Begin per-frame render statistics ──
#ifdef HAS_MESH_CACHE_MODULE
    Godot_RenderStats_BeginFrame();
#endif

    // ── Phase 59: Poll UI state machine before rendering/input ──
    Godot_UI_Update();

    // NOTE: Cursor management moved to after Com_Frame() so that state
    // changes during the frame (menu open/close, map load, etc.) are
    // reflected immediately without a 1-frame lag.

    // ── Phase 146: Custom cursor from engine (gfx/2d/mouse_cursor.tga) ──
    // The engine's UI system calls IN_SetCursorFromImage() with raw RGBA
    // pixel data.  We pick it up here and set Godot's custom cursor.
    {
        const unsigned char *cursor_pixels = nullptr;
        int cw = 0, ch = 0;
        if (Godot_GetPendingCursorImage(&cursor_pixels, &cw, &ch)) {
            PackedByteArray pba;
            pba.resize(cw * ch * 4);
            memcpy(pba.ptrw(), cursor_pixels, cw * ch * 4);
            Ref<Image> cursor_img = Image::create_from_data(cw, ch, false, Image::FORMAT_RGBA8, pba);
            if (cursor_img.is_valid() && !cursor_img->is_empty()) {
                Ref<ImageTexture> cursor_tex = ImageTexture::create_from_image(cursor_img);
                if (cursor_tex.is_valid()) {
                    Input *input = Input::get_singleton();
                    if (input) {
                        input->set_custom_mouse_cursor(cursor_tex, Input::CURSOR_ARROW, Vector2(0, 0));
                        UtilityFunctions::print(String("[MoHAA] Custom cursor set: ") +
                            String::num_int64(cw) + String("x") + String::num_int64(ch));
                    }
                }
            }
            Godot_ClearPendingCursorImage();
        }
    }

    // ── Update 3D camera from engine viewpoint (Phase 7a) ──
    update_camera();

    // ── Phase 133: Frustum culling — extract planes after camera update ──
#ifdef HAS_FRUSTUM_CULL_MODULE
    Godot_FrustumCull_UpdateCamera(camera);
#endif

    // ── Phase 133: Draw distance — apply near/far planes and fog from cvars ──
#ifdef HAS_DRAW_DISTANCE_MODULE
    if (camera && world_env) {
        Ref<Environment> dd_env = world_env->get_environment();
        if (dd_env.is_valid()) {
            Godot_DrawDistance_Update(camera, dd_env.ptr(), static_cast<float>(delta));
        }
    }
#endif

    // ── Sync weapon viewport camera (Phase 62) ──
#ifdef HAS_WEAPON_VIEWPORT_MODULE
    Godot_WeaponViewport::get().sync_camera();
#endif

    // ── Load BSP world geometry if a new map was loaded (Phase 7b) ──
    check_world_load();

    // ── PVS cluster visibility culling ──
    update_pvs_visibility();

    // ── Per-frame terrain visibility via engine BSP tree walk ──
    update_terrain_visibility();

    // ── Update entity debug meshes from captured render data (Phase 7e) ──
    update_entities();
    update_dlights();
    update_polys();
    update_swipe_effects();     // Phase 24: swipe/melee trails
    update_terrain_marks();     // Phase 25: terrain mark decals
    update_shadow_blobs();      // Shadow blob projection under RF_SHADOW entities
    update_shader_animations(delta);

    // ── Animate sky clouds (tcMod scroll) ──
    if (sky_cloud_material.is_valid() && (sky_cloud_scroll_s != 0.0f || sky_cloud_scroll_t != 0.0f)) {
        sky_cloud_time += delta;
        sky_cloud_material->set_shader_parameter("cloud_time", (float)sky_cloud_time);
    }

    update_sun_flare();         // Sun lens flare 2D overlay

    // Update planar reflections early before drawing HUD
    // update_mirrors(); // Vanity mirror hack (Phase FUN) - Commented out as requested.

    // ── Update 2D HUD overlay from captured draw commands (Phase 7h) ──
    update_2d_overlay();

    // ── Update scoreboard overlay (TAB key) ──
    update_scoreboard();

    // ── Update HUD model previews (Phase 148) ──
    update_hud_models();

    // ── Update audio from captured sound events (Phase 8) ──
    update_audio(delta);

    // ── Update cinematic video display (Phase 11) ──
    update_cinematic();

    // ── Screen effect triggers (Phase 6 — VFX integration) ──
#ifdef HAS_SCREEN_EFFECTS_MODULE
    {
        // Damage flash: detect health decrease between frames
        int cur_health = Godot_Client_GetPlayerHealth();
        if (prev_health > 0 && cur_health > 0 && cur_health < prev_health) {
            float damage = static_cast<float>(prev_health - cur_health);
            // Scale intensity: 10 HP → 0.2, 50 HP → 1.0
            float intensity = Math::clamp(damage / 50.0f, 0.1f, 1.0f);
            Godot_ScreenFX_DamageFlash(intensity);
        }
        if (cur_health > 0) {
            prev_health = cur_health;
        }

        // Screen blend from playerState_t — the engine computes this for
        // underwater tint, pain flash, and other full-screen colour effects.
        float br, bg, bb, ba;
        Godot_Client_GetScreenBlend(&br, &bg, &bb, &ba);
        // Underwater detection: blend is blue-green when submerged
        // (blue > 0.1 and alpha > 0.05 with low red indicates underwater)
        bool is_underwater = (ba > 0.05f && bb > 0.1f && br < 0.2f);
        Godot_ScreenFX_UnderwaterTint(is_underwater);
    }
#endif

    // ── Module update hooks (defensive — only called if module exists) ──
#ifdef HAS_MUSIC_MODULE
    Godot_Music_Update(delta);
#endif
#ifdef HAS_WEATHER_MODULE
    Godot_Weather_Update(camera ? camera->get_global_position() : Vector3(), static_cast<float>(delta));
#endif
#ifdef HAS_VFX_MODULE
    Godot_VFX_Update(delta);
#endif
#ifdef HAS_SCREEN_EFFECTS_MODULE
    Godot_ScreenFX_Update(static_cast<float>(delta), camera);
#endif
#ifdef HAS_WEAPON_EFFECTS_MODULE
    Godot_MuzzleFlash_Update(static_cast<float>(delta));
    Godot_ShellCasing_Update(static_cast<float>(delta));
#endif
#ifdef HAS_IMPACT_EFFECTS_MODULE
    Godot_Impact_Update(static_cast<float>(delta));
#endif
#ifdef HAS_EXPLOSION_EFFECTS_MODULE
    Godot_Explosion_Update(static_cast<float>(delta));
    Godot_CameraShake_Update(static_cast<float>(delta), camera);
#endif
#ifdef HAS_DEBUG_RENDER_MODULE
    Godot_DebugRender_Update(delta);
#endif

    // ── Phase 60/61: Evict stale mesh and material cache entries ──
#ifdef HAS_MESH_CACHE_MODULE
    {
        static uint64_t frame_counter = 0;
        frame_counter++;
        Godot_MeshCache::get().evict_stale(frame_counter);
        Godot_MaterialCache::get().evict_stale(frame_counter);
    }
#endif

    // ── Phase 85: End per-frame render statistics ──
#ifdef HAS_MESH_CACHE_MODULE
    Godot_RenderStats_EndFrame();
#endif

    // ── Update game flow state machine (Phase 261) ──
    update_game_flow_state();

    // ── Phase 59: Input mode enforcement now handled by UI state machine ──
    // Godot_UI_Update() + Godot_UI_ShouldShowCursor() above manage the
    // cursor mode automatically.  The engine's keyCatchers-based UI
    // routing (cl_keys.c) handles console/menu/game input dispatch.
    // We no longer forcibly clear catchers or unpause here — that is
    // the engine's responsibility via its own UI code paths.
    update_input_routing();

    // ── State change detection for signals (Task 2.5.4) ──
    if (initialized) {
        int cur_state = Godot_GetServerState();
        const char *cur_map_raw = Godot_GetMapName();
        godot::String cur_map(cur_map_raw ? cur_map_raw : "");

        // Detect map loaded: state transitioned to SS_GAME (3) with a valid map name
        if (cur_state == 3 && last_server_state != 3 && !cur_map.is_empty()) {
            UtilityFunctions::print(godot::String("[MoHAA] Map loaded: ") + cur_map);

            // Dump client diagnostics once after map load
            int cl_state = Godot_Client_GetState();
            int catchers = Godot_Client_GetKeyCatchers();
            int gui_mouse = Godot_Client_GetGuiMouse();
            int start_stage = Godot_Client_GetStartStage();
            int mx, my;
            Godot_Client_GetMousePos(&mx, &my);
            // Log diagnostics; input routing is handled by update_input_routing()
            UtilityFunctions::print(String("[MoHAA] Client: state=") + String::num_int64(cl_state) +
                String(" keyCatchers=0x") + String::num_int64(catchers, 16) +
                String(" guiMouse=") + String::num_int64(gui_mouse) +
                String(" startStage=") + String::num_int64(start_stage) +
                String(" mousePos=(") + String::num_int64(mx) + String(",") + String::num_int64(my) + String(")"));

            emit_signal("map_loaded", cur_map);

            // The loading screen menu may still be active (waiting for
            // a "continue" button click).  Dismiss it automatically so
            // the 3D view is visible.  "finishloadingscreen" is the
            // engine's own command (UI_FinishLoadingScreen_f) — it calls
            // UI_ForceMenuOff(true), clears loading state, and unpauses.
            if (Godot_Client_IsMenuUp()) {
                UtilityFunctions::print("[MoHAA] Dismissing loading screen after map load.");
                Cbuf_AddText("finishloadingscreen\n");
            }
        }

        // Detect map unloaded: was in SS_GAME, now not
        else if (last_server_state == 3 && cur_state != 3) {
            UtilityFunctions::print("[MoHAA] Map unloaded.");
            emit_signal("map_unloaded");
        }

        last_server_state = cur_state;
        last_map_name = cur_map;
    }
}

// ──────────────────────────────────────────────
//  Commands
// ──────────────────────────────────────────────

void MoHAARunner::execute_command(const godot::String &p_command) {
    if (!initialized) {
        UtilityFunctions::printerr("[MoHAA] Engine not initialized, cannot execute command.");
        return;
    }
    godot::CharString cmd = p_command.utf8();
    Cbuf_AddText(cmd.get_data());
    Cbuf_AddText("\n");
}

void MoHAARunner::load_map(const godot::String &p_map_name) {
    if (!initialized) {
        UtilityFunctions::printerr("[MoHAA] Engine not initialized, cannot load map.");
        return;
    }
    UtilityFunctions::print(godot::String("[MoHAA] Loading map: ") + p_map_name);
    godot::String cmd = godot::String("map ") + p_map_name;
    execute_command(cmd);
}

// ──────────────────────────────────────────────
//  Server status (Task 2.5.3)
// ──────────────────────────────────────────────

bool MoHAARunner::is_map_loaded() const {
    if (!initialized) return false;
    return Godot_GetServerState() == 3;  // SS_GAME
}

godot::String MoHAARunner::get_current_map() const {
    if (!initialized) return "";
    const char *name = Godot_GetMapName();
    return godot::String(name ? name : "");
}

int MoHAARunner::get_player_count() const {
    if (!initialized) return 0;
    return Godot_GetPlayerCount();
}

int MoHAARunner::get_server_state() const {
    if (!initialized) return 0;
    return Godot_GetServerState();
}

godot::String MoHAARunner::get_server_state_string() const {
    switch (get_server_state()) {
        case 0: return "dead";
        case 1: return "loading";
        case 2: return "loading2";
        case 3: return "game";
        default: return "unknown";
    }
}

godot::String MoHAARunner::get_cvar_string(const godot::String &p_name) const {
    if (!initialized) return "";
    godot::CharString name = p_name.utf8();
    char buffer[1024];
    buffer[0] = '\0';
    Cvar_VariableStringBuffer(name.get_data(), buffer, (int)sizeof(buffer));
    return godot::String(buffer);
}

// ──────────────────────────────────────────────
//  VFS access (Task 4.1)
// ──────────────────────────────────────────────

godot::PackedByteArray MoHAARunner::vfs_read_file(const godot::String &p_qpath) const {
    godot::PackedByteArray result;
    if (!initialized) {
        UtilityFunctions::printerr("[MoHAA] Engine not initialised, cannot read VFS file.");
        return result;
    }

    godot::CharString path = p_qpath.utf8();

    // Optimisation: read directly into PackedByteArray to avoid double allocation + memcpy.
    // 1. Open the file and get its size (without allocating a buffer in the engine)
    int handle = 0;
    long len = Godot_VFS_FileOpenRead(path.get_data(), &handle);

    if (len < 0 || handle == 0) {
        return result;  // File not found — return empty array
    }

    // 2. Allocate the result buffer once
    result.resize(len);

    // 3. Read directly into the buffer
    if (len > 0) {
        long read_len = Godot_VFS_FileRead(handle, result.ptrw(), len);
        if (read_len != len) {
            UtilityFunctions::printerr("[MoHAA] vfs_read_file: Short read or error reading ", p_qpath);
            // We could resize result to 0 here, but partial data might be useful or at least expected size.
            // For now, keep it as is (filled with zeroes past read_len if any).
        }
    }

    // 4. Close the file handle
    Godot_VFS_FileClose(handle);

    return result;
}

bool MoHAARunner::vfs_file_exists(const godot::String &p_qpath) const {
    if (!initialized) return false;
    godot::CharString path = p_qpath.utf8();
    return Godot_VFS_FileExists(path.get_data()) != 0;
}

godot::PackedStringArray MoHAARunner::vfs_list_files(const godot::String &p_directory, const godot::String &p_extension) const {
    godot::PackedStringArray result;
    if (!initialized) {
        UtilityFunctions::printerr("[MoHAA] Engine not initialised, cannot list VFS files.");
        return result;
    }

    godot::CharString dir = p_directory.utf8();
    godot::CharString ext = p_extension.utf8();
    int count = 0;
    char **list = Godot_VFS_ListFiles(dir.get_data(), ext.get_data(), &count);

    if (!list) return result;

    for (int i = 0; i < count; i++) {
        if (list[i]) {
            result.push_back(godot::String(list[i]));
        }
    }

    Godot_VFS_FreeFileList(list);
    return result;
}

godot::String MoHAARunner::vfs_get_gamedir() const {
    if (!initialized) return "";
    const char *dir = Godot_VFS_GetGamedir();
    return godot::String(dir ? dir : "");
}

// ──────────────────────────────────────────────
//  Input bridge (Phase 6)
// ──────────────────────────────────────────────

void MoHAARunner::set_mouse_captured(bool p_captured) {
    mouse_captured = p_captured;
    Input *input = Input::get_singleton();
    if (!input) return;

    if (p_captured) {
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    } else {
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    }
}

bool MoHAARunner::is_mouse_captured() const {
    return mouse_captured;
}

void MoHAARunner::set_hud_visible(bool p_visible) {
    hud_visible = p_visible;
    if (hud_layer) {
        hud_layer->set_visible(hud_visible);
    }
}

bool MoHAARunner::is_hud_visible() const {
    return hud_visible;
}

// ──────────────────────────────────────────────
//  Input routing — automatic cursor management
// ──────────────────────────────────────────────

/*
 * update_input_routing — Now a no-op.  Cursor mode is driven entirely
 *   by the engine's in_guimouse flag, which is synced at the top of
 *   _process().  The engine manages in_guimouse via IN_MouseOn/Off
 *   when menus open/close (UI_FocusMenuIfExists, UI_MenuEscape, etc.).
 *   We do NOT forcibly clear keyCatchers or call SetGameInputMode —
 *   the engine owns its own UI state.
 */
void MoHAARunner::update_input_routing() {
    // Intentionally empty — cursor sync is handled at top of _process()
    // by reading Godot_Client_GetGuiMouse().
}

void MoHAARunner::poll_mouse_input_web(bool overlay_active) {
#ifdef __EMSCRIPTEN__
    Viewport *vp = get_viewport();
    if (!vp) {
        return;
    }

    Vector2 pos = vp->get_mouse_position();

    if (!mouse_poll_initialised) {
        mouse_poll_prev_pos = pos;
        for (int b = 0; b < 10; b++) {
            mouse_poll_prev_buttons[b] = false;
        }
        mouse_poll_initialised = true;
    }

    // ALWAYS update position when in_guimouse is true AND a menu is actually up
    // (reliable check that bypasses any stale overlay_active parameter).
    int gui_mouse_active = Godot_Client_GetGuiMouse();
    int menu_up = Godot_Client_IsMenuUp();
    bool should_poll = overlay_active || (gui_mouse_active != 0 && menu_up != 0);

    if (should_poll) {
        update_ui_transform();
        float sx = (ui_scale_x > 0.0001f) ? ui_scale_x : 1.0f;
        float sy = (ui_scale_y > 0.0001f) ? ui_scale_y : 1.0f;
        int ex = (int)((pos.x - ui_offset_x) / sx);
        int ey = (int)((pos.y - ui_offset_y) / sy);
        if (ex < 0) ex = 0;
        if (ey < 0) ey = 0;
        if (ex >= ui_vid_w) ex = ui_vid_w - 1;
        if (ey >= ui_vid_h) ey = ui_vid_h - 1;
        Godot_Client_SetMousePos(ex, ey);
    } else {
        for (int b = 0; b < 10; b++) {
            mouse_poll_prev_buttons[b] = false;
        }
    }

    mouse_poll_prev_pos = pos;
#else
    (void)overlay_active;
#endif
}

// ──────────────────────────────────────────────
//  Game flow state machine (Phase 261)
// ──────────────────────────────────────────────

void MoHAARunner::update_game_flow_state() {
    if (!initialized) return;

    int sv_state = Godot_GetServerState();
    int catchers = Godot_Client_GetKeyCatchers();
    GameFlowState new_state = game_flow_state;

    switch (game_flow_state) {
        case GameFlowState::BOOT:
            // After engine init, transition to TITLE_SCREEN
            // The engine opens the main menu UI automatically on boot
            if (catchers & 0x2) {  // KEYCATCH_UI active
                new_state = GameFlowState::MAIN_MENU;
            } else {
                new_state = GameFlowState::TITLE_SCREEN;
            }
            break;

        case GameFlowState::TITLE_SCREEN:
            // Any key press or UI activation moves to main menu
            if (catchers & 0x2) {  // KEYCATCH_UI
                new_state = GameFlowState::MAIN_MENU;
            } else if (sv_state == 1 || sv_state == 2) {  // SS_LOADING / SS_LOADING2
                new_state = GameFlowState::LOADING;
            }
            break;

        case GameFlowState::MAIN_MENU:
            if (sv_state == 1 || sv_state == 2) {  // SS_LOADING / SS_LOADING2
                new_state = GameFlowState::LOADING;
            } else if (sv_state == 3 && !(catchers & 0x2)) {  // SS_GAME, no UI
                new_state = GameFlowState::IN_GAME;
            }
            break;

        case GameFlowState::LOADING:
            if (sv_state == 3) {  // SS_GAME
                new_state = GameFlowState::IN_GAME;
            } else if (sv_state == 0) {  // SS_DEAD — load failed or disconnected
                new_state = GameFlowState::DISCONNECTED;
            }
            break;

        case GameFlowState::IN_GAME:
            if (sv_state != 3) {  // No longer in game
                if (sv_state == 1 || sv_state == 2) {
                    new_state = GameFlowState::LOADING;  // Map change
                } else {
                    new_state = GameFlowState::DISCONNECTED;
                }
            } else if (Godot_Client_GetPaused()) {
                new_state = GameFlowState::PAUSED;
            }
            break;

        case GameFlowState::PAUSED:
            if (!Godot_Client_GetPaused()) {
                new_state = GameFlowState::IN_GAME;
            }
            if (sv_state != 3) {
                new_state = GameFlowState::DISCONNECTED;
            }
            break;

        case GameFlowState::MISSION_COMPLETE:
            // Stay until a new map loads or menu opens
            if (sv_state == 1 || sv_state == 2) {
                new_state = GameFlowState::LOADING;
            } else if (catchers & 0x2) {
                new_state = GameFlowState::MAIN_MENU;
            }
            break;

        case GameFlowState::DISCONNECTED:
            if (catchers & 0x2) {  // KEYCATCH_UI
                new_state = GameFlowState::MAIN_MENU;
            } else if (sv_state == 1 || sv_state == 2) {
                new_state = GameFlowState::LOADING;
            } else if (sv_state == 3) {
                new_state = GameFlowState::IN_GAME;
            }
            break;
    }

    if (new_state != game_flow_state) {
        game_flow_state = new_state;
        emit_signal("game_flow_state_changed", (int)new_state);
    }
}

int MoHAARunner::get_game_flow_state() const {
    return (int)game_flow_state;
}

godot::String MoHAARunner::get_game_flow_state_string() const {
    switch (game_flow_state) {
        case GameFlowState::BOOT:             return "boot";
        case GameFlowState::TITLE_SCREEN:     return "title_screen";
        case GameFlowState::MAIN_MENU:        return "main_menu";
        case GameFlowState::LOADING:          return "loading";
        case GameFlowState::IN_GAME:          return "in_game";
        case GameFlowState::PAUSED:           return "paused";
        case GameFlowState::MISSION_COMPLETE: return "mission_complete";
        case GameFlowState::DISCONNECTED:     return "disconnected";
        default:                              return "unknown";
    }
}

// ──────────────────────────────────────────────
//  New game flow (Phase 262)
// ──────────────────────────────────────────────

void MoHAARunner::start_new_game(int difficulty) {
    if (!initialized) {
        UtilityFunctions::printerr("[MoHAA] Engine not initialised, cannot start new game.");
        return;
    }
    // Set difficulty cvar: 0 = easy, 1 = medium, 2 = hard
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "set skill %d\n", difficulty);
    Cbuf_AddText(cmd);
    // Load the first Allied Assault mission
    Cbuf_AddText("map m1l1\n");
}

void MoHAARunner::set_difficulty(int difficulty) {
    if (!initialized) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "set skill %d\n", difficulty);
    Cbuf_AddText(cmd);
}

// ──────────────────────────────────────────────
//  Save / load game (Phase 264)
// ──────────────────────────────────────────────

void MoHAARunner::quick_save() {
    if (!initialized) return;
    Cbuf_AddText("savegame quick\n");
}

void MoHAARunner::quick_load() {
    if (!initialized) return;
    Cbuf_AddText("loadgame quick\n");
}

void MoHAARunner::save_game(const godot::String &p_slot_name) {
    if (!initialized) return;
    godot::CharString slot = p_slot_name.utf8();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "savegame %s\n", slot.get_data());
    Cbuf_AddText(cmd);
}

void MoHAARunner::load_game(const godot::String &p_slot_name) {
    if (!initialized) return;
    godot::CharString slot = p_slot_name.utf8();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "loadgame %s\n", slot.get_data());
    Cbuf_AddText(cmd);
}

godot::PackedStringArray MoHAARunner::get_save_list() const {
    godot::PackedStringArray result;
    if (!initialized) return result;

    // Save files are in fs_homepath/save/ — list .sav files via VFS
    int count = 0;
    char **list = Godot_VFS_ListFiles("save", ".sav", &count);
    if (!list) return result;

    for (int i = 0; i < count; i++) {
        if (list[i]) {
            result.push_back(godot::String(list[i]));
        }
    }
    Godot_VFS_FreeFileList(list);
    return result;
}

// ──────────────────────────────────────────────
//  Multiplayer helpers (Phases 265-266)
// ──────────────────────────────────────────────

godot::PackedStringArray MoHAARunner::list_available_maps() const {
    godot::PackedStringArray result;
    if (!initialized) return result;

    // BSP maps are in maps/ directory
    int count = 0;
    char **list = Godot_VFS_ListFiles("maps", ".bsp", &count);
    if (!list) return result;

    for (int i = 0; i < count; i++) {
        if (list[i]) {
            // Strip .bsp extension for cleaner display
            godot::String name(list[i]);
            if (name.ends_with(".bsp")) {
                name = name.substr(0, name.length() - 4);
            }
            result.push_back(name);
        }
    }
    Godot_VFS_FreeFileList(list);
    return result;
}

void MoHAARunner::start_server(const godot::String &p_map, const godot::String &p_gametype, int max_clients) {
    if (!initialized) return;
    godot::CharString map = p_map.utf8();
    godot::CharString gt = p_gametype.utf8();
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "set g_gametype %s\nset sv_maxclients %d\nmap %s\n",
             gt.get_data(), max_clients, map.get_data());
    Cbuf_AddText(cmd);
}

void MoHAARunner::connect_to_server(const godot::String &p_address) {
    if (!initialized) return;
#ifdef HAS_MULTIPLAYER_MODULE
    Godot_MP_ConnectToServer(p_address.utf8().get_data());
#else
    godot::CharString addr = p_address.utf8();
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "connect %s\n", addr.get_data());
    Cbuf_AddText(cmd);
#endif
}

void MoHAARunner::disconnect_from_server() {
    if (!initialized) return;
#ifdef HAS_MULTIPLAYER_MODULE
    Godot_MP_Disconnect();
#else
    Cbuf_AddText("disconnect\n");
#endif
}

// ──────────────────────────────────────────────
//  Multiplayer server browser + hosting (Phase 263)
// ──────────────────────────────────────────────

void MoHAARunner::host_server(const godot::String &p_map, int maxplayers, int gametype) {
    if (!initialized) return;
#ifdef HAS_MULTIPLAYER_MODULE
    Godot_MP_HostServer(p_map.utf8().get_data(), maxplayers, gametype);
#endif
}

void MoHAARunner::refresh_server_list() {
    if (!initialized) return;
#ifdef HAS_MULTIPLAYER_MODULE
    Godot_MP_RefreshServerList();
#endif
}

void MoHAARunner::refresh_lan() {
    if (!initialized) return;
#ifdef HAS_MULTIPLAYER_MODULE
    Godot_MP_RefreshLAN();
#endif
}

int MoHAARunner::get_server_count() const {
#ifdef HAS_MULTIPLAYER_MODULE
    return Godot_MP_GetServerCount();
#else
    return 0;
#endif
}

// ──────────────────────────────────────────────
//  Settings helpers (Phases 267-270)
// ──────────────────────────────────────────────

void MoHAARunner::set_audio_volume(float master, float music, float dialog) {
    if (!initialized) return;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "set s_volume %f\nset s_musicvolume %f\nset s_dialogvolume %f\n",
             master, music, dialog);
    Cbuf_AddText(cmd);
}

void MoHAARunner::set_video_fullscreen(bool fullscreen) {
    // Keep Godot window mode and engine cvar in sync.
    if (initialized) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "set r_fullscreen %d\n", fullscreen ? 1 : 0);
        Cbuf_AddText(cmd);
    }

    cached_r_fullscreen = fullscreen ? 1 : 0;

    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds) return;
    if (fullscreen) {
        ds->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
    } else {
        ds->window_set_mode(DisplayServer::WINDOW_MODE_WINDOWED);
    }
}

void MoHAARunner::set_video_resolution(int width, int height) {
    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds) return;
    ds->window_set_size(Vector2i(width, height));

#ifdef HAS_WEAPON_VIEWPORT_MODULE
    Godot_WeaponViewport::get().resize(width, height);
#endif
}

void MoHAARunner::set_network_rate(const godot::String &p_preset) {
    if (!initialized) return;
    godot::CharString preset = p_preset.utf8();
    const char *p = preset.get_data();

    // Standard MOHAA rate presets
    if (strcmp(p, "modem") == 0) {
        Cbuf_AddText("set rate 4000\nset snaps 20\nset cl_maxpackets 30\n");
    } else if (strcmp(p, "isdn") == 0) {
        Cbuf_AddText("set rate 8000\nset snaps 30\nset cl_maxpackets 30\n");
    } else if (strcmp(p, "cable") == 0) {
        Cbuf_AddText("set rate 15000\nset snaps 40\nset cl_maxpackets 42\n");
    } else if (strcmp(p, "lan") == 0) {
        Cbuf_AddText("set rate 25000\nset snaps 40\nset cl_maxpackets 42\n");
    }
}

// ──────────────────────────────────────────────
//  Render quality settings
// ──────────────────────────────────────────────

void MoHAARunner::set_render_quality(const godot::String &p_preset) {
    godot::CharString preset = p_preset.utf8();
    const char *p = preset.get_data();

    if (strcmp(p, "low") == 0) {
        set_texture_quality(0);
        set_shadow_quality(0);
        set_geometry_quality(0);
        set_effects_quality(0);
        set_msaa(0);
        set_fxaa_enabled(false);
    } else if (strcmp(p, "medium") == 0) {
        set_texture_quality(1);
        set_shadow_quality(1);
        set_geometry_quality(1);
        set_effects_quality(1);
        set_msaa(0);
        set_fxaa_enabled(true);
    } else if (strcmp(p, "high") == 0) {
        set_texture_quality(2);
        set_shadow_quality(2);
        set_geometry_quality(2);
        set_effects_quality(2);
        set_msaa(1);
        set_fxaa_enabled(false);
    } else if (strcmp(p, "ultra") == 0) {
        set_texture_quality(3);
        set_shadow_quality(3);
        set_geometry_quality(3);
        set_effects_quality(3);
        set_msaa(2);
        set_fxaa_enabled(false);
    } else {
        UtilityFunctions::push_warning("[MoHAA] Unknown render quality preset: ", p_preset);
    }
}

void MoHAARunner::set_texture_quality(int level) {
    level = (level < 0) ? 0 : (level > 3) ? 3 : level;
    texture_quality = level;

    if (initialized) {
        // r_picmip: 0=full, 1=half, 2=quarter, 3=eighth — invert from quality
        int picmip = 3 - level;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "set r_picmip %d\n", picmip);
        Cbuf_AddText(cmd);
    }

    emit_signal("render_quality_changed", "texture_quality", level);
}

int MoHAARunner::get_texture_quality() const {
    return texture_quality;
}

void MoHAARunner::set_shadow_quality(int level) {
    level = (level < 0) ? 0 : (level > 3) ? 3 : level;
    shadow_quality = level;

    // No sun_light exists — OpenMOHAA uses baked shadows only.
    // Keep the atlas size setting for potential future use.
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        int atlas_sizes[] = { 512, 1024, 2048, 4096 };
        rs->directional_shadow_atlas_set_size(atlas_sizes[level], level < 2);
    }

    emit_signal("render_quality_changed", "shadow_quality", level);
}

int MoHAARunner::get_shadow_quality() const {
    return shadow_quality;
}

void MoHAARunner::set_geometry_quality(int level) {
    level = (level < 0) ? 0 : (level > 3) ? 3 : level;
    geometry_quality = level;

    if (initialized) {
        // r_subdivisions: lower = more tessellation. 16/8/4/2 for low→ultra
        int subdivs[] = { 16, 8, 4, 2 };
        // r_lodBias: positive = further LOD switch. 2/1/0/0 for low→ultra
        int lodbias[] = { 2, 1, 0, 0 };
        // ter_maxlod: terrain LOD (higher = coarser). 6/4/2/0 for low→ultra
        int termaxlod[] = { 6, 4, 2, 0 };

        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "set r_subdivisions %d\nset r_lodBias %d\nset ter_maxlod %d\n",
                 subdivs[level], lodbias[level], termaxlod[level]);
        Cbuf_AddText(cmd);
    }

    emit_signal("render_quality_changed", "geometry_quality", level);
}

int MoHAARunner::get_geometry_quality() const {
    return geometry_quality;
}

void MoHAARunner::set_effects_quality(int level) {
    level = (level < 0) ? 0 : (level > 3) ? 3 : level;
    effects_quality = level;

    if (initialized) {
        // r_detailTextures: 0=off for low, 1=on otherwise
        int detail = (level > 0) ? 1 : 0;
        // r_fastsky: 1=skip sky for low, 0=render sky otherwise
        int fastsky = (level == 0) ? 1 : 0;
        // r_drawmarks: 0=off for low, 1=on otherwise (decals/bullet holes)
        int drawmarks = (level > 0) ? 1 : 0;

        char cmd[256];
        snprintf(cmd, sizeof(cmd),
                 "set r_detailTextures %d\nset r_fastsky %d\nset r_drawmarks %d\n",
                 detail, fastsky, drawmarks);
        Cbuf_AddText(cmd);
    }

    // Godot-side: SSAO for high/ultra
    if (world_env) {
        Ref<Environment> env = world_env->get_environment();
        if (env.is_valid()) {
            env->set_ssao_enabled(level >= 2);
            if (level >= 2) {
                env->set_ssao_radius(level >= 3 ? 2.0f : 1.0f);
                env->set_ssao_intensity(level >= 3 ? 1.5f : 1.0f);
            }
        }
    }

    emit_signal("render_quality_changed", "effects_quality", level);
}

int MoHAARunner::get_effects_quality() const {
    return effects_quality;
}

void MoHAARunner::set_msaa(int level) {
    level = (level < 0) ? 0 : (level > 3) ? 3 : level;
    msaa_level = level;

    // Apply to the root viewport at runtime via get_viewport()
    // 0=disabled, 1=2x, 2=4x, 3=8x
    Viewport *vp = get_viewport();
    if (vp) {
        vp->set_msaa_3d(static_cast<Viewport::MSAA>(level));
    }

    emit_signal("render_quality_changed", "msaa", level);
}

int MoHAARunner::get_msaa() const {
    return msaa_level;
}

void MoHAARunner::set_fxaa_enabled(bool enabled) {
    fxaa_enabled = enabled;

    Viewport *vp = get_viewport();
    if (vp) {
        vp->set_screen_space_aa(enabled
            ? Viewport::SCREEN_SPACE_AA_FXAA
            : Viewport::SCREEN_SPACE_AA_DISABLED);
    }

    emit_signal("render_quality_changed", "fxaa", enabled ? 1 : 0);
}

bool MoHAARunner::is_fxaa_enabled() const {
    return fxaa_enabled;
}

void MoHAARunner::set_vsync_mode(int mode) {
    mode = (mode < 0) ? 0 : (mode > 3) ? 3 : mode;
    DisplayServer *ds = DisplayServer::get_singleton();
    if (ds) {
        ds->window_set_vsync_mode(static_cast<DisplayServer::VSyncMode>(mode));
    }

    emit_signal("render_quality_changed", "vsync_mode", mode);
}

int MoHAARunner::get_vsync_mode() const {
    DisplayServer *ds = DisplayServer::get_singleton();
    if (ds) {
        return static_cast<int>(ds->window_get_vsync_mode());
    }
    return 0;
}

// ──────────────────────────────────────────────
//  Menu control (Phase 261)
// ──────────────────────────────────────────────

void MoHAARunner::open_main_menu() {
    if (!initialized) return;
    Cbuf_AddText("pushmenu main\n");
}

void MoHAARunner::close_menu() {
    if (!initialized) return;
    Cbuf_AddText("popmenu 0\n");
}

void MoHAARunner::push_menu(const String &menu_name) {
    if (!initialized || menu_name.is_empty()) return;
    CharString name = menu_name.utf8();
    String cmd = String("pushmenu ") + String(name.get_data()) + String("\n");
    Cbuf_AddText(cmd.utf8().get_data());
}

void MoHAARunner::show_menu(const String &menu_name, bool force) {
    if (!initialized || menu_name.is_empty()) return;
    CharString name = menu_name.utf8();
    String cmd;
    if (force) {
        cmd = String("showmenu ") + String(name.get_data()) + String(" 1\n");
    } else {
        cmd = String("showmenu ") + String(name.get_data()) + String("\n");
    }
    Cbuf_AddText(cmd.utf8().get_data());
}

void MoHAARunner::toggle_menu(const String &menu_name) {
    if (!initialized || menu_name.is_empty()) return;
    CharString name = menu_name.utf8();
    String cmd = String("togglemenu ") + String(name.get_data()) + String("\n");
    Cbuf_AddText(cmd.utf8().get_data());
}

void MoHAARunner::pop_menu(bool restore_cvars) {
    if (!initialized) return;
    String cmd = String("popmenu ") + String(restore_cvars ? "1" : "0") + String("\n");
    Cbuf_AddText(cmd.utf8().get_data());
}

void MoHAARunner::hide_menu(const String &menu_name) {
    if (!initialized || menu_name.is_empty()) return;
    CharString name = menu_name.utf8();
    String cmd = String("hidemenu ") + String(name.get_data()) + String("\n");
    Cbuf_AddText(cmd.utf8().get_data());
}

bool MoHAARunner::is_menu_active() const {
    if (!initialized) return false;
    return Godot_UI_IsMenuActive() != 0;
}

void MoHAARunner::_input(const Ref<InputEvent> &p_event) {
    if (!initialized) return;

    // Keep UI transform current for accurate viewport→engine cursor mapping.
    update_ui_transform();

    bool overlay_active = (Godot_Client_IsAnyOverlayActive() != 0) ||
                          (Godot_Client_GetGuiMouse() != 0);

    // Handle fullscreen hotkey early so UI controls cannot consume Enter first.
    InputEventKey *key_event = Object::cast_to<InputEventKey>(p_event.ptr());
    if (key_event) {
        bool pressed = key_event->is_pressed();
        bool echo = key_event->is_echo();
        int keycode = (int)key_event->get_keycode();
        bool is_enter_key = (keycode == Key::KEY_ENTER || keycode == Key::KEY_KP_ENTER);
        bool has_toggle_modifier = key_event->is_alt_pressed() || key_event->is_meta_pressed();

        if (pressed && !echo && is_enter_key && has_toggle_modifier) {
            DisplayServer *ds = DisplayServer::get_singleton();
            int new_fullscreen = 1;
            if (ds) {
                DisplayServer::WindowMode cur_mode = ds->window_get_mode();
                new_fullscreen = (cur_mode == DisplayServer::WINDOW_MODE_FULLSCREEN) ? 0 : 1;
                ds->window_set_mode(new_fullscreen ? DisplayServer::WINDOW_MODE_FULLSCREEN
                                                   : DisplayServer::WINDOW_MODE_WINDOWED);
            }

            char cmd[64];
            snprintf(cmd, sizeof(cmd), "set r_fullscreen %d\n", new_fullscreen);
            Cbuf_AddText(cmd);
            cached_r_fullscreen = new_fullscreen;

            UtilityFunctions::print(String("[MoHAA] Fullscreen hotkey -> ") +
                String::num_int64(new_fullscreen));

            Viewport *vp = get_viewport();
            if (vp) vp->set_input_as_handled();
            return;
        }
    }

    // ── Mouse motion ──
    InputEventMouseMotion *motion_event = Object::cast_to<InputEventMouseMotion>(p_event.ptr());
    if (motion_event) {
        if (!overlay_active) {
            Vector2 rel = motion_event->get_relative();
            Godot_InjectMouseMotion((int)rel.x, (int)rel.y);
        } else {
            Vector2 pos = motion_event->get_position();
            float sx = (ui_scale_x > 0.0001f) ? ui_scale_x : 1.0f;
            float sy = (ui_scale_y > 0.0001f) ? ui_scale_y : 1.0f;
            int ex = (int)((pos.x - ui_offset_x) / sx);
            int ey = (int)((pos.y - ui_offset_y) / sy);
            if (ex < 0) ex = 0;
            if (ey < 0) ey = 0;
            if (ex >= ui_vid_w) ex = ui_vid_w - 1;
            if (ey >= ui_vid_h) ey = ui_vid_h - 1;
            Godot_Client_SetMousePos(ex, ey);
        }

        Viewport *vp = get_viewport();
        if (vp) vp->set_input_as_handled();
        return;
    }

    // ── Mouse buttons ──
    InputEventMouseButton *button_event = Object::cast_to<InputEventMouseButton>(p_event.ptr());
    if (button_event) {
        int godot_button = (int)button_event->get_button_index();
        bool pressed = button_event->is_pressed();

        if (overlay_active) {
            Vector2 pos = button_event->get_position();
            float sx = (ui_scale_x > 0.0001f) ? ui_scale_x : 1.0f;
            float sy = (ui_scale_y > 0.0001f) ? ui_scale_y : 1.0f;
            int ex = (int)((pos.x - ui_offset_x) / sx);
            int ey = (int)((pos.y - ui_offset_y) / sy);
            if (ex < 0) ex = 0;
            if (ey < 0) ey = 0;
            if (ex >= ui_vid_w) ex = ui_vid_w - 1;
            if (ey >= ui_vid_h) ey = ui_vid_h - 1;
            Godot_Client_SetMousePos(ex, ey);
        }

        if (godot_button >= 1 && godot_button <= 3) {
            Godot_InjectMouseButton(godot_button, pressed ? 1 : 0);
        } else if (godot_button == 8 || godot_button == 9) {
            Godot_InjectMouseButton(godot_button, pressed ? 1 : 0);
        } else if (godot_button >= 4 && godot_button <= 5) {
            if (pressed) {
                Godot_InjectMouseButton(godot_button, 1);
                Godot_InjectMouseButton(godot_button, 0);
            }
        }

#ifdef __EMSCRIPTEN__
        // On web, pointer lock requires a user gesture (click/keydown).
        // _input() runs inside the browser's DOM event handler, which is the
        // only context where requestPointerLock() is allowed.  If we want
        // capture but the browser doesn't have it yet, re-assert the mode
        // NOW — inside the gesture context — so the lock actually activates.
        if (pressed && !overlay_active) {
            Input *inp = Input::get_singleton();
            if (inp && inp->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
                inp->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
                mouse_captured = true;
            }
        }
#endif

        Viewport *vp = get_viewport();
        if (vp) vp->set_input_as_handled();
        return;
    }
}

void MoHAARunner::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!initialized) return;

    // Keep UI transform current for accurate viewport→engine cursor mapping,
    // especially on web where canvas/layout scale can change dynamically.
    update_ui_transform();

    // ── Keyboard events ──
    InputEventKey *key_event = Object::cast_to<InputEventKey>(p_event.ptr());
    if (key_event) {
        bool pressed = key_event->is_pressed();
        bool echo = key_event->is_echo();

        // OpenMOHAA parity: Alt/Meta+Enter toggles fullscreen immediately.
        {
            int keycode = (int)key_event->get_keycode();
            bool is_enter_key = (keycode == Key::KEY_ENTER || keycode == Key::KEY_KP_ENTER);
            bool has_toggle_modifier = key_event->is_alt_pressed() || key_event->is_meta_pressed();

            if (pressed && !echo && is_enter_key && has_toggle_modifier) {
                int new_fullscreen = Cvar_VariableIntegerValue("r_fullscreen") ? 0 : 1;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "set r_fullscreen %d\n", new_fullscreen);
                Cbuf_AddText(cmd);
                cached_r_fullscreen = new_fullscreen;

                DisplayServer *ds = DisplayServer::get_singleton();
                if (ds) {
                    ds->window_set_mode(new_fullscreen ? DisplayServer::WINDOW_MODE_FULLSCREEN
                                                       : DisplayServer::WINDOW_MODE_WINDOWED);
                }

                UtilityFunctions::print(String("[MoHAA] Fullscreen hotkey -> ") +
                    String::num_int64(new_fullscreen));

                Viewport *vp = get_viewport();
                if (vp) vp->set_input_as_handled();
                return;
            }
        }

        // ── DEBUG: Layer toggle keys to isolate double-rendering ──
        // F1 — toggle BSP world mesh
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F1) {
            if (bsp_map_node) {
                bsp_map_node->set_visible(!bsp_map_node->is_visible());
                UtilityFunctions::print(String("[DEBUG] BSP world mesh: ") +
                    (bsp_map_node->is_visible() ? String("ON") : String("OFF")));
            } else {
                UtilityFunctions::print("[DEBUG] BSP world mesh: no bsp_map_node!");
            }
            return;
        }
        // F2 — toggle static models
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F2) {
            if (static_model_root) {
                static_model_root->set_visible(!static_model_root->is_visible());
                UtilityFunctions::print(String("[DEBUG] Static models (") +
                    String::num_int64(static_model_root->get_child_count()) +
                    " children): " + (static_model_root->is_visible() ? String("ON") : String("OFF")));
            }
            return;
        }
        // F3 — toggle entities
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F3) {
            if (entity_root) {
                entity_root->set_visible(!entity_root->is_visible());
                UtilityFunctions::print(String("[DEBUG] Entities (") +
                    String::num_int64(entity_root->get_child_count()) +
                    " children): " + (entity_root->is_visible() ? String("ON") : String("OFF")));
            }
            return;
        }
        // F4 — dump scene tree summary
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F4) {
            UtilityFunctions::print("[DEBUG] === Scene Tree Dump ===");
            if (game_world) {
                UtilityFunctions::print(String("[DEBUG] game_world children: ") +
                    String::num_int64(game_world->get_child_count()));
                for (int ci = 0; ci < game_world->get_child_count(); ci++) {
                    Node *child = game_world->get_child(ci);
                    int gc = child ? child->get_child_count() : 0;
                    UtilityFunctions::print(String("[DEBUG]   ") +
                        String::num_int64(ci) + ": " + child->get_name() +
                        " (" + child->get_class() + ", " +
                        String::num_int64(gc) + " children)");
                }
            }
            return;
        }

        // F5 — toggle fog
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F5) {
            debug_fog_off = !debug_fog_off;
            if (world_env) {
                Ref<Environment> env = world_env->get_environment();
                if (env.is_valid()) {
                    env->set_fog_enabled(!debug_fog_off);
                    UtilityFunctions::print(String("[DEBUG] Fog: ") +
                        (!debug_fog_off ? String("ON") : String("OFF")));
                }
            }
            return;
        }
        // F7 — toggle wireframe mode (viewport debug draw)
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F7) {
            Viewport *vp = get_viewport();
            if (vp) {
                auto cur = vp->get_debug_draw();
                if (cur == Viewport::DEBUG_DRAW_WIREFRAME) {
                    vp->set_debug_draw(Viewport::DEBUG_DRAW_DISABLED);
                    UtilityFunctions::print("[DEBUG] Wireframe: OFF");
                } else {
                    // Must enable wireframe generation first
                    RenderingServer::get_singleton()->set_debug_generate_wireframes(true);
                    vp->set_debug_draw(Viewport::DEBUG_DRAW_WIREFRAME);
                    UtilityFunctions::print("[DEBUG] Wireframe: ON");
                }
            } else {
                UtilityFunctions::print("[DEBUG] Wireframe: no viewport!");
            }
            return;
        }
        // F8 — toggle textures off (flat colour materials on BSP)
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F8) {
            debug_notex = !debug_notex;
            if (bsp_map_node) {
                for (int ci = 0; ci < bsp_map_node->get_child_count(); ci++) {
                    MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(bsp_map_node->get_child(ci));
                    if (!mi) continue;
                    Ref<Mesh> m = mi->get_mesh();
                    if (m.is_null()) continue;
                    for (int si = 0; si < m->get_surface_count(); si++) {
                        Ref<Material> base = m->surface_get_material(si);
                        Ref<StandardMaterial3D> smat = Object::cast_to<StandardMaterial3D>(base.ptr());
                        if (smat.is_null()) continue;
                        if (debug_notex) {
                            smat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, Ref<Texture2D>());
                            smat->set_feature(BaseMaterial3D::FEATURE_DETAIL, false);
                            smat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
                        }
                    }
                }
                UtilityFunctions::print(String("[DEBUG] BSP textures: ") +
                    (!debug_notex ? String("ON (reload map to restore)") : String("OFF")));
            }
            return;
        }
        // F6 — quick save
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F6) {
            Godot_Save_QuickSave();
            UtilityFunctions::print("[MoHAA] Quick save requested");
            return;
        }

        // F9 — quick load
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F9) {
            Godot_Save_QuickLoad();
            UtilityFunctions::print("[MoHAA] Quick load requested");
            return;
        }

        // F10 — toggle HUD overlay visibility (debug aid)
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F10) {
            hud_visible = !hud_visible;
            if (hud_layer) hud_layer->set_visible(hud_visible);
            UtilityFunctions::print(String("[MoHAA] HUD overlay ") + (hud_visible ? String("ON") : String("OFF")));
            return;
        }

        // F11 — dump input state (debug: keyCatchers, bindings, mouse, pause)
        if (pressed && !echo && key_event->get_keycode() == Key::KEY_F11) {
            Godot_Client_DumpInputState();
            UtilityFunctions::print(String("[MoHAA] mouse_captured=") +
                String::num_int64(mouse_captured ? 1 : 0) +
                String(" godot_mouse_mode=") +
                String::num_int64((int)Input::get_singleton()->get_mouse_mode()));
            return;
        }

        // TAB — track held state for Godot-side scoreboard overlay.
        // The key event still flows through to the engine below so the
        // engine's own +scores binding (if present) also fires.
        if (!echo && key_event->get_keycode() == Key::KEY_TAB) {
            scoreboard_visible = pressed;
        }

        // Get the keycode (logical key, respects keyboard layout)
        int godot_key = (int)key_event->get_keycode();
        if (godot_key == 0) {
            // Fallback to physical keycode if logical is unavailable
            godot_key = (int)key_event->get_physical_keycode();
        }

        if (godot_key != 0) {
            /* Suppress SE_CHAR for console toggle keys (backtick/tilde)
               to prevent typing ` or ~ into the console input field. */
            bool is_console_key = (godot_key == Key::KEY_QUOTELEFT
                                   || godot_key == Key::KEY_ASCIITILDE);

            // Always inject key events into the engine's event queue.
            // The engine's CL_KeyEvent (cl_keys.cpp) checks keyCatchers
            // internally and routes to UI_KeyEvent / Console_Key / game
            // bindings as appropriate.  No need for a parallel routing layer.
            Godot_InjectKeyEvent(godot_key, pressed ? 1 : 0);
            if ((pressed || echo) && !is_console_key) {
                int64_t unicode = key_event->get_unicode();
                // Most non-printable keys (Tab=9, Delete=127, etc.) are handled
                // via SE_KEY (Godot_InjectKeyEvent) above, but UIField specifically
                // expects Backspace as a character event '\b' (8).
                // Godot's get_unicode() might return 0 for Backspace on some layouts.
                if (godot_key == Key::KEY_BACKSPACE || (int)key_event->get_physical_keycode() == Key::KEY_BACKSPACE) {
                    unicode = 8;
                }

                if ((unicode >= 32 && unicode != 127) || unicode == 8) {
                    Godot_InjectCharEvent((int)unicode);
                }
            }
        }

        return;
    }

    // Mouse events are handled in _input() so UI hover/click still works even
    // when controls consume input before the unhandled phase.
}
