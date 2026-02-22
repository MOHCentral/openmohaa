/*
 * godot_ubersound.h — Sound alias accessor for the OpenMoHAA GDExtension.
 *
 * Thin wrapper around the engine's global alias system (code/qcommon/alias.c).
 * The cgame module loads ubersound .scr files at map load, populating the
 * global Aliases list.  This module queries that list via Alias_FindRandom()
 * and Alias_Find() — no parsing, no parallel data structures.
 *
 * Phase 45 — Audio Completeness.
 */

#ifndef GODOT_UBERSOUND_H
#define GODOT_UBERSOUND_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_Ubersound_Init — parse all ubersound .scr files from VFS.
 *
 * Scans the "ubersound/" directory for .scr files, parses each one,
 * and builds the alias lookup table.
 */
void Godot_Ubersound_Init(void);

/*
 * Godot_Ubersound_Shutdown — free all alias data.
 */
void Godot_Ubersound_Shutdown(void);

/*
 * Godot_Ubersound_Resolve — resolve a sound alias to a real filename.
 *
 * @param alias  The alias name (e.g. "snd_step_dirt").
 * @param out_path   Buffer to receive the resolved filename.
 * @param out_len    Size of the output buffer.
 * @param out_volume Receives the alias's volume override (0 = use default).
 * @param out_mindist Receives minimum distance (0 = use default).
 * @param out_maxdist Receives maximum distance (0 = use default).
 * @param out_pitch  Receives pitch override (0 = use default).
 * @param out_channel Receives the channel number (-1 = not specified).
 *
 * @return  1 if the alias was found and resolved, 0 if not.
 *          When an alias has multiple variants, one is chosen at random.
 */
int Godot_Ubersound_Resolve(const char *alias,
                            char *out_path, int out_len,
                            float *out_volume,
                            float *out_mindist,
                            float *out_maxdist,
                            float *out_pitch,
                            int   *out_channel);

/*
 * Godot_Ubersound_GetAliasCount — return the total number of aliases loaded.
 */
int Godot_Ubersound_GetAliasCount(void);

/*
 * Godot_Ubersound_IsLoaded — check whether ubersound data has been loaded.
 *
 * @return 1 if loaded, 0 if not.
 */
int Godot_Ubersound_IsLoaded(void);

/*
 * Godot_Ubersound_HasAlias — check whether a given alias exists.
 *
 * @param alias  The alias name to check.
 * @return 1 if found, 0 otherwise.
 */
int Godot_Ubersound_HasAlias(const char *alias);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_UBERSOUND_H */
