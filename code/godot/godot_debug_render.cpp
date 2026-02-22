/*
 * godot_debug_render.cpp — Debug rendering overlays for developer diagnostics.
 *
 * Phase 84: Implements wireframe overlay (r_showtris), surface normal
 * visualisation (r_shownormals), performance stats (r_speeds), and
 * entity bounding box display (r_showbbox).
 *
 * All debug cvars are read via C accessors in godot_debug_render_accessors.c
 * to avoid engine header conflicts with godot-cpp.
 */

#include "godot_debug_render.h"
#include "godot_mesh_cache.h"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

/* ── Engine accessor prototypes ── */
extern "C" {
    int  Godot_Renderer_GetEntityCount(void);
    /* Signature must match godot_renderer.c exactly (8 params). */
    int  Godot_Renderer_GetEntity(int index,
                                  float *origin, float *axis, float *out_scale,
                                  int *hModel, int *entityNumber,
                                  unsigned char *rgba, int *renderfx);
    /* Returns 1 if entity has a TIKI (skeletal), 0 otherwise. */
    int  Godot_Renderer_GetEntityAnim(int index, void **outTiki,
                                      int *outEntityNumber, void *outFrameInfo,
                                      int *outBoneTag, float *outBoneQuat,
                                      float *outActionWeight, float *outScale);
}

/* ── Constants ── */
static constexpr float MOHAA_UNIT_SCALE = 1.0f / 39.37f;
static constexpr int   STATS_UPDATE_INTERVAL = 10;  /* update every N frames */
static constexpr float NORMAL_LINE_LENGTH    = 0.1f; /* metres */
static constexpr float NORMAL_MAX_DISTANCE   = 20.0f; /* metres from camera */
static constexpr int   MAX_NORMAL_MESHES     = 32;
static constexpr float BBOX_HALF_SIZE        = 0.5f;  /* default half-extent (metres) */
static constexpr float BBOX_MIN_THRESHOLD    = 0.05f; /* below this, use MIN_SIZE */
static constexpr float BBOX_MIN_SIZE         = 0.25f; /* minimum visible half-extent */
static constexpr int   ESTIMATED_POLYS_PER_ENTITY = 500;

/* ── Static state ── */
static Node               *s_parent      = nullptr;
static Viewport           *s_viewport    = nullptr;

/* Stats overlay */
static CanvasLayer        *s_stats_layer = nullptr;
static Label              *s_stats_label = nullptr;
static int                 s_stats_frame_counter = 0;

/* Wireframe state tracking */
static int                 s_prev_showtris = 0;

/* Normal visualisation pool */
static MeshInstance3D     *s_normal_meshes[MAX_NORMAL_MESHES];
static int                 s_normal_mesh_count = 0;
static Ref<StandardMaterial3D> s_normal_material;

/* Bounding box visualisation pool */
static MeshInstance3D     *s_bbox_meshes[MAX_NORMAL_MESHES];
static int                 s_bbox_mesh_count = 0;
static Ref<StandardMaterial3D> s_bbox_material_static;
static Ref<StandardMaterial3D> s_bbox_material_dynamic;

static bool                s_initialised = false;

/* ── Forward declarations ── */
static void update_showtris(void);
static void update_speeds(float delta);
static void update_shownormals(void);
static void update_showbbox(void);
static Ref<StandardMaterial3D> create_line_material(const Color &colour);
static void hide_normal_pool(void);
static void hide_bbox_pool(void);

/* ===================================================================
 *  Convert id Tech 3 origin to Godot coordinates.
 *  id: X=Forward, Y=Left, Z=Up  →  Godot: X=Right, Y=Up, -Z=Forward
 * ================================================================ */
static Vector3 id_to_godot(const float o[3]) {
    return Vector3(-o[1] * MOHAA_UNIT_SCALE,
                    o[2] * MOHAA_UNIT_SCALE,
                   -o[0] * MOHAA_UNIT_SCALE);
}

/* ===================================================================
 *  Godot_DebugRender_Init
 * ================================================================ */
void Godot_DebugRender_Init(Node *parent) {
    if (s_initialised || !parent) return;

    s_parent   = parent;
    s_viewport = parent->get_viewport();
    s_prev_showtris = 0;
    s_stats_frame_counter = 0;

    /* ── Stats overlay (CanvasLayer z=150 with monospace Label) ── */
    s_stats_layer = memnew(CanvasLayer);
    s_stats_layer->set_name("DebugStatsLayer");
    s_stats_layer->set_layer(150);
    parent->add_child(s_stats_layer);

    s_stats_label = memnew(Label);
    s_stats_label->set_name("DebugStatsLabel");
    s_stats_label->set_position(Vector2(10.0f, 10.0f));
    s_stats_label->set_size(Vector2(500.0f, 300.0f));
    s_stats_label->add_theme_color_override("font_color", Color(0.0f, 1.0f, 0.0f, 1.0f));
    s_stats_label->add_theme_constant_override("outline_size", 2);
    s_stats_label->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f, 1.0f));
    s_stats_label->set_visible(false);
    s_stats_layer->add_child(s_stats_label);

    /* ── Line materials for normal and bbox visualisation ── */
    s_normal_material       = create_line_material(Color(0.2f, 0.4f, 1.0f, 1.0f)); /* blue */
    s_bbox_material_static  = create_line_material(Color(0.0f, 1.0f, 0.0f, 1.0f)); /* green */
    s_bbox_material_dynamic = create_line_material(Color(1.0f, 1.0f, 0.0f, 1.0f)); /* yellow */

    /* ── ImmediateMesh pools ── */
    for (int i = 0; i < MAX_NORMAL_MESHES; i++) {
        s_normal_meshes[i] = memnew(MeshInstance3D);
        s_normal_meshes[i]->set_name(String("DebugNormal_") + String::num_int64(i));
        s_normal_meshes[i]->set_visible(false);
        parent->add_child(s_normal_meshes[i]);

        s_bbox_meshes[i] = memnew(MeshInstance3D);
        s_bbox_meshes[i]->set_name(String("DebugBBox_") + String::num_int64(i));
        s_bbox_meshes[i]->set_visible(false);
        parent->add_child(s_bbox_meshes[i]);
    }

    s_normal_mesh_count = 0;
    s_bbox_mesh_count   = 0;
    s_initialised       = true;
}

/* ===================================================================
 *  Godot_DebugRender_Update
 * ================================================================ */
void Godot_DebugRender_Update(float delta) {
    if (!s_initialised) return;

    update_showtris();
    update_speeds(delta);
    update_shownormals();
    update_showbbox();
}

/* ===================================================================
 *  Godot_DebugRender_Shutdown
 * ================================================================ */
void Godot_DebugRender_Shutdown(void) {
    if (!s_initialised) return;

    /* Restore wireframe state */
    if (s_viewport && s_prev_showtris != 0) {
        s_viewport->set_debug_draw(Viewport::DEBUG_DRAW_DISABLED);
    }

    /* Remove stats overlay */
    if (s_stats_label && s_stats_layer) {
        s_stats_layer->remove_child(s_stats_label);
        memdelete(s_stats_label);
    }
    if (s_stats_layer && s_parent) {
        s_parent->remove_child(s_stats_layer);
        memdelete(s_stats_layer);
    }

    /* Remove ImmediateMesh pools */
    for (int i = 0; i < MAX_NORMAL_MESHES; i++) {
        if (s_normal_meshes[i] && s_parent) {
            s_parent->remove_child(s_normal_meshes[i]);
            memdelete(s_normal_meshes[i]);
        }
        s_normal_meshes[i] = nullptr;

        if (s_bbox_meshes[i] && s_parent) {
            s_parent->remove_child(s_bbox_meshes[i]);
            memdelete(s_bbox_meshes[i]);
        }
        s_bbox_meshes[i] = nullptr;
    }

    s_normal_material.unref();
    s_bbox_material_static.unref();
    s_bbox_material_dynamic.unref();

    s_stats_label   = nullptr;
    s_stats_layer   = nullptr;
    s_parent        = nullptr;
    s_viewport      = nullptr;
    s_initialised   = false;
    s_prev_showtris = 0;
    s_normal_mesh_count = 0;
    s_bbox_mesh_count   = 0;
}

/* ===================================================================
 *  r_showtris — wireframe overlay via viewport debug draw
 * ================================================================ */
static void update_showtris(void) {
    if (!s_viewport) return;

    int val = Godot_Debug_GetShowTris();
    if (val == s_prev_showtris) return;

    if (val >= 1) {
        s_viewport->set_debug_draw(Viewport::DEBUG_DRAW_WIREFRAME);
    } else {
        s_viewport->set_debug_draw(Viewport::DEBUG_DRAW_DISABLED);
    }
    s_prev_showtris = val;
}

/* ===================================================================
 *  r_speeds — stats overlay on CanvasLayer
 * ================================================================ */
static void update_speeds(float delta) {
    int val = Godot_Debug_GetSpeeds();

    if (val == 0) {
        if (s_stats_label && s_stats_label->is_visible()) {
            s_stats_label->set_visible(false);
        }
        s_stats_frame_counter = 0;
        return;
    }

    if (!s_stats_label) return;
    s_stats_label->set_visible(true);

    /* Only update text every STATS_UPDATE_INTERVAL frames */
    s_stats_frame_counter++;
    if (s_stats_frame_counter < STATS_UPDATE_INTERVAL) return;
    s_stats_frame_counter = 0;

    int ent_count = Godot_Renderer_GetEntityCount();

    /* Count skeletal vs static entities */
    int skeletal_count = 0;
    int static_count   = 0;
    for (int i = 0; i < ent_count; i++) {
        float origin[3], axis[3][3], scale;
        int hModel, entNum, renderfx;
        unsigned char rgba[4];
        int reType = Godot_Renderer_GetEntity(i, origin, (float*)axis, &scale,
                                               &hModel, &entNum, rgba, &renderfx);
        if (reType == 0) { /* RT_MODEL */
            /* Use GetEntityAnim to detect skeletal (has TIKI) vs static */
            if (Godot_Renderer_GetEntityAnim(i, nullptr, nullptr, nullptr,
                                             nullptr, nullptr, nullptr, nullptr))
                skeletal_count++;
            else
                static_count++;
        }
    }

    float fps = (delta > 0.0001f) ? (1.0f / delta) : 0.0f;

    int cache_hits   = g_render_stats.mesh_cache_hits;
    int cache_misses = g_render_stats.mesh_cache_misses;
    int cache_total  = cache_hits + cache_misses;
    float hit_rate   = (cache_total > 0)
                       ? (100.0f * (float)cache_hits / (float)cache_total)
                       : 0.0f;

    String text;
    text += String("FPS: ") + String::num(fps, 1) + "\n";
    text += String("Entities: ") + String::num_int64(ent_count)
          + String("  (skel: ") + String::num_int64(skeletal_count)
          + String("  static: ") + String::num_int64(static_count) + ")\n";
    text += String("Cache hits/miss: ") + String::num_int64(cache_hits)
          + "/" + String::num_int64(cache_misses)
          + String("  (") + String::num(hit_rate, 1) + "%)\n";
    text += String("Draw calls: ") + String::num_int64(g_render_stats.draw_calls) + "\n";
    text += String("Polys (est): ") + String::num_int64(ent_count * ESTIMATED_POLYS_PER_ENTITY) + "\n";

    s_stats_label->set_text(text);
}

/* ===================================================================
 *  r_shownormals — draw entity orientation axes as coloured lines
 *
 *  Since the module cannot access per-vertex mesh normals (those are
 *  in MoHAARunner's MeshInstance3D nodes), we draw the entity's local
 *  axes (forward/right/up) at each entity origin.  This is still
 *  useful for debugging entity orientation and transform issues.
 *  Blue line = entity forward axis.
 * ================================================================ */
static void update_shownormals(void) {
    int val = Godot_Debug_GetShowNormals();

    if (val == 0) {
        hide_normal_pool();
        return;
    }

    /* Find camera position for distance culling */
    Camera3D *cam = nullptr;
    if (s_viewport) {
        cam = s_viewport->get_camera_3d();
    }
    Vector3 cam_pos;
    if (cam) {
        cam_pos = cam->get_global_position();
    }

    int ent_count = Godot_Renderer_GetEntityCount();
    int mesh_idx  = 0;

    for (int i = 0; i < ent_count && mesh_idx < MAX_NORMAL_MESHES; i++) {
        float origin[3], axis[3][3], scale;
        int hModel, entNum, renderfx;
        unsigned char rgba[4];
        int reType = Godot_Renderer_GetEntity(i, origin, (float*)axis, &scale,
                                               &hModel, &entNum, rgba, &renderfx);
        if (reType != 0) continue; /* only RT_MODEL */

        Vector3 gpos = id_to_godot(origin);

        /* Distance cull */
        if (cam && cam_pos.distance_to(gpos) > NORMAL_MAX_DISTANCE) continue;

        /* Build ImmediateMesh with 3 axis lines */
        Ref<ImmediateMesh> im;
        im.instantiate();
        im->surface_begin(Mesh::PRIMITIVE_LINES);

        /* Forward axis (blue) — id X axis */
        Vector3 fwd(-axis[0][1] * NORMAL_LINE_LENGTH,
                      axis[0][2] * NORMAL_LINE_LENGTH,
                     -axis[0][0] * NORMAL_LINE_LENGTH);
        im->surface_set_color(Color(0.2f, 0.4f, 1.0f));
        im->surface_add_vertex(gpos);
        im->surface_set_color(Color(0.2f, 0.4f, 1.0f));
        im->surface_add_vertex(gpos + fwd);

        im->surface_end();

        s_normal_meshes[mesh_idx]->set_mesh(im);
        s_normal_meshes[mesh_idx]->set_material_override(s_normal_material);
        s_normal_meshes[mesh_idx]->set_visible(true);
        mesh_idx++;
    }

    /* Hide unused pool entries */
    for (int i = mesh_idx; i < MAX_NORMAL_MESHES; i++) {
        if (s_normal_meshes[i]) {
            s_normal_meshes[i]->set_visible(false);
        }
    }
    s_normal_mesh_count = mesh_idx;
}

/* ===================================================================
 *  r_showbbox — wireframe bounding boxes around entities
 *
 *  Green for static models (no animation), yellow for animated.
 *  Uses entity origin as box centre with a default half-extent
 *  scaled by the entity's scale factor.
 * ================================================================ */
static void update_showbbox(void) {
    int val = Godot_Debug_GetDrawBBox();

    if (val == 0) {
        hide_bbox_pool();
        return;
    }

    int ent_count = Godot_Renderer_GetEntityCount();
    int mesh_idx  = 0;

    for (int i = 0; i < ent_count && mesh_idx < MAX_NORMAL_MESHES; i++) {
        float origin[3], axis[3][3], scale;
        int hModel, entNum, renderfx;
        unsigned char rgba[4];
        int reType = Godot_Renderer_GetEntity(i, origin, (float*)axis, &scale,
                                               &hModel, &entNum, rgba, &renderfx);
        if (reType != 0) continue; /* only RT_MODEL */

        Vector3 gpos = id_to_godot(origin);
        float half = BBOX_HALF_SIZE * scale * MOHAA_UNIT_SCALE;
        if (half < BBOX_MIN_THRESHOLD) half = BBOX_MIN_SIZE;

        /* Use GetEntityAnim as skeletal heuristic (returns 1 if TIKI present) */
        bool is_dynamic = (Godot_Renderer_GetEntityAnim(i, nullptr, nullptr, nullptr,
                                                         nullptr, nullptr, nullptr, nullptr) != 0);

        /* Build wireframe box with 12 line edges */
        Ref<ImmediateMesh> im;
        im.instantiate();
        im->surface_begin(Mesh::PRIMITIVE_LINES);

        /* 8 corners of axis-aligned box */
        Vector3 c[8] = {
            gpos + Vector3(-half, -half, -half),
            gpos + Vector3( half, -half, -half),
            gpos + Vector3( half,  half, -half),
            gpos + Vector3(-half,  half, -half),
            gpos + Vector3(-half, -half,  half),
            gpos + Vector3( half, -half,  half),
            gpos + Vector3( half,  half,  half),
            gpos + Vector3(-half,  half,  half),
        };

        /* 12 edges as line pairs */
        static const int edges[12][2] = {
            {0,1}, {1,2}, {2,3}, {3,0},  /* bottom face */
            {4,5}, {5,6}, {6,7}, {7,4},  /* top face */
            {0,4}, {1,5}, {2,6}, {3,7},  /* verticals */
        };

        Color col = is_dynamic ? Color(1.0f, 1.0f, 0.0f) : Color(0.0f, 1.0f, 0.0f);

        for (int e = 0; e < 12; e++) {
            im->surface_set_color(col);
            im->surface_add_vertex(c[edges[e][0]]);
            im->surface_set_color(col);
            im->surface_add_vertex(c[edges[e][1]]);
        }

        im->surface_end();

        Ref<StandardMaterial3D> mat = is_dynamic ? s_bbox_material_dynamic
                                                 : s_bbox_material_static;
        s_bbox_meshes[mesh_idx]->set_mesh(im);
        s_bbox_meshes[mesh_idx]->set_material_override(mat);
        s_bbox_meshes[mesh_idx]->set_visible(true);
        mesh_idx++;
    }

    /* Hide unused pool entries */
    for (int i = mesh_idx; i < MAX_NORMAL_MESHES; i++) {
        if (s_bbox_meshes[i]) {
            s_bbox_meshes[i]->set_visible(false);
        }
    }
    s_bbox_mesh_count = mesh_idx;
}

/* ===================================================================
 *  Helper: create an unshaded line material
 * ================================================================ */
static Ref<StandardMaterial3D> create_line_material(const Color &colour) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_albedo(colour);
    mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
    return mat;
}

/* ===================================================================
 *  Helper: hide all normal pool meshes
 * ================================================================ */
static void hide_normal_pool(void) {
    for (int i = 0; i < MAX_NORMAL_MESHES; i++) {
        if (s_normal_meshes[i]) {
            s_normal_meshes[i]->set_visible(false);
        }
    }
    s_normal_mesh_count = 0;
}

/* ===================================================================
 *  Helper: hide all bbox pool meshes
 * ================================================================ */
static void hide_bbox_pool(void) {
    for (int i = 0; i < MAX_NORMAL_MESHES; i++) {
        if (s_bbox_meshes[i]) {
            s_bbox_meshes[i]->set_visible(false);
        }
    }
    s_bbox_mesh_count = 0;
}
