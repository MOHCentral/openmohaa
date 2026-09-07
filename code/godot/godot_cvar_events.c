/*
 * godot_cvar_events.c — one-shot cvar change events for Godot bridge.
 */

#include "../qcommon/q_shared.h"

static int g_fullscreen_changed_pending = 0;
static int g_fullscreen_value = 0;

/* Called from qcommon/cvar.c when r_fullscreen changes. */
void Godot_NotifyFullscreenCvarChanged(int fullscreen)
{
    g_fullscreen_value = fullscreen ? 1 : 0;
    g_fullscreen_changed_pending = 1;
}

/* One-shot consume called from MoHAARunner::_process. */
int Godot_ConsumeFullscreenCvarChanged(int *out_fullscreen)
{
    if (!g_fullscreen_changed_pending) {
        return 0;
    }

    g_fullscreen_changed_pending = 0;
    if (out_fullscreen) {
        *out_fullscreen = g_fullscreen_value;
    }
    return 1;
}