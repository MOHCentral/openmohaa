/*
 * godot_animation_events.h — TIKI animation event system public API.
 *
 * Provides frame-accurate sound, footstep, effect, and other events
 * triggered by TIKI animation playback.  The C accessor layer reads
 * event data from the engine's dtikianimdef_t / dtikicmd_t structures;
 * the dispatcher tracks per-entity playback state and fires events at
 * the correct frame.
 *
 * Phase 241-242 — Animation events.
 */

#ifndef GODOT_ANIMATION_EVENTS_H
#define GODOT_ANIMATION_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── C accessor API (godot_animation_event_accessors.cpp) ────────── */

/*
 * Return the number of server + client frame commands for a given
 * animation index inside the TIKI pointed to by tikiPtr.
 */
int Godot_AnimEvent_GetEventCount(void *tikiPtr, int anim_index);

/*
 * Fetch a single event's data.
 *
 *  frame_num   : TIKI_FRAME_ENTRY (-3), TIKI_FRAME_EXIT (-2),
 *                TIKI_FRAME_EVERY (-1), or >= 0 for a specific frame.
 *  type_buf    : first argument of the command, e.g. "sound", "footstep".
 *  param_buf   : remaining arguments concatenated with spaces.
 */
void Godot_AnimEvent_GetEvent(void *tikiPtr, int anim_index,
                              int event_index,
                              int *frame_num,
                              char *type_buf,  int type_buf_size,
                              char *param_buf, int param_buf_size);

/* Return the number of animations in the TIKI. */
int Godot_AnimEvent_GetAnimCount(void *tikiPtr);

/* Copy the alias name of animation anim_index into buf. */
void Godot_AnimEvent_GetAnimAlias(void *tikiPtr, int anim_index,
                                  char *buf, int buf_size);

/* ── Dispatcher API (godot_animation_events.cpp) ─────────────────── */

/* Initialise per-entity tracking state.  Call once at startup. */
void Godot_AnimEvents_Init(void);

/*
 * Advance the event state for an entity and fire any events whose
 * frame has been reached.
 *
 *  entity_index  : slot in the entity buffer (0 .. MAX-1)
 *  tikiPtr       : pointer to the entity's dtiki_t (may be NULL)
 *  anim_index    : current animation index inside the TIKI
 *  current_frame : current playback frame
 *  entity_pos    : world-space position (for 3D audio / effects)
 *
 * Call once per entity per frame.
 */
void Godot_AnimEvents_Fire(int entity_index, void *tikiPtr,
                           int anim_index, int current_frame,
                           float entity_pos_x, float entity_pos_y,
                           float entity_pos_z);

/* Reset tracking for a single entity (e.g. on entity removal). */
void Godot_AnimEvents_ResetEntity(int entity_index);

/* Shut down and free all tracking state. */
void Godot_AnimEvents_Shutdown(void);

/* ── Fired-event queue (read by MoHAARunner each frame) ──────────── */

#define GODOT_ANIM_EVENT_SOUND     0
#define GODOT_ANIM_EVENT_FOOTSTEP  1
#define GODOT_ANIM_EVENT_EFFECT    2
#define GODOT_ANIM_EVENT_BODYFALL  3
#define GODOT_ANIM_EVENT_OTHER     4

#define MAX_ANIM_FIRED_EVENTS 256

typedef struct {
    int   type;          /* GODOT_ANIM_EVENT_* */
    int   entity_index;
    float origin[3];     /* world position */
    char  param[128];    /* event-specific parameter string */
} godot_anim_fired_event_t;

/* Return the number of fired events this frame and a pointer to the array. */
int  Godot_AnimEvents_GetFiredCount(void);
const godot_anim_fired_event_t *Godot_AnimEvents_GetFiredEvents(void);

/* Clear the fired-event queue (call after draining). */
void Godot_AnimEvents_ClearFired(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_ANIMATION_EVENTS_H */
