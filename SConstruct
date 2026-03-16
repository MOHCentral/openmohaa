#!/usr/bin/env python
import os
import subprocess
import sys

sys.setrecursionlimit(10000)

env = SConscript("../godot-cpp/SConstruct")

# Prevent Linux host shell argv overflows when archiving very large static libs
# (notably godot-cpp) from long workspace paths. This must also apply to
# Linux-hosted cross-builds (e.g. platform=macos with osxcross, platform=windows
# with MinGW).
# Disable for Web to avoid RecursionError during Emscripten SHLINK expansion.
if sys.platform.startswith("linux") and env.get("ARCOM") and env.get("platform") != "web":
    if env.get("platform") in ("macos", "windows"):
        # cctools ar (macOS) and MinGW ar don't support @response files;
        # archive incrementally to keep each subprocess argv below ARG_MAX.
        def _archive_in_chunks(target, source, env):
            ar = env.subst("$AR")
            ranlib = env.subst("$RANLIB")
            out = str(target[0])

            if os.path.exists(out):
                os.remove(out)

            # Use 'q' (quick append) NOT 'r' (replace) — duplicate basenames
            # like object.o (src/core/ vs gen/src/classes/) would silently
            # replace each other with 'r', losing symbols.
            # 'P' stores full path names so MinGW ar treats them as distinct
            # members even with the same basename.  cctools ar (macOS/osxcross)
            # does NOT support 'P', so omit it for macOS.
            plat = env.get("platform", "")
            use_P = "P" if plat != "macos" else ""
            chunk_size = 128
            for i in range(0, len(source), chunk_size):
                chunk = [str(s) for s in source[i:i + chunk_size]]
                flags = ("qcS" if i == 0 else "qS") + use_P
                cmd = [ar, flags, out] + chunk
                subprocess.check_call(cmd)

            if ranlib:
                subprocess.check_call([ranlib, out])

            return 0

        env["ARCOM"] = _archive_in_chunks
    else:
        env["ARCOM_POSIX"] = env["ARCOM"].replace("$TARGET", "$TARGET.posix").replace("$SOURCES", "$SOURCES.posix")
        env["ARCOM"] = "${TEMPFILE(ARCOM_POSIX)}"

# ──────────────────────────────────────────────
#  OpenMoHAA GDExtension Build — Full Client + Server
# ──────────────────────────────────────────────
#
# Module breakdown:
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
if env.get("platform") == "linux":
    env.Append(LINKFLAGS=["-Wl,--export-dynamic"])

env.Append(CPPPATH=[
    "code",
    "code/botlib",
    "code/cgame",
    "code/client",
    # code/fgame is added conditionally below — on Windows it uses -iquote
    # to prevent code/fgame/windows.h (a game entity class) from shadowing
    # the system <windows.h>.
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

# code/fgame/ contains a "windows.h" header (WindowObject entity class) that
# shadows the system <windows.h> on Windows when added via -I.
# On GCC/MinGW we use -iquote which is only searched for #include "file"
# (quoted), NOT for #include <file> (angled).
# On MSVC, -iquote is not supported.  Instead we prepend the Windows SDK
# um/ include directory before code/fgame in the /I search order so that
# #include <windows.h> finds the system header first, while
# #include "windows.h" from within code/fgame/ still finds the local copy
# via MSVC's "search parent directory of the including file" rule.
if env.get("platform") == "windows":
    _is_mingw = "mingw" in env.get("CC", "").lower() or "mingw" in env.get("CXX", "").lower()
    if not _is_mingw and os.name != "nt":
        _is_mingw = True  # Cross-compiling from Linux → assume MinGW
    if _is_mingw:
        env.Append(CCFLAGS=["-iquote", "code/fgame"])
    else:
        # MSVC: prepend Windows SDK um/ directory so <windows.h> resolves to
        # the system header before /I code/fgame.
        _sdk_dir = os.environ.get("WindowsSdkDir", "")
        _sdk_ver = os.environ.get("WindowsSDKVersion", "").rstrip("\\")
        if _sdk_dir and _sdk_ver:
            _sdk_um = os.path.join(_sdk_dir, "include", _sdk_ver, "um")
            env.Prepend(CPPPATH=[_sdk_um])
        env.Append(CPPPATH=["code/fgame"])
else:
    env.Append(CPPPATH=["code/fgame"])

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
    env.Append(CPPDEFINES=["_LINUX", "__linux__", "_UNIX"])
    env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
    env.Append(CXXFLAGS=["-fexceptions", "-frtti"])
    sys_sources.append("code/sys/sys_unix.c")
    sys_sources.append("code/sys/con_tty.c")
    sys_sources.append("code/sys/sys_curl.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "windows":
    env.Append(CPPDEFINES=["_WIN32", "WIN32", "_WINDOWS", "_USE_MATH_DEFINES"])
    if env["arch"] in ["x86_64", "arm64"]:
        env.Append(CPPDEFINES=["_WIN64"])
    # Detect MinGW vs MSVC to use the right compiler flags.
    _is_mingw_sys = "mingw" in env.get("CC", "").lower() or "mingw" in env.get("CXX", "").lower()
    if not _is_mingw_sys and os.name != "nt":
        _is_mingw_sys = True  # Cross-compiling from Linux → assume MinGW
    if _is_mingw_sys:
        env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
        env.Append(CXXFLAGS=["-fexceptions", "-frtti"])
    # /EHsc is added by godot-cpp when disable_exceptions=no — no /TP:
    # C files compile as C (MSVC default); C++ files compile as C++ by extension.
    sys_sources.append("code/sys/sys_win32.c")
    sys_sources.append("code/sys/con_win32.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "android":
    env.Append(CPPDEFINES=["_LINUX", "__linux__", "ANDROID"])
    env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
    env.Append(CXXFLAGS=["-fexceptions", "-frtti"])
    sys_sources.append("code/sys/sys_unix.c")
    sys_sources.append("code/sys/con_tty.c")
    sys_sources.append("code/sys/sys_curl.c")
    sys_sources.append("code/sys/win_localization.cpp")
    sys_sources.append("code/sys/win_bounds.cpp")
elif env["platform"] == "macos":
    env.Append(CPPDEFINES=["__APPLE__", "MACOS_X", "_MACOSX", "_UNIX"])
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
    # godot-cpp injects -fno-exceptions and -sSUPPORT_LONGJMP='wasm' by
    # default.  We need C++ throw/catch for the engine, so we must use
    # -fexceptions (Emscripten JS-based EH via invoke_* wrappers).
    # -fwasm-exceptions would import a __cpp_exception WebAssembly.Tag that
    # Godot's main module doesn't provide (LinkError at instantiation).
    # -fexceptions is INCOMPATIBLE with -sSUPPORT_LONGJMP='wasm' at the
    # compiler level, so we must strip the wasm longjmp flag too and fall
    # back to Emscripten's JS-based longjmp (invoke_* + __THREW__).
    # The JS patches in build-web.sh provide emscripten_longjmp fallbacks.
    env["CXXFLAGS"] = [f for f in env.get("CXXFLAGS", []) if str(f) != "-fno-exceptions"]
    env["CCFLAGS"] = [f for f in env.get("CCFLAGS", []) if "-sSUPPORT_LONGJMP" not in str(f)]
    env["LINKFLAGS"] = [f for f in env.get("LINKFLAGS", []) if "-sSUPPORT_LONGJMP" not in str(f)]
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
    # GL stubs — duplicates resolved by #ifndef GODOT_GDEXTENSION guards in real files
    # (Linux also uses -z muldefs as a safety net; web relies solely on guards)
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
    # Render capture: engine pipeline sprite/entity vertex computation
    "code/renderergl1/godot_render_capture.c",
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

    # GameSpy platform wrappers (gsPlatformThread/Util/Socket) include their
    # platform implementation .c files directly. Building common/linux/*.c as
    # standalone translation units causes duplicate symbols on non-Linux
    # targets (and is redundant on Linux too).
    if "code/gamespy/common/linux/" in fp:
        return True

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
    # Use Emscripten JS-based exception handling (invoke_* wrappers).
    # -fwasm-exceptions would import __cpp_exception WebAssembly.Tag which
    # Godot's main module does not provide.
    env.Append(LINKFLAGS=["-fexceptions"])
    # Note: wasm-ld does not support -z muldefs. Duplicate symbols between
    # tr_godot_gl_stubs.c and real renderer files are resolved by
    # #ifndef GODOT_GDEXTENSION guards in the real renderer source files.
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
    # macOS ld64 does not support -multiply_defined suppress for dylibs.
    # Duplicates are resolved in a pre-link step via llvm-objcopy (see below).
    # -fcommon merges tentative C globals automatically.
    env.Append(CFLAGS=["-fcommon"])
    env.Append(LINKFLAGS=[
        "-Wl,-undefined,dynamic_lookup",
        "-Wl,-w",
    ])
elif env["platform"] == "windows":
    # Detect MinGW vs MSVC — MinGW uses GCC-style flags, MSVC uses /FORCE etc.
    _is_mingw = "mingw" in env.get("CC", "").lower() or "mingw" in env.get("CXX", "").lower() or env.get("tools", [""])[0] == "mingw"
    if not _is_mingw and os.name != "nt":
        # Cross-compiling from Linux — assume MinGW
        _is_mingw = True
    if _is_mingw:
        env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])
        # godot-cpp's windows.py adds --no-undefined, but GDExtension symbols
        # (godot::internal::*) are resolved at runtime by the Godot engine.
        # Strip it so the monolithic link succeeds.
        env["LINKFLAGS"] = [f for f in env.get("LINKFLAGS", []) if "--no-undefined" not in str(f)]
    else:
        # MSVC linker allows multiple definitions by default (/FORCE:MULTIPLE)
        env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])

# ── Link system libraries ──
if env["platform"] == "linux":
    env.Append(LIBS=["z", "dl"])  # zlib, dlopen
elif env["platform"] == "macos":
    env.Append(LIBS=["z", "dl"])  # zlib, dlopen
    # Cross-compiling from Linux: SCons defaults SHLIBSUFFIX to ".so".
    # macOS convention (and .gdextension expects) ".dylib".
    env["SHLIBSUFFIX"] = ".dylib"
elif env["platform"] == "windows":
    # Embed zlib sources instead of linking a system library (CI runners
    # and cross-compilation environments may not have a pre-built zlib).
    env.Append(CPPPATH=["code/thirdparty/zlib-1.3.1"])
    sources.extend([
        "code/thirdparty/zlib-1.3.1/adler32.c",
        "code/thirdparty/zlib-1.3.1/crc32.c",
        "code/thirdparty/zlib-1.3.1/inffast.c",
        "code/thirdparty/zlib-1.3.1/inflate.c",
        "code/thirdparty/zlib-1.3.1/inftrees.c",
        "code/thirdparty/zlib-1.3.1/zutil.c",
    ])
    env.Append(LIBS=["ws2_32", "winmm", "shell32", "advapi32", "user32", "kernel32", "psapi", "crypt32"])  # winsock, multimedia timer, SHGetFolderPath, registry/crypt, MessageBox, OS basics, process info, crypto
    # gdextension expects "openmohaa.dll" (no lib prefix).
    env["SHLIBPREFIX"] = ""

# ── Build the shared library ──
library = env.SharedLibrary(
    target="bin/openmohaa",
    source=sources,
)

# ── macOS pre-link: resolve duplicate symbols ──
# macOS ld64 does not support -z muldefs / -multiply_defined suppress for
# dylibs.  Instead we use llvm-objcopy --localize-symbol on later .os files
# to hide duplicate definitions, matching Linux -z muldefs first-wins order.
# NOTE: llvm-nm on fat (universal) Mach-O reports symbols from BOTH arch
# slices — we must only count cross-file duplicates (same sym, different file).
if env["platform"] == "macos":
    def _macos_localize_dups(target, source, env):
        import subprocess

        nm = "llvm-nm"
        objcopy = "llvm-objcopy"

        seen = {}          # symbol -> first .os file path
        to_localize = {}   # filepath -> list of symbols to localize

        for src in source:
            path = str(src)
            try:
                out = subprocess.check_output(
                    [nm, "-g", "--defined-only", "--format=posix", path],
                    stderr=subprocess.DEVNULL, text=True,
                )
            except (subprocess.CalledProcessError, FileNotFoundError):
                continue

            for line in out.strip().split("\n"):
                if not line:
                    continue
                parts = line.split()
                if len(parts) < 2:
                    continue
                sym = parts[0]
                stype = parts[1]
                # T=text, D=data, B=bss, S=section — skip C (common, handled
                # by -fcommon) and other minor types.
                if stype not in ("T", "D", "B", "S"):
                    continue
                if sym in seen:
                    # Fat Mach-O: nm reports each arch's symbols separately.
                    # Only localize if the duplicate is in a DIFFERENT file.
                    if seen[sym] != path:
                        to_localize.setdefault(path, []).append(sym)
                else:
                    seen[sym] = path

        total = sum(len(s) for s in to_localize.values())
        if total:
            print("  macOS: localizing %d duplicate symbol(s) across %d object file(s)" % (total, len(to_localize)))
        for path, syms in to_localize.items():
            cmd = [objcopy]
            for sym in syms:
                cmd.extend(["--localize-symbol", sym])
            cmd.append(path)
            subprocess.check_call(cmd)

        return None

    env.AddPreAction(library, env.Action(_macos_localize_dups, "Resolving macOS duplicate symbols..."))

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
    # code/fgame added conditionally below (iquote on Windows)
    "code/qcommon",
    "code/renderercommon",
    "code/server",
    "code/script",
    "code/skeletor",
    "code/tiki",
    "code/corepp",
    "generated",
])

# Same -iquote / MSVC treatment as main env — see comment in global CPPPATH block.
if env.get("platform") == "windows":
    _is_mingw_cgame = "mingw" in env.get("CC", "").lower() or "mingw" in env.get("CXX", "").lower()
    if not _is_mingw_cgame and os.name != "nt":
        _is_mingw_cgame = True
    if _is_mingw_cgame:
        cgame_env.Append(CCFLAGS=["-iquote", "code/fgame"])
    else:
        _sdk_dir = os.environ.get("WindowsSdkDir", "")
        _sdk_ver = os.environ.get("WindowsSDKVersion", "").rstrip("\\")
        if _sdk_dir and _sdk_ver:
            _sdk_um = os.path.join(_sdk_dir, "include", _sdk_ver, "um")
            cgame_env.Prepend(CPPPATH=[_sdk_um])
        cgame_env.Append(CPPPATH=["code/fgame"])
else:
    cgame_env.Append(CPPPATH=["code/fgame"])

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
    cgame_env.Append(CPPDEFINES=["__APPLE__", "MACOS_X", "_MACOSX", "_UNIX"])
    # Reuse the macOS toolchain selected by godot-cpp (native clang or osxcross).
    for tool_var in ("CC", "CXX", "AR", "RANLIB", "AS", "SHLINK"):
        if env.get(tool_var):
            cgame_env[tool_var] = env[tool_var]
    if env.get("ENV", {}).get("PATH"):
        cgame_env["ENV"]["PATH"] = env["ENV"]["PATH"]
    # Propagate architecture flags from main env (universal: -arch x86_64 -arch arm64)
    _arch_flags = [str(f) for f in env.get("CCFLAGS", []) if str(f) in ("-arch",) or str(f) in ("x86_64", "arm64", "universal")]
    # Rebuild -arch pairs from the main env's CCFLAGS list
    _main_ccflags = [str(f) for f in env.get("CCFLAGS", [])]
    _arch_pairs = []
    for i, f in enumerate(_main_ccflags):
        if f == "-arch" and i + 1 < len(_main_ccflags):
            _arch_pairs.extend(["-arch", _main_ccflags[i + 1]])
    if _arch_pairs:
        cgame_env.Append(CCFLAGS=_arch_pairs)
        cgame_env.Append(LINKFLAGS=_arch_pairs)
    cgame_env["SHLIBSUFFIX"] = ".dylib"
    cgame_env.Append(CCFLAGS=[
        "-fPIC", "-g", "-O2",
        "-fvisibility=hidden",
    ])
    cgame_env.Append(CFLAGS=["-Wno-incompatible-pointer-types"])
    cgame_env.Append(CXXFLAGS=["-std=c++17", "-fexceptions", "-frtti"])
    cgame_env.Append(LINKFLAGS=["-Wl,-undefined,dynamic_lookup", "-Wl,-w"])
elif env["platform"] == "windows":
    cgame_env.Append(CPPDEFINES=["_WIN32", "WIN32", "_WINDOWS", "_USE_MATH_DEFINES"])
    # Detect MinGW vs MSVC for cgame build.
    _cgame_mingw = "mingw" in env.get("CC", "").lower() or "mingw" in env.get("CXX", "").lower()
    if not _cgame_mingw and os.name != "nt":
        _cgame_mingw = True  # Cross-compiling from Linux → assume MinGW
    if _cgame_mingw:
        # Inherit cross-compiler from the main env.
        for tool_var in ("CC", "CXX", "AR", "RANLIB", "AS", "SHLINK"):
            if env.get(tool_var):
                cgame_env[tool_var] = env[tool_var]
        if env.get("ENV", {}).get("PATH"):
            cgame_env["ENV"]["PATH"] = env["ENV"]["PATH"]
        cgame_env.Append(CCFLAGS=["-O2", "-fvisibility=hidden"])
        cgame_env.Append(CFLAGS=["-Wno-discarded-qualifiers", "-Wno-incompatible-pointer-types"])
        cgame_env.Append(CXXFLAGS=["-std=c++17", "-fexceptions", "-frtti"])
        cgame_env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])
    else:
        cgame_env.Append(CCFLAGS=["/EHsc", "/O2"])
        cgame_env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])
    # cgame_env is a bare Environment() so it defaults to Linux naming.
    # Force Windows output: cgame.dll (no lib prefix).
    cgame_env["SHLIBPREFIX"] = ""
    cgame_env["SHLIBSUFFIX"] = ".dll"
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
    cgame_env.Append(CPPDEFINES=["__EMSCRIPTEN__", "_LINUX", "__linux__", "GODOT_GDEXTENSION"])
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
    ])
    # Replace SHLINKFLAGS entirely — we do NOT want SCons's default -shared
    # flag; emcc uses -sSIDE_MODULE instead.
    # -pthread must also appear in link flags so emcc generates shared-memory
    # imports in the final .wasm SIDE_MODULE.
    cgame_env.Replace(SHLINKFLAGS=[
        "-sSIDE_MODULE=2",
        "-pthread",
        "-fexceptions",
        "-Wl,-z,muldefs",
        "-sEXPORTED_FUNCTIONS=['_GetCGameAPI']",
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

# macOS pre-link for cgame: same duplicate-symbol resolution as main library
if env["platform"] == "macos":
    cgame_env.AddPreAction(cgame_lib, env.Action(_macos_localize_dups, "Resolving macOS cgame duplicate symbols..."))

# Build cgame alongside the main library
Default(cgame_lib)

# ══════════════════════════════════════════════
#  Unit Tests
# ══════════════════════════════════════════════

# Test for godot_render_sort
test_env = env.Clone()
test_env.Append(CPPPATH=["code/godot"])
test_env.VariantDir("build/test_render_sort", "code", duplicate=0)

test_render_sort_sources = [
    "build/test_render_sort/godot/tests/test_render_sort.cpp",
    "build/test_render_sort/godot/godot_render_sort.cpp",
]

test_render_sort_prog = test_env.Program(
    target="bin/test_render_sort",
    source=test_render_sort_sources,
)
