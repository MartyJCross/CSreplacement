// Static world made of axis-aligned boxes, plus swept-box and ray traces against it.
#pragma once
#include <cstdint>
#include <vector>
#include "vecmath.h"

struct Box {
    Vec3 mins, maxs;
    uint32_t color;  // 0xRRGGBB
};

struct TraceResult {
    float fraction = 1.0f;  // 0..1 of the way from start to end
    Vec3 endpos;
    Vec3 normal;            // surface normal at impact (valid if fraction < 1)
    bool startSolid = false;
};

struct World {
    std::vector<Box> solids;

    // Sweep an AABB (given by mins/maxs relative to origin) from start to end.
    TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const;
    // Infinitely thin ray.
    TraceResult traceRay(const Vec3& start, const Vec3& end) const { return traceBox(start, end, {}, {}); }
    // True if the AABB at origin overlaps no solid.
    bool boxFits(const Vec3& origin, const Vec3& mins, const Vec3& maxs) const;
};

// Ray vs AABB slab test. Returns true and entry t in [0, maxT] on hit.
bool rayHitsBox(const Vec3& start, const Vec3& dir, float maxT, const Vec3& bmin, const Vec3& bmax,
                float& tHit, Vec3* normalOut = nullptr);

// Builds the "feel lab" greybox map.
World buildFeelLab();
