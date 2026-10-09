// Replays and the killcam: what everyone was doing, 64 times a second, for the last few minutes (a ring
// buffer), plus every shot. Pure data (no SDL): the game records into it and draws from it; sim_tests checks
// the recording and the playback.
#pragma once
#include <cstdint>
#include <vector>
#include "vecmath.h"

// One player (a bot, a remote player, or you) at one moment: what the game needs to draw them.
struct ReplayAgent {
    Vec3 pos, hitDir;
    float yaw = 0, pitch = 0, crouch = 0, deadFor = 0, stepDist = 0;
    uint8_t weapon = 0;
    bool alive = true, friendly = false, lostHelmet = false;
};

// One moment. agents: the dummies in their order, then you (the last one).
struct ReplayFrame {
    double t = 0;
    std::vector<ReplayAgent> agents;
    struct Smoke { Vec3 pos; double start; };
    std::vector<Smoke> smokes;
};

// A shot: tracer and sound in the replay. shooter: an agent index (you = the last agent).
struct ReplayShot { double t; Vec3 from, to; int shooter; uint8_t weapon; };

class Replay {
public:
    static constexpr double kRate = 64.0;       // frames a second
    static constexpr double kSeconds = 150.0;   // how far back it keeps

    void clear();
    // A frame (its time must be later than the last one's). Reuses old frames' memory: no allocations once full.
    ReplayFrame& next(double t);
    void shot(const ReplayShot& s);
    // The round (or match) started here: the viewer starts at the latest mark.
    void mark(double t) { mark_ = t; }
    double markTime() const { return mark_; }

    bool empty() const { return count_ == 0; }
    double start() const;
    double end() const;
    // Everyone at time t (clamped to what's recorded), interpolated between the two frames round it. Returns
    // false if nothing is recorded. `out`'s memory is reused.
    bool sample(double t, ReplayFrame& out) const;
    // The shots fired in (t0, t1].
    void shotsBetween(double t0, double t1, std::vector<ReplayShot>& out) const;
    size_t frames() const { return count_; }

private:
    std::vector<ReplayFrame> ring_;
    size_t head_ = 0, count_ = 0;  // the oldest frame is ring_[head_]
    std::vector<ReplayShot> shots_;
    double mark_ = -1;
    const ReplayFrame& at(size_t k) const { return ring_[(head_ + k) % ring_.size()]; }
};
