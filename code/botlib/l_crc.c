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

/*****************************************************************************
 * name:		l_crc.c
 *
 * desc:		CRC calculation
 *
 * $Archive: /MissionPack/CODE/botlib/l_crc.c $
 *
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../qcommon/q_shared.h"
#include "botlib.h"
#include "be_interface.h"			//for botimport.Print
#include "l_crc.h"


#ifndef GODOT_GDEXTENSION
// FIXME: byte swap?

// this is a 16 bit, non-reflected CRC using the polynomial 0x1021
// and the initial and final xor values shown below...  in other words, the
// CCITT standard CRC used by XMODEM

#define CRC_INIT_VALUE	0xffff
#define CRC_XOR_VALUE	0x0000

unsigned short crctable[257] =
...
	for (i = 0; i < length; i++)
	{
		*crc = (*crc << 8) ^ crctable[(*crc >> 8) ^ data[i]];
	} //end for
} //end of the function CRC_ProcessString
#endif
