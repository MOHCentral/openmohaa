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
    int  Godot_Client_GetMouseButtons(void);
    void Godot_Client_DumpInputState(void);

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
                                   int *outBoneCount);
    int   Godot_Skel_SkinSurface(void *tikiPtr, int meshIndex, int surfIndex,
                                  const void *boneCache, int boneCount,
                                  float *outPositions, float *outNormals);
    int   Godot_Skel_GetMeshCount(void *tikiPtr);
    float Godot_Skel_GetScale(void *tikiPtr);
    void  Godot_Skel_GetOrigin(void *tikiPtr, float *out);
    int   Godot_Skel_GetSurfaceCount(void *tikiPtr, int meshIndex);
    int   Godot_Skel_GetSurfaceInfo(void *tikiPtr, int meshIndex, int surfIndex,
                                     int *numVerts, int *numTriangles,
                                     char *surfName, int surfNameLen,
                                     char *shaderName, int shaderNameLen);
    int   Godot_Skel_GetSurfaceVertices(void *tikiPtr, int meshIndex, int surfIndex,
                                         float *positions, float *normals, float *texcoords);
    int   Godot_Skel_GetSurfaceIndices(void *tikiPtr, int meshIndex, int surfIndex,
                                        int *indices);

    // Phase 35: Entity parenting — from godot_renderer.c
    int   Godot_Renderer_GetEntityParent(int index);

    // Phase 268: Entity lighting origin — from godot_renderer.c
    void  Godot_Renderer_GetEntityLightingOrigin(int index, float *out);

    // Model/Shadow accessors
    float Godot_Renderer_GetEntityShadowPlane(int index);
    float Godot_Model_GetRadius(int hModel);
    int   Godot_Model_GetSpriteShader(int hModel);

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
            loaded_bsp_name = "";
            GodotSkelModelCache::get().clear();  // Invalidate model cache
            skel_mesh_cache.clear();              // Phase 60: Clear skinned mesh cache
            tinted_mat_cache.clear();             // Phase 61: Clear tinted material cache
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
#ifdef HAS_MESH_CACHE_MODULE
            Godot_MeshCache::get().clear();
            Godot_MaterialCache::get().clear();
#endif
#ifdef HAS_SHADER_MATERIAL_MODULE
            Godot_Shader_ClearCache();
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
            Godot_VFX_Clear();  // Flush VFX caches — shader handles are invalidated by BeginRegistration
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
    s_surf_anim_cache.clear();
    s_sprite_mat_cache.clear();
    s_beam_mat_cache.clear();
    s_poly_mat_cache.clear();
    s_terrain_mark_mat_cache.clear();
    s_sprite_tint_cache.clear();
    s_beam_tint_cache.clear();

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

        // ── Module hooks for world load (defensive) ──
#ifdef HAS_WEATHER_MODULE
        Godot_Weather_Init(game_world);
#endif
        // Enable sound occlusion now that BSP collision data is available
        Godot_SoundOcclusion_SetEnabled(1);
        UtilityFunctions::print("[MoHAA] Sound occlusion enabled.");

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

    // Camera position in Godot coordinates for distance checks
    Vector3 cam_pos = id_to_godot_position(origin[0], origin[1], origin[2]);

    // Distance threshold: always show clusters within this radius of the camera
    // regardless of PVS, to compensate for incomplete BSP vis data and cluster
    // boundary edge cases.  1024 id units ~ 26 metres ~ a couple of rooms.
    static constexpr float PVS_FORCE_VISIBLE_DISTANCE = 1024.0f * MOHAA_UNIT_SCALE;
    static constexpr float PVS_FORCE_VISIBLE_DIST_SQ = PVS_FORCE_VISIBLE_DISTANCE * PVS_FORCE_VISIBLE_DISTANCE;

    // If camera is outside the world (cluster -1), show everything
    if (new_cluster < 0) {
        for (int c = 0; c < num_clusters; c++) {
            MeshInstance3D *mi = Godot_BSP_GetClusterMesh(c);
            if (mi) mi->set_visible(true);
        }
        return;
    }

    // Toggle per-cluster visibility based on PVS + distance override
    int visible_count = 0;
    int hidden_count = 0;
    int forced_count = 0;
    for (int c = 0; c < num_clusters; c++) {
        MeshInstance3D *mi = Godot_BSP_GetClusterMesh(c);
        if (!mi) continue;

        bool vis = (Godot_BSP_ClusterVisible(new_cluster, c) != 0);

        // Distance override: force-show clusters whose AABB is near the camera.
        // This prevents blue gaps from incomplete PVS data at cluster boundaries.
        if (!vis) {
            AABB aabb = mi->get_global_transform().xform(mi->get_aabb());
            // Closest point on AABB to camera
            Vector3 closest;
            closest.x = CLAMP(cam_pos.x, aabb.position.x, aabb.position.x + aabb.size.x);
            closest.y = CLAMP(cam_pos.y, aabb.position.y, aabb.position.y + aabb.size.y);
            closest.z = CLAMP(cam_pos.z, aabb.position.z, aabb.position.z + aabb.size.z);
            float dist_sq = (cam_pos - closest).length_squared();
            if (dist_sq < PVS_FORCE_VISIBLE_DIST_SQ) {
                vis = true;
                forced_count++;
            }
        }

        mi->set_visible(vis);
        if (vis) visible_count++;
        else     hidden_count++;
    }

    if (pvs_log_count < 5) {
        UtilityFunctions::print(String("[PVS] Cluster ") +
                                String::num_int64(new_cluster) +
                                ": " + String::num_int64(visible_count) +
                                " visible (" + String::num_int64(forced_count) +
                                " forced), " + String::num_int64(hidden_count) +
                                " hidden of " + String::num_int64(num_clusters) +
                                " total.");
        pvs_log_count++;
    }
}

// ──────────────────────────────────────────────
//  Static BSP model loading (Phase 10)
// ──────────────────────────────────────────────

/// Apply shader transparency / cull properties to a StandardMaterial3D.
/// Call after setting the albedo texture.  shader_name is the C-string
/// name used for the Godot_ShaderProps_Find lookup (e.g. "textures/foo/bar").
static void apply_shader_props_to_material(Ref<StandardMaterial3D> &mat,
                                            const char *shader_name)
{
    if (!shader_name || !shader_name[0]) return;

    const GodotShaderProps *sp = Godot_ShaderProps_Find(shader_name);
    if (!sp) {
        if (shader_name && strstr(shader_name, "bh_wood")) {
            UtilityFunctions::print(String("[MoHAA][3D] ShaderProps NULL for ") + String(shader_name) + ", FORCING TRANSPARENCY_ALPHA!");
            mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        }
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

        if (!cached || !cached->mesh.is_valid()) {
            failed++;
            continue;
        }

        // Create MeshInstance3D
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String("SM_") + String::num_int64(i));
        mi->set_mesh(cached->mesh);
        mi->set_extra_cull_margin(4.0f);

        // Apply shader textures to each surface.
        // Mirrors R_InitStaticModels: register each surface shader via
        // RegisterShader, then use the returned handle for texture lookup
        // through the standard get_shader_texture pipeline.
        for (int s = 0; s < (int)cached->surfaces.size(); s++) {
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            // MOHAA default is CT_FRONT_SIDED (back-face cull).
            // apply_shader_props_to_material() overrides to CULL_DISABLED
            // only if the shader says "cull none".
            mat->set_cull_mode(BaseMaterial3D::CULL_BACK);

            /* Static models: PER_PIXEL so they cast and receive dynamic
             * shadows from the directional light.  The sun's low energy
             * prevents double-lighting while ambient=1.0 preserves the
             * original brightness. */
            mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            mat->set_specular(0.2f);
            mat->set_roughness(0.9f);

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
        placed++;
    }

    UtilityFunctions::print(String("[MoHAA] Static models: ") +
                            String::num_int64(placed) + " placed, " +
                            String::num_int64(failed) + " failed.");
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

    // id Tech 3 / OpenGL cubemap face suffixes in Godot cubemap layer order:
    //   Layer 0 = +X (right),  Layer 1 = -X (left),
    //   Layer 2 = +Y (up),     Layer 3 = -Y (down),
    //   Layer 4 = +Z (back),   Layer 5 = -Z (front)
    static const char *suffixes[6] = { "_rt", "_lf", "_up", "_dn", "_bk", "_ft" };
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

        face_images[i] = img;
        loaded++;
    }

    if (loaded != 6) return;

    // Create Cubemap from the 6 face images
    Ref<Cubemap> cubemap;
    cubemap.instantiate();
    Error err = cubemap->create_from_images(face_images);
    if (err != OK) {
        UtilityFunctions::printerr("[MoHAA] Failed to create sky cubemap.");
        return;
    }

    // Create a sky shader that samples the cubemap
    Ref<Shader> sky_shader;
    sky_shader.instantiate();
    sky_shader->set_code(
        "shader_type sky;\n"
        "uniform samplerCube sky_cubemap : source_color;\n"
        "void sky() {\n"
        "    COLOR = texture(sky_cubemap, EYEDIR).rgb;\n"
        "}\n"
    );

    // Create ShaderMaterial and assign cubemap
    Ref<ShaderMaterial> sky_mat;
    sky_mat.instantiate();
    sky_mat->set_shader(sky_shader);
    sky_mat->set_shader_parameter("sky_cubemap", cubemap);

    // Create Sky resource
    Ref<Sky> sky;
    sky.instantiate();
    sky->set_material(sky_mat);
    sky->set_radiance_size(Sky::RADIANCE_SIZE_256);

    // Update environment to use the skybox
    Ref<Environment> env = world_env->get_environment();
    if (env.is_valid()) {
        env->set_background(Environment::BG_SKY);
        env->set_sky(sky);
        UtilityFunctions::print(
            String("[MoHAA] Skybox loaded: ") + sky_env + " (6 faces).");
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
// RT_MODEL=0, RT_POLY=1, RT_SPRITE=2, RT_BEAM=3
static constexpr int RT_MODEL   = 0;
static constexpr int RT_SPRITE  = 2;
static constexpr int RT_BEAM    = 3;

void MoHAARunner::update_entities() {
    if (!game_world) return;

    int ent_count = Godot_Renderer_GetEntityCount();

    // Log entity breakdown once when first entities appear
    static bool logged_entity_count = false;
    if (!logged_entity_count && ent_count > 0) {
        int n_brush = 0, n_tiki = 0, n_sprite = 0, n_beam = 0, n_other = 0;
        for (int ei = 0; ei < ent_count; ei++) {
            float eo[3], ea[9], es = 1.0f;
            int eh = 0, en = 0, erf = 0;
            unsigned char ec[4] = {255,255,255,255};
            int et = Godot_Renderer_GetEntity(ei, eo, ea, &es, &eh, &en, ec, &erf);
            if (et == RT_MODEL && eh > 0) {
                int mt = Godot_Model_GetType(eh);
                if (mt == 1) n_brush++;
                else n_tiki++;
            } else if (et == RT_SPRITE) n_sprite++;
            else if (et == RT_BEAM) n_beam++;
            else n_other++;
        }
        UtilityFunctions::print(String("[MoHAA] Entity breakdown: ") +
            String::num_int64(ent_count) + " total = " +
            String::num_int64(n_brush) + " brush + " +
            String::num_int64(n_tiki) + " tiki + " +
            String::num_int64(n_sprite) + " sprite + " +
            String::num_int64(n_beam) + " beam + " +
            String::num_int64(n_other) + " other");

        // Log first 10 brush model entities with their positions and mesh info
        if (n_brush > 0) {
            int logged = 0;
            for (int ei = 0; ei < ent_count && logged < 10; ei++) {
                float eo[3], ea[9], es = 1.0f;
                int eh = 0, en = 0, erf = 0;
                unsigned char ec[4] = {255,255,255,255};
                int et = Godot_Renderer_GetEntity(ei, eo, ea, &es, &eh, &en, ec, &erf);
                if (et == RT_MODEL && eh > 0) {
                    int mt = Godot_Model_GetType(eh);
                    if (mt == 1) {
                        const char *mn = Godot_Model_GetName(eh);
                        int subIdx = 0;
                        if (mn && mn[0] == '*') subIdx = atoi(mn + 1);
                        Ref<ArrayMesh> bmesh = Godot_BSP_GetBrushModelMesh(subIdx);
                        String mesh_info = "null";
                        if (bmesh.is_valid()) {
                            int sc = bmesh->get_surface_count();
                            int total_verts = 0;
                            for (int si = 0; si < sc; si++) {
                                Array arr = bmesh->surface_get_arrays(si);
                                if (arr.size() > 0) {
                                    PackedVector3Array verts = arr[Mesh::ARRAY_VERTEX];
                                    total_verts += verts.size();
                                }
                            }
                            mesh_info = String::num_int64(sc) + " surfaces, " +
                                        String::num_int64(total_verts) + " verts";
                        }
                        Vector3 gpos = id_to_godot_position(eo[0], eo[1], eo[2]);
                        UtilityFunctions::print(String("[MoHAA]   Brush ent #") +
                            String::num_int64(ei) + ": model=" + String(mn ? mn : "?") +
                            " idPos=(" + String::num(eo[0], 1) + ", " +
                            String::num(eo[1], 1) + ", " + String::num(eo[2], 1) + ")" +
                            " godotPos=(" + String::num(gpos.x, 2) + ", " +
                            String::num(gpos.y, 2) + ", " + String::num(gpos.z, 2) + ")" +
                            " mesh=[" + mesh_info + "]" +
                            " renderfx=0x" + String::num_int64(erf, 16) +
                            " axis0=(" + String::num(ea[0], 2) + "," + String::num(ea[1], 2) + "," + String::num(ea[2], 2) + ")");
                        logged++;
                    }
                }
            }
        }
        logged_entity_count = true;
    }

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
    int dbg_reached_modtype = 0, dbg_reached_visible = 0, dbg_brush_path = 0;
    for (int i = 0; i < ent_count; i++) {
        float origin[3], axis[9], scale = 1.0f;
        int hModel = 0, entityNumber = 0, renderfx = 0;
        unsigned char rgba[4] = {255, 255, 255, 255};

        int reType = Godot_Renderer_GetEntity(i, origin, axis, &scale,
                                               &hModel, &entityNumber,
                                               rgba, &renderfx);

        MeshInstance3D *mi = entity_meshes[i];

        // Quick pre-check: is this a brush entity?
        // Brush models (doors, movers) need special handling for frustum
        // culling since their entity origin is a movement delta, not the
        // mesh centre.
        bool is_brush_entity = false;
        if (reType == 0 /* RT_MODEL */ && hModel > 0) {
            const char *mn = Godot_Model_GetName(hModel);
            if (mn && mn[0] == '*') is_brush_entity = true;
        }

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

        // RF_THIRD_PERSON (1<<0 = 0x01): player body — not visible in first-person
        // RF_FIRST_PERSON (1<<1 = 0x02): view weapon — only visible in first-person
        // RF_DEPTHHACK     (1<<2 = 0x04): compress depth so weapon doesn't clip into walls
        // RF_DONTDRAW      (1<<7 = 0x80): don't draw this entity
        if (renderfx & 0x01) {  // RF_THIRD_PERSON — skip player body
            mi->set_visible(false);
            continue;
        }

        // PVS culling: skip entities not visible from the camera's cluster.
        // Skip first-person entities (RF_FIRST_PERSON / RF_DEPTHHACK) — they
        // are always visible as they're attached to the view weapon.
        if (pvs_current_cluster >= 0 && !(renderfx & 0x06)) {
            if (!Godot_BSP_InPVS(pvs_cam_origin, origin)) {
                mi->set_visible(false);
                continue;
            }
        }

        // Phase 133: Frustum and draw distance culling — skip entities that
        // are outside the camera frustum or beyond the far-plane cull distance.
        // First-person entities (RF_FIRST_PERSON / RF_DEPTHHACK) are always
        // visible because they are the view weapon.
#if defined(HAS_FRUSTUM_CULL_MODULE) || defined(HAS_DRAW_DISTANCE_MODULE)
        if (!(renderfx & 0x06)) {
            Vector3 ent_pos = id_to_godot_position(origin[0], origin[1], origin[2]);
#ifdef HAS_FRUSTUM_CULL_MODULE
            // Brush models can be very large (doors, bridges, platforms) and
            // their entity origin is the movement origin, not the mesh centre.
            // Skip our manual frustum culling for brush entities — Godot's own
            // per-AABB frustum culling in the renderer handles them correctly.
            if (!is_brush_entity && !Godot_FrustumCull_TestSphere(ent_pos, 2.0f)) {
                mi->set_visible(false);
                continue;
            }
#endif
#ifdef HAS_DRAW_DISTANCE_MODULE
            float cull_dist = Godot_DrawDistance_GetCullDistance();
            if (cull_dist > 0.0f) {
                Vector3 cam_pos = camera ? camera->get_global_position() : Vector3();
                if (ent_pos.distance_to(cam_pos) > cull_dist) {
                    mi->set_visible(false);
                    continue;
                }
            }
#endif
        }
#endif

        // RT_SPRITE: billboard quad at entity origin (Phase 16)
        if (reType == RT_SPRITE) {
            float radius = 0.0f, rotation = 0.0f;
            int customShader = 0;
            Godot_Renderer_GetEntitySprite(i, &radius, &rotation, &customShader);

            if (radius < 0.001f) {
                mi->set_visible(false);
                continue;
            }
            // Engine radius is already the half-extent (see RB_SurfaceSprite:
            // VectorScale(axis, radius, left/up) → quad spans -radius to +radius).
            // We build vertices at ±half, so half = radius converted to metres.

            // Use customShader if set, else extract the registered shader from the sprite model handle
            int spriteShader = (customShader > 0) ? customShader : Godot_Model_GetSpriteShader(hModel);

            float half = radius * MOHAA_UNIT_SCALE;

            // Build a simple quad (2 triangles) — billboard handled by material
            PackedVector3Array gPos;
            PackedVector2Array gUV;
            PackedColorArray   gCol;
            PackedInt32Array   gIdx;
            gPos.resize(4);
            gUV.resize(4);
            gCol.resize(4);
            gIdx.resize(6);

            gPos.set(0, Vector3(-half, -half, 0.0f));
            gPos.set(1, Vector3( half, -half, 0.0f));
            gPos.set(2, Vector3( half,  half, 0.0f));
            gPos.set(3, Vector3(-half,  half, 0.0f));
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

                bool shader_props_found = false;
                if (spriteShader > 0) {
                    const char *sn = Godot_Renderer_GetShaderName(spriteShader);
                    if (sn && sn[0]) {
                        const GodotShaderProps *sp = Godot_ShaderProps_Find(sn);
                        if (sp) {
                            shader_props_found = true;
                            apply_shader_props_to_material(smat, sn);

                            // Re-enforce sprite-specific settings that
                            // apply_shader_props_to_material may override:
                            // - cull: shader default SHADER_CULL_BACK → CULL_BACK,
                            //   but billboard sprites must show both sides.
                            // - billboard: must stay BILLBOARD_ENABLED.
                            // - depth: sprites should not write depth.
                            // - shading: sprites are fullbright/unshaded.
                            smat->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
                            smat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
                            smat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                            smat->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);

                            // If the shader was classified as OPAQUE (no blendFunc
                            // on first non-lightmap stage, or stage parse missed it),
                            // sprites still need transparency. Infer from texture
                            // alpha: no alpha → additive (fire/flash), has alpha →
                            // standard alpha blend (smoke/blood).
                            if (sp->transparency == SHADER_OPAQUE) {
                                Ref<ImageTexture> tex = get_shader_texture(spriteShader);
                                if (tex.is_valid()) {
                                    smat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                                }
                                auto ha_it = shader_texture_has_alpha.find(spriteShader);
                                bool tex_has_alpha = (ha_it != shader_texture_has_alpha.end()) && ha_it->second;
                                if (!tex_has_alpha) {
                                    smat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                                }
                            }
                        }
                    }
                }

                // When no .shader definition is found, determine blend mode
                // from the texture's alpha channel. In MOHAA, sprite effects
                // without alpha (fire, flash, corona, sparks) use additive
                // blending (blendFunc add) — black pixels add nothing,
                // making quad edges invisible. Sprites WITH alpha (smoke,
                // blood) use standard alpha blending.
                if (!shader_props_found && spriteShader > 0) {
                    Ref<ImageTexture> tex = get_shader_texture(spriteShader);
                    if (tex.is_valid()) {
                        smat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                    }
                    auto ha_it = shader_texture_has_alpha.find(spriteShader);
                    bool tex_has_alpha = (ha_it != shader_texture_has_alpha.end()) && ha_it->second;
                    if (!tex_has_alpha) {
                        // No alpha channel → additive blend (fire/flash/corona)
                        smat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
                    }
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
            mi->set_visible(true);
            continue;
        }

        // 
        if (renderfx & 0x80) {  // RF_DONTDRAW
            mi->set_visible(false);
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
        dbg_reached_modtype++;

        if (modType == 1 /* GR_MOD_BRUSH */) {
            dbg_brush_path++;
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
                // One-shot: log brush model mesh details on first assignment
                static std::unordered_set<int> logged_brush_assign;
                if (logged_brush_assign.find(subIdx) == logged_brush_assign.end()) {
                    logged_brush_assign.insert(subIdx);
                    int sc = bmesh->get_surface_count();
                    int total_v = 0, total_i = 0;
                    for (int si = 0; si < sc; si++) {
                        Array arr = bmesh->surface_get_arrays(si);
                        if (arr.size() > 0) {
                            PackedVector3Array vv = arr[Mesh::ARRAY_VERTEX];
                            PackedInt32Array ii = arr[Mesh::ARRAY_INDEX];
                            total_v += vv.size();
                            total_i += ii.size();
                        }
                        Ref<Material> mat = bmesh->surface_get_material(si);
                        String mattype = mat.is_valid() ? mat->get_class() : "null";
                        UtilityFunctions::print(String("[MoHAA-BRUSH] *") +
                            String::num_int64(subIdx) + " surf" + String::num_int64(si) +
                            " mat=" + mattype);
                    }
                    AABB aabb = bmesh->get_aabb();
                    UtilityFunctions::print(String("[MoHAA-BRUSH] *") +
                        String::num_int64(subIdx) + ": " +
                        String::num_int64(sc) + " surfaces, " +
                        String::num_int64(total_v) + " verts, " +
                        String::num_int64(total_i) + " indices" +
                        " AABB=(" + String::num(aabb.position.x, 2) + "," +
                        String::num(aabb.position.y, 2) + "," +
                        String::num(aabb.position.z, 2) + ")-(" +
                        String::num(aabb.position.x + aabb.size.x, 2) + "," +
                        String::num(aabb.position.y + aabb.size.y, 2) + "," +
                        String::num(aabb.position.z + aabb.size.z, 2) + ")");
                }
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
            // Diagnostic: log model type for entities that reach TIKI path
            {
                static std::unordered_set<int> logged_tiki_path_models;
                if (logged_tiki_path_models.find(hModel) == logged_tiki_path_models.end()) {
                    logged_tiki_path_models.insert(hModel);
                    const char *modName = Godot_Model_GetName(hModel);
                    void *tikiCheck = Godot_Model_GetTikiPtr(hModel);
                    UtilityFunctions::print(String("[MoHAA][TIKI-PATH] Entity reaching TIKI path: hModel=") +
                        String::num_int64(hModel) + " modType=" + String::num_int64(modType) +
                        " hasTiki=" + String(tikiCheck ? "YES" : "NO") +
                        " name=" + String(modName ? modName : "(null)") +
                        " rgba=(" + String::num_int64(rgba[0]) + "," + String::num_int64(rgba[1]) + "," +
                        String::num_int64(rgba[2]) + "," + String::num_int64(rgba[3]) + ")");
                }
            }
            // Try to get the cached bind-pose model (may be null for
            // dynamically registered models like FPS weapon TIKIs).
            const GodotSkelModelCache::CachedModel *cached =
                GodotSkelModelCache::get().get_model(hModel);

            // ── Build / cache materials for this model (one-time) ──
            // Materials are built from the cached model's surface shader
            // names.  If the cache has no model, build materials from the
            // TIKI data directly via the accessor layer.
            static std::unordered_map<int, std::vector<Ref<StandardMaterial3D>>> mat_cache;
            if (!same_key && mat_cache.find(hModel) == mat_cache.end()) {
                auto &mats = mat_cache[hModel];

                // Determine surface count and shader names from cache or TIKI
                int surf_total = 0;
                std::vector<String> surf_shader_names;

                if (cached && cached->mesh.is_valid()) {
                    surf_total = (int)cached->surfaces.size();
                    for (int s = 0; s < surf_total; s++) {
                        surf_shader_names.push_back(cached->surfaces[s].shader_name);
                    }
                } else {
                    // No cached model — read surface info directly from TIKI
                    void *tiki_for_mats = Godot_Model_GetTikiPtr(hModel);
                    if (tiki_for_mats) {
                        int meshCount = Godot_Skel_GetMeshCount(tiki_for_mats);
                        for (int m = 0; m < meshCount; m++) {
                            int sc = Godot_Skel_GetSurfaceCount(tiki_for_mats, m);
                            for (int s = 0; s < sc; s++) {
                                int nv = 0, nt = 0;
                                char sn[64] = {0}, sh[64] = {0};
                                Godot_Skel_GetSurfaceInfo(tiki_for_mats, m, s,
                                    &nv, &nt, sn, sizeof(sn), sh, sizeof(sh));
                                if (nv > 0 && nt > 0) {
                                    surf_shader_names.push_back(String(sh));
                                    surf_total++;
                                }
                            }
                        }
                    }
                }

                for (int s = 0; s < surf_total; s++) {
                    Ref<StandardMaterial3D> mat;
                    mat.instantiate();
                    mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
                    /* UNSHADED: OpenMOHAA lights entities via the baked Light Grid,
                     * which is applied as an albedo color multiplier below.
                     * No dynamic Godot lights exist, so PER_PIXEL would be black. */
                    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

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
                    }

                    mats.push_back(mat);
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

            // One-time diagnostic: report first entity with anim data
            static bool logged_anim_diag = false;
            if (!logged_anim_diag && has_anim && tikiPtr) {
                UtilityFunctions::print(
                    String("[MoHAA] First entity with anim data: entNum=") +
                    String::num_int64(entNum) +
                    String(" hModel=") + String::num_int64(hModel));
                logged_anim_diag = true;
            }

            if (has_anim && tikiPtr) {
                // Phase 60: Compute FNV-1a hash of animation state to
                // skip mesh rebuild when the pose hasn't changed.
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

                // Check cache: if animation state unchanged, reuse mesh
                auto cache_it = skel_mesh_cache.find(entNum);
                if (cache_it != skel_mesh_cache.end() &&
                    cache_it->second.anim_hash == anim_hash &&
                    cache_it->second.mesh != nullptr) {
                    skinned_mesh = cache_it->second.mesh;
                } else {
                    int boneCount = 0;
                    void *boneCache = Godot_Skel_PrepareBones(
                        tikiPtr, entNum,
                        (const void *)frameInfoBuf, boneTagBuf,
                        (const float *)boneQuatBuf,
                        actionWeight, &boneCount);

                    if (boneCache && boneCount > 0) {
                        skinned_mesh.instantiate();
                        int meshCount = Godot_Skel_GetMeshCount(tikiPtr);
                        float tikiScale = Godot_Skel_GetScale(tikiPtr);

                        for (int mesh = 0; mesh < meshCount; mesh++) {
                            int surfCount = Godot_Skel_GetSurfaceCount(tikiPtr, mesh);
                            for (int surf = 0; surf < surfCount; surf++) {
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

                                // Get skinned positions + normals
                                if (!Godot_Skel_SkinSurface(tikiPtr, mesh, surf,
                                        boneCache, boneCount,
                                        positions, normals)) {
                                    ::free(positions); ::free(normals);
                                    ::free(texcoords); ::free(indices);
                                    continue;
                                }

                                // Get UVs + indices from static data
                                Godot_Skel_GetSurfaceVertices(tikiPtr, mesh, surf,
                                    nullptr, nullptr, texcoords);
                                Godot_Skel_GetSurfaceIndices(tikiPtr, mesh, surf,
                                    indices);

                                // Build Godot arrays with coord conversion
                                PackedVector3Array gPos, gNrm;
                                PackedVector2Array gUVs;
                                PackedInt32Array   gIdx;
                                gPos.resize(numVerts);
                                gNrm.resize(numVerts);
                                gUVs.resize(numVerts);
                                gIdx.resize(numTris * 3);

                                for (int v = 0; v < numVerts; v++) {
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
                                skinned_mesh->add_surface_from_arrays(
                                    Mesh::PRIMITIVE_TRIANGLES, arrays);

                                ::free(positions);
                                ::free(normals);
                                ::free(texcoords);
                                ::free(indices);
                            }
                        }

                        ::free(boneCache);
                    }

                    // Phase 60: Cache the newly built skinned mesh
                    if (skinned_mesh.is_valid() && skinned_mesh->get_surface_count() > 0) {
                        auto &entry = skel_mesh_cache[entNum];
                        entry.anim_hash = anim_hash;
                        entry.mesh = skinned_mesh;
                        entry.mesh_surfaces = skinned_mesh->get_surface_count();
                    }
                }  // end else (cache miss)
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
            } else if (cached && cached->mesh.is_valid()) {
                if (mi->get_mesh() != cached->mesh) {
                    mi->set_mesh(cached->mesh);
                    mesh_changed = true;
                }
            } else {
                // No mesh available — parity with OpenMOHAA: do not render
                mi->set_visible(false);
                continue; // Skip material application and drawing
            }

            // Apply cached materials (after set_mesh which clears overrides)
            if (mesh_changed && mat_cache.find(hModel) != mat_cache.end()) {
                auto &mats = mat_cache[hModel];
                int sc = mi->get_mesh().is_valid()
                       ? mi->get_mesh()->get_surface_count() : 0;

                for (int s = 0; s < (int)mats.size() && s < sc; s++) {
                    mi->set_surface_override_material(s, mats[s]);
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

        // ── Phase 62: Weapon viewport for first-person entities ──
        // Reparent RF_FIRST_PERSON / RF_DEPTHHACK entities into the
        // weapon SubViewport so they render in a separate pass and
        // composite on top of the main scene (correct self-occlusion).
        if (is_first_person || is_depthhack) {
            // Reparent FPS entities into the weapon SubViewport so they
            // render with a separate depth buffer and composite on top.
            if (weapon_root) {
                Node *cur_parent = mi->get_parent();
                if (cur_parent && cur_parent != weapon_root) {
                    cur_parent->remove_child(mi);
                    weapon_root->add_child(mi);
                }
            }
            // Periodic weapon diagnostic (first 30 frames) — now with mesh AABB
            static int fp_diag_count = 0;
            if (fp_diag_count < 30) {
                fp_diag_count++;
                Ref<Mesh> dbg_mesh = mi->get_mesh();
                int dbg_sc = dbg_mesh.is_valid() ? dbg_mesh->get_surface_count() : -1;
                const char *dbg_mod_name = Godot_Model_GetName(hModel);
                void *dbg_tiki = Godot_Model_GetTikiPtr(hModel);

                String aabb_str = "no_mesh";
                if (dbg_mesh.is_valid()) {
                    AABB aabb = dbg_mesh->get_aabb();
                    aabb_str = String("AABB=(") +
                        String::num(aabb.position.x, 2) + "," +
                        String::num(aabb.position.y, 2) + "," +
                        String::num(aabb.position.z, 2) + ")+(" +
                        String::num(aabb.size.x, 2) + "," +
                        String::num(aabb.size.y, 2) + "," +
                        String::num(aabb.size.z, 2) + ")";
                }

                Vector3 gpos = id_to_godot_position(origin[0], origin[1], origin[2]);
                Transform3D gt = mi->get_global_transform();

                UtilityFunctions::print(
                    String("[WEAPON-DIAG] ent#") + String::num_int64(i) +
                    String(" entNum=") + String::num_int64(entityNumber) +
                    String(" hModel=") + String::num_int64(hModel) +
                    String(" modType=") + String::num_int64(modType) +
                    String(" rfx=0x") + String::num_int64(renderfx, 16) +
                    String(" hasTiki=") + String::num_int64(dbg_tiki ? 1 : 0) +
                    String(" surfaces=") + String::num_int64(dbg_sc) +
                    String(" vis=") + String::num_int64(mi->is_visible() ? 1 : 0) +
                    String(" id_pos=(") + String::num(origin[0], 1) + "," +
                    String::num(origin[1], 1) + "," + String::num(origin[2], 1) + ")" +
                    String(" godot_pos=(") + String::num(gpos.x, 2) + "," +
                    String::num(gpos.y, 2) + "," + String::num(gpos.z, 2) + ")" +
                    String(" gt_pos=(") + String::num(gt.origin.x, 2) + "," +
                    String::num(gt.origin.y, 2) + "," + String::num(gt.origin.z, 2) + ")" +
                    String(" ") + aabb_str +
                    String(" skinned=") + (mi->get_mesh().is_valid() ? String("Y") : String("N")) +
                    String(" scale=") + String::num(scale, 3) +
                    String(" name=") + String(dbg_mod_name ? dbg_mod_name : "null"));
            }
        } else {
            // Non-weapon entity — ensure it is parented under entity_root
            // (it may have been in weapon_root from a previous frame)
            if (weapon_root) {
                Node *cur_parent = mi->get_parent();
                if (cur_parent && cur_parent != entity_root) {
                    cur_parent->remove_child(mi);
                    entity_root->add_child(mi);
                }
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
            // RF_LIGHTING_ORIGIN (0x0080): sample at lightingOrigin instead
            if (renderfx & 0x0080) {
                Godot_Renderer_GetEntityLightingOrigin(i, light_pos);
            }
            float lr, lg, lb;
            Godot_EntityLight_Combined(light_pos, 4, &lr, &lg, &lb);
            // Apply overbrightMult (1 << overbrightShift) = 2.0 on modern systems
            // See RB_GetEntityGridLighting in tr_light.c
            lr = lr * 2.0f; lg = lg * 2.0f; lb = lb * 2.0f;
            // Hue-preserving clamp to 1.0 (mirrors identityLightByte clamp)
            float maxc = lr; if (lg > maxc) maxc = lg; if (lb > maxc) maxc = lb;
            if (maxc > 1.0f) { lr /= maxc; lg /= maxc; lb /= maxc; }
            light_mul = Color(lr, lg, lb, 1.0f);
        }
#else
        {
            float ambient[3] = {1.0f, 1.0f, 1.0f};
            float directed[3] = {0.0f, 0.0f, 0.0f};
            float ldir[3] = {0.0f, 0.0f, 1.0f};
            float point[3] = { origin[0], origin[1], origin[2] };
            int lit = Godot_BSP_LightForPoint(point, ambient, directed, ldir);
            if (lit) {
                // Combine ambient + directed (same as R_GetLightingGridValue)
                float lr = clamp01(ambient[0] + directed[0] * 0.5f);
                float lg = clamp01(ambient[1] + directed[1] * 0.5f);
                float lb = clamp01(ambient[2] + directed[2] * 0.5f);
                // Apply overbrightMult = 2.0 (overbrightShift=1 on modern systems)
                // See RB_GetEntityGridLighting in tr_light.c
                lr *= 2.0f; lg *= 2.0f; lb *= 2.0f;
                // Hue-preserving clamp to 1.0
                float maxc = lr; if (lg > maxc) maxc = lg; if (lb > maxc) maxc = lb;
                if (maxc > 1.0f) { lr /= maxc; lg /= maxc; lb /= maxc; }
                light_mul = Color(lr, lg, lb, 1.0f);
            }
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
                            if (sp->stages[st].rgbGen == 4) { // STAGE_RGBGEN_ENTITY
                                apply_rgb_entity = true;
                            } else if (sp->stages[st].rgbGen == 5) { // STAGE_RGBGEN_ONE_MINUS_ENTITY
                                apply_rgb_one_minus_entity = true;
                            }
                            // MohaaStageAlphaGen: IDENTITY=0, VERTEX=1, WAVE=2, ENTITY=3, ONE_MINUS_ENTITY=4, PORTAL=5, CONST=6
                            if (sp->stages[st].alphaGen == 3) { // STAGE_ALPHAGEN_ENTITY
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
                    // Mirrors update_shader_animations() wave logic but applied per-entity
                    if (sp) {
                        if (sp->rgbgen_type == 2) { // wave
                            float wave_val = eval_wave(sp->rgbgen_wave_func,
                                sp->rgbgen_wave_base, sp->rgbgen_wave_amp,
                                sp->rgbgen_wave_phase, sp->rgbgen_wave_freq,
                                shader_anim_time);
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
                                shader_anim_time);
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

        mi->set_visible(true);
        dbg_reached_visible++;

        // One-shot per-entity log: verify brush entities are visible
        static std::unordered_set<int> logged_visible_ents;
        if (modType == 1 && logged_visible_ents.find(i) == logged_visible_ents.end()) {
            logged_visible_ents.insert(i);
            Transform3D t = mi->get_global_transform();
            bool in_tree = mi->is_inside_tree();
            bool visible = mi->is_visible_in_tree();
            Ref<Mesh> mref = mi->get_mesh();
            int msc = mref.is_valid() ? mref->get_surface_count() : -1;
            UtilityFunctions::print(String("[MoHAA-BRUSH-VIS] ent#") +
                String::num_int64(i) + " pos=(" +
                String::num(t.origin.x, 2) + "," +
                String::num(t.origin.y, 2) + "," +
                String::num(t.origin.z, 2) + ")" +
                " inTree=" + (in_tree ? "Y" : "N") +
                " visInTree=" + (visible ? "Y" : "N") +
                " surfaces=" + String::num_int64(msc) +
                " cullMargin=" + String::num(mi->get_extra_cull_margin(), 1));
        }

        entity_cache_keys[i] = key;
    }

    // One-shot per-map diagnostic: log entity type breakdown
    {
        // DEBUG: stderr trace of entity loop stage counters (unbuffered)
        static int dbg_loop_frame = 0;
        dbg_loop_frame++;
        if (dbg_loop_frame <= 3 && ent_count > 0) {
            fprintf(stderr, "[ENT-LOOP] frame=%d total=%d modtype=%d brush=%d visible=%d\n",
                    dbg_loop_frame, ent_count, dbg_reached_modtype, dbg_brush_path, dbg_reached_visible);
        }

        static int diag_frame_count = 0;
        static bool diag_done = false;
        if (!diag_done && ent_count > 0) {
            diag_frame_count++;
            // Wait a few frames for entities to fully populate
            if (diag_frame_count == 30) {
                diag_done = true;
                int n_brush = 0, n_tiki = 0, n_sprite = 0, n_beam = 0, n_other = 0;
                for (int d = 0; d < ent_count; d++) {
                    float dorg[3], dax[9], dsc = 1; int dhm = 0, den = 0, drf = 0;
                    unsigned char drg[4];
                    int drt = Godot_Renderer_GetEntity(d, dorg, dax, &dsc, &dhm, &den, drg, &drf);
                    if (drt == 0 /* RT_MODEL */ && dhm > 0) {
                        int dmt = Godot_Model_GetType(dhm);
                        if (dmt == 1) n_brush++;
                        else if (dmt == 2) n_tiki++;
                        else if (dmt == 3) n_sprite++;
                        else n_other++;
                    } else if (drt == 3) n_sprite++;
                    else if (drt == 4) n_beam++;
                    else n_other++;
                }
                UtilityFunctions::print(String("[MoHAA-DIAG] Entity breakdown: ") +
                    String::num_int64(ent_count) + " total, " +
                    String::num_int64(n_brush) + " brush, " +
                    String::num_int64(n_tiki) + " tiki, " +
                    String::num_int64(n_sprite) + " sprite, " +
                    String::num_int64(n_beam) + " beam, " +
                    String::num_int64(n_other) + " other");
            }
        }
    }

    // ── Renderfx diagnostic: log weapon entities once per session ──
    {
        static int rfx_diag_frames = 0;
        static bool rfx_logged_summary = false;
        rfx_diag_frames++;
        // Log a summary after 60 frames (about 1 second)
        if (!rfx_logged_summary && rfx_diag_frames > 60) {
            rfx_logged_summary = true;
            int n_fp = 0, n_dh = 0, n_tp = 0, n_shad = 0;
            for (int i = 0; i < ent_count; i++) {
                int rfx = 0;
                Godot_Renderer_GetEntity(i, nullptr, nullptr, nullptr,
                                         nullptr, nullptr, nullptr, &rfx);
                if (rfx & 0x02) n_fp++;
                if (rfx & 0x04) n_dh++;
                if (rfx & 0x01) n_tp++;
                if (rfx & 0x800) n_shad++;
            }
            UtilityFunctions::print(
                String("[MoHAA-RFX] Entity renderfx summary after ") +
                String::num_int64(rfx_diag_frames) + " frames: " +
                String::num_int64(ent_count) + " total, " +
                String::num_int64(n_fp) + " RF_FIRST_PERSON, " +
                String::num_int64(n_dh) + " RF_DEPTHHACK, " +
                String::num_int64(n_tp) + " RF_THIRD_PERSON, " +
                String::num_int64(n_shad) + " RF_SHADOW");
        }
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

    // Debug logging for poly activity (once per session + count changes)
    static int last_logged_count = -1;
    if (poly_count != last_logged_count) {
        if (poly_count > 0) {
            UtilityFunctions::print(String("[MoHAA] Poly count changed: ") +
                                    String::num_int64(poly_count) +
                                    " (particles/beams/decals active)");
        }
        last_logged_count = poly_count;
    }

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
        float positions[4 * 3];    // max 4 verts (quads)
        float texcoords[4 * 2];
        unsigned char colors[4 * 4];

        int numVerts = Godot_Renderer_GetPoly(i, &hShader,
                                               positions, texcoords,
                                               colors, 4);

        MeshInstance3D *mi = poly_meshes[i];

        if (numVerts < 3) {
            mi->set_visible(false);
            continue;
        }

        // Clamp to 4 max (quads is the common case)
        if (numVerts > 4) numVerts = 4;

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
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

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
                smat->set_render_priority(-1);  // polygonOffset equivalent

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
    smesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

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
        tmesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
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
//  Shadow blob projection for RF_SHADOW entities
// ──────────────────────────────────────────────

void MoHAARunner::update_shadow_blobs() {
    if (!game_world) return;

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
    shader_anim_time += delta;

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
    static std::unordered_map<int, bool> logged_serverback_ok;

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

    const bool is_serverback = (strstr(name, "serverback") != nullptr);
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

        const char *extensions[] = { "", ".tga", ".jpg", ".png", NULL };
        for (int ext_i = 0; extensions[ext_i]; ext_i++) {
            char path[256];
            snprintf(path, sizeof(path), "%s%s", qpath, extensions[ext_i]);

            void *buf = NULL;
            long len = Godot_VFS_ReadFile(path, &buf);
            if (len <= 0 || !buf) {
                if (buf) {
                    Godot_VFS_FreeFile(buf);
                }
                continue;
            }

            PackedByteArray pba;
            pba.resize(len);
            memcpy(pba.ptrw(), buf, len);
            Godot_VFS_FreeFile(buf);

            Ref<Image> img;
            img.instantiate();
            Error err;

            // Detect format by magic bytes
            const uint8_t *data = pba.ptr();
            if (len > 2 && data[0] == 0xFF && data[1] == 0xD8) {
                err = img->load_jpg_from_buffer(pba);
            } else if (len > 3 && data[0] == 0x89 && data[1] == 'P') {
                err = img->load_png_from_buffer(pba);
            } else {
                // Try TGA first, then JPEG
                err = img->load_tga_from_buffer(pba);
                if (err != OK) {
                    err = img->load_jpg_from_buffer(pba);
                }
            }

            if (err == OK && !img->is_empty()) {
                // Fix dead alpha: 32-bit TGA files sometimes have an unused
                // alpha channel filled with zeros.  The real renderer
                // ignores texture alpha for opaque shaders (GL_BLEND is
                // disabled via GLS_DEFAULT stateBits), but our Godot canvas
                // always blends.  Convert all-zero-alpha RGBA8 to RGB8 so
                // the texture renders fully opaque.
                if (img->get_format() == Image::FORMAT_RGBA8) {
                    PackedByteArray imgdata = img->get_data();
                    int pixel_count = img->get_width() * img->get_height();
                    const uint8_t *pix = imgdata.ptr();
                    bool all_zero_alpha = true;
                    for (int p = 0; p < pixel_count && all_zero_alpha; p++) {
                        if (pix[p * 4 + 3] > 0) {
                            all_zero_alpha = false;
                        }
                    }
                    if (all_zero_alpha) {
                        img->convert(Image::FORMAT_RGB8);
                    }
                }
                if (out_has_alpha) {
                    *out_has_alpha = (img->detect_alpha() != Image::ALPHA_NONE);
                }
                // Diagnostic: log texture format/alpha for levelshot shaders
                if (name && (strstr(name, "mohdm") || strstr(name, "levelshot"))) {
                    UtilityFunctions::print(
                        String("[MoHAA][TEX-DIAG] shader='") + String(name) +
                        String("' path='") + String(path) +
                        String("' format=") + String::num_int64(img->get_format()) +
                        String(" size=") + String::num_int64(img->get_width()) +
                        String("x") + String::num_int64(img->get_height()) +
                        String(" detect_alpha=") + String::num_int64(img->detect_alpha()) +
                        String(" has_alpha=") + String(*out_has_alpha ? "true" : "false"));
                }
                img->generate_mipmaps();
                Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
                if (is_serverback && logged_serverback_ok.find(shader_handle) == logged_serverback_ok.end()) {
                    logged_serverback_ok[shader_handle] = true;
                    UtilityFunctions::print(String("[MoHAA][2D] serverback loaded #") +
                                            String::num_int64(shader_handle) +
                                            String(" shader='") + String(name) +
                                            String("' path='") + String(path) +
                                            String("' size=") +
                                            String::num_int64(img->get_width()) + String("x") +
                                            String::num_int64(img->get_height()));
                }
                return tex;
            }

            // Always log decode errors globally, unconditionally!
            static std::unordered_set<std::string> logged_errors;
            if (logged_errors.find(path) == logged_errors.end()) {
                UtilityFunctions::print(String("[MoHAA][TEX-ERR] Decode failed for shader='") +
                                        String(name) + String("' path='") + String(path) +
                                        String("' error_code=") + String::num_int64(err) +
                                        String(" size=") + String::num_int64(len) + String(" bytes."));
                logged_errors.insert(path);
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
        UtilityFunctions::print(String("[MoHAA][2D] Invalidate shader tex #") + String::num_int64(shader_handle) + 
            String(" (was ") + String(it_name != s_shader_texture_loaded_names.end() ? it_name->second.c_str() : "unknown") + String(", now ") + String(name) + String(")"));
        shader_textures.erase(it);
        shader_texture_has_alpha.erase(shader_handle);
        s_shader_texture_loaded_names.erase(shader_handle);
    }
    
    UtilityFunctions::print(String("[MoHAA][2D] Reloading shader tex #") + String::num_int64(shader_handle) + 
        String(" for name ") + String(name) + String(" (raw: ") + String(raw_name) + String(")"));

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
                if (texture_paths[i] && strstr(texture_paths[i], "sprites/white"))
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
            UtilityFunctions::print(String("[MoHAA][2D] FALLING BACK to $whiteimage for ") + String(name ? name : "NULL"));
        }
        Ref<ImageTexture> fallback_tex = get_shader_texture(Godot_Renderer_RegisterShader("$whiteimage"));
        if (!fallback_tex.is_null()) {
            shader_textures[shader_handle] = fallback_tex;
            shader_texture_has_alpha[shader_handle] = false;
            s_shader_texture_loaded_names[shader_handle] = name ? name : "";
        }
        return fallback_tex;
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
// Calculate the transformation from engine's 640×480 virtual space to actual viewport.
// Uses non-uniform (stretch-to-fill) scaling to match OPM's SCR_AdjustFrom640() behaviour.
// Used by both update_2d_overlay() for rendering and _unhandled_input() for mouse mapping.
void MoHAARunner::update_ui_transform() {
    // Get engine's virtual resolution (always 640×480 in MOHAA)
    Godot_Renderer_GetVidSize(&ui_vid_w, &ui_vid_h);
    if (ui_vid_w < 1) ui_vid_w = 640;
    if (ui_vid_h < 1) ui_vid_h = 480;
    
    // Get actual viewport size
    Vector2 viewport_size(0, 0);
    if (hud_control) {
        viewport_size = hud_control->get_size();
    }
    
    // Fallback chain if Control hasn't been laid out yet
    if (viewport_size.x < 1.0f || viewport_size.y < 1.0f) {
        Rect2 visible_rect = get_viewport()->get_visible_rect();
        viewport_size = visible_rect.size;
        
        if (viewport_size.x < 1.0f || viewport_size.y < 1.0f) {
            Vector2i win = DisplayServer::get_singleton()->window_get_size();
            viewport_size = Vector2(win);
        }
    }
    
    // Non-uniform scaling — stretch 640×480 to fill the entire viewport.
    // This matches OPM behaviour: SCR_AdjustFrom640() scales X by
    // vidWidth/640 and Y by vidHeight/480 independently, so the HUD
    // fills the full screen regardless of aspect ratio.
    ui_scale_x = viewport_size.x / (float)ui_vid_w;
    ui_scale_y = viewport_size.y / (float)ui_vid_h;
    ui_offset_x = 0.0f;
    ui_offset_y = 0.0f;
}

void MoHAARunner::update_2d_overlay() {
    static std::unordered_set<int> logged_serverback_draw;
    static std::unordered_set<int> logged_serverback_geometry;
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
            "    // Pre-composite two-stage GL pipeline:\n"
            "    // Stage 0: $whiteimage (opaque white fill)\n"
            "    // Stage 1: texture with GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA\n"
            "    // result = tex.rgb * (1.0 - tex.a) + white * tex.a\n"
            "    vec3 composited = tex.rgb * (1.0 - tex.a) + vec3(1.0) * tex.a;\n"
            "    COLOR = vec4(composited * COLOR.rgb, COLOR.a);\n"
            "}\n"
        );
        alpha_inv_material.instantiate();
        alpha_inv_material->set_shader(alpha_inv_shader);
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
                rs->canvas_item_set_material(seg.item, RID());
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

    // Determine whether we should allow fullscreen background fills.
    // When a UI menu is active (main menu, options, console, loading screen)
    // the engine draws UI_ClearBackground() as a fullscreen black box, plus
    // menu background textures.  We must let those through so the menu is
    // visible.  Only suppress large fills when we're in-game (CA_ACTIVE=8)
    // with no overlay active and the world map is loaded — that's when fills
    // would harmfully cover the 3D view.
    bool overlay_on = (Godot_Client_IsMenuUp() != 0)
                    || (Godot_Client_IsAnyOverlayActive() != 0);
    bool in_active_game = (Godot_Client_GetState() == 8); // CA_ACTIVE
    bool world_loaded = Godot_Renderer_IsWorldMapLoaded() != 0;
    bool allow_fullscreen_fills = overlay_on || !in_active_game || !world_loaded;

    bool scissor_enabled = false;
    bool saw_textured_draw = false;
    static bool logged_late_clear_skip = false;

    Rect2 scissor_rect;

    // ── Temporary diagnostic: dump all 2D commands when scoreboard first appears ──
    {
        static bool sb_dump_done = false;
        bool sb_vis = (Godot_SB_IsVisible() != 0);
        if (sb_vis && !sb_dump_done) {
            sb_dump_done = true;
            UtilityFunctions::print(String("[SB-DIAG] Scoreboard visible, dumping ") +
                                    String::num(cmd_count) + String(" 2D commands:"));
            UtilityFunctions::print(String("[SB-DIAG] overlay_on=") + String::num((int)overlay_on) +
                                    String(" allow_fills=") + String::num((int)allow_fullscreen_fills) +
                                    String(" vid_area=") + String::num(vid_area));
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

        // Menu/UI path: a late fullscreen black box can appear after textured
        // quads and hide the backdrop. In GL this is constrained by state;
        // in our flattened 2D stream we suppress that specific late clear.
        if (overlay_on && saw_textured_draw && type == 1 &&
            w * h > vid_area * 0.5f && color[3] > 0.95f &&
            color[0] < 0.05f && color[1] < 0.05f && color[2] < 0.05f) {
            if (!logged_late_clear_skip) {
                logged_late_clear_skip = true;
                UtilityFunctions::print("[MoHAA][2D] Skipping late fullscreen black clear after textured UI draws");
            }
            continue;
        }

        // Skip fullscreen opaque fills when in-game with no overlay.
        // These screen clears would cover the 3D view.  But when UI menus
        // are active, we need them for menu backgrounds.
        if (!allow_fullscreen_fills && type == 1 && w * h > vid_area * 0.5f && color[3] > 0.9f) {
            continue;
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
                // The real GL renderer processes per-stage alphaGen (e.g.
                // "alphagen constant 0.0") which overrides the vertex alpha
                // set by SetColor.  Our 2D overlay must replicate this.
                Color draw_col = col;
                const char *sname = Godot_Renderer_GetShaderName(shader);

                if (sname && sname[0]) {
                    const GodotShaderProps *sp = Godot_ShaderProps_Find(sname);
                    if (sp && sp->stage_count > 0) {
                        // Find first non-lightmap stage (same as get_shader_texture)
                        for (int st = 0; st < sp->stage_count; st++) {
                            if (!sp->stages[st].active) continue;
                            if (sp->stages[st].isLightmap) continue;
                            const MohaaShaderStage *stg = &sp->stages[st];

                            // rgbGen handling: identity/const override vertex colour.
                            if (stg->rgbGen == STAGE_RGBGEN_IDENTITY ||
                                stg->rgbGen == STAGE_RGBGEN_IDENTITY_LIGHTING) {
                                draw_col.r = 1.0f;
                                draw_col.g = 1.0f;
                                draw_col.b = 1.0f;
                            } else if (stg->rgbGen == STAGE_RGBGEN_CONST) {
                                draw_col.r = stg->rgbConst[0];
                                draw_col.g = stg->rgbConst[1];
                                draw_col.b = stg->rgbConst[2];
                            }

                            // alphaGen handling: identity/const override vertex alpha.
                            if (stg->alphaGen == STAGE_ALPHAGEN_IDENTITY) {
                                draw_col.a = 1.0f;
                            } else if (stg->alphaGen == STAGE_ALPHAGEN_CONST) {
                                draw_col.a = stg->alphaConst;
                            }
                            break;  // Only process first non-lightmap stage
                        }
                    }
                }

                if (sname && strstr(sname, "serverback") &&
                    logged_serverback_draw.find(shader) == logged_serverback_draw.end()) {
                    logged_serverback_draw.insert(shader);
                    UtilityFunctions::print(String("[MoHAA][2D] serverback draw colour src=") +
                                            String::num(col.r, 3) + String(",") +
                                            String::num(col.g, 3) + String(",") +
                                            String::num(col.b, 3) + String(",") +
                                            String::num(col.a, 3) + String(" -> final=") +
                                            String::num(draw_col.r, 3) + String(",") +
                                            String::num(draw_col.g, 3) + String(",") +
                                            String::num(draw_col.b, 3) + String(",") +
                                            String::num(draw_col.a, 3) +
                                            String(" shader='") + String(sname) + String("'"));
                }

                if (sname && strstr(sname, "serverback") &&
                    logged_serverback_geometry.find(shader) == logged_serverback_geometry.end()) {
                    logged_serverback_geometry.insert(shader);

                    int fullscreen_opaque_after = 0;
                    String first_opaque_info;
                    for (int j = i + 1; j < cmd_count; j++) {
                        int t2 = 0, sh2 = 0;
                        float x2 = 0, y2 = 0, w2 = 0, h2 = 0, ss1 = 0, tt1 = 0, ss2 = 0, tt2 = 0, c2[4] = {0, 0, 0, 0};
                        if (!Godot_Renderer_Get2DCmd(j, &t2, &x2, &y2, &w2, &h2, &ss1, &tt1, &ss2, &tt2, c2, &sh2)) {
                            continue;
                        }
                        if (t2 == 1 && (w2 * h2) > (vid_area * 0.5f) && c2[3] > 0.9f) {
                            fullscreen_opaque_after++;
                            if (first_opaque_info.is_empty()) {
                                first_opaque_info = String(" idx=") + String::num_int64(j) +
                                                    String(" rect=") + String::num(x2, 1) + String(",") +
                                                    String::num(y2, 1) + String(" ") +
                                                    String::num(w2, 1) + String("x") +
                                                    String::num(h2, 1) + String(" rgba=") +
                                                    String::num(c2[0], 3) + String(",") +
                                                    String::num(c2[1], 3) + String(",") +
                                                    String::num(c2[2], 3) + String(",") +
                                                    String::num(c2[3], 3);
                            }
                        }
                    }

                    UtilityFunctions::print(String("[MoHAA][2D] serverback draw geometry cmd=") +
                                            String::num_int64(i) + String(" rect=") +
                                            String::num(rect.position.x, 1) + String(",") +
                                            String::num(rect.position.y, 1) + String(" ") +
                                            String::num(rect.size.x, 1) + String("x") +
                                            String::num(rect.size.y, 1) + String(" uv=") +
                                            String::num(s1, 3) + String(",") +
                                            String::num(t1, 3) + String(" -> ") +
                                            String::num(s2, 3) + String(",") +
                                            String::num(t2, 3) + String(" post_fullscreen_opaque=") +
                                            String::num_int64(fullscreen_opaque_after) +
                                            first_opaque_info);
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
                    const GodotShaderProps *sp2 = Godot_ShaderProps_Find(sname);
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
                            /* In Godot, UI textures often contain soft alpha halos (e.g. hover states).
                             * Forcing BLEND_OPAQUE causes these halos to render as solid black boxes.
                             * We unconditionally use BLEND_MIX here so texture transparency resolves correctly. */
                            draw_blend = BLEND_MIX;
                            /* Multi-stage shader: check the actual texture stage
                             * for a custom blendFunc that overrides the opaque
                             * default (e.g. GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA). */
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
                RID target_ci = get_segment_ci(draw_blend);

                // Detect tiling: if the source rect extends beyond the
                // texture dimensions, the engine expects GL_REPEAT wrapping
                // (e.g. statbar_tileshader for ammo bullet icons).
                // Godot's canvas_item_add_texture_rect_region clamps source rects
                // to image bounds, so tiled draws need special handling.
                bool needs_tiling = (src.position.x + src.size.x > tw + 0.5f) ||
                                    (src.position.y + src.size.y > th + 0.5f);

                // Debug: log every 2D draw with UVs that suggest tiling
                {
                    static int tile_log_count = 0;
                    if (tile_log_count < 50 && (s2 > 1.01f || t2 > 1.01f || s1 < -0.01f || t1 < -0.01f)) {
                        tile_log_count++;
                        UtilityFunctions::print(String("[MoHAA][TILE-DBG] shader=") + String(sname ? sname : "?") +
                            String(" uv=") + String::num(s1,3) + String(",") + String::num(t1,3) +
                            String("->") + String::num(s2,3) + String(",") + String::num(t2,3) +
                            String(" texWH=") + String::num(tw,0) + String("x") + String::num(th,0) +
                            String(" src=") + String::num(src.position.x,1) + String(",") + String::num(src.position.y,1) +
                            String(" ") + String::num(src.size.x,1) + String("x") + String::num(src.size.y,1) +
                            String(" draw=") + String::num(draw_rect.position.x,1) + String(",") + String::num(draw_rect.position.y,1) +
                            String(" ") + String::num(draw_rect.size.x,1) + String("x") + String::num(draw_rect.size.y,1) +
                            String(" needs_tiling=") + String(needs_tiling ? "YES" : "NO"));
                    }
                }

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
                    // Targeted diagnostic for map preview shader
                    if (sname && strstr(sname, "mohdm")) {
                        static int mohdm_log_count = 0;
                        if (mohdm_log_count < 5) {
                            mohdm_log_count++;
                            UtilityFunctions::print(String("[MAP-PREVIEW] shader='") + String(sname) +
                                String("' raw=(") + String::num(x,1) + String(",") + String::num(y,1) +
                                String(",") + String::num(w,1) + String(",") + String::num(h,1) +
                                String(") rect=(") + String::num(rect.position.x,1) + String(",") +
                                String::num(rect.position.y,1) + String(",") + String::num(rect.size.x,1) +
                                String(",") + String::num(rect.size.y,1) +
                                String(") draw=(") + String::num(draw_rect.position.x,1) + String(",") +
                                String::num(draw_rect.position.y,1) + String(",") + String::num(draw_rect.size.x,1) +
                                String(",") + String::num(draw_rect.size.y,1) +
                                String(") src=(") + String::num(src.position.x,1) + String(",") +
                                String::num(src.position.y,1) + String(",") + String::num(src.size.x,1) +
                                String(",") + String::num(src.size.y,1) +
                                String(") scissor=") + String(scissor_enabled ? "ON" : "OFF") +
                                String(" scis_rect=(") + String::num(scissor_rect.position.x,1) + String(",") +
                                String::num(scissor_rect.position.y,1) + String(",") + String::num(scissor_rect.size.x,1) +
                                String(",") + String::num(scissor_rect.size.y,1) +
                                String(") col=(") + String::num(draw_col.r,3) + String(",") +
                                String::num(draw_col.g,3) + String(",") + String::num(draw_col.b,3) +
                                String(",") + String::num(draw_col.a,3) +
                                String(") blend=") + String::num(draw_blend) +
                                String(" tex=") + String::num(tw,0) + String("x") + String::num(th,0) +
                                String(" uv=") + String::num(s1,3) + String(",") + String::num(t1,3) +
                                String("->") + String::num(s2,3) + String(",") + String::num(t2,3) +
                                String(" scale=") + String::num(ui_scale_x,4) + String(",") + String::num(ui_scale_y,4));
                        }
                    }
                    // Flipped UVs (s1>s2 or t1>t2) need explicit UV
                    // mapping — canvas_item_add_texture_rect_region does
                    // not flip with negative src dimensions.  Use a
                    // textured polygon with per-vertex UVs for full control.
                    bool need_flip = (src.size.x < 0.0f || src.size.y < 0.0f);
                    if (!need_flip) {
                        rs->canvas_item_add_texture_rect_region(target_ci, draw_rect, tex_rid, src, draw_col, false, false);
                    } else {
                        // Build quad as 2-triangle polygon with explicit UVs.
                        // The raw s1/t1/s2/t2 already encode the flip direction.
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

                        fuv[0] = Vector2(s1, t1);
                        fuv[1] = Vector2(s2, t1);
                        fuv[2] = Vector2(s2, t2);
                        fuv[3] = Vector2(s1, t2);

                        cols[0] = cols[1] = cols[2] = cols[3] = draw_col;

                        idx[0] = 0; idx[1] = 1; idx[2] = 2;
                        idx[3] = 0; idx[4] = 2; idx[5] = 3;

                        rs->canvas_item_add_triangle_array(target_ci, idx, pts, cols, fuv, PackedInt32Array(), PackedFloat32Array(), tex_rid, -1);
                    }
                }
                saw_textured_draw = true;
            }
            // If texture not loaded, skip — don't draw opaque coloured rect fallback
        } else if (type == 0) {
            // StretchPic with no shader — draw unless it's a large opaque fill
            // that would cover the 3D view when in-game with no overlay.
            if (allow_fullscreen_fills || w * h < vid_area * 0.5f || color[3] < 0.9f) {
                RID noshader_ci = get_segment_ci(BLEND_MIX);
                rs->canvas_item_add_rect(noshader_ci, draw_rect, col);
            }
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
                    if (sname && sname[0]) {
                        const GodotShaderProps *sp = Godot_ShaderProps_Find(sname);
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
                        const GodotShaderProps *sp2 = Godot_ShaderProps_Find(sname);
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
                                /* UI hover fix: use BLEND_MIX to properly resolve texture alpha */
                                draw_blend = BLEND_MIX;
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
                    RID target_ci = get_segment_ci(draw_blend);

                    RID tex_rid = tex->get_rid();
                    rs->canvas_item_add_polygon(target_ci, points, colors_arr, uvs, tex_rid);
                    saw_textured_draw = true;
                }
            }
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
            void *boneCache = Godot_Skel_PrepareBones(
                tiki, 1023 /* entityNumber from CL_Draw3DModel */,
                (const void *)frameInfoBuf, boneTagBuf,
                (const float *)boneQuatBuf,
                actionWeight, &boneCount);

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
                                boneCache, boneCount, positions, normals)) {
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
                        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

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

    // -- 4. Sound fade (Phase 50) --
    if (Godot_Sound_GetFadeActive()) {
        float fade_time = Godot_Sound_GetFadeTime();
        if (fade_time > 0.0f) {
            sound_fade_duration = fade_time;
            sound_fade_elapsed = 0.0f;
            sound_fading = true;
        }
        Godot_Sound_ClearFade();
    }

    // -- 5. Log sound stats once --
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
#ifdef __EMSCRIPTEN__
    {
        Godot_Client_SyncGuiMouseToOverlayState();
        bool pre_overlay = Godot_Client_IsAnyOverlayActive() != 0;
        // Also include in_guimouse in case it was set by a non-keycatcher path
        if (!pre_overlay) pre_overlay = (Godot_Client_GetGuiMouse() != 0);
        // Fall back to prev-frame value for the very first overlay frame
        if (!pre_overlay) pre_overlay = overlay_prev_frame;
        poll_mouse_input_web(pre_overlay);
    }
#endif

    Com_Frame();
    godot_jmpbuf_valid = false;

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
        bool should_capture = !(overlay_active || engine_wants_gui);
        bool allow_capture = true;
#ifdef __EMSCRIPTEN__
        // Browser pointer lock requires a user gesture. Automatic capture
        // from the per-frame engine state can throw and break frame flow.
        allow_capture = false;
#endif
        bool applied_capture = should_capture && allow_capture;
        if (applied_capture != mouse_captured) {
            mouse_captured = applied_capture;
            Input *input = Input::get_singleton();
            if (input) {
                if (applied_capture) {
                    input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
                } else {
                    input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
                }
            }
            Godot_ResetMousePosition();
        }
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

            DisplayServer *ds = DisplayServer::get_singleton();
            if (ds) {
                if (fs) {
                    ds->window_set_mode(DisplayServer::WINDOW_MODE_FULLSCREEN);
                } else {
                    ds->window_set_mode(DisplayServer::WINDOW_MODE_WINDOWED);
                    if (vw > 0 && vh > 0) {
                        ds->window_set_size(Vector2i(vw, vh));
                    }
                }
                UtilityFunctions::print(String("[MoHAA] vid_restart: fullscreen=") +
                    String::num_int64(fs) + String(" size=") +
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

    // ── Update entity debug meshes from captured render data (Phase 7e) ──
    update_entities();
    update_dlights();
    update_polys();
    update_swipe_effects();     // Phase 24: swipe/melee trails
    update_terrain_marks();     // Phase 25: terrain mark decals
    update_shadow_blobs();      // Shadow blob projection under RF_SHADOW entities
    update_shader_animations(delta);  // Phase 36: tcMod scroll/rotate

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
    bool requested_capture = p_captured;
#ifdef __EMSCRIPTEN__
    // On web, only capture after explicit in-canvas user gesture; avoid
    // forcing pointer lock from scripts because browsers may reject it.
    requested_capture = false;
#endif
    mouse_captured = requested_capture;
    Input *input = Input::get_singleton();
    if (!input) return;

    if (requested_capture) {
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
    static int s_poll_frame = 0;
    s_poll_frame++;

    Input *input = Input::get_singleton();
    Viewport *vp = get_viewport();
    if (!input || !vp) {
        if (s_poll_frame % 120 == 1)
            fprintf(stderr, "[MoHAA-mouse] poll: no input/viewport\n");
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

    // ALWAYS update position when in_guimouse is true (reliable check
    // that bypasses any stale overlay_active parameter).
    int gui_mouse_active = Godot_Client_GetGuiMouse();
    bool should_poll = overlay_active || (gui_mouse_active != 0);

    if (s_poll_frame % 120 == 1) {
        int kc = Godot_Client_GetKeyCatchers();
        update_ui_transform();
        fprintf(stderr,
            "[MoHAA-mouse] frame=%d overlay=%d gui_mouse=%d keyCatchers=0x%x "
            "pos=(%.0f,%.0f) scale=(%.2f,%.2f) vid=%dx%d\n",
            s_poll_frame, (int)overlay_active, gui_mouse_active, kc,
            pos.x, pos.y, ui_scale_x, ui_scale_y, ui_vid_w, ui_vid_h);
    }

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

        if (s_poll_frame % 120 == 1)
            fprintf(stderr, "[MoHAA-mouse] -> engine cursor (%d, %d)\n", ex, ey);

        const int tracked_buttons[] = {1, 2, 3, 8, 9};
        for (int i = 0; i < 5; i++) {
            int b = tracked_buttons[i];
            bool now_pressed = input->is_mouse_button_pressed((MouseButton)b);
            bool prev_pressed = mouse_poll_prev_buttons[b];
            if (now_pressed != prev_pressed) {
                Godot_InjectMouseButton(b, now_pressed ? 1 : 0);
                mouse_poll_prev_buttons[b] = now_pressed;
                fprintf(stderr, "[MoHAA-mouse] button %d %s\n",
                    b, now_pressed ? "DOWN" : "UP");
            }
        }
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
    // This is a Godot-side setting, not an engine cvar
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

    // ── Mouse motion ──
    InputEventMouseMotion *motion_event = Object::cast_to<InputEventMouseMotion>(p_event.ptr());
    if (motion_event) {
#ifdef __EMSCRIPTEN__
        return;
#endif
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

#ifdef __EMSCRIPTEN__
        if (godot_button >= 4 && godot_button <= 5 && pressed) {
            Godot_InjectMouseButton(godot_button, 1);
            Godot_InjectMouseButton(godot_button, 0);
        }
        return;
#endif

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
            static const int GODOT_KEY_BACKTICK = 96;  /* KEY_QUOTELEFT  */
            static const int GODOT_KEY_TILDE    = 126; /* KEY_ASCIITILDE */
            bool is_console_key = (godot_key == GODOT_KEY_BACKTICK
                                   || godot_key == GODOT_KEY_TILDE);

            // Always inject key events into the engine's event queue.
            // The engine's CL_KeyEvent (cl_keys.cpp) checks keyCatchers
            // internally and routes to UI_KeyEvent / Console_Key / game
            // bindings as appropriate.  No need for a parallel routing layer.
            if (!echo) {
                Godot_InjectKeyEvent(godot_key, pressed ? 1 : 0);
            }
            if ((pressed || echo) && !is_console_key) {
                int64_t unicode = key_event->get_unicode();
                if (unicode > 0) {
                    Godot_InjectCharEvent((int)unicode);
                }
            }
        }

        return;
    }

    // Mouse events are handled in _input() so UI hover/click still works even
    // when controls consume input before the unhandled phase.
}
