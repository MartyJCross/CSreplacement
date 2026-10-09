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

// Fires: the one `p` stands in (within `margin` past its edge), if any.
using Fire = Vec3;
const Fire* fireAt(const BotSenses& s, const Vec3& p, float margin) {
    for (const Vec3& f : *s.fires)
        if (length2d(p - f) < s.fireRadius + margin && std::fabs(p.z - f.z) < 64.0f) return &f;
    return nullptr;
}

// The way out of fire `f` from `p`: the nearest spot just outside it (round the ring, or further out) that it can
// walk to in a straight line, else the nearest one it has a route to (off a ledge the other way, round a corner).
bool fireExit(const BotSenses& s, const Vec3& p, const Fire& f, Vec3& out) {
    std::pair<float, Vec3> spots[48];
    int n = 0;
    for (int k = 0; k < 48; ++k) {  // two rings: just outside, and further out
        const float a = float(k % 24) * (6.2831853f / 24.0f);
        Vec3 c = f + Vec3{std::cos(a), std::sin(a), 0} * (s.fireRadius + (k < 24 ? 28.0f : 90.0f));
        if (!s.nav->standable(c)) continue;
        c.z = s.nav->floorAt(c);
        if (!fireAt(s, c, 8.0f)) spots[n++] = {length2d(c - p), c};
    }
    std::sort(spots, spots + n, [](const auto& x, const auto& y) { return x.first < y.first; });
    for (int k = 0; k < n; ++k)
        if (straightWalk(s, p, spots[k].second)) { out = spots[k].second; return true; }
    static std::vector<Vec3> route;  // (sim thread only)
    for (int k = 0; k < n && k < 8; ++k)
        if (s.nav->findPath(p, spots[k].second, route)) { out = spots[k].second; return true; }
    return false;
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
    // In a fire: out of it first, whatever it was doing (it keeps facing its fight), to the nearest open ground
    // outside it. Walking on stops at the edge (see the end): it waits for the fire to burn out.
    const Fire* burning = s.fires ? fireAt(s, d.pos, 8.0f) : nullptr;
    const bool escaping = !b.frozen && burning;
    if (!escaping) b.hasFireExit = false;
    const Vec3 before = d.pos;
    if (escaping) {  // (it keeps the way out it picked, so it doesn't dither between two)
        if (!b.hasFireExit || fireAt(s, b.fireExit, 8.0f) || length2d(b.fireExit - d.pos) < 2.0f)
            b.hasFireExit = fireExit(s, d.pos, *burning, b.fireExit);
        const Vec3 was = d.pos;
        if (b.hasFireExit && stepToward(d, s, b.fireExit, step)) b.hasFireExit = false;
        if (b.hasFireExit && length2d(d.pos - was) < 0.01f) {  // a corner in the way: along the route's cells
            static std::vector<Vec3> route;  // (sim thread only)
            if (s.nav->findPath(d.pos, b.fireExit, route) && route.size() >= 2) {
                stepToward(d, s, route[1], step);
                if (length2d(d.pos - was) < 0.01f) stepToward(d, s, route[0], step);
            }
        }
        b.strafing = false;
    } else if (b.frozen) {  // stays on its spot; back to its angle a moment after losing you
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

    // Never a step further into a fire (it waits at the edge).
    if (!escaping && !b.frozen && s.fires && d.pos.x != before.x) {
        const Fire* f = fireAt(s, d.pos, 8.0f);
        if (f && length2d(d.pos - *f) < length2d(before - *f)) d.pos = before;
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

BuyRound teamBuyRound(int avgMoney, bool pistolRound, bool mustWin) {
    if (pistolRound) return kBuyPistol;
    if (avgMoney >= 3700) return kBuyFull;  // a rifle, kevlar and a helmet each
    if (mustWin || avgMoney >= 2900) return kBuyForce;
    return kBuyEco;  // save: next round they can buy properly
}

BotBuy botBuy(BuyRound round, int money, int side, int role, int gun, bool armor, bool helmet) {
    BotBuy b{gun, armor, helmet, 0};
    auto spend = [&](int c) { money -= c; b.spent += c; };
    auto armorUp = [&](bool withHelmet) {
        if (!b.armor && money >= 650) { spend(650); b.armor = true; }
        if (withHelmet && b.armor && !b.helmet && money >= 350) { spend(350); b.helmet = true; }
    };
    auto take = [&](int w) {
        if (money < weaponDef(w).price) return false;
        spend(weaponDef(w).price);
        b.gun = w;
        return true;
    };
    if (weaponDef(gun).primary) {  // it kept its gun: armor up - and on a full buy, a cheaper one gets swapped
        const int better = role == 0 && money >= weaponDef(kWSniper).price + 1000 ? kWSniper : side == 0 ? kWRifle : kWM4A1S;
        const int kit = (armor ? 0 : 650) + (helmet ? 0 : 350);
        if (round == kBuyFull && weaponDef(gun).price < weaponDef(better).price && money >= weaponDef(better).price + kit)
            take(better);
        armorUp(true);
        return b;
    }
    if (round == kBuyEco && money >= 5500) round = kBuyFull;  // rich enough to buy anyway
    switch (round) {
        case kBuyPistol:  // $800: kevlar mostly, one Deagle, one pair of Berettas
            if (role == 1) take(kWDeagle);
            else if (role == 3) take(kWBerettas);
            else armorUp(false);
            break;
        case kBuyEco:  // save; one bot gambles on a Deagle
            if (role == 1 && money >= 1400) take(kWDeagle);
            break;
        case kBuyFull: {
            const int rifle = side == 0 ? kWRifle : kWM4A1S;
            if (role == 0 && money >= weaponDef(kWSniper).price + 650 && take(kWSniper)) { armorUp(true); break; }
            if (money >= weaponDef(rifle).price + 650 && take(rifle)) { armorUp(true); break; }
            [[fallthrough]];  // can't afford the full kit: force
        }
        case kBuyForce: {
            armorUp(false);
            const int t[5] = {kWSsg08, kWGalil, kWGalil, kWMac10, kWNova};
            const int ct[5] = {kWSsg08, kWUmp45, kWUmp45, kWUmp45, kWXm1014};
            const int want = side == 0 ? t[role % 5] : ct[role % 5];
            if (take(want)) break;
            if (take(side == 0 ? kWMac10 : kWUmp45)) break;  // the cheap SMG
            if (take(kWNova)) break;
            take(kWDeagle);
            break;
        }
    }
    return b;
}

int deathmatchBotGun(float r01) {
    struct Pick { int gun, weight; };
    const Pick picks[] = {{kWRifle, 30}, {kWM4A1S, 22}, {kWGalil, 8}, {kWSniper, 8}, {kWSsg08, 5}, {kWMac10, 6},
                          {kWUmp45, 6}, {kWNova, 3}, {kWXm1014, 4}, {kWDeagle, 8}};
    int total = 0;
    for (const Pick& p : picks) total += p.weight;
    int at = std::min(total - 1, int(r01 * float(total)));
    for (const Pick& p : picks) {
        if (at < p.weight) return p.gun;
        at -= p.weight;
    }
    return kWRifle;
}

float botShotGap(int w) {
    float tap = 0.22f;  // rifles: taps and short bursts
    switch (w) {
        case kWMac10: case kWUmp45: tap = 0.15f; break;  // SMGs: spray closer
        case kWPistol: tap = 0.32f; break;
        case kWBerettas: tap = 0.26f; break;
        case kWDeagle: tap = 0.5f; break;
        default: break;
    }
    return std::max(tap, weaponDef(w).fireInterval);  // the bolt guns and the pump: the gun's own pace
}
