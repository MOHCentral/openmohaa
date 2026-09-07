#include "godot_settings_accessors.h"

#ifdef GODOT_GDEXTENSION
#include "../qcommon/qcommon.h"
#include "../client/client.h"

/* --- Cvar Read/Write --- */
float Godot_Settings_GetFloat(const char *name) {
    cvar_t *cv = Cvar_FindVar(name);
    return cv ? cv->value : 0.0f;
}

int Godot_Settings_GetInt(const char *name) {
    cvar_t *cv = Cvar_FindVar(name);
    return cv ? cv->integer : 0;
}

const char *Godot_Settings_GetString(const char *name) {
    cvar_t *cv = Cvar_FindVar(name);
    return cv ? cv->string : "";
}

void Godot_Settings_Set(const char *name, const char *value) {
    Cvar_Set(name, value);
}

/* --- Audio --- */
float Godot_Settings_GetMasterVolume(void) {
    return Godot_Settings_GetFloat("s_volume");
}

void Godot_Settings_SetMasterVolume(float vol) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", vol);
    Cvar_Set("s_volume", buf);
}

float Godot_Settings_GetMusicVolume(void) {
    return Godot_Settings_GetFloat("s_musicvolume");
}

void Godot_Settings_SetMusicVolume(float vol) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", vol);
    Cvar_Set("s_musicvolume", buf);
}

/* --- Video --- */
int Godot_Settings_GetTextureQuality(void) {
    return Godot_Settings_GetInt("r_picmip");
}

void Godot_Settings_SetTextureQuality(int picmip) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", picmip);
    Cvar_Set("r_picmip", buf);
}

/* --- Network --- */
int Godot_Settings_GetRate(void) {
    return Godot_Settings_GetInt("rate");
}

void Godot_Settings_SetRate(int rate) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", rate);
    Cvar_Set("rate", buf);
}

/* --- Key Bindings --- */
void Godot_Settings_BindKey(int keynum, const char *command) {
    Key_SetBinding(keynum, command);
}

const char *Godot_Settings_GetKeyBinding(int keynum) {
    return Key_GetBinding(keynum);
}

/* --- Config File --- */
void Godot_Settings_WriteConfig(void) {
    Cbuf_ExecuteText(EXEC_APPEND, "writeconfig mohaaconfig.cfg\n");
}
#endif
