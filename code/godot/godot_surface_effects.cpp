/*
===========================================================================
Copyright (C) 2024-2025 OpenMoHAA Godot Port

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

/**
 * godot_surface_effects.cpp
 * 
 * Implementation of centralised surface effect lookup tables.
 */

#include "../qcommon/surfaceflags.h"
#include "../cgame/cg_specialfx.h"
#include "godot_surface_effects.h"

extern "C" {

/**
 * Surface effect data structure
 * Maps a surface type to its sound suffix and visual effect ID
 */
typedef struct {
    int         surfType;       // Surface flag (e.g., SURF_METAL, SURF_GRASS)
    const char *soundSuffix;    // Sound alias suffix (e.g., "metal", "grass")
    int         effectID;       // Visual effect ID (e.g., SFX_FOOT_GRASS) or -1
} SurfaceEffectEntry;

/**
 * Footstep effect table
 * Used for walking/running footstep sounds and effects
 */
static const SurfaceEffectEntry footstepEffects[] = {
    { SURF_FOLIAGE, "foliage", SFX_FOOT_GRASS },
    { SURF_SNOW,    "snow",    SFX_FOOT_SNOW },
    { SURF_CARPET,  "carpet",  SFX_FOOT_LIGHT_DUST },
    { SURF_SAND,    "sand",    SFX_FOOT_SAND },
    { SURF_PUDDLE,  "puddle",  SFX_FOOT_PUDDLE },
    { SURF_GLASS,   "glass",   SFX_FOOT_LIGHT_DUST },
    { SURF_GRAVEL,  "gravel",  SFX_FOOT_HEAVY_DUST },
    { SURF_MUD,     "mud",     SFX_FOOT_MUD },
    { SURF_DIRT,    "dirt",    SFX_FOOT_DIRT },
    { SURF_GRILL,   "grill",   SFX_FOOT_LIGHT_DUST },
    { SURF_GRASS,   "grass",   SFX_FOOT_GRASS },
    { SURF_ROCK,    "stone",   SFX_FOOT_HEAVY_DUST },
    { SURF_PAPER,   "paper",   SFX_FOOT_LIGHT_DUST },
    { SURF_WOOD,    "wood",    SFX_FOOT_LIGHT_DUST },
    { SURF_METAL,   "metal",   SFX_FOOT_LIGHT_DUST },
    { 0,            NULL,      -1 }  // Terminator
};

/**
 * Landing effect table
 * Used for landing sounds and effects (similar to footsteps but with different effect IDs for some surfaces)
 */
static const SurfaceEffectEntry landingEffects[] = {
    { SURF_FOLIAGE, "foliage", SFX_FOOT_GRASS },
    { SURF_SNOW,    "snow",    SFX_FOOT_SNOW },
    { SURF_CARPET,  "carpet",  -1 },
    { SURF_SAND,    "sand",    SFX_FOOT_SAND },
    { SURF_PUDDLE,  "puddle",  SFX_FOOT_PUDDLE },
    { SURF_GLASS,   "glass",   -1 },
    { SURF_GRAVEL,  "gravel",  SFX_FOOT_HEAVY_DUST },
    { SURF_MUD,     "mud",     SFX_FOOT_MUD },
    { SURF_DIRT,    "dirt",    SFX_FOOT_DIRT },
    { SURF_GRILL,   "grill",   -1 },
    { SURF_GRASS,   "grass",   SFX_FOOT_GRASS },
    { SURF_ROCK,    "stone",   SFX_FOOT_LIGHT_DUST },
    { SURF_PAPER,   "paper",   -1 },
    { SURF_WOOD,    "wood",    -1 },
    { SURF_METAL,   "metal",   -1 },
    { 0,            NULL,      -1 }  // Terminator
};

/**
 * Body fall effect table
 * Used for body falling sounds and effects
 */
static const SurfaceEffectEntry bodyFallEffects[] = {
    { SURF_FOLIAGE, "foliage", SFX_FOOT_GRASS },
    { SURF_SNOW,    "snow",    SFX_FOOT_SNOW },
    { SURF_CARPET,  "carpet",  SFX_FOOT_LIGHT_DUST },
    { SURF_SAND,    "sand",    SFX_FOOT_SAND },
    { SURF_PUDDLE,  "puddle",  SFX_FOOT_PUDDLE },
    { SURF_GLASS,   "glass",   SFX_FOOT_LIGHT_DUST },
    { SURF_GRAVEL,  "gravel",  SFX_FOOT_HEAVY_DUST },
    { SURF_MUD,     "mud",     SFX_FOOT_MUD },
    { SURF_DIRT,    "dirt",    SFX_FOOT_DIRT },
    { SURF_GRILL,   "grill",   SFX_FOOT_LIGHT_DUST },
    { SURF_GRASS,   "grass",   SFX_FOOT_GRASS },
    { SURF_ROCK,    "stone",   SFX_FOOT_HEAVY_DUST },
    { SURF_PAPER,   "paper",   SFX_FOOT_LIGHT_DUST },
    { SURF_WOOD,    "wood",    SFX_FOOT_LIGHT_DUST },
    { SURF_METAL,   "metal",   SFX_FOOT_LIGHT_DUST },
    { 0,            NULL,      -1 }  // Terminator
};

/**
 * Bullet impact sound table
 * Used for bullet hit sounds (no visual effects in this table)
 */
static const SurfaceEffectEntry bulletImpactSounds[] = {
    { SURF_FOLIAGE, "foliage", -1 },
    { SURF_SNOW,    "snow",    -1 },
    { SURF_CARPET,  "carpet",  -1 },
    { SURF_SAND,    "sand",    -1 },
    { SURF_PUDDLE,  "puddle",  -1 },
    { SURF_GLASS,   "glass",   -1 },
    { SURF_GRAVEL,  "gravel",  -1 },
    { SURF_MUD,     "mud",     -1 },
    { SURF_DIRT,    "dirt",    -1 },
    { SURF_GRILL,   "grill",   -1 },
    { SURF_GRASS,   "grass",   -1 },
    { SURF_ROCK,    "stone",   -1 },
    { SURF_PAPER,   "paper",   -1 },
    { SURF_WOOD,    "wood",    -1 },
    { SURF_METAL,   "metal",   -1 },
    { 0,            NULL,      -1 }  // Terminator
};

/**
 * Helper: Look up sound suffix in a table
 */
static const char *LookupSound(const SurfaceEffectEntry *table, int surfType)
{
    const SurfaceEffectEntry *entry;
    
    for (entry = table; entry->soundSuffix != NULL; ++entry) {
        if (entry->surfType == surfType) {
            return entry->soundSuffix;
        }
    }
    
    // Default: stone
    return "stone";
}

/**
 * Helper: Look up effect ID in a table
 */
static int LookupEffect(const SurfaceEffectEntry *table, int surfType)
{
    const SurfaceEffectEntry *entry;
    
    for (entry = table; entry->soundSuffix != NULL; ++entry) {
        if (entry->surfType == surfType) {
            return entry->effectID;
        }
    }
    
    // Default: light dust for footsteps/landings, heavy dust for body falls
    if (table == bodyFallEffects) {
        return SFX_FOOT_HEAVY_DUST;
    } else {
        return SFX_FOOT_LIGHT_DUST;
    }
}

// Public API implementations

const char *SurfaceEffects_GetFootstepSound(int surfType)
{
    return LookupSound(footstepEffects, surfType);
}

int SurfaceEffects_GetFootstepEffect(int surfType)
{
    return LookupEffect(footstepEffects, surfType);
}

const char *SurfaceEffects_GetLandingSound(int surfType)
{
    return LookupSound(landingEffects, surfType);
}

int SurfaceEffects_GetLandingEffect(int surfType)
{
    return LookupEffect(landingEffects, surfType);
}

const char *SurfaceEffects_GetBodyFallSound(int surfType)
{
    return LookupSound(bodyFallEffects, surfType);
}

int SurfaceEffects_GetBodyFallEffect(int surfType)
{
    return LookupEffect(bodyFallEffects, surfType);
}

const char *SurfaceEffects_GetBulletImpactSound(int surfType)
{
    return LookupSound(bulletImpactSounds, surfType);
}

} // extern "C"
