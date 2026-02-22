/*
 * godot_sound_occlusion.h — Basic sound occlusion via BSP trace for the
 * OpenMoHAA GDExtension.
 *
 * Provides a line-of-sight check between the listener and a sound origin
 * using the engine's collision model (CM_BoxTrace).  Occluded sounds
 * should be attenuated by the returned factor.
 *
 * Phase 48 — Audio Completeness (optional).
 */

#ifndef GODOT_SOUND_OCCLUSION_H
#define GODOT_SOUND_OCCLUSION_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_SoundOcclusion_Check — test whether a sound origin is occluded
 * from the listener.
 *
 * @param listener_x/y/z  Listener position in id-space coordinates.
 * @param origin_x/y/z    Sound origin in id-space coordinates.
 *
 * @return  An attenuation factor in the range [0.0, 1.0]:
 *          1.0 = fully visible (no occlusion),
 *          0.3 = fully occluded (behind solid geometry).
 *          Intermediate values are not currently used — the result
 *          is binary (1.0 or OCCLUSION_FACTOR).
 */
float Godot_SoundOcclusion_Check(float listener_x, float listener_y,
                                 float listener_z,
                                 float origin_x, float origin_y,
                                 float origin_z);

/*
 * Godot_SoundOcclusion_SetEnabled — enable or disable occlusion checks.
 *
 * @param enabled  1 to enable, 0 to disable.
 *                 When disabled, Check always returns 1.0.
 */
void Godot_SoundOcclusion_SetEnabled(int enabled);

/*
 * Godot_SoundOcclusion_IsEnabled — query occlusion state.
 *
 * @return 1 if enabled, 0 if disabled.
 */
int Godot_SoundOcclusion_IsEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_SOUND_OCCLUSION_H */
