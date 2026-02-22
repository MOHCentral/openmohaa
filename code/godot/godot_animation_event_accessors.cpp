/*
 * godot_animation_event_accessors.cpp — C accessor for TIKI animation event data.
 *
 * Reads dtikianimdef_t / dtikicmd_t from the engine's parsed TIKI structures
 * and exposes the data through a flat C interface that the Godot-side
 * dispatcher (godot_animation_events.cpp) can call without pulling in
 * engine C++ headers.
 *
 * Must be .cpp because dtiki_t contains C++ members (skelChannelList_c).
 *
 * Phase 241-242 — Animation events.
 */

#include "../corepp/tiki.h"

#include <string.h>
#include <stdio.h>

/* ────────────────────────────────────────────────────────────────── */

extern "C" int Godot_AnimEvent_GetAnimCount(void *tikiPtr)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || !tiki->a) return 0;
    return tiki->a->num_anims;
}

extern "C" void Godot_AnimEvent_GetAnimAlias(void *tikiPtr, int anim_index,
                                             char *buf, int buf_size)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || !tiki->a || buf_size <= 0) return;
    buf[0] = '\0';

    if (anim_index < 0 || anim_index >= tiki->a->num_anims) return;

    dtikianimdef_t *def = tiki->a->animdefs[anim_index];
    if (!def) return;

    Q_strncpyz(buf, def->alias, buf_size);
}

/*
 * Return the total number of server + client frame commands for a
 * given animation in the TIKI.
 */
extern "C" int Godot_AnimEvent_GetEventCount(void *tikiPtr, int anim_index)
{
    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || !tiki->a) return 0;
    if (anim_index < 0 || anim_index >= tiki->a->num_anims) return 0;

    dtikianimdef_t *def = tiki->a->animdefs[anim_index];
    if (!def) return 0;

    return def->num_server_cmds + def->num_client_cmds;
}

/*
 * Fetch a single event by flat index (server commands first, then client).
 *
 * The first argument of the command is written to type_buf (e.g. "sound").
 * The remaining arguments are concatenated with spaces into param_buf.
 */
extern "C" void Godot_AnimEvent_GetEvent(void *tikiPtr, int anim_index,
                                         int event_index,
                                         int *frame_num,
                                         char *type_buf,  int type_buf_size,
                                         char *param_buf, int param_buf_size)
{
    if (type_buf  && type_buf_size  > 0) type_buf[0]  = '\0';
    if (param_buf && param_buf_size > 0) param_buf[0] = '\0';
    if (frame_num) *frame_num = 0;

    dtiki_t *tiki = (dtiki_t *)tikiPtr;
    if (!tiki || !tiki->a) return;
    if (anim_index < 0 || anim_index >= tiki->a->num_anims) return;

    dtikianimdef_t *def = tiki->a->animdefs[anim_index];
    if (!def) return;

    int total = def->num_server_cmds + def->num_client_cmds;
    if (event_index < 0 || event_index >= total) return;

    /* Pick the right command array: server cmds first, then client */
    dtikicmd_t *cmd;
    if (event_index < def->num_server_cmds) {
        cmd = &def->server_cmds[event_index];
    } else {
        cmd = &def->client_cmds[event_index - def->num_server_cmds];
    }

    if (frame_num) {
        *frame_num = cmd->frame_num;
    }

    /* First arg → type */
    if (type_buf && type_buf_size > 0 && cmd->num_args > 0 && cmd->args[0]) {
        Q_strncpyz(type_buf, cmd->args[0], type_buf_size);
    }

    /* Remaining args → param (space-separated) */
    if (param_buf && param_buf_size > 0 && cmd->num_args > 1) {
        param_buf[0] = '\0';
        int offset = 0;
        for (int i = 1; i < cmd->num_args; i++) {
            if (!cmd->args[i]) continue;
            int len = (int)strlen(cmd->args[i]);
            /* Add space separator between arguments */
            if (offset > 0 && offset + 1 < param_buf_size) {
                param_buf[offset++] = ' ';
            }
            /* Copy argument, leave room for null terminator */
            int avail = param_buf_size - offset - 1;
            if (avail <= 0) break;
            int copy = (len < avail) ? len : avail;
            memcpy(param_buf + offset, cmd->args[i], copy);
            offset += copy;
        }
        param_buf[offset] = '\0';
    }
}
