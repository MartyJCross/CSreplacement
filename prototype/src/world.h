// Static world made of axis-aligned boxes, plus swept-box and ray traces against it.
#pragma once
#include <cstdint>
#include <vector>
#include "vecmath.h"

enum Material : uint8_t { kMatStone = 0, kMatWood = 1, kMatMetal = 2, kMatPlain = 3 };  // plain: decor only, no pattern

// A box, or a ramp (wedge): the same footprint, but its top slopes from lowZ at one edge up to
// maxs.z at the opposite edge.
enum Slope : uint8_t { kFlat = 0, kRisePosX, kRiseNegX, kRisePosY, kRiseNegY };

struct Box {
    Vec3 mins, maxs;
    uint32_t color;  // 0xRRGGBB
    uint8_t material = kMatStone;  // footstep sound + how easily bullets go through
    uint8_t slope = kFlat;         // which way the top rises (ramps)
    float lowZ = 0;                // ramps: top height at the low edge
};

// The (outward) planes of a ramp, as n.p <= d. Six of them: four sides, bottom, and the slope.
struct Plane { Vec3 n; float d; };
void rampPlanes(const Box& b, Plane out[6]);

struct TraceResult {
    float fraction = 1.0f;  // 0..1 of the way from start to end
    Vec3 endpos;
    Vec3 normal;            // surface normal at impact (valid if fraction < 1)
    bool startSolid = false;
    int box = -1;           // index into World::solids that was hit
};

struct World {
    std::vector<Box> solids;
    // Scenery that is drawn but never collides (wall trim, windows, beams, awnings). Keep it thin, against
    // walls or above head height, so it can never hide a player or stop a shot you'd expect to land.
    std::vector<Box> decor;

    // Sweep an AABB (given by mins/maxs relative to origin) from start to end.
    TraceResult traceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) const;
    // Infinitely thin ray.
    TraceResult traceRay(const Vec3& start, const Vec3& end) const { return traceBox(start, end, {}, {}); }
    // True if the AABB at origin overlaps no solid.
    bool boxFits(const Vec3& origin, const Vec3& mins, const Vec3& maxs) const;

    // Optional 2D broadphase for big maps (call once after filling `solids`). Without it every
    // trace tests every box, which is fine for the small Lab.
    void buildIndex(float cellSize = 256.0f);
    bool indexed() const { return idxW_ > 0; }

private:
    template <class Fn> void forCandidates(float x0, float y0, float x1, float y1, Fn&& fn) const;
    float idxX0_ = 0, idxY0_ = 0, idxCell_ = 0;
    int idxW_ = 0, idxH_ = 0;
    std::vector<uint32_t> idxStart_, idxItems_;  // per-cell ranges into idxItems_ (box indices)
    mutable std::vector<uint32_t> stamp_;        // per-box "already tested in this query" marks
    mutable uint32_t stampId_ = 0;
};

// Ray vs AABB slab test. Returns true and entry t in [0, maxT] on hit.
bool rayHitsBox(const Vec3& start, const Vec3& dir, float maxT, const Vec3& bmin, const Vec3& bmax,
                float& tHit, Vec3* normalOut = nullptr);

// Builds the Lab: the greybox test map (range, spray wall, crates, stairs, KZ course).
World buildLab();

// Dust2 at real scale (~4200 x 4250 units, +y = north). Built from named floor areas on a 32-unit
// grid (flat areas, ramps, roofed tunnels); everything else is solid wall. See world.cpp.
World buildDust();
struct PeekSpot { Vec3 cover, peek; };  // bot hides at `cover`, steps out to `peek`
const std::vector<PeekSpot>& dustPeekSpots();

struct MapGrid {
    float x0 = 0, y0 = 0, cell = 32;  // world position of cell (0,0)'s min corner, cell size
    int w = 0, h = 0;
    std::vector<float> floor;    // floor height per cell (kNoFloor = solid wall)
    std::vector<float> ceiling;  // absolute ceiling height of roofed cells, kOpenSky otherwise
    std::vector<int> area;       // index of the named area the cell belongs to, -1 for walls
    static constexpr float kNoFloor = -1e9f, kOpenSky = 1e9f;
    int index(int i, int j) const { return j * w + i; }
    bool walkable(int i, int j) const { return i >= 0 && j >= 0 && i < w && j < h && floor[size_t(index(i, j))] > kNoFloor; }
    bool cellAt(float x, float y, int& i, int& j) const;
    float floorAt(float x, float y) const;  // kNoFloor inside walls / outside the map
    Vec3 center(int i, int j) const { return {x0 + (float(i) + 0.5f) * cell, y0 + (float(j) + 0.5f) * cell, floor[size_t(index(i, j))]}; }
};
// Dust's size relative to real Dust2 (default 0.6, range 0.5..1). Heights scale too, so slopes stay
// walkable; headroom, crates and doorways (at least 96 wide) keep their real size. Returns true if it
// changed: rebuild the world (buildDust) and anything built from the grid.
bool setDustScale(float scale);
float dustScale();
const MapGrid& dustGrid();
const char* dustCallout(const Vec3& p);  // area name under p ("" in walls)
struct MapSpawn { Vec3 pos; float yaw; };
MapSpawn dustSpawn();
// A point given in real-Dust2 coordinates, at the map's current scale, on the floor.
Vec3 dustPoint(float x, float y);
// Competitive: five spawn spots per side (0 = T, 1 = CT), at the current scale.
std::vector<Vec3> dustTeamSpawns(int side);

// Retakes: per bombsite, spots for the bots to hold (each facing a look-at point) and entries you
// retake from. All in real-Dust2 coordinates: place them with dustPoint().
struct RetakeSpot { float x, y, lookX, lookY; };
struct RetakeSite {
    const char* name;
    float bombX, bombY;  // where the bomb is planted
    std::vector<RetakeSpot> holds, entries;
};
const std::vector<RetakeSite>& dustRetakeSites();

// Prefire practice: where you start (looking the way to go) and the spots bots hold along the route,
// each looking back at where you come from, in the order you meet them. Real-Dust2 coordinates.
struct PrefireRoute {
    const char* name;
    RetakeSpot start;
    std::vector<RetakeSpot> bots;
};
const std::vector<PrefireRoute>& dustPrefireRoutes();

// KZ / bhop course (behind the spray wall): start pad, lava floor, pads, end pad.
constexpr float kKzMinY = -880.0f, kKzMaxY = -580.0f;
constexpr float kKzStartMinX = 720.0f, kKzStartMaxX = 848.0f;  // start pad
constexpr float kKzLavaMinX = 848.0f, kKzLavaMaxX = 2352.0f;   // touching the floor here = reset
constexpr float kKzEndMinX = 2352.0f, kKzEndMaxX = 2540.0f;    // end pad
constexpr float kKzPadHeight = 24.0f;
