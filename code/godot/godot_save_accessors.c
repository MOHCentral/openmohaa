/*
 * godot_save_accessors.c — Save/load game accessors for Godot integration.
 *
 * Wraps the engine's savegame/loadgame console commands so that
 * MoHAARunner.cpp (C++) can trigger saves and loads via extern "C"
 * without including engine headers that collide with godot-cpp.
 *
 * The engine's SV_Savegame_f / SV_Loadgame_f (in sv_ccmds.c) handle
 * all the heavy lifting — these functions simply queue the commands.
 */

#ifdef GODOT_GDEXTENSION

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

void Godot_Save_QuickSave(void) {
    Cbuf_ExecuteText(EXEC_APPEND, "savegame quick\n");
}

void Godot_Save_QuickLoad(void) {
    Cbuf_ExecuteText(EXEC_APPEND, "loadgame quick\n");
}

void Godot_Save_SaveToSlot(int slot) {
    char cmd[64];
    Com_sprintf(cmd, sizeof(cmd), "savegame slot%d\n", slot);
    Cbuf_ExecuteText(EXEC_APPEND, cmd);
}

void Godot_Save_LoadFromSlot(int slot) {
    char cmd[64];
    Com_sprintf(cmd, sizeof(cmd), "loadgame slot%d\n", slot);
    Cbuf_ExecuteText(EXEC_APPEND, cmd);
}

int Godot_Save_SlotExists(int slot) {
    char name[64];
    const char *path;

    Com_sprintf(name, sizeof(name), "slot%d", slot);
    path = Com_GetArchiveFileName(name, "sav");
    /* FS_ReadFile with NULL buffer returns file length or -1 if missing */
    return (FS_ReadFile(path, NULL) >= 0) ? 1 : 0;
}

#endif
