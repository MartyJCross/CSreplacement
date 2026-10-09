#include "replay.h"
#include <algorithm>
#include <cmath>

namespace {

float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return a + d * t;
}

}  // namespace

void Replay::clear() {
    head_ = count_ = 0;
    shots_.clear();
    mark_ = -1;
}

ReplayFrame& Replay::next(double t) {
    const size_t cap = size_t(kRate * kSeconds);
    if (ring_.size() < cap) ring_.resize(cap);
    ReplayFrame* f;
    if (count_ < cap) {
        f = &ring_[(head_ + count_) % cap];
        ++count_;
    } else {  // full: the oldest goes
        f = &ring_[head_];
        head_ = (head_ + 1) % cap;
    }
    f->t = t;
    f->agents.clear();  // (keeps their capacity)
    f->smokes.clear();
    // Shots older than what's kept go too.
    const double oldest = at(0).t;
    if (!shots_.empty() && shots_.front().t < oldest - 1.0)
        shots_.erase(shots_.begin(), std::find_if(shots_.begin(), shots_.end(), [&](const ReplayShot& s) { return s.t >= oldest; }));
    return *f;
}

void Replay::shot(const ReplayShot& s) {
    if (shots_.size() < 20000) shots_.push_back(s);
}

double Replay::start() const { return count_ ? at(0).t : 0.0; }
double Replay::end() const { return count_ ? at(count_ - 1).t : 0.0; }

bool Replay::sample(double t, ReplayFrame& out) const {
    if (count_ == 0) return false;
    t = std::clamp(t, start(), end());
    // The last frame at or before t (frames are in time order).
    size_t lo = 0, hi = count_ - 1;
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (at(mid).t <= t) lo = mid;
        else hi = mid - 1;
    }
    const ReplayFrame& a = at(lo);
    const ReplayFrame& b = at(std::min(lo + 1, count_ - 1));
    const float k = b.t > a.t ? float((t - a.t) / (b.t - a.t)) : 0.0f;
    out.t = t;
    out.smokes = a.smokes;
    out.agents.resize(a.agents.size());
    for (size_t i = 0; i < a.agents.size(); ++i) {
        ReplayAgent r = a.agents[i];
        if (i < b.agents.size() && b.agents[i].alive == r.alive) {  // (no sliding across a respawn)
            const ReplayAgent& n = b.agents[i];
            r.pos = r.pos + (n.pos - r.pos) * k;
            r.yaw = lerpAngle(r.yaw, n.yaw, k);
            r.pitch += (n.pitch - r.pitch) * k;
            r.crouch += (n.crouch - r.crouch) * k;
            r.deadFor += (n.deadFor - r.deadFor) * k;
            r.stepDist += (n.stepDist - r.stepDist) * k;
        }
        out.agents[i] = r;
    }
    return true;
}

void Replay::shotsBetween(double t0, double t1, std::vector<ReplayShot>& out) const {
    out.clear();
    for (const ReplayShot& s : shots_)
        if (s.t > t0 && s.t <= t1) out.push_back(s);
}
