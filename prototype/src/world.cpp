#include "world.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

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
            fits = false;
    });
    return fits;
}

// ---------------------------------------------------------------------------------------------
// Dust2 at real scale. Coordinates are Source units, +x = east, +y = north, 250 u/s = knife run.
// The layout is described as named floor areas on a 32-unit grid; later areas override earlier
// ones. Any cell no area covers is solid wall, with its height taken from the nearest floor.
// Ramps are stepped per cell (at most 16 units a step, so you walk up them) until real slopes exist.
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
                   kDBlue = 0x3f6f9a, kDRoof = 0x6b5a42;

const DustArea kDustAreas[] = {
    // T side.
    {"T SPAWN", -900, -1100, 200, -500, 64, 64, 0, kDSand, 0},
    {"OUTSIDE LONG", 200, -900, 650, -500, 64, 48, 'x', kDSand, 0},
    {"OUTSIDE LONG", 400, -500, 650, 200, 48, 0, 'y', kDSand, 0},
    {"OUTSIDE LONG", 400, 200, 650, 460, 0, 0, 0, kDSand, 0},
    {"OUTSIDE TUNNELS", -1700, -900, -900, -500, 64, 64, 0, kDSand, 0},
    {"OUTSIDE TUNNELS", -2150, -900, -1700, 700, 64, 0, 'y', kDSand, 0},
    {"TOP MID", -450, -500, 50, 300, 64, 0, 'y', kDSand, 0},
    // Long A.
    {"LONG DOORS", 650, 300, 900, 460, 0, 0, 0, kDSandDark, 144},
    {"LONG A", 900, 150, 1500, 850, 0, 0, 0, kDSand, 0},
    {"LONG A", 1250, 850, 1850, 2050, 0, 0, 0, kDSand, 0},
    {"PIT", 1500, 150, 1850, 600, -160, -160, 0, kDSandDark, 0},
    {"PIT", 1650, 600, 1850, 950, -160, 0, 'y', kDSandDark, 0},
    {"A RAMP", 1300, 2050, 1850, 2450, 0, 96, 'y', kDSandLight, 0},
    {"A SITE", 750, 2450, 1850, 3150, 96, 96, 0, kDSite, 0},
    {"GOOSE", 1650, 2950, 1850, 3150, 144, 144, 0, kDSandLight, 0},
    // Mid, catwalk and short A.
    {"MID", -400, 300, 150, 1880, 0, -64, 'y', kDSand, 0},
    {"CATWALK", 150, 1000, 450, 1250, -28, 32, 'y', kDSandLight, 0},
    {"CATWALK", 150, 1250, 450, 2050, 32, 32, 0, kDSandLight, 0},
    {"SHORT", 180, 2050, 450, 2350, 32, 96, 'y', kDSandLight, 0},
    {"SHORT", 150, 2350, 750, 2650, 96, 96, 0, kDSite, 0},
    {"MID DOORS", -240, 1880, -120, 1912, -64, -64, 0, kDSandDark, 128},
    // CT side.
    {"CT MID", -700, 1912, 150, 2350, -64, -64, 0, kDSand, 0},
    {"CT SPAWN", -450, 2350, 120, 3150, -64, -64, 0, kDSand, 0},
    {"CT RAMP", 120, 2800, 750, 3150, -64, 96, 'x', kDSandLight, 0},
    {"MID TO B", -1350, 2050, -700, 2350, 32, -64, 'x', kDSand, 0},
    {"B DOORS", -1420, 2140, -1350, 2280, 32, 32, 0, kDSandDark, 128},
    // B.
    {"B SITE", -2300, 1950, -1420, 3150, 32, 32, 0, kDSite, 0},
    {"BACK PLAT", -2300, 2850, -2000, 3150, 80, 80, 0, kDSandLight, 0},
    {"UPPER TUNNELS", -2250, 700, -1850, 1500, 0, 0, 0, kDTunnel, 128},
    {"UPPER TUNNELS", -2250, 1500, -1950, 1950, 0, 32, 'y', kDTunnel, 128},
    {"LOWER TUNNELS", -1850, 1050, -1550, 1250, 0, -128, 'x', kDTunnel, 128},
    {"LOWER TUNNELS", -1550, 1050, -750, 1250, -128, -128, 0, kDTunnel, 128},
    {"LOWER TUNNELS", -750, 1050, -400, 1250, -128, -36, 'x', kDTunnel, 128},
};
constexpr int kDustAreaCount = int(sizeof(kDustAreas) / sizeof(kDustAreas[0]));
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

float g_dustScale = 0.6f;
bool g_dustBuilt = false;
MapGrid g_dustGrid;

// An area's extent along one axis at the current scale. It never shrinks below min(original, 96)
// units, so doorways stay a doorway (and thin areas don't fall between grid cells).
void scaledSpan(float a0, float a1, float& b0, float& b1) {
    float c = (a0 + a1) * 0.5f * g_dustScale;
    float len = std::max((a1 - a0) * g_dustScale, std::min(a1 - a0, 96.0f));
    b0 = c - len * 0.5f;
    b1 = c + len * 0.5f;
}

void buildDustGrid() {
    const float s = g_dustScale;
    MapGrid m;
    m.cell = 32;
    m.x0 = std::floor(-2400.0f * s / m.cell) * m.cell - 64;
    m.y0 = std::floor(-1216.0f * s / m.cell) * m.cell - 64;
    m.w = int(std::ceil((1952.0f * s - m.x0) / m.cell)) + 2;
    m.h = int(std::ceil((3264.0f * s - m.y0) / m.cell)) + 2;
    size_t n = size_t(m.w * m.h);
    m.floor.assign(n, MapGrid::kNoFloor);
    m.ceiling.assign(n, MapGrid::kOpenSky);
    m.area.assign(n, -1);
    for (int a = 0; a < kDustAreaCount; ++a) {
        const DustArea& d = kDustAreas[a];
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

float dustScale() { return g_dustScale; }

const MapGrid& dustGrid() {
    if (!g_dustBuilt) buildDustGrid();
    return g_dustGrid;
}

const char* dustCallout(const Vec3& p) {
    const MapGrid& m = dustGrid();
    int i, j;
    if (!m.cellAt(p.x, p.y, i, j)) return "";
    int a = m.area[size_t(m.index(i, j))];
    return a >= 0 ? kDustAreas[a].name : "";
}

MapSpawn dustSpawn() {
    float x = -350.0f * g_dustScale, y = -800.0f * g_dustScale;
    return {{x, y, dustGrid().floorAt(x, y)}, 90.0f};
}

World buildDust() {
    const MapGrid& m = dustGrid();
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

    // Floors (one key per height + colour).
    for (size_t c = 0; c < n; ++c)
        if (m.area[c] >= 0) key[c] = (zKey(m.floor[c]) << 24) | int64_t(kDustAreas[m.area[c]].color);
    mergeRects(m.w, m.h, key, [&](int i0, int j0, int i1, int j1, int64_t k) {
        rect(i0, j0, i1, j1, kDustBottom, float((k >> 24) - 65536), uint32_t(k & 0xFFFFFF));
    });

    // Walls, in three slightly different stone shades so building blocks read apart.
    const uint32_t shades[3] = {kDStone, 0xd2bd94, 0xc7b089};
    for (size_t c = 0; c < n; ++c) key[c] = m.area[c] < 0 ? zKey(wallTop(nearZ[c])) : -1;
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

    // Props, standing on the floor under their centre. Crates keep their real size wherever the map's
    // scale puts them; `anchored` ones are placed relative to a point that scales (a wall edge).
    const float sc = dustScale();
    auto prop = [&](float x0, float y0, float x1, float y1, float h, uint32_t color, float lift = 0) {
        float cx = (x0 + x1) * 0.5f * sc, cy = (y0 + y1) * 0.5f * sc, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
        float z = m.floorAt(cx, cy) + lift;
        w.solids.push_back({{cx - hx, cy - hy, z}, {cx + hx, cy + hy, z + h}, color});
    };
    auto anchored = [&](float ax, float ay, float dx0, float dy0, float dx1, float dy1, float h, uint32_t color) {
        float x = ax * sc, y = ay * sc;
        float z = m.floorAt(x + (dx0 + dx1) * 0.5f, y + (dy0 + dy1) * 0.5f);
        w.solids.push_back({{x + dx0, y + dy0, z}, {x + dx1, y + dy1, z + h}, color});
    };
    prop(-500, -800, -440, -740, 64, kDCrate);    // T spawn crates
    prop(-60, -1000, 20, -920, 64, kDCrate);
    prop(1050, 450, 1150, 650, 96, kDBlue);       // long: the blue container outside the doors
    prop(1560, 200, 1640, 280, 48, kDCrate);      // pit box
    anchored(150, 1360, -80, -40, 0, 40, 52, kDCrate);  // xbox, against the catwalk ledge (jump on it)
    prop(1200, 2650, 1290, 2740, 64, kDCrate);    // A default box (double stack)
    prop(1215, 2665, 1275, 2725, 48, kDCrate, 64);
    prop(1550, 2500, 1610, 2560, 48, kDCrate);    // A site box near the ramp
    prop(820, 2950, 900, 3030, 64, kDCrate);      // A site, CT side
    prop(-1940, 2500, -1840, 2580, 64, kDCrate);  // B default box (double stack)
    prop(-1925, 2510, -1855, 2570, 44, kDCrate, 64);
    prop(-1700, 2900, -1550, 3000, 56, 0x8a3b32); // B car
    prop(-2280, 2200, -2200, 2280, 64, kDCrate);  // B site, by the wall
    prop(-500, 2100, -440, 2160, 64, kDCrate);    // CT mid
    {
        // Door leaves stand at the edge of their (scaled, at least 96 wide) doorway.
        float y0, y1;
        scaledSpan(2140, 2280, y0, y1);
        w.solids.push_back({{-1420.0f * sc - 10, y1, 32 * sc}, {-1350.0f * sc + 10, y1 + 16, 32 * sc + 128}, kDWood});
        scaledSpan(300, 460, y0, y1);
        w.solids.push_back({{900.0f * sc - 8, y0, 0}, {900.0f * sc, y0 + 80, 128}, kDWood});
    }

    w.buildIndex();
    return w;
}

const std::vector<PeekSpot>& dustPeekSpots() {
    static std::vector<PeekSpot> spots;
    static float builtFor = -1;
    if (builtFor != dustScale()) {
        builtFor = dustScale();
        const MapGrid& m = dustGrid();
        const float sc = builtFor;
        // rel(): a fixed offset from something that scales (a box's centre); at(): a point that scales.
        auto rel = [&](float ax, float ay, float dx, float dy) {
            Vec3 p{ax * sc + dx, ay * sc + dy, 0};
            p.z = m.floorAt(p.x, p.y);
            return p;
        };
        auto at = [&](float x, float y) { return rel(x, y, 0, 0); };
        spots = {
            {rel(1100, 550, 120, 170), rel(1100, 550, 120, -150)},    // long corner, behind the blue container -> doors
            {at(-320, 1990), at(-180, 1990)},                          // CT mid, through mid doors
            {rel(-1890, 2540, 10, 100), rel(-1890, 2540, 120, 100)},  // B default box -> tunnel exit
            {rel(1245, 2695, -5, 105), rel(1245, 2695, 105, 105)},    // A default box -> A ramp / long
            {at(-1520, 2420), at(-1520, 2210)},                        // B site -> through B doors
            {at(600, 2500), at(350, 2500)},                            // short -> down the catwalk stairs
        };
    }
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
