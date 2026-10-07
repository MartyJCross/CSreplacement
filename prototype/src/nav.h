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
    void build(const MapGrid& grid, const World& world);
    bool ready() const { return grid_ != nullptr; }
    // Shortest route from `from` to `to` as cell centres on the floor (first = start cell).
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const;
    // True if a standing player fits in the cell under p.
    bool standable(const Vec3& p) const;

private:
    bool clear(int i, int j) const;
    bool canStep(int a, int b, int c, int d) const;  // walk from cell (a, b) into neighbour (c, d)
    const MapGrid* grid_ = nullptr;
    std::vector<uint8_t> clear_, edge_;  // edge_: next to a wall or ledge (paths keep off it)
    // Scratch space reused between searches, so pathfinding doesn't allocate after warm-up.
    mutable std::vector<float> dist_;
    mutable std::vector<int> prev_;
    mutable std::vector<std::pair<float, int>> heap_;
};

// Moves `pos` along `path` (heading for path[next]) by `distance` units. Height follows the path
// smoothly between cell centres. Returns true once the last point is reached.
bool followPath(Vec3& pos, const std::vector<Vec3>& path, size_t& next, float distance);
