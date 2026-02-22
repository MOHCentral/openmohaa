/*
 * godot_animation_events.cpp — TIKI animation event dispatcher.
 *
 * Tracks per-entity animation playback state and fires events (sound,
 * footstep, effect, bodyfall, etc.) when the current frame crosses
 * the event's trigger frame.  Fired events are queued in a fixed-size
 * buffer that MoHAARunner.cpp drains each frame.
 *
 * Phase 241-242 — Animation events.
 */

#include "godot_animation_events.h"

#include <string.h>
#include <stdio.h>

/* ── Per-entity tracking state ─────────────────────────────────────── */

#define MAX_ANIM_ENTITIES 1024

typedef struct {
    int  current_anim;      /* animation index last seen         */
    int  last_fired_frame;  /* highest frame for which we fired  */
    int  entry_fired;       /* 1 if entry events already fired   */
    void *tiki_ptr;         /* dtiki_t * last seen (for change)  */
} anim_event_state_t;

static anim_event_state_t g_anim_state[MAX_ANIM_ENTITIES];
static int                g_anim_inited = 0;

/* ── Fired-event output queue ──────────────────────────────────────── */

static godot_anim_fired_event_t g_fired[MAX_ANIM_FIRED_EVENTS];
static int                      g_fired_count = 0;

/* ── Helpers ───────────────────────────────────────────────────────── */

static int classify_event_type(const char *type_str)
{
    if (!type_str || !type_str[0]) return GODOT_ANIM_EVENT_OTHER;

    if (strcmp(type_str, "sound")        == 0) return GODOT_ANIM_EVENT_SOUND;
    if (strcmp(type_str, "stopsound")    == 0) return GODOT_ANIM_EVENT_SOUND;
    if (strcmp(type_str, "soundonly")     == 0) return GODOT_ANIM_EVENT_SOUND;
    if (strcmp(type_str, "footstep")     == 0) return GODOT_ANIM_EVENT_FOOTSTEP;
    if (strcmp(type_str, "effect")       == 0) return GODOT_ANIM_EVENT_EFFECT;
    if (strcmp(type_str, "tagspawn")     == 0) return GODOT_ANIM_EVENT_EFFECT;
    if (strcmp(type_str, "tagspawnlinked") == 0) return GODOT_ANIM_EVENT_EFFECT;
    if (strcmp(type_str, "bodyfall")     == 0) return GODOT_ANIM_EVENT_BODYFALL;

    return GODOT_ANIM_EVENT_OTHER;
}

static void push_fired_event(int type, int entity_index,
                             float x, float y, float z,
                             const char *param)
{
    if (g_fired_count >= MAX_ANIM_FIRED_EVENTS) return;

    godot_anim_fired_event_t *ev = &g_fired[g_fired_count++];
    ev->type         = type;
    ev->entity_index = entity_index;
    ev->origin[0]    = x;
    ev->origin[1]    = y;
    ev->origin[2]    = z;

    if (param && param[0]) {
        int len = (int)strlen(param);
        if (len >= (int)sizeof(ev->param)) len = (int)sizeof(ev->param) - 1;
        memcpy(ev->param, param, len);
        ev->param[len] = '\0';
    } else {
        ev->param[0] = '\0';
    }
}

/*
 * Process a single TIKI animation command and push a fired event if
 * the command type is recognised.
 */
static void fire_single_event(int entity_index,
                              float pos_x, float pos_y, float pos_z,
                              void *tikiPtr, int anim_index, int event_index)
{
    int  frame_num = 0;
    char type_buf[64];
    char param_buf[128];

    Godot_AnimEvent_GetEvent(tikiPtr, anim_index, event_index,
                             &frame_num, type_buf, sizeof(type_buf),
                             param_buf, sizeof(param_buf));

    int type = classify_event_type(type_buf);
    push_fired_event(type, entity_index, pos_x, pos_y, pos_z, param_buf);
}

/* ── Public API ────────────────────────────────────────────────────── */

void Godot_AnimEvents_Init(void)
{
    memset(g_anim_state, 0, sizeof(g_anim_state));
    /* Initialise to -1 so the first frame always detects a change */
    for (int i = 0; i < MAX_ANIM_ENTITIES; i++) {
        g_anim_state[i].current_anim    = -1;
        g_anim_state[i].last_fired_frame = -1;
        g_anim_state[i].entry_fired     = 0;
        g_anim_state[i].tiki_ptr        = NULL;
    }
    g_fired_count = 0;
    g_anim_inited = 1;
}

void Godot_AnimEvents_ResetEntity(int entity_index)
{
    if (entity_index < 0 || entity_index >= MAX_ANIM_ENTITIES) return;

    g_anim_state[entity_index].current_anim     = -1;
    g_anim_state[entity_index].last_fired_frame  = -1;
    g_anim_state[entity_index].entry_fired       = 0;
    g_anim_state[entity_index].tiki_ptr          = NULL;
}

void Godot_AnimEvents_Fire(int entity_index, void *tikiPtr,
                           int anim_index, int current_frame,
                           float entity_pos_x, float entity_pos_y,
                           float entity_pos_z)
{
    if (!g_anim_inited) return;
    if (entity_index < 0 || entity_index >= MAX_ANIM_ENTITIES) return;
    if (!tikiPtr) return;

    anim_event_state_t *st = &g_anim_state[entity_index];

    int event_count = Godot_AnimEvent_GetEventCount(tikiPtr, anim_index);
    if (event_count <= 0) {
        /* Update tracking even if there are no events */
        st->current_anim     = anim_index;
        st->last_fired_frame = current_frame;
        st->tiki_ptr         = tikiPtr;
        return;
    }

    /*
     * Detect animation change: if the anim index or TIKI changed,
     * fire exit events for the old animation, then entry events for
     * the new one.
     */
    int anim_changed = (st->current_anim != anim_index ||
                        st->tiki_ptr     != tikiPtr);

    if (anim_changed) {
        /* ── Fire EXIT events on the OLD animation ── */
        if (st->tiki_ptr && st->current_anim >= 0) {
            int old_count = Godot_AnimEvent_GetEventCount(st->tiki_ptr,
                                                          st->current_anim);
            for (int i = 0; i < old_count; i++) {
                int  fn = 0;
                char tb[64], pb[128];
                Godot_AnimEvent_GetEvent(st->tiki_ptr, st->current_anim, i,
                                         &fn, tb, sizeof(tb), pb, sizeof(pb));
                if (fn == -2) {  /* TIKI_FRAME_EXIT */
                    int type = classify_event_type(tb);
                    push_fired_event(type, entity_index,
                                     entity_pos_x, entity_pos_y,
                                     entity_pos_z, pb);
                }
            }
        }

        /* Reset state for the new animation */
        st->current_anim     = anim_index;
        st->last_fired_frame = -1;
        st->entry_fired      = 0;
        st->tiki_ptr         = tikiPtr;
    }

    /* ── Fire ENTRY events (once per animation start) ── */
    if (!st->entry_fired) {
        for (int i = 0; i < event_count; i++) {
            int  fn = 0;
            char tb[64], pb[128];
            Godot_AnimEvent_GetEvent(tikiPtr, anim_index, i,
                                     &fn, tb, sizeof(tb), pb, sizeof(pb));
            if (fn == -3) {  /* TIKI_FRAME_ENTRY */
                int type = classify_event_type(tb);
                push_fired_event(type, entity_index,
                                 entity_pos_x, entity_pos_y,
                                 entity_pos_z, pb);
            }
        }
        st->entry_fired = 1;
    }

    /* ── Fire EVERY-frame events ── */
    for (int i = 0; i < event_count; i++) {
        int  fn = 0;
        char tb[64], pb[128];
        Godot_AnimEvent_GetEvent(tikiPtr, anim_index, i,
                                 &fn, tb, sizeof(tb), pb, sizeof(pb));
        if (fn == -1) {  /* TIKI_FRAME_EVERY */
            int type = classify_event_type(tb);
            push_fired_event(type, entity_index,
                             entity_pos_x, entity_pos_y,
                             entity_pos_z, pb);
        }
    }

    /* ── Fire frame-specific events ── */
    int last = st->last_fired_frame;

    if (current_frame < last) {
        /*
         * Animation has looped — fire events from (last+1) to the end
         * of the previous cycle, then from 0 to current_frame.
         * We only fire events <= current_frame in the new cycle.
         */
        for (int i = 0; i < event_count; i++) {
            int  fn = 0;
            char tb[64], pb[128];
            Godot_AnimEvent_GetEvent(tikiPtr, anim_index, i,
                                     &fn, tb, sizeof(tb), pb, sizeof(pb));
            if (fn >= 0) {
                /* Frames past the old position (tail of previous cycle) */
                if (fn > last) {
                    fire_single_event(entity_index,
                                      entity_pos_x, entity_pos_y,
                                      entity_pos_z,
                                      tikiPtr, anim_index, i);
                }
                /* Frames in the new cycle up to current */
                else if (fn <= current_frame) {
                    fire_single_event(entity_index,
                                      entity_pos_x, entity_pos_y,
                                      entity_pos_z,
                                      tikiPtr, anim_index, i);
                }
            }
        }
    } else {
        /* Normal forward playback */
        for (int i = 0; i < event_count; i++) {
            int  fn = 0;
            char tb[64], pb[128];
            Godot_AnimEvent_GetEvent(tikiPtr, anim_index, i,
                                     &fn, tb, sizeof(tb), pb, sizeof(pb));
            if (fn >= 0 && fn <= current_frame && fn > last) {
                fire_single_event(entity_index,
                                  entity_pos_x, entity_pos_y,
                                  entity_pos_z,
                                  tikiPtr, anim_index, i);
            }
        }
    }

    st->last_fired_frame = current_frame;
}

void Godot_AnimEvents_Shutdown(void)
{
    memset(g_anim_state, 0, sizeof(g_anim_state));
    g_fired_count = 0;
    g_anim_inited = 0;
}

/* ── Fired-event queue accessors ───────────────────────────────────── */

int Godot_AnimEvents_GetFiredCount(void)
{
    return g_fired_count;
}

const godot_anim_fired_event_t *Godot_AnimEvents_GetFiredEvents(void)
{
    return g_fired;
}

void Godot_AnimEvents_ClearFired(void)
{
    g_fired_count = 0;
}
