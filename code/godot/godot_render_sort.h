/*
 * godot_render_sort.h — Transparent surface sort order for the OpenMoHAA GDExtension.
 *
 * idTech 3 sorts transparent surfaces by shader sort key, then by distance
 * from the camera (back-to-front) to achieve correct alpha blending.  Without
 * this ordering, transparent surfaces render in the wrong order causing
 * visual artefacts such as see-through geometry appearing in front of
 * solid objects or additive effects being occluded.
 *
 * Shader sort key values (from engine .shader files):
 *   0  = portal
 *   2  = opaque (default)
 *   6  = decal
 *   8  = see-through (fences, grates)
 *   9  = banner (flags, cloth)
 *  12  = underwater
 *  14  = blend0 (standard alpha blend)
 *  15  = blend1
 *  16  = additive (GL_ONE GL_ONE)
 *
 * Godot render_priority mapping (-128 to 127):
 *   Opaque surfaces      : priority  0   (default, no special ordering)
 *   Transparent surfaces  : priority  1–100 (furthest = 1, nearest = 100)
 *   Additive surfaces     : priority  101–127
 *
 * MoHAARunner Integration Required:
 *   In update_entities(), after building the entity list each frame:
 *     1. Build a SortableEntity array for all transparent entities.
 *     2. Call Godot_RenderSort_SortEntities() with camera position.
 *     3. For each entity, call Godot_RenderSort_ApplyPriority() on its
 *        MeshInstance3D to set the material render_priority.
 *
 * Phase 251 — Transparent Surface Sort Order.
 */

#ifndef GODOT_RENDER_SORT_H
#define GODOT_RENDER_SORT_H

#ifdef __cplusplus

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

/* ── Sort key thresholds ── */

/* Surfaces with sort_key > this value are considered transparent. */
#define SORT_KEY_OPAQUE_MAX    2

/* Surfaces with sort_key >= this value are considered additive. */
#define SORT_KEY_ADDITIVE_MIN  16

/* ── Sortable entity descriptor ── */

struct SortableEntity {
    int   entity_index;     /* index in gr_entities[]                   */
    float sort_key;         /* shader sort value (0–16)                 */
    float camera_distance;  /* squared distance from camera (computed)  */
    bool  is_additive;      /* true when blend mode is additive         */
};

/* ── API ── */

/*
 * Godot_RenderSort_Init — initialise the sort system (currently a no-op).
 */
void Godot_RenderSort_Init(void);

/*
 * Godot_RenderSort_SortEntities — sort an array of transparent entities.
 *
 * Groups entities by opaque/transparent/additive, then sorts transparent
 * entities back-to-front (by descending camera distance) within each
 * shader sort key group.  Opaque entities are sorted front-to-back for
 * early-z optimisation.
 *
 * @param entities    Array of SortableEntity to sort in-place.
 * @param count       Number of elements in the array.
 * @param camera_pos  Camera position in Godot world space (metres).
 *                    Used to compute camera_distance for each entity.
 */
void Godot_RenderSort_SortEntities(SortableEntity *entities,
                                   int count,
                                   const godot::Vector3 &camera_pos);

/*
 * Godot_RenderSort_ApplyPriority — set render_priority on a MeshInstance3D.
 *
 * Maps the entity's sort_key and camera_distance to Godot's integer
 * render_priority range:
 *   - Opaque (sort_key <= 2)     → priority 0
 *   - Transparent (3–15)         → priority 1–100 (furthest first)
 *   - Additive (>= 16)          → priority 101–127
 *
 * Applies the priority to all surface material overrides on the mesh.
 *
 * @param mesh             The MeshInstance3D to update.
 * @param sort_key         Shader sort value for this entity.
 * @param camera_distance  Squared distance from camera.
 */
void Godot_RenderSort_ApplyPriority(godot::MeshInstance3D *mesh,
                                    float sort_key,
                                    float camera_distance);

/*
 * Godot_RenderSort_Shutdown — shut down the sort system (currently a no-op).
 */
void Godot_RenderSort_Shutdown(void);

#endif /* __cplusplus */

#endif /* GODOT_RENDER_SORT_H */
