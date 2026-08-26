/*
 * godot_multiplayer_accessors.h — Multiplayer server browser + hosting accessors.
 *
 * Thin C API for server browsing (global/LAN) and hosting, callable
 * from MoHAARunner.cpp via extern "C".
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Godot_MP_ConnectToServer(const char *address);
void Godot_MP_Disconnect(void);
void Godot_MP_HostServer(const char *mapname, int maxclients, int gametype);
void Godot_MP_RefreshServerList(void);
void Godot_MP_RefreshLAN(void);
int  Godot_MP_GetServerCount(void);

#ifdef __cplusplus
}
#endif
