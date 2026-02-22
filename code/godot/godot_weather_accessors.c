/*
 * godot_weather_accessors.c — C accessors for weather state from the engine.
 *
 * The server sets rain configstrings (CS_RAIN_DENSITY, CS_RAIN_SPEED,
 * etc.) which are stored in sv.configstrings[].  This file reads those
 * configstrings directly from the server_t struct and exposes the
 * values to the Godot C++ layer (godot_weather.cpp).
 *
 * Rain density > 0 indicates active rain; WEATHER_RAIN is returned.
 * Snow detection is reserved for future expansion.
 */

#ifdef GODOT_GDEXTENSION

#include "../server/server.h"
#include <stdlib.h>    /* atof, atoi */

/*
 * Helper: safely read a server configstring.
 * Returns "" if the index is out of range or the string is NULL.
 */
static const char *safe_configstring(int index) {
    if (index < 0 || index >= MAX_CONFIGSTRINGS)
        return "";
    if (!sv.configstrings[index])
        return "";
    return sv.configstrings[index];
}

/*
 * Godot_Weather_GetState — returns current weather type.
 *
 * 0 = no weather, 1 = rain, 2 = snow.
 *
 * Reads CS_RAIN_DENSITY from the server configstrings.  If density > 0
 * we report WEATHER_RAIN (1).  Snow is not yet signalled by the engine
 * so WEATHER_SNOW (2) is reserved for future use.
 */
int Godot_Weather_GetState(void) {
    float density = (float)atof(safe_configstring(CS_RAIN_DENSITY));
    if (density > 0.0f)
        return 1;  /* WEATHER_RAIN */
    return 0;      /* WEATHER_NONE */
}

/*
 * Godot_Weather_GetDensity — returns weather intensity [0..1].
 */
float Godot_Weather_GetDensity(void) {
    return (float)atof(safe_configstring(CS_RAIN_DENSITY));
}

/*
 * Godot_Weather_GetSpeed — rain particle speed in engine units.
 * Default 2048.
 */
float Godot_Weather_GetSpeed(void) {
    float v = (float)atof(safe_configstring(CS_RAIN_SPEED));
    return (v > 0.0f) ? v : 2048.0f;
}

/*
 * Godot_Weather_GetSpeedVary — speed randomisation in engine units.
 * Default 512.
 */
int Godot_Weather_GetSpeedVary(void) {
    const char *s = safe_configstring(CS_RAIN_SPEEDVARY);
    if (s[0] == '\0') return 512;
    return atoi(s);
}

/*
 * Godot_Weather_GetSlant — horizontal wind factor.
 * Default 50.
 */
int Godot_Weather_GetSlant(void) {
    const char *s = safe_configstring(CS_RAIN_SLANT);
    if (s[0] == '\0') return 50;
    return atoi(s);
}

/*
 * Godot_Weather_GetLength — rain streak length in engine units.
 * Default 90.
 */
float Godot_Weather_GetLength(void) {
    float v = (float)atof(safe_configstring(CS_RAIN_LENGTH));
    return (v > 0.0f) ? v : 90.0f;
}

/*
 * Godot_Weather_GetWidth — rain streak width in engine units.
 * Default 1.
 */
float Godot_Weather_GetWidth(void) {
    float v = (float)atof(safe_configstring(CS_RAIN_WIDTH));
    return (v > 0.0f) ? v : 1.0f;
}

/*
 * Godot_Weather_GetMinDist — minimum render distance in engine units.
 * Default 512.
 */
float Godot_Weather_GetMinDist(void) {
    float v = (float)atof(safe_configstring(CS_RAIN_MINDIST));
    return (v > 0.0f) ? v : 512.0f;
}

/*
 * Godot_Weather_GetShaderName — current rain shader path.
 * Returns "" if none is set.
 */
const char *Godot_Weather_GetShaderName(void) {
    return safe_configstring(CS_RAIN_SHADER);
}

#endif /* GODOT_GDEXTENSION */
