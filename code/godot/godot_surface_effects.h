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
 * godot_surface_effects.h
 * 
 * Centralised data tables for surface-based sound and visual effects.
 * 
 * This module provides lookup functions that map surface types (from surfaceflags.h)
 * to their corresponding sound aliases and visual effect IDs. It eliminates the need
 * for duplicated switch statements across footsteps, bullet impacts, landings, and
 * body fall code.
 * 
 * Usage:
 *   int surfType = trace.surfaceFlags & MASK_SURF_TYPE;
 *   const char *soundSuffix = SurfaceEffects_GetFootstepSound(surfType);
 *   int effectNum = SurfaceEffects_GetFootstepEffect(surfType);
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the sound alias suffix for footstep sounds based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          String suffix to append to "snd_step_" (e.g., "metal", "grass", "stone")
 */
const char *SurfaceEffects_GetFootstepSound(int surfType);

/**
 * Get the visual effect ID for footstep effects based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          Effect ID (e.g., SFX_FOOT_GRASS, SFX_FOOT_SNOW) or -1 for none
 */
int SurfaceEffects_GetFootstepEffect(int surfType);

/**
 * Get the sound alias suffix for landing sounds based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          String suffix to append to "snd_landing_" (e.g., "metal", "grass", "stone")
 */
const char *SurfaceEffects_GetLandingSound(int surfType);

/**
 * Get the visual effect ID for landing effects based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          Effect ID (e.g., SFX_FOOT_GRASS, SFX_FOOT_SNOW) or -1 for none
 */
int SurfaceEffects_GetLandingEffect(int surfType);

/**
 * Get the sound alias suffix for body fall sounds based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          String suffix to append to "snd_bodyfall_" (e.g., "metal", "grass", "stone")
 */
const char *SurfaceEffects_GetBodyFallSound(int surfType);

/**
 * Get the visual effect ID for body fall effects based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          Effect ID (e.g., SFX_FOOT_GRASS, SFX_FOOT_SNOW) or -1 for none
 */
int SurfaceEffects_GetBodyFallEffect(int surfType);

/**
 * Get the sound alias suffix for bullet impact sounds based on surface type.
 * 
 * @param surfType  Surface type flags (from trace.surfaceFlags & MASK_SURF_TYPE)
 * @return          String suffix to append to "snd_bh_" (e.g., "metal", "glass", "stone")
 */
const char *SurfaceEffects_GetBulletImpactSound(int surfType);

#ifdef __cplusplus
}
#endif
