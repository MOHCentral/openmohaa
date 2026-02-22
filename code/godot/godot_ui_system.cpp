/*
 * godot_ui_system.cpp — UI rendering manager for the Godot GDExtension.
 *
 * Tracks the engine's keyCatchers flags to determine the current UI
 * state (main menu, console, loading, scoreboard, chat) and provides
 * helper queries that MoHAARunner.cpp (or the integration layer) calls
 * each frame to manage cursor visibility, input mode toggling, and
 * background rendering.
 *
 * The engine's uilib handles all menu logic internally — this module
 * only bridges state queries and rendering hints to the Godot side.
 *
 * Compiled as C++ because it may need to call C++ client accessors,
 * but all public functions use extern "C" linkage.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/*
 * We cannot include client.h here directly since this file is compiled
 * with godot-cpp defines.  Instead, use the C accessor functions from
 * godot_client_accessors.cpp and godot_renderer.c.
 */

extern "C" {

/* ── Accessors from godot_client_accessors.cpp ── */
extern int  Godot_Client_GetKeyCatchers(void);
extern int  Godot_Client_GetState(void);
extern void Godot_Client_GetMousePos(int *mx, int *my);

/* ── Accessors from godot_renderer.c ── */
extern int Godot_Renderer_GetBackground(int *cols, int *rows, int *bgr,
                                        const unsigned char **data);

} /* extern "C" */

/* ── KEYCATCH flags from q_shared.h ── */
#ifndef KEYCATCH_CONSOLE
#define KEYCATCH_CONSOLE  0x0001
#endif
#ifndef KEYCATCH_UI
#define KEYCATCH_UI       0x0002
#endif
#ifndef KEYCATCH_MESSAGE
#define KEYCATCH_MESSAGE  0x0004
#endif
#ifndef KEYCATCH_CGAME
#define KEYCATCH_CGAME    0x0008
#endif

/* ── Client connection states (from client.h connstate_t) ── */
#define CA_LOADING  8
#define CA_PRIMED   9

#include "godot_ui_system.h"

/* -------------------------------------------------------------------
 *  Internal state
 * ---------------------------------------------------------------- */

static godot_ui_state_t s_uiState     = GODOT_UI_NONE;
static int              s_mapLoading  = 0;

/* -------------------------------------------------------------------
 *  Public API
 * ---------------------------------------------------------------- */

extern "C" {

godot_ui_state_t Godot_UI_Update(void)
{
    int catchers = Godot_Client_GetKeyCatchers();
    int connState = Godot_Client_GetState();

    /* ── Loading screen takes priority ── */
    if (s_mapLoading || connState == CA_LOADING) {
        s_uiState = GODOT_UI_LOADING;
        /* Clear loading flag once we move past CA_LOADING */
        if (connState != CA_LOADING) {
            s_mapLoading = 0;
        }
        return s_uiState;
    }
    /* Once we reach PRIMED or beyond, clear loading */
    if (connState >= CA_PRIMED && s_mapLoading) {
        s_mapLoading = 0;
    }

    /* ── Console overlay ── */
    if (catchers & KEYCATCH_CONSOLE) {
        s_uiState = GODOT_UI_CONSOLE;
        return s_uiState;
    }

    /* ── UI / main menu ── */
    if (catchers & KEYCATCH_UI) {
        s_uiState = GODOT_UI_MAIN_MENU;
        return s_uiState;
    }

    /* ── Chat message input ── */
    if (catchers & KEYCATCH_MESSAGE) {
        s_uiState = GODOT_UI_MESSAGE;
        return s_uiState;
    }

    /* ── No UI active ── */
    s_uiState = GODOT_UI_NONE;
    return s_uiState;
}

godot_ui_state_t Godot_UI_GetState(void)
{
    return s_uiState;
}

int Godot_UI_IsActive(void)
{
    return (s_uiState != GODOT_UI_NONE) ? 1 : 0;
}

int Godot_UI_IsMenuActive(void)
{
    return (s_uiState == GODOT_UI_MAIN_MENU) ? 1 : 0;
}

int Godot_UI_IsConsoleActive(void)
{
    return (s_uiState == GODOT_UI_CONSOLE) ? 1 : 0;
}

int Godot_UI_IsMessageActive(void)
{
    return (s_uiState == GODOT_UI_MESSAGE) ? 1 : 0;
}

/* -------------------------------------------------------------------
 *  Background rendering helpers
 * ---------------------------------------------------------------- */

int Godot_UI_HasBackground(void)
{
    return Godot_Renderer_GetBackground(NULL, NULL, NULL, NULL);
}

int Godot_UI_GetBackgroundData(int *cols, int *rows, int *bgr,
                               const unsigned char **data)
{
    return Godot_Renderer_GetBackground(cols, rows, bgr, data);
}

/* -------------------------------------------------------------------
 *  Cursor helpers
 * ---------------------------------------------------------------- */

int Godot_UI_ShouldShowCursor(void)
{
    /* Show cursor when UI or console is active */
    int catchers = Godot_Client_GetKeyCatchers();
    if (catchers & (KEYCATCH_UI | KEYCATCH_CONSOLE | KEYCATCH_MESSAGE)) {
        return 1;
    }
    return 0;
}

void Godot_UI_GetCursorPos(int *mx, int *my)
{
    Godot_Client_GetMousePos(mx, my);
}

/* -------------------------------------------------------------------
 *  Loading screen
 * ---------------------------------------------------------------- */

void Godot_UI_OnMapLoad(void)
{
    s_mapLoading = 1;
    s_uiState = GODOT_UI_LOADING;
}

int Godot_UI_IsLoading(void)
{
    return (s_uiState == GODOT_UI_LOADING) ? 1 : 0;
}

} /* extern "C" */
