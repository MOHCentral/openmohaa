#include "MoHAARunner.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

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

    return init_obj.init();
}
}
