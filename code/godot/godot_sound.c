/*
 * godot_sound.c — Godot sound backend for the OpenMoHAA GDExtension.
 *
 * Replaces the DMA/OpenAL/Miles sound backends with an event-capture
 * system.  Each frame the engine calls S_StartSound, S_AddLoopingSound,
 * etc.  We record those events into fixed-size queues that are drained
 * by MoHAARunner.cpp on the Godot side, which creates AudioStreamPlayer
 * and AudioStreamPlayer3D nodes.
 *
 * Sound data lives in pk3 archives and is loaded lazily on the C++ side
 * via the engine's FS_ReadFile.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/snd_local_new.h"

#include <string.h>

/* ===================================================================
 *  Sound registry — maps sfxHandle_t -> file path
 * ================================================================ */

#define MAX_GR_SFX  2048

typedef struct {
    char  name[MAX_QPATH];   /* e.g. "sound/weapons/m1_fire.wav" */
    int   handle;            /* sfxHandle_t returned to the engine */
} gr_sfx_entry_t;

static gr_sfx_entry_t gr_sfx[MAX_GR_SFX];
static int            gr_sfx_count = 0;

/* ===================================================================
 *  Sound event queue — drained once per frame by the C++ side
 * ================================================================ */

#define MAX_SOUND_EVENTS 128

/* Event types */
#define GR_SND_START        0   /* 3D positional sound */
#define GR_SND_START_LOCAL  1   /* 2D UI / announcer sound */
#define GR_SND_STOP         2   /* stop sound on entity+channel */
#define GR_SND_STOP_ALL     3   /* stop everything */

typedef struct {
    int   type;
    float origin[3];
    int   entnum;
    int   channel;
    int   sfxHandle;
    float volume;
    float minDist;
    float maxDist;
    float pitch;
    int   streamed;
    char  name[MAX_QPATH];  /* for StartLocalSound (name-based) */
} gr_sound_event_t;

static gr_sound_event_t gr_sound_events[MAX_SOUND_EVENTS];
static int              gr_sound_event_count = 0;

/* ===================================================================
 *  Looping-sound buffer — rebuilt every frame
 *  (S_ClearLoopingSounds then S_AddLoopingSound * N)
 * ================================================================ */

#define MAX_LOOP_SOUNDS 64

typedef struct {
    float origin[3];
    float velocity[3];
    int   sfxHandle;
    float volume;
    float minDist;
    float maxDist;
    float pitch;
    int   flags;
    int   entnum;           /* entity number for position tracking (-1 = none) */
} gr_loop_sound_t;

static gr_loop_sound_t gr_loop_sounds[MAX_LOOP_SOUNDS];
static int             gr_loop_sound_count = 0;

/* ===================================================================
 *  Listener state — updated every frame via S_Respatialize
 * ================================================================ */

static float gr_listener_origin[3]  = {0, 0, 0};
static float gr_listener_axis[9]    = {1,0,0, 0,1,0, 0,0,1};
static int   gr_listener_entnum     = 0;

/* ===================================================================
 *  Music state
 * ================================================================ */

#define GR_MUSIC_NONE       0
#define GR_MUSIC_PLAY       1
#define GR_MUSIC_STOP       2
#define GR_MUSIC_VOLUME     3

typedef struct {
    int    action;               /* GR_MUSIC_* */
    char   name[MAX_QPATH];     /* soundtrack / song name */
    float  volume;
    float  fadeTime;
    int    currentMood;
    int    fallbackMood;
} gr_music_state_t;

static gr_music_state_t gr_music = {0};

/* ===================================================================
 *  Entity position tracking  (Phase 49)
 *  S_UpdateEntity stores per-entity position so MoHAARunner can
 *  update AudioStreamPlayer3D positions for sounds attached to
 *  moving entities.
 * ================================================================ */

#define MAX_SOUND_ENTITIES 1024

typedef struct {
    float origin[3];
    float velocity[3];
    int   use_listener;
    int   valid;
} gr_snd_entity_t;

static gr_snd_entity_t gr_snd_entities[MAX_SOUND_ENTITIES];

/* Accessor for MoHAARunner.cpp */
int Godot_Sound_GetEntityPosition(int entnum, float *origin, float *velocity)
{
    if (entnum < 0 || entnum >= MAX_SOUND_ENTITIES) return 0;
    if (!gr_snd_entities[entnum].valid) return 0;
    if (origin) {
        origin[0] = gr_snd_entities[entnum].origin[0];
        origin[1] = gr_snd_entities[entnum].origin[1];
        origin[2] = gr_snd_entities[entnum].origin[2];
    }
    if (velocity) {
        velocity[0] = gr_snd_entities[entnum].velocity[0];
        velocity[1] = gr_snd_entities[entnum].velocity[1];
        velocity[2] = gr_snd_entities[entnum].velocity[2];
    }
    return 1;
}

/* ===================================================================
 *  Global fade state (Phase 50)
 * ================================================================ */

static float gr_sound_fade_time    = 0.0f;   /* seconds to fade over */
static float gr_sound_fade_target  = 1.0f;   /* target volume (0..1) */
static int   gr_sound_fade_active  = 0;

float Godot_Sound_GetFadeTime(void)   { return gr_sound_fade_time; }
float Godot_Sound_GetFadeTarget(void) { return gr_sound_fade_target; }
int   Godot_Sound_GetFadeActive(void) { return gr_sound_fade_active; }
void  Godot_Sound_ClearFade(void)     { gr_sound_fade_active = 0; }

/* ===================================================================
 *  Reverb state (Phase 50)
 * ================================================================ */

static int   gr_reverb_type  = 0;
static float gr_reverb_level = 0.0f;

int   Godot_Sound_GetReverbType(void)  { return gr_reverb_type; }
float Godot_Sound_GetReverbLevel(void) { return gr_reverb_level; }

/* ===================================================================
 *  IsSoundPlaying tracking (Phase 49)
 *  We track recently-started sounds per channel so S_IsSoundPlaying
 *  can return meaningful results.  The Godot side clears entries
 *  when playback finishes.
 * ================================================================ */

#define MAX_PLAYING_TRACKS 64

typedef struct {
    int   channel;
    int   sfxHandle;
    char  name[MAX_QPATH];
    float startTime;       /* engine time in seconds (from cls.realtime) */
} gr_playing_track_t;

static gr_playing_track_t gr_playing[MAX_PLAYING_TRACKS];
static int                gr_playing_count = 0;

int Godot_Sound_GetPlayingCount(void) { return gr_playing_count; }

int Godot_Sound_GetPlaying(int index, int *channel, int *sfxHandle,
                           char *name, int nameLen)
{
    if (index < 0 || index >= gr_playing_count) return 0;
    const gr_playing_track_t *t = &gr_playing[index];
    if (channel)   *channel   = t->channel;
    if (sfxHandle) *sfxHandle = t->sfxHandle;
    if (name && nameLen > 0) {
        strncpy(name, t->name, nameLen - 1);
        name[nameLen - 1] = '\0';
    }
    return 1;
}

void Godot_Sound_MarkStopped(int channel)
{
    for (int i = 0; i < gr_playing_count; i++) {
        if (gr_playing[i].channel == channel) {
            /* Swap-remove */
            gr_playing[i] = gr_playing[--gr_playing_count];
            return;
        }
    }
}

static void track_playing(int channel, int sfxHandle, const char *name)
{
    /* Update existing entry for this channel */
    for (int i = 0; i < gr_playing_count; i++) {
        if (gr_playing[i].channel == channel) {
            gr_playing[i].sfxHandle = sfxHandle;
            if (name) strncpy(gr_playing[i].name, name, MAX_QPATH - 1);
            return;
        }
    }
    if (gr_playing_count >= MAX_PLAYING_TRACKS) return;
    gr_playing_track_t *t = &gr_playing[gr_playing_count++];
    t->channel   = channel;
    t->sfxHandle = sfxHandle;
    t->startTime = 0;  /* not currently used */
    if (name) {
        strncpy(t->name, name, MAX_QPATH - 1);
        t->name[MAX_QPATH - 1] = '\0';
    } else {
        t->name[0] = '\0';
    }
}

static void untrack_playing(int channel)
{
    for (int i = 0; i < gr_playing_count; i++) {
        if (gr_playing[i].channel == channel) {
            gr_playing[i] = gr_playing[--gr_playing_count];
            return;
        }
    }
}

/* ===================================================================
 *  Music mood tracking (Phase 50)
 * ================================================================ */

int Godot_Sound_GetMusicMood(int *current, int *fallback)
{
    if (current)  *current  = gr_music.currentMood;
    if (fallback) *fallback = gr_music.fallbackMood;
    return 1;
}

/* ===================================================================
 *  C accessor functions — called by MoHAARunner.cpp (extern "C")
 * ================================================================ */

int Godot_Sound_GetSfxCount(void)
{
    return gr_sfx_count;
}

const char *Godot_Sound_GetSfxName(int index)
{
    if (index < 0 || index >= gr_sfx_count) return "";
    return gr_sfx[index].name;
}

int Godot_Sound_GetSfxHandle(int index)
{
    if (index < 0 || index >= gr_sfx_count) return 0;
    return gr_sfx[index].handle;
}

/* Find the registry index for a given sfxHandle_t */
int Godot_Sound_FindSfxIndex(int handle)
{
    for (int i = 0; i < gr_sfx_count; i++) {
        if (gr_sfx[i].handle == handle) return i;
    }
    return -1;
}

/* --- Event queue accessors --- */

int Godot_Sound_GetEventCount(void)
{
    return gr_sound_event_count;
}

int Godot_Sound_GetEvent(int index, float *origin, int *entnum,
                         int *channel, int *sfxHandle, float *volume,
                         float *minDist, float *maxDist, float *pitch,
                         int *streamed, char *nameOut, int nameLen)
{
    if (index < 0 || index >= gr_sound_event_count) return -1;
    const gr_sound_event_t *ev = &gr_sound_events[index];
    if (origin)    { origin[0] = ev->origin[0]; origin[1] = ev->origin[1]; origin[2] = ev->origin[2]; }
    if (entnum)    *entnum    = ev->entnum;
    if (channel)   *channel   = ev->channel;
    if (sfxHandle) *sfxHandle = ev->sfxHandle;
    if (volume)    *volume    = ev->volume;
    if (minDist)   *minDist   = ev->minDist;
    if (maxDist)   *maxDist   = ev->maxDist;
    if (pitch)     *pitch     = ev->pitch;
    if (streamed)  *streamed  = ev->streamed;
    if (nameOut && nameLen > 0) {
        strncpy(nameOut, ev->name, nameLen - 1);
        nameOut[nameLen - 1] = '\0';
    }
    return ev->type;
}

void Godot_Sound_ClearEvents(void)
{
    gr_sound_event_count = 0;
}

/* --- Looping sound accessors --- */

int Godot_Sound_GetLoopCount(void)
{
    return gr_loop_sound_count;
}

void Godot_Sound_GetLoop(int index, float *origin, float *velocity,
                         int *sfxHandle, float *volume, float *minDist,
                         float *maxDist, float *pitch, int *flags)
{
    if (index < 0 || index >= gr_loop_sound_count) return;
    const gr_loop_sound_t *ls = &gr_loop_sounds[index];
    if (origin)    { origin[0] = ls->origin[0]; origin[1] = ls->origin[1]; origin[2] = ls->origin[2]; }
    if (velocity)  { velocity[0] = ls->velocity[0]; velocity[1] = ls->velocity[1]; velocity[2] = ls->velocity[2]; }
    if (sfxHandle) *sfxHandle = ls->sfxHandle;
    if (volume)    *volume    = ls->volume;
    if (minDist)   *minDist   = ls->minDist;
    if (maxDist)   *maxDist   = ls->maxDist;
    if (pitch)     *pitch     = ls->pitch;
    if (flags)     *flags     = ls->flags;
}

/* Extended loop accessor — includes entity number for position tracking. */
void Godot_Sound_GetLoopEx(int index, float *origin, float *velocity,
                           int *sfxHandle, float *volume, float *minDist,
                           float *maxDist, float *pitch, int *flags,
                           int *entnum)
{
    Godot_Sound_GetLoop(index, origin, velocity, sfxHandle, volume,
                        minDist, maxDist, pitch, flags);
    if (index >= 0 && index < gr_loop_sound_count && entnum)
        *entnum = gr_loop_sounds[index].entnum;
}

/* --- Listener accessors --- */

void Godot_Sound_GetListener(float *origin, float *axis, int *entnum)
{
    if (origin) { origin[0] = gr_listener_origin[0]; origin[1] = gr_listener_origin[1]; origin[2] = gr_listener_origin[2]; }
    if (axis)   { memcpy(axis, gr_listener_axis, 9 * sizeof(float)); }
    if (entnum) *entnum = gr_listener_entnum;
}

/* --- Music accessors --- */

int Godot_Sound_GetMusicAction(void)
{
    return gr_music.action;
}

const char *Godot_Sound_GetMusicName(void)
{
    return gr_music.name;
}

float Godot_Sound_GetMusicVolume(void)
{
    return gr_music.volume;
}

float Godot_Sound_GetMusicFadeTime(void)
{
    return gr_music.fadeTime;
}

void Godot_Sound_ClearMusicAction(void)
{
    gr_music.action = GR_MUSIC_NONE;
}

/* ===================================================================
 *  Helper: push a sound event
 * ================================================================ */

static void push_event(int type, const vec3_t origin, int entnum,
                       int channel, int sfxHandle, float volume,
                       float minDist, float maxDist, float pitch,
                       int streamed, const char *name)
{
    if (gr_sound_event_count >= MAX_SOUND_EVENTS) return;
    gr_sound_event_t *ev = &gr_sound_events[gr_sound_event_count++];
    ev->type      = type;
    ev->entnum    = entnum;
    ev->channel   = channel;
    ev->sfxHandle = sfxHandle;
    ev->volume    = volume;
    ev->minDist   = minDist;
    ev->maxDist   = maxDist;
    ev->pitch     = pitch;
    ev->streamed  = streamed;
    if (origin) {
        ev->origin[0] = origin[0];
        ev->origin[1] = origin[1];
        ev->origin[2] = origin[2];
    } else {
        ev->origin[0] = ev->origin[1] = ev->origin[2] = 0;
    }
    if (name) {
        strncpy(ev->name, name, sizeof(ev->name) - 1);
        ev->name[sizeof(ev->name) - 1] = '\0';
    } else {
        ev->name[0] = '\0';
    }
}

/* ===================================================================
 *  Sound system lifecycle
 * ================================================================ */

void S_Init(qboolean full_startup)
{
    Com_Printf("[GodotSound] S_Init (full_startup=%d)\n", (int)full_startup);
    gr_sfx_count = 0;
    gr_sound_event_count = 0;
    gr_loop_sound_count = 0;
    gr_playing_count = 0;
    gr_sound_fade_active = 0;
    memset(gr_snd_entities, 0, sizeof(gr_snd_entities));
    memset(&gr_music, 0, sizeof(gr_music));
}

void S_Shutdown(qboolean full_shutdown)
{
    Com_Printf("[GodotSound] S_Shutdown\n");
    /* Phase 149: Push stop-all event so MoHAARunner stops all Godot AudioStreamPlayers.
     * Without this, snd_restart/vid_restart leaves ghost sounds playing on the Godot side
     * while the engine-side tables are cleared. */
    push_event(GR_SND_STOP_ALL, NULL, 0, 0, 0,
               0, 0, 0, 0, 0, NULL);
    gr_sfx_count = 0;
    gr_sound_event_count = 0;
    gr_loop_sound_count = 0;
    gr_playing_count = 0;
}

void S_SoundInfo_f(void)
{
    Com_Printf("Godot Sound Backend: %d sfx registered, %d events queued\n",
               gr_sfx_count, gr_sound_event_count);
}

void S_SoundDump_f(void) {}

/* ===================================================================
 *  Registration
 * ================================================================ */

void S_BeginRegistration(void) {}

sfxHandle_t S_RegisterSound(const char *name, int streamed, qboolean force_load)
{
    if (!name || !name[0]) return 0;

    /* De-duplicate: check if already registered */
    for (int i = 0; i < gr_sfx_count; i++) {
        if (!Q_stricmp(gr_sfx[i].name, name)) {
            return gr_sfx[i].handle;
        }
    }

    if (gr_sfx_count >= MAX_GR_SFX) {
        Com_Printf("[GodotSound] WARNING: sfx table full, cannot register '%s'\n", name);
        return 0;
    }

    gr_sfx_entry_t *entry = &gr_sfx[gr_sfx_count];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->handle = gr_sfx_count + 1;  /* 1-based handles, 0 = invalid */
    gr_sfx_count++;

    return entry->handle;
}

qboolean S_IsSoundRegistered(const char *name)
{
    if (!name) return qfalse;
    for (int i = 0; i < gr_sfx_count; i++) {
        if (!Q_stricmp(gr_sfx[i].name, name)) return qtrue;
    }
    return qfalse;
}

qboolean S_NameExists(const char *name)
{
    return S_IsSoundRegistered(name);
}

void S_EndRegistration(void)
{
    static qboolean logged = qfalse;
    if (!logged) {
        Com_Printf("[GodotSound] EndRegistration: %d sounds registered\n", gr_sfx_count);
        logged = qtrue;
    }
}

float S_GetSoundTime(sfxHandle_t handle)
{
    /* Phase 49: Approximate — we don't track precise playback time yet.
     * Return 0.0 (start of sound).  When MoHAARunner gets full
     * AudioStreamPlayer tracking, we can query the actual position. */
    (void)handle;
    return 0.0f;
}

/* ===================================================================
 *  Playback
 * ================================================================ */

void S_StartSound(const vec3_t origin, int entnum, int entchannel,
                  sfxHandle_t sfxHandle, float volume, float min_dist,
                  float pitch, float maxDist, int streamed)
{
    push_event(GR_SND_START, origin, entnum, entchannel, sfxHandle,
               volume, min_dist, maxDist, pitch, streamed, NULL);
    /* Phase 49: Track as playing */
    const char *name = "";
    int idx = Godot_Sound_FindSfxIndex(sfxHandle);
    if (idx >= 0) name = gr_sfx[idx].name;
    track_playing(entchannel, sfxHandle, name);
}

void S_StartLocalSound(const char *sound_name, qboolean force_load)
{
    /* Register-on-the-fly if needed */
    sfxHandle_t h = S_RegisterSound(sound_name, 0, force_load);
    push_event(GR_SND_START_LOCAL, NULL, 0, 0, h,
               1.0f, 0, 0, 1.0f, 0, sound_name);
}

void S_StartLocalSoundChannel(const char *sound_name, qboolean force_load,
                              soundChannel_t channel)
{
    sfxHandle_t h = S_RegisterSound(sound_name, 0, force_load);
    push_event(GR_SND_START_LOCAL, NULL, 0, (int)channel, h,
               1.0f, 0, 0, 1.0f, 0, sound_name);
}

void S_StopSound(int entnum, int channel)
{
    push_event(GR_SND_STOP, NULL, entnum, channel, 0,
               0, 0, 0, 0, 0, NULL);
    /* Phase 49: Untrack */
    untrack_playing(channel);
}

void S_StopAllSounds(qboolean stop_music)
{
    push_event(GR_SND_STOP_ALL, NULL, 0, 0, 0,
               0, 0, 0, 0, 0, NULL);
    gr_playing_count = 0;  /* Phase 49: clear all tracking */
    if (stop_music) {
        gr_music.action = GR_MUSIC_STOP;
    }
}

/* ===================================================================
 *  Looping sounds
 * ================================================================ */

void S_ClearLoopingSounds(void)
{
    gr_loop_sound_count = 0;
}

void S_AddLoopingSound(const vec3_t origin, const vec3_t velocity,
                       sfxHandle_t sfxHandle, float volume,
                       float min_dist, float max_dist, float pitch,
                       int flags)
{
    if (gr_loop_sound_count >= MAX_LOOP_SOUNDS) return;
    gr_loop_sound_t *ls = &gr_loop_sounds[gr_loop_sound_count++];
    if (origin) {
        ls->origin[0] = origin[0]; ls->origin[1] = origin[1]; ls->origin[2] = origin[2];
    } else {
        ls->origin[0] = ls->origin[1] = ls->origin[2] = 0;
    }
    if (velocity) {
        ls->velocity[0] = velocity[0]; ls->velocity[1] = velocity[1]; ls->velocity[2] = velocity[2];
    } else {
        ls->velocity[0] = ls->velocity[1] = ls->velocity[2] = 0;
    }
    ls->sfxHandle = sfxHandle;
    ls->volume    = volume;
    ls->minDist   = min_dist;
    ls->maxDist   = max_dist;
    ls->pitch     = pitch;
    ls->flags     = flags;
    ls->entnum    = -1;  /* Extended API has no entnum; set via matching later */
}

/* ===================================================================
 *  Spatialisation & updating
 * ================================================================ */

void S_Respatialize(int entityNum, const vec3_t head, vec3_t axis[3])
{
    gr_listener_entnum = entityNum;
    if (head) {
        gr_listener_origin[0] = head[0];
        gr_listener_origin[1] = head[1];
        gr_listener_origin[2] = head[2];
    }
    if (axis) {
        memcpy(gr_listener_axis, axis, 9 * sizeof(float));
    }
}

void S_UpdateEntity(int entityNum, const vec3_t origin, const vec3_t vel,
                    qboolean use_listener)
{
    /* Phase 49: Track entity positions for moving sounds */
    if (entityNum < 0 || entityNum >= MAX_SOUND_ENTITIES) return;
    gr_snd_entity_t *e = &gr_snd_entities[entityNum];
    if (origin) {
        e->origin[0] = origin[0]; e->origin[1] = origin[1]; e->origin[2] = origin[2];
    }
    if (vel) {
        e->velocity[0] = vel[0]; e->velocity[1] = vel[1]; e->velocity[2] = vel[2];
    }
    e->use_listener = (int)use_listener;
    e->valid = 1;
}

void S_Update(void)
{
}

void S_SetGlobalAmbientVolumeLevel(float volume) {}

void S_SetReverb(int reverb_type, float reverb_level)
{
    /* Phase 50: Store reverb parameters for MoHAARunner to apply */
    gr_reverb_type  = reverb_type;
    gr_reverb_level = reverb_level;
}

void S_FadeSound(float fTime)
{
    /* Phase 50: Global sound fade — MoHAARunner reads this and
       applies gradual volume reduction over fTime seconds */
    gr_sound_fade_time   = fTime;
    gr_sound_fade_target = 0.0f;
    gr_sound_fade_active = 1;
}

/* ===================================================================
 *  Query
 * ================================================================ */

qboolean S_IsSoundPlaying(int channel_number, const char *sfxName)
{
    /* Phase 49: Check if a sound is currently tracked as playing */
    for (int i = 0; i < gr_playing_count; i++) {
        if (channel_number >= 0 && gr_playing[i].channel == channel_number) {
            if (!sfxName || !sfxName[0]) return qtrue;
            if (!Q_stricmp(gr_playing[i].name, sfxName)) return qtrue;
        }
        if (sfxName && sfxName[0] && !Q_stricmp(gr_playing[i].name, sfxName)) {
            return qtrue;
        }
    }
    return qfalse;
}

void S_Play(void) {}
void S_SoundList(void) {}

/* ===================================================================
 *  Clear / misc
 * ================================================================ */

void S_ClearSoundBuffer(void) {}
void S_DisableSounds(void) {}

/* ===================================================================
 *  SNDDMA stubs
 * ================================================================ */

qboolean SNDDMA_Init(void)        { return qfalse; }
int      SNDDMA_GetDMAPos(void)   { return 0; }
void     SNDDMA_Shutdown(void)    {}
void     SNDDMA_BeginPainting(void) {}
void     SNDDMA_Submit(void)      {}
void     SNDDMA_Activate(void)    {}

/* ===================================================================
 *  Music / soundtrack
 * ================================================================ */

qboolean MUSIC_LoadSoundtrackFile(const char *filename)
{
    if (filename && filename[0]) {
        strncpy(gr_music.name, filename, sizeof(gr_music.name) - 1);
        gr_music.name[sizeof(gr_music.name) - 1] = '\0';
    }
    return qtrue;
}

qboolean MUSIC_SongValid(const char *mood)
{
    /* Phase 50: A song is valid if a soundtrack has been loaded */
    return (gr_music.name[0] != '\0') ? qtrue : qfalse;
}

qboolean MUSIC_Loaded(void)
{
    return (gr_music.name[0] != '\0') ? qtrue : qfalse;
}

void     Music_Update(void)                   {}
void     MUSIC_SongEnded(void)                {}

void MUSIC_NewSoundtrack(const char *name)
{
    if (name && name[0]) {
        strncpy(gr_music.name, name, sizeof(gr_music.name) - 1);
        gr_music.name[sizeof(gr_music.name) - 1] = '\0';
        gr_music.action = GR_MUSIC_PLAY;
    }
}

void MUSIC_UpdateMood(int current, int fallback)
{
    gr_music.currentMood  = current;
    gr_music.fallbackMood = fallback;
}

void MUSIC_UpdateVolume(float volume, float fade_time)
{
    gr_music.volume   = volume;
    gr_music.fadeTime = fade_time;
    gr_music.action   = GR_MUSIC_VOLUME;
}

void MUSIC_StopAllSongs(void)  { gr_music.action = GR_MUSIC_STOP; }
void MUSIC_FreeAllSongs(void)  { gr_music.action = GR_MUSIC_STOP; }
qboolean MUSIC_Playing(void)
{
    /* Phase 50: Report as playing if a soundtrack is active */
    return (gr_music.action == GR_MUSIC_PLAY || gr_music.name[0] != '\0') ? qtrue : qfalse;
}

int MUSIC_FindSong(const char *name)
{
    /* Phase 50: Return 0 (found) if the name matches the current soundtrack */
    if (name && name[0] && gr_music.name[0]) {
        if (!Q_stricmp(name, gr_music.name)) return 0;
    }
    return -1;
}
int      MUSIC_CurrentSongChannel(void)        { return -1; }
void     MUSIC_StopChannel(int channel_number) {}
qboolean MUSIC_PlaySong(const char *alias)     { return qfalse; }
void     MUSIC_UpdateMusicVolumes(void)        {}
void     MUSIC_CheckForStoppedSongs(void)      {}

void        S_loadsoundtrack(void)             {}
const char *S_CurrentSoundtrack(void)          { return gr_music.name; }
void        S_PlaySong(void)                   {}

/* ===================================================================
 *  Triggered music (Phase 51)
 *  Stores triggered-music state for MoHAARunner to pick up and play.
 * ================================================================ */

#define GR_TRIG_NONE   0
#define GR_TRIG_SETUP  1
#define GR_TRIG_START  2
#define GR_TRIG_STOP   3
#define GR_TRIG_PAUSE  4
#define GR_TRIG_UNPAUSE 5

static struct {
    char  name[MAX_QPATH];
    int   loopCount;
    int   offset;
    int   autostart;
    int   action;
} gr_triggered_music = {0};

/* Accessors for MoHAARunner */
int         Godot_Sound_GetTriggeredAction(void)    { return gr_triggered_music.action; }
const char *Godot_Sound_GetTriggeredName(void)      { return gr_triggered_music.name; }
int         Godot_Sound_GetTriggeredLoopCount(void) { return gr_triggered_music.loopCount; }
int         Godot_Sound_GetTriggeredOffset(void)    { return gr_triggered_music.offset; }
void        Godot_Sound_ClearTriggeredAction(void)  { gr_triggered_music.action = GR_TRIG_NONE; }

void S_TriggeredMusic_SetupHandle(const char *pszName, int iLoopCount,
                                  int iOffset, qboolean autostart)
{
    if (pszName) {
        strncpy(gr_triggered_music.name, pszName, MAX_QPATH - 1);
        gr_triggered_music.name[MAX_QPATH - 1] = '\0';
    }
    gr_triggered_music.loopCount = iLoopCount;
    gr_triggered_music.offset    = iOffset;
    gr_triggered_music.autostart = (int)autostart;
    gr_triggered_music.action    = GR_TRIG_SETUP;
    if (autostart) gr_triggered_music.action = GR_TRIG_START;
}

void S_TriggeredMusic_Start(void)          { gr_triggered_music.action = GR_TRIG_START; }
void S_TriggeredMusic_StartLoop(void)      { gr_triggered_music.action = GR_TRIG_START; }
void S_TriggeredMusic_Stop(void)           { gr_triggered_music.action = GR_TRIG_STOP; }
void S_TriggeredMusic_Pause(void)          { gr_triggered_music.action = GR_TRIG_PAUSE; }
void S_TriggeredMusic_Unpause(void)        { gr_triggered_music.action = GR_TRIG_UNPAUSE; }
void S_TriggeredMusic_Volume(void)         {}
void S_TriggeredMusic_PlayIntroMusic(void)
{
    /* Intro music plays the special "intro" soundtrack */
    strncpy(gr_triggered_music.name, "sound/music/mus_MainTheme.mp3",
            MAX_QPATH - 1);
    gr_triggered_music.action = GR_TRIG_START;
}

/* ===================================================================
 *  Movie audio
 * ================================================================ */

void S_StopMovieAudio(void)                           {}
void S_SetupMovieAudio(const char *pszMovieName)      {}
int  S_CurrentMoviePosition(void)                     { return 0; }

/* ===================================================================
 *  Music info queries
 * ================================================================ */

const char  *S_GetMusicFilename(void)   { return gr_triggered_music.name; }
int          S_GetMusicLoopCount(void)  { return gr_triggered_music.loopCount; }
unsigned int S_GetMusicOffset(void)     { return (unsigned int)gr_triggered_music.offset; }

/* ===================================================================
 *  MP3-in-WAV detection (Phase 46)
 *  Some MOHAA .wav files use WAVE format tag 0x0055 (MPEG audio inside
 *  a WAV container).  This helper checks the fmt chunk format tag.
 * ================================================================ */

#define WAV_FORMAT_PCM  0x0001
#define WAV_FORMAT_MPEG 0x0055

/*
 * Godot_Sound_DetectMP3InWav — check if raw WAV data contains MP3 payload.
 *
 * @param data     Pointer to the RIFF/WAV file data.
 * @param dataLen  Length of the data in bytes.
 * @param out_mp3_offset  Receives the byte offset to the MP3 payload (data chunk).
 * @param out_mp3_length  Receives the length of the MP3 payload in bytes.
 *
 * @return  1 if the WAV contains MP3 (format tag 0x0055), 0 if standard PCM.
 *          Returns -1 on parse error.
 */
int Godot_Sound_DetectMP3InWav(const unsigned char *data, int dataLen,
                               int *out_mp3_offset, int *out_mp3_length)
{
    if (!data || dataLen < 44) return -1;

    /* Verify RIFF header */
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return -1;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
        return -1;

    /* Walk chunks to find 'fmt ' and 'data' */
    int pos = 12;
    int formatTag = 0;
    int foundFmt = 0;

    while (pos + 8 <= dataLen) {
        int chunkId   = *(int *)(data + pos);
        int chunkSize = *(int *)(data + pos + 4);

        /* Little-endian 'fmt ' = 0x20746D66 */
        if (data[pos] == 'f' && data[pos+1] == 'm' &&
            data[pos+2] == 't' && data[pos+3] == ' ') {
            if (pos + 8 + 2 > dataLen) return -1;
            formatTag = (int)(data[pos + 8] | (data[pos + 9] << 8));
            foundFmt = 1;
        }

        /* Little-endian 'data' = 0x61746164 */
        if (data[pos] == 'd' && data[pos+1] == 'a' &&
            data[pos+2] == 't' && data[pos+3] == 'a') {
            if (foundFmt && formatTag == WAV_FORMAT_MPEG) {
                if (out_mp3_offset) *out_mp3_offset = pos + 8;
                if (out_mp3_length) *out_mp3_length = chunkSize;
                return 1;
            }
            /* Standard PCM — not MP3-in-WAV */
            return 0;
        }

        pos += 8 + chunkSize;
        /* Chunks are word-aligned */
        if (chunkSize & 1) pos++;
    }

    return foundFmt ? 0 : -1;
}

/* ===================================================================
 *  Variables and functions from snd_local_new.h
 * ================================================================ */

cvar_t *s_volume        = NULL;
cvar_t *s_khz           = NULL;
cvar_t *s_loadas8bit    = NULL;
cvar_t *s_separation    = NULL;
cvar_t *s_musicVolume   = NULL;
cvar_t *s_ambientVolume = NULL;

qboolean   s_bLastInitSound  = qfalse;
qboolean   s_bSoundStarted   = qfalse;
qboolean   s_bSoundPaused    = qfalse;
qboolean   s_bTryUnpause     = qfalse;
int        s_iListenerNumber  = 0;
float      s_fAmbientVolume   = 1.0f;
int        number_of_sfx_infos = 0;
sfx_info_t sfx_infos[MAX_SFX_INFOS];
sfx_t      s_knownSfx[MAX_SFX];
int        s_numSfx = 0;
s_entity_t s_entity[MAX_GENTITIES];

cvar_t *s_show_sounds = NULL;

void load_sfx_info(void) {}

sfx_t *S_FindName(const char *name, int sequenceNumber)
{
    (void)name; (void)sequenceNumber;
    return NULL;
}

void S_DefaultSound(sfx_t *sfx)
{
    (void)sfx;
}

void S_LoadData(soundsystemsavegame_t *pSave)
{
    (void)pSave;
}

void S_SaveData(soundsystemsavegame_t *pSave)
{
    if (pSave) {
        memset(pSave, 0, sizeof(*pSave));
    }
}

qboolean S_LoadSound(const char *fileName, sfx_t *sfx, int streamed,
                     qboolean force_load)
{
    (void)fileName; (void)sfx; (void)streamed; (void)force_load;
    return qfalse;
}

void S_PrintInfo(void)
{
    Com_Printf("Godot Sound Backend: %d sfx registered\n", gr_sfx_count);
}

void S_DumpInfo(void) {}

qboolean S_NeedFullRestart(void) { return qfalse; }

void S_ReLoad(soundsystemsavegame_t *pSave) { (void)pSave; }

void S_Init2(void)
{
    Com_Printf("[GodotSound] S_Init2\n");
}
