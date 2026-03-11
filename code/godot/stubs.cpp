/*
 * stubs.cpp — Residual stubs for the Godot GDExtension build.
 *
 * With the full client + server build, most stubs that were here
 * previously are now provided by real code:
 *   CL_*       → code/client/cl_main.cpp, cl_keys.cpp, cl_scrn.cpp, etc.
 *   Key_*      → code/client/cl_keys.cpp
 *   SCR_*      → code/client/cl_scrn.cpp
 *   IN_*       → code/godot/godot_input.c
 *   S_*        → code/godot/godot_sound.c
 *   GetRefAPI  → code/godot/godot_renderer.c
 *
 * What remains here:
 *   - Sys_GetGameAPI / Sys_GetCGameAPI (monolithic DLL loading)
 *   - Sys misc stubs (platform functions not needed under Godot)
 *   - VM stubs
 *   - Renderer-thread stubs
 *   - Clipboard / registry stubs
 *   - UpdateChecker stubs (avoids pulling in cURL / json / threads)
 *   - Cursor / mouse stubs (replaces SDL mouse code)
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

// sys_loadlib.h must be inside extern "C" to match C linkage of Sys_LoadDll
// (defined in sys_main.c which is compiled as C).
extern "C" {
#include "../sys/sys_loadlib.h"
}

#include <cstring>
#include <chrono>
#include <random>
#include <errno.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <direct.h>   // _getcwd, _mkdir
#  include <sys/stat.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <libgen.h>
#  include <dlfcn.h>
#endif

// ──────────────────────────────────────────────
//  UpdateChecker stubs — the real implementation in sys_update_checker.cpp
//  pulls in cURL, json.hpp, and std::thread which we don't need under Godot.
//  We provide a minimal matching class so the global symbol resolves.
// ──────────────────────────────────────────────
#include "../sys/sys_update_checker.h"

// Provide the global that other TUs reference via `extern UpdateChecker updateChecker`.
UpdateChecker updateChecker;

void Sys_UpdateChecker_Init()  {}
void Sys_UpdateChecker_Process() {}
void Sys_UpdateChecker_Shutdown() {}

// UpdateChecker member functions — stubs
UpdateChecker::UpdateChecker() : lastMajor(0), lastMinor(0), lastPatch(0), versionChecked(false), thread(NULL) {}
UpdateChecker::~UpdateChecker() {}
void UpdateChecker::Init() {}
void UpdateChecker::Process() {}
void UpdateChecker::Shutdown() {}
void UpdateChecker::CheckInitClientThread() {}
bool UpdateChecker::CanHaveRequestThread() const { return false; }
void UpdateChecker::SetLatestVersion(int, int, int) {}
bool UpdateChecker::CheckNewVersion() const { return false; }
bool UpdateChecker::CheckNewVersion(int& major, int& minor, int& patch) const { return false; }

// UpdateCheckerThread — minimal stubs
UpdateCheckerThread::UpdateCheckerThread() : handle(NULL), osThread(NULL), requestThreadIsActive(qfalse), shouldBeActive(qfalse) {}
UpdateCheckerThread::~UpdateCheckerThread() {}
void UpdateCheckerThread::Init() {}
void UpdateCheckerThread::Shutdown() {}
bool UpdateCheckerThread::IsRoutineActive() const { return false; }
void UpdateCheckerThread::InitClient() {}
void UpdateCheckerThread::ShutdownClient() {}
bool UpdateCheckerThread::ParseVersionNumber(const char*, int&, int&, int&) const { return false; }
void UpdateCheckerThread::DoRequest() {}
void UpdateCheckerThread::RequestThread() {}

extern "C" {

// ──────────────────────────────────────────────
//  Sys — Game DLL loading (monolithic build)
//  Instead of dlopen, we call GetGameAPI / GetCGameAPI directly.
// ──────────────────────────────────────────────

// Forward-declare the game exports
typedef struct game_export_s game_export_t;
typedef struct game_import_s game_import_t;
extern game_export_t *GetGameAPI(game_import_t *import);

// Forward-declare the cgame export.
// NOTE: cgame is NOT compiled in the monolithic build (it shares corepp/
// with fgame but needs CGAME_DLL, which conflicts with GAME_DLL).
// Returning NULL makes the client skip cgame initialisation for now.
// A future phase will compile cgame separately or provide real stubs.

// cgame is compiled as a separate shared library because it shares corepp/
// with fgame but needs CGAME_DLL.  We load it via the engine's Sys_LoadDll.
static void *cgame_library = NULL;
// When true, final shutdown is in progress — do NOT unload cgame
// because atexit/static-destructor handlers may reference its code pages.
static qboolean cgame_shutting_down = qfalse;

#ifdef _WIN32
static const char *cgame_gamename = "cgame.dll";
#else
static const char *cgame_gamename = "cgame.so";
#endif

void Sys_CGameFinalShutdown(void) {
    cgame_shutting_down = qtrue;
}

void *Sys_GetGameAPI(void *parms) {
    return (void *)GetGameAPI((game_import_t *)parms);
}

void Sys_UnloadGame(void) {
    Com_Printf("GDExtension: Sys_UnloadGame (monolithic, no-op)\n");
}

void *Sys_GetCGameAPI(void *parms) {
    typedef void *(*GetCGameAPI_t)(void);
    GetCGameAPI_t getCGameAPI = NULL;

    if (cgame_library) {
        Com_Printf("GDExtension: Sys_GetCGameAPI — already loaded\n");
        return NULL;
    }

#ifdef __EMSCRIPTEN__
    {
        // Primary web path: cgame.so is preloaded via loadDylibs() with global
        // visibility, so resolve the export directly first. This avoids a
        // synchronous dlopen attempt from the worker thread.
        getCGameAPI = (GetCGameAPI_t)dlsym(RTLD_DEFAULT, "GetCGameAPI");
        if (!getCGameAPI) {
            getCGameAPI = (GetCGameAPI_t)dlsym(RTLD_DEFAULT, "_GetCGameAPI");
        }
        if (getCGameAPI) {
            Com_Printf("GDExtension: Sys_GetCGameAPI — resolved from preloaded globals\n");
            return (void *)getCGameAPI();
        }

        // Fallback: try dlopen on known VFS paths (cgame.so is preloaded into
        // the Emscripten virtual FS by the pk3 preloader in build-web.sh).
        const char *web_paths[] = {
            "/main/cgame.so",
            "main/cgame.so",
            "/cgame.so",
            NULL,
        };
        for (int wi = 0; web_paths[wi]; wi++) {
            void *wlib = dlopen(web_paths[wi], RTLD_NOW | RTLD_GLOBAL);
            if (wlib) {
                getCGameAPI = (GetCGameAPI_t)dlsym(wlib, "GetCGameAPI");
                if (!getCGameAPI) getCGameAPI = (GetCGameAPI_t)dlsym(wlib, "_GetCGameAPI");
                if (getCGameAPI) {
                    cgame_library = wlib;
                    Com_Printf("GDExtension: Sys_GetCGameAPI — dlopen('%s') OK\n", web_paths[wi]);
                    return (void *)getCGameAPI();
                }
                dlclose(wlib);
            }
        }
        Com_Printf("GDExtension: Sys_GetCGameAPI — cgame not loadable on web (checked RTLD_DEFAULT + dlopen paths)\n");
        return NULL;
    }
#else

    /* Try next to the GDExtension library first (e.g. project/bin/cgame.dll),
       then fall back to fs_homedatapath/game/, fs_basepath/game/, fs_homepath/game/.
       This keeps the main/ game directory free of build artifacts. */
    {
        char libPath[1024];

#ifdef _WIN32
        /* On Windows, use GetModuleHandleEx + GetModuleFileName to locate
           the directory containing the GDExtension DLL itself. */
        {
            HMODULE hModule = NULL;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)Sys_GetCGameAPI, &hModule) && hModule) {
                char pathBuf[1024];
                DWORD len = GetModuleFileNameA(hModule, pathBuf, sizeof(pathBuf));
                if (len > 0 && len < sizeof(pathBuf)) {
                    /* Strip the filename to get the directory. */
                    char *lastSlash = strrchr(pathBuf, '\\');
                    char *lastFwd   = strrchr(pathBuf, '/');
                    char *sep = lastSlash > lastFwd ? lastSlash : lastFwd;
                    if (sep) *sep = '\0';
                    Com_sprintf(libPath, sizeof(libPath), "%s\\%s", pathBuf, cgame_gamename);
                    Com_Printf("GDExtension: trying cgame next to library at \"%s\"...\n", libPath);
                    cgame_library = Sys_LoadLibrary(libPath);
                    if (!cgame_library) {
                        const char *dlErr = Sys_LibraryError();
                        Com_Printf("GDExtension: LoadLibrary failed: %s\n", dlErr ? dlErr : "(null)");
                    }
                }
            }
        }
#else
        /* On POSIX, use dladdr to locate the directory containing our .so. */
        {
            Dl_info dlInfo;
            if (dladdr((void *)Sys_GetCGameAPI, &dlInfo) && dlInfo.dli_fname) {
                char pathBuf[1024];
                Q_strncpyz(pathBuf, dlInfo.dli_fname, sizeof(pathBuf));
                char *dir = dirname(pathBuf);
                Com_sprintf(libPath, sizeof(libPath), "%s/%s", dir, cgame_gamename);
                Com_Printf("GDExtension: trying cgame next to library at \"%s\"...\n", libPath);
                cgame_library = Sys_LoadLibrary(libPath);
                if (!cgame_library) {
                    const char *dlErr = Sys_LibraryError();
                    Com_Printf("GDExtension: dlopen failed: %s\n", dlErr ? dlErr : "(null)");
                }
            }
        }
#endif

        if (!cgame_library) {
            const char *gameDir = Cvar_VariableString("fs_game");
            const char *paths[3];
            int i;

            if (!gameDir || !*gameDir) gameDir = "main";

            paths[0] = Cvar_VariableString("fs_homedatapath");
            paths[1] = Cvar_VariableString("fs_basepath");
            paths[2] = Cvar_VariableString("fs_homepath");

            for (i = 0; i < 3 && !cgame_library; i++) {
                if (paths[i] && *paths[i]) {
                    Com_sprintf(libPath, sizeof(libPath), "%s/%s/%s", paths[i], gameDir, cgame_gamename);
                    Com_Printf("GDExtension: trying cgame at \"%s\"...\n", libPath);
                    cgame_library = Sys_LoadLibrary(libPath);
                    if (!cgame_library) {
                        const char *dlErr = Sys_LibraryError();
                        Com_Printf("GDExtension: library load failed: %s\n", dlErr ? dlErr : "(null)");
                    }
                }
            }
        }
    }
#endif

    if (!cgame_library) {
        cgame_library = Sys_LoadDll(cgame_gamename, 0);
    }

    if (!cgame_library) {
        Com_Printf("GDExtension: Sys_GetCGameAPI — could not load %s, running without cgame\n", cgame_gamename);
        return NULL;
    }

    Com_Printf("GDExtension: Sys_GetCGameAPI(%s) — loaded OK\n", cgame_gamename);

    getCGameAPI = (GetCGameAPI_t)Sys_LoadFunction(cgame_library, "GetCGameAPI");
    if (!getCGameAPI) {
        Com_Printf("GDExtension: GetCGameAPI symbol not found in %s\n", cgame_gamename);
        Sys_UnloadLibrary(cgame_library);
        cgame_library = NULL;
        return NULL;
    }

    return (void *)getCGameAPI();
}

void Sys_UnloadCGame(void) {
    if (cgame_library) {
        if (cgame_shutting_down) {
            // Final shutdown — do NOT unload the cgame library.  C++ static
            // destructors inside it may have registered atexit handlers;
            // closing the library would unmap those code pages and crash
            // when the handlers run at process exit.
            Com_Printf("GDExtension: Sys_UnloadCGame — keeping cgame mapped (final shutdown)\n");
        } else {
            // Map reload — unload so that the next Sys_GetCGameAPI
            // re-opens a fresh copy with all static data reinitialised.
            Com_Printf("GDExtension: Sys_UnloadCGame — closing cgame (map reload)\n");
            Sys_UnloadLibrary(cgame_library);
        }
        cgame_library = NULL;
    }
}

// ──────────────────────────────────────────────
//  UI stubs — residual symbols not provided by the real UI code.
//  UI_ClearResource is defined in cl_ui.cpp (the real implementation).
// ──────────────────────────────────────────────

qboolean UI_GameCommand(void) {
    return qfalse;
}

// ──────────────────────────────────────────────
//  Sys — Misc system stubs
// ──────────────────────────────────────────────

void Sys_ShowConsole(int visLevel, qboolean quitOnClose) {
}

void Sys_CloseMutex(void) {
}

void Sys_InitEx(void) {
}

void Sys_ShutdownEx(void) {
}

void Sys_ProcessBackgroundTasks(void) {
}

// ── Platform path / file-system stubs ──
// On Windows, sys_win32.c + con_win32.c provide most platform functions
// (Sys_ListFiles, Sys_Cwd, Sys_Mkdir, CON_Init, etc.).  On Emscripten,
// we use fixed VFS paths.  On native Unix (Linux/macOS), we use POSIX
// APIs and XDG paths from sys_unix.c.

#ifdef __EMSCRIPTEN__
static char s_web_home_path[] = "/userfs";
static char s_web_install_path[] = "/";
#endif
static char s_web_empty_path[] = "";

// ── Install path storage (all platforms) ──
static char s_install_path[MAX_OSPATH] = { 0 };

void Sys_SetDefaultInstallPath(const char *path) {
    if (path) {
        Q_strncpyz(s_install_path, path, sizeof(s_install_path));
    }
}

// ── Path stubs: three-way split (Emscripten / Windows / Unix) ──

#ifdef __EMSCRIPTEN__
// Web/Emscripten: use fixed VFS paths
char *Sys_DefaultHomePath(void) {
    return s_web_home_path;
}
char *Sys_DefaultHomeConfigPath(void) {
    return s_web_home_path;
}
char *Sys_DefaultHomeDataPath(void) {
    return s_web_home_path;
}
char *Sys_DefaultHomeStatePath(void) {
    return s_web_home_path;
}
char *Sys_DefaultInstallPath(void) {
    return s_web_install_path;
}
char *Sys_DefaultAppPath(void) {
    return s_web_install_path;
}
char *Sys_DefaultBasePath(void) {
    return s_web_install_path;
}
char *Sys_SteamPath(void) {
    return s_web_empty_path;
}
char *Sys_GogPath(void) {
    return s_web_empty_path;
}
char *Sys_MicrosoftStorePath(void) {
    return s_web_empty_path;
}
char *Sys_DefaultUserPath(void) {
    return s_web_home_path;
}
char *Sys_DefaultOutputPath(void) {
    return s_web_home_path;
}

#elif defined(_WIN32)
// Windows: sys_win32.c provides Sys_DefaultHomeConfigPath,
// Sys_DefaultHomeDataPath, Sys_DefaultHomeStatePath, Sys_SteamPath,
// Sys_GogPath, Sys_MicrosoftStorePath, Sys_Basename, Sys_Dirname,
// Sys_FOpen, Sys_Mkdir, Sys_Mkfifo, Sys_Cwd, Sys_ListFiles,
// Sys_FreeFileList, Sys_PlatformInit, Sys_SetEnv, Sys_GetCurrentUser,
// Sys_PID, Sys_PIDIsRunning, Sys_DllExtension, Sys_Milliseconds,
// Sys_RandomBytes, Sys_PlatformExit.
// con_win32.c provides CON_Init, CON_Shutdown, CON_Input.
// We only provide the Install/App/Base/Home/User/Output path stubs.

char *Sys_DefaultHomePath(void) {
    return Sys_DefaultHomeDataPath();
}
char *Sys_DefaultInstallPath(void) {
    if (*s_install_path)
        return s_install_path;
    static char cwd[MAX_OSPATH];
    if (!cwd[0]) {
        if (!_getcwd(cwd, sizeof(cwd) - 1))
            Q_strncpyz(cwd, ".", sizeof(cwd));
        cwd[MAX_OSPATH - 1] = 0;
    }
    return cwd;
}
char *Sys_DefaultAppPath(void) {
    return Sys_DefaultInstallPath();
}
char *Sys_DefaultBasePath(void) {
    return Sys_DefaultInstallPath();
}
char *Sys_DefaultUserPath(void) {
    return Sys_DefaultHomeDataPath();
}
char *Sys_DefaultOutputPath(void) {
    return Sys_DefaultHomeDataPath();
}

#else
// Native Unix (Linux/macOS): use real XDG paths from sys_unix.c.
// These are defined in code/sys/sys_unix.c and NOT guarded by GODOT_GDEXTENSION.
extern char *Sys_HomeConfigPath(void);
extern char *Sys_HomeDataPath(void);
extern char *Sys_HomeStatePath(void);
extern char *Sys_BinaryPath(void);

char *Sys_DefaultHomePath(void) {
    return Sys_HomeDataPath();
}
char *Sys_DefaultHomeConfigPath(void) {
    return Sys_HomeConfigPath();
}
char *Sys_DefaultHomeDataPath(void) {
    return Sys_HomeDataPath();
}
char *Sys_DefaultHomeStatePath(void) {
    return Sys_HomeStatePath();
}
char *Sys_DefaultInstallPath(void) {
    if (*s_install_path)
        return s_install_path;
    static char cwd[MAX_OSPATH];
    if (!cwd[0]) {
        char *result = getcwd(cwd, sizeof(cwd) - 1);
        if (result != cwd)
            Q_strncpyz(cwd, ".", sizeof(cwd));
        cwd[MAX_OSPATH - 1] = 0;
    }
    return cwd;
}
char *Sys_DefaultAppPath(void) {
    return Sys_BinaryPath();
}
char *Sys_DefaultBasePath(void) {
    return Sys_DefaultInstallPath();
}
char *Sys_SteamPath(void) {
    return s_web_empty_path;
}
char *Sys_GogPath(void) {
    return s_web_empty_path;
}
char *Sys_MicrosoftStorePath(void) {
    return s_web_empty_path;
}
char *Sys_DefaultUserPath(void) {
    return Sys_HomeDataPath();
}
char *Sys_DefaultOutputPath(void) {
    return Sys_HomeDataPath();
}
#endif  // __EMSCRIPTEN__ / _WIN32 / Unix

// ── File system / console / misc platform stubs ──
// On Windows, sys_win32.c + con_win32.c provide all of these.
// On Emscripten and native Unix, we provide our own implementations.

#if !defined(_WIN32)

static constexpr int STUB_MAX_FOUND_FILES = 0x1000;

static void Godot_Sys_ListFilteredFiles(
    const char *basedir,
    const char *subdirs,
    const char *filter,
    qboolean wantsubs,
    char **list,
    int *numfiles
) {
    char search[MAX_OSPATH];
    char newsubdirs[MAX_OSPATH];
    char filename[MAX_OSPATH];
    DIR *fdir;
    struct dirent *d;
    struct stat st;

    if (*numfiles >= STUB_MAX_FOUND_FILES - 1 || !basedir || basedir[0] == '\0') {
        return;
    }

    if (subdirs && subdirs[0]) {
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    } else {
        Com_sprintf(search, sizeof(search), "%s", basedir);
    }

    fdir = opendir(search);
    if (!fdir) {
        return;
    }

    while ((d = readdir(fdir)) != NULL) {
        if (!(Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..") && Q_stricmp(d->d_name, "cvs"))) {
            continue;
        }

        Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
        if (stat(filename, &st) == -1) {
            continue;
        }

        if ((st.st_mode & S_IFDIR) != 0 && wantsubs) {
            if (subdirs && subdirs[0]) {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
            } else {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
            }
            Godot_Sys_ListFilteredFiles(basedir, newsubdirs, filter, wantsubs, list, numfiles);
        }

        if (*numfiles >= STUB_MAX_FOUND_FILES - 1) {
            break;
        }

        if (subdirs && subdirs[0]) {
            Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);
        } else {
            Q_strncpyz(filename, d->d_name, sizeof(filename));
        }

        if (!Com_FilterPath(filter, filename, qfalse)) {
            continue;
        }

        list[*numfiles] = CopyString(filename);
        (*numfiles)++;
    }

    closedir(fdir);
}

const char *Sys_Basename(char *path) {
    return basename(path);
}

const char *Sys_Dirname(char *path) {
    return dirname(path);
}

FILE *Sys_FOpen(const char *ospath, const char *mode) {
    struct stat buf;

    if (ospath && !stat(ospath, &buf) && S_ISDIR(buf.st_mode)) {
        return NULL;
    }

    return fopen(ospath, mode);
}

qboolean Sys_Mkdir(const char *path) {
    int result = mkdir(path, 0750);
    if (result != 0) {
        return (errno == EEXIST) ? qtrue : qfalse;
    }
    return qtrue;
}

FILE *Sys_Mkfifo(const char *ospath) {
    (void)ospath;
    return NULL;
}

char *Sys_Cwd(void) {
    static char cwd[MAX_OSPATH];
    char *result = getcwd(cwd, sizeof(cwd) - 1);
    if (result != cwd) {
        Q_strncpyz(cwd, "/", sizeof(cwd));
    }
    cwd[MAX_OSPATH - 1] = 0;
    return cwd;
}

char **Sys_ListFiles(const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs) {
    struct dirent *d;
    DIR *fdir;
    char search[MAX_OSPATH];
    int nfiles = 0;
    char *list[STUB_MAX_FOUND_FILES];
    char **listCopy;
    int i;
    struct stat st;
    char buffer[64];

    if (!numfiles) {
        return NULL;
    }

    *numfiles = 0;
    if (!directory || directory[0] == '\0') {
        return NULL;
    }

    if (!extension) {
        extension = "";
    }

    if (!filter && (extension[0] != '/' || extension[1])) {
        Q_snprintf(buffer, sizeof(buffer), "*%s", extension);
        filter = buffer;
    }

    if (filter) {
        Godot_Sys_ListFilteredFiles(directory, "", filter, wantsubs, list, &nfiles);
        list[nfiles] = NULL;
        *numfiles = nfiles;
        if (!nfiles) {
            return NULL;
        }

        listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy));
        for (i = 0; i < nfiles; i++) {
            listCopy[i] = list[i];
        }
        listCopy[i] = NULL;
        return listCopy;
    }

    fdir = opendir(directory);
    if (!fdir) {
        return NULL;
    }

    while ((d = readdir(fdir)) != NULL) {
        Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
        if (stat(search, &st) == -1) {
            continue;
        }

        if (!(Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..") && Q_stricmp(d->d_name, "cvs"))) {
            continue;
        }

        if ((st.st_mode & S_IFDIR) == 0) {
            continue;
        }

        if (nfiles >= STUB_MAX_FOUND_FILES - 1) {
            break;
        }

        list[nfiles++] = CopyString(d->d_name);
    }

    closedir(fdir);

    list[nfiles] = NULL;
    *numfiles = nfiles;
    if (!nfiles) {
        return NULL;
    }

    listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (i = 0; i < nfiles; i++) {
        listCopy[i] = list[i];
    }
    listCopy[i] = NULL;
    return listCopy;
}

void Sys_FreeFileList(char **list) {
    int i;

    if (!list) {
        return;
    }

    for (i = 0; list[i]; i++) {
        Z_Free(list[i]);
    }

    Z_Free(list);
}

void CON_Init(void) {
}

void CON_Shutdown(void) {
}

char *CON_Input(void) {
    return NULL;
}

void Sys_PlatformInit(void) {
}

void Sys_SetEnv(const char *name, const char *value) {
    (void)name;
    (void)value;
}

char *Sys_GetCurrentUser(void) {
    static char s_web_user[] = "player";
    return s_web_user;
}

int Sys_PID(void) {
    return 1;
}

qboolean Sys_PIDIsRunning(int pid) {
    (void)pid;
    return qfalse;
}

qboolean Sys_DllExtension(const char *name) {
    const char *ext;

    if (!name) {
        return qfalse;
    }

    ext = strrchr(name, '.');
    if (!ext) {
        return qfalse;
    }

    if (!Q_stricmp(ext, ".so") || !Q_stricmp(ext, ".dll") || !Q_stricmp(ext, ".wasm")) {
        return qtrue;
    }

    return qfalse;
}

int Sys_Milliseconds(void) {
    static const auto base = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - base).count();
}

qboolean Sys_RandomBytes(byte *string, int len) {
    if (!string || len <= 0) {
        return qfalse;
    }

    static std::random_device rd;
    for (int i = 0; i < len; ++i) {
        string[i] = (byte)(rd() & 0xFF);
    }
    return qtrue;
}

void Sys_PlatformExit(void) {
    // No-op under Godot — platform cleanup is handled by Godot's own shutdown.
}

#endif  // !defined(_WIN32)

// Registry stubs (Windows-origin, called on all platforms in some paths)
qboolean SaveRegistryInfo(qboolean user, const char *pszName, void *pvBuf, long lSize) {
    return qfalse;
}

qboolean LoadRegistryInfo(qboolean user, const char *pszName, void *pvBuf, long *plSize) {
    return qfalse;
}

qboolean IsFirstRun(void) {
    return qfalse;
}

qboolean IsNewConfig(void) {
    return qfalse;
}

qboolean IsSafeMode(void) {
    return qfalse;
}

void ClearNewConfigFlag(void) {
}

void RecoverLostAutodialData(void) {
}

// VM stubs
void VM_Forced_Unload_Start(void) {
}

void VM_Forced_Unload_Done(void) {
}

// Renderer-thread stubs (SMP rendering not used under Godot)
qboolean GLimp_SpawnRenderThread(void (*function)(void)) {
    return qfalse;
}

void *GLimp_RendererSleep(void) {
    return NULL;
}

#ifndef _UNIX
void GLimp_FrontEndSleep(void) {
}
#endif

void GLimp_WakeRenderer(void *data) {
}

// Clipboard — use a static buffer for Get since the engine expects the
// pointer to remain valid until the next call.
static char s_clipboard_buf[4096];

const char *Sys_GetWholeClipboard(void) {
    // Godot clipboard access requires C++ (DisplayServer).  We declare an
    // accessor implemented in a C++ TU that can call Godot APIs.
    extern int Godot_Clipboard_Get(char *buf, int bufSize);
    if (Godot_Clipboard_Get(s_clipboard_buf, sizeof(s_clipboard_buf))) {
        return s_clipboard_buf;
    }
    return NULL;
}

void Sys_SetClipboard(const char *contents) {
    extern void Godot_Clipboard_Set(const char *text);
    if (contents) {
        Godot_Clipboard_Set(contents);
    }
}

// ──────────────────────────────────────────────
//  Sys misc
// ──────────────────────────────────────────────

void Sys_DebugPrint(const char *msg) {
    Com_Printf("%s", msg);
}

void Sys_PrintBackTrace(void) {
}

/* Real signature: const char *Sys_LV_CL_ConvertString(const char *var)
 * Localisation passthrough — returns the input string unchanged. */
const char *Sys_LV_CL_ConvertString(const char *var) {
    return var ? var : "";
}

void Sys_LV_ConvertString(char *dest, const char *src, int maxlen) {
    if (dest && src) {
        strncpy(dest, src, maxlen);
        dest[maxlen - 1] = '\0';
    }
}

// Thread priority stubs
void SetBelowNormalThreadPriority(void) {}
void SetNormalThreadPriority(void) {}

// CL_CDKeyValidate — the original client validates CD keys
qboolean CL_CDKeyValidate(const char *key, const char *checksum) {
    return qtrue;
}

// R_ImageExists — Global symbol referenced by some shared code paths.
// Forward to GR_ImageExists in godot_renderer.c which checks the shader table
// and probes the VFS for actual image files.
extern qboolean GR_ImageExists(const char *name);
qboolean R_ImageExists(const char *name) {
    return GR_ImageExists(name);
}

// ──────────────────────────────────────────────
//  Cursor / mouse stubs — replace SDL mouse code (sdl_mouse.c)
//  that is not compiled in the Godot build.
// ──────────────────────────────────────────────

// Accessor from godot_client_accessors.cpp — returns cl.mousex/cl.mousey
extern void Godot_Client_GetMousePos(int *, int *);

void IN_GetMousePosition(int *x, int *y) {
    /* Under Godot, cursor position is tracked internally via SE_MOUSE
       events and accumulated in cl.mousex/cl.mousey.  Return the
       engine's current position so any callers get sensible values. */
    Godot_Client_GetMousePos(x, y);
}

// ── Cursor image capture for Godot-side custom cursor ──
// IN_SetCursorFromImage stores the raw RGBA pixels here; MoHAARunner
// picks them up via Godot_GetPendingCursorImage() and creates a Godot
// custom cursor texture with Input::set_custom_mouse_cursor().
static byte  *s_cursor_pixels  = NULL;
static int    s_cursor_width   = 0;
static int    s_cursor_height  = 0;
static int    s_cursor_pending = 0;

qboolean IN_SetCursorFromImage(const byte *pic, int width, int height, pCursorFree cursorFreeFn) {
    if (!pic || width <= 0 || height <= 0) return qfalse;

    // Free previous buffer
    if (s_cursor_pixels) {
        free(s_cursor_pixels);
        s_cursor_pixels = NULL;
    }

    int size = width * height * 4;  // RGBA
    s_cursor_pixels = (byte *)malloc(size);
    if (!s_cursor_pixels) return qfalse;

    memcpy(s_cursor_pixels, pic, size);
    s_cursor_width  = width;
    s_cursor_height = height;
    s_cursor_pending = 1;

    // Free the source image if a free function was provided.
    // (Unlike SDL which references pic directly via SDL_CreateRGBSurfaceWithFormatFrom,
    //  we memcpy'd the data, so the original can be freed immediately.)
    if (cursorFreeFn) {
        cursorFreeFn((byte *)pic);
    }

    return qtrue;
}

void IN_FreeCursor(void) {
    if (s_cursor_pixels) {
        free(s_cursor_pixels);
        s_cursor_pixels = NULL;
    }
    s_cursor_width = 0;
    s_cursor_height = 0;
    s_cursor_pending = 0;
}

/* Accessor for MoHAARunner to retrieve pending cursor image data.
 * Returns 1 if a new cursor image is waiting, 0 otherwise.
 * After the caller consumes the data, it should call
 * Godot_ClearPendingCursorImage() to acknowledge. */
int Godot_GetPendingCursorImage(const byte **out_pixels, int *out_w, int *out_h) {
    if (!s_cursor_pending || !s_cursor_pixels) return 0;
    if (out_pixels) *out_pixels = s_cursor_pixels;
    if (out_w) *out_w = s_cursor_width;
    if (out_h) *out_h = s_cursor_height;
    return 1;
}

void Godot_ClearPendingCursorImage(void) {
    s_cursor_pending = 0;
}

// Return qtrue when the UI mouse is active (menus/console visible).
// This matches the SDL implementation which checked the cursor visibility state.
extern qboolean in_guimouse;
qboolean IN_IsCursorActive(void) {
    return in_guimouse;
}

// ──────────────────────────────────────────────
//  Sys_PumpMessageLoop — replaces SDL event pump (not used under Godot)
// ──────────────────────────────────────────────

void Sys_PumpMessageLoop(void) {
}

} // extern "C"

