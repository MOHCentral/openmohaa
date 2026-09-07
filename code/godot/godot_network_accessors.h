/*
 * godot_network_accessors.h — Network state accessor declarations.
 *
 * Thin C API so MoHAARunner.cpp (Godot C++ side) can query connection
 * status, ping, client/server counts, and packet statistics without
 * including engine headers that conflict with godot-cpp.
 */

#ifndef GODOT_NETWORK_ACCESSORS_H
#define GODOT_NETWORK_ACCESSORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Client connection state (maps to connstate_t: CA_UNINITIALIZED..CA_ACTIVE). */
int Godot_Net_GetClientState(void);

/* Number of active (CS_ACTIVE) server-side client slots. */
int Godot_Net_GetServerClientCount(void);

/* Number of connected (>= CS_CONNECTED) server-side client slots. */
int Godot_Net_GetServerConnectedCount(void);

/* Latest snapshot ping for the local client (milliseconds, 0 if no snap). */
int Godot_Net_GetPing(void);

/* Current client snapshot rate (snaps cvar value). */
int Godot_Net_GetSnapshotRate(void);

/* Textual address of the server the client is connected to.
 * Returns empty string if disconnected. */
const char *Godot_Net_GetServerAddress(void);

/* Returns 1 if the current server address is on the LAN, 0 otherwise. */
int Godot_Net_IsLANGame(void);

/* Server running state (0 = not running, 1 = running). */
int Godot_Net_IsServerRunning(void);

/* UDP port the server is bound to (net_port cvar). */
int Godot_Net_GetPort(void);

/* Protocol version used by the engine. */
int Godot_Net_GetProtocolVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_NETWORK_ACCESSORS_H */
