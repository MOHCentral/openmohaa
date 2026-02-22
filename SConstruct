#!/usr/bin/env python
import os
import sys

env = SConscript("../godot-cpp/SConstruct")

# Prevent Linux shell argv overflows when archiving very large static libs
# (notably godot-cpp) from long workspace paths.
if env.get("platform") == "linux" and env.get("ARCOM"):
    env["ARCOM_POSIX"] = env["ARCOM"].replace("$TARGET", "$TARGET.posix").replace("$SOURCES", "$SOURCES.posix")
    env["ARCOM"] = "${TEMPFILE(ARCOM_POSIX)}"

# ──────────────────────────────────────────────
#  OpenMoHAA GDExtension Build — Full Client + Server
# ──────────────────────────────────────────────
#
# Module breakdown (see copilot-instructions.md for architecture details):
#
#   Core/Network     code/qcommon          cvars, commands, VFS, memory, net
#   Core C++         code/corepp           C++ helpers (str, containers)
#   Server           code/server           dedicated / listen server
#   Game logic       code/fgame            entities, AI, Morfuse glue
#   Script engine    code/script           Morfuse compiler & executor
#   Parser           code/parser           tokeniser / parser
#   TIKI/Skeletor    code/tiki,skeletor    model & animation loading
#   Bot AI           code/botlib           bot pathfinding & AI
#   Client           code/client           client state, prediction, keys
#   Client game      code/cgame            effects, HUD, view, snapshots
#   UI framework     code/uilib            MOHAA menu / widget system
#   Online services  code/gamespy          GameSpy master-server support
#   Godot glue       code/godot            GDExtension: renderer, sound,
#                                          input, lifecycle, accessors
#   Navigation       code/thirdparty/…     Recast / Detour pathfinding
#
# DEDICATED is kept as a build define so that SDL-specific code in
# sys_main.c / sys_unix.c stays disabled.  common.c and memory.c
# #undef DEDICATED under GODOT_GDEXTENSION so the client subsystem
# (CL_Init, CL_Frame, etc.) is active.

env["symbols_visibility"] = "visible"
env["debug_symbols"] = True
env.Append(LINKFLAGS=["-Wl,--export-dynamic"])

env.Append(CPPPATH=[
    "code",
    "code/botlib",
    "code/cgame",
    "code/client",
    "code/fgame",
    "code/gamespy",
    "code/qcommon",
    "code/renderercommon",
    "code/renderergl1",
    "code/server",
    "code/script",
    "code/skeletor",
    "code/sys",
    "code/tiki",
    "code/ui",
    "code/uilib",
    "code/corepp",
    "generated",
])

env.Append(CPPDEFINES=[
    # DEDICATED stays defined to keep SDL code in sys files inactive.
    # common.c / memory.c #undef it under GODOT_GDEXTENSION.
    "DEDICATED",
    "BOTLIB",
    "GAME_DLL",
    "ARCHIVE_SUPPORTED",
    "WITH_SCRIPT_ENGINE",
    "APP_MODULE",
    "GODOT_GDEXTENSION",
])

# ── Source directories ──
sources = []

src_dirs = [
    # ── Core engine ──
    "code/qcommon",
    "code/corepp",

    # ── Server ──
    "code/server",

    # ── Game logic (server-side) ──
    "code/fgame",

    # ── Script engine ──
    "code/script",
    "code/parser",

    # ── TIKI / Skeletor ──
    "code/tiki",
    "code/skeletor",

    # ── Bot AI ──
    "code/botlib",

    # ── Client ──
    "code/client",

    # ── Client game (cgame) — compiled separately or stubbed ──
    # NOTE: cgame is NOT included in the monolithic build because it
    # shares corepp/ code with fgame but needs CGAME_DLL instead of
    # GAME_DLL.  Stub exports are provided in stubs.cpp.
    # "code/cgame",

    # ── UI framework ──
    "code/uilib",

    # ── Online services ──
    "code/gamespy",

    # ── Godot GDExtension glue ──
    "code/godot",

    # ── Third-party navigation ──
    "code/thirdparty/recastnavigation/Recast/Source",
    "code/thirdparty/recastnavigation/Detour/Source",
    "code/thirdparty/recastnavigation/DetourCrowd/Source",
    "code/thirdparty/recastnavigation/DetourTileCache/Source",
]

env.Append(CPPPATH=[
    "code/thirdparty/recastnavigation/Recast/Include",
    "code/thirdparty/recastnavigation/Detour/Include",
    "code/thirdparty/recastnavigation/DetourCrowd/Include",
    "code/thirdparty/recastnavigation/DetourTileCache/Include",
    "code/thirdparty/jpeg-9f",
])

# ── Bundled libjpeg (jpeg-9f) for JPEG texture loading ──
sources.extend(Glob("code/thirdparty/jpeg-9f/j*.c"))

# ── Sys files — platform specific ──
sys_sources = ["code/sys/sys_main.c", "code/sys/con_log.c"]
if env["platform"] == "linux":
    env.Append(CPPDEFINES=["_LINUX", "__linux__"])
    env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
    env.Append(CXXFLAGS=["-fexceptions", "-frtti"])
    sys_sources.append("code/sys/sys_unix.c")
    sys_sources.append("code/sys/con_tty.c")
    sys_sources.append("code/sys/sys_curl.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "windows":
    env.Append(CPPDEFINES=["_WIN32", "WIN32", "_WINDOWS"])
    env.Append(CCFLAGS=["/EHsc"])  # C++ exceptions
    sys_sources.append("code/sys/sys_win32.c")
    sys_sources.append("code/sys/con_win32.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "macos":
    env.Append(CPPDEFINES=["__APPLE__", "MACOS_X"])
    env.Append(CFLAGS=["-Wno-incompatible-pointer-types"])
    env.Append(CXXFLAGS=["-fexceptions", "-frtti"])
    sys_sources.append("code/sys/sys_unix.c")   # macOS uses POSIX sys layer
    sys_sources.append("code/sys/con_tty.c")
    sys_sources.append("code/sys/sys_curl.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "web":
    env.Append(CPPDEFINES=["__EMSCRIPTEN__", "_LINUX", "__linux__"])
    env.Append(CFLAGS=["-Wno-incompatible-pointer-types"])
    env.Append(CXXFLAGS=["-fexceptions", "-frtti"])

sources.extend(sys_sources)

# ── Collect sources recursively ──
def add_sources(directory):
    for root, dirs, files in os.walk(directory):
        for f in files:
            if f.endswith((".c", ".cpp")):
                sources.append(os.path.join(root, f))

for d in src_dirs:
    add_sources(d)

# ── Renderer data modules (selective — GL draw-path files excluded) ──
# These provide real shader parsing, image loading, BSP loading, model
# management, etc.  GL upload/draw calls are guarded with
# #ifndef GODOT_GDEXTENSION.  Stub rendering functions for un-compiled
# modules live in tr_godot_gl_stubs.c.
renderer_sources = [
    # GL stubs FIRST — -z muldefs picks the first definition
    "code/renderergl1/tr_godot_gl_stubs.c",
    # Data management modules
    "code/renderergl1/tr_init.c",
    "code/renderergl1/tr_shader.c",
    "code/renderergl1/tr_image.c",
    "code/renderergl1/tr_bsp.c",
    "code/renderergl1/tr_model.cpp",
    "code/renderergl1/tr_staticmodels.cpp",
    "code/renderergl1/tr_font.cpp",
    "code/renderergl1/tr_curve.c",
    "code/renderergl1/tr_marks.c",
    "code/renderergl1/tr_marks_permanent.c",
    "code/renderergl1/tr_terrain.c",
    "code/renderergl1/tr_main.c",
    "code/renderergl1/tr_world.c",
    "code/renderergl1/tr_light.c",
    "code/renderergl1/tr_vis.cpp",
    "code/renderergl1/tr_scene.c",
    "code/renderergl1/tr_sprite.c",
    "code/renderergl1/tr_util.cpp",
    # renderercommon: noise generator + PVR image loader
    "code/renderercommon/tr_noise.c",
    "code/renderercommon/tr_image_pvr.c",
    # Accessor bridge: reads real shader_t data → GodotShaderProps structs
    "code/renderergl1/godot_shader_accessors.c",
    # Accessor bridge: reads tr.world BSP data for Godot-side queries
    "code/renderergl1/godot_bsp_accessors.c",
]
sources.extend(renderer_sources)

env.Append(CPPPATH=["code/parser/generated"])

# ── Exclusion filters ──
# Sound backend files — godot_sound.cpp provides all S_* / MUSIC_* functions.
# SDL / OpenGL / OpenAL files — godot_renderer.cpp and godot_input.cpp replace.
# Additional exclusions for files that need libraries we don't link.

excluded_gamespy_patterns = [
    "/mfc/", "/mfc", "mfc/", "/win32/",
    "/gp_stats/", "/laddertrack/", "/multitrack/", "/track/", "/chatty/",
    "/sbmfcsample/", "/scracesample/", "/peerlobby/", "/cdkeygen/",
    "/gt2proxy/", "/voice2bench/", "/gt2action/", "/gt2nat/", "/nitro/",
    "/revolution/", "/ps2/", "/ps3/", "/psp/", "/macosx/", "/xbox/",
    "/x360/", "/common_light/", "/gt2hostmig/", "/chatc/",
    "/gpstress/", "/peerc/", "/qr2csample/", "/fpupdate/",
    "/webservices/", "/peer/", "/voice2/",
    "/ghttp/ghttpmfc/",
    "/gp/",
    "/sc/",
    "/sake/",
    "common_light",
]

# Files to EXCLUDE from code/client/  (sound backends, OpenAL, curl, etc.)
excluded_client_basenames = {
    # DMA / OpenAL / Miles sound backends — replaced by godot_sound.cpp
    "snd_dma.c", "snd_dma_new.cpp",
    "snd_mem.c", "snd_mem_new.cpp",
    "snd_mix.c",
    "snd_main.c",
    "snd_openal.c", "snd_openal_new.cpp",
    "snd_miles_new.cpp",
    "snd_adpcm.c", "snd_altivec.c", "snd_wavelet.c",
    "snd_codec.c",
    "snd_codec_mp3.c", "snd_codec_ogg.c", "snd_codec_opus.c", "snd_codec_wav.c",
    "snd_info.cpp",
    # OpenAL dynamic loader
    "qal.c",
    # Mumble positional audio
    "libmumblelink.c",
    # libcurl HTTP downloads
    "cl_curl.c",
    # New sound backend (also a sound implementation)
    "snd_main_new.cpp",
}

def should_exclude(filepath):
    fp = str(filepath).lower().replace("\\", "/")

    # Old parser duplicates
    if fp.endswith(("code/parser/lex.yy.cpp", "code/parser/y.tab.cpp")):
        return True

    # GameSpy exclusions
    for pat in excluded_gamespy_patterns:
        if pat in fp:
            return True

    # Exclude standalone tests
    if "code/godot/tests" in fp:
        return True

    # Exclude test_*.cpp stubs from code/godot/ — they contain mock
    # implementations that shadow real functions via -z muldefs.
    if "code/godot/" in fp and os.path.basename(fp).startswith("test_"):
        return True

    # Replaced by godot_shader_accessors.c which reads real shader_t data
    if fp.endswith("godot_shader_props.cpp"):
        return True

    # Dead code — surface effect lookup tables are never called
    if fp.endswith("godot_surface_effects.cpp"):
        return True

    # Dead code — animation event/blend modules are never called;
    # cgame already handles anim events via CG_ClientCommands → cgi.S_StartSound
    if fp.endswith(("godot_animation_events.cpp", "godot_animation_event_accessors.cpp",
                    "godot_anim_blend.cpp")):
        return True

    # Dead code — speaker entities module never initialised (Init/LoadFromEntities
    # never called); engine's S_AddLoopingSound pipeline handles ambient sounds
    if fp.endswith("godot_speaker_entities.cpp"):
        return True

    # Client sound / backend exclusions
    if "code/client/" in fp:
        basename = os.path.basename(fp)
        if basename in excluded_client_basenames:
            return True

    return False

sources = [s for s in sources if not should_exclude(s)]

# Allow multiple definitions to handle legacy code conflicts
# (fgame + cgame compile overlapping shared sources; platform linker resolves)
if env["platform"] == "linux":
    env.Append(LINKFLAGS=["-z", "muldefs"])
    # Bind template symbols locally within each .so to prevent ELF interposition
    env.Append(LINKFLAGS=["-Wl,-Bsymbolic-functions"])
elif env["platform"] == "web":
    env["CCFLAGS"] = [flag for flag in env.get("CCFLAGS", []) if "SUPPORT_LONGJMP='wasm'" not in str(flag)]
    env["LINKFLAGS"] = [flag for flag in env.get("LINKFLAGS", []) if "SUPPORT_LONGJMP='wasm'" not in str(flag)]
    env.Append(CCFLAGS=["-sSUPPORT_LONGJMP='emscripten'"])
    env.Append(LINKFLAGS=["-sSUPPORT_LONGJMP='emscripten'"])
    env.Append(LINKFLAGS=["-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$$emscripten_longjmp','$$emscripten_throw_longjmp']"])
    env.Append(LINKFLAGS=["-sDISABLE_EXCEPTION_CATCHING=0"])
    env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])
    env.Append(CPPPATH=["code/thirdparty/zlib-1.3.1"])
    sources.extend([
        "code/thirdparty/zlib-1.3.1/adler32.c",
        "code/thirdparty/zlib-1.3.1/crc32.c",
        "code/thirdparty/zlib-1.3.1/inffast.c",
        "code/thirdparty/zlib-1.3.1/inflate.c",
        "code/thirdparty/zlib-1.3.1/inftrees.c",
        "code/thirdparty/zlib-1.3.1/zutil.c",
    ])
elif env["platform"] == "macos":
    # macOS Mach-O linker uses -undefined dynamic_lookup instead of -z muldefs
    env.Append(LINKFLAGS=["-Wl,-undefined,dynamic_lookup"])
elif env["platform"] == "windows":
    # MSVC linker allows multiple definitions by default (/FORCE:MULTIPLE)
    env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])

# ── Link system libraries ──
if env["platform"] == "linux":
    env.Append(LIBS=["z", "dl"])  # zlib, dlopen
elif env["platform"] == "macos":
    env.Append(LIBS=["z", "dl"])  # zlib, dlopen
elif env["platform"] == "windows":
    env.Append(LIBS=["zlib", "ws2_32", "winmm"])  # zlib, winsock, multimedia

# ── Build the shared library ──
library = env.SharedLibrary(
    target="bin/openmohaa",
    source=sources,
)

Default(library)

# ══════════════════════════════════════════════
#  cgame — Separate shared library
# ══════════════════════════════════════════════
# cgame must be built as its own .so because it shares corepp/ code with
# fgame but needs CGAME_DLL (different function-pointer routing through
# cgi instead of gi).  The main GDExtension .so loads it via dlopen at
# runtime, exactly like the upstream engine.
#
# Output: bin/cgame.so  (copied to the MOHAA game dir e.g. main/)
# ══════════════════════════════════════════════

cgame_env = Environment()
cgame_env.Append(CPPPATH=[
    "code",
    "code/cgame",
    "code/client",
    "code/fgame",
    "code/qcommon",
    "code/renderercommon",
    "code/server",
    "code/script",
    "code/skeletor",
    "code/tiki",
    "code/corepp",
    "generated",
])

cgame_env.Append(CPPDEFINES=[
    "CGAME_DLL",
    "GODOT_GDEXTENSION",
])

# Use a separate build directory so SCons doesn't conflict with the main build.
# For web use a distinct dir to avoid mixing native .o and Emscripten .o files.
_cgame_variant_dir = "build/cgame-web" if env["platform"] == "web" else "build/cgame"
cgame_env.VariantDir(_cgame_variant_dir, "code", duplicate=0)

if env["platform"] == "linux":
    cgame_env.Append(CPPDEFINES=["_LINUX", "__linux__"])
    cgame_env.Append(CCFLAGS=[
        "-fPIC", "-g", "-O2",
        "-fvisibility=hidden",      # Hide all symbols by default —
    ])                               # prevents ELF interposition of
                                     # template instantiations (e.g.
                                     # con_arrayset::DeleteTable) between
                                     # cgame.so and the main .so.
    cgame_env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
    cgame_env.Append(CXXFLAGS=["-std=c++17", "-fexceptions", "-frtti"])
    cgame_env.Append(LINKFLAGS=["-z", "muldefs"])
    cgame_env.Append(LINKFLAGS=["-Wl,-Bsymbolic-functions"])
elif env["platform"] == "macos":
    cgame_env.Append(CPPDEFINES=["__APPLE__", "MACOS_X"])
    cgame_env.Append(CCFLAGS=[
        "-fPIC", "-g", "-O2",
        "-fvisibility=hidden",
    ])
    cgame_env.Append(CFLAGS=["-Wno-incompatible-pointer-types"])
    cgame_env.Append(CXXFLAGS=["-std=c++17", "-fexceptions", "-frtti"])
    cgame_env.Append(LINKFLAGS=["-Wl,-undefined,dynamic_lookup"])
elif env["platform"] == "windows":
    cgame_env.Append(CPPDEFINES=["_WIN32", "WIN32", "_WINDOWS"])
    cgame_env.Append(CCFLAGS=["/EHsc", "/O2"])
    cgame_env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])
elif env["platform"] == "web":
    # cgame for web: compiled as Emscripten WASM SIDE_MODULE so that
    # Sys_GetCGameAPI / Sys_LoadLibrary (dlopen) can load it at runtime
    # from the virtual FS where the preloader deposits it as /main/cgame.so.
    # SIDE_MODULE=2 is lightweight — libc/C++ runtime are supplied by the
    # main Godot WASM module.
    cgame_env.Replace(
        CC     = env.get("CC",     "emcc"),
        CXX    = env.get("CXX",    "em++"),
        AR     = env.get("AR",     "emar"),
        RANLIB = env.get("RANLIB", "emranlib"),
        SHLINK = env.get("CXX",    "em++"),
    )
    # Inherit the PATH that contains emcc/em++
    if "ENV" in env and "PATH" in env["ENV"]:
        cgame_env["ENV"]["PATH"] = env["ENV"]["PATH"]
    cgame_env.Append(CPPDEFINES=["__EMSCRIPTEN__", "_LINUX", "__linux__"])
    # -pthread is REQUIRED: the main Godot WASM module uses SharedArrayBuffer
    # (shared memory). Without -pthread, cgame.so declares it imports unshared
    # memory, causing a LinkError ("imported shared memory but unshared required")
    # when loadDylibs() instantiates it. -pthread makes cgame.so import shared
    # memory, matching the main module's SharedArrayBuffer.
    cgame_env.Append(CCFLAGS=["-O2", "-fvisibility=hidden", "-pthread"])
    cgame_env.Append(CFLAGS=[
        "-Wno-incompatible-pointer-types",
        "-Wno-discarded-qualifiers",
    ])
    cgame_env.Append(CXXFLAGS=[
        "-std=c++17", "-fexceptions", "-frtti",
        "-sSUPPORT_LONGJMP=emscripten",
    ])
    # Replace SHLINKFLAGS entirely — we do NOT want SCons's default -shared
    # flag; emcc uses -sSIDE_MODULE instead.
    # -pthread must also appear in link flags so emcc generates shared-memory
    # imports in the final .wasm SIDE_MODULE.
    cgame_env.Replace(SHLINKFLAGS=[
        "-sSIDE_MODULE=2",
        "-pthread",
        "-sDISABLE_EXCEPTION_CATCHING=0",
        "-sSUPPORT_LONGJMP=emscripten",
        "-Wl,--allow-multiple-definition",
    ])
    # Use .wasm suffix so the web cgame artifact (bin/libcgame.wasm) cannot be
    # confused with the native ELF build (bin/libcgame.so).  build-web.sh checks
    # for libcgame.wasm first, then falls back to libcgame.so.  If the native
    # libcgame.so already exists, SCons would re-use it without rebuilding when
    # the target name is the same.  Distinct names prevent that staleness.
    cgame_env["SHLIBSUFFIX"] = ".wasm"

# Remap sources to the chosen cgame variant directory
def cgame_path(src):
    """Remap code/xxx to <variant_dir>/xxx for variant dir isolation."""
    if src.startswith("code/"):
        return _cgame_variant_dir + "/" + src[5:]
    return src

cgame_sources_raw = []

# cgame module sources
for root, dirs, files in os.walk("code/cgame"):
    for f in files:
        if f.endswith((".c", ".cpp")):
            cgame_sources_raw.append(os.path.join(root, f))

# BG (shared game) sources
cgame_sources_raw += [
    "code/fgame/bg_misc.cpp",
    "code/fgame/bg_pmove.cpp",
    "code/fgame/bg_slidemove.cpp",
    "code/fgame/bg_voteoptions.cpp",
]

# SCRIPT_SYSTEM_SOURCES (corepp + script helpers — compiled with CGAME_DLL)
cgame_sources_raw += [
    "code/corepp/class.cpp",
    "code/corepp/con_set.cpp",
    "code/corepp/con_timer.cpp",
    "code/corepp/delegate.cpp",
    "code/corepp/lightclass.cpp",
    "code/corepp/listener.cpp",
    "code/corepp/lz77.cpp",
    "code/corepp/mem_blockalloc.cpp",
    "code/corepp/mem_tempalloc.cpp",
    "code/corepp/script.cpp",
    "code/corepp/str.cpp",
    "code/script/scriptexception.cpp",
    "code/script/scriptvariable.cpp",
]

# Shared engine sources needed by cgame
cgame_sources_raw += [
    "code/qcommon/q_math.c",
    "code/qcommon/q_shared.c",
]

cgame_sources = [cgame_path(s) for s in cgame_sources_raw]

cgame_lib = cgame_env.SharedLibrary(
    target="bin/cgame",
    source=cgame_sources,
)

# Build cgame alongside the main library
Default(cgame_lib)

# ══════════════════════════════════════════════
#  Unit Tests
# ══════════════════════════════════════════════

# Test for godot_render_sort
test_env = env.Clone()
test_env.Append(CPPPATH=["code/godot"])

test_render_sort_sources = [
    "code/godot/tests/test_render_sort.cpp",
    "code/godot/godot_render_sort.cpp",
]

test_render_sort_prog = test_env.Program(
    target="bin/test_render_sort",
    source=test_render_sort_sources,
)
