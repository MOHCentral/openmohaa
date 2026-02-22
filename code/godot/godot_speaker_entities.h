/*
 * godot_speaker_entities.h — Speaker entity sound support for the
 * OpenMoHAA GDExtension.
 *
 * BSP maps can contain speaker entities that emit ambient sounds at
 * fixed world positions.  This module manages those speakers — parsing
 * the entity data (noise, wait, random keys) and maintaining persistent
 * AudioStreamPlayer3D nodes.
 *
 * Phase 47 — Audio Completeness.
 */

#ifndef GODOT_SPEAKER_ENTITIES_H
#define GODOT_SPEAKER_ENTITIES_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Speaker entity definition parsed from BSP entity lump.
 */
typedef struct {
    float origin[3];        /* World position in id-space coordinates */
    char  noise[256];       /* Sound file path (from "noise" key) */
    float wait_time;        /* Seconds between repeats (0 = play once) */
    float random_time;      /* Random offset added to wait_time */
    int   triggered;        /* 1 = only plays when triggered, 0 = auto */
    int   active;           /* 1 = currently playing, 0 = stopped */
} godot_speaker_t;

#define MAX_SPEAKER_ENTITIES 64

/*
 * Godot_Speakers_Init — initialise the speaker entity system.
 *
 * @param parent_node  Opaque pointer to a godot::Node3D* that will
 *                     parent the speaker AudioStreamPlayer3D nodes.
 */
void Godot_Speakers_Init(void *parent_node);

/*
 * Godot_Speakers_Shutdown — destroy all speaker nodes and free resources.
 */
void Godot_Speakers_Shutdown(void);

/*
 * Godot_Speakers_LoadFromEntities — parse BSP entity string for speakers.
 *
 * @param entity_string  The raw BSP entity lump text.
 *                       Pass NULL to clear all speakers.
 */
void Godot_Speakers_LoadFromEntities(const char *entity_string);

/*
 * Godot_Speakers_Update — per-frame update for speaker entities.
 *
 * Handles timed repeats and trigger activation.
 *
 * @param delta  Frame time in seconds.
 */
void Godot_Speakers_Update(float delta);

/*
 * Godot_Speakers_GetCount — return the number of parsed speaker entities.
 */
int Godot_Speakers_GetCount(void);

/*
 * Godot_Speakers_TriggerByIndex — activate a speaker by index.
 *
 * @param index  Speaker index (0 to count-1).
 */
void Godot_Speakers_TriggerByIndex(int index);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_SPEAKER_ENTITIES_H */
