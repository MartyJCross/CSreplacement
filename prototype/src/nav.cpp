#include "nav.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "movement.h"

void NavGrid::build(const MapGrid& grid, const World& world, const Vec3& seed) {
    grid_ = &grid;
    const MapGrid& m = grid;
    size_t n = size_t(m.w * m.h);
    clear_.assign(n, 0);
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i)
            // Checked 20 units up: on a smooth ramp the grid's stepped height can sit a little inside the
            // slope. Props (crates are 44+ tall) and low ceilings still rule a cell out.
            clear_[size_t(m.index(i, j))] =
                m.walkable(i, j) && world.boxFits(m.center(i, j) + Vec3{0, 0, 20.0f}, hullMins(), hullMaxs(false));
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

    // Roamable = reachable from the seed (forwards) and able to walk back to it (backwards).
    roam_.assign(n, 0);
    roamCells_.clear();
    openCells_.clear();
    int si, sj;
    if (!m.cellAt(seed.x, seed.y, si, sj) || !clear(si, sj)) return;
    auto flood = [&](bool forward, std::vector<uint8_t>& seen) {
        seen.assign(n, 0);
        std::vector<int> stack{m.index(si, sj)};
        seen[size_t(stack[0])] = 1;
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            int i = c % m.w, j = c / m.w;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    if ((di && dj) || (!di && !dj)) continue;
                    int a = i + di, b = j + dj;
                    if (!clear(a, b) || seen[size_t(m.index(a, b))]) continue;
                    if (forward ? !canStep(i, j, a, b) : !canStep(a, b, i, j)) continue;
                    seen[size_t(m.index(a, b))] = 1;
                    stack.push_back(m.index(a, b));
                }
        }
    };
    std::vector<uint8_t> fwd, back;
    flood(true, fwd);
    flood(false, back);
    for (size_t c = 0; c < n; ++c) {
        roam_[c] = fwd[c] && back[c];
        if (!roam_[c]) continue;
        roamCells_.push_back(int(c));
        if (!edge_[c]) openCells_.push_back(int(c));
    }
}

bool NavGrid::roamable(const Vec3& p) const {
    int i, j;
    return grid_ && grid_->cellAt(p.x, p.y, i, j) && roam_[size_t(grid_->index(i, j))];
}

bool NavGrid::nearestRoamable(const Vec3& p, int rings, Vec3& out) const {
    if (!grid_) return false;
    const MapGrid& m = *grid_;
    int ci, cj;
    if (!m.cellAt(p.x, p.y, ci, cj)) return false;
    float best = 1e30f;
    for (int j = cj - rings; j <= cj + rings; ++j)
        for (int i = ci - rings; i <= ci + rings; ++i) {
            if (i < 0 || j < 0 || i >= m.w || j >= m.h || !roam_[size_t(m.index(i, j))]) continue;
            const Vec3 c = m.center(i, j);
            if (std::fabs(c.z - p.z) > 72.0f) continue;  // not up on a roof or down a level
            const float d = length2d(c - p);
            if (d < best) { best = d; out = c; }
        }
    return best < 1e30f;
}

Vec3 NavGrid::roamPoint(float r01, bool awayFromEdges) const {

    const std::vector<int>& cells = awayFromEdges && !openCells_.empty() ? openCells_ : roamCells_;
    if (cells.empty()) return {};
    size_t k = std::min(cells.size() - 1, size_t(std::max(0.0f, r01) * float(cells.size())));
    int c = cells[k];
    return grid_->center(c % grid_->w, c / grid_->w);
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
    // A*: the cells nearest the goal first. The estimate (straight and diagonal steps, no detours, no edge
    // cost) never overshoots, so the route is still the shortest - it just looks at far fewer cells.
    auto estimate = [&](int c) {
        const float dx = float(std::abs(c % m.w - ti)), dy = float(std::abs(c / m.w - tj));
        return std::max(dx, dy) + 0.4142f * std::min(dx, dy);
    };
    dist_[size_t(start)] = 0;
    heap_.push_back({estimate(start), start});
    bool found = false;
    while (!heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), cmp);
        std::pair<float, int> it = heap_.back();
        heap_.pop_back();
        int c = it.second, i = c % m.w, j = c / m.w;
        if (it.first > dist_[size_t(c)] + estimate(c) + 1e-3f) continue;  // (a stale entry)
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
                if (!avoid_.empty() && avoid_[size_t(nb)]) nd += 12.0f;  // round it if there's another way
                if (nd < dist_[size_t(nb)]) {
                    dist_[size_t(nb)] = nd;
                    prev_[size_t(nb)] = c;
                    heap_.push_back({nd + estimate(nb), nb});
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
        if (from.z - target.z > MoveParams{}.stepSize) {
            // A drop off a ledge: stay up until the edge (halfway between cell centres), then fall.
            float f = std::clamp((t - 0.5f) * 2.0f, 0.0f, 1.0f);
            pos.z = from.z + (target.z - from.z) * f * f;
        } else {
            pos.z = from.z + (target.z - from.z) * t;  // steps and stepped ramps: smooth
        }
        return false;
    }
    return true;
}
