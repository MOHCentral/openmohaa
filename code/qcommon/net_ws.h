/*
 * net_ws.h — WebSocket relay transport declarations for Emscripten web builds
 *
 * Provides UDP-over-WebSocket networking through a relay server.
 * Only compiled/used when building for Emscripten (__EMSCRIPTEN__).
 */

#ifndef NET_WS_H
#define NET_WS_H

#if defined(GODOT_GDEXTENSION) && defined(__EMSCRIPTEN__)

void        NET_WS_Init(void);
void        NET_WS_Shutdown(void);
void        NET_WS_SendPacket(int length, const void *data, netadr_t to);
qboolean    NET_WS_GetPacket(netadr_t *net_from, msg_t *net_message);
void        NET_WS_Poll(void);

#endif /* GODOT_GDEXTENSION && __EMSCRIPTEN__ */

#endif /* NET_WS_H */
