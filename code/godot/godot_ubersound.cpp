/*
 * godot_ubersound.cpp — Sound alias accessor for the OpenMoHAA GDExtension.
 *
 * Thin wrapper around the engine's global alias system (code/qcommon/alias.c).
 * The cgame module loads ubersound .scr files at map load via
 * CG_RegisterSounds() → CG_Command_ProcessFile() → cgi.Alias_Add(), which
 * populates the global Aliases list.  This module simply queries that list
 * using Alias_FindRandom() / Alias_Find() / Alias_GetGlobalList() — no
 * parsing, no parallel data structures.
 *
 * Phase 45 — Audio Completeness.
 */

#include "godot_ubersound.h"

#include <cstring>

/* ------------------------------------------------------------------ */
/*  Engine alias API — declared via extern "C" to avoid header        */
/*  conflicts between engine headers and godot-cpp.                   */
/*  Definitions live in code/qcommon/alias.c (linked into main .so).  */
/* ------------------------------------------------------------------ */

/* Mirror the engine's AliasListNode_t (from alias.h) without including
 * it — the struct layout must match exactly. */
struct AliasListNode_s {
    char  alias_name[40];
    char  real_name[128];
    float weight;

    unsigned char        stop_flag;
    struct AliasListNode_s *next;

    float pitch;
    float volume;
    float pitchMod;
    float volumeMod;
    float dist;
    float maxDist;
    int   channel;
    int   streamed;
    int   forcesubtitle;   /* qboolean = int */
    char *subtitle;
};

struct AliasList_s {
    char                    name[40];
    int                     dirty;     /* qboolean = int */
    int                     num_in_list;
    struct AliasListNode_s **sorted_list;
    struct AliasListNode_s  *data_list;
};

extern "C" {
    const char            *Alias_Find(const char *alias);
    const char            *Alias_FindRandom(const char *alias, struct AliasListNode_s **ret);
    struct AliasList_s    *Alias_GetGlobalList(void);
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

extern "C" void Godot_Ubersound_Init(void)
{
    /* No-op — aliases are loaded by cgame at map load via cgi.Alias_Add().
     * The engine's global Aliases list is populated before any Resolve()
     * calls reach us. */
}

extern "C" void Godot_Ubersound_Shutdown(void)
{
    /* No-op — alias memory is owned by the engine (Z_Malloc'd).
     * Alias_Clear() is called by the engine during map change. */
}

extern "C" int Godot_Ubersound_Resolve(const char *alias,
                                       char *out_path, int out_len,
                                       float *out_volume,
                                       float *out_mindist,
                                       float *out_maxdist,
                                       float *out_pitch,
                                       int   *out_channel)
{
    if (!alias || !alias[0]) return 0;

    struct AliasListNode_s *node = nullptr;
    const char *real_name = Alias_FindRandom(alias, &node);
    if (!real_name || !real_name[0]) return 0;

    if (out_path && out_len > 0) {
        strncpy(out_path, real_name, (size_t)(out_len - 1));
        out_path[out_len - 1] = '\0';
    }

    if (node) {
        if (out_volume)  *out_volume  = node->volume;
        if (out_mindist) *out_mindist = node->dist;
        if (out_maxdist) *out_maxdist = node->maxDist;
        if (out_pitch)   *out_pitch   = node->pitch;
        if (out_channel) *out_channel = node->channel;
    } else {
        if (out_volume)  *out_volume  = 0.0f;
        if (out_mindist) *out_mindist = 0.0f;
        if (out_maxdist) *out_maxdist = 0.0f;
        if (out_pitch)   *out_pitch   = 0.0f;
        if (out_channel) *out_channel = -1;
    }

    return 1;
}

extern "C" int Godot_Ubersound_GetAliasCount(void)
{
    struct AliasList_s *list = Alias_GetGlobalList();
    if (!list) return 0;
    return list->num_in_list;
}

extern "C" int Godot_Ubersound_IsLoaded(void)
{
    struct AliasList_s *list = Alias_GetGlobalList();
    return (list && list->num_in_list > 0) ? 1 : 0;
}

extern "C" int Godot_Ubersound_HasAlias(const char *alias)
{
    if (!alias || !alias[0]) return 0;
    const char *result = Alias_Find(alias);
    return (result && result[0]) ? 1 : 0;
}
