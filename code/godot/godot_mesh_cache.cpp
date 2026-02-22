/*
 * godot_mesh_cache.cpp — Per-entity mesh & material caching.
 *
 * Phase 60: Entity mesh caching to eliminate redundant ArrayMesh rebuilds.
 * Phase 61: Material caching to share materials across entities.
 * Phase 85: Render performance statistics and logging.
 */

#include "godot_mesh_cache.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

/* ── Phase 85: Global stats ── */
Godot_RenderStats g_render_stats;

/* ===================================================================
 *  Phase 60: Entity Mesh Cache
 * ================================================================ */

Godot_MeshCache &Godot_MeshCache::get()
{
    static Godot_MeshCache instance;
    return instance;
}

const EntityMeshCacheEntry *Godot_MeshCache::lookup(
    const EntityMeshCacheKey &key,
    uint64_t current_frame)
{
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        misses_++;
        return nullptr;
    }
    it->second.last_used_frame = current_frame;
    hits_++;
    return &it->second;
}

void Godot_MeshCache::store(
    const EntityMeshCacheKey &key,
    Ref<ArrayMesh> mesh,
    const std::vector<godot::String> &surface_shaders,
    uint64_t current_frame)
{
    EntityMeshCacheEntry &entry = cache_[key];
    entry.mesh             = mesh;
    entry.surface_shaders  = surface_shaders;
    entry.last_used_frame  = current_frame;
}

void Godot_MeshCache::evict_stale(uint64_t current_frame,
                                   uint64_t max_stale_frames)
{
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (current_frame - it->second.last_used_frame > max_stale_frames) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void Godot_MeshCache::clear()
{
    cache_.clear();
    hits_   = 0;
    misses_ = 0;
}

/* ===================================================================
 *  Phase 61: Material Cache
 * ================================================================ */

Godot_MaterialCache &Godot_MaterialCache::get()
{
    static Godot_MaterialCache instance;
    return instance;
}

Ref<Material> Godot_MaterialCache::lookup(
    const MaterialCacheKey &key,
    uint64_t current_frame)
{
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        misses_++;
        return Ref<Material>();
    }
    it->second.last_used_frame = current_frame;
    hits_++;
    return it->second.material;
}

void Godot_MaterialCache::store(
    const MaterialCacheKey &key,
    Ref<Material> material,
    uint64_t current_frame)
{
    MaterialCacheEntry &entry = cache_[key];
    entry.material        = material;
    entry.last_used_frame = current_frame;
}

void Godot_MaterialCache::evict_stale(uint64_t current_frame,
                                       uint64_t max_stale_frames)
{
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (current_frame - it->second.last_used_frame > max_stale_frames) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void Godot_MaterialCache::clear()
{
    cache_.clear();
    hits_   = 0;
    misses_ = 0;
}

/* ===================================================================
 *  Phase 85: Render Performance Stats
 * ================================================================ */

extern "C" {

void Godot_RenderStats_BeginFrame(void)
{
    g_render_stats.reset();
    g_render_stats.frame_start_usec =
        Time::get_singleton()->get_ticks_usec();
}

void Godot_RenderStats_EndFrame(void)
{
    g_render_stats.frame_end_usec =
        Time::get_singleton()->get_ticks_usec();
}

void Godot_RenderStats_Log(void)
{
    uint64_t dt = g_render_stats.frame_end_usec
                - g_render_stats.frame_start_usec;

    UtilityFunctions::print(
        String("[MoHAA RenderStats] frame=") +
        String::num_int64(dt) + String("us") +
        String(" ents=") +
        String::num_int64(g_render_stats.entities_rendered) +
        String(" skel=") +
        String::num_int64(g_render_stats.entities_skeletal) +
        String(" static=") +
        String::num_int64(g_render_stats.entities_static) +
        String(" mesh_hit=") +
        String::num_int64(g_render_stats.mesh_cache_hits) +
        String(" mesh_miss=") +
        String::num_int64(g_render_stats.mesh_cache_misses) +
        String(" mat_hit=") +
        String::num_int64(g_render_stats.material_cache_hits) +
        String(" mat_miss=") +
        String::num_int64(g_render_stats.material_cache_misses) +
        String(" draws=") +
        String::num_int64(g_render_stats.draw_calls) +
        String(" mesh_cache_sz=") +
        String::num_int64(Godot_MeshCache::get().stat_size()) +
        String(" mat_cache_sz=") +
        String::num_int64(Godot_MaterialCache::get().stat_size())
    );
}

} /* extern "C" */
