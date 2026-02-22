# Implementation Roadmap: GodotMoHAA

## Phase 1: The Build System & Scaffolding ✅
- [x] **Task 1.1:** Create a `SConstruct` (SCons) file to build the project as a GDExtension library.
- [x] **Task 1.2:** Configure `godot-cpp` bindings.
- [x] **Task 1.3:** Create the GDExtension entry point (`register_types.cpp`).
- [x] **Task 1.4:** Compile and verify a "Hello World" load in the Godot Editor.

## Phase 2: The Engine Heartbeat ✅
- [x] **Task 2.1:** Identify the `Com_Frame()` (main loop) in OpenMoHAA.
- [x] **Task 2.2:** Create a Godot Node class `MoHAARunner` (extends `Node`).
- [x] **Task 2.3:** Hook Godot's `_process(delta)` to drive the OpenMoHAA frame ticker.
- [x] **Task 2.4:** Redirect `Com_Printf`/`Sys_Print` to Godot's console.
- [x] **Task 2.5:** Make `Com_Frame` non-blocking (bypass `NET_Sleep`/`Sys_Sleep` under Godot).
- [x] **Task 2.6:** Resolve monolithic build symbol conflicts (`Com_Printf`, `Com_Error`, `SV_Malloc`, `SV_Free`, `SV_Error` shadowed by fgame function pointers).
- [x] **Task 2.7:** Verify dedicated server init + map load (mohdm1 loads, entities spawn, server runs).

### Key technical details (Phase 2):
- `DEDICATED` and `GODOT_GDEXTENSION` defines active
- `main()` in sys_main.c guarded with `#ifndef GODOT_GDEXTENSION`
- `Sys_Print` redirects to `Godot_SysPrint` callback under `GODOT_GDEXTENSION`
- `Com_Frame` sleep loop bypassed with `break;` under `GODOT_GDEXTENSION`
- `SV_Frame`'s `Sys_Sleep(-1)` (no-map idle) guarded out under `GODOT_GDEXTENSION`
- Monolithic build links fgame directly; `Sys_GetGameAPI` calls `GetGameAPI` without dlopen
- fgame's `Com_Printf`, `Com_Error`, `SV_Malloc`, `SV_Free`, `SV_Error` guarded with `#ifndef GODOT_GDEXTENSION`
- Comprehensive client/UI/sound/input stubs in `stubs.cpp`
- Build: `scons platform=linux target=template_debug -j$(nproc) dev_build=yes`

## Phase 2.5: Server Operations ✅
- [x] **Task 2.5.1:** Expose `execute_command(cmd)` and `load_map(name)` to GDScript.
- [x] **Task 2.5.2:** Handle `Com_Error` gracefully (ERR_FATAL/Sys_Error no longer kills Godot; uses longjmp + error signal).
- [x] **Task 2.5.3:** Expose server status (`is_map_loaded`, `get_player_count`, `get_current_map`, `get_server_state`, `get_server_state_string`) to GDScript.
- [x] **Task 2.5.4:** Signal/notification system (`engine_error`, `map_loaded`, `map_unloaded`, `engine_shutdown_requested`).
- [x] **Task 2.5.5:** Fix library unload crash (ScriptMaster destructor calling gi.Malloc after engine shutdown — added safe fallbacks for all gi.Malloc/gi.Free call sites + Com_Shutdown in module teardown).

### Key technical details (Phase 2.5):
- `Sys_Error` under `GODOT_GDEXTENSION`: calls `Godot_SysError()` which stores message + longjmps to `godot_error_jmpbuf` (set up in `_ready` and `_process`)
- `Sys_Quit` under `GODOT_GDEXTENSION`: calls `Godot_SysQuit()` which sets flag + longjmps back
- `Sys_SigHandler` under `GODOT_GDEXTENSION`: routes to `Godot_SysError`/`Godot_SysQuit` instead of `Sys_Exit`
- `Q_NO_RETURN` attribute removed from `Sys_Error`/`Sys_Quit` declarations under `GODOT_GDEXTENSION` (qcommon.h)
- `gi.Malloc`/`gi.Free` safe wrappers (`gi_Malloc_Safe`/`gi_Free_Safe`) in `g_main.h` — fall back to system `malloc`/`free` when `gi` function pointers are NULL (library teardown)
- Applied to: `mem_blockalloc.cpp`, `con_arrayset.h`, `con_set.h`, `script.cpp`, `mem_tempalloc.cpp`, `lightclass.cpp`
- Server state accessors via `godot_server_accessors.c` (avoids header conflicts between engine `server.h` and godot-cpp)
- `register_types.cpp` calls `Com_Shutdown()` in uninitialize as safety net
- State-change detection in `_process()` polls `sv.state` and emits signals on transitions

## Phase 3: Script Engine (Morfuse) ✅
The OpenMoHAA script engine (`code/script/` — ScriptVM, ScriptCompiler, ScriptMaster) is already compiled and functional in the monolithic build (`WITH_SCRIPT_ENGINE` define). When maps load, `.scr` scripts are loaded via `gi.FS_ReadFile`, compiled by ScriptCompiler, and executed by ScriptMaster. No separate integration step was needed.

- [x] **Task 3.1:** Script engine compiled into the GDExtension build (already in `src_dirs` in SConstruct).
- [x] **Task 3.2:** ScriptMaster global destructors handled safely (gi_Malloc_Safe/gi_Free_Safe — done in Phase 2.5.5).
- [x] **Task 3.3:** Verified: map load triggers `.scr` script compilation and execution (mohdm1 tested in Phase 2.7).

## Phase 4: Asset Pipeline
**Constraint:** Full compatibility with existing MOHAA/SH/BT pk3 archives, .scr scripts, .tik files, BSP maps, and shader definitions. The engine's native VFS (`files.cpp`) and script loader must not be replaced — they already handle pk3 mounting, search-path ordering, and game selection for all three titles (`main/`, `mainta/`, `maintt/`).

- [ ] **Task 4.1:** Bridge the VFS so Godot can also read assets via the engine's `FS_*` functions (expose file-read helpers to GDScript). Ensure `fs_basepath` correctly resolves for all three game directories. Do not bypass or re-implement the VFS.
- [ ] **Task 4.2:** Implement a BSP parser that generates Godot `ArrayMesh` data from map files at runtime, reading BSP data through the engine VFS.
- [ ] **Task 4.3:** Convert Quake 3 / MOHAA textures and shader definitions to Godot `ShaderMaterial` / `StandardMaterial3D` at runtime.