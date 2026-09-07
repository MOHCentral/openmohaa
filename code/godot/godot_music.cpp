/*
 * godot_music.cpp — Music playback manager for the OpenMoHAA GDExtension.
 *
 * Uses two AudioStreamPlayer nodes for crossfade support.  Music files
 * are loaded from the engine VFS (sound/music/*.mp3) as raw bytes and
 * wrapped in Godot AudioStreamMP3 resources.
 *
 * Audio Completeness.
 */

#include "godot_music.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

/* ------------------------------------------------------------------ */
/*  C accessors — declared here to avoid engine header collisions     */
/* ------------------------------------------------------------------ */

extern "C" {
    /* godot_sound.c — music state */
    int         Godot_Sound_GetMusicAction(void);
    const char *Godot_Sound_GetMusicName(void);
    float       Godot_Sound_GetMusicVolume(void);
    float       Godot_Sound_GetMusicFadeTime(void);
    void        Godot_Sound_ClearMusicAction(void);
    int         Godot_Sound_GetMusicMood(int *current, int *fallback);

    /* godot_sound.c — triggered music */
    int         Godot_Sound_GetTriggeredAction(void);
    const char *Godot_Sound_GetTriggeredName(void);
    int         Godot_Sound_GetTriggeredLoopCount(void);
    int         Godot_Sound_GetTriggeredOffset(void);
    void        Godot_Sound_ClearTriggeredAction(void);

    /* godot_vfs_accessors.c */
    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer);
    void Godot_VFS_FreeFile(void *buffer);
    int  Godot_VFS_FileExists(const char *qpath);
}

/* Music action constants (must match godot_sound.c) */
#define GR_MUSIC_NONE   0
#define GR_MUSIC_PLAY   1
#define GR_MUSIC_STOP   2
#define GR_MUSIC_VOLUME 3

/* Triggered action constants (must match godot_sound.c) */
#define GR_TRIG_NONE    0
#define GR_TRIG_SETUP   1
#define GR_TRIG_START   2
#define GR_TRIG_STOP    3
#define GR_TRIG_PAUSE   4
#define GR_TRIG_UNPAUSE 5

/* Maximum file path length (matches engine MAX_QPATH). */
#define MUSIC_MAX_PATH 256

/* Mood count — matches music_mood_t::mood_totalnumber in q_shared.h. */
#define MOOD_COUNT 16

using namespace godot;

/* ================================================================== */
/*  Mood string table (must match q_shared.h music_mood_t order)       */
/* ================================================================== */

static const char *s_mood_names[MOOD_COUNT] = {
    "none", "normal", "action", "suspense", "mystery",
    "success", "failure", "surprise", "special",
    "aux1", "aux2", "aux3", "aux4", "aux5", "aux6", "aux7"
};

/* ================================================================== */
/*  Per-mood track info from a .mus soundtrack file                     */
/* ================================================================== */

struct MoodTrackInfo {
    char  path[MUSIC_MAX_PATH];   /* Full resolved path (base_dir/filename) */
    bool  loop;
    float volume;
    float fadetime;
    bool  defined;                /* Was this mood present in the .mus? */
};

/* ================================================================== */
/*  Internal state                                                     */
/* ================================================================== */

static Node *s_parent = nullptr;

/* Two players for crossfade support.  Index 0 = primary, 1 = secondary. */
static AudioStreamPlayer *s_players[2]  = { nullptr, nullptr };
static int                s_active_idx  = 0;    /* which player is current */

/* Triggered music uses a separate player. */
static AudioStreamPlayer *s_triggered_player = nullptr;

static char  s_current_track[MUSIC_MAX_PATH] = {0};
static float s_master_volume   = 1.0f;
static float s_target_volume   = 1.0f;
static float s_current_volume  = 1.0f;
static float s_fade_duration   = 0.0f;
static float s_fade_elapsed    = 0.0f;
static bool  s_fading          = false;
static bool  s_initialised     = false;

/* ── Mood-driven track switching state ── */
static MoodTrackInfo s_soundtrack_moods[MOOD_COUNT];
static int   s_current_mood    = 0;   /* mood_none */
static int   s_fallback_mood   = 0;   /* mood_none */
static char  s_soundtrack_name[MUSIC_MAX_PATH] = {0};
static bool  s_soundtrack_loaded = false;

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

/*
 * Convert a linear volume (0–1) to Godot's dB scale.
 * Godot AudioStreamPlayer uses dB, where 0 dB = full volume.
 * We clamp the minimum to -80 dB (effectively silent).
 */
static float linear_to_db(float linear)
{
    if (linear <= 0.0001f) return -80.0f;
    return 20.0f * log10f(linear);
}

/*
 * Try to load an MP3 file directly from the VFS given a resolved path.
 * Returns a valid Ref or null on failure.
 */
static Ref<AudioStreamMP3> load_mp3_from_path(const char *path)
{
    if (!path || !path[0]) return Ref<AudioStreamMP3>();

    void *buf = nullptr;
    long  len = Godot_VFS_ReadFile(path, &buf);
    if (len <= 0 || !buf) return Ref<AudioStreamMP3>();

    PackedByteArray pba;
    pba.resize(len);
    memcpy(pba.ptrw(), buf, (size_t)len);
    Godot_VFS_FreeFile(buf);

    Ref<AudioStreamMP3> stream;
    stream.instantiate();
    stream->set_data(pba);

    UtilityFunctions::print(
        String("[GodotMusic] Loaded music: ") + String(path) +
        String(" (") + String::num_int64(len) + String(" bytes)"));
    return stream;
}

/*
 * Parse a MOHAA .mus file to extract the track path, loop flag, and volume
 * for the "normal" mood.  Returns true if a track path was resolved.
 *
 * .mus format (simplified):
 *   path <base_dir>
 *   normal <filename>
 *   !normal loop
 *   !normal volume <float>
 */
static bool parse_mus_file(const char *mus_path, char *out_track,
                           int track_size, bool *out_loop, float *out_volume)
{
    void *buf = nullptr;
    long  len = Godot_VFS_ReadFile(mus_path, &buf);
    if (len <= 0 || !buf) return false;

    char base_dir[MUSIC_MAX_PATH] = {0};
    char track_file[MUSIC_MAX_PATH] = {0};
    *out_loop   = false;
    *out_volume = 1.0f;

    /* Simple line-by-line parse */
    const char *src = (const char *)buf;
    const char *end = src + len;

    while (src < end) {
        /* Skip leading whitespace */
        while (src < end && (*src == ' ' || *src == '\t' || *src == '\r')) src++;
        if (src >= end) break;

        /* Find end of line */
        const char *eol = src;
        while (eol < end && *eol != '\n') eol++;

        int line_len = (int)(eol - src);
        /* Strip trailing whitespace */
        while (line_len > 0 && (src[line_len - 1] == ' ' || src[line_len - 1] == '\t' || src[line_len - 1] == '\r'))
            line_len--;

        if (line_len > 5 && strncmp(src, "path ", 5) == 0) {
            int val_len = line_len - 5;
            if (val_len >= MUSIC_MAX_PATH) val_len = MUSIC_MAX_PATH - 1;
            strncpy(base_dir, src + 5, (size_t)val_len);
            base_dir[val_len] = '\0';
        } else if (line_len > 7 && strncmp(src, "normal ", 7) == 0
                   && strncmp(src, "!normal", 7) != 0) {
            int val_len = line_len - 7;
            if (val_len >= MUSIC_MAX_PATH) val_len = MUSIC_MAX_PATH - 1;
            strncpy(track_file, src + 7, (size_t)val_len);
            track_file[val_len] = '\0';
            /* Strip inline // comments and trailing whitespace */
            char *comment = strstr(track_file, "//");
            if (comment) *comment = '\0';
            int tlen = (int)strlen(track_file);
            while (tlen > 0 && (track_file[tlen-1] == ' ' || track_file[tlen-1] == '\t'))
                track_file[--tlen] = '\0';
        } else if (line_len >= 12 && strncmp(src, "!normal loop", 12) == 0) {
            *out_loop = true;
        } else if (line_len > 16 && strncmp(src, "!normal volume ", 15) == 0) {
            char *endptr = nullptr;
            float parsed = strtof(src + 15, &endptr);
            if (endptr != src + 15) {
                if (parsed < 0.0f) parsed = 0.0f;
                if (parsed > 2.0f) parsed = 2.0f;
                *out_volume = parsed;
            }
        }

        src = (eol < end) ? eol + 1 : end;
    }

    Godot_VFS_FreeFile(buf);

    if (track_file[0] == '\0') return false;

    /* Build the full track path */
    if (base_dir[0]) {
        snprintf(out_track, track_size, "%s/%s", base_dir, track_file);
    } else {
        snprintf(out_track, track_size, "sound/music/%s", track_file);
    }
    return true;
}

/*
 * Look up a mood name string → index (0–15).  Returns -1 if unknown.
 */
static int mood_name_to_index(const char *name, int name_len)
{
    for (int i = 0; i < MOOD_COUNT; i++) {
        if ((int)strlen(s_mood_names[i]) == name_len &&
            strncmp(name, s_mood_names[i], (size_t)name_len) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Parse a .mus script to extract ALL mood tracks into s_soundtrack_moods[].
 * Resolves paths using the "path" directive from the file.
 *
 * .mus format (full):
 *   path <base_dir>
 *   <mood_name> <filename>
 *   !<mood_name> loop
 *   !<mood_name> volume <float>
 *   !<mood_name> fadetime <float>
 */
static bool parse_mus_full(const char *name)
{
    /* Reset all moods */
    for (int i = 0; i < MOOD_COUNT; i++) {
        s_soundtrack_moods[i].path[0] = '\0';
        s_soundtrack_moods[i].loop     = false;
        s_soundtrack_moods[i].volume   = 1.0f;
        s_soundtrack_moods[i].fadetime = 2.0f;
        s_soundtrack_moods[i].defined  = false;
    }

    /* Build the .mus path (same resolution as load_music_from_vfs) */
    char mus_base[MUSIC_MAX_PATH];
    strncpy(mus_base, name, MUSIC_MAX_PATH - 1);
    mus_base[MUSIC_MAX_PATH - 1] = '\0';

    const char *base = mus_base;
    if (strncmp(base, "sound/", 6) == 0) base += 6;

    char mus_path[MUSIC_MAX_PATH];
    if (strncmp(base, "music/", 6) != 0) {
        snprintf(mus_path, MUSIC_MAX_PATH, "music/%s", base);
    } else {
        strncpy(mus_path, base, MUSIC_MAX_PATH - 1);
        mus_path[MUSIC_MAX_PATH - 1] = '\0';
    }

    /* Replace or append .mus extension */
    char *dot = strrchr(mus_path, '.');
    if (dot && (strcmp(dot, ".mus") == 0 || strcmp(dot, ".mp3") == 0)
        && (dot - mus_path + 5) <= MUSIC_MAX_PATH) {
        memcpy(dot, ".mus", 5);
    } else {
        size_t plen = strlen(mus_path);
        if (plen + 4 < MUSIC_MAX_PATH) {
            strcat(mus_path, ".mus");
        }
    }

    void *buf = nullptr;
    long  len = Godot_VFS_ReadFile(mus_path, &buf);
    if (len <= 0 || !buf) return false;

    char base_dir[MUSIC_MAX_PATH] = {0};
    const char *src = (const char *)buf;
    const char *end = src + len;

    while (src < end) {
        /* Skip leading whitespace */
        while (src < end && (*src == ' ' || *src == '\t' || *src == '\r')) src++;
        if (src >= end) break;

        /* Find end of line */
        const char *eol = src;
        while (eol < end && *eol != '\n') eol++;

        int line_len = (int)(eol - src);
        /* Strip trailing whitespace */
        while (line_len > 0 && (src[line_len - 1] == ' ' ||
               src[line_len - 1] == '\t' || src[line_len - 1] == '\r'))
            line_len--;

        /* Skip empty lines and comments */
        if (line_len <= 0 || src[0] == '/' || src[0] == '#') {
            src = (eol < end) ? eol + 1 : end;
            continue;
        }

        if (line_len > 5 && strncmp(src, "path ", 5) == 0) {
            /* path <base_dir> */
            int val_len = line_len - 5;
            if (val_len >= MUSIC_MAX_PATH) val_len = MUSIC_MAX_PATH - 1;
            strncpy(base_dir, src + 5, (size_t)val_len);
            base_dir[val_len] = '\0';
        } else if (src[0] == '!') {
            /* Attribute line: !<mood_name> <attr> [value] */
            const char *mood_start = src + 1;
            const char *mood_end = mood_start;
            while (mood_end < src + line_len && *mood_end != ' ' && *mood_end != '\t')
                mood_end++;
            int mood_len = (int)(mood_end - mood_start);
            int mood_idx = mood_name_to_index(mood_start, mood_len);

            if (mood_idx >= 0) {
                const char *attr = mood_end;
                while (attr < src + line_len && (*attr == ' ' || *attr == '\t')) attr++;
                int remaining = (int)(src + line_len - attr);

                if (remaining >= 4 && strncmp(attr, "loop", 4) == 0) {
                    s_soundtrack_moods[mood_idx].loop = true;
                } else if (remaining > 7 && strncmp(attr, "volume ", 7) == 0) {
                    float v = strtof(attr + 7, nullptr);
                    if (v < 0.0f) v = 0.0f;
                    if (v > 2.0f) v = 2.0f;
                    s_soundtrack_moods[mood_idx].volume = v;
                } else if (remaining > 9 && strncmp(attr, "fadetime ", 9) == 0) {
                    float f = strtof(attr + 9, nullptr);
                    if (f < 0.0f) f = 0.0f;
                    s_soundtrack_moods[mood_idx].fadetime = f;
                }
            }
        } else {
            /* Track line: <mood_name> <filename> */
            const char *mood_start = src;
            const char *mood_end = mood_start;
            while (mood_end < src + line_len && *mood_end != ' ' && *mood_end != '\t')
                mood_end++;
            int mood_len = (int)(mood_end - mood_start);
            int mood_idx = mood_name_to_index(mood_start, mood_len);

            if (mood_idx >= 0) {
                const char *file_start = mood_end;
                while (file_start < src + line_len &&
                       (*file_start == ' ' || *file_start == '\t'))
                    file_start++;
                int file_len = (int)(src + line_len - file_start);

                /* Strip inline // comments */
                for (int ci = 0; ci < file_len - 1; ci++) {
                    if (file_start[ci] == '/' && file_start[ci + 1] == '/') {
                        file_len = ci;
                        while (file_len > 0 &&
                               (file_start[file_len - 1] == ' ' ||
                                file_start[file_len - 1] == '\t'))
                            file_len--;
                        break;
                    }
                }

                if (file_len > 0 && file_len < MUSIC_MAX_PATH) {
                    char filename[MUSIC_MAX_PATH];
                    strncpy(filename, file_start, (size_t)file_len);
                    filename[file_len] = '\0';

                    if (base_dir[0]) {
                        snprintf(s_soundtrack_moods[mood_idx].path,
                                 MUSIC_MAX_PATH, "%s/%s", base_dir, filename);
                    } else {
                        snprintf(s_soundtrack_moods[mood_idx].path,
                                 MUSIC_MAX_PATH, "sound/music/%s", filename);
                    }
                    s_soundtrack_moods[mood_idx].defined = true;
                }
            }
        }

        src = (eol < end) ? eol + 1 : end;
    }

    Godot_VFS_FreeFile(buf);

    /* Log what we found */
    int defined_count = 0;
    for (int i = 0; i < MOOD_COUNT; i++) {
        if (s_soundtrack_moods[i].defined) {
            defined_count++;
            UtilityFunctions::print(
                String("[GodotMusic] Mood '") + String(s_mood_names[i]) +
                String("' → '") + String(s_soundtrack_moods[i].path) +
                String("' loop=") + String(s_soundtrack_moods[i].loop ? "yes" : "no") +
                String(" vol=") + String::num(s_soundtrack_moods[i].volume, 2) +
                String(" fade=") + String::num(s_soundtrack_moods[i].fadetime, 2));
        }
    }
    UtilityFunctions::print(
        String("[GodotMusic] Parsed .mus '") + String(mus_path) +
        String("': ") + String::num_int64(defined_count) +
        String(" mood tracks defined"));

    return defined_count > 0;
}

/*
 * Play the track for a specific mood, with crossfade.
 * Falls back: target_mood → fallback_mood → mood_normal (1).
 */
static void play_mood_track(int target_mood, int fallback_mood)
{
    /* Resolve the mood to play */
    int mood = target_mood;
    if (mood <= 0 || mood >= MOOD_COUNT || !s_soundtrack_moods[mood].defined) {
        mood = fallback_mood;
    }
    if (mood <= 0 || mood >= MOOD_COUNT || !s_soundtrack_moods[mood].defined) {
        mood = 1; /* normal */
    }
    if (mood <= 0 || mood >= MOOD_COUNT || !s_soundtrack_moods[mood].defined) {
        return; /* No track available at all */
    }

    MoodTrackInfo &ti = s_soundtrack_moods[mood];

    /* Already playing this exact track? */
    if (strcmp(s_current_track, ti.path) == 0) return;

    Ref<AudioStreamMP3> stream = load_mp3_from_path(ti.path);
    if (stream.is_null()) return;

    stream->set_loop(ti.loop);

    int old_idx = s_active_idx;
    s_active_idx = 1 - s_active_idx;

    s_players[s_active_idx]->set_stream(stream);
    s_players[s_active_idx]->set_volume_db(
        linear_to_db(ti.volume * s_current_volume * s_master_volume));
    s_players[s_active_idx]->play();

    strncpy(s_current_track, ti.path, MUSIC_MAX_PATH - 1);
    s_current_track[MUSIC_MAX_PATH - 1] = '\0';

    /* Crossfade from old player */
    if (ti.fadetime > 0.01f && s_players[old_idx]->is_playing()) {
        s_fade_duration = ti.fadetime;
        s_fade_elapsed  = 0.0f;
        s_fading        = true;
    } else {
        s_players[old_idx]->stop();
    }

    UtilityFunctions::print(
        String("[GodotMusic] Mood switch → '") + String(s_mood_names[mood]) +
        String("' playing '") + String(ti.path) + String("'"));
}

/*
 * Try to load a music file from the VFS.
 * First checks if the name refers to a .mus script (MOHAA soundtrack
 * descriptor) and parses it.  Otherwise tries several path variants:
 *   1. The name as-is
 *   2. "sound/music/<name>.mp3"
 *   3. "sound/music/<name>"
 *   4. "<name>.mp3"
 *
 * When loaded via .mus, the loop and volume settings from the script are
 * applied.  The out_loop and out_volume pointers (if non-null) receive
 * the parsed values; callers that don't need them may pass nullptr.
 *
 * Returns a Ref<AudioStreamMP3> or null on failure.
 */
static Ref<AudioStreamMP3> load_music_from_vfs(const char *name,
                                               bool *out_loop = nullptr,
                                               float *out_volume = nullptr)
{
    if (!name || !name[0]) return Ref<AudioStreamMP3>();

    bool  mus_loop   = false;
    float mus_volume = 1.0f;

    /* ── Try .mus soundtrack descriptor first ── */
    {
        /* Build the .mus path: strip leading "sound/", ensure "music/" prefix */
        char mus_base[MUSIC_MAX_PATH];
        strncpy(mus_base, name, MUSIC_MAX_PATH - 1);
        mus_base[MUSIC_MAX_PATH - 1] = '\0';

        /* Strip "sound/" prefix if present */
        const char *base = mus_base;
        if (strncmp(base, "sound/", 6) == 0) base += 6;

        char mus_path[MUSIC_MAX_PATH];
        if (strncmp(base, "music/", 6) != 0) {
            snprintf(mus_path, MUSIC_MAX_PATH, "music/%s", base);
        } else {
            strncpy(mus_path, base, MUSIC_MAX_PATH - 1);
            mus_path[MUSIC_MAX_PATH - 1] = '\0';
        }

        /* Replace or append .mus extension */
        char *dot = strrchr(mus_path, '.');
        if (dot && (strcmp(dot, ".mus") == 0 || strcmp(dot, ".mp3") == 0)
            && (dot - mus_path + 5) <= MUSIC_MAX_PATH) {
            memcpy(dot, ".mus", 5); /* includes NUL terminator */
        } else {
            size_t plen = strlen(mus_path);
            if (plen + 4 < MUSIC_MAX_PATH) {
                strcat(mus_path, ".mus");
            }
        }

        char track_path[MUSIC_MAX_PATH] = {0};
        if (parse_mus_file(mus_path, track_path, MUSIC_MAX_PATH,
                           &mus_loop, &mus_volume)) {
            Ref<AudioStreamMP3> stream = load_mp3_from_path(track_path);
            if (stream.is_valid()) {
                if (out_loop)   *out_loop   = mus_loop;
                if (out_volume) *out_volume = mus_volume;
                UtilityFunctions::print(
                    String("[GodotMusic] Resolved .mus '") + String(mus_path) +
                    String("' → '") + String(track_path) + String("'"));
                return stream;
            }
        }
    }

    /* ── Fallback: try direct path variants ── */
    /* Don't attempt to load .mus script files as raw audio; they are text. */
    const char *ext_check = strrchr(name, '.');
    if (ext_check && strcmp(ext_check, ".mus") == 0) {
        UtilityFunctions::print(
            String("[GodotMusic] WARNING: Could not load music '") +
            String(name) + String("' (.mus parse failed, no mp3 found)"));
        return Ref<AudioStreamMP3>();
    }

    char paths[4][MUSIC_MAX_PATH];
    int  path_count = 0;

    /* Strip "sound/" or "music/" prefix for canonical name comparisons. */
    const char *bare = name;
    if (strncmp(bare, "sound/music/", 12) == 0) bare += 12;
    else if (strncmp(bare, "sound/", 6) == 0)   bare += 6;
    else if (strncmp(bare, "music/", 6) == 0)    bare += 6;

    /* Strip .mp3 extension from bare name if present */
    char bare_noext[MUSIC_MAX_PATH];
    strncpy(bare_noext, bare, MUSIC_MAX_PATH - 1);
    bare_noext[MUSIC_MAX_PATH - 1] = '\0';
    char *bare_dot = strrchr(bare_noext, '.');
    if (bare_dot && strcmp(bare_dot, ".mp3") == 0) *bare_dot = '\0';

    /* 1. sound/music/<bare>.mp3 — canonical location */
    snprintf(paths[path_count], MUSIC_MAX_PATH, "sound/music/%s.mp3", bare_noext);
    path_count++;

    /* 2. sound/music/<bare> — no extension (VFS may handle extension-less) */
    snprintf(paths[path_count], MUSIC_MAX_PATH, "sound/music/%s", bare_noext);
    path_count++;

    /* 3. music/<bare>.mp3 — alternate prefix */
    if (strncmp(name, "sound/", 6) != 0) {
        snprintf(paths[path_count], MUSIC_MAX_PATH, "music/%s.mp3", bare_noext);
        path_count++;
    }

    /* 4. Name as-is (only if it looks like an audio file, not .mus) */
    if (strstr(name, ".mp3") != nullptr || strstr(name, ".wav") != nullptr) {
        strncpy(paths[path_count], name, MUSIC_MAX_PATH - 1);
        paths[path_count][MUSIC_MAX_PATH - 1] = '\0';
        path_count++;
    }

    for (int i = 0; i < path_count; i++) {
        Ref<AudioStreamMP3> stream = load_mp3_from_path(paths[i]);
        if (stream.is_valid()) {
            if (out_loop)   *out_loop   = false;
            if (out_volume) *out_volume = 1.0f;
            return stream;
        }
    }

    UtilityFunctions::print(
        String("[GodotMusic] WARNING: Could not load music '") +
        String(name) + String("'"));
    return Ref<AudioStreamMP3>();
}

/*
 * Start playing a track on the given player.
 */
static void play_track(AudioStreamPlayer *player, const char *name)
{
    if (!player || !name || !name[0]) return;

    bool  mus_loop   = true;
    float mus_volume = 1.0f;
    Ref<AudioStreamMP3> stream = load_music_from_vfs(name, &mus_loop, &mus_volume);
    if (stream.is_null()) return;

    stream->set_loop(mus_loop);
    player->set_stream(stream);
    /* Apply both the .mus per-track volume and the current fade/master volume */
    player->set_volume_db(linear_to_db(mus_volume * s_current_volume * s_master_volume));
    player->play();
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

extern "C" void Godot_Music_Init(void *parent_node)
{
    if (s_initialised) return;
    if (!parent_node) return;

    s_parent = reinterpret_cast<Node *>(parent_node);

    /* Create the two crossfade players */
    for (int i = 0; i < 2; i++) {
        s_players[i] = memnew(AudioStreamPlayer);
        s_players[i]->set_name(String("MusicPlayer") + String::num_int64(i));
        s_players[i]->set_bus(StringName("Master"));
        s_parent->add_child(s_players[i]);
    }

    /* Create the triggered-music player */
    s_triggered_player = memnew(AudioStreamPlayer);
    s_triggered_player->set_name(String("TriggeredMusicPlayer"));
    s_triggered_player->set_bus(StringName("Master"));
    s_parent->add_child(s_triggered_player);

    s_active_idx     = 0;
    s_current_track[0] = '\0';
    s_master_volume  = 1.0f;
    s_target_volume  = 1.0f;
    s_current_volume = 1.0f;
    s_fading         = false;
    s_initialised    = true;

    UtilityFunctions::print("[GodotMusic] Initialised");
}

extern "C" void Godot_Music_Shutdown(void)
{
    if (!s_initialised) return;

    // Shutdown is called from MoHAARunner PREDELETE while players are valid.
    // Stop playback and drop stream refs so AudioStream resources are released
    // before ObjectDB cleanup.
    for (int i = 0; i < 2; i++) {
        if (s_players[i]) {
            if (s_players[i]->is_playing()) s_players[i]->stop();
            s_players[i]->set_stream(Ref<AudioStream>());
        }
    }

    if (s_triggered_player) {
        if (s_triggered_player->is_playing()) s_triggered_player->stop();
        s_triggered_player->set_stream(Ref<AudioStream>());
    }

    s_players[0] = nullptr;
    s_players[1] = nullptr;
    s_triggered_player = nullptr;

    s_parent        = nullptr;
    s_initialised   = false;
    s_current_track[0] = '\0';

    UtilityFunctions::print("[GodotMusic] Shutdown");
}

extern "C" void Godot_Music_Update(float delta)
{
    if (!s_initialised) return;

    /* ── Handle soundtrack music state ── */
    int action = Godot_Sound_GetMusicAction();
    if (action != GR_MUSIC_NONE) {
        switch (action) {
        case GR_MUSIC_PLAY: {
            const char *name = Godot_Sound_GetMusicName();
            if (name && name[0]) {
                /* Parse the full .mus for mood-driven switching */
                if (strcmp(s_soundtrack_name, name) != 0) {
                    strncpy(s_soundtrack_name, name, MUSIC_MAX_PATH - 1);
                    s_soundtrack_name[MUSIC_MAX_PATH - 1] = '\0';
                    s_soundtrack_loaded = parse_mus_full(name);
                    s_current_mood    = 0;
                    s_fallback_mood   = 0;
                }

                /* If moods were parsed, use mood-aware playback */
                if (s_soundtrack_loaded) {
                    int cur = 0, fb = 0;
                    Godot_Sound_GetMusicMood(&cur, &fb);
                    s_current_mood  = cur;
                    s_fallback_mood = fb;
                    play_mood_track(cur, fb);
                } else {
                    /* Fallback: play the named track directly */
                    if (strcmp(s_current_track, name) != 0) {
                        int old_idx = s_active_idx;
                        s_active_idx = 1 - s_active_idx;

                        play_track(s_players[s_active_idx], name);
                        strncpy(s_current_track, name, MUSIC_MAX_PATH - 1);
                        s_current_track[MUSIC_MAX_PATH - 1] = '\0';

                        float fade = Godot_Sound_GetMusicFadeTime();
                        if (fade > 0.01f && s_players[old_idx]->is_playing()) {
                            s_fade_duration = fade;
                            s_fade_elapsed  = 0.0f;
                            s_fading        = true;
                        } else {
                            s_players[old_idx]->stop();
                        }
                    }
                }
            }
            break;
        }
        case GR_MUSIC_STOP:
            for (int i = 0; i < 2; i++) {
                if (s_players[i]) s_players[i]->stop();
            }
            s_current_track[0] = '\0';
            s_soundtrack_name[0] = '\0';
            s_soundtrack_loaded = false;
            s_current_mood  = 0;
            s_fallback_mood = 0;
            s_fading = false;
            break;

        case GR_MUSIC_VOLUME: {
            float vol  = Godot_Sound_GetMusicVolume();
            float fade = Godot_Sound_GetMusicFadeTime();
            if (fade > 0.01f) {
                s_target_volume = vol;
                s_fade_duration = fade;
                s_fade_elapsed  = 0.0f;
                s_fading        = true;
            } else {
                s_current_volume = vol;
                s_target_volume  = vol;
                s_fading = false;
                if (s_players[s_active_idx]) {
                    s_players[s_active_idx]->set_volume_db(
                        linear_to_db(s_current_volume * s_master_volume));
                }
            }
            break;
        }
        }
        Godot_Sound_ClearMusicAction();
    }

    /* ── Handle crossfade / volume fade ── */
    if (s_fading && s_fade_duration > 0.0f) {
        s_fade_elapsed += delta;
        float t = s_fade_elapsed / s_fade_duration;
        if (t >= 1.0f) {
            t = 1.0f;
            s_fading = false;

            /* Stop the old player once fade completes */
            int old_idx = 1 - s_active_idx;
            if (s_players[old_idx] && s_players[old_idx]->is_playing()) {
                s_players[old_idx]->stop();
            }
        }

        /* Fade in the active player, fade out the old one */
        float active_vol = s_target_volume * t;

        if (s_players[s_active_idx]) {
            s_players[s_active_idx]->set_volume_db(
                linear_to_db(active_vol * s_master_volume));
        }
        int old_idx = 1 - s_active_idx;
        if (s_players[old_idx] && s_players[old_idx]->is_playing()) {
            float old_vol = s_current_volume * (1.0f - t);
            s_players[old_idx]->set_volume_db(
                linear_to_db(old_vol * s_master_volume));
        }

        if (!s_fading) {
            s_current_volume = s_target_volume;
        }
    }

    /* ── Mood-driven track switching (poll each frame) ── */
    if (s_soundtrack_loaded) {
        int cur = 0, fb = 0;
        Godot_Sound_GetMusicMood(&cur, &fb);
        if (cur != s_current_mood || fb != s_fallback_mood) {
            s_current_mood  = cur;
            s_fallback_mood = fb;
            play_mood_track(cur, fb);
        }
    }

    /* ── Handle triggered music ── */
    static char s_trig_last_track[MUSIC_MAX_PATH] = {0};
    static bool s_trig_last_failed = false;

    int trig_action = Godot_Sound_GetTriggeredAction();
    if (trig_action != GR_TRIG_NONE && s_triggered_player) {
        switch (trig_action) {
        case GR_TRIG_SETUP:
            /* Just store — don't auto-play; reset failure state for new setup */
            s_trig_last_failed = false;
            s_trig_last_track[0] = '\0';
            break;
        case GR_TRIG_START: {
            const char *tname = Godot_Sound_GetTriggeredName();
            if (tname && tname[0]) {
                /* Skip if we already failed to load this exact track */
                if (s_trig_last_failed && strcmp(s_trig_last_track, tname) == 0) {
                    break;
                }
                strncpy(s_trig_last_track, tname, MUSIC_MAX_PATH - 1);
                s_trig_last_track[MUSIC_MAX_PATH - 1] = '\0';

                Ref<AudioStreamMP3> stream = load_music_from_vfs(tname);
                if (stream.is_valid()) {
                    s_trig_last_failed = false;
                    int loop_count = Godot_Sound_GetTriggeredLoopCount();
                    stream->set_loop(loop_count != 0);
                    s_triggered_player->set_stream(stream);
                    s_triggered_player->set_volume_db(
                        linear_to_db(s_master_volume));
                    s_triggered_player->play();
                } else {
                    s_trig_last_failed = true;
                }
            }
            break;
        }
        case GR_TRIG_STOP:
            s_triggered_player->stop();
            s_trig_last_failed = false;
            s_trig_last_track[0] = '\0';
            break;
        case GR_TRIG_PAUSE:
            s_triggered_player->set_stream_paused(true);
            break;
        case GR_TRIG_UNPAUSE:
            s_triggered_player->set_stream_paused(false);
            break;
        }
        Godot_Sound_ClearTriggeredAction();
    }
}

extern "C" void Godot_Music_SetVolume(float volume)
{
    s_master_volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
    if (s_initialised && s_players[s_active_idx]) {
        s_players[s_active_idx]->set_volume_db(
            linear_to_db(s_current_volume * s_master_volume));
    }
    if (s_initialised && s_triggered_player) {
        s_triggered_player->set_volume_db(
            linear_to_db(s_master_volume));
    }
}

extern "C" int Godot_Music_IsPlaying(void)
{
    if (!s_initialised) return 0;
    if (s_players[s_active_idx] && s_players[s_active_idx]->is_playing())
        return 1;
    if (s_triggered_player && s_triggered_player->is_playing())
        return 1;
    return 0;
}

extern "C" const char *Godot_Music_GetCurrentTrack(void)
{
    return s_current_track;
}
