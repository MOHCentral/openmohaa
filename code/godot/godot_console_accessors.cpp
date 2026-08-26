/*
 * godot_console_accessors.cpp — C accessors for console state.
 *
 * Exposes the engine's console state (open/closed, developer console,
 * DM console) to the Godot-side UI system without requiring engine
 * header inclusion in godot-cpp translation units.
 *
 * Compiled as C++ because cl_ui.h includes C++ headers transitively.
 * All public functions use extern "C" linkage.
 */

#include "../client/client.h"
#include "../client/cl_ui.h"

extern "C" {

/* -------------------------------------------------------------------
 *  Console state queries
 * ---------------------------------------------------------------- */

/*
 * Godot_Console_IsOpen — Return 1 if the engine's console is currently
 *   visible (the MOHAA developer/DM console, not just KEYCATCH_CONSOLE).
 */
int Godot_Console_IsOpen(void)
{
    return (int)UI_ConsoleIsOpen();
}

/*
 * Godot_Console_GetKeyCatchers — Return the raw keyCatchers bitmask.
 *   Convenience wrapper so the console accessor file can be used
 *   independently of godot_client_accessors.cpp.
 */
int Godot_Console_GetKeyCatchers(void)
{
    return cls.keyCatchers;
}

/*
 * Godot_Console_IsConsoleKeyActive — Return 1 if the KEYCATCH_CONSOLE
 *   flag is set in the engine's key catchers.
 */
int Godot_Console_IsConsoleKeyActive(void)
{
    return (cls.keyCatchers & KEYCATCH_CONSOLE) ? 1 : 0;
}

} /* extern "C" */
