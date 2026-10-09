#include "world.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {
// Keep this far away from surfaces after a sweep so the next trace never starts inside.
constexpr float kDistEpsilon = 0.03125f;

// Plane offset so the moving box's nearest corner touches it (Minkowski sum with the box).
float offsetDist(const Plane& p, const Vec3& mins, const Vec3& maxs) {
    Vec3 ofs{p.n.x < 0 ? maxs.x : mins.x, p.n.y < 0 ? maxs.y : mins.y, p.n.z < 0 ? maxs.z : mins.z};
    return p.d - dot(ofs, p.n);
}

// Sweeps a box (mins/maxs) from start along delta against a ramp, Quake-style (clip against each plane
// pushed out by the box). For an axis-aligned ramp these planes are every separating axis, so it's
// exact. Returns the entry fraction and normal, -1 for no hit; `inside` if it starts embedded.
float sweepRamp(const Box& b, const Vec3& start, const Vec3& delta, const Vec3& mins, const Vec3& maxs, Vec3& normal,
                bool& inside) {
    Plane pl[6];
    rampPlanes(b, pl);
    float enter = -1.0f, leave = 2.0f, nearest = -1e30f, nearestD2 = 0;
    Vec3 enterN, nearestN;
    bool startOut = false;
    for (const Plane& p : pl) {
        float dist = offsetDist(p, mins, maxs);
        float d1 = dot(start, p.n) - dist, d2 = dot(start + delta, p.n) - dist;
        if (d1 > nearest) { nearest = d1; nearestN = p.n; nearestD2 = d2; }
        if (d1 > 0) startOut = true;
        if (d1 > 0 && d2 > 0) return -1.0f;  // the whole move stays outside this face
        if (d1 <= 0 && d2 <= 0) continue;
        float f = d1 / (d1 - d2);
        if (d1 > d2) {
            if (f > enter) { enter = f; enterN = p.n; }
        } else if (f < leave) {
            leave = f;
        }
    }
    if (!startOut) {
        if (-nearest < kDistEpsilon) {  // just touching: block unless moving away from that face
            if (nearestD2 > nearest) return -1.0f;
            normal = nearestN;
            return 0.0f;
        }
        inside = true;
        return -1.0f;
    }
    if (enter >= 0 && enter < leave) {
        normal = enterN;
        return enter;
    }
    return -1.0f;
}

bool overlapsRamp(const Box& b, const Vec3& origin, const Vec3& mins, const Vec3& maxs) {
    Plane pl[6];
    rampPlanes(b, pl);
    for (const Plane& p : pl)
        if (dot(origin, p.n) - offsetDist(p, mins, maxs) >= 0) return false;
    return true;
}
}  // namespace

void rampPlanes(const Box& b, Plane out[6]) {
    out[0] = {{-1, 0, 0}, -b.mins.x};
    out[1] = {{1, 0, 0}, b.maxs.x};
    out[2] = {{0, -1, 0}, -b.mins.y};
    out[3] = {{0, 1, 0}, b.maxs.y};
    out[4] = {{0, 0, -1}, -b.mins.z};
    // Top: z <= lowZ + k * (distance from the low edge).
    const float rise = b.maxs.z - b.lowZ;
    Vec3 n{0, 0, 1};
    float d = b.maxs.z;
    switch (b.slope) {
        case kRisePosX: { float k = rise / (b.maxs.x - b.mins.x); n = {-k, 0, 1}; d = b.lowZ - k * b.mins.x; break; }
        case kRiseNegX: { float k = rise / (b.maxs.x - b.mins.x); n = {k, 0, 1}; d = b.lowZ + k * b.maxs.x; break; }
        case kRisePosY: { float k = rise / (b.maxs.y - b.mins.y); n = {0, -k, 1}; d = b.lowZ - k * b.mins.y; break; }
        case kRiseNegY: { float k = rise / (b.maxs.y - b.mins.y); n = {0, k, 1}; d = b.lowZ + k * b.maxs.y; break; }
        default: break;
    }
    float len = length(n);
    out[5] = {n * (1.0f / len), d / len};
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

template <class Fn>
void World::forCandidates(float x0, float y0, float x1, float y1, Fn&& fn) const {
    if (idxW_ == 0) {
        for (size_t k = 0; k < solids.size(); ++k) fn(solids[k]);
        return;
    }
    auto cellX = [&](float x) { return std::clamp(int(std::floor((x - idxX0_) / idxCell_)), 0, idxW_ - 1); };
    auto cellY = [&](float y) { return std::clamp(int(std::floor((y - idxY0_) / idxCell_)), 0, idxH_ - 1); };
    int i0 = cellX(x0), i1 = cellX(x1), j0 = cellY(y0), j1 = cellY(y1);
    if (++stampId_ == 0) {  // wrapped: forget every old mark
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        stampId_ = 1;
    }
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            size_t c = size_t(j * idxW_ + i);
            for (uint32_t k = idxStart_[c]; k < idxStart_[c + 1]; ++k) {
                uint32_t b = idxItems_[k];
                if (stamp_[b] == stampId_) continue;
                stamp_[b] = stampId_;
                fn(solids[b]);
            }
        }
}

void World::buildIndex(float cellSize) {
    if (solids.empty()) return;
    Vec3 lo = solids[0].mins, hi = solids[0].maxs;
    for (const Box& b : solids)
        for (int a = 0; a < 2; ++a) { lo[a] = std::min(lo[a], b.mins[a]); hi[a] = std::max(hi[a], b.maxs[a]); }
    idxCell_ = cellSize;
    idxX0_ = lo.x;
    idxY0_ = lo.y;
    idxW_ = std::max(1, int(std::ceil((hi.x - lo.x) / cellSize)));
    idxH_ = std::max(1, int(std::ceil((hi.y - lo.y) / cellSize)));
    // Counting sort into per-cell ranges. A box goes in every cell it overlaps (with a small margin).
    auto range = [&](const Box& b, int& i0, int& i1, int& j0, int& j1) {
        i0 = std::clamp(int(std::floor((b.mins.x - 1 - idxX0_) / idxCell_)), 0, idxW_ - 1);
        i1 = std::clamp(int(std::floor((b.maxs.x + 1 - idxX0_) / idxCell_)), 0, idxW_ - 1);
        j0 = std::clamp(int(std::floor((b.mins.y - 1 - idxY0_) / idxCell_)), 0, idxH_ - 1);
        j1 = std::clamp(int(std::floor((b.maxs.y + 1 - idxY0_) / idxCell_)), 0, idxH_ - 1);
    };
    idxStart_.assign(size_t(idxW_ * idxH_) + 1, 0u);
    int i0, i1, j0, j1;
    for (const Box& b : solids) {
        range(b, i0, i1, j0, j1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) idxStart_[size_t(j * idxW_ + i) + 1]++;
    }
    for (size_t c = 1; c < idxStart_.size(); ++c) idxStart_[c] += idxStart_[c - 1];
    idxItems_.assign(idxStart_.back(), 0u);
    std::vector<uint32_t> fill(idxStart_.begin(), idxStart_.end() - 1);
    for (size_t k = 0; k < solids.size(); ++k) {
        range(solids[k], i0, i1, j0, j1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) idxItems_[fill[size_t(j * idxW_ + i)]++] = uint32_t(k);
    }
    stamp_.assign(solids.size(), 0u);
    stampId_ = 0;
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
    auto test = [&](const Box& b) {
        if (b.slope != kFlat) {  // ramp
            Vec3 n;
            bool inside = false;
            float t = sweepRamp(b, start, delta, mins, maxs, n, inside);
            if (inside) tr.startSolid = true;
            if (t >= 0 && t < bestT) {
                bestT = t;
                tr.normal = n;
                tr.box = int(&b - solids.data());
            }
            return;
        }
        // Minkowski-expand the solid by the moving box, then trace a ray.
        Vec3 emin = b.mins - maxs, emax = b.maxs - mins;
        float t;
        Vec3 n;
        if (!rayHitsBox(start, delta, bestT, emin, emax, t, &n)) return;
        if (t < 0) {
            // Genuinely inside: flag it but let the mover escape. Merely touching: block.
            if (t * len < -kDistEpsilon) { tr.startSolid = true; return; }
            t = 0;
        }
        if (t < bestT) {
            bestT = t;
            tr.normal = n;
            tr.box = int(&b - solids.data());
        }
    };
    forCandidates(std::min(start.x, end.x) + mins.x, std::min(start.y, end.y) + mins.y,
                  std::max(start.x, end.x) + maxs.x, std::max(start.y, end.y) + maxs.y, test);

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
    bool fits = true;
    forCandidates(a.x, a.y, b.x, b.y, [&](const Box& s) {
        if (a.x < s.maxs.x && b.x > s.mins.x && a.y < s.maxs.y && b.y > s.mins.y && a.z < s.maxs.z && b.z > s.mins.z)
            if (s.slope == kFlat || overlapsRamp(s, origin, mins, maxs)) fits = false;
    });
    return fits;
}

// ---------------------------------------------------------------------------------------------
// Dust2 at real scale. Coordinates are Source units, +x = east, +y = north, 250 u/s = knife run.
// The layout is described as named floor areas on a 32-unit grid; later areas override earlier
// ones. Any cell no area covers is solid wall, with its height taken from the nearest floor.
// Ramps are smooth wedges for movement; the grid keeps them as steps for the bots' navigation.
// ---------------------------------------------------------------------------------------------
namespace {

struct DustArea {
    const char* name;
    float x0, y0, x1, y1;  // cells whose centre lies in [x0, x1) x [y0, y1)
    float z0, z1;          // floor height; a ramp goes from z0 at the min edge to z1 at the max edge
    char axis;             // 0 = flat, 'x' or 'y' = ramp along that axis
    uint32_t color;
    float roof;            // ceiling height above the floor (0 = open sky)
};

constexpr uint32_t kDSand = 0xc9b48a, kDSandLight = 0xd3bf94, kDSandDark = 0xb7a079, kDTunnel = 0x8f7d5e,
                   kDSite = 0xd6c39b, kDWood = 0x7a5230, kDStone = 0xdcc9a3, kDCrate = 0xb5763a,
                   kDBlue = 0x3f6f9a, kDRoof = 0x6b5a42, kDCar = 0x8a3b32, kDCarTop = 0x5d2a24, kDBarrel = 0x4f6d7a,
                   kDTrimLight = 0xe4d4b0, kDTrimDark = 0x9c8763, kDPane = 0x2c2a2a, kDDoor = 0x5e3f24;

const DustArea kDustAreas[] = {
    // T side.
    {"T SPAWN", -900, -1100, 200, -500, 64, 64, 0, kDSand, 0},
    {"OUTSIDE LONG", 200, -880, 650, -560, 64, 48, 'x', kDSand, 0},
    {"OUTSIDE LONG", 420, -560, 650, 200, 48, 0, 'y', kDSand, 0},
    {"OUTSIDE LONG", 420, 200, 650, 460, 0, 0, 0, kDSand, 0},
    {"OUTSIDE TUNNELS", -1820, -880, -900, -580, 64, 64, 0, kDSand, 0},
    {"OUTSIDE TUNNELS", -2150, -880, -1820, 700, 64, 0, 'y', kDSand, 0},
    {"TOP MID", -420, -500, 20, 300, 64, 0, 'y', kDSand, 0},
    // Long A.
    {"LONG DOORS", 650, 300, 900, 460, 0, 0, 0, kDSandDark, 144},
    {"LONG A", 900, 180, 1500, 820, 0, 0, 0, kDSand, 0},
    {"LONG A", 1290, 820, 1800, 2050, 0, 0, 0, kDSand, 0},
    {"PIT", 1500, 180, 1800, 600, -160, -160, 0, kDSandDark, 0},
    {"PIT", 1620, 600, 1800, 950, -160, 0, 'y', kDSandDark, 0},
    {"A RAMP", 1320, 2050, 1800, 2450, 0, 96, 'y', kDSandLight, 0},
    {"A SITE", 750, 2450, 1800, 3150, 96, 96, 0, kDSite, 0},
    {"GOOSE", 1620, 2950, 1800, 3150, 144, 144, 0, kDSandLight, 0},
    // Mid, catwalk and short A.
    {"MID", -300, 300, 60, 1880, 0, -64, 'y', kDSand, 0},
    {"CATWALK", 60, 1000, 280, 1250, -28, 32, 'y', kDSandLight, 0},
    {"CATWALK", 60, 1250, 280, 2050, 32, 32, 0, kDSandLight, 0},
    {"SHORT", 150, 2050, 330, 2350, 32, 96, 'y', kDSandLight, 0},
    {"SHORT", 150, 2350, 750, 2650, 96, 96, 0, kDSite, 0},
    {"MID DOORS", -240, 1880, -120, 1912, -64, -64, 0, kDSandDark, 128},
    // CT side.
    {"CT MID", -700, 1912, 120, 2350, -64, -64, 0, kDSand, 0},
    {"CT SPAWN", -450, 2350, 120, 3150, -64, -64, 0, kDSand, 0},
    {"CT RAMP", 120, 2800, 750, 3150, -64, 96, 'x', kDSandLight, 0},
    {"MID TO B", -1350, 2050, -700, 2350, 32, -64, 'x', kDSand, 0},
    {"B DOORS", -1420, 2140, -1350, 2280, 32, 32, 0, kDSandDark, 128},
    // B window: a hole through the door wall (sill at +48, 64 high): see and shoot through, can't walk.
    {"B WINDOW", -1420, 2296, -1350, 2344, 80, 80, 0, kDSandDark, 64},
    // B.
    {"B SITE", -2300, 1950, -1420, 3150, 32, 32, 0, kDSite, 0},
    {"BACK PLAT", -2300, 2850, -2000, 3150, 80, 80, 0, kDSandLight, 0},
    {"UPPER TUNNELS", -2200, 700, -1940, 1500, 0, 0, 0, kDTunnel, 128},
    {"UPPER TUNNELS", -2200, 1500, -1960, 1950, 0, 32, 'y', kDTunnel, 128},
    {"LOWER TUNNELS", -1940, 1060, -1550, 1240, 0, -128, 'x', kDTunnel, 128},
    {"LOWER TUNNELS", -1550, 1060, -750, 1240, -128, -128, 0, kDTunnel, 128},
    {"LOWER TUNNELS", -750, 1060, -300, 1240, -128, -36, 'x', kDTunnel, 128},
};
constexpr int kDustAreaCount = int(sizeof(kDustAreas) / sizeof(kDustAreas[0]));

// Harbor: Crisp's own map (designed for it, free to share). A harbour town: T spawn south, CT spawn north, A east
// up from the docks, B west through the underpass, a market and mid between them. Units are its real size (it
// doesn't scale). +y = north.
constexpr uint32_t kHSand = 0xc8bca2, kHSandLight = 0xd4c9b0, kHSandDark = 0xb4a68a, kHDock = 0xa49a8a,
                   kHSite = 0xd2c4a6, kHTunnel = 0x847a6a, kHMarket = 0xcdb894, kHContainerRed = 0x9a3b32,
                   kHContainerGreen = 0x3f7a4a;
const DustArea kHarborAreas[] = {
    // T side.
    {"T SPAWN", -600, -1600, 600, -1100, 0, 0, 0, kHSand, 0},
    {"T EAST", 600, -1500, 900, -1250, 0, 0, 0, kHSand, 0},
    {"T WEST", -900, -1500, -600, -1250, 0, 0, 0, kHSand, 0},
    // A: the docks (a long straight lane by the water), the ramp up, the site, the way back to CT.
    {"DOCKS", 900, -1500, 1200, -1000, 0, -48, 'y', kHDock, 0},
    {"DOCKS", 900, -1000, 1200, 650, -48, -48, 0, kHDock, 0},
    {"A RAMP", 900, 650, 1250, 950, -48, 64, 'y', kHSandLight, 0},
    {"A SITE", 550, 950, 1450, 1550, 64, 64, 0, kHSite, 0},
    {"A CT RAMP", 550, 1550, 750, 1800, 64, 0, 'y', kHSandLight, 0},
    // Mid: the market, mid (a gentle slope down), the warehouse to A short, mid doors to CT mid.
    {"MARKET", -250, -1100, 250, -350, 0, 0, 0, kHMarket, 0},
    {"MID", -400, -350, 400, 500, 0, -32, 'y', kHSand, 0},
    {"WAREHOUSE", 400, 150, 700, 450, -16, -16, 0, kHTunnel, 160},
    {"A SHORT", 550, 450, 700, 950, -16, 64, 'y', kHSandLight, 0},
    {"MID DOORS", -100, 500, 100, 560, -32, -32, 0, kHSandDark, 128},
    {"CT MID", -450, 560, 450, 1050, -32, -32, 0, kHSand, 0},
    {"CT RAMP", -450, 1050, 450, 1250, -32, 0, 'y', kHSandLight, 0},
    {"CT SPAWN", -450, 1250, 450, 1800, 0, 0, 0, kHSand, 0},
    {"CT SPAWN", -450, 1800, 750, 1900, 0, 0, 0, kHSand, 0},
    // B: the lane, the roofed underpass, the way up into the site; the alley from mid; the platform.
    {"B LANE", -1200, -1500, -900, -700, 0, 0, 0, kHSand, 0},
    {"UNDERPASS", -1200, -700, -900, -400, 0, -32, 'y', kHTunnel, 140},
    {"UNDERPASS", -1200, -400, -900, 400, -32, -32, 0, kHTunnel, 140},
    {"B ENTRANCE", -1250, 400, -850, 700, -32, 0, 'y', kHSandDark, 0},
    {"B ALLEY", -700, 0, -400, 250, -16, -16, 0, kHSandDark, 0},
    {"B ALLEY", -800, 250, -600, 700, -16, 0, 'y', kHSandDark, 0},
    {"B SITE", -1600, 700, -650, 1400, 0, 0, 0, kHSite, 0},
    {"B PLATFORM", -1600, 1400, -1350, 1650, 64, 64, 0, kHSandLight, 0},
    {"B RAMP", -1350, 1400, -1200, 1650, 0, 64, 'y', kHSandLight, 0},
    {"B CT PATH", -650, 1300, -450, 1500, 0, 0, 0, kHSand, 0},
};

// The town maps: their areas, how far the grid reaches (real units), whether DUST SIZE scales them, and their
// three wall shades.
struct TownDef {
    const char* name;
    const DustArea* areas;
    int count;
    float minX, minY, maxX, maxY;
    bool scalable;
    uint32_t shades[3];
};
const TownDef kTowns[2] = {
    {"DUST2", kDustAreas, kDustAreaCount, -2400, -1216, 1952, 3264, true, {kDStone, 0xd2bd94, 0xc7b089}},
    {"HARBOR", kHarborAreas, int(sizeof(kHarborAreas) / sizeof(kHarborAreas[0])), -1650, -1650, 1500, 1950, false,
     {0xdcd6c8, 0xcfc8b6, 0xc2bba8}},
};
constexpr float kDustBottom = -320.0f;  // underside of every floor and wall column

// Wall top for a wall whose nearest floor is at `z`: high enough that nothing can be climbed.
float wallTop(float z) { return std::ceil((z + 224.0f) / 32.0f) * 32.0f; }

// Greedy rectangle merge over a w*h grid of keys (-1 = nothing). emit(i0, j0, i1, j1, key), inclusive.
template <class Emit>
void mergeRects(int w, int h, const std::vector<int64_t>& key, Emit&& emit) {
    std::vector<char> used(key.size(), 0);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            size_t c = size_t(j * w + i);
            if (used[c] || key[c] < 0) continue;
            int64_t k = key[c];
            int i1 = i;
            while (i1 + 1 < w && !used[size_t(j * w + i1 + 1)] && key[size_t(j * w + i1 + 1)] == k) ++i1;
            int j1 = j;
            for (bool grow = true; grow && j1 + 1 < h;) {
                for (int x = i; x <= i1 && grow; ++x) {
                    size_t nb = size_t((j1 + 1) * w + x);
                    grow = !used[nb] && key[nb] == k;
                }
                if (grow) ++j1;
            }
            for (int y = j; y <= j1; ++y)
                for (int x = i; x <= i1; ++x) used[size_t(y * w + x)] = 1;
            emit(i, j, i1, j1, k);
        }
}

int64_t zKey(float z) { return int64_t(std::lround(z)) + 65536; }  // non-negative integer for a height

}  // namespace

bool MapGrid::cellAt(float x, float y, int& i, int& j) const {
    i = int(std::floor((x - x0) / cell));
    j = int(std::floor((y - y0) / cell));
    return i >= 0 && j >= 0 && i < w && j < h;
}

float MapGrid::floorAt(float x, float y) const {
    int i, j;
    return cellAt(x, y, i, j) ? floor[size_t(index(i, j))] : kNoFloor;
}

namespace {

float g_dustScale = 0.6f;   // DUST SIZE (Dust2 only: Harbor is built at its own size)
int g_town = 0;             // the active town map: 0 Dust2, 1 Harbor
bool g_dustBuilt = false;
MapGrid g_dustGrid;
const TownDef& town() { return kTowns[g_town]; }
float scaleNow() { return town().scalable ? g_dustScale : 1.0f; }

// An area's extent along one axis at the current scale. It never shrinks below min(original, 96)
// units, so doorways stay a doorway (and thin areas don't fall between grid cells).
void scaledSpan(float a0, float a1, float& b0, float& b1) {
    const float sc = scaleNow();
    float c = (a0 + a1) * 0.5f * sc;
    float len = std::max((a1 - a0) * sc, std::min(a1 - a0, 96.0f));
    b0 = c - len * 0.5f;
    b1 = c + len * 0.5f;
}

void buildDustGrid() {
    const float s = scaleNow();
    const TownDef& td = town();
    MapGrid m;
    m.cell = 32;
    m.x0 = std::floor(td.minX * s / m.cell) * m.cell - 64;
    m.y0 = std::floor(td.minY * s / m.cell) * m.cell - 64;
    m.w = int(std::ceil((td.maxX * s - m.x0) / m.cell)) + 2;
    m.h = int(std::ceil((td.maxY * s - m.y0) / m.cell)) + 2;
    size_t n = size_t(m.w * m.h);
    m.floor.assign(n, MapGrid::kNoFloor);
    m.ceiling.assign(n, MapGrid::kOpenSky);
    m.area.assign(n, -1);
    for (int a = 0; a < td.count; ++a) {
        const DustArea& d = td.areas[a];
        float x0, x1, y0, y1;
        scaledSpan(d.x0, d.x1, x0, x1);
        scaledSpan(d.y0, d.y1, y0, y1);
        for (int j = 0; j < m.h; ++j)
            for (int i = 0; i < m.w; ++i) {
                float cx = m.x0 + (float(i) + 0.5f) * m.cell, cy = m.y0 + (float(j) + 0.5f) * m.cell;
                if (cx < x0 || cx >= x1 || cy < y0 || cy >= y1) continue;
                float t = d.axis == 'x' ? (cx - x0) / (x1 - x0) : d.axis == 'y' ? (cy - y0) / (y1 - y0) : 0.0f;
                // Heights scale with the map, so every slope stays exactly as walkable.
                float z = std::round((d.z0 + (d.z1 - d.z0) * t) * s / 4.0f) * 4.0f;
                size_t c = size_t(m.index(i, j));
                m.floor[c] = z;
                m.ceiling[c] = d.roof > 0 ? z + d.roof : MapGrid::kOpenSky;  // headroom never shrinks
                m.area[c] = a;
            }
    }
    g_dustGrid = std::move(m);
    g_dustBuilt = true;
}

}  // namespace

bool setDustScale(float scale) {
    scale = std::clamp(scale, 0.5f, 1.0f);
    if (g_dustBuilt && scale == g_dustScale) return false;
    g_dustScale = scale;
    buildDustGrid();
    return true;
}

bool setTownMap(int map) {
    map = std::clamp(map, 0, kTownMaps - 1);
    if (g_dustBuilt && map == g_town) return false;
    g_town = map;
    buildDustGrid();
    return true;
}
int townMap() { return g_town; }
const char* townMapName(int map) { return kTowns[std::clamp(map, 0, kTownMaps - 1)].name; }

float townScale() { return scaleNow(); }

const MapGrid& townGrid() {
    if (!g_dustBuilt) buildDustGrid();
    return g_dustGrid;
}

const char* townCallout(const Vec3& p) {
    const MapGrid& m = townGrid();
    int i, j;
    if (!m.cellAt(p.x, p.y, i, j)) return "";
    int a = m.area[size_t(m.index(i, j))];
    return a >= 0 ? town().areas[a].name : "";
}

Vec3 townPoint(float x, float y) {
    float s = townScale();
    return {x * s, y * s, townGrid().floorAt(x * s, y * s)};
}

std::vector<Vec3> townTeamSpawns(int side) {
    // Spread over each spawn, the first (yours) at the front, and off the long sightline through mid doors
    // (T spawn <-> CT spawn), so nobody can see the other team when the round starts (tested).
    static const float xy[2][2][5][2] = {
        {{{-560, -650}, {-720, -820}, {120, -760}, {100, -950}, {-600, -1000}},   // Dust2: T spawn
         {{40, 2600}, {-410, 2650}, {60, 2850}, {-410, 2950}, {80, 3050}}},       // CT spawn
        {{{-400, -1200}, {-250, -1480}, {300, -1200}, {250, -1480}, {-520, -1300}},  // Harbor: T spawn
         {{-300, 1400}, {300, 1400}, {-250, 1700}, {350, 1650}, {200, 1550}}},       // CT spawn
    };
    std::vector<Vec3> out;
    for (const auto& p : xy[g_town][side == 0 ? 0 : 1]) out.push_back(townPoint(p[0], p[1]));
    return out;
}

const std::vector<RetakeSite>& townRetakeSites() {
    static const std::vector<RetakeSite> harbor = {
        {"A", 1050, 1150,  // bomb: by the default box
         {
             {945, 1330, 1050, 700},    // behind the default box, on the ramp
             {1240, 1450, 1050, 800},   // by the container, down the ramp
             {720, 1350, 620, 500},     // CT side, down short
             {1380, 1050, 1050, 650},   // the corner past the crate, on the ramp
             {650, 1050, 620, 500},     // top of short
             {1100, 1500, 650, 1700},   // back of the site, on the CT ramp
         },
         {
             {0, 1650, 650, 1650},      // CT spawn, round to the CT ramp
             {1050, 0, 1050, 900},      // the docks
             {600, 300, 620, 900},      // the warehouse, up short
         }},
        {"B", -1050, 1050,  // bomb: beside the default box
         {
             {-1500, 1550, -1050, 550},  // the platform, on the entrance
             {-1155, 1130, -1050, 550},  // behind the default box
             {-850, 1350, -550, 1400},   // by the CT path
             {-1420, 900, -1050, 550},   // by the car
             {-760, 800, -700, 300},     // the alley exit
             {-1550, 1200, -700, 900},   // back of the site
         },
         {
             {-1050, 0, -1050, 800},     // the underpass
             {-700, 400, -1100, 1000},   // the alley
             {-300, 1400, -1000, 1100},  // CT spawn, through the CT path
         }},
    };
    if (g_town == 1) return harbor;
    static const std::vector<RetakeSite> sites = {
        {"A", 1380, 2560,  // bomb: beside the default box
         {
             {1240, 2860, 1500, 2200},  // behind the default box, on the ramp
             {1750, 3060, 1450, 2300},  // goose
             {800, 2850, 400, 2500},    // CT side of site, on short
             {1700, 2420, 1550, 1600},  // top of the ramp, down long
             {550, 2550, 170, 1700},    // short, down the catwalk
             {980, 3100, 1500, 2300},   // back of site by the CT ramp
         },
         {
             {-150, 2900, 600, 2900},   // CT spawn, up the ramp
             {1550, 1500, 1550, 2500},  // long
             {170, 1500, 170, 2300},    // catwalk
         }},
        {"B", -1780, 2700,  // bomb: beside the default box
         {
             {-2150, 3000, -2100, 1950},  // back plat, on the tunnel exit
             {-1880, 2650, -2100, 1950},  // behind the default box
             {-1600, 2450, -1100, 2200},  // inside B doors
             {-1625, 2800, -2100, 1950},  // by the car
             {-1900, 3080, -2100, 2000},  // back of site
             {-2250, 2380, -1450, 2200},  // tunnel-side corner, on the doors
         },
         {
             {-2050, 1200, -2050, 2200},  // upper tunnels
             {-950, 2200, -1500, 2200},   // mid to B, through the doors
             {-350, 2250, -1500, 2200},   // CT mid
         }},
    };
    return sites;
}

const std::vector<RetakeSpot>& townCtSpots(int role) {
    static const std::vector<RetakeSpot> harbor[kCtRoles] = {
        {{0, 700, 0, -200}, {-300, 850, -50, 500}, {300, 800, 50, 500}},             // mid: behind the doors
        {{650, 1050, 620, 500}, {760, 1150, 620, 500}, {720, 1350, 620, 600}},        // short: down A short
        {{1100, 1050, 1050, 0}, {1380, 1150, 1050, 300}, {1000, 1350, 1050, 600}},    // long: down the docks
        {{1240, 1450, 1050, 800}, {820, 1450, 1000, 900}, {1200, 1100, 1050, 500}},   // an extra on A
        {{-1500, 1550, -1050, 600}, {-1155, 1130, -1050, 500}, {-850, 1350, -700, 500},  // B site
         {-1420, 900, -1050, 500}, {-760, 850, -700, 300}, {-1550, 1200, -1050, 500}},
    };
    if (g_town == 1) return harbor[std::clamp(role, 0, kCtRoles - 1)];
    static const std::vector<RetakeSpot> spots[kCtRoles] = {
        {   // mid: behind mid doors, or further back in CT mid on an angle
            {-180, 1990, -180, 1500},
            {-380, 2220, -200, 1880},
            {-560, 2280, -180, 1900},
        },
        {   // short: on short looking down the catwalk, or back on the site
            {550, 2550, 170, 1700},
            {650, 2560, 250, 2400},
            {880, 2850, 400, 2500},
        },
        {   // long: the top of the ramp, the top of long, or the box by the ramp
            {1700, 2420, 1550, 1600},
            {1550, 1950, 1550, 1200},
            {1690, 2650, 1500, 2100},
        },
        {   // an extra on A site
            {1250, 2850, 1550, 2300},
            {1580, 3080, 1450, 2400},
            {880, 2850, 1400, 2500},
        },
        {   // B site
            {-2050, 2800, -2080, 2000},
            {-1880, 2690, -2080, 1950},
            {-1625, 2800, -2080, 1950},
            {-1600, 2450, -2080, 2000},
            {-2250, 2380, -1450, 2200},
            {-1900, 3080, -2080, 2000},
        },
    };
    return spots[std::clamp(role, 0, kCtRoles - 1)];
}

const std::vector<PrefireRoute>& townPrefireRoutes() {
    static const std::vector<PrefireRoute> harbor = {
        {"DOCKS", {750, -1375, 1050, -1375},
         {
             {960, -380, 1050, -1300},   // behind the blue container
             {1140, 200, 1050, -800},    // behind the red one
             {950, 480, 1050, -500},     // past the crates
             {1100, 1050, 1050, 0},      // top of the ramp
             {945, 1330, 1050, 700},     // behind the default box
             {1240, 1450, 1050, 800},    // by the container
             {720, 1350, 1050, 800},     // CT side of the site
             {650, 1700, 650, 1100},     // the CT ramp
         }},
        {"UNDERPASS", {-750, -1375, -1050, -1375},
         {
             {-1050, -1000, -1050, -1450},  // the lane
             {-1100, 100, -1050, -600},     // the underpass
             {-1500, 1550, -1050, 600},     // the platform
             {-1155, 1130, -1050, 500},     // behind the default box
             {-1420, 900, -1050, 500},      // by the car
             {-850, 1350, -1050, 600},      // by the CT path
             {-1550, 1200, -1050, 500},     // back of the site
         }},
        {"MID", {0, -1250, 0, -500},
         {
             {330, -150, 0, -900},     // the mid box
             {-350, 150, 0, -800},     // the west crate
             {600, 300, 0, 250},       // the warehouse
             {-600, 150, -300, 150},   // the alley
             {0, 700, 0, -200},        // behind mid doors
             {300, 800, 0, 500},       // CT mid
         }},
        {"A SHORT", {0, -300, 400, 250},
         {
             {650, 300, 400, 300},     // the warehouse
             {620, 800, 620, 400},     // up short
             {650, 1050, 620, 500},    // top of short
             {945, 1330, 650, 950},    // behind the default box
             {1240, 1450, 650, 950},   // by the container
             {720, 1350, 650, 1000},   // CT side
         }},
    };
    if (g_town == 1) return harbor;
    static const std::vector<PrefireRoute> routes = {
        {"A LONG", {540, -400, 540, 300},
         {
             {1400, 760, 775, 380},     // long corner, far side
             {1700, 450, 1100, 500},    // pit
             {1560, 1400, 1100, 600},   // halfway up long
             {1550, 1950, 1550, 1200},  // top of long, past the car
             {1690, 2650, 1500, 2100},  // A site, the box by the ramp
             {1250, 2850, 1550, 2300},  // behind the default box
             {1720, 3060, 1450, 2400},  // goose
             {880, 2850, 1400, 2500},   // CT side of the site
             {500, 2560, 1200, 2600},   // short
         }},
        {"B TUNNELS", {-1100, -730, -1990, -730},
         {
             {-2080, 1450, -2080, 800},   // end of upper tunnels
             {-1300, 1150, -2000, 1150},  // lower tunnels
             {-2250, 2380, -2080, 1950},  // the corner by the tunnel exit
             {-1880, 2690, -2080, 1950},  // behind the default box
             {-2150, 3000, -2080, 2000},  // back plat
             {-1625, 2800, -2080, 1950},  // by the car
             {-1600, 2450, -2080, 2000},  // inside B doors
             {-1900, 3080, -2080, 2000},  // back of the site
         }},
        {"MID", {-700, -650, -200, -450},
         {
             {-200, 950, -150, 0},       // mid, by the xbox
             {-900, 1150, -100, 1150},   // lower tunnels, into mid
             {150, 1500, -100, 600},     // catwalk, looking down mid
             {200, 2000, -100, 1200},    // top of catwalk
             {-180, 2050, -180, 1500},   // CT mid, through the doors
             {-600, 2200, -200, 1950},   // CT mid, the far side
         }},
        {"A SHORT", {-950, 1150, -300, 1150},
         {
             {170, 1900, 170, 1100},   // top of catwalk
             {250, 2450, 170, 1900},   // top of the short stairs
             {650, 2560, 250, 2400},   // short, by the site
             {880, 2850, 600, 2550},   // CT side of the site
             {1250, 2850, 600, 2550},  // behind the default box
             {1690, 2650, 700, 2550},  // the box by the ramp
             {1720, 3060, 700, 2550},  // goose
             {1000, 3080, 450, 2500},  // back of the site, watching short
         }},
    };
    return routes;
}

MapSpawn townSpawn() {
    const float sc = scaleNow(), x = (g_town == 1 ? 0.0f : -350.0f) * sc, y = (g_town == 1 ? -1350.0f : -800.0f) * sc;
    return {{x, y, townGrid().floorAt(x, y)}, 90.0f};
}

const TownTactics& townTactics() {
    static const TownTactics kTactics[2] = {
        {   // Dust2
            {{1500, 1100}, {170, 1350}, {-2070, 1150}, {-180, 1650}, {-150, 700}},
            {{{1450, 2300, 0}, {1400, 2650, 140}},    // A long: smoke the top of the ramp, flash the site
             {{850, 2850, 0}, {450, 2520, 140}},      // short: smoke CT side of A, flash short
             {{-1450, 2210, 0}, {-1800, 2450, 140}}}, // B: smoke the doors, flash the site
            {{1150, 500, 720, 380}, {-120, 1100, -120, 100}, {-2070, 1750, -2070, 900}},
            {"PUSHING LONG", "PUSHING MID", "PUSHING B TUNNELS"},
            "SPLIT A, LONG AND SHORT", "SPLIT B, TUNNELS AND MID",
        },
        {   // Harbor
            {{1050, 450}, {550, 300}, {-1050, 250}, {-550, 120}, {0, -500}},
            {{{1050, 1150, 0}, {1000, 1250, 140}},    // the docks: smoke the top of the ramp, flash the site
             {{700, 1500, 0}, {650, 1050, 140}},      // short: smoke the CT side, flash the top of short
             {{-650, 1400, 0}, {-1150, 1050, 140}}},  // B: smoke the CT path, flash the site
            {{1050, 500, 1050, -600}, {0, 300, 0, -600}, {-1050, 300, -1050, -600}},
            {"PUSHING DOCKS", "PUSHING MID", "PUSHING UNDERPASS"},
            "SPLIT A, DOCKS AND SHORT", "SPLIT B, UNDERPASS AND ALLEY",
        },
    };
    return kTactics[g_town];
}

World buildTown() {
    const MapGrid& m = townGrid();
    World w;
    size_t n = m.floor.size();

    // Wall heights: spread the nearest floor height outwards through the walls, layer by layer.
    std::vector<float> nearZ(n, MapGrid::kNoFloor);
    std::vector<char> assigned(n, 0);
    std::vector<int> frontier, next;
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i)
            if (m.walkable(i, j)) {
                size_t c = size_t(m.index(i, j));
                nearZ[c] = m.floor[c];
                assigned[c] = 1;
                frontier.push_back(m.index(i, j));
            }
    const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
    while (!frontier.empty()) {
        next.clear();
        for (int c : frontier)
            for (int k = 0; k < 4; ++k) {
                int i = c % m.w + di[k], j = c / m.w + dj[k];
                if (i < 0 || j < 0 || i >= m.w || j >= m.h || assigned[size_t(m.index(i, j))]) continue;
                next.push_back(m.index(i, j));
            }
        std::sort(next.begin(), next.end());
        next.erase(std::unique(next.begin(), next.end()), next.end());
        for (int c : next) {  // a wall between two levels takes the higher one
            int i = c % m.w, j = c / m.w;
            for (int k = 0; k < 4; ++k) {
                int a = i + di[k], b = j + dj[k];
                if (a < 0 || b < 0 || a >= m.w || b >= m.h || !assigned[size_t(m.index(a, b))]) continue;
                nearZ[size_t(c)] = std::max(nearZ[size_t(c)], nearZ[size_t(m.index(a, b))]);
            }
        }
        for (int c : next) assigned[size_t(c)] = 1;
        frontier.swap(next);
    }

    auto rect = [&](int i0, int j0, int i1, int j1, float zMin, float zMax, uint32_t color) {
        w.solids.push_back({{m.x0 + float(i0) * m.cell, m.y0 + float(j0) * m.cell, zMin},
                            {m.x0 + float(i1 + 1) * m.cell, m.y0 + float(j1 + 1) * m.cell, zMax}, color});
    };
    std::vector<int64_t> key(n, -1);

    // Ramps: a ramp area that kept its whole rectangle becomes one smooth wedge (its cells are left out
    // of the stepped floors below). The grid keeps its steps for the bots' navigation.
    std::vector<char> smooth(n, 0);
    const TownDef& td = town();
    for (int a = 0; a < td.count; ++a) {
        const DustArea& d = td.areas[a];
        if (d.axis == 0) continue;
        int i0 = m.w, j0 = m.h, i1 = -1, j1 = -1;
        for (int j = 0; j < m.h; ++j)
            for (int i = 0; i < m.w; ++i)
                if (m.area[size_t(m.index(i, j))] == a) { i0 = std::min(i0, i); i1 = std::max(i1, i); j0 = std::min(j0, j); j1 = std::max(j1, j); }
        if (i1 < 0) continue;
        bool whole = true;
        for (int j = j0; j <= j1 && whole; ++j)
            for (int i = i0; i <= i1 && whole; ++i) whole = m.area[size_t(m.index(i, j))] == a;
        if (!whole) continue;  // partly covered by another area: stays stepped
        float u0, u1;
        d.axis == 'x' ? scaledSpan(d.x0, d.x1, u0, u1) : scaledSpan(d.y0, d.y1, u0, u1);
        const float s = townScale();
        auto zAt = [&](float u) { return (d.z0 + (d.z1 - d.z0) * std::clamp((u - u0) / (u1 - u0), 0.0f, 1.0f)) * s; };
        Box b{{m.x0 + float(i0) * m.cell, m.y0 + float(j0) * m.cell, kDustBottom},
              {m.x0 + float(i1 + 1) * m.cell, m.y0 + float(j1 + 1) * m.cell, 0}, d.color};
        float zA = d.axis == 'x' ? zAt(b.mins.x) : zAt(b.mins.y), zB = d.axis == 'x' ? zAt(b.maxs.x) : zAt(b.maxs.y);
        if (std::fabs(zB - zA) < 1.0f) continue;
        b.slope = d.axis == 'x' ? (zB > zA ? kRisePosX : kRiseNegX) : (zB > zA ? kRisePosY : kRiseNegY);
        b.lowZ = std::min(zA, zB);
        b.maxs.z = std::max(zA, zB);
        w.solids.push_back(b);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) smooth[size_t(m.index(i, j))] = 1;
    }

    // Floors (one key per height + colour).
    for (size_t c = 0; c < n; ++c)
        if (m.area[c] >= 0 && !smooth[c]) key[c] = (zKey(m.floor[c]) << 24) | int64_t(td.areas[m.area[c]].color);
    mergeRects(m.w, m.h, key, [&](int i0, int j0, int i1, int j1, int64_t k) {
        rect(i0, j0, i1, j1, kDustBottom, float((k >> 24) - 65536), uint32_t(k & 0xFFFFFF));
        if (uint32_t(k & 0xFFFFFF) == kDSite || uint32_t(k & 0xFFFFFF) == kHSite) w.solids.back().material = kMatPaving;  // the sites are paved
    });

    // Walls: each 5x5-cell block is its own building with its own height (so the skyline isn't one flat
    // line), in three slightly different stone shades.
    auto blockExtra = [](int i, int j) {
        static const float kExtra[8] = {0, 0, 32, 64, 64, 96, 160, 224};
        uint32_t h = uint32_t(i / 5) * 73856093u ^ uint32_t(j / 5) * 19349663u;
        h = (h ^ (h >> 13)) * 0x5bd1e995u;
        return kExtra[(h ^ (h >> 15)) % 8];
    };
    std::vector<float> top(n, 0.0f);
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i) {
            size_t c = size_t(m.index(i, j));
            if (m.area[c] < 0) top[c] = wallTop(nearZ[c]) + blockExtra(i, j);
        }
    const uint32_t* shades = td.shades;
    for (size_t c = 0; c < n; ++c) key[c] = m.area[c] < 0 ? zKey(top[c]) : -1;
    mergeRects(m.w, m.h, key, [&](int i0, int j0, int i1, int j1, int64_t k) {
        rect(i0, j0, i1, j1, kDustBottom, float(k - 65536), shades[uint32_t(i0 * 7 + j0 * 13) % 3]);
    });

    // Roofs over tunnels and doorways: from the ceiling up to the surrounding wall height.
    for (size_t c = 0; c < n; ++c)
        key[c] = m.area[c] >= 0 && m.ceiling[c] < MapGrid::kOpenSky
                     ? (zKey(m.ceiling[c]) << 24) | zKey(std::max(wallTop(m.floor[c]), m.ceiling[c] + 32))
                     : -1;
    mergeRects(m.w, m.h, key, [&](int i0, int j0, int i1, int j1, int64_t k) {
        rect(i0, j0, i1, j1, float((k >> 24) - 65536), float((k & 0xFFFFFF) - 65536), kDRoof);
    });

    // Facades (decor only, nothing here collides): trim along the foot and the top of every wall that
    // faces a walkway, and here and there a window, a door with an awning, or beam ends.
    auto X = [&](int i) { return m.x0 + float(i) * m.cell; };
    auto Y = [&](int j) { return m.y0 + float(j) * m.cell; };
    // A box against wall face k (0 +x, 1 -x, 2 +y, 3 -y: the side the walkway is on) of column/row `line`:
    // a0..a1 along the face, sticking out p0..p1 from it, z0..z1 high.
    auto faceBox = [&](int k, int line, float a0, float a1, float p0, float p1, float z0, float z1, uint32_t col,
                       uint8_t mat) {
        const float f = k == 0 ? X(line + 1) : k == 1 ? X(line) : k == 2 ? Y(line + 1) : Y(line);
        const float sgn = (k == 0 || k == 2) ? 1.0f : -1.0f;
        const float u0 = std::min(f + sgn * p0, f + sgn * p1), u1 = std::max(f + sgn * p0, f + sgn * p1);
        if (k < 2) w.decor.push_back({{u0, a0, z0}, {u1, a1, z1}, col, mat});
        else w.decor.push_back({{a0, u0, z0}, {a1, u1, z1}, col, mat});
    };
    auto cellHash = [](int i, int j, int k) {
        uint32_t h = uint32_t(i) * 374761393u + uint32_t(j) * 668265263u + uint32_t(k) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    };
    const uint32_t shutterCols[3] = {0x3f6f9a, 0x4f7d5a, 0x8a5a3a}, awningCols[3] = {0xa8443a, 0x3f6f9a, 0xc9a35a};
    for (int k = 0; k < 4; ++k) {
        const int lines = k < 2 ? m.w : m.h, len = k < 2 ? m.h : m.w;
        const size_t cells = static_cast<size_t>(len);
        std::vector<char> facing(cells, 0), open(cells, 0);
        std::vector<float> fz(cells, 0.0f), tp(cells, 0.0f), skirt(cells, 0.0f);
        for (int line = 0; line < lines; ++line) {
            for (int a = 0; a < len; ++a) {
                const int i = k < 2 ? line : a, j = k < 2 ? a : line, ni = i + di[k], nj = j + dj[k];
                const size_t c = size_t(m.index(i, j));
                facing[size_t(a)] = m.area[c] < 0 && m.walkable(ni, nj);
                if (!facing[size_t(a)]) continue;
                const size_t nc = size_t(m.index(ni, nj));
                fz[size_t(a)] = m.floor[nc];
                tp[size_t(a)] = top[c];
                open[size_t(a)] = m.ceiling[nc] >= MapGrid::kOpenSky;
                skirt[size_t(a)] = td.areas[m.area[nc]].axis == 0 ? m.floor[nc] : MapGrid::kNoFloor;
            }
            auto along = [&](int a) { return k < 2 ? Y(a) : X(a); };
            // Trim: runs of cells with the same heights become one box each.
            for (int a = 0; a < len;) {
                if (!facing[size_t(a)]) { ++a; continue; }
                int b = a + 1;
                while (b < len && facing[size_t(b)] && tp[size_t(b)] == tp[size_t(a)] && skirt[size_t(b)] == skirt[size_t(a)] &&
                       open[size_t(b)] == open[size_t(a)])
                    ++b;
                if (open[size_t(a)]) faceBox(k, line, along(a), along(b), 0, 5, tp[size_t(a)] - 12, tp[size_t(a)], kDTrimLight, kMatStone);
                if (skirt[size_t(a)] > MapGrid::kNoFloor)
                    faceBox(k, line, along(a), along(b), 0, 3, skirt[size_t(a)] - 4, skirt[size_t(a)] + 12, kDTrimDark, kMatStone);
                a = b;
            }
            // Features need three facing cells in a row at one height, open to the sky.
            for (int a = 1; a + 1 < len; ++a) {
                bool ok = true;
                for (int q = a - 1; q <= a + 1; ++q)
                    ok = ok && facing[size_t(q)] && open[size_t(q)] && tp[size_t(q)] == tp[size_t(a)] && fz[size_t(q)] == fz[size_t(a)];
                if (!ok) continue;
                const float z = fz[size_t(a)], height = tp[size_t(a)] - z, mid = along(a) + m.cell * 0.5f;
                const uint32_t h = cellHash(k < 2 ? line : a, k < 2 ? a : line, k), r = h % 100;
                if (r < 24 && height >= 200) {  // window(s): frame, dark pane, sill, maybe shutters
                    for (float wz = z + 110; wz + 60 <= tp[size_t(a)] - 30; wz += 110) {
                        faceBox(k, line, mid - 26, mid + 26, 0, 1.5f, wz - 6, wz + 62, kDTrimLight, kMatStone);
                        faceBox(k, line, mid - 20, mid + 20, 0, 2.5f, wz, wz + 56, kDPane, kMatPlain);
                        faceBox(k, line, mid - 28, mid + 28, 0, 5, wz - 10, wz - 5, kDTrimLight, kMatStone);
                        if ((h >> 8) % 2) {
                            const uint32_t sc = shutterCols[(h >> 9) % 3];
                            faceBox(k, line, mid - 42, mid - 28, 0, 3, wz - 2, wz + 58, sc, kMatWood);
                            faceBox(k, line, mid + 28, mid + 42, 0, 3, wz - 2, wz + 58, sc, kMatWood);
                        }
                        if ((h >> 12) % 3 != 0) break;  // mostly one storey of windows
                    }
                    a += 2;
                } else if (r < 32 && height >= 150) {  // a shut door, often under a cloth awning
                    faceBox(k, line, mid - 32, mid + 32, 0, 1.5f, z, z + 112, kDTrimLight, kMatStone);
                    faceBox(k, line, mid - 26, mid + 26, 0, 2.5f, z, z + 104, kDDoor, kMatWood);
                    if ((h >> 8) % 3 != 0)
                        faceBox(k, line, mid - 40, mid + 40, 0, 34, z + 122, z + 126, awningCols[(h >> 10) % 3], kMatPlain);
                    a += 2;
                } else if (r < 42 && height >= 170) {  // wooden beam ends under the roof line
                    for (float off : {-32.0f, 0.0f, 32.0f})
                        faceBox(k, line, mid + off - 4, mid + off + 4, 0, 16, tp[size_t(a)] - 56, tp[size_t(a)] - 48, kDWood, kMatWood);
                    a += 2;
                }
            }
        }
    }

    // Props, standing on the floor under their centre. Crates keep their real size wherever the map's
    // scale puts them; `anchored` ones are placed relative to a point that scales (a wall edge).
    const float sc = townScale();
    // Crates and doors are wood (hollow footsteps, easy to shoot through); the container and car metal.
    auto materialOf = [](uint32_t color) -> uint8_t {
        return color == kDCrate || color == kDWood ? kMatWood
               : color == kDBlue || color == kDCar || color == kDCarTop || color == kDBarrel ? kMatMetal
                                                                                              : kMatStone;
    };
    auto prop = [&](float x0, float y0, float x1, float y1, float h, uint32_t color, float lift = 0) {
        float cx = (x0 + x1) * 0.5f * sc, cy = (y0 + y1) * 0.5f * sc, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
        float z = m.floorAt(cx, cy) + lift;
        w.solids.push_back({{cx - hx, cy - hy, z}, {cx + hx, cy + hy, z + h}, color, materialOf(color)});
    };
    auto anchored = [&](float ax, float ay, float dx0, float dy0, float dx1, float dy1, float h, uint32_t color,
                        float lift = 0) {
        float x = ax * sc, y = ay * sc;
        float z = m.floorAt(x + (dx0 + dx1) * 0.5f, y + (dy0 + dy1) * 0.5f) + lift;
        w.solids.push_back({{x + dx0, y + dy0, z}, {x + dx1, y + dy1, z + h}, color, materialOf(color)});
    };
    // A car (body + cabin) and a barrel, placed like anchored().
    auto car = [&](float ax, float ay, float dx0, float dy0, float dx1, float dy1) {
        anchored(ax, ay, dx0, dy0, dx1, dy1, 40, kDCar);
        const float inX = (dx1 - dx0) * 0.18f, inY = (dy1 - dy0) * 0.18f;
        anchored(ax, ay, dx0 + inX, dy0 + inY, dx1 - inX, dy1 - inY, 24, kDCarTop, 40);
    };
    auto barrel = [&](float ax, float ay, float dx, float dy) { anchored(ax, ay, dx, dy, dx + 28, dy + 28, 44, kDBarrel); };
    // Arches: a stone beam across a passage, high enough to walk under (the lane's width scales).
    auto arch = [&](float x0, float y0, float x1, float y1, float clearance) {
        float ax0, ax1, ay0, ay1;
        scaledSpan(x0, x1, ax0, ax1);
        scaledSpan(y0, y1, ay0, ay1);
        float z = m.floorAt((ax0 + ax1) * 0.5f, (ay0 + ay1) * 0.5f) + clearance;
        w.solids.push_back({{ax0, ay0, z}, {ax1, ay1, z + 28}, 0xb9a37c, kMatStone});
    };
    if (g_town == 1) {  // Harbor: containers on the docks, market stalls, crates and a car on the sites
        car(-600, -1600, 20, 20, 120, 220);                   // T spawn: a car in the south-west corner
        prop(-450, -1450, -390, -1390, 64, kDCrate);
        prop(380, -1560, 460, -1480, 64, kDCrate);
        barrel(900, -1250, -32, -30);
        prop(910, -750, 1010, -450, 96, kDBlue);              // the docks: containers along both sides
        prop(1090, -150, 1190, 150, 96, kHContainerRed);
        prop(910, 250, 980, 320, 64, kDCrate);                // and crates further up
        prop(910, 330, 980, 400, 48, kDCrate);
        barrel(1200, -1300, -30, 0);
        prop(1180, 700, 1240, 760, 48, kDCrate);              // A ramp
        prop(900, 1200, 990, 1290, 64, kDCrate);              // A default box (double stack)
        prop(915, 1215, 975, 1275, 48, kDCrate, 64);
        prop(1250, 1000, 1330, 1080, 64, kDCrate);            // A site, by the ramp
        prop(600, 1420, 680, 1500, 64, kDCrate);              // A site, CT side
        prop(1300, 1350, 1440, 1540, 96, kHContainerGreen);   // A site: the container in the corner
        prop(-230, -950, -170, -890, 40, kDCrate);            // market stalls
        prop(170, -700, 230, -640, 48, kDCrate);
        prop(-230, -500, -150, -420, 56, kDCrate);
        prop(-380, 50, -320, 110, 64, kDCrate);               // mid
        prop(300, -250, 360, -190, 48, kDCrate);
        prop(420, 180, 480, 240, 64, kDCrate);                // warehouse
        prop(420, 380, 480, 440, 48, kDCrate);
        prop(-430, 700, -370, 760, 64, kDCrate);              // CT mid
        prop(370, 900, 430, 960, 64, kDCrate);
        prop(-430, 1600, -370, 1660, 64, kDCrate);            // CT spawn
        prop(-1190, -1100, -1130, -1040, 64, kDCrate);        // the B lane
        barrel(-900, -900, -30, 0);
        barrel(-1200, 0, 2, 0);                               // the underpass
        prop(-960, -200, -910, -150, 48, kDCrate);
        prop(-1200, 1000, -1110, 1090, 64, kDCrate);          // B default box (double stack)
        prop(-1185, 1015, -1125, 1075, 44, kDCrate, 64);
        prop(-1550, 780, -1450, 980, 40, kDCar);              // B: a car
        prop(-1530, 810, -1470, 950, 24, kDCarTop, 40);
        prop(-800, 1240, -740, 1300, 64, kDCrate);            // B site, by the CT path
        prop(-1000, 780, -940, 840, 48, kDCrate);
        prop(-780, 500, -730, 550, 48, kDCrate);              // the alley
    } else {
    prop(-500, -800, -440, -740, 64, kDCrate);    // T spawn crates
    prop(-60, -1000, 20, -920, 64, kDCrate);
    prop(1050, 450, 1150, 650, 96, kDBlue);       // long: the blue container outside the doors
    prop(1560, 200, 1640, 280, 48, kDCrate);      // pit box
    anchored(60, 1360, -80, -40, 0, 40, 52, kDCrate);  // xbox, against the catwalk ledge (jump on it)
    prop(1200, 2650, 1290, 2740, 64, kDCrate);    // A default box (double stack)
    prop(1215, 2665, 1275, 2725, 48, kDCrate, 64);
    prop(1550, 2500, 1610, 2560, 48, kDCrate);    // A site box near the ramp
    prop(820, 2950, 900, 3030, 64, kDCrate);      // A site, CT side
    prop(-1940, 2500, -1840, 2580, 64, kDCrate);  // B default box (double stack)
    prop(-1925, 2510, -1855, 2570, 44, kDCrate, 64);
    prop(-1700, 2900, -1550, 3000, 56, 0x8a3b32); // B car
    prop(-2280, 2200, -2200, 2280, 64, kDCrate);  // B site, by the wall
    prop(-500, 2100, -440, 2160, 64, kDCrate);    // CT mid
    // Cover all over the map, mostly against walls (anchored to an area's edge, so it stays against the
    // wall at every map size). Real sizes: crates 48-64, barrels 28, cars 100 x 200.
    car(-900, -1050, 16, -60, 116, 60);                    // T spawn: car by the west wall
    barrel(-250, -1100, 0, 2); barrel(-250, -1100, 30, 2);
    barrel(-900, -500, 14, -44);
    anchored(200, -500, -72, -72, -8, -8, 64, kDCrate);    // T spawn, north-east corner
    barrel(420, 100, 2, -14);
    anchored(-900, -880, -64, 2, 0, 58, 56, kDCrate);      // outside tunnels, by T spawn
    barrel(-2150, 200, 2, 0); barrel(-2150, 200, 2, 32);
    anchored(-1820, 400, -56, 0, 0, 56, 56, kDCrate);      // outside tunnels, east wall
    anchored(20, -300, -64, -32, 0, 32, 64, kDCrate);      // top mid, east wall
    barrel(-420, 100, 2, 0);
    barrel(-300, 700, 2, -14);                              // mid, west wall
    anchored(-300, 1500, 0, -28, 48, 28, 48, kDCrate);
    anchored(900, 820, 2, -62, 62, -2, 64, kDCrate);       // long corner, north-west
    anchored(1290, 1200, 0, -32, 60, 32, 64, kDCrate);     // long A, west wall
    anchored(1290, 1200, 6, -26, 54, 26, 44, kDCrate, 64);
    barrel(1800, 1100, -30, 0); barrel(1800, 1100, -30, 30);
    car(1800, 1800, -100, -110, -4, 90);                   // long A: the car near the top
    barrel(1320, 2300, 2, 0);                               // A ramp
    anchored(750, 2450, 2, 2, 66, 66, 64, kDCrate);        // A site, short corner
    anchored(1800, 2700, -40, 0, 0, 140, 36, kDStone);     // A site, stone bench by the east wall
    barrel(1620, 2950, -32, -32);                           // below goose
    anchored(750, 2350, -50, 2, 0, 50, 48, kDCrate);       // short, by the way onto the site
    anchored(-450, 3150, 2, -66, 66, -2, 64, kDCrate);     // CT spawn, north-west
    barrel(120, 2400, -30, 2);
    barrel(120, 1912, -30, 4);                              // CT mid, east end
    anchored(-700, 2350, -64, -50, 0, -2, 56, kDCrate);    // mid to B, by CT mid
    anchored(-1420, 1950, -66, 2, -2, 66, 64, kDCrate);    // B site, south-east corner
    barrel(-1420, 2900, -30, 0); barrel(-1420, 2900, -30, 30);
    anchored(-2300, 2600, 2, 0, 58, 56, 56, kDCrate);      // B site, west wall
    prop(-1640, 2010, -1560, 2090, 64, kDCrate);          // B: a stack beside the doors
    prop(-1630, 2020, -1570, 2080, 40, kDCrate, 64);
    anchored(-2300, 3150, 2, -58, 58, -2, 56, kDCrate);    // back plat
    anchored(-2200, 1300, 0, 0, 48, 48, 48, kDCrate);      // upper tunnels
    barrel(-1200, 1240, 0, -30);                            // lower tunnels
    }
    {
        // Doorways (long, B and mid doors): a wooden frame at both ends (a jamb up each side, a lintel under
        // the roof) with the door leaves hung on it, so no door floats and no gap shows round one. Everything
        // sits on the doorway's own grid cells, so it fits at every map size. Wood: wallbangable.
        struct Cells { int i0, j0, i1, j1; };
        auto cellsOf = [&](const char* name) {
            Cells c{m.w, m.h, -1, -1};
            for (int j = 0; j < m.h; ++j)
                for (int i = 0; i < m.w; ++i) {
                    const int a = m.area[size_t(m.index(i, j))];
                    if (a < 0 || std::strcmp(td.areas[a].name, name) != 0) continue;
                    c.i0 = std::min(c.i0, i); c.i1 = std::max(c.i1, i);
                    c.j0 = std::min(c.j0, j); c.j1 = std::max(c.j1, j);
                }
            return c;
        };
        const float jamb = 4, depth = 10, beam = 10, leafT = 6;
        // A box by passage coordinates: p along the doorway, a across it.
        auto wood = [&](bool alongX, float p0, float p1, float a0, float a1, float z0, float z1) {
            Box b = alongX ? Box{{p0, a0, z0}, {p1, a1, z1}, kDWood} : Box{{a0, p0, z0}, {a1, p1, z1}, kDWood};
            b.material = kMatWood;
            return b;
        };
        // A leaf swung open flat against the wall, shortened where it would run into a prop.
        auto openLeaf = [&](Box b, bool alongX, bool growsUp) {
            auto overlaps = [&](const Box& s) {
                const float e = 0.5f;
                return s.mins.x < b.maxs.x - e && s.maxs.x > b.mins.x + e && s.mins.y < b.maxs.y - e &&
                       s.maxs.y > b.mins.y + e && s.mins.z < b.maxs.z - e && s.maxs.z > b.mins.z + e;
            };
            for (int tries = 0; tries < 40 && std::any_of(w.solids.begin(), w.solids.end(), overlaps); ++tries) {
                float& end = growsUp ? (alongX ? b.maxs.y : b.maxs.x) : (alongX ? b.mins.y : b.mins.x);
                end += growsUp ? -4.0f : 4.0f;
            }
            w.solids.push_back(b);
        };
        // The frame at both ends of a doorway; returns its opening (a0..a1 across, p0..p1 along), floor and
        // the lintel's underside.
        struct Opening { float p0, p1, a0, a1, z, top; };
        auto frame = [&](const Cells& c, bool alongX) {
            Opening o;
            o.p0 = alongX ? X(c.i0) : Y(c.j0);
            o.p1 = alongX ? X(c.i1 + 1) : Y(c.j1 + 1);
            o.a0 = alongX ? Y(c.j0) : X(c.i0);
            o.a1 = alongX ? Y(c.j1 + 1) : X(c.i1 + 1);
            const size_t mid = size_t(m.index((c.i0 + c.i1) / 2, (c.j0 + c.j1) / 2));
            o.z = m.floor[mid];
            const float ceil = m.ceiling[mid];
            o.top = ceil - beam;
            for (float e : {o.p0, o.p1 - depth}) {
                w.solids.push_back(wood(alongX, e, e + depth, o.a0, o.a0 + jamb, o.z, ceil));
                w.solids.push_back(wood(alongX, e, e + depth, o.a1 - jamb, o.a1, o.z, ceil));
                w.solids.push_back(wood(alongX, e, e + depth, o.a0 + jamb, o.a1 - jamb, o.top, ceil));
            }
            return o;
        };
        const Cells longCells = cellsOf("LONG DOORS"), bCells = cellsOf("B DOORS"), midCells = cellsOf("MID DOORS");
        if (longCells.i1 >= 0) {
            // Long doors: both leaves swung out into long A, flat against the wall either side.
            const Opening o = frame(longCells, true);
            const float leaf = (o.a1 - o.a0) * 0.5f - jamb, h = o.top - 2;
            openLeaf(wood(true, o.p1, o.p1 + leafT, o.a0 + jamb - leaf, o.a0 + jamb, o.z, h), true, false);
            openLeaf(wood(true, o.p1, o.p1 + leafT, o.a1 - jamb, o.a1 - jamb + leaf, o.z, h), true, true);
        }
        if (bCells.i1 >= 0) {
            // B doors: one wide leaf swung into the site against the wall south of the doors (B window is north).
            const Opening o = frame(bCells, true);
            const float leaf = (o.a1 - o.a0) - 2 * jamb;
            openLeaf(wood(true, o.p0 - leafT, o.p0, o.a0 + jamb - leaf, o.a0 + jamb, o.z, o.top - 2), true, false);
        }
        if (midCells.i1 >= 0) {
            // Mid doors, mostly shut like Dust2's: a leaf from each jamb across the doorway, a gap in the middle
            // you can walk (and shoot) through. The gap sits on the doorway's middle grid cell, so the bots' nav
            // (and you) always fit through it at every map size. The leaves hang at the CT end, up to the lintel.
            const Opening o = frame(midCells, false);
            const float mid = X((midCells.i0 + midCells.i1) / 2) + m.cell * 0.5f, gap = 22.0f;
            if (mid - gap > o.a0 + jamb) w.solids.push_back(wood(false, o.p1 - 8, o.p1, o.a0 + jamb, mid - gap, o.z, o.top));
            if (mid + gap < o.a1 - jamb) w.solids.push_back(wood(false, o.p1 - 8, o.p1, mid + gap, o.a1 - jamb, o.z, o.top));
        }
    }
    if (g_town == 1) {
        arch(-250, -365, 250, -335, 176);  // the market into mid
        arch(-450, 1235, 450, 1265, 190);  // the CT ramp into CT spawn
    } else {
        arch(-300, 280, 20, 320, 176);    // top mid into mid
        arch(150, 2330, 330, 2370, 176);  // top of the short stairs
        arch(-450, 2335, 120, 2365, 196); // CT mid into CT spawn
    }

    w.buildIndex();
    return w;
}

const std::vector<PeekSpot>& townPeekSpots() {
    static std::vector<PeekSpot> spots;
    static float builtFor = -1;
    static int builtTown = -1;
    if (builtFor != townScale() || builtTown != g_town) {
        builtFor = townScale();
        builtTown = g_town;
        const MapGrid& m = townGrid();
        const float sc = builtFor;
        // rel(): a fixed offset from something that scales (a box's centre); at(): a point that scales.
        auto rel = [&](float ax, float ay, float dx, float dy) {
            Vec3 p{ax * sc + dx, ay * sc + dy, 0};
            p.z = m.floorAt(p.x, p.y);
            return p;
        };
        auto at = [&](float x, float y) { return rel(x, y, 0, 0); };
        if (g_town == 1) {
            spots = {
                {at(960, -380), at(1100, -380)},     // behind the blue container -> down the docks
                {at(-200, 620), at(0, 620)},         // CT mid -> through mid doors
                {at(-1155, 1130), at(-1050, 1130)},  // B default box -> the entrance
                {at(945, 1330), at(1050, 1330)},     // A default box -> the ramp
                {at(-760, 800), at(-700, 700)},      // B, the alley exit
                {at(720, 1050), at(650, 1000)},      // A, the top of short
            };
            return spots;
        }
        spots = {
            {rel(1100, 550, 90, 40), rel(1100, 550, 90, -150)},       // long corner, beside the blue container -> doors
            {at(-320, 1990), at(-180, 1990)},                          // CT mid, through mid doors
            {rel(-1890, 2540, 10, 100), rel(-1890, 2540, 120, 100)},  // B default box -> tunnel exit
            {rel(1245, 2695, -5, 105), rel(1245, 2695, 105, 105)},    // A default box -> A ramp / long
            {at(-1520, 2420), at(-1520, 2210)},                        // B site -> through B doors
            {at(600, 2500), at(350, 2500)},                            // short -> down the catwalk stairs
        };
    }
    return spots;
}

World buildLab() {
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
