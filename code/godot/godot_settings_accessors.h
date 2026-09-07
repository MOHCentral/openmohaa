#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Generic cvar access */
float       Godot_Settings_GetFloat(const char *name);
int         Godot_Settings_GetInt(const char *name);
const char *Godot_Settings_GetString(const char *name);
void        Godot_Settings_Set(const char *name, const char *value);

/* Audio */
float Godot_Settings_GetMasterVolume(void);
void  Godot_Settings_SetMasterVolume(float vol);
float Godot_Settings_GetMusicVolume(void);
void  Godot_Settings_SetMusicVolume(float vol);

/* Video */
int  Godot_Settings_GetTextureQuality(void);
void Godot_Settings_SetTextureQuality(int picmip);

/* Network */
int  Godot_Settings_GetRate(void);
void Godot_Settings_SetRate(int rate);

/* Key Bindings */
void        Godot_Settings_BindKey(int keynum, const char *command);
const char *Godot_Settings_GetKeyBinding(int keynum);

/* Config persistence */
void Godot_Settings_WriteConfig(void);

#ifdef __cplusplus
}
#endif
