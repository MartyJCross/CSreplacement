#include "bots.h"
#include <algorithm>
#include <cmath>
#include "movement.h"

namespace {

float wrapDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

float yawTo(const Vec3& from, const Vec3& to) { return std::atan2(to.y - from.y, to.x - from.x) / kDegToRad; }

float turnToward(float yaw, float target, float maxStep) {
    return wrapDeg(yaw + std::clamp(wrapDeg(target - yaw), -maxStep, maxStep));
}

bool sightClear(const BotSenses& s, const Vec3& a, const Vec3& b) {
    if (s.world->traceRay(a, b).fraction < 1.0f) return false;
    return !(s.blocked && s.blocked(s.blockCtx, a, b));
}

}  // namespace

float botRand(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state & 0xFFFFFF) / float(0x1000000);
}

Vec3 randomSpawnPoint(const NavGrid& nav, const World& world, const std::vector<Vec3>& watcherEyes, float minDist,
                      const std::vector<Vec3>& occupied, uint32_t& rng) {
    Vec3 best = nav.roamPoint(botRand(rng), true);
    float bestScore = -1e30f;
    for (int tries = 0; tries < 40; ++tries) {
        Vec3 p = nav.roamPoint(botRand(rng), true);
        Vec3 head = p + Vec3{0, 0, 64};
        float score = 0;
        for (const Vec3& eye : watcherEyes) {
            float d = length(eye - head);
            if (d < minDist) score -= minDist - d;
            if (world.traceRay(eye, head).fraction >= 1.0f) score -= 10000.0f;  // in their sight
        }
        for (const Vec3& o : occupied)
            if (length2d(o - p) < 96.0f) score -= 20000.0f;
        if (score >= 0) return p;  // good enough: anywhere on the map that's safe
        if (score > bestScore) { bestScore = score; best = p; }
    }
    return best;
}

void spawnDeathmatchBot(Dummy& d, BotBrain& b, const Vec3& at, uint32_t& rng) {
    d.pos = d.prevPos = at;
    d.yaw = d.prevYaw = botRand(rng) * 360.0f - 180.0f;
    b.path.clear();
    b.state = 0;
    b.alertUntil = 0;
    b.sees = b.aimed = false;
}

void updateDeathmatchBot(Dummy& d, BotBrain& b, const BotSenses& s, uint32_t& rng) {
    // Perception: a 150 degree view cone (all round for a moment after being shot), line of sight,
    // no smoke in between. With several enemies it fights the nearest one it can see (sticking with
    // its current target while that one stays visible).
    const Vec3 head = d.pos + Vec3{0, 0, 64};
    const bool blind = s.now < b.blindUntil;
    auto visible = [&](const BotTarget& t) {
        const float dist = length(t.eye - head);
        const bool inView = std::fabs(wrapDeg(yawTo(d.pos, t.origin) - d.yaw)) < 75.0f || dist < 250.0f ||
                            s.now < b.alertUntil;
        return !blind && inView && dist < 4000.0f && sightClear(s, head, t.eye);
    };
    const BotTarget* chosen = nullptr;
    float bestDist = 1e30f;
    auto consider = [&](const BotTarget& t) {
        if (!visible(t)) return;
        float dist = length(t.origin - d.pos) * (t.id == b.target ? 0.7f : 1.0f);  // stick with the current one
        if (dist < bestDist) { bestDist = dist; chosen = &t; }
    };
    const BotTarget you{-1, s.playerOrigin, s.playerEye};
    if (s.targets) {
        for (const BotTarget& t : *s.targets) consider(t);
    } else if (s.playerUp) {
        consider(you);
    }
    b.sees = chosen != nullptr;
    b.target = chosen ? chosen->id : -2;
    const float toYaw = chosen ? yawTo(d.pos, chosen->origin) : yawTo(d.pos, b.lastSeen);
    if (b.sees) {
        b.lastSeen = chosen->origin;
        if (b.state != 2) { b.state = 2; b.path.clear(); }
        b.timer = 0.6f;  // keep the angle for a moment after losing sight
    } else if (s.noiseFresh && s.playerUp && b.state != 2 && length(s.noisePos - d.pos) < s.noiseRadius) {
        b.lastSeen = s.noisePos;
        b.state = 3;
        b.path.clear();
    }

    const float step = kBotRunSpeed * kTickDt;
    switch (b.state) {
        case 0:  // roam: walk to the goal if it has one, else a random spot anywhere on the map
            if (b.path.empty()) {
                const Vec3 dest = b.hasGoal ? b.goal : s.nav->roamPoint(botRand(rng), false);
                if (!s.nav->findPath(d.pos, dest, b.path)) {
                    if (b.hasGoal) { b.hasGoal = false; b.state = 1; b.timer = 0.5f; }  // unreachable: give up
                    break;
                }
                b.next = 1;
            }
            if (followPath(d.pos, b.path, b.next, step)) {
                b.path.clear();
                b.hasGoal = false;
                b.state = 1;
                b.timer = 0.3f + botRand(rng) * 0.9f;
            }
            break;
        case 1:  // hold an angle for a moment (anchors hold it for good)
            if ((b.timer -= kTickDt) <= 0 && !b.holdOnly) b.state = 0;
            break;
        case 2:  // fighting: stand and shoot (like CS bots); when you're gone, go and look
            if (!b.sees && (b.timer -= kTickDt) <= 0) { b.state = 3; b.path.clear(); }
            break;
        case 3:  // investigate where you were last seen or heard
            if (b.path.empty()) {
                if (!s.nav->findPath(d.pos, b.lastSeen, b.path)) { b.state = 0; break; }
                b.next = 1;
            }
            if (followPath(d.pos, b.path, b.next, step)) {
                b.path.clear();
                b.state = 1;
                b.timer = 0.8f + botRand(rng);
            }
            break;
        default:
            break;
    }

    // Face you when fighting, otherwise the way they walk (anchors: back to their angle).
    const Vec3 mv = d.pos - d.prevPos;
    const float idle = b.holdOnly && b.state == 1 ? b.holdYaw : d.yaw;
    const float want = b.state == 2 ? toYaw : length2d(mv) > 0.01f ? std::atan2(mv.y, mv.x) / kDegToRad : idle;
    d.yaw = turnToward(d.yaw, want, (b.state == 2 ? 600.0f : 360.0f) * kTickDt);
    b.aimed = b.sees && std::fabs(wrapDeg(toYaw - d.yaw)) < 12.0f;
}
