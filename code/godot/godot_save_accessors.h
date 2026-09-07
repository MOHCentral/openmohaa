/*
 * godot_save_accessors.h — Save/load game accessors for Godot integration.
 *
 * These thin C functions wrap the engine's savegame/loadgame console commands,
 * allowing MoHAARunner.cpp to trigger saves and loads without including
 * engine headers directly.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Godot_Save_QuickSave(void);
void Godot_Save_QuickLoad(void);
void Godot_Save_SaveToSlot(int slot);
void Godot_Save_LoadFromSlot(int slot);
int  Godot_Save_SlotExists(int slot);

#ifdef __cplusplus
}
#endif
