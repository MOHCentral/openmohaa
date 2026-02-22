/*
 * godot_input_bridge.c — Thin C bridge for injecting Godot input events
 *                        into the engine's event queue.
 *
 * This file can include engine headers (qcommon.h, keycodes.h) without
 * conflicting with godot-cpp C++ headers.  MoHAARunner.cpp calls these
 * functions via extern "C" declarations.
 *
 * Flow:  Godot _unhandled_input() → MoHAARunner.cpp → these C wrappers
 *        → Com_QueueEvent() → engine event loop.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/keycodes.h"

/* ====================================================================
 *  Godot key constant values (from godot-cpp global_constants.hpp)
 *  These are stable GDExtension ABI values that won't change.
 * ==================================================================== */

#define GK_SPECIAL          4194304

/* Special keys — offset from GK_SPECIAL */
#define GK_ESCAPE           4194305
#define GK_TAB              4194306
#define GK_BACKTAB          4194307
#define GK_BACKSPACE        4194308
#define GK_ENTER            4194309
#define GK_KP_ENTER         4194310
#define GK_INSERT           4194311
#define GK_DELETE           4194312
#define GK_PAUSE            4194313
#define GK_PRINT            4194314
#define GK_SYSREQ           4194315
#define GK_CLEAR            4194316
#define GK_HOME             4194317
#define GK_END              4194318
#define GK_LEFT             4194319
#define GK_UP               4194320
#define GK_RIGHT            4194321
#define GK_DOWN             4194322
#define GK_PAGEUP           4194323
#define GK_PAGEDOWN         4194324
#define GK_SHIFT            4194325
#define GK_CTRL             4194326
#define GK_META             4194327
#define GK_ALT              4194328
#define GK_CAPSLOCK         4194329
#define GK_NUMLOCK          4194330
#define GK_SCROLLLOCK       4194331

/* F keys */
#define GK_F1               4194332
#define GK_F2               4194333
#define GK_F3               4194334
#define GK_F4               4194335
#define GK_F5               4194336
#define GK_F6               4194337
#define GK_F7               4194338
#define GK_F8               4194339
#define GK_F9               4194340
#define GK_F10              4194341
#define GK_F11              4194342
#define GK_F12              4194343
#define GK_F13              4194344
#define GK_F14              4194345
#define GK_F15              4194346

/* Keypad */
#define GK_KP_MULTIPLY      4194433
#define GK_KP_DIVIDE        4194434
#define GK_KP_SUBTRACT      4194435
#define GK_KP_PERIOD        4194436
#define GK_KP_ADD           4194437
#define GK_KP_0             4194438
#define GK_KP_1             4194439
#define GK_KP_2             4194440
#define GK_KP_3             4194441
#define GK_KP_4             4194442
#define GK_KP_5             4194443
#define GK_KP_6             4194444
#define GK_KP_7             4194445
#define GK_KP_8             4194446
#define GK_KP_9             4194447

/* Godot mouse button indices (MouseButton enum) */
#define GMB_LEFT            1
#define GMB_RIGHT           2
#define GMB_MIDDLE          3
#define GMB_WHEEL_UP        4
#define GMB_WHEEL_DOWN      5
#define GMB_WHEEL_LEFT      6
#define GMB_WHEEL_RIGHT     7
#define GMB_XBUTTON1        8
#define GMB_XBUTTON2        9


/* ====================================================================
 *  Godot Key → engine keyNum_t translation
 *
 *  Mirrors the logic in sdl_input.c IN_TranslateSDLToQ3Key().
 * ==================================================================== */

static int godot_key_to_engine(int gk)
{
    /* ── Console toggle: backtick / tilde ── */
    if (gk == 96)   /* '`' — KEY_QUOTELEFT */
        return K_CONSOLE;
    if (gk == 126)  /* '~' — KEY_ASCIITILDE */
        return K_CONSOLE;

    /* ── Printable ASCII (letters as lowercase) ── */
    if (gk >= 'A' && gk <= 'Z')
        return gk + 32;               /* Engine expects lowercase */
    if (gk >= 32 && gk < 127)
        return gk;                    /* space, digits, punctuation */

    /* ── Special keys ── */
    switch (gk) {
    case GK_ESCAPE:     return K_ESCAPE;
    case GK_TAB:        return K_TAB;
    case GK_BACKTAB:    return K_TAB;      /* Shift+Tab still sends Tab */
    case GK_BACKSPACE:  return K_BACKSPACE;
    case GK_ENTER:      return K_ENTER;
    case GK_KP_ENTER:   return K_KP_ENTER;
    case GK_INSERT:     return K_INS;
    case GK_DELETE:     return K_DEL;
    case GK_PAUSE:      return K_PAUSE;
    case GK_PRINT:      return K_PRINT;
    case GK_SYSREQ:     return K_SYSREQ;
    case GK_HOME:       return K_HOME;
    case GK_END:        return K_END;
    case GK_LEFT:       return K_LEFTARROW;
    case GK_UP:         return K_UPARROW;
    case GK_RIGHT:      return K_RIGHTARROW;
    case GK_DOWN:       return K_DOWNARROW;
    case GK_PAGEUP:     return K_PGUP;
    case GK_PAGEDOWN:   return K_PGDN;
    case GK_SHIFT:      return K_SHIFT;
    case GK_CTRL:       return K_CTRL;
    case GK_ALT:        return K_ALT;
    case GK_META:       return K_SUPER;
    case GK_CAPSLOCK:   return K_CAPSLOCK;
    case GK_NUMLOCK:    return K_KP_NUMLOCK;
    case GK_SCROLLLOCK: return K_SCROLLOCK;

    /* ── F keys ── */
    case GK_F1:   return K_F1;
    case GK_F2:   return K_F2;
    case GK_F3:   return K_F3;
    case GK_F4:   return K_F4;
    case GK_F5:   return K_F5;
    case GK_F6:   return K_F6;
    case GK_F7:   return K_F7;
    case GK_F8:   return K_F8;
    case GK_F9:   return K_F9;
    case GK_F10:  return K_F10;
    case GK_F11:  return K_F11;
    case GK_F12:  return K_F12;
    case GK_F13:  return K_F13;
    case GK_F14:  return K_F14;
    case GK_F15:  return K_F15;

    /* ── Keypad (numlock off → named keys, matches SDL mapping) ── */
    case GK_KP_0:         return K_KP_INS;
    case GK_KP_1:         return K_KP_END;
    case GK_KP_2:         return K_KP_DOWNARROW;
    case GK_KP_3:         return K_KP_PGDN;
    case GK_KP_4:         return K_KP_LEFTARROW;
    case GK_KP_5:         return K_KP_5;
    case GK_KP_6:         return K_KP_RIGHTARROW;
    case GK_KP_7:         return K_KP_HOME;
    case GK_KP_8:         return K_KP_UPARROW;
    case GK_KP_9:         return K_KP_PGUP;
    case GK_KP_MULTIPLY:  return K_KP_STAR;
    case GK_KP_DIVIDE:    return K_KP_SLASH;
    case GK_KP_SUBTRACT:  return K_KP_MINUS;
    case GK_KP_PERIOD:    return K_KP_DEL;
    case GK_KP_ADD:       return K_KP_PLUS;

    default:
        break;
    }

    return 0;  /* Unmapped — ignore */
}


/* ====================================================================
 *  Public API — called from MoHAARunner.cpp via extern "C"
 * ==================================================================== */

/*
 * Godot_InjectKeyEvent — translate a Godot key constant to an engine
 *   keycode and inject it as a SE_KEY event.
 *
 *   @param godot_key  Raw integer from InputEventKey::get_keycode()
 *   @param down       1 = pressed, 0 = released
 *   @return           The mapped engine keycode (0 if unmapped)
 */
int Godot_InjectKeyEvent(int godot_key, int down)
{
    int engine_key = godot_key_to_engine(godot_key);
    if (engine_key) {
        Com_QueueEvent(Sys_Milliseconds(), SE_KEY, engine_key, down, 0, NULL);
    }
    return engine_key;
}

/*
 * Godot_InjectCharEvent — inject a character for text input (SE_CHAR).
 *   Only call this on key-press, not on key-release.
 *   The engine console and chat use SE_CHAR for text entry.
 *
 *   @param unicode  Unicode codepoint from InputEventKey::get_unicode()
 */
void Godot_InjectCharEvent(int unicode)
{
    if (unicode > 0 && unicode < 0x10000) {
        Com_QueueEvent(Sys_Milliseconds(), SE_CHAR, unicode, 0, 0, NULL);
    }
}

/*
 * Godot_InjectMouseMotion — inject relative mouse movement (SE_MOUSE).
 *   The engine coalesces SE_MOUSE events automatically.
 *
 *   @param dx  Horizontal pixels (positive = right)
 *   @param dy  Vertical pixels   (positive = down)
 */
void Godot_InjectMouseMotion(int dx, int dy)
{
    if (dx != 0 || dy != 0) {
        Com_QueueEvent(Sys_Milliseconds(), SE_MOUSE, dx, dy, 0, NULL);
    }
}

/*
 * Godot_InjectMouseButton — translate a Godot mouse button index to an
 *   engine keycode and inject as SE_KEY.
 *
 *   For wheel events the caller must send both press (1) and release (0)
 *   because Godot only fires pressed=true for wheel notches.
 *
 *   @param godot_button  MouseButton enum value (1=left, 2=right, etc.)
 *   @param down          1 = pressed, 0 = released
 */
void Godot_InjectMouseButton(int godot_button, int down)
{
    int engine_key = 0;

    switch (godot_button) {
    case GMB_LEFT:        engine_key = K_MOUSE1;     break;
    case GMB_RIGHT:       engine_key = K_MOUSE2;     break;
    case GMB_MIDDLE:      engine_key = K_MOUSE3;     break;
    case GMB_WHEEL_UP:    engine_key = K_MWHEELUP;   break;
    case GMB_WHEEL_DOWN:  engine_key = K_MWHEELDOWN; break;
    case GMB_XBUTTON1:    engine_key = K_MOUSE4;     break;
    case GMB_XBUTTON2:    engine_key = K_MOUSE5;     break;
    default:
        return;
    }

    Com_QueueEvent(Sys_Milliseconds(), SE_KEY, engine_key, down, 0, NULL);
}

/* ====================================================================
 *  Phase 48: Absolute mouse position injection for UI mode.
 *
 *  When the engine's UI system is active, it needs absolute mouse
 *  coordinates rather than relative deltas.  This function injects
 *  an SE_MOUSE event with the delta from the previous position,
 *  allowing the engine's UI code to track cursor position correctly.
 * ==================================================================== */

static int s_lastAbsX = 0;
static int s_lastAbsY = 0;
static int s_absInitialized = 0;

/*
 * Godot_InjectMousePosition — inject an absolute mouse position by
 *   computing the delta from the previous call and sending SE_MOUSE.
 *
 *   @param x  Absolute horizontal position in engine screen coordinates
 *   @param y  Absolute vertical position in engine screen coordinates
 */
void Godot_InjectMousePosition(int x, int y)
{
    if (!s_absInitialized) {
        s_lastAbsX = x;
        s_lastAbsY = y;
        s_absInitialized = 1;
        return;
    }

    int dx = x - s_lastAbsX;
    int dy = y - s_lastAbsY;
    s_lastAbsX = x;
    s_lastAbsY = y;

    if (dx != 0 || dy != 0) {
        Com_QueueEvent(Sys_Milliseconds(), SE_MOUSE, dx, dy, 0, NULL);
    }
}

/*
 * Godot_ResetMousePosition — reset the absolute mouse tracking state.
 *   Call when switching between UI and game input modes.
 */
void Godot_ResetMousePosition(void)
{
    s_absInitialized = 0;
    s_lastAbsX = 0;
    s_lastAbsY = 0;
}
