#include "world.h"
#include <algorithm>

namespace {
// Keep this far away from surfaces after a sweep so the next trace never starts inside.
constexpr float kDistEpsilon = 0.03125f;
}

bool rayHitsBox(const Vec3& start, const Vec3& dir, float maxT, const Vec3& bmin, const Vec3& bmax,
                float& tHit, Vec3* normalOut) {
    float tEnter = -1e30f, tExit = 1e30f;
    int enterAxis = -1;
    float enterSign = 0;
    for (int a = 0; a < 3; ++a) {
        float s = start[a], d = dir[a];
        if (std::fabs(d) < 1e-12f) {
            if (s <= bmin[a] || s >= bmax[a]) return false;
            continue;
        }
        float t1 = (bmin[a] - s) / d, t2 = (bmax[a] - s) / d;
        float sign = -1.0f;  // entering through the min face
        if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
        if (t1 > tEnter) { tEnter = t1; enterAxis = a; enterSign = sign; }
        tExit = std::min(tExit, t2);
        if (tEnter >= tExit) return false;
    }
    if (tExit <= 0 || tEnter > maxT) return false;
    tHit = tEnter;
    if (normalOut) {
        Vec3 n;
        if (enterAxis >= 0) n[enterAxis] = enterSign;
        *normalOut = n;
    }
    return true;
}

TraceResult World::traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const {
    TraceResult tr;
    tr.endpos = end;
    Vec3 delta = end - start;
    float len = length(delta);

    if (len < 1e-6f) {
        if (!boxFits(start, mins, maxs)) { tr.startSolid = true; tr.fraction = 0; }
        tr.endpos = start;
        return tr;
    }

    float bestT = 1.0f;  // in units of delta
    for (const Box& b : solids) {
        // Minkowski-expand the solid by the moving box, then trace a ray.
        Vec3 emin = b.mins - maxs, emax = b.maxs - mins;
        float t;
        Vec3 n;
        if (!rayHitsBox(start, delta, bestT, emin, emax, t, &n)) continue;
        if (t < 0) {
            // Genuinely inside: flag it but let the mover escape. Merely touching: block.
            if (t * len < -kDistEpsilon) { tr.startSolid = true; continue; }
            t = 0;
        }
        if (t < bestT) {
            bestT = t;
            tr.normal = n;
        }
    }

    if (bestT < 1.0f) {
        // Pull back so we stay kDistEpsilon off the surface.
        float backed = std::max(0.0f, bestT - kDistEpsilon / len);
        tr.fraction = backed;
        tr.endpos = start + delta * backed;
    }
    return tr;
}

bool World::boxFits(const Vec3& origin, const Vec3& mins, const Vec3& maxs) const {
    Vec3 a = origin + mins, b = origin + maxs;
    for (const Box& s : solids) {
        if (a.x < s.maxs.x && b.x > s.mins.x && a.y < s.maxs.y && b.y > s.mins.y && a.z < s.maxs.z &&
            b.z > s.mins.z)
            return false;
    }
    return true;
}

World buildFeelLab() {
    World w;
    auto add = [&](Vec3 mn, Vec3 mx, uint32_t color) { w.solids.push_back({mn, mx, color}); };

    const uint32_t kFloor = 0x5a6370, kWall = 0x9aa3ad, kCrate = 0xc08a4a, kStair = 0x7d8794,
                   kSpray = 0x30343a, kPillar = 0x8c7a6b, kLane = 0x4d5662;

    // Floor and boundary walls. Player spawns at (0,0) facing +X.
    add({-512, -1024, -16}, {2560, 1024, 0}, kFloor);
    add({-528, -1040, 0}, {-512, 1040, 320}, kWall);
    add({2560, -1040, 0}, {2576, 1040, 320}, kWall);
    add({-528, -1040, 0}, {2576, -1024, 320}, kWall);
    add({-528, 1024, 0}, {2576, 1040, 320}, kWall);

    // Range lane edge markers (flush with floor, slightly raised so they read).
    add({0, -300, 0}, {2400, -296, 1}, kLane);
    add({0, 296, 0}, {2400, 300, 1}, kLane);

    // Pillar to peek off toward the range.
    add({256, 160, 0}, {320, 224, 192}, kPillar);

    // Right side: spray wall ~512 units (~13 m) from the marker at (0,-700).
    add({512, -950, 0}, {528, -450, 224}, kSpray);
    add({-8, -708, 0}, {8, -692, 2}, kLane);  // standing marker

    // Left side: crates of 32 / 64 / 80 units (64 needs a crouch-jump, 80 is unreachable).
    add({128, 480, 0}, {192, 544, 32}, kCrate);
    add({256, 480, 0}, {320, 544, 64}, kCrate);
    add({384, 480, 0}, {448, 544, 80}, kCrate);

    // Stairs: 8 steps of 16 units up to a 128-unit platform.
    for (int i = 0; i < 8; ++i)
        add({640.0f + 32 * i, 400, 0}, {672.0f + 32 * i, 528, 16.0f * (i + 1)}, kStair);
    add({896, 400, 0}, {1088, 608, 128}, kStair);

    // Peek wall with a doorway (gap y 600..664, 112 high) and dummies behind it.
    add({1280, 300, 0}, {1296, 600, 256}, kWall);
    add({1280, 664, 0}, {1296, 1024, 256}, kWall);
    add({1280, 600, 112}, {1296, 664, 256}, kWall);

    // A low wall to jump-peek / crouch behind near the range.
    add({1600, -300, 0}, {1616, -120, 40}, kWall);

    return w;
}
