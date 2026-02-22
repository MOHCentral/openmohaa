/*
 * godot_bsp_mesh.h — MOHAA BSP world loader for the Godot GDExtension.
 *
 * Reads a MOHAA BSP file (ident "2015", versions 17–21) via the engine's
 * VFS and creates Godot MeshInstance3D/ArrayMesh nodes for the world
 * geometry.  Handles MST_PLANAR, MST_TRIANGLE_SOUP, MST_PATCH, and
 * MOHAA terrain patches (LUMP_TERRAIN).
 *
 * Also parses:
 *   - LUMP_STATICMODELDEF (25) — static TIKI model placements
 *   - LUMP_MODELS (13) — inline brush sub-models for doors/movers
 *   - LUMP_FOGS (13 in BSP ≤18) — per-surface fog volumes
 *   - Fullbright/nolightmap surfaces (SURF_NOLIGHTMAP flag)
 *   - Portal surfaces (surfaceparm portal)
 *   - Flare surfaces (MST_FLARE type)
 *
 * Coordinate conversion from id Tech 3 (X=Forward, Y=Left, Z=Up) to
 * Godot (X=Right, Y=Up, -Z=Forward) and unit scaling from inches to
 * metres (1/39.37) is applied to all vertex data.
 */

#ifndef GODOT_BSP_MESH_H
#define GODOT_BSP_MESH_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>

/* ── Static model definition from BSP LUMP_STATICMODELDEF ── */
struct BSPStaticModelDef {
    char  model[128];     /* TIKI path, e.g. "static/bush_full.tik" */
    float origin[3];      /* world-space position (id coordinates) */
    float angles[3];      /* [pitch, yaw, roll] in degrees */
    float scale;          /* additional instance scale factor */
};

/// Load a MOHAA BSP file from the engine VFS and return a Node3D tree
/// containing MeshInstance3D children with the world geometry.
/// Also parses static model definitions and brush sub-models.
/// @param bsp_path  Engine-relative path, e.g. "maps/DM/mohdm1.bsp"
/// @return  Owning pointer to a Node3D, or nullptr on failure.
///          Caller is responsible for adding it to the scene tree
///          (or calling memdelete).
godot::Node3D *Godot_BSP_LoadWorld(const char *bsp_path);

/// Free any internally cached data from a previous load.
void Godot_BSP_Unload();

/// Get the number of static model definitions from the last BSP load.
int Godot_BSP_GetStaticModelCount();

/// Get a static model definition by index (0-based).
/// Returns nullptr if index is out of range.
const BSPStaticModelDef *Godot_BSP_GetStaticModelDef(int index);

/// Get the number of brush sub-models (excluding model 0, the world).
int Godot_BSP_GetBrushModelCount();

/// Get the pre-built ArrayMesh for a brush sub-model (1-based index).
/// Returns an empty Ref if the index is out of range or the mesh is empty.
godot::Ref<godot::ArrayMesh> Godot_BSP_GetBrushModelMesh(int submodelIndex);

/* ── Phase 78: BSP fog volume definition ── */
struct BSPFogVolume {
    char  shader[64];       /* Fog shader name */
    int   brushNum;         /* BSP brush defining the fog region */
    int   visibleSide;      /* Brush side for ray tests (-1 = none) */
    float color[3];         /* Fog colour (RGB, 0–1) — from shader props */
    float depthForOpaque;   /* Distance for full opacity — from shader props */
};

/// Get the number of fog volumes parsed from the BSP.
int Godot_BSP_GetFogVolumeCount();

/// Get a fog volume definition by index (0-based).
/// Returns nullptr if index is out of range.
const BSPFogVolume *Godot_BSP_GetFogVolume(int index);

/* ── Phase 74: Flare surface definition ── */
struct BSPFlare {
    float origin[3];        /* Flare position in Godot coordinates */
    float color[3];         /* Flare colour (RGB, 0–1) */
    char  shader[64];       /* Shader name for flare texture */
};

/// Get the number of flare surfaces parsed from the BSP.
int Godot_BSP_GetFlareCount();

/// Get a flare definition by index (0-based).
/// Returns nullptr if index is out of range.
const BSPFlare *Godot_BSP_GetFlare(int index);

/* C API functions (entity tokens, PVS, lightgrid, marks, etc.)
 * are now in renderergl1/godot_bsp_accessors.c */
#include "godot_bsp_accessors.h"

/* PVS cluster mesh accessors (C++ only — return Godot types) */
#ifdef __cplusplus
namespace godot { class MeshInstance3D; }
int Godot_BSP_GetPVSNumClusters();
godot::MeshInstance3D *Godot_BSP_GetClusterMesh(int cluster);
#endif

#endif // GODOT_BSP_MESH_H
