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

World buildDust() {
    World w;
    auto add = [&](Vec3 mn, Vec3 mx, uint32_t color) { w.solids.push_back({mn, mx, color}); };
    const uint32_t kSand = 0xc9b48a, kStone = 0xdcc9a3, kStoneDark = 0xc2aa80, kWood = 0x7a5230,
                   kCrate = 0xb5763a, kBlue = 0x3f6f9a;

    add({-1600, -1600, -16}, {1600, 1600, 0}, kSand);
    add({-1616, -1616, 0}, {-1600, 1616, 400}, kStoneDark);  // boundary
    add({1600, -1616, 0}, {1616, 1616, 400}, kStoneDark);
    add({-1616, -1616, 0}, {1616, -1600, 400}, kStoneDark);
    add({-1616, 1600, 0}, {1616, 1616, 400}, kStoneDark);

    // Buildings that form the three lanes.
    add({-1000, -1000, 0}, {-200, 500, 256}, kStone);   // between B tunnels and mid
    add({200, -1000, 0}, {1000, 300, 256}, kStone);     // between mid and long
    add({-600, 700, 0}, {600, 1200, 256}, kStone);      // CT building
    add({-1000, 480, 240}, {-200, 500, 264}, kBlue);    // painted eaves (orientation cues)
    add({200, 280, 240}, {1000, 300, 264}, kBlue);

    // Long doors (long A), B tunnel exit, mid doors: walls with a doorway.
    add({1000, -216, 0}, {1200, -200, 256}, kStoneDark);
    add({1360, -216, 0}, {1600, -200, 256}, kStoneDark);
    add({1200, -216, 128}, {1360, -200, 256}, kWood);
    add({-1600, 200, 0}, {-1400, 216, 256}, kStoneDark);
    add({-1240, 200, 0}, {-1000, 216, 256}, kStoneDark);
    add({-1400, 200, 120}, {-1240, 216, 256}, kWood);
    add({-200, 500, 0}, {-60, 516, 256}, kWood);
    add({60, 500, 0}, {200, 516, 256}, kWood);
    add({-60, 500, 140}, {60, 516, 256}, kStoneDark);

    // Cover.
    add({-60, -200, 0}, {40, -120, 72}, kCrate);         // xbox in mid
    add({400, 300, 0}, {1000, 420, 40}, kStoneDark);     // catwalk ledge on short
    add({1150, 850, 0}, {1250, 950, 64}, kCrate);        // A site default boxes
    add({1170, 870, 64}, {1230, 930, 112}, kCrate);
    add({1450, 1150, 0}, {1600, 1300, 48}, kStoneDark);  // goose
    add({-1300, 800, 0}, {-1200, 900, 64}, kCrate);      // B site boxes
    add({-1290, 810, 64}, {-1230, 870, 104}, kCrate);
    add({-1480, 1100, 0}, {-1360, 1250, 56}, 0x8a3b32); // B car
    add({-300, -1300, 0}, {-200, -1200, 64}, kCrate);    // T spawn cover
    add({250, -1350, 0}, {330, -1270, 64}, kCrate);
    add({900, 1350, 0}, {1000, 1450, 64}, kCrate);       // CT spawn cover
    return w;
}

const std::vector<PeekSpot>& dustPeekSpots() {
    static const std::vector<PeekSpot> spots = {
        {{1100, -150, 0}, {1280, -150, 0}},    // long doors
        {{-130, 570, 0}, {0, 570, 0}},         // mid doors
        {{-1500, 260, 0}, {-1320, 260, 0}},    // B tunnel exit
        {{1200, 1000, 0}, {1300, 1000, 0}},    // A site default box
        {{-1250, 950, 0}, {-1150, 950, 0}},    // B site box
        {{-10, -90, 0}, {100, -90, 0}},        // xbox
    };
    return spots;
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

    // Detail: overhead beams, wall pillars, windows and barrels (kept out of the play lanes).
    for (float bx : {600.0f, 1200.0f, 1800.0f}) {
        add({bx, -1024, 262}, {bx + 24, 1024, 280}, 0x6b4a2f);            // roof beam
        add({bx, -1022, 0}, {bx + 24, -1004, 262}, 0xcfc2a5);             // pillars under it
        add({bx, 1004, 0}, {bx + 24, 1022, 262}, 0xcfc2a5);
    }
    for (float wx : {300.0f, 900.0f, 1500.0f, 2100.0f}) {                 // windows (frame + dark glass)
        add({wx, 1018, 118}, {wx + 120, 1022, 188}, 0x7a5c3a);
        add({wx + 8, 1016, 126}, {wx + 112, 1018, 180}, 0x24384f);
        add({wx, -1022, 118}, {wx + 120, -1018, 188}, 0x7a5c3a);
        add({wx + 8, -1018, 126}, {wx + 112, -1016, 180}, 0x24384f);
    }
    for (float bx : {-420.0f, -360.0f, 2440.0f, 2480.0f}) {               // barrels in the back corners
        float by = bx < 0 ? 960.0f : -980.0f;
        add({bx, by - 14, 0}, {bx + 28, by + 14, 44}, 0x2f6fb0);
        add({bx - 1, by - 15, 40}, {bx + 29, by + 15, 44}, 0x1f4a7a);
    }

    // Painted trim bands high on the boundary walls (out of reach: purely visual orientation cues).
    add({-512, -1024, 200}, {-510, 1024, 216}, kTrimTeal);      // behind spawn
    add({2558, -1024, 200}, {2560, 1024, 216}, kTrimOrange);    // far end of the range
    add({-512, -1024, 200}, {2560, -1022, 216}, kTrimBlue);     // right side
    add({-512, 1022, 200}, {2560, 1024, 216}, kTrimBlue);       // left side

    return w;
}
