/*
 * godot_sound_occlusion.c — Basic sound occlusion via BSP trace for the
 * OpenMoHAA GDExtension.
 *
 * Uses CM_BoxTrace (point trace) to check line-of-sight between the
 * listener position and a sound origin.  If the trace hits solid
 * geometry, the sound is considered occluded and should be attenuated.
 *
 * This is a deliberately simple implementation: binary occluded/not
 * with a fixed attenuation factor of 0.3.  More sophisticated multi-
 * ray or material-based approaches are left for future work.
 *
 * Phase 48 — Audio Completeness (optional).
 */

#include "godot_sound_occlusion.h"

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/cm_public.h"

/* Attenuation factor applied to fully occluded sounds. */
#define OCCLUSION_FACTOR 0.3f

static int s_occlusion_enabled = 0;

float Godot_SoundOcclusion_Check(float listener_x, float listener_y,
                                 float listener_z,
                                 float origin_x, float origin_y,
                                 float origin_z)
{
    if (!s_occlusion_enabled) return 1.0f;

    vec3_t start, end, zero;
    trace_t trace;

    start[0] = listener_x;
    start[1] = listener_y;
    start[2] = listener_z;
    end[0]   = origin_x;
    end[1]   = origin_y;
    end[2]   = origin_z;
    zero[0]  = zero[1] = zero[2] = 0;

    /* Point trace (zero-size box) through the world model (handle 0)
     * using CONTENTS_SOLID as the brush mask. */
    CM_BoxTrace(&trace, start, end, zero, zero, 0, CONTENTS_SOLID, 0);

    if (trace.fraction < 1.0f) {
        /* Trace hit something — sound is occluded */
        return OCCLUSION_FACTOR;
    }

    return 1.0f;
}

void Godot_SoundOcclusion_SetEnabled(int enabled)
{
    s_occlusion_enabled = enabled ? 1 : 0;
}

int Godot_SoundOcclusion_IsEnabled(void)
{
    return s_occlusion_enabled;
}
