#include "MoHAARunner.h"

#include <gdextension_interface.h>
#include <cstdio>
#include <cstdlib>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

// Cross-platform helper for debug build-marker log path.
static const char *get_build_marker_path() {
#ifdef _WIN32
    // On Windows, use %TEMP% or fall back to the current directory.
    static char path[512];
    if (!path[0]) {
        const char *tmp = getenv("TEMP");
        if (!tmp) tmp = getenv("TMP");
        if (!tmp) tmp = ".";
        snprintf(path, sizeof(path), "%s\\openmohaa_build_marker.log", tmp);
    }
    return path;
#else
    return "/tmp/openmohaa_build_marker.log";
#endif
}

using namespace godot;

static const char *kOpenMohaaBuildMarker = "ALPHA-FIX-R4 " __DATE__ " " __TIME__;

// Track whether the engine was ever initialised (set by MoHAARunner)
extern "C" void Com_Shutdown(void);
extern "C" void Z_MarkShutdown(void);
extern "C" void Sys_CGameFinalShutdown(void);
static bool g_engine_was_initialized = false;

void Godot_SetEngineInitialized(bool v) { g_engine_was_initialized = v; }

void initialize_openmohaa_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    printf("OpenMoHAA: initialize_openmohaa_module called at SCENE level.\n");
    printf("OpenMoHAA: build marker: %s\n", kOpenMohaaBuildMarker);
    {
        FILE *fp = fopen(get_build_marker_path(), "a");
        if (fp) {
            fprintf(fp, "init %s\n", kOpenMohaaBuildMarker);
            fclose(fp);
        }
    }
    ClassDB::register_class<MoHAARunner>();
}

void uninitialize_openmohaa_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Ensure engine is shut down before global C++ destructors run.
    // This prevents SIGSEGV from ScriptMaster::~ScriptMaster calling
    // MEM_Alloc after the allocator function pointers are gone.
    if (g_engine_was_initialized) {
        printf("OpenMoHAA: uninitialize — calling Com_Shutdown.\n");
        // Tell cgame loader to skip dlclose during final shutdown to
        // avoid unmapping atexit/static-destructor code pages.
        Sys_CGameFinalShutdown();
        Com_Shutdown();
        /* Mark zone allocator as shut down BEFORE global C++ destructors run.
           This prevents SIGSEGV from dtors like ~con_arrayset trying to
           Z_Free zone memory after the engine's error-handling context
           (longjmp buffer) is gone. */
        Z_MarkShutdown();
        g_engine_was_initialized = false;
    } else {
        /* Safety net: always mark zone shutdown even if engine wasn't initialized */
        Z_MarkShutdown();
    }
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT openmohaa_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_openmohaa_module);
    init_obj.register_terminator(uninitialize_openmohaa_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    printf("OpenMoHAA: openmohaa_library_init called.\n");
    printf("OpenMoHAA: build marker: %s\n", kOpenMohaaBuildMarker);
    {
        FILE *fp = fopen(get_build_marker_path(), "a");
        if (fp) {
            fprintf(fp, "library_init %s\n", kOpenMohaaBuildMarker);
            fclose(fp);
        }
    }

    return init_obj.init();
}
}
