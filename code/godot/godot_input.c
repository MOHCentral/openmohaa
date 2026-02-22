/*
 * godot_input.c — Godot input & window backend for the OpenMoHAA GDExtension.
 *
 * Replaces SDL input (sdl_input.c) and window management (sdl_glimp.c).
 * The engine calls IN_Init/IN_Frame/IN_Shutdown for input polling, and
 * GLimp_Init/GLimp_EndFrame/GLimp_Shutdown for the display surface.
 *
 * In the final implementation these will bridge to Godot's Input singleton
 * and DisplayServer.  For now they are no-ops so the engine can boot.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* -------------------------------------------------------------------
 *  Input subsystem
 * ---------------------------------------------------------------- */

void IN_Init( void *windowData )
{
    Com_Printf( "[GodotInput] IN_Init\n" );
}

void IN_Shutdown( void )
{
    Com_Printf( "[GodotInput] IN_Shutdown\n" );
}

void IN_Restart( void )
{
    Com_Printf( "[GodotInput] IN_Restart\n" );
}

void IN_Frame( void )
{
    /* Will poll Godot input state and inject key/mouse events */
}

void IN_Activate( qboolean active )
{
    (void)active;
}

void IN_MouseEvent( int mstate )
{
    (void)mstate;
}

void Sys_SendKeyEvents( void )
{
    /* Called from common.c to pump the event queue.
       Under Godot, input events are handled in _input/_process. */
}

void Key_KeynameCompletion( void (*callback)(const char *s) )
{
    /* Console auto-completion for key names — stubbed for now */
    (void)callback;
}

/* -------------------------------------------------------------------
 *  GLimp — OpenGL window management replacement
 *
 *  In the original engine, these create an SDL window + GL context.
 *  Under Godot, the display surface is owned by Godot itself.
 * ---------------------------------------------------------------- */

void GLimp_Init( qboolean fixedFunction )
{
    Com_Printf( "[GodotInput] GLimp_Init (Godot owns the display)\n" );
}

void GLimp_Shutdown( void )
{
    Com_Printf( "[GodotInput] GLimp_Shutdown\n" );
}

void GLimp_EndFrame( void )
{
    /* No buffer swap needed — Godot handles presentation */
}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256],
                     unsigned char blue[256] )
{
    /* Gamma ramp — not applicable under Godot */
}

void GLimp_Minimize( void )
{
}

/* -------------------------------------------------------------------
 *  System functions that the renderer imports
 * ---------------------------------------------------------------- */

void Sys_GLimpSafeInit( void )
{
    /* Nothing to do — no GL context to reset */
}

void Sys_GLimpInit( void )
{
    /* Nothing to do — Godot owns the rendering context */
}

qboolean Sys_LowPhysicalMemory( void )
{
    return qfalse;
}

/* -------------------------------------------------------------------
 *  Dialog used during abnormal-exit recovery
 * ---------------------------------------------------------------- */

dialogResult_t Sys_Dialog( dialogType_t type, const char *message,
                           const char *title )
{
    /* Can't show native dialogs from a GDExtension.
       Log the message instead. */
    Com_Printf( "[GodotInput] Dialog (%s): %s\n",
                title ? title : "?", message ? message : "?" );
    return DR_OK;
}
