#ifndef BE_INTERFACE_H_NEW
#define BE_INTERFACE_H_NEW

#include "botlib.h" // Assumes this exists or will be found

// Constants/types usually in botlib.h, but if missing we might need them here or assume included.
// be_interface.c includes botlib.h first.


typedef struct botlib_globals_s
{
    int maxclients;
    int maxentities;
    int botlibsetup;
    float time;
    int goalareanum;
    vec3_t goalorigin;
    int runai;
} botlib_globals_t;

extern botlib_globals_t botlibglobals;
extern int botDeveloper;
extern botlib_import_t botimport;

// Based on likely dependencies
qboolean BotLibSetup(char *str);
int Sys_MilliSeconds(void);

#endif
