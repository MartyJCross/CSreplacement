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
    int box = -1;           // index into World::solids that was hit
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

// Super-basic Dust2-style map: T spawn south, long A east, mid, B tunnels west, sites + CT north.
World buildDust();
struct PeekSpot { Vec3 cover, peek; };  // bot hides at `cover`, steps out to `peek`
const std::vector<PeekSpot>& dustPeekSpots();

// KZ / bhop course (behind the spray wall): start pad, lava floor, pads, end pad.
constexpr float kKzMinY = -880.0f, kKzMaxY = -580.0f;
constexpr float kKzStartMinX = 720.0f, kKzStartMaxX = 848.0f;  // start pad
constexpr float kKzLavaMinX = 848.0f, kKzLavaMaxX = 2352.0f;   // touching the floor here = reset
constexpr float kKzEndMinX = 2352.0f, kKzEndMaxX = 2540.0f;    // end pad
constexpr float kKzPadHeight = 24.0f;
