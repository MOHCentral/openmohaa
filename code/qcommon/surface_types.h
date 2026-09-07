/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

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

// DESCRIPTION:
// Centralised surface type data tables.  Maps SURF_* flags from
// surfaceflags.h to human-readable names, sound suffixes, and a
// compact enum used for table-driven lookups throughout fgame and
// cgame, replacing duplicated switch statements.

#pragma once

#include "surfaceflags.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compact enum for indexing into surface-type lookup tables.
typedef enum {
    SURF_TYPE_NONE,
    SURF_TYPE_PAPER,
    SURF_TYPE_WOOD,
    SURF_TYPE_METAL,
    SURF_TYPE_ROCK,
    SURF_TYPE_DIRT,
    SURF_TYPE_GRILL,
    SURF_TYPE_GRASS,
    SURF_TYPE_MUD,
    SURF_TYPE_PUDDLE,
    SURF_TYPE_GLASS,
    SURF_TYPE_GRAVEL,
    SURF_TYPE_SAND,
    SURF_TYPE_FOLIAGE,
    SURF_TYPE_SNOW,
    SURF_TYPE_CARPET,
    NUM_SURFACE_TYPES
} surfaceType_t;

// Per-surface-type metadata used by footstep, impact, and debug code.
typedef struct {
    surfaceType_t type;
    int           flag;         // SURF_* bit from surfaceflags.h (0 for NONE)
    const char   *name;         // display name (e.g. "rock", "metal grill")
    const char   *soundSuffix;  // sound-alias suffix (e.g. "stone" for ROCK)
} surfaceTypeInfo_t;

// ── Master lookup table ────────────────────────────────────────────
// Indexed by surfaceType_t.  SURF_TYPE_NONE is the fallback entry.
static const surfaceTypeInfo_t g_surfaceTypes[NUM_SURFACE_TYPES] = {
    { SURF_TYPE_NONE,    0,            "none",        "stone"   },
    { SURF_TYPE_PAPER,   SURF_PAPER,   "paper",       "paper"   },
    { SURF_TYPE_WOOD,    SURF_WOOD,    "wood",        "wood"    },
    { SURF_TYPE_METAL,   SURF_METAL,   "metal",       "metal"   },
    { SURF_TYPE_ROCK,    SURF_ROCK,    "rock",        "stone"   },
    { SURF_TYPE_DIRT,    SURF_DIRT,    "dirt",        "dirt"    },
    { SURF_TYPE_GRILL,   SURF_GRILL,   "metal grill", "grill"   },
    { SURF_TYPE_GRASS,   SURF_GRASS,   "grass",       "grass"   },
    { SURF_TYPE_MUD,     SURF_MUD,     "mud",         "mud"     },
    { SURF_TYPE_PUDDLE,  SURF_PUDDLE,  "puddle",      "puddle"  },
    { SURF_TYPE_GLASS,   SURF_GLASS,   "glass",       "glass"   },
    { SURF_TYPE_GRAVEL,  SURF_GRAVEL,  "gravel",      "gravel"  },
    { SURF_TYPE_SAND,    SURF_SAND,    "sand",        "sand"    },
    { SURF_TYPE_FOLIAGE, SURF_FOLIAGE, "foliage",     "foliage" },
    { SURF_TYPE_SNOW,    SURF_SNOW,    "snow",        "snow"    },
    { SURF_TYPE_CARPET,  SURF_CARPET,  "carpet",      "carpet"  },
};

// ── Inline helpers ─────────────────────────────────────────────────

// Return the full surfaceTypeInfo_t for a masked SURF_* flag.
// Uses a switch for O(1) lookup on the hot path.
static inline const surfaceTypeInfo_t *SurfaceFlag_GetInfo(int surfaceFlag)
{
    switch (surfaceFlag) {
    case SURF_PAPER:   return &g_surfaceTypes[SURF_TYPE_PAPER];
    case SURF_WOOD:    return &g_surfaceTypes[SURF_TYPE_WOOD];
    case SURF_METAL:   return &g_surfaceTypes[SURF_TYPE_METAL];
    case SURF_ROCK:    return &g_surfaceTypes[SURF_TYPE_ROCK];
    case SURF_DIRT:    return &g_surfaceTypes[SURF_TYPE_DIRT];
    case SURF_GRILL:   return &g_surfaceTypes[SURF_TYPE_GRILL];
    case SURF_GRASS:   return &g_surfaceTypes[SURF_TYPE_GRASS];
    case SURF_MUD:     return &g_surfaceTypes[SURF_TYPE_MUD];
    case SURF_PUDDLE:  return &g_surfaceTypes[SURF_TYPE_PUDDLE];
    case SURF_GLASS:   return &g_surfaceTypes[SURF_TYPE_GLASS];
    case SURF_GRAVEL:  return &g_surfaceTypes[SURF_TYPE_GRAVEL];
    case SURF_SAND:    return &g_surfaceTypes[SURF_TYPE_SAND];
    case SURF_FOLIAGE: return &g_surfaceTypes[SURF_TYPE_FOLIAGE];
    case SURF_SNOW:    return &g_surfaceTypes[SURF_TYPE_SNOW];
    case SURF_CARPET:  return &g_surfaceTypes[SURF_TYPE_CARPET];
    default:           return &g_surfaceTypes[SURF_TYPE_NONE];
    }
}

// Convert a masked SURF_* flag (already ANDed with MASK_SURF_TYPE)
// to the compact surfaceType_t enum.
static inline surfaceType_t SurfaceFlag_ToType(int surfaceFlag)
{
    return SurfaceFlag_GetInfo(surfaceFlag)->type;
}

// Return the sound-alias suffix for a masked SURF_* flag.
// e.g. SURF_ROCK → "stone", SURF_METAL → "metal", unknown → "stone"
static inline const char *SurfaceFlag_ToSoundSuffix(int surfaceFlag)
{
    return SurfaceFlag_GetInfo(surfaceFlag)->soundSuffix;
}

// Return the human-readable name for a masked SURF_* flag.
// e.g. SURF_GRILL → "metal grill", SURF_ROCK → "rock"
static inline const char *SurfaceFlag_ToName(int surfaceFlag)
{
    return SurfaceFlag_GetInfo(surfaceFlag)->name;
}

#ifdef __cplusplus
}
#endif
