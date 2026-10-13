// Replays and the killcam: what everyone was doing, 64 times a second, for the last few minutes (a ring
// buffer), plus every shot. Pure data (no SDL): the game records into it and draws from it; sim_tests checks
// the recording and the playback.
#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "vecmath.h"

// One player (a bot, a remote player, or you) at one moment: what the game needs to draw them.
struct ReplayAgent {
    Vec3 pos, hitDir;
    float yaw = 0, pitch = 0, crouch = 0, deadFor = 0, stepDist = 0;
    uint8_t weapon = 0;
    uint8_t nade = 0;          // the grenade type in hand (when weapon is the grenade slot)
    uint8_t team = 255;        // competitive: 0 T, 1 CT (255: no teams) - their models (sides swap at half-time)
    bool alive = true, friendly = false, lostHelmet = false;
};

// One moment. agents: the dummies in their order, then you (the last one).
struct ReplayFrame {
    double t = 0;
    std::vector<ReplayAgent> agents;
    struct Smoke { Vec3 pos; double start; };
    std::vector<Smoke> smokes;
    struct Nade { Vec3 pos; uint8_t type; };   // grenades in the air
    std::vector<Nade> nades;
    struct Fire { Vec3 pos; double start; };   // molotov fires
    std::vector<Fire> fires;
};

// A shot: tracer and sound in the replay. shooter: an agent index (you = the last agent).
struct ReplayShot { double t; Vec3 from, to; int shooter; uint8_t weapon; };

class Replay {
public:
    static constexpr double kRate = 64.0;       // frames a second
    static constexpr double kSeconds = 150.0;   // how far back it keeps

    void clear();
    // How many frames it keeps (a saved match: all of them). Clears it.
    void setCapacity(size_t frames, size_t shots);
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
    size_t cap_ = size_t(kRate * kSeconds), shotCap_ = 20000;
    size_t head_ = 0, count_ = 0;  // the oldest frame is ring_[head_]
    std::vector<ReplayShot> shots_;
    double mark_ = -1;
    const ReplayFrame& at(size_t k) const { return ring_[(head_ + k) % ring_.size()]; }
};

// ---- Saved matches (v0.38): a whole match on disk, to watch again (REPLAYS on the main menu) ----
// What it was: the map (1 Dust, 2 Harbor; 3 was Inferno, removed), the mode (1 deathmatch, 3 competitive; wingman / armsRace), when,
// everyone's names (in agent order, you last), how it ended (team A = yours: its rounds, the other team's) and how long.
struct MatchInfo {
    int map = 1, mode = 3;
    bool wingman = false, armsRace = false;
    std::string date;                   // "2026-10-12 15:30"
    std::vector<std::string> names;
    int scoreA = 0, scoreB = 0;
    float seconds = 0;
};
// The timeline's marks: a round starting (round, the scores then) or a kill (killer, victim: agent indices; 255 none).
struct MatchEvent {
    double t = 0;
    uint8_t kind = 0;  // 0 round start, 1 kill
    uint8_t round = 0, scoreA = 0, scoreB = 0, killer = 255, victim = 255;
};
// Writes a match as it's played: a header, then frames, shots and events as they come (times from the match's start).
// Closing it writes the result into the header.
class MatchWriter {
public:
    ~MatchWriter() { close(0, 0); }
    bool open(const std::string& path, const MatchInfo& info, double startTime);
    bool isOpen() const { return file_.is_open(); }
    void frame(const ReplayFrame& f);
    void shot(const ReplayShot& s);
    void event(const MatchEvent& e);
    void close(int scoreA, int scoreB);
    const std::string& path() const { return path_; }
    double seconds() const { return last_; }  // how long it's been recording

private:
    std::ofstream file_;
    std::string path_;
    std::vector<uint8_t> buf_;  // (reused: no allocations per frame)
    double t0_ = 0, last_ = 0;
};
// Reads a saved match: its header (`info`), and with `into` / `events` everything in it (`into` sized to fit). False if it
// isn't one.
bool readMatch(const std::string& path, MatchInfo& info, Replay* into = nullptr, std::vector<MatchEvent>* events = nullptr);
