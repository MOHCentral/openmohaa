/*
 * godot_mesh_cache.h — Per-entity mesh & material caching.
 *
 * Phase 60: Entity mesh caching keyed on (hModel, animation state, LOD).
 * Phase 61: Material caching keyed on (shader_handle, RGBA, blend_mode).
 *
 * Eliminates redundant ArrayMesh rebuilds for animated entities and
 * redundant Material allocations for entities sharing appearance.
 *
 * Phase 85: Performance statistics for render audit.
 */

#ifndef GODOT_MESH_CACHE_H
#define GODOT_MESH_CACHE_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/string.hpp>

#include <unordered_map>
#include <cstdint>
#include <cstring>

using namespace godot;

/* ── Phase 60: Entity Mesh Cache ── */

/*
 * EntityMeshCacheKey — uniquely identifies a skinned mesh pose.
 *
 * Captures the model handle, per-frame animation state (index + weight +
 * time for 4 blend slots), and the LOD level so that identical poses at
 * the same LOD share a single ArrayMesh.
 */
struct EntityMeshCacheKey {
    int hModel;
    struct FrameInfo {
        int   index;
        float weight;
        float time;
    } frames[4];
    int lodLevel;

    bool operator==(const EntityMeshCacheKey &o) const {
        if (hModel != o.hModel || lodLevel != o.lodLevel) return false;
        return memcmp(frames, o.frames, sizeof(frames)) == 0;
    }
};

struct EntityMeshCacheHash {
    size_t operator()(const EntityMeshCacheKey &k) const {
        /* FNV-1a over the raw bytes of the key */
        const uint8_t *data = reinterpret_cast<const uint8_t *>(&k);
        size_t hash = 2166136261u;
        for (size_t i = 0; i < sizeof(k); i++) {
            hash ^= data[i];
            hash *= 16777619u;
        }
        return hash;
    }
};

struct EntityMeshCacheEntry {
    Ref<ArrayMesh>           mesh;
    std::vector<godot::String> surface_shaders;  /* per-surface shader name */
    uint64_t                 last_used_frame;     /* for eviction */
};

/*
 * Godot_MeshCache — singleton entity mesh cache.
 *
 * MoHAARunner Integration Required:
 *   In update_entities(), replace direct mesh building with:
 *     1. Build EntityMeshCacheKey from entity anim state
 *     2. Call Godot_MeshCache_Get() — returns cached mesh or nullptr
 *     3. If nullptr, build mesh, then Godot_MeshCache_Put()
 *
 *   In _process(), call Godot_MeshCache_EvictStale() once per frame
 *   to remove entries unused for N frames.
 */
class Godot_MeshCache {
public:
    static Godot_MeshCache &get();

    /* Look up a cached mesh.  Returns nullptr if not found. */
    const EntityMeshCacheEntry *lookup(const EntityMeshCacheKey &key,
                                       uint64_t current_frame);

    /* Insert a mesh into the cache. */
    void store(const EntityMeshCacheKey &key,
               Ref<ArrayMesh> mesh,
               const std::vector<godot::String> &surface_shaders,
               uint64_t current_frame);

    /* Evict entries not used for `max_stale_frames` frames. */
    void evict_stale(uint64_t current_frame, uint64_t max_stale_frames = 120);

    /* Clear the entire cache (map change / shutdown). */
    void clear();

    /* Phase 85: statistics */
    uint64_t stat_hits()   const { return hits_;   }
    uint64_t stat_misses() const { return misses_; }
    size_t   stat_size()   const { return cache_.size(); }
    void     stat_reset() { hits_ = 0; misses_ = 0; }

private:
    Godot_MeshCache() : hits_(0), misses_(0) {}

    std::unordered_map<EntityMeshCacheKey,
                       EntityMeshCacheEntry,
                       EntityMeshCacheHash> cache_;
    uint64_t hits_;
    uint64_t misses_;
};

/* ── Phase 61: Material Cache ── */

/*
 * MaterialCacheKey — uniquely identifies a material appearance.
 *
 * Keyed on shader handle + RGBA tint + blend mode so entities with
 * identical appearance share a single Material instance.
 */
struct MaterialCacheKey {
    int     shader_handle;
    uint8_t rgba[4];
    int     blend_mode;

    bool operator==(const MaterialCacheKey &o) const {
        return shader_handle == o.shader_handle
            && blend_mode    == o.blend_mode
            && memcmp(rgba, o.rgba, 4) == 0;
    }
};

struct MaterialCacheHash {
    size_t operator()(const MaterialCacheKey &k) const {
        size_t h = static_cast<size_t>(k.shader_handle);
        h ^= (static_cast<size_t>(k.rgba[0]) << 0)
           | (static_cast<size_t>(k.rgba[1]) << 8)
           | (static_cast<size_t>(k.rgba[2]) << 16)
           | (static_cast<size_t>(k.rgba[3]) << 24);
        h ^= static_cast<size_t>(k.blend_mode) * 2654435761u;
        return h;
    }
};

struct MaterialCacheEntry {
    Ref<Material> material;
    uint64_t      last_used_frame;
};

/*
 * Godot_MaterialCache — singleton material cache.
 *
 * MoHAARunner Integration Required:
 *   In update_entities(), when applying material tint / alpha:
 *     1. Build MaterialCacheKey from (shader_handle, rgba, blend_mode)
 *     2. Call Godot_MaterialCache_Get() — returns cached material or nullptr
 *     3. If nullptr, create material, then Godot_MaterialCache_Put()
 *
 *   Works with both StandardMaterial3D and ShaderMaterial (stores as
 *   Ref<Material> base class).
 */
class Godot_MaterialCache {
public:
    static Godot_MaterialCache &get();

    /* Look up a cached material.  Returns empty Ref if not found. */
    Ref<Material> lookup(const MaterialCacheKey &key, uint64_t current_frame);

    /* Insert a material into the cache. */
    void store(const MaterialCacheKey &key,
               Ref<Material> material,
               uint64_t current_frame);

    /* Evict entries not used for `max_stale_frames` frames. */
    void evict_stale(uint64_t current_frame, uint64_t max_stale_frames = 300);

    /* Clear the entire cache. */
    void clear();

    /* Phase 85: statistics */
    uint64_t stat_hits()   const { return hits_;   }
    uint64_t stat_misses() const { return misses_; }
    size_t   stat_size()   const { return cache_.size(); }
    void     stat_reset() { hits_ = 0; misses_ = 0; }

private:
    Godot_MaterialCache() : hits_(0), misses_(0) {}

    std::unordered_map<MaterialCacheKey,
                       MaterialCacheEntry,
                       MaterialCacheHash> cache_;
    uint64_t hits_;
    uint64_t misses_;
};

/* ── Phase 85: Performance Statistics ── */

/*
 * Godot_RenderStats — per-frame counters for the render performance audit.
 *
 * MoHAARunner Integration Required:
 *   In _process(), call Godot_RenderStats_BeginFrame() at start and
 *   Godot_RenderStats_EndFrame() at end.  Optionally log via
 *   Godot_RenderStats_Log() every N frames or on cvar toggle.
 */
struct Godot_RenderStats {
    uint64_t frame_start_usec;
    uint64_t frame_end_usec;

    int entities_rendered;
    int entities_skeletal;
    int entities_static;
    int mesh_cache_hits;
    int mesh_cache_misses;
    int material_cache_hits;
    int material_cache_misses;
    int draw_calls;

    void reset() { memset(this, 0, sizeof(*this)); }
};

/* Global stats instance (defined in godot_mesh_cache.cpp) */
extern Godot_RenderStats g_render_stats;

#ifdef __cplusplus
extern "C" {
#endif

/* C-callable performance stat helpers */
void Godot_RenderStats_BeginFrame(void);
void Godot_RenderStats_EndFrame(void);
void Godot_RenderStats_Log(void);

#ifdef __cplusplus
}
#endif

#endif /* GODOT_MESH_CACHE_H */
