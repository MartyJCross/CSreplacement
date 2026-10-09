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

// Walkable in a straight line from a to b: standing room all the way, no step higher than a stair.
bool straightWalk(const BotSenses& s, const Vec3& a, const Vec3& b) {
    const int steps = std::max(1, int(length2d(b - a) / 16.0f));
    float z = a.z;
    for (int k = 1; k <= steps; ++k) {
        Vec3 p = a + (b - a) * (float(k) / float(steps));
        if (!s.nav->standable(p)) return false;
        const float fz = s.nav->floorAt(p);
        if (std::fabs(fz - z) > 18.0f) return false;
        z = fz;
    }
    return true;
}

// Moves the bot up to `step` units straight towards `to` (stops at walls and ledges). True once there.
bool stepToward(Dummy& d, const BotSenses& s, const Vec3& to, float step) {
    Vec3 delta{to.x - d.pos.x, to.y - d.pos.y, 0};
    const float len = length(delta);
    if (len < 0.5f) return true;
    Vec3 p = d.pos + delta * (std::min(step, len) / len);
    const float fz = s.nav->floorAt(p);
    if (!s.nav->standable(p) || std::fabs(fz - d.pos.z) > 18.0f) return true;  // blocked: stay put
    p.z = fz;
    d.pos = p;
    return len <= step;
}

// A route to `to`; if `to` itself can't be reached (in a wall or a prop at this map size, or on a ledge),
// to the nearest cell that can, which `to` is then moved to.
bool pathTo(const BotSenses& s, const Vec3& from, Vec3& to, std::vector<Vec3>& path) {
    if (s.nav->findPath(from, to, path)) return true;
    Vec3 alt;
    if (!s.nav->nearestRoamable(to, 6, alt) || !s.nav->findPath(from, alt, path)) return false;
    to = alt;
    return true;
}

// Done chasing or fighting: a bot with a spot of its own walks back to it (else holds or looks around).
void backHome(Dummy& d, BotBrain& b, int otherwise) {
    if (b.hasHome && length2d(b.home - d.pos) > 160.0f) {
        b.goal = b.home;
        b.hasGoal = true;
        b.holdCoverChecked = false;
        b.state = 0;
    } else {
        b.state = otherwise;
    }
    b.path.clear();
}

}  // namespace

bool findCover(const BotSenses& s, const Vec3& around, const Vec3& threatEye, float radius, Vec3& cover, Vec3& peek) {
    if (!s.nav || !s.world) return false;
    // Seen = the head or the chest of someone standing there is in the threat's line of sight (walls and
    // props; smokes don't count, they go away).
    auto seen = [&](const Vec3& feet) {
        return s.world->traceRay(threatEye, feet + Vec3{0, 0, 62.0f * modelScale()}).fraction >= 1.0f ||
               s.world->traceRay(threatEye, feet + Vec3{0, 0, 40.0f * modelScale()}).fraction >= 1.0f;
    };
    struct Cand { Vec3 p; float d; };
    static thread_local std::vector<Cand> cands;  // reused: no allocation after warm-up
    cands.clear();
    const int n = int(radius / 32.0f);
    for (int j = -n; j <= n; ++j)
        for (int i = -n; i <= n; ++i) {
            Vec3 p = around + Vec3{float(i) * 32.0f, float(j) * 32.0f, 0};
            const float d = length2d(p - around);
            if (d > radius || !s.nav->standable(p)) continue;
            p.z = s.nav->floorAt(p);
            if (std::fabs(p.z - around.z) > 24.0f) continue;
            cands.push_back({p, d});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    int tried = 0;
    for (const Cand& c : cands) {
        if (seen(c.p)) continue;
        if (++tried > 16) break;
        if (!straightWalk(s, around, c.p)) continue;
        float best = 1e30f;
        for (int k = 0; k < 8; ++k)
            for (float r : {40.0f, 72.0f}) {
                const float a = float(k) * 45.0f * kDegToRad;
                Vec3 q = c.p + Vec3{std::cos(a) * r, std::sin(a) * r, 0};
                if (r >= best || !s.nav->standable(q)) continue;
                q.z = s.nav->floorAt(q);
                if (std::fabs(q.z - c.p.z) > 18.0f || !seen(q) || !straightWalk(s, c.p, q)) continue;
                best = r;
                peek = q;
            }
        if (best < 1e30f) {
            cover = c.p;
            return true;
        }
    }
    return false;
}

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
        Vec3 head = p + Vec3{0, 0, dummyEyeZ()};
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
    b.hasCover = b.inCover = false;
    b.alertUntil = 0;
    b.sees = b.aimed = false;
}

void updateDeathmatchBot(Dummy& d, BotBrain& b, const BotSenses& s, uint32_t& rng) {
    // Perception: a 150 degree view cone (all round for a moment after being shot), line of sight,
    // no smoke in between. With several enemies it fights the nearest one it can see (sticking with
    // its current target while that one stays visible).
    const Vec3 head = d.pos + Vec3{0, 0, dummyEyeZ(d.crouch)};
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
        if (t.id == s.self || !visible(t)) return;
        float dist = length(t.origin - d.pos) * (t.id == b.target ? 0.7f : 1.0f);  // stick with the current one
        if (dist < bestDist) { bestDist = dist; chosen = &t; }
    };
    const BotTarget you{-1, s.playerOrigin, s.playerEye};
    if (s.targets) {
        for (const BotTarget& t : *s.targets) consider(t);
    } else if (s.playerUp) {
        consider(you);
    }
    // In a hurry (the bomb carrier, the clock running out): don't stop for someone far away, keep going.
    if (chosen && b.urgent && length2d(chosen->origin - d.pos) > 1000.0f) chosen = nullptr;
    b.sees = chosen != nullptr;
    b.target = chosen ? chosen->id : -2;
    const float toYaw = chosen ? yawTo(d.pos, chosen->origin) : yawTo(d.pos, b.lastSeen);
    if (b.sees) {
        // Holding an empty angle for a while dulls you: the first contact after a quiet spell is slower.
        if (b.state != 2) b.surprise = std::clamp(float(s.now - b.lastSawAt - 4.0) / 8.0f, 0.0f, 1.0f);
        b.lastSeen = chosen->origin;
        if (b.frozen) {
            b.state = 2;
        } else if (b.state != 2 || chosen->id != b.coverFor) {
            // A fight starts (or a new enemy): shoot from here first, then work from cover if there's
            // some close by (duck in, peek out, shoot), else jiggle in the open.
            b.state = 2;
            b.path.clear();
            b.strafing = false;
            b.strafeTimer = 0.4f + botRand(rng) * 0.4f;
            b.coverFor = chosen->id;
            // In a hurry (bomb carrier, clock running out): no hiding, fight and keep going.
            b.hasCover = !b.urgent && findCover(s, d.pos, chosen->eye, 200.0f, b.cover, b.peek);
            b.inCover = false;
            b.firstShots = true;
            b.coverTimer = 0.5f + botRand(rng) * 0.5f;
        }
        b.lastSawAt = s.now;
        b.timer = b.urgent ? 0.25f : 0.6f;  // keep the angle for a moment after losing sight
    } else if (!b.frozen && s.noiseFresh && s.playerUp && b.state != 2 && length(s.noisePos - d.pos) < s.noiseRadius) {
        b.lastSeen = s.noisePos;
        b.state = 3;
        b.path.clear();
    }

    const float step = kBotRunSpeed * kTickDt;
    if (b.frozen) {  // stays on its spot; back to its angle a moment after losing you
        b.strafing = false;
        if (b.state == 2 && !b.sees && (b.timer -= kTickDt) <= 0) b.state = 1;
    } else switch (b.state) {
        case 0:  // roam: walk to the goal if it has one, else a random spot (deathmatch: often near you)
            if (b.path.empty()) {
                Vec3 dest = b.hasGoal ? b.goal : s.nav->roamPoint(botRand(rng), false);
                if (!b.hasGoal && (!s.targets || s.huntYou) && s.playerUp && botRand(rng) < 0.5f)
                    for (int k = 0; k < 8; ++k) {  // hunt: somewhere in your part of the map
                        Vec3 p = s.nav->roamPoint(botRand(rng), false);
                        if (length2d(p - s.playerOrigin) < 900.0f) { dest = p; break; }
                    }
                if (!pathTo(s, d.pos, dest, b.path)) {
                    if (b.hasGoal) { b.hasGoal = false; b.state = 1; b.timer = 0.5f; }  // unreachable: give up
                    break;
                }
                if (b.hasGoal) b.goal = dest;
                b.next = 1;
            }
            if (followPath(d.pos, b.path, b.next, step)) {
                b.path.clear();
                b.hasGoal = false;
                b.holdCoverChecked = b.hasCover = false;
                b.state = 1;
                b.timer = 0.2f + botRand(rng) * 0.5f;
            }
            break;
        case 1:  // hold an angle for a moment (anchors hold it for good); a new goal sends it off
            if (b.hasGoal) { b.state = 0; b.path.clear(); break; }
            if (b.holdOnly && b.hasHoldLook && !b.holdCoverChecked) {
                // An anchor: hold the angle from here, but find cover from it to step back into.
                b.holdCoverChecked = true;
                b.hasCover = findCover(s, d.pos, b.holdLook + Vec3{0, 0, dummyEyeZ()}, 160.0f, b.cover, b.peek);
                b.peek = d.pos;
                b.inCover = false;
                b.coverTimer = 5.0f + botRand(rng) * 5.0f;
            }
            if (b.holdOnly && b.hasCover) {
                if ((b.coverTimer -= kTickDt) <= 0) {
                    b.inCover = !b.inCover;
                    b.coverTimer = b.inCover ? 1.5f + botRand(rng) * 2.0f : 7.0f + botRand(rng) * 7.0f;
                }
                stepToward(d, s, b.inCover ? b.cover : b.peek, 130.0f * kTickDt);
                break;
            }
            if ((b.timer -= kTickDt) <= 0 && !b.holdOnly) b.state = 0;
            break;
        case 2:  // fighting: from cover if it has some, else jiggle (strafe a step, stop and shoot, like CS bots)
            if (b.hasCover) {
                if ((b.coverTimer -= kTickDt) <= 0) {
                    b.inCover = !b.inCover;
                    if (b.inCover) b.firstShots = false;
                    const bool hurt = d.hp < 45.0f;  // hurt: stays hidden longer, waits for you to come
                    b.coverTimer = b.inCover ? (hurt ? 1.2f + botRand(rng) * 1.3f : 0.35f + botRand(rng) * 0.55f)
                                             : 0.6f + botRand(rng) * 0.8f;
                }
                const Vec3 to = b.inCover ? b.cover : b.firstShots ? d.pos : b.peek;
                b.strafing = !stepToward(d, s, to, 200.0f * kTickDt);
                if (s.now - b.lastSawAt > 2.5) {  // peeked and peeked, nobody there: go and look / back to its spot
                    b.hasCover = b.strafing = false;
                    b.coverFor = -3;
                    b.timer = 1.0f;
                    if (b.holdOnly) backHome(d, b, 1);
                    else {
                        b.state = 3;
                        b.path.clear();
                    }
                }
                break;
            }
            if (!b.sees) {
                b.strafing = false;
                if ((b.timer -= kTickDt) <= 0) {  // gone: go and look - or, in a hurry, straight back on the way
                    b.state = b.urgent && b.hasGoal ? 0 : 3;
                    b.path.clear();
                }
                break;
            }
            if ((b.strafeTimer -= kTickDt) <= 0) {
                b.strafing = !b.strafing;
                b.strafeTimer = b.strafing ? 0.2f + botRand(rng) * 0.25f : 0.35f + botRand(rng) * 0.5f;
                if (b.strafing) b.strafeDir = botRand(rng) < 0.5f ? -1 : 1;
            }
            if (b.strafing) {
                const float y = toYaw * kDegToRad;
                const Vec3 side{-std::sin(y) * float(b.strafeDir), std::cos(y) * float(b.strafeDir), 0};
                Vec3 p = d.pos + side * (200.0f * kTickDt);
                const float fz = s.nav->floorAt(p);
                if (s.nav->standable(p) && std::fabs(fz - d.pos.z) < 18.0f) {
                    p.z = fz;
                    d.pos = p;
                } else {
                    b.strafeDir = -b.strafeDir;  // wall or ledge: step the other way next time
                }
            }
            break;
        case 3:  // investigate where you were last seen or heard, then (an anchor) back to its spot
            if (b.path.empty()) {
                if (!pathTo(s, d.pos, b.lastSeen, b.path)) {
                    backHome(d, b, 0);
                    break;
                }
                b.next = 1;
            }
            if (followPath(d.pos, b.path, b.next, step)) {
                b.timer = 0.8f + botRand(rng);
                if (b.holdOnly) backHome(d, b, 1);
                else {
                    b.path.clear();
                    b.state = 1;
                }
            }
            break;

        default:
            break;
    }

    // Face you when fighting, otherwise the way they walk (anchors: back to their angle).
    const Vec3 mv = d.pos - d.prevPos;
    const float idle = b.holdOnly && b.state == 1 ? b.holdYaw : d.yaw;
    const bool anchoring = b.holdOnly && b.state == 1 && b.hasCover;  // stepping in and out of cover: keeps its angle
    const float want = b.state == 2 ? toYaw
                       : anchoring || length2d(mv) <= 0.01f ? idle
                                                           : std::atan2(mv.y, mv.x) / kDegToRad;
    d.yaw = turnToward(d.yaw, want, (b.state == 2 ? 600.0f : 360.0f) * kTickDt);
    b.aimed = b.sees && !b.strafing && std::fabs(wrapDeg(toYaw - d.yaw)) < 12.0f;  // they stop to shoot
}
