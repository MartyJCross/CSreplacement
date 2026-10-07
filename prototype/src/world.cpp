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
            tr.box = int(&b - solids.data());
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

    // Warm sunlit palette: sand floor, plaster walls, painted accents.
    const uint32_t kFloor = 0xc9b48a, kWall = 0xe6dcc6, kCrate = 0xb5763a, kStair = 0xb8603e,
                   kSpray = 0x1f4a52, kPillar = 0x5b7fa6, kLane = 0xe8c547, kPeek = 0x86a873, kLintel = 0x9c3d3d,
                   kLowWall = 0x6f8fb3, kTrimBlue = 0x2f6fb0, kTrimOrange = 0xd9822b, kTrimTeal = 0x2a8c84;

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
    add({1280, 300, 0}, {1296, 600, 256}, kPeek);
    add({1280, 664, 0}, {1296, 1024, 256}, kPeek);
    add({1280, 600, 112}, {1296, 664, 256}, kLintel);

    // A low wall to jump-peek / crouch behind near the range.
    add({1600, -300, 0}, {1616, -120, 40}, kLowWall);

    // KZ course: hop pad to pad over lava, gaps get wider (the last ones need air-strafe speed).
    add({kKzStartMinX, kKzMinY, 0}, {kKzStartMaxX, kKzMaxY, kKzPadHeight}, 0x3fae5a);
    add({kKzLavaMinX, kKzMinY, 0}, {kKzLavaMaxX, kKzMaxY, 0.25f}, 0xd2401e);
    {
        const float gaps[6] = {100, 120, 140, 160, 180, 200};
        float x = kKzLavaMinX;
        for (float gapLen : gaps) {
            x += gapLen;
            add({x, kKzMinY, 0}, {x + 64, kKzMaxY, kKzPadHeight}, 0x5b7fa6);
            x += 64;
        }
        // x is now 2252: the final 100-unit gap leads to the end pad at 2352.
    }
    add({kKzEndMinX, kKzMinY, 0}, {kKzEndMaxX, kKzMaxY, kKzPadHeight}, 0xe8c547);

    // Painted trim bands high on the boundary walls (out of reach: purely visual orientation cues).
    add({-512, -1024, 200}, {-510, 1024, 216}, kTrimTeal);      // behind spawn
    add({2558, -1024, 200}, {2560, 1024, 216}, kTrimOrange);    // far end of the range
    add({-512, -1024, 200}, {2560, -1022, 216}, kTrimBlue);     // right side
    add({-512, 1022, 200}, {2560, 1024, 216}, kTrimBlue);       // left side

    return w;
}
