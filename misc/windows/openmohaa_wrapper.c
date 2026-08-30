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
 * Flow: start openmohaa.exe → check official GitHub releases
 * (via launch_openmohaa.ps1) → start openmohaa_game.exe.
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

#pragma comment(lib, "user32.lib")

#define GAME_EXE_NAME L"openmohaa_game.exe"
#define PS1_NAME L"launch_openmohaa.ps1"
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

static int CmdHasSwitch(const wchar_t *name)
{
    const wchar_t *cmd = GetCommandLineW();
    return (cmd && wcsstr(cmd, name) != NULL);
}

static void RunUpdateCheck(const wchar_t *gameDir, const wchar_t *ps1Path, int checkOnly)
{
    wchar_t cmd[4096];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD waitRc;

    if (GetFileAttributesW(ps1Path) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Check failed.\n");
        fflush(stdout);
        return;
    }

    if (checkOnly)
        _snwprintf(cmd, 4096,
            L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -CheckOnly",
            ps1Path);
    else
        _snwprintf(cmd, 4096,
            L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -SkipLaunch",
            ps1Path);
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
    const wchar_t *rest;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (GetFileAttributesW(gameExe) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Could not start the game: %s is missing.\n", GAME_EXE_NAME);
        Sleep(3000);
        return 1;
    }

    rest = SkipArg0(GetCommandLineW());
    if (rest && rest[0])
        _snwprintf(cmd, 32768, L"\"%s\" %s", gameExe, rest);
    else
        _snwprintf(cmd, 32768, L"\"%s\"", gameExe);
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
    DWORD gamePid = 0;

    SetConsoleTitleW(L"OpenMoHAA");
    SetConsoleOutputCP(65001);

    GetDirOfModule(gameDir, MAX_PATH);
    _snwprintf(gameExe, MAX_PATH, L"%s\\%s", gameDir, GAME_EXE_NAME);
    gameExe[MAX_PATH - 1] = 0;
    _snwprintf(ps1Path, MAX_PATH, L"%s\\%s", gameDir, PS1_NAME);
    ps1Path[MAX_PATH - 1] = 0;

    wprintf(L"Checking updates...\n");
    fflush(stdout);
    RunUpdateCheck(gameDir, ps1Path, CmdHasSwitch(L"-CheckOnly"));

    if (CmdHasSwitch(L"-CheckOnly"))
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
