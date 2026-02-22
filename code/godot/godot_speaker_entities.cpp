/*
 * godot_speaker_entities.cpp — Speaker entity sound support for the
 * OpenMoHAA GDExtension.
 *
 * Parses BSP entity strings for ambient_generic / ambient_speaker /
 * sound-emitting entities that use a "noise" key, and creates persistent
 * AudioStreamPlayer3D nodes at their positions.
 *
 * Phase 47 — Audio Completeness.
 */

#include "godot_speaker_entities.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

/* ------------------------------------------------------------------ */
/*  C accessors (avoid engine header collisions)                      */
/* ------------------------------------------------------------------ */

extern "C" {
    long Godot_VFS_ReadFile(const char *qpath, void **out_buffer);
    void Godot_VFS_FreeFile(void *buffer);
    int  Godot_Sound_DetectMP3InWav(const unsigned char *data, int dataLen,
                                    int *out_mp3_offset, int *out_mp3_length);
}

/* id Tech 3 → Godot coordinate conversion scale. */
#define MOHAA_UNIT_SCALE (1.0f / 39.37f)

using namespace godot;

/* ================================================================== */
/*  Internal state                                                     */
/* ================================================================== */

static Node3D *s_parent = nullptr;
static godot_speaker_t  s_speakers[MAX_SPEAKER_ENTITIES];
static AudioStreamPlayer3D *s_players[MAX_SPEAKER_ENTITIES] = {0};
static float  s_timers[MAX_SPEAKER_ENTITIES] = {0};
static int    s_speaker_count = 0;
static bool   s_initialised   = false;

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

static float linear_to_db(float linear)
{
    if (linear <= 0.0001f) return -80.0f;
    return 20.0f * log10f(linear);
}

/*
 * Load a WAV or MP3 file from VFS into a Godot audio stream.
 * Handles both standard WAV and MP3-in-WAV containers.
 */
static Ref<AudioStream> load_sound_from_vfs(const char *name)
{
    if (!name || !name[0]) return Ref<AudioStream>();

    /* Try with and without "sound/" prefix */
    char paths[2][512];
    int path_count = 0;

    strncpy(paths[path_count], name, 511);
    paths[path_count][511] = '\0';
    path_count++;

    if (strncmp(name, "sound/", 6) != 0) {
        snprintf(paths[path_count], 512, "sound/%s", name);
        path_count++;
    }

    for (int i = 0; i < path_count; i++) {
        void *buf = nullptr;
        long  len = Godot_VFS_ReadFile(paths[i], &buf);
        if (len <= 0 || !buf) continue;

        const unsigned char *data = (const unsigned char *)buf;

        /* Check for MP3-in-WAV */
        int mp3_offset = 0, mp3_length = 0;
        int mp3_result = Godot_Sound_DetectMP3InWav(data, (int)len,
                                                     &mp3_offset, &mp3_length);

        if (mp3_result == 1 && mp3_length > 0) {
            /* MP3-in-WAV: extract the MPEG payload */
            PackedByteArray pba;
            pba.resize(mp3_length);
            memcpy(pba.ptrw(), data + mp3_offset, (size_t)mp3_length);
            Godot_VFS_FreeFile(buf);

            Ref<AudioStreamMP3> stream;
            stream.instantiate();
            stream->set_data(pba);
            return stream;
        }

        /* Check if it's a raw MP3 file (starts with ID3 or sync bytes) */
        if (len > 2 && ((data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) ||
                        (data[0] == 'I' && data[1] == 'D' && data[2] == '3'))) {
            PackedByteArray pba;
            pba.resize(len);
            memcpy(pba.ptrw(), data, (size_t)len);
            Godot_VFS_FreeFile(buf);

            Ref<AudioStreamMP3> stream;
            stream.instantiate();
            stream->set_data(pba);
            return stream;
        }

        /* Standard PCM WAV — parse header */
        if (len > 44 && data[0] == 'R' && data[1] == 'I' &&
            data[2] == 'F' && data[3] == 'F') {
            /* Find fmt and data chunks */
            int pos = 12;
            int format = 0, channels_count = 0, sample_rate = 0, bits = 0;
            int data_offset = 0, data_size = 0;

            while (pos + 8 <= (int)len) {
                int chunk_size = *(int *)(data + pos + 4);

                if (data[pos] == 'f' && data[pos+1] == 'm' &&
                    data[pos+2] == 't' && data[pos+3] == ' ') {
                    if (pos + 8 + 16 <= (int)len) {
                        format         = data[pos+8]  | (data[pos+9]  << 8);
                        channels_count = data[pos+10] | (data[pos+11] << 8);
                        sample_rate    = data[pos+12] | (data[pos+13] << 8) |
                                         (data[pos+14] << 16) | (data[pos+15] << 24);
                        bits           = data[pos+22] | (data[pos+23] << 8);
                    }
                }
                if (data[pos] == 'd' && data[pos+1] == 'a' &&
                    data[pos+2] == 't' && data[pos+3] == 'a') {
                    data_offset = pos + 8;
                    data_size   = chunk_size;
                    break;
                }
                pos += 8 + chunk_size;
                if (chunk_size & 1) pos++;
            }

            if (format == 1 && data_offset > 0 && data_size > 0 &&
                data_offset + data_size <= (int)len) {
                PackedByteArray pba;
                pba.resize(data_size);
                memcpy(pba.ptrw(), data + data_offset, (size_t)data_size);
                Godot_VFS_FreeFile(buf);

                Ref<AudioStreamWAV> stream;
                stream.instantiate();
                stream->set_data(pba);
                stream->set_mix_rate(sample_rate);
                stream->set_stereo(channels_count == 2);
                if (bits == 16) {
                    stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
                } else {
                    stream->set_format(AudioStreamWAV::FORMAT_8_BITS);
                }
                return stream;
            }
        }

        Godot_VFS_FreeFile(buf);
    }

    return Ref<AudioStream>();
}

/* ================================================================== */
/*  Entity string parser                                               */
/* ================================================================== */

/*
 * Simple BSP entity parser.  Finds entities with a "noise" key
 * (ambient_generic, sound_speaker, or any entity with a noise field).
 */
static void parse_entities(const char *text)
{
    if (!text) return;

    const char *p = text;
    char key[256], value[256];

    while (*p) {
        /* Find opening brace */
        while (*p && *p != '{') p++;
        if (!*p) break;
        p++; /* skip '{' */

        /* Parse key-value pairs until closing brace */
        float origin[3] = {0, 0, 0};
        char  noise[256] = {0};
        float wait_val = 0.0f;
        float random_val = 0.0f;
        int   has_noise = 0;
        char  classname[256] = {0};
        int   spawnflags = 0;

        while (*p && *p != '}') {
            /* Skip whitespace */
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (*p == '}' || !*p) break;

            /* Parse quoted key */
            if (*p != '"') { p++; continue; }
            p++; /* skip opening quote */
            int ki = 0;
            while (*p && *p != '"' && ki < 255) key[ki++] = *p++;
            key[ki] = '\0';
            if (*p == '"') p++;

            /* Skip whitespace between key and value */
            while (*p && (*p == ' ' || *p == '\t')) p++;

            /* Parse quoted value */
            if (*p != '"') continue;
            p++; /* skip opening quote */
            int vi = 0;
            while (*p && *p != '"' && vi < 255) value[vi++] = *p++;
            value[vi] = '\0';
            if (*p == '"') p++;

            /* Store relevant keys */
            if (strcasecmp(key, "origin") == 0) {
                sscanf(value, "%f %f %f", &origin[0], &origin[1], &origin[2]);
            } else if (strcasecmp(key, "noise") == 0) {
                strncpy(noise, value, 255);
                noise[255] = '\0';
                has_noise = 1;
            } else if (strcasecmp(key, "wait") == 0) {
                wait_val = (float)atof(value);
            } else if (strcasecmp(key, "random") == 0) {
                random_val = (float)atof(value);
            } else if (strcasecmp(key, "classname") == 0) {
                strncpy(classname, value, 255);
                classname[255] = '\0';
            } else if (strcasecmp(key, "spawnflags") == 0) {
                spawnflags = atoi(value);
            }
        }
        if (*p == '}') p++;

        /* Record any entity with a noise key as a speaker */
        if (has_noise && noise[0] && s_speaker_count < MAX_SPEAKER_ENTITIES) {
            godot_speaker_t *sp = &s_speakers[s_speaker_count];
            sp->origin[0] = origin[0];
            sp->origin[1] = origin[1];
            sp->origin[2] = origin[2];
            strncpy(sp->noise, noise, 255);
            sp->noise[255] = '\0';
            sp->wait_time   = wait_val;
            sp->random_time = random_val;
            sp->triggered   = (spawnflags & 1) ? 1 : 0;
            sp->active      = sp->triggered ? 0 : 1;
            s_speaker_count++;
        }
    }
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

extern "C" void Godot_Speakers_Init(void *parent_node)
{
    if (s_initialised) return;
    if (!parent_node) return;

    s_parent = reinterpret_cast<Node3D *>(parent_node);
    s_speaker_count = 0;
    memset(s_speakers, 0, sizeof(s_speakers));
    memset(s_players, 0, sizeof(s_players));
    memset(s_timers, 0, sizeof(s_timers));
    s_initialised = true;

    UtilityFunctions::print("[GodotSpeakers] Initialised");
}

extern "C" void Godot_Speakers_Shutdown(void)
{
    if (!s_initialised) return;

    for (int i = 0; i < MAX_SPEAKER_ENTITIES; i++) {
        if (s_players[i]) {
            s_players[i]->stop();
            if (s_players[i]->get_parent()) {
                s_players[i]->get_parent()->remove_child(s_players[i]);
            }
            memdelete(s_players[i]);
            s_players[i] = nullptr;
        }
    }

    s_speaker_count = 0;
    s_parent        = nullptr;
    s_initialised   = false;

    UtilityFunctions::print("[GodotSpeakers] Shutdown");
}

extern "C" void Godot_Speakers_LoadFromEntities(const char *entity_string)
{
    if (!s_initialised) return;

    /* Clean up any existing speaker nodes */
    for (int i = 0; i < MAX_SPEAKER_ENTITIES; i++) {
        if (s_players[i]) {
            s_players[i]->stop();
            if (s_players[i]->get_parent()) {
                s_players[i]->get_parent()->remove_child(s_players[i]);
            }
            memdelete(s_players[i]);
            s_players[i] = nullptr;
        }
    }
    s_speaker_count = 0;
    memset(s_timers, 0, sizeof(s_timers));

    if (!entity_string) return;

    /* Parse the entity string */
    parse_entities(entity_string);

    if (s_speaker_count > 0) {
        UtilityFunctions::print(
            String("[GodotSpeakers] Found ") +
            String::num_int64(s_speaker_count) +
            String(" speaker entities"));
    }

    /* Create AudioStreamPlayer3D nodes for each speaker */
    for (int i = 0; i < s_speaker_count; i++) {
        godot_speaker_t *sp = &s_speakers[i];

        AudioStreamPlayer3D *player = memnew(AudioStreamPlayer3D);
        player->set_name(String("Speaker_") + String::num_int64(i));

        /* Convert id-space position to Godot coordinates */
        float gx =  sp->origin[1] * MOHAA_UNIT_SCALE;  /* id Y → Godot X */
        float gy =  sp->origin[2] * MOHAA_UNIT_SCALE;  /* id Z → Godot Y */
        float gz = -sp->origin[0] * MOHAA_UNIT_SCALE;  /* id X → Godot -Z */
        player->set_position(Vector3(gx, gy, gz));

        /* Set attenuation model */
        player->set_attenuation_model(
            AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
        player->set_max_distance(100.0f);  /* ~100m default */
        player->set_unit_size(10.0f);

        s_parent->add_child(player);
        s_players[i] = player;

        /* Load the sound and start if not triggered */
        Ref<AudioStream> stream = load_sound_from_vfs(sp->noise);
        if (stream.is_valid()) {
            player->set_stream(stream);
            if (sp->active && !sp->triggered) {
                player->play();
            }
        }
    }
}

extern "C" void Godot_Speakers_Update(float delta)
{
    if (!s_initialised) return;

    for (int i = 0; i < s_speaker_count; i++) {
        godot_speaker_t *sp = &s_speakers[i];
        if (!sp->active || !s_players[i]) continue;

        /* Handle repeating speakers */
        if (sp->wait_time > 0.0f && !s_players[i]->is_playing()) {
            s_timers[i] += delta;
            float interval = sp->wait_time;
            if (sp->random_time > 0.0f) {
                /* Add deterministic pseudo-random offset */
                float r = (float)((i * 17 + (int)(s_timers[i] * 100)) % 100) / 100.0f;
                interval += sp->random_time * r;
            }
            if (s_timers[i] >= interval) {
                s_timers[i] = 0.0f;
                s_players[i]->play();
            }
        }
    }
}

extern "C" int Godot_Speakers_GetCount(void)
{
    return s_speaker_count;
}

extern "C" void Godot_Speakers_TriggerByIndex(int index)
{
    if (index < 0 || index >= s_speaker_count) return;
    s_speakers[index].active = 1;
    if (s_players[index]) {
        s_players[index]->play();
    }
}
