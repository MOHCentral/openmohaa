/*
 * godot_ui_system.h — UI rendering manager API.
 *
 * Provides the Godot-side UI state machine that tracks the engine's
 * keyCatchers flags and exposes helper queries for MoHAARunner.cpp
 * (or Agent 10's integration layer) to call each frame.
 *
 * The engine's uilib already handles menu logic, layout, widget state,
 * and serialisation.  This module ensures rendering output reaches Godot
 * correctly and input comes from Godot correctly.
 *
 * All public functions use C linkage so they can be called from both
 * C and C++ translation units.
 */

#ifndef GODOT_UI_SYSTEM_H
#define GODOT_UI_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
 *  UI state enum — derived from engine keyCatchers each frame.
 * ---------------------------------------------------------------- */

typedef enum {
    GODOT_UI_NONE       = 0,  /* Game is active, no UI overlay           */
    GODOT_UI_MAIN_MENU  = 1,  /* Main menu active (KEYCATCH_UI set)      */
    GODOT_UI_CONSOLE    = 2,  /* Console overlay  (KEYCATCH_CONSOLE set) */
    GODOT_UI_LOADING    = 3,  /* Loading screen                          */
    GODOT_UI_SCOREBOARD = 4,  /* Scoreboard overlay                      */
    GODOT_UI_MESSAGE    = 5,  /* Chat message input (KEYCATCH_MESSAGE)   */
} godot_ui_state_t;

/* -------------------------------------------------------------------
 *  Core lifecycle — called from MoHAARunner or integration layer.
 * ---------------------------------------------------------------- */

/*
 * Godot_UI_Update — Poll engine keyCatchers and update internal UI state.
 *
 * Call once per frame from _process() before input/rendering.
 * Returns the current UI state after the update.
 */
godot_ui_state_t Godot_UI_Update(void);

/*
 * Godot_UI_GetState — Return the current UI state without polling.
 */
godot_ui_state_t Godot_UI_GetState(void);

/*
 * Godot_UI_IsActive — Return 1 if any UI overlay is active (menus,
 *   console, loading, scoreboard, chat), 0 if in-game only.
 */
int Godot_UI_IsActive(void);

/*
 * Godot_UI_IsMenuActive — Return 1 if the main menu / UI system is
 *   active (KEYCATCH_UI), 0 otherwise.
 */
int Godot_UI_IsMenuActive(void);

/*
 * Godot_UI_IsConsoleActive — Return 1 if the drop-down console is
 *   active (KEYCATCH_CONSOLE), 0 otherwise.
 */
int Godot_UI_IsConsoleActive(void);

/*
 * Godot_UI_IsMessageActive — Return 1 if chat message input is
 *   active (KEYCATCH_MESSAGE), 0 otherwise.
 */
int Godot_UI_IsMessageActive(void);

/* -------------------------------------------------------------------
 *  Background rendering helpers.
 * ---------------------------------------------------------------- */

/*
 * Godot_UI_HasBackground — Return 1 if the engine has provided a
 *   background image this frame (loading screen, menu background, etc.).
 *   Delegates to Godot_Renderer_GetBackground().
 */
int Godot_UI_HasBackground(void);

/*
 * Godot_UI_GetBackgroundData — Retrieve background image parameters.
 *   Returns 1 on success, 0 if no background is active.
 *
 *   @param cols  Output: image width in pixels
 *   @param rows  Output: image height in pixels
 *   @param bgr   Output: 1 if BGR byte order, 0 if RGB
 *   @param data  Output: pointer to raw pixel data (3 bytes per pixel)
 */
int Godot_UI_GetBackgroundData(int *cols, int *rows, int *bgr,
                               const unsigned char **data);

/* -------------------------------------------------------------------
 *  Cursor helpers.
 * ---------------------------------------------------------------- */

/*
 * Godot_UI_ShouldShowCursor — Return 1 if the mouse cursor should
 *   be visible (UI or console is active), 0 for captured game mode.
 */
int Godot_UI_ShouldShowCursor(void);

/*
 * Godot_UI_GetCursorPos — Retrieve the engine's UI mouse position.
 *   Only meaningful when Godot_UI_ShouldShowCursor() returns 1.
 *
 *   @param mx  Output: horizontal cursor position in engine screen coords
 *   @param my  Output: vertical cursor position in engine screen coords
 */
void Godot_UI_GetCursorPos(int *mx, int *my);

/* -------------------------------------------------------------------
 *  Loading screen.
 * ---------------------------------------------------------------- */

/*
 * Godot_UI_OnMapLoad — Notify the UI system that a map load has
 *   started.  Sets state to GODOT_UI_LOADING until the next
 *   Godot_UI_Update() detects that loading is complete.
 */
void Godot_UI_OnMapLoad(void);

/*
 * Godot_UI_IsLoading — Return 1 if the loading screen is active.
 */
int Godot_UI_IsLoading(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_UI_SYSTEM_H */
