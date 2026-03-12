/*
 * godot_client_accessors.cpp — Thin accessors for client-side state.
 *
 * Compiled as C++ to access cl_ui.h (which uses C++ bool).
 * All public functions use extern "C" linkage so MoHAARunner.cpp
 * can call them from its own extern "C" block.
 */

#include "../client/client.h"
#include "../client/cl_ui.h"

/* For accessing the paused cvar */
extern cvar_t *paused;

extern "C" {

int Godot_Client_GetState(void) {
    return (int)clc.state;
}

int Godot_Client_GetKeyCatchers(void) {
    return cls.keyCatchers;
}

int Godot_Client_GetGuiMouse(void) {
    return (int)in_guimouse;
}

int Godot_Client_GetStartStage(void) {
    return cls.startStage;
}

void Godot_Client_GetMousePos(int *mx, int *my) {
    if (mx) *mx = cl.mousex;
    if (my) *my = cl.mousey;
}

int Godot_Client_GetPaused(void) {
    return paused ? paused->integer : 0;
}

/*
 * Force the client into game-input mode by clearing the UI key catcher,
 * disabling GUI mouse, dismissing any open menus, and unpausing.
 * Called from MoHAARunner after map load and periodically during gameplay.
 *
 * Order matters: UI_ForceMenuOff → UI_FocusMenuIfExists may re-enable
 * in_guimouse via IN_MouseOn() if a persistent menu remains.
 * We call IN_MouseOff() last to guarantee freelook mode.
 */
void Godot_Client_SetGameInputMode(void) {
    cls.keyCatchers &= ~(KEYCATCH_UI | KEYCATCH_CONSOLE);
    UI_ForceMenuOff(true);
    /* IN_MouseOff sets in_guimouse = qfalse — must come AFTER
       UI_ForceMenuOff which may call IN_MouseOn internally.
       This ensures mouse look works in game mode. */
    IN_MouseOff();

    /* Force unpause — in single-player listen-server mode the engine
       may auto-pause when the UI/console was active. */
    if (paused && paused->integer) {
        Cvar_Set("paused", "0");
    }
}

void Godot_Client_SetKeyCatchers(int catchers) {
    cls.keyCatchers = catchers;
}

void Godot_Client_ForceUnpause(void) {
    if (paused && paused->integer) {
        Cvar_Set("paused", "0");
    }
    IN_MouseOff();
}

/* -------------------------------------------------------------------
 *  Phase 46–55: UI system accessors
 * ---------------------------------------------------------------- */

/*
 * Godot_Client_IsUIActive — Return 1 if the engine's UI system is
 *   currently capturing input (KEYCATCH_UI flag set).
 */
int Godot_Client_IsUIActive(void) {
    return (cls.keyCatchers & KEYCATCH_UI) ? 1 : 0;
}

/*
 * Godot_Client_IsConsoleVisible — Return 1 if the developer console
 *   (fakk_console / UIFloatingConsole, toggled by ~) is visible.
 *   MOHAA's console operates under KEYCATCH_UI (not KEYCATCH_CONSOLE),
 *   so we must check UI_ConsoleIsVisible() rather than the catcher flag.
 */
int Godot_Client_IsConsoleVisible(void) {
    return UI_ConsoleIsVisible() ? 1 : 0;
}

/*
 * Godot_Client_IsMessageActive — Return 1 if the chat message input
 *   is active (KEYCATCH_MESSAGE flag set).
 */
int Godot_Client_IsMessageActive(void) {
    return (cls.keyCatchers & KEYCATCH_MESSAGE) ? 1 : 0;
}

/*
 * Godot_Client_GetUIMousePos — Retrieve the engine's UI mouse
 *   coordinates.  These are the same as cl.mousex/cl.mousey but
 *   with a semantic name for UI usage.
 */
void Godot_Client_GetUIMousePos(int *mx, int *my) {
    if (mx) *mx = cl.mousex;
    if (my) *my = cl.mousey;
}

/*
 * Godot_Client_IsAnyOverlayActive — Return 1 if any overlay is
 *   capturing input (UI, console, or message mode).
 *   KEYCATCH_CGAME is deliberately excluded: it is always set during
 *   gameplay and indicates that cgame is loaded, NOT that an overlay
 *   is active.  Including it made this function always return true,
 *   which broke overlay detection, safety-net KEYCATCH_UI clearing,
 *   and mouse sync.
 */
int Godot_Client_IsAnyOverlayActive(void) {
    return (cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) ? 1 : 0;
}

/*
 * Godot_Client_SyncGuiMouseToOverlayState — Keep in_guimouse aligned with
 *   overlay catcher state. Some web paths can leave in_guimouse stale even
 *   when KEYCATCH_UI is active, which breaks hover/click hit testing.
 *
 *   IMPORTANT: Only force IN_MouseOn() when a mouse-interactive overlay is
 *   actually visible (menu or developer console).  The DM console (chat)
 *   sets KEYCATCH_UI but intentionally calls IN_MouseOff() because it is
 *   keyboard-only.  Overriding that causes CL_GetMouseState() to return
 *   cl.mouseButtons, which makes ServiceEvents() think a mouse click
 *   occurred, which activates view3d and deactivates dm_console — closing
 *   chat instantly.
 */
void Godot_Client_SyncGuiMouseToOverlayState(void) {
    if (cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) {
        /* Only force mouse ON when an overlay that needs the cursor is up.
         * UI_MenuUp() covers ESC/options menus.
         * UI_ConsoleIsVisible() covers the developer console.
         * The DM console (chat) is keyboard-only — leave in_guimouse as
         * the engine set it (false). */
        if (UI_MenuUp() || UI_ConsoleIsVisible()) {
            IN_MouseOn();
        }
        /* If KEYCATCH_UI is set but no menu/console is visible (DM console),
         * do NOT override in_guimouse — the engine set it intentionally. */
    } else {
        IN_MouseOff();
    }
}

/*
 * Godot_Client_SetMousePos — Set the engine's UI cursor position directly.
 *   Used when transitioning to UI mode to place the cursor at a sensible
 *   position (e.g. centre of screen) instead of (0,0).
 */
void Godot_Client_SetMousePos(int x, int y) {
    cl.mousex = x;
    cl.mousey = y;
}

/*
 * Godot_Client_IsUIStarted — Return 1 if the UI system has been
 *   initialised (CL_InitializeUI completed).
 */
int Godot_Client_IsUIStarted(void) {
    return cls.uiStarted ? 1 : 0;
}

/*
 * Godot_Client_IsMenuUp — Return 1 if a menu is currently on the stack.
 *   Wraps UI_MenuUp() for the Godot side.
 */
int Godot_Client_IsMenuUp(void) {
    return UI_MenuUp() ? 1 : 0;
}

/*
 * Godot_Client_GetKeyBinding — Return the binding string for an engine
 *   keynum.  Returns "" if no binding.  Caller must NOT free the pointer.
 */
const char *Godot_Client_GetKeyBinding(int keynum) {
    if (keynum < 0 || keynum >= MAX_KEYS) return "";
    if (!keys[keynum].binding) return "";
    return keys[keynum].binding;
}

/*
 * Godot_Client_GetMouseButtons — Return the current cl.mouseButtons bitmask.
 */
int Godot_Client_GetMouseButtons(void) {
    return cl.mouseButtons;
}

/* Player zoom state — returns STAT_INZOOM from playerState_t.
 * 0 = not zooming, >0 = zoom FOV value. Used by the Godot side
 * to hide first-person viewmodel during weapon zoom. */
int Godot_Client_GetPlayerZoom(void) {
    return cl.snap.ps.stats[8]; /* STAT_INZOOM = 8 in bg_public.h enum */
}

/* Sync cls.glconfig.vidWidth/vidHeight with the actual Godot viewport.
 * Under Godot there is no physical display-mode switch — the viewport
 * is always at the window's real resolution.  This keeps the engine's
 * UI framework (uid.vidWidth/vidHeight, SCR_AdjustFrom640, widget
 * Realign) in sync with the real viewport dimensions. */
void Godot_Client_SyncGlConfigVidSize(int w, int h) {
    if (w > 0 && h > 0) {
        cls.glconfig.vidWidth     = w;
        cls.glconfig.vidHeight    = h;
        cls.glconfig.windowAspect = (float)w / (float)h;
    }
}

/* Refresh uid.vidWidth/vidHeight from cls.glconfig.
 * Must be called after syncing cls.glconfig so that the UI widget
 * framework (SetVirtualScale, Realign) sees the correct dimensions. */
void Godot_Client_RefreshUIDef(void) {
    CL_FillUIDef();
}

/* Run the same UI + cgame resolution-update path used by OpenMoHAA
 * after a video mode change (CL_SetVidMode / CL_SetFullscreen).
 *
 * This rebuilds scaled widget geometry, menu frames, HUD layout, AND
 * notifies cgame.so so that cgs.glconfig / cgs.uiHiResScale are
 * refreshed.  Without the cgame notification the crosshair, compass,
 * and all CG_AdjustFrom640-based draws use stale dimensions. */
void Godot_Client_ResolutionChange(void) {
    /* Update renderer stored config so re.SetMode path is consistent */
    re.SetMode(-1, &cls.glconfig);

    /* Notify cgame (crosshair, HUD, scope overlay, etc.) */
    if (cge) {
        cge->CG_GetRendererConfig();
    }

    /* Rebuild UI widget geometry, menu frames, HUD layout */
    if (cls.uiStarted) {
        UI_ResolutionChange();
    } else {
        CL_FillUIDef();
    }
}

/* Player health — STAT_HEALTH (index 0) from playerState_t.stats[].
 * Returns 0 when dead, negative when not yet valid. */
int Godot_Client_GetPlayerHealth(void) {
    return cl.snap.ps.stats[0]; /* STAT_HEALTH = 0 */
}

/* Full-screen blend colour from playerState_t.blend[4].
 * The engine sets this for damage flash (red), underwater tint (blue-green),
 * etc.  Values are in [0..1] RGBA. */
void Godot_Client_GetScreenBlend(float *r, float *g, float *b, float *a) {
    *r = cl.snap.ps.blend[0];
    *g = cl.snap.ps.blend[1];
    *b = cl.snap.ps.blend[2];
    *a = cl.snap.ps.blend[3];
}

} /* extern "C" */
