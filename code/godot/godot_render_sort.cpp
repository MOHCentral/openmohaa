/*
 * godot_render_sort.cpp — Transparent surface sort order for the OpenMoHAA GDExtension.
 *
 * Phase 251: Correct back-to-front rendering order for transparent surfaces.
 *
 * idTech 3 sorts transparent surfaces by shader sort key, then by distance
 * from camera.  This module replicates that ordering using Godot's
 * render_priority system on StandardMaterial3D / ShaderMaterial.
 */

#include "godot_render_sort.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader_material.hpp>

/* ===================================================================
 *  Constants
 * ================================================================ */

/* Render priority range for transparent surfaces (1–100). */
static const int PRIORITY_TRANSPARENT_MIN = 1;
static const int PRIORITY_TRANSPARENT_MAX = 100;

/* Render priority range for additive surfaces (101–127). */
static const int PRIORITY_ADDITIVE_MIN = 101;
static const int PRIORITY_ADDITIVE_MAX = 127;

/*
 * Maximum squared distance (in Godot metres) used to normalise the
 * camera_distance → priority mapping.  Entities beyond this distance
 * all receive the minimum transparent priority (rendered first).
 * 10 000 id-Tech-3 units ≈ 254 metres ≈ 64 516 m².
 */
static const float MAX_DISTANCE_SQ = 64516.0f;

/* ===================================================================
 *  Init / Shutdown
 * ================================================================ */

void Godot_RenderSort_Init(void)
{
    /* Reserved for future state initialisation. */
}

void Godot_RenderSort_Shutdown(void)
{
    /* Reserved for future cleanup. */
}

/* ===================================================================
 *  Sorting
 * ================================================================ */

/*
 * Comparator for SortableEntity.
 *
 * 1. Primary key: sort_key (ascending — lower sort keys render first).
 * 2. Within the same sort_key:
 *    - Opaque (sort_key <= 2): front-to-back (ascending distance) for early-z.
 *    - Transparent / additive : back-to-front (descending distance) for correct blending.
 */
static bool sort_compare(const SortableEntity &a, const SortableEntity &b)
{
    /* Different sort keys — render lower keys first. */
    if (a.sort_key != b.sort_key) {
        return a.sort_key < b.sort_key;
    }

    /* Same sort key — direction depends on transparency. */
    if (a.sort_key <= static_cast<float>(SORT_KEY_OPAQUE_MAX)) {
        /* Opaque: front-to-back (smaller distance first). */
        return a.camera_distance < b.camera_distance;
    }

    /* Transparent / additive: back-to-front (larger distance first). */
    return a.camera_distance > b.camera_distance;
}

void Godot_RenderSort_SortEntities(SortableEntity *entities,
                                   int count,
                                   const godot::Vector3 &camera_pos)
{
    if (!entities || count <= 0) {
        return;
    }

    /*
     * The caller must set camera_distance on each SortableEntity to the
     * squared distance from camera_pos to the entity's Godot-space origin
     * before invoking this function.  This module does not access the
     * entity buffer directly — distance computation is the caller's
     * responsibility.
     */

    std::sort(entities, entities + count, sort_compare);
}

/* ===================================================================
 *  Priority Application
 * ================================================================ */

/*
 * Map a squared distance to an integer priority within [lo, hi].
 *
 * For transparent surfaces the furthest entity gets the lowest priority
 * (rendered first) and the nearest gets the highest (rendered last).
 * This achieves back-to-front ordering via Godot's render_priority.
 */
static int distance_to_priority(float dist_sq, int lo, int hi)
{
    float t = dist_sq / MAX_DISTANCE_SQ;
    if (t > 1.0f) t = 1.0f;

    /* Invert: t=0 (nearest) → hi, t=1 (furthest) → lo. */
    int priority = hi - static_cast<int>(t * static_cast<float>(hi - lo));
    if (priority < lo) priority = lo;
    if (priority > hi) priority = hi;
    return priority;
}

void Godot_RenderSort_ApplyPriority(godot::MeshInstance3D *mesh,
                                    float sort_key,
                                    float camera_distance)
{
    if (!mesh) {
        return;
    }

    /* ── Determine render priority ── */
    int priority = 0;

    if (sort_key <= static_cast<float>(SORT_KEY_OPAQUE_MAX)) {
        /* Opaque — default priority, no special ordering needed. */
        priority = 0;
    } else if (sort_key >= static_cast<float>(SORT_KEY_ADDITIVE_MIN)) {
        /* Additive — rendered after all other transparents. */
        priority = distance_to_priority(camera_distance,
                                        PRIORITY_ADDITIVE_MIN,
                                        PRIORITY_ADDITIVE_MAX);
    } else {
        /* Standard transparent — back-to-front. */
        priority = distance_to_priority(camera_distance,
                                        PRIORITY_TRANSPARENT_MIN,
                                        PRIORITY_TRANSPARENT_MAX);
    }

    /* ── Apply to all surface materials on the mesh ── */
    int surface_count = mesh->get_surface_override_material_count();
    for (int i = 0; i < surface_count; i++) {
        godot::Ref<godot::Material> mat = mesh->get_surface_override_material(i);
        if (mat.is_null()) {
            continue;
        }

        mat->set_render_priority(priority);
    }
}
