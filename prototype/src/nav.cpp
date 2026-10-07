#include "nav.h"
#include <algorithm>
#include <cmath>
#include "movement.h"

void NavGrid::build(const MapGrid& grid, const World& world) {
    grid_ = &grid;
    const MapGrid& m = grid;
    size_t n = size_t(m.w * m.h);
    clear_.assign(n, 0);
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i)
            clear_[size_t(m.index(i, j))] =
                m.walkable(i, j) && world.boxFits(m.center(i, j) + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false));
    edge_.assign(n, 0);
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i) {
            if (!clear(i, j)) continue;
            float z = m.floor[size_t(m.index(i, j))];
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di)
                    if (!clear(i + di, j + dj) || std::fabs(m.floor[size_t(m.index(i + di, j + dj))] - z) > 20.0f)
                        edge_[size_t(m.index(i, j))] = 1;
        }
    dist_.assign(n, 0.0f);
    prev_.assign(n, -1);
    heap_.reserve(n);
}

bool NavGrid::clear(int i, int j) const {
    const MapGrid& m = *grid_;
    return i >= 0 && j >= 0 && i < m.w && j < m.h && clear_[size_t(m.index(i, j))];
}

bool NavGrid::canStep(int a, int b, int c, int d) const {
    if (!clear(a, b) || !clear(c, d)) return false;
    const MapGrid& m = *grid_;
    size_t p = size_t(m.index(a, b)), q = size_t(m.index(c, d));
    if (m.floor[q] - m.floor[p] > MoveParams{}.stepSize) return false;
    return std::min(m.ceiling[p], m.ceiling[q]) - std::max(m.floor[p], m.floor[q]) >= kStandHeight + 2;
}

bool NavGrid::standable(const Vec3& p) const {
    int i, j;
    return grid_ && grid_->cellAt(p.x, p.y, i, j) && clear(i, j);
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const {
    out.clear();
    if (!grid_) return false;
    const MapGrid& m = *grid_;
    int si, sj, ti, tj;
    if (!m.cellAt(from.x, from.y, si, sj) || !m.cellAt(to.x, to.y, ti, tj) || !clear(si, sj) || !clear(ti, tj))
        return false;
    std::fill(dist_.begin(), dist_.end(), 1e30f);
    std::fill(prev_.begin(), prev_.end(), -1);
    heap_.clear();
    auto cmp = [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; };
    const int start = m.index(si, sj), goal = m.index(ti, tj);
    dist_[size_t(start)] = 0;
    heap_.push_back({0.0f, start});
    bool found = false;
    while (!heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), cmp);
        std::pair<float, int> it = heap_.back();
        heap_.pop_back();
        int c = it.second, i = c % m.w, j = c / m.w;
        if (it.first > dist_[size_t(c)]) continue;
        if (c == goal) { found = true; break; }
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                if ((!di && !dj) || !canStep(i, j, i + di, j + dj)) continue;
                // Diagonals only where both straight neighbours are walkable too (no corner cutting).
                if (di && dj && !(canStep(i, j, i + di, j) && canStep(i, j, i, j + dj) &&
                                  canStep(i + di, j, i + di, j + dj) && canStep(i, j + dj, i + di, j + dj)))
                    continue;
                int nb = m.index(i + di, j + dj);
                float nd = dist_[size_t(c)] + (di && dj ? 1.4142f : 1.0f) + float(edge_[size_t(nb)]);
                if (nd < dist_[size_t(nb)]) {
                    dist_[size_t(nb)] = nd;
                    prev_[size_t(nb)] = c;
                    heap_.push_back({nd, nb});
                    std::push_heap(heap_.begin(), heap_.end(), cmp);
                }
            }
    }
    if (!found) return false;
    for (int c = goal; c >= 0; c = prev_[size_t(c)]) out.push_back(m.center(c % m.w, c / m.w));
    std::reverse(out.begin(), out.end());
    return true;
}

bool followPath(Vec3& pos, const std::vector<Vec3>& path, size_t& next, float distance) {
    while (next < path.size()) {
        const Vec3& target = path[next];
        Vec3 d{target.x - pos.x, target.y - pos.y, 0};
        float len = length(d);
        if (len <= distance) {
            pos = target;
            distance -= len;
            ++next;
            continue;
        }
        Vec3 from = next > 0 ? path[next - 1] : pos;
        pos.x += d.x / len * distance;
        pos.y += d.y / len * distance;
        float seg = length2d(target - from);
        float t = seg > 1e-3f ? std::clamp(1.0f - length2d(target - pos) / seg, 0.0f, 1.0f) : 1.0f;
        pos.z = from.z + (target.z - from.z) * t;
        return false;
    }
    return true;
}
