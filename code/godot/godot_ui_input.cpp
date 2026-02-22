/*
 * godot_ui_input.cpp — UI-specific input routing for the Godot GDExtension.
 *
 * When the engine's UI system is active (menus, console, chat), this
 * module routes Godot input events to the engine's UI/console handlers
 * rather than the game input system.
 *
 * The engine's existing input bridge (godot_input_bridge.c) handles the
 * low-level Godot key → engine keycode translation and event injection.
 * This module adds a decision layer on top: check UI state first, then
 * forward through the same injection path (the engine's key dispatch
 * in cl_keys.c routes events to UI_KeyEvent / Con_KeyEvent based on
 * the keyCatchers flags).
 *
 * Compiled as C++ for consistency with the accessor pattern.
 * All public functions use extern "C" linkage.
 */

#include "godot_ui_system.h"
#include "godot_ui_input.h"

extern "C" {

/* ── Input injection from godot_input_bridge.c ── */
extern int  Godot_InjectKeyEvent(int godot_key, int down);
extern void Godot_InjectCharEvent(int unicode);
extern void Godot_InjectMouseMotion(int dx, int dy);
extern void Godot_InjectMouseButton(int godot_button, int down);

} /* extern "C" */

extern "C" {

/* -------------------------------------------------------------------
 *  Key events
 * ---------------------------------------------------------------- */

int Godot_UI_HandleKeyEvent(int godot_key, int down)
{
    if (!Godot_UI_IsActive()) {
        return 0;  /* No UI active — let game handle it */
    }

    /*
     * The engine's key dispatch (CL_KeyEvent → Key_Event in cl_keys.c)
     * already checks cls.keyCatchers and routes to:
     *   - Console_Key()      when KEYCATCH_CONSOLE
     *   - UI_KeyEvent()      when KEYCATCH_UI
     *   - Message_Key()      when KEYCATCH_MESSAGE
     *   - CG_KeyEvent()      when KEYCATCH_CGAME
     *
     * We just need to inject the event; the engine does the routing.
     */
    Godot_InjectKeyEvent(godot_key, down);
    return 1;  /* Consumed by UI */
}

/* -------------------------------------------------------------------
 *  Character events (text entry)
 * ---------------------------------------------------------------- */

int Godot_UI_HandleCharEvent(int unicode)
{
    if (!Godot_UI_IsActive()) {
        return 0;
    }

    Godot_InjectCharEvent(unicode);
    return 1;
}

/* -------------------------------------------------------------------
 *  Mouse button events
 * ---------------------------------------------------------------- */

int Godot_UI_HandleMouseButton(int godot_button, int down)
{
    if (!Godot_UI_IsActive()) {
        return 0;
    }

    Godot_InjectMouseButton(godot_button, down);
    return 1;
}

/* -------------------------------------------------------------------
 *  Mouse motion events
 * ---------------------------------------------------------------- */

int Godot_UI_HandleMouseMotion(int dx, int dy)
{
    if (!Godot_UI_IsActive()) {
        return 0;
    }

    /*
     * In UI mode the engine uses SE_MOUSE events for cursor movement.
     * The engine's UI code (cl_input.c / cl_ui.cpp) accumulates mouse
     * deltas and updates the cursor position internally.
     */
    Godot_InjectMouseMotion(dx, dy);
    return 1;
}

/* -------------------------------------------------------------------
 *  Convenience query
 * ---------------------------------------------------------------- */

int Godot_UI_ShouldCaptureInput(void)
{
    return Godot_UI_IsActive();
}

} /* extern "C" */
