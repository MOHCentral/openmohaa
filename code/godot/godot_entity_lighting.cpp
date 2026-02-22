/*
 * godot_entity_lighting.cpp — Lightgrid + dynamic light sampling for entities.
 *
 * Phase 63: Lightgrid entity lighting.
 * Phase 64: Dynamic lights on entities.
 */

#include "godot_entity_lighting.h"

#include <cmath>
#include <cstring>
#include <algorithm>

/* ── BSP lightgrid accessor (godot_bsp_mesh.cpp) ── */
extern "C" {
    int Godot_BSP_LightForPoint(const float point[3], float ambientLight[3],
                                float directedLight[3], float lightDir[3]);
}

/* ── Dynamic light accessor (godot_renderer.c) ── */
extern "C" {
    int  Godot_Renderer_GetDlightCount(void);
    void Godot_Renderer_GetDlight(int index,
                                  float *origin,
                                  float *intensity,
                                  float *r, float *g, float *b,
                                  int   *type);
}

/* ── Helpers ── */

static inline float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline float vec3_dist_sq(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

/* ===================================================================
 *  Phase 63: Lightgrid Entity Lighting
 * ================================================================ */

extern "C"
void Godot_EntityLight_Sample(const float id_origin[3],
                              float *out_r, float *out_g, float *out_b)
{
    float ambient[3]  = {0.5f, 0.5f, 0.5f};
    float directed[3] = {0.0f, 0.0f, 0.0f};
    float lightDir[3] = {0.0f, 0.0f, 1.0f};

    Godot_BSP_LightForPoint(id_origin, ambient, directed, lightDir);

    /*
     * Combine ambient + directed for a single entity modulation colour.
     * The directed component represents the strongest directional light;
     * we add half of it to the ambient for a reasonable approximation
     * without per-vertex dot-product lighting.
     */
    if (out_r) *out_r = clamp01(ambient[0] + directed[0] * 0.5f);
    if (out_g) *out_g = clamp01(ambient[1] + directed[1] * 0.5f);
    if (out_b) *out_b = clamp01(ambient[2] + directed[2] * 0.5f);
}

/* ===================================================================
 *  Phase 64: Dynamic Lights on Entities
 * ================================================================ */

/* Dlight candidate for sorting by distance. */
struct DlightCandidate {
    float dist_sq;
    float r, g, b;
    float intensity;
};

extern "C"
void Godot_EntityLight_Dlights(const float id_origin[3],
                               int max_lights,
                               float *out_r, float *out_g, float *out_b)
{
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;

    int dlightCount = Godot_Renderer_GetDlightCount();
    if (dlightCount <= 0 || max_lights <= 0) {
        if (out_r) *out_r = 0.0f;
        if (out_g) *out_g = 0.0f;
        if (out_b) *out_b = 0.0f;
        return;
    }

    /* Cap at a reasonable maximum */
    if (max_lights > 64) max_lights = 64;

    /* Collect all dlights with their distances */
    DlightCandidate candidates[64];
    int numCandidates = 0;

    for (int i = 0; i < dlightCount && i < 64; i++) {
        float origin[3], intensity, cr, cg, cb;
        int type;

        Godot_Renderer_GetDlight(i, origin, &intensity, &cr, &cg, &cb, &type);
        if (intensity <= 0.0f) continue;

        float dsq = vec3_dist_sq(id_origin, origin);

        /* Skip lights beyond their effective radius.
         * Effective radius ≈ intensity (in id units). */
        if (dsq > intensity * intensity) continue;

        candidates[numCandidates].dist_sq   = dsq;
        candidates[numCandidates].r         = cr;
        candidates[numCandidates].g         = cg;
        candidates[numCandidates].b         = cb;
        candidates[numCandidates].intensity = intensity;
        numCandidates++;
    }

    /* Sort by distance (closest first) — simple insertion sort for ≤64 */
    for (int i = 1; i < numCandidates; i++) {
        DlightCandidate tmp = candidates[i];
        int j = i - 1;
        while (j >= 0 && candidates[j].dist_sq > tmp.dist_sq) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = tmp;
    }

    /* Accumulate contribution from the N closest lights */
    int count = (numCandidates < max_lights) ? numCandidates : max_lights;
    for (int i = 0; i < count; i++) {
        float dist = sqrtf(candidates[i].dist_sq);
        float radius = candidates[i].intensity;

        /* Linear attenuation: full at centre, zero at radius */
        float atten = 1.0f - (dist / radius);
        if (atten <= 0.0f) continue;

        /* Normalise dlight colour (engine stores as 0–1 float) */
        rr += candidates[i].r * atten;
        gg += candidates[i].g * atten;
        bb += candidates[i].b * atten;
    }

    if (out_r) *out_r = rr;
    if (out_g) *out_g = gg;
    if (out_b) *out_b = bb;
}

/* ===================================================================
 *  Convenience: Combined lightgrid + dlights
 * ================================================================ */

extern "C"
void Godot_EntityLight_Combined(const float id_origin[3],
                                int max_dlights,
                                float *out_r, float *out_g, float *out_b)
{
    float lr, lg, lb;
    float dr, dg, db;

    Godot_EntityLight_Sample(id_origin, &lr, &lg, &lb);
    Godot_EntityLight_Dlights(id_origin, max_dlights, &dr, &dg, &db);

    if (out_r) *out_r = clamp01(lr + dr);
    if (out_g) *out_g = clamp01(lg + dg);
    if (out_b) *out_b = clamp01(lb + db);
}
