/*
 * godot_weather.h — Rain and snow particle system manager.
 *
 * Reads weather state from the engine (via C accessors) and manages
 * Godot GPUParticles3D nodes for rain and snow effects.  The engine
 * sets weather state through server commands and configstrings; this
 * module translates that state into visual particle effects.
 *
 * Integration: MoHAARunner calls Godot_Weather_Init() after map load
 * and Godot_Weather_Update() each frame.
 */

#ifndef GODOT_WEATHER_H
#define GODOT_WEATHER_H

#ifdef __cplusplus

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

/*
 * Weather type constants.
 */
#define WEATHER_NONE  0
#define WEATHER_RAIN  1
#define WEATHER_SNOW  2

/*
 * Initialise the weather system for a newly loaded map.
 * Creates particle emitter nodes as children of the given parent.
 *
 * @param parent  Scene node to attach weather particles to.
 *                Typically the BSP root or MoHAARunner's 3D scene.
 */
void Godot_Weather_Init(godot::Node3D *parent);

/*
 * Update weather each frame.  Reads current weather state from the
 * engine, enables/disables rain or snow particles, and repositions
 * the emitter volume around the camera.
 *
 * @param camera_pos  Current camera world position (Godot coords).
 * @param delta       Frame delta time in seconds.
 */
void Godot_Weather_Update(const godot::Vector3 &camera_pos, float delta);

/*
 * Shut down weather: remove particle nodes and free resources.
 */
void Godot_Weather_Shutdown(void);

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Query the current weather state from the engine.
 * Returns: WEATHER_NONE (0), WEATHER_RAIN (1), or WEATHER_SNOW (2).
 */
int Godot_Weather_GetState(void);

/*
 * Query the current weather density from the engine.
 * Returns a float [0..1] representing rain/snow intensity.
 */
float Godot_Weather_GetDensity(void);

/*
 * Rain particle speed in engine units (default 2048).
 */
float Godot_Weather_GetSpeed(void);

/*
 * Speed randomisation factor in engine units (default 512).
 */
int Godot_Weather_GetSpeedVary(void);

/*
 * Horizontal wind slant factor (default 50).
 */
int Godot_Weather_GetSlant(void);

/*
 * Rain streak length in engine units (default 90).
 */
float Godot_Weather_GetLength(void);

/*
 * Rain streak width in engine units (default 1).
 */
float Godot_Weather_GetWidth(void);

/*
 * Minimum render distance in engine units (default 512).
 */
float Godot_Weather_GetMinDist(void);

/*
 * Current rain shader path (or "" if none set).
 */
const char *Godot_Weather_GetShaderName(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_WEATHER_H */
