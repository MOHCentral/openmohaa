/*
 * godot_ui_input.h — UI-specific input routing API.
 *
 * When the engine's UI system is active (KEYCATCH_UI, KEYCATCH_CONSOLE,
 * KEYCATCH_MESSAGE), input from Godot must be forwarded to the UI rather
 * than the game.  This module provides the routing logic and absolute
 * mouse position injection for menu navigation.
 *
 * All public functions use C linkage.
 */

#ifndef GODOT_UI_INPUT_H
#define GODOT_UI_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot_UI_HandleKeyEvent — Route a key event through the UI system
 *   when a UI overlay is active.
 *
 *   @param godot_key  Raw integer from InputEventKey::get_keycode()
 *   @param down       1 = pressed, 0 = released
 *   @return           1 if the event was consumed by the UI, 0 if it
 *                     should fall through to game input.
 */
int Godot_UI_HandleKeyEvent(int godot_key, int down);

/*
 * Godot_UI_HandleCharEvent — Route a character event through the UI
 *   system when a UI overlay is active (console text entry, chat, etc.).
 *
 *   @param unicode  Unicode codepoint from InputEventKey::get_unicode()
 *   @return         1 if consumed by UI, 0 otherwise.
 */
int Godot_UI_HandleCharEvent(int unicode);

/*
 * Godot_UI_HandleMouseButton — Route a mouse button event through the
 *   UI system when a UI overlay is active.
 *
 *   @param godot_button  MouseButton enum value (1=left, 2=right, etc.)
 *   @param down          1 = pressed, 0 = released
 *   @return              1 if consumed by UI, 0 otherwise.
 */
int Godot_UI_HandleMouseButton(int godot_button, int down);

/*
 * Godot_UI_HandleMouseMotion — Forward mouse movement to the UI system
 *   when a UI overlay is active.  Uses relative deltas for compatibility
 *   with the engine's mouse handling.
 *
 *   @param dx  Horizontal delta in pixels (positive = right)
 *   @param dy  Vertical delta in pixels   (positive = down)
 *   @return    1 if consumed by UI, 0 otherwise.
 */
int Godot_UI_HandleMouseMotion(int dx, int dy);

/*
 * Godot_UI_ShouldCaptureInput — Return 1 if the UI system should
 *   capture all input (preventing game from receiving it), 0 otherwise.
 *
 *   This is a convenience wrapper around Godot_UI_IsActive().
 */
int Godot_UI_ShouldCaptureInput(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_UI_INPUT_H */
