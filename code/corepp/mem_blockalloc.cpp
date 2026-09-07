/*
===========================================================================
Copyright (C) 2015 the OpenMoHAA team

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

// mem_blockalloc.cpp: Fast block memory manager

#if defined(GAME_DLL)

#    include "../fgame/g_local.h"
#    ifdef GODOT_GDEXTENSION
#        include <cstdlib>  // fallback malloc/free for library teardown
// Forward-declare engine allocators (from qcommon.h, extern "C")
extern "C" void *Z_Malloc(int size);
extern "C" void  Z_Free(void *ptr);
#    endif

void *MEM_Alloc(int size)
{
#ifdef GODOT_GDEXTENSION
    // Monolithic build: always use engine zone allocator directly.
    // gi.Malloc (= SV_Malloc) tags memory with TAG_GAME and may not
    // be populated when corepp code is called from CL_Init.
    return Z_Malloc(size);
#else
    return gi.Malloc(size);
#endif
}

void MEM_Free(void *ptr)
{
#ifdef GODOT_GDEXTENSION
    return Z_Free(ptr);
#else
    return gi.Free(ptr);
#endif
}

#elif defined(CGAME_DLL)

#    include "../cgame/cg_local.h"

void *MEM_Alloc(int size)
{
    return cgi.Malloc(size);
}

void MEM_Free(void *ptr)
{
    return cgi.Free(ptr);
}

#elif defined(REF_DLL)

#    include "../renderercommon/tr_common.h"

void *MEM_Alloc(int size)
{
    return ri.Malloc(size);
}

void MEM_Free(void *ptr)
{
    return ri.Free(ptr);
}

#else

#    include "../qcommon/qcommon.h"

void *MEM_Alloc(int size)
{
    return Z_Malloc(size);
}

void MEM_Free(void *ptr)
{
    return Z_Free(ptr);
}

#endif
