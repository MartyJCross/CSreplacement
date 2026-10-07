// Bot navigation on a MapGrid (Dust): which cells a standing player fits in, and shortest walkable
// routes between them (climb at most one step, drop any height, enough headroom, around props).
#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "world.h"

class NavGrid {
public:
    // `seed` is any point on the main walkable area (e.g. a spawn): cells you can walk to from it and
    // back again are "roamable" (this leaves out spots that need a jump, like goose or back plat).
    void build(const MapGrid& grid, const World& world, const Vec3& seed);
    bool ready() const { return grid_ != nullptr; }
    // Shortest route from `from` to `to` as cell centres on the floor (first = start cell).
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const;
    // True if a standing player fits in the cell under p.
    bool standable(const Vec3& p) const;
    bool roamable(const Vec3& p) const;
    float floorAt(const Vec3& p) const { return grid_ ? grid_->floorAt(p.x, p.y) : 0.0f; }
    // A roamable cell centre picked by r01 in [0, 1), uniformly over the whole map. With
    // `awayFromEdges`, only cells not touching a wall or ledge (good spawn spots).
    Vec3 roamPoint(float r01, bool awayFromEdges) const;
    size_t roamCount() const { return roamCells_.size(); }

private:
    bool clear(int i, int j) const;
    bool canStep(int a, int b, int c, int d) const;  // walk from cell (a, b) into neighbour (c, d)
    const MapGrid* grid_ = nullptr;
    std::vector<uint8_t> clear_, edge_;  // edge_: next to a wall or ledge (paths keep off it)
    std::vector<uint8_t> roam_;          // reachable from the seed and back
    std::vector<int> roamCells_, openCells_;  // roamable cells; roamable cells away from edges
    // Scratch space reused between searches, so pathfinding doesn't allocate after warm-up.
    mutable std::vector<float> dist_;
    mutable std::vector<int> prev_;
    mutable std::vector<std::pair<float, int>> heap_;
};

// Moves `pos` along `path` (heading for path[next]) by `distance` units. Height follows the path
// smoothly between cell centres. Returns true once the last point is reached.
bool followPath(Vec3& pos, const std::vector<Vec3>& path, size_t& next, float distance);
