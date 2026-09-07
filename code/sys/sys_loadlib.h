/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef _WIN32
/* On Windows always use the Win32 DLL API — regardless of DEDICATED or
   GODOT_GDEXTENSION.  The original code placed this under #ifdef DEDICATED,
   but under MSVC /TP (treat-.c-as-C++) the DEDICATED guard occasionally
   fails to pull in <windows.h> early enough, leaving HMODULE undeclared.
   Checking _WIN32 first is unconditional: MSVC always defines it. */
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#	define Sys_LoadLibrary(f) (void*)LoadLibrary(f)
#	define Sys_UnloadLibrary(h) FreeLibrary((HMODULE)h)
#	define Sys_LoadFunction(h,fn) (void*)GetProcAddress((HMODULE)h,fn)
/* Return a human-readable error string from GetLastError(). Uses a
   thread-local buffer so the caller does not need to free it. */
static inline const char *Sys_LibraryError(void) {
	static __declspec(thread) char errBuf[512];
	DWORD err = GetLastError();
	if (!err) return "no error";
	errBuf[0] = '\0';
	FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		errBuf, sizeof(errBuf) - 1, NULL);
	/* Strip trailing \r\n */
	{
		size_t len = strlen(errBuf);
		while (len > 0 && (errBuf[len-1] == '\r' || errBuf[len-1] == '\n'))
			errBuf[--len] = '\0';
	}
	return errBuf;
}
#elif defined(DEDICATED) || defined(GODOT_GDEXTENSION)
#	include <dlfcn.h>
#	define Sys_LoadLibrary(f) dlopen(f,RTLD_NOW)
#	define Sys_UnloadLibrary(h) dlclose(h)
#	define Sys_LoadFunction(h,fn) dlsym(h,fn)
#	define Sys_LibraryError() dlerror()
#else
#	ifdef USE_INTERNAL_SDL_HEADERS
#		include "SDL.h"
#		include "SDL_loadso.h"
#	else
#		include <SDL.h>
#		include <SDL_loadso.h>
#	endif
#	define Sys_LoadLibrary(f) SDL_LoadObject(f)
#	define Sys_UnloadLibrary(h) SDL_UnloadObject(h)
#	define Sys_LoadFunction(h,fn) SDL_LoadFunction(h,fn)
#	define Sys_LibraryError() SDL_GetError()
#endif

void * QDECL Sys_LoadDll(const char *name, qboolean useSystemLib);
