/*
 * godot_music.h — Music playback manager for the OpenMoHAA GDExtension.
 *
 * Manages music playback using Godot AudioStreamPlayer + AudioStreamMP3.
 * Loads music files from the engine VFS (sound/music/*.mp3) and handles
 * the MOHAA music state machine: play, stop, crossfade, volume control,
 * current/fallback mood tracks, and triggered music.
 *
 * All music state is read from godot_sound.c via the Godot_Sound_GetMusic*()
 * and Godot_Sound_GetTriggered*() accessor functions.
 *
 * Phase 42 — Audio Completeness.
 */

#ifndef GODOT_MUSIC_H
#define GODOT_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_Music_Init — initialise the music manager.
 *
 * @param parent_node  Opaque pointer to a godot::Node* that will parent
 *                     the internal AudioStreamPlayer nodes.  Pass the
 *                     MoHAARunner node (cast to void*).
 */
void Godot_Music_Init(void *parent_node);

/*
 * Godot_Music_Shutdown — shut down the music manager and free resources.
 */
void Godot_Music_Shutdown(void);

/*
 * Godot_Music_Update — per-frame update.
 *
 * Reads music state from godot_sound.c, handles play/stop/crossfade/
 * volume transitions, and updates the triggered-music player.
 *
 * @param delta  Frame time in seconds.
 */
void Godot_Music_Update(float delta);

/*
 * Godot_Music_SetVolume — set the master music volume.
 *
 * @param volume  Volume in the range 0.0–1.0.
 */
void Godot_Music_SetVolume(float volume);

/*
 * Godot_Music_IsPlaying — check whether music is currently playing.
 *
 * @return 1 if music is playing, 0 otherwise.
 */
int Godot_Music_IsPlaying(void);

/*
 * Godot_Music_GetCurrentTrack — get the name of the current track.
 *
 * @return  Pointer to a static buffer with the current track name,
 *          or an empty string if nothing is playing.
 */
const char *Godot_Music_GetCurrentTrack(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_MUSIC_H */
