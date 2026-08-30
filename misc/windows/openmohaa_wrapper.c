/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*
 * openmohaa.exe — thin Windows wrapper.
 *
 * Flow: start openmohaa.exe → choose x86/x64 if needed → check official
 * GitHub releases (via launch_openmohaa.ps1) → start openmohaa_game.exe.
 *
 * Architecture is never auto-picked. First run asks:
 *   Architectuur? [1] x86  [2] x64
 * Later starts use launcher_state (AppData or next to this exe).
 * Override with -x86 / -x64 (also saved).
 *
 * Official zip updates must copy the zip's openmohaa.exe onto
 * openmohaa_game.exe. Never overwrite this wrapper.
 */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "user32.lib")

#define GAME_EXE_NAME L"openmohaa_game.exe"
#define PS1_NAME L"launch_openmohaa.ps1"
#define STATE_DIRNAME L"openmohaa"
#define STATE_FILENAME L"launcher_state.txt"
/* API 12s + download 90s + extract/copy slack. Hang must not block forever. */
#define UPDATE_WAIT_MS 120000

static void GetDirOfModule(wchar_t *dir, size_t dirChars)
{
    wchar_t path[MAX_PATH];
    wchar_t *slash;

    GetModuleFileNameW(NULL, path, MAX_PATH);
    slash = wcsrchr(path, L'\\');
    if (slash)
        *slash = 0;
    wcsncpy(dir, path, dirChars - 1);
    dir[dirChars - 1] = 0;
}

static const wchar_t *SkipArg0(const wchar_t *cmd)
{
    if (!cmd)
        return L"";
    while (*cmd == L' ')
        cmd++;
    if (*cmd == L'"') {
        cmd++;
        while (*cmd && *cmd != L'"')
            cmd++;
        if (*cmd == L'"')
            cmd++;
    } else {
        while (*cmd && *cmd != L' ')
            cmd++;
    }
    while (*cmd == L' ')
        cmd++;
    return cmd;
}

static int EqualsSwitch(const wchar_t *s, const wchar_t *name)
{
    return _wcsicmp(s, name) == 0;
}

static int TokenIsLauncherSwitch(const wchar_t *tok)
{
    return EqualsSwitch(tok, L"-x86") ||
           EqualsSwitch(tok, L"-x64") ||
           EqualsSwitch(tok, L"-CheckOnly");
}

static int NextArg(const wchar_t **p, wchar_t *tok, size_t tokChars, int *quoted)
{
    const wchar_t *s = *p;
    size_t n = 0;

    *quoted = 0;
    while (*s == L' ')
        s++;
    if (!*s) {
        *p = s;
        tok[0] = 0;
        return 0;
    }
    if (*s == L'"') {
        *quoted = 1;
        s++;
        while (*s && *s != L'"' && n + 1 < tokChars)
            tok[n++] = *s++;
        if (*s == L'"')
            s++;
    } else {
        while (*s && *s != L' ' && n + 1 < tokChars)
            tok[n++] = *s++;
    }
    tok[n] = 0;
    *p = s;
    return 1;
}

static int CmdHasToken(const wchar_t *name)
{
    const wchar_t *p = GetCommandLineW();
    wchar_t tok[256];
    int quoted = 0;

    if (!p)
        return 0;
    while (NextArg(&p, tok, 256, &quoted)) {
        if (EqualsSwitch(tok, name))
            return 1;
    }
    return 0;
}

static BOOL FindProcessId(const wchar_t *exeName, DWORD *outPid)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    BOOL found = FALSE;

    *outPid = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return FALSE;

    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                *outPid = pe.th32ProcessID;
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static BOOL CALLBACK FocusByPid(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lp && IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return FALSE;
    }
    return TRUE;
}

static void GetStatePaths(const wchar_t *gameDir, wchar_t *appDataPath, size_t appN,
                          wchar_t *localPath, size_t locN)
{
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);

    if (n > 0 && n < MAX_PATH)
        _snwprintf(appDataPath, appN, L"%s\\%s\\%s", appdata, STATE_DIRNAME, STATE_FILENAME);
    else
        appDataPath[0] = 0;
    appDataPath[appN - 1] = 0;

    _snwprintf(localPath, locN, L"%s\\%s", gameDir, STATE_FILENAME);
    localPath[locN - 1] = 0;
}

static int ParseArchValue(const char *v, wchar_t *arch, size_t n)
{
    while (*v == ' ' || *v == '\t')
        v++;
    if (_strnicmp(v, "x86", 3) == 0 &&
        (v[3] == 0 || v[3] == '\r' || v[3] == '\n' || v[3] == ' ' || v[3] == '\t')) {
        wcsncpy(arch, L"x86", n);
        arch[n - 1] = 0;
        return 1;
    }
    if (_strnicmp(v, "x64", 3) == 0 &&
        (v[3] == 0 || v[3] == '\r' || v[3] == '\n' || v[3] == ' ' || v[3] == '\t')) {
        wcsncpy(arch, L"x64", n);
        arch[n - 1] = 0;
        return 1;
    }
    return 0;
}

static int ReadArchFromFile(const wchar_t *path, wchar_t *arch, size_t n)
{
    HANDLE h;
    char buf[8192];
    DWORD readn = 0;
    char *p;
    char *line;

    if (!path || !path[0])
        return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &readn, NULL)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    buf[readn] = 0;

    p = buf;
    if (readn >= 3 &&
        (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        p = buf + 3;
    }

    while (*p) {
        line = p;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        if (*p) {
            *p++ = 0;
            while (*p == '\n' || *p == '\r')
                p++;
        }
        if (_strnicmp(line, "arch=", 5) == 0 && ParseArchValue(line + 5, arch, n))
            return 1;
    }
    return 0;
}

static int EnsureParentDir(const wchar_t *filePath)
{
    wchar_t dir[MAX_PATH];
    wchar_t *slash;

    wcsncpy(dir, filePath, MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    slash = wcsrchr(dir, L'\\');
    if (!slash)
        return 1;
    *slash = 0;
    if (GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES)
        return 1;
    return CreateDirectoryW(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int WriteArchToFile(const wchar_t *path, const wchar_t *arch)
{
    HANDLE h;
    char oldBuf[8192];
    char newBuf[8192];
    wchar_t tmpPath[MAX_PATH];
    DWORD readn = 0;
    DWORD written = 0;
    char *src;
    char *dst;
    int wroteArch = 0;
    char archLine[32];

    if (!path || !path[0])
        return 0;
    if (!EnsureParentDir(path))
        return 0;

    _snprintf(archLine, sizeof(archLine), "arch=%ls\n", arch);
    archLine[sizeof(archLine) - 1] = 0;

    oldBuf[0] = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (!ReadFile(h, oldBuf, sizeof(oldBuf) - 1, &readn, NULL))
            readn = 0;
        CloseHandle(h);
        oldBuf[readn] = 0;
    }

    dst = newBuf;
    src = oldBuf;
    if (readn >= 3 &&
        (unsigned char)oldBuf[0] == 0xEF &&
        (unsigned char)oldBuf[1] == 0xBB &&
        (unsigned char)oldBuf[2] == 0xBF) {
        memcpy(dst, oldBuf, 3);
        dst += 3;
        src = oldBuf + 3;
    }

    while (*src) {
        char *line = src;
        char *eol;
        size_t lineLen;
        int isArch;

        eol = src;
        while (*eol && *eol != '\n' && *eol != '\r')
            eol++;
        lineLen = (size_t)(eol - src);
        isArch = (lineLen >= 5 && _strnicmp(line, "arch=", 5) == 0);

        if (*eol == '\r')
            eol++;
        if (*eol == '\n')
            eol++;
        src = eol;

        if (isArch) {
            size_t alen = strlen(archLine);
            memcpy(dst, archLine, alen);
            dst += alen;
            wroteArch = 1;
        } else {
            memcpy(dst, line, (size_t)(src - line));
            dst += (src - line);
        }
        if ((size_t)(dst - newBuf) + 64 >= sizeof(newBuf))
            break;
    }
    if (!wroteArch) {
        size_t alen = strlen(archLine);
        memcpy(dst, archLine, alen);
        dst += alen;
    }
    *dst = 0;

    _snwprintf(tmpPath, MAX_PATH, L"%s.tmp", path);
    tmpPath[MAX_PATH - 1] = 0;
    h = CreateFileW(tmpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(h, newBuf, (DWORD)(dst - newBuf), &written, NULL)) {
        CloseHandle(h);
        DeleteFileW(tmpPath);
        return 0;
    }
    CloseHandle(h);

    if (!MoveFileExW(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmpPath);
        return 0;
    }
    return 1;
}

static void SaveArch(const wchar_t *gameDir, const wchar_t *arch)
{
    wchar_t appPath[MAX_PATH];
    wchar_t localPath[MAX_PATH];
    int wrote;

    GetStatePaths(gameDir, appPath, MAX_PATH, localPath, MAX_PATH);
    wrote = WriteArchToFile(appPath, arch);
    if (GetFileAttributesW(localPath) != INVALID_FILE_ATTRIBUTES || !wrote)
        WriteArchToFile(localPath, arch);
}

static int LoadSavedArch(const wchar_t *gameDir, wchar_t *arch, size_t n)
{
    wchar_t appPath[MAX_PATH];
    wchar_t localPath[MAX_PATH];

    GetStatePaths(gameDir, appPath, MAX_PATH, localPath, MAX_PATH);
    if (ReadArchFromFile(localPath, arch, n))
        return 1;
    if (ReadArchFromFile(appPath, arch, n))
        return 1;
    return 0;
}

static int PromptArch(wchar_t *arch, size_t n)
{
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);

    wprintf(L"Architectuur? [1] x86  [2] x64\n");
    fflush(stdout);

    if (!hin || hin == INVALID_HANDLE_VALUE)
        return 0;

    for (;;) {
        INPUT_RECORD rec;
        DWORD nread = 0;
        wchar_t ch;
        WORD vk;

        if (!ReadConsoleInputW(hin, &rec, 1, &nread) || nread < 1)
            return 0;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;

        ch = rec.Event.KeyEvent.uChar.UnicodeChar;
        vk = rec.Event.KeyEvent.wVirtualKeyCode;
        if (ch == L'1' || vk == VK_NUMPAD1 || vk == VK_RETURN) {
            wcsncpy(arch, L"x86", n);
            arch[n - 1] = 0;
            wprintf(L"x86\n");
            fflush(stdout);
            return 1;
        }
        if (ch == L'2' || vk == VK_NUMPAD2) {
            wcsncpy(arch, L"x64", n);
            arch[n - 1] = 0;
            wprintf(L"x64\n");
            fflush(stdout);
            return 1;
        }
    }
}

static int ResolveArch(const wchar_t *gameDir, wchar_t *arch, size_t n)
{
    if (CmdHasToken(L"-x86")) {
        wcsncpy(arch, L"x86", n);
        arch[n - 1] = 0;
        SaveArch(gameDir, arch);
        return 1;
    }
    if (CmdHasToken(L"-x64")) {
        wcsncpy(arch, L"x64", n);
        arch[n - 1] = 0;
        SaveArch(gameDir, arch);
        return 1;
    }
    if (LoadSavedArch(gameDir, arch, n))
        return 1;
    if (PromptArch(arch, n)) {
        SaveArch(gameDir, arch);
        return 1;
    }
    return 0;
}

static void RunUpdateCheck(const wchar_t *gameDir, const wchar_t *ps1Path, int checkOnly)
{
    wchar_t cmd[4096];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD waitRc;
    const wchar_t *archArg = L"";

    if (GetFileAttributesW(ps1Path) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Check failed.\n");
        fflush(stdout);
        return;
    }

    /* Only forward an explicit user switch. Saved/prompted arch is in launcher_state;
     * passing -x86/-x64 every time would skip the 6h API cache. */
    if (CmdHasToken(L"-x86"))
        archArg = L" -x86";
    else if (CmdHasToken(L"-x64"))
        archArg = L" -x64";

    if (checkOnly)
        _snwprintf(cmd, 4096,
            L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -CheckOnly%s",
            ps1Path, archArg);
    else
        _snwprintf(cmd, 4096,
            L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -SkipLaunch%s",
            ps1Path, archArg);
    cmd[4095] = 0;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, gameDir, &si, &pi)) {
        wprintf(L"Check failed.\n");
        fflush(stdout);
        return;
    }

    waitRc = WaitForSingleObject(pi.hProcess, UPDATE_WAIT_MS);
    if (waitRc == WAIT_TIMEOUT) {
        wprintf(L"Timeout.\n");
        fflush(stdout);
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static int StartGame(const wchar_t *gameDir, const wchar_t *gameExe)
{
    wchar_t cmd[32768];
    const wchar_t *p;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    size_t used;

    if (GetFileAttributesW(gameExe) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Could not start the game: %s is missing.\n", GAME_EXE_NAME);
        Sleep(3000);
        return 1;
    }

    used = (size_t)_snwprintf(cmd, 32768, L"\"%s\"", gameExe);
    if (used >= 32768) {
        cmd[32767] = 0;
        used = 32767;
    }
    p = SkipArg0(GetCommandLineW());
    while (p && *p && used < 32760) {
        wchar_t tok[4096];
        int quoted = 0;
        int n;

        if (!NextArg(&p, tok, 4096, &quoted))
            break;
        if (TokenIsLauncherSwitch(tok))
            continue;
        if (quoted)
            n = _snwprintf(cmd + used, 32768 - used, L" \"%s\"", tok);
        else
            n = _snwprintf(cmd + used, 32768 - used, L" %s", tok);
        if (n < 0) {
            cmd[32767] = 0;
            break;
        }
        used += (size_t)n;
    }
    cmd[32767] = 0;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(gameExe, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                        NULL, gameDir, &si, &pi)) {
        wprintf(L"CreateProcess failed for %s (error %lu).\n",
                GAME_EXE_NAME, GetLastError());
        Sleep(3000);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

int wmain(void)
{
    wchar_t gameDir[MAX_PATH];
    wchar_t gameExe[MAX_PATH];
    wchar_t ps1Path[MAX_PATH];
    wchar_t arch[8];
    DWORD gamePid = 0;
    int haveArch;

    SetConsoleTitleW(L"OpenMoHAA");
    SetConsoleOutputCP(65001);

    GetDirOfModule(gameDir, MAX_PATH);
    _snwprintf(gameExe, MAX_PATH, L"%s\\%s", gameDir, GAME_EXE_NAME);
    gameExe[MAX_PATH - 1] = 0;
    _snwprintf(ps1Path, MAX_PATH, L"%s\\%s", gameDir, PS1_NAME);
    ps1Path[MAX_PATH - 1] = 0;

    haveArch = ResolveArch(gameDir, arch, 8);
    if (!haveArch) {
        wprintf(L"No architecture chosen.\n");
        fflush(stdout);
        arch[0] = 0;
    }

    wprintf(L"Checking updates...\n");
    fflush(stdout);
    RunUpdateCheck(gameDir, ps1Path, CmdHasToken(L"-CheckOnly"));

    if (CmdHasToken(L"-CheckOnly"))
        return 0;

    if (FindProcessId(GAME_EXE_NAME, &gamePid)) {
        wprintf(L"Game already running.\n");
        fflush(stdout);
        EnumWindows(FocusByPid, (LPARAM)gamePid);
        Sleep(800);
        return 0;
    }

    if (GetFileAttributesW(gameExe) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"openmohaa_game.exe is missing.\n");
        fflush(stdout);
        Sleep(3000);
        return 1;
    }

    wprintf(L"Starting...\n");
    fflush(stdout);
    Sleep(400);
    return StartGame(gameDir, gameExe);
}
