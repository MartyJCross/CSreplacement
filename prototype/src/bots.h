// Deathmatch bot brains: roam the whole map, react to what they see and hear, chase, stop to shoot.
// Pure simulation (no SDL), so the headless tests can run whole matches' worth of bot movement.
#pragma once
#include <cstdint>
#include <vector>
#include "combat.h"
#include "nav.h"

struct BotBrain {
    std::vector<Vec3> path;
    size_t next = 0;
    int state = -1;           // -1 just spawned, 0 roam, 1 hold an angle, 2 fighting, 3 investigate
    float timer = 0;
    Vec3 lastSeen;            // where they last saw or heard you
    double alertUntil = 0;    // just got shot: aware all round for a moment
    double blindUntil = 0;    // flashed: sees nothing until then
    bool sees = false, aimed = false;
    int target = -2;          // who it's fighting: -1 = you, i = bot i, -2 = nobody
    bool strafing = false;    // fighting: jiggling sideways (doesn't shoot while moving)
    int strafeDir = 1;
    float strafeTimer = 0;
    // Retakes: an anchor that holds its angle (holdYaw) instead of roaming. It still turns on you,
    // checks noises and fights, then holds wherever it ends up.
    bool holdOnly = false;
    float holdYaw = 0;
    // Competitive: walk here (then hold, if holdOnly) instead of roaming at random.
    Vec3 goal;
    bool hasGoal = false;
};

// Someone a bot can fight: id -1 = you, i = bot i.
struct BotTarget { int id; Vec3 origin, eye; };

// What the bots get to know this tick.
struct BotSenses {
    const World* world = nullptr;
    const NavGrid* nav = nullptr;
    double now = 0;
    Vec3 playerOrigin, playerEye;
    bool playerUp = false;    // alive, not noclipping, match running
    // Competitive: the enemies this bot may fight (its own team's view). Null = just you (the player).
    const std::vector<BotTarget>* targets = nullptr;
    bool noiseFresh = false;  // you made a sound this tick (footstep, shot)
    Vec3 noisePos;
    float noiseRadius = 0;
    // Optional extra sight blocker (smoke): returns true if the segment a->b is blocked.
    bool (*blocked)(const void* ctx, const Vec3& a, const Vec3& b) = nullptr;
    const void* blockCtx = nullptr;
};

constexpr float kBotRunSpeed = 215.0f;  // rifle run speed

float botRand(uint32_t& state);  // 0..1, xorshift (bots only: the player's shots never use randomness)

// A random standing spot anywhere on the walkable map: at least `minDist` from every watcher and out of
// their sight, and not on top of an occupied spot. Falls back to the best of the spots tried.
Vec3 randomSpawnPoint(const NavGrid& nav, const World& world, const std::vector<Vec3>& watcherEyes, float minDist,
                      const std::vector<Vec3>& occupied, uint32_t& rng);

// Puts a bot that has just come back to life at `at`, facing a random way, ready to roam.
void spawnDeathmatchBot(Dummy& d, BotBrain& b, const Vec3& at, uint32_t& rng);

// True when a live bot hasn't been placed since it (re)spawned: call spawnDeathmatchBot first.
inline bool needsSpawn(const Dummy& d, const BotBrain& b) { return d.alive() && b.state < 0; }

// Advances one placed, living deathmatch bot by one tick (moves d.pos, turns d.yaw, sets b.sees / b.aimed).
void updateDeathmatchBot(Dummy& d, BotBrain& b, const BotSenses& s, uint32_t& rng);
