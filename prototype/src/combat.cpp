#include "combat.h"
#include "movement.h"
#include <algorithm>

const char* hitGroupName(HitGroup g) {
    switch (g) {
        case kHead: return "HEAD";
        case kChest: return "CHEST";
        case kStomach: return "STOMACH";
        case kLegs: return "LEGS";
        default: return "?";
    }
}

namespace {

// Original 30-round pattern: climbs for ~9 shots, pulls left, then right, then wobbles.
const RecoilStep kRiflePattern[30] = {
    {0.00f, 0.00f}, {0.95f, 0.05f}, {1.15f, 0.10f}, {1.30f, 0.05f}, {1.40f, 0.10f}, {1.30f, 0.15f},
    {1.10f, 0.20f}, {0.90f, 0.20f}, {0.70f, 0.25f}, {0.35f, -0.55f}, {0.30f, -0.75f}, {0.25f, -0.85f},
    {0.20f, -0.80f}, {0.15f, -0.70f}, {0.10f, -0.55f}, {0.10f, 0.40f}, {0.10f, 0.70f}, {0.05f, 0.85f},
    {0.05f, 0.85f}, {0.05f, 0.75f}, {0.05f, 0.60f}, {0.00f, 0.40f}, {0.05f, -0.35f}, {0.00f, -0.50f},
    {0.05f, -0.45f}, {0.00f, 0.30f}, {0.05f, 0.45f}, {0.00f, 0.35f}, {0.05f, -0.30f}, {0.00f, -0.30f},
};

const WeaponDef kRifle = {
    "RIFLE", true, 215.0f, 36.0f, 0.98f, 0.1f, 30, 2.4f,
    0.34f, 5.0f, 8.0f, 0.12f, kRiflePattern, 30, true, 24.0f,
};

// Semi-auto pistol: strong per-shot kick that climbs fast and recovers fast. Tap, don't spam.
const RecoilStep kPistolPattern[12] = {
    {0.00f, 0.00f}, {1.60f, 0.15f}, {1.50f, -0.25f}, {1.40f, 0.30f}, {1.30f, -0.30f}, {1.20f, 0.25f},
    {1.10f, -0.20f}, {1.00f, 0.20f}, {0.90f, -0.20f}, {0.80f, 0.15f}, {0.70f, -0.15f}, {0.60f, 0.10f},
};

const WeaponDef kPistol = {
    "PISTOL", true, 240.0f, 35.0f, 0.91f, 0.15f, 12, 2.2f,
    0.34f, 3.5f, 6.0f, 0.25f, kPistolPattern, 12, false, 0.0f,
};

// Bolt-action sniper: one-shot body kill, big single kick, slow cycle. Semi-auto.
const RecoilStep kSniperPattern[2] = {{0.0f, 0.0f}, {2.2f, 0.0f}};

const WeaponDef kSniper = {
    "SNIPER", true, 200.0f, 115.0f, 0.99f, 1.46f, 5, 3.6f,
    0.34f, 8.0f, 12.0f, 0.0f, kSniperPattern, 2, false, 40.0f,
};

const WeaponDef kKnife = {
    "KNIFE", false, 250.0f, 0, 1, 1, 0, 0, 0.34f, 0, 0, 0, nullptr, 0, false,
};

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
float rand01(uint32_t seed) { return (hash32(seed) & 0xFFFFFF) / float(0x1000000); }

float hitGroupMultiplier(HitGroup g) {
    switch (g) {
        case kHead: return 4.0f;
        case kStomach: return 1.25f;
        case kLegs: return 0.75f;
        default: return 1.0f;
    }
}

}  // namespace

const WeaponDef& rifleDef() { return kRifle; }
const WeaponDef& knifeDef() { return kKnife; }
const WeaponDef& grenadeDef() {
    static const WeaponDef kGrenade = {"SMOKE", false, 245.0f, 0, 1, 1, 0, 0, 0.34f, 0, 0, 0, nullptr, 0, false};
    return kGrenade;
}
const WeaponDef& pistolDef() { return kPistol; }
const WeaponDef& sniperDef() { return kSniper; }

RecoilStep recoilAt(const WeaponDef& w, float index) {
    RecoilStep sum{0, 0};
    if (!w.pattern || index <= 0) return sum;
    int whole = std::min(int(index), w.patternLen - 1);
    for (int i = 0; i <= whole; ++i) { sum.up += w.pattern[i].up; sum.right += w.pattern[i].right; }
    // Interpolate into the next step so recovery is smooth.
    float frac = std::min(index, float(w.patternLen - 1)) - whole;
    if (frac > 0 && whole + 1 < w.patternLen) {
        sum.up += w.pattern[whole + 1].up * frac;
        sum.right += w.pattern[whole + 1].right * frac;
    }
    // Step 0 is zero, so the first shot of a spray always goes exactly where you aim.
    return sum;
}

float currentInaccuracy(const WeaponState& ws, float horizSpeed, bool onGround, bool ducked) {
    const WeaponDef& w = *ws.def;
    float inacc = ws.spraySpread ? std::min(ws.recoilIndex, 8.0f) * w.sprayInaccuracy : 0.0f;
    if (!ws.moveSpread) return inacc;
    float threshold = w.maxSpeed * w.accurateSpeedFrac;
    if (horizSpeed > threshold) {
        float t = std::min(1.0f, (horizSpeed - threshold) / (w.maxSpeed - threshold));
        inacc += t * w.moveInaccuracy;
    }
    if (!onGround) inacc += w.airInaccuracy;
    if (ducked && onGround) inacc *= 0.8f;
    return inacc;
}

void decayRecoil(WeaponState& ws, float dt) {
    // dI/dt = -(3 + 3.5 I): one tap recovers in ~0.2 s, a full spray in ~1 s.
    ws.recoilIndex = std::max(0.0f, ws.recoilIndex - dt * (3.0f + 3.5f * ws.recoilIndex));
}

const std::vector<Hitbox>& dummyHitboxes() {
    // Model space: the dummy faces -X, so it's wider along Y. Arms count as chest, like CS.
    static const std::vector<Hitbox> boxes = {
        {{-5.0f, -8.0f, 0}, {5.0f, 8.0f, 34}, kLegs},
        {{-6.0f, -9.0f, 34}, {6.0f, 9.0f, 46}, kStomach},
        {{-6.5f, -10.0f, 46}, {6.5f, 10.0f, 58}, kChest},
        {{-4.5f, -4.5f, 59}, {4.5f, 4.5f, 69}, kHead},
        {{-13.0f, -13.5f, 44}, {3.0f, -10.0f, 57}, kChest},  // right arm, reaching to the grip
        {{-15.0f, 10.0f, 44}, {1.0f, 13.5f, 57}, kChest},    // left arm, on the handguard
    };
    return boxes;
}

std::vector<Dummy> buildDummies() {
    std::vector<Dummy> out;
    auto addStatic = [&](Vec3 p) {
        Dummy d; d.pos = d.prevPos = p; out.push_back(d);
    };
    auto addStrafe = [&](Vec3 a, Vec3 b, float speed, float pause) {
        Dummy d; d.motion = DummyMotion::Strafe; d.a = a; d.b = b; d.speed = speed; d.pause = pause;
        d.pos = d.prevPos = a; out.push_back(d);
    };
    // Range: 512 / 1024 / 1536 / 2048 units (~13 / 26 / 39 / 52 m).
    addStatic({512, 0, 0});
    addStatic({1024, 64, 0});
    addStatic({1536, -64, 0});
    addStatic({2048, 0, 0});
    // Strafers: one that counter-strafes (stops at the ends), one that runs through.
    addStrafe({768, -192, 0}, {768, 192, 0}, 215, 0.25f);
    addStrafe({1280, -256, 0}, {1280, 256, 0}, 250, 0.0f);
    // Behind the doorway in the peek wall.
    addStatic({1700, 640, 0});
    addStrafe({1500, 560, 0}, {1500, 720, 0}, 150, 0.4f);
    return out;
}

float crouchZ(float z, float crouch) {
    constexpr float kHip = 34.0f, kDrop = 18.0f;
    return z <= kHip ? z * (1.0f - kDrop / kHip * crouch) : z - kDrop * crouch;
}

bool rayHitsDummy(const Vec3& pos, float yaw, float crouch, const Vec3& start, const Vec3& dir, float maxT, float& t,
                  HitGroup& group) {
    const float a = -(yaw - 180.0f) * kDegToRad, c = std::cos(a), s = std::sin(a);
    auto toModel = [&](const Vec3& v) { return Vec3{c * v.x - s * v.y, s * v.x + c * v.y, v.z}; };
    const Vec3 localStart = toModel(start - pos), localDir = toModel(dir);
    bool hit = false;
    for (const Hitbox& hb : dummyHitboxes()) {
        float th;
        const Vec3 mn{hb.mins.x, hb.mins.y, crouchZ(hb.mins.z, crouch)}, mx{hb.maxs.x, hb.maxs.y, crouchZ(hb.maxs.z, crouch)};
        if (rayHitsBox(localStart, localDir, maxT, mn, mx, th, nullptr) && th >= 0 && th < maxT) {
            maxT = th;
            t = th;
            group = hb.group;
            hit = true;
        }
    }
    return hit;
}

float hitGroupDamageScale(HitGroup g) { return hitGroupMultiplier(g); }

float armoredDamage(float damage, HitGroup group, float armor, bool helmet) {
    if (armor <= 0 || group == kLegs || (group == kHead && !helmet)) return damage;
    return damage * 0.775f;
}

void updateDummy(Dummy& d, float dt) {
    d.prevPos = d.pos;
    d.prevYaw = d.yaw;
    d.prevCrouch = d.crouch;
    for (float& f : d.flash) f = std::max(0.0f, f - dt);
    if (!d.alive()) {
        d.respawnLeft -= dt;
        d.deadFor += dt;
        if (d.alive()) {
            d.hp = 100;
            d.deadFor = 0;
            if (d.randomRespawn) {
                // Hand-placed peek spots (some half behind cover); never the same spot twice in a row.
                static const Vec3 kSpots[] = {
                    {600, -220, 0},  {760, 230, 0},   {980, -60, 0},   {1150, 200, 0},
                    {1400, -230, 0}, {1560, 40, 0},   {1700, 230, 0},  {1900, -180, 0},
                    {2100, 100, 0},  {2300, -60, 0},  {1650, -210, 0}, {360, 250, 0},
                };
                const int n = int(sizeof(kSpots) / sizeof(kSpots[0]));
                uint32_t s = hash32(++d.respawns * 7919u + 17u);
                int spot = int(s % uint32_t(n));
                if (spot == d.lastSpot) spot = (spot + 1) % n;
                d.lastSpot = spot;
                d.pos = d.prevPos = kSpots[spot];
                if ((s >> 7) & 1) {  // about half the spawns ADAD-strafe across their spot
                    d.motion = DummyMotion::Strafe;
                    d.a = kSpots[spot] - Vec3{0, 56, 0};
                    d.b = kSpots[spot] + Vec3{0, 56, 0};
                    d.speed = 215;
                    d.pause = 0.2f;
                    d.towardB = true;
                    d.pauseLeft = 0;
                } else {
                    d.motion = DummyMotion::Static;
                }
            }
        }
        return;
    }
    if (d.motion != DummyMotion::Strafe) return;
    if (d.pauseLeft > 0) { d.pauseLeft -= dt; return; }
    Vec3 target = d.towardB ? d.b : d.a;
    Vec3 delta = target - d.pos;
    float dist = length(delta), step = d.speed * dt;
    if (dist <= step) {
        d.pos = target;
        d.towardB = !d.towardB;
        d.pauseLeft = d.pause;
    } else {
        d.pos += delta * (step / dist);
    }
}

ShotResult fireBullet(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed,
                      bool onGround, bool ducked, const World& world, std::vector<Dummy>& dummies,
                      const std::vector<Vec3>& dummyRenderPos) {
    const WeaponDef& w = *ws.def;
    ShotResult res;
    res.sprayIndex = int(ws.recoilIndex);

    // Bullet direction = view + full aim punch + deterministic spread.
    RecoilStep punch = recoilAt(w, ws.recoilIndex);
    float inacc = currentInaccuracy(ws, horizSpeed, onGround, ducked);
    uint32_t seed = ws.shotCounter * 2654435761u;
    float theta = rand01(seed) * 2.0f * kPi;
    float radius = rand01(seed ^ 0x9e3779b9u) * inacc;
    float pitch = viewPitch - punch.up + std::sin(theta) * radius;
    float yaw = viewYaw - punch.right + std::cos(theta) * radius;
    Vec3 dir = anglesToForward(pitch, yaw);

    // Trace in segments: hit a wall -> if it is thin enough for this weapon, pass through
    // (losing damage) and keep going. Up to two walls.
    const float kRange = 8192.0f;
    float bestT = kRange, travelled = 0, dmgScale = 1.0f, penLeft = w.penetration;
    Vec3 segStart = eye;
    for (int seg = 0; seg < 3; ++seg) {
        float remaining = kRange - travelled;
        TraceResult wt = world.traceRay(segStart, segStart + dir * remaining);
        float segT = wt.fraction * remaining;
        bool hitDummy = false;
        for (size_t i = 0; i < dummies.size(); ++i) {
            if (!dummies[i].alive() || dummies[i].friendly) continue;  // teammates: your bullets pass through
            // Test in the dummy's model space (it faces -X there): turn the ray by -(yaw - 180).
            const float a = -(dummies[i].shownYaw - 180.0f) * kDegToRad, c = std::cos(a), s = std::sin(a);
            auto toModel = [&](const Vec3& v) { return Vec3{c * v.x - s * v.y, s * v.x + c * v.y, v.z}; };
            const Vec3 localStart = toModel(segStart - dummyRenderPos[i]), localDir = toModel(dir);
            const float crouch = dummies[i].shownCrouch;
            for (const Hitbox& hb : dummyHitboxes()) {
                float t;
                Vec3 n;
                const Vec3 mn{hb.mins.x, hb.mins.y, crouchZ(hb.mins.z, crouch)};
                const Vec3 mx{hb.maxs.x, hb.maxs.y, crouchZ(hb.maxs.z, crouch)};
                if (rayHitsBox(localStart, localDir, segT, mn, mx, t, &n) && t >= 0 && t < segT) {
                    segT = t;
                    res.dummyIndex = int(i);
                    res.group = hb.group;
                    res.normal = Vec3{c * n.x + s * n.y, -s * n.x + c * n.y, n.z};  // back to world space
                    hitDummy = true;
                }
            }
        }
        bestT = travelled + segT;
        if (hitDummy) { res.hitWorld = false; break; }
        res.normal = wt.normal;
        res.hitWorld = wt.fraction < 1.0f;
        res.worldBox = res.hitWorld ? wt.box : -1;
        if (!res.hitWorld || wt.box < 0 || res.penCount >= 2) break;

        // Thickness of the box along the ray.
        const Box& b = world.solids[size_t(wt.box)];
        Vec3 entry = segStart + dir * segT;
        float thick = 1e30f;
        for (int a = 0; a < 3; ++a) {
            if (std::fabs(dir[a]) < 1e-6f) continue;
            float exitPlane = dir[a] > 0 ? b.maxs[a] : b.mins[a];
            thick = std::min(thick, (exitPlane - entry[a]) / dir[a]);
        }
        // Material: wood is easy to shoot through, metal hard, stone in between.
        const float cost = thick * (b.material == kMatWood ? 0.5f : b.material == kMatMetal ? 1.5f : 1.0f);
        if (cost > penLeft) break;
        res.penEntry[res.penCount] = entry;
        res.penExit[res.penCount] = entry + dir * thick;
        res.penNormal[res.penCount] = wt.normal;
        res.penCount++;
        dmgScale *= 1.0f - 0.5f * cost / w.penetration;
        penLeft -= cost;
        travelled += segT + thick + 0.1f;
        segStart = entry + dir * (thick + 0.1f);
    }

    res.start = eye;
    res.end = eye + dir * bestT;
    res.distance = bestT;

    if (res.dummyIndex >= 0) {
        Dummy& d = dummies[res.dummyIndex];
        res.damage = armoredDamage(
            w.damage * hitGroupMultiplier(res.group) * std::pow(w.rangeModifier, bestT / 500.0f) * dmgScale, res.group,
            d.armor, d.helmet);
        d.hp -= res.damage;
        d.flash[res.group] = 0.15f;
        if (d.hp <= 0) {
            res.kill = true;
            // Drill: varied respawn delay so you can't pre-time it.
            d.respawnLeft = d.randomRespawn ? 0.5f + rand01(d.respawns * 31u + 7u) * 0.9f : 1.0f;
        }
    }

    ws.shotCounter++;
    ws.recoilIndex = std::min(ws.recoilIndex + 1.0f, float(w.patternLen - 1));
    return res;
}

bool takeShotTiming(WeaponState& ws, double now) {
    if (now < ws.nextFireTime) return false;
    if (now - ws.nextFireTime > double(kTickDt)) ws.nextFireTime = now;  // not a held, on-time shot: from now
    ws.nextFireTime += double(ws.def->fireInterval);
    return true;
}

Vec3 grenadeThrowVelocity(float viewPitch, float viewYaw, float strength, const Vec3& throwerVelocity) {
    const float pitch = viewPitch < 0 ? -10.0f + viewPitch * (80.0f / 90.0f) : -10.0f + viewPitch * (100.0f / 90.0f);
    return anglesToForward(pitch, viewYaw) * (kNadeThrowSpeed * strength) + throwerVelocity * 1.25f;
}

NadeStep stepGrenade(const World& world, Vec3& pos, Vec3& vel) {
    NadeStep r;
    vel.z -= kNadeGravity * kTickDt;
    const Vec3 next = pos + vel * kTickDt;
    const TraceResult tr = world.traceRay(pos, next);
    if (tr.fraction < 1.0f) {
        const float into = dot(vel, tr.normal);
        vel = (vel - tr.normal * (2.0f * into)) * 0.45f;
        if (tr.normal.z > 0.7f && length(vel) < 20.0f) vel = {};  // comes to rest on the floor
        pos = tr.endpos + tr.normal * 0.1f;
        r.bounced = true;
        r.landed = tr.normal.z > 0.7f;
        r.impactSpeed = -into;
    } else {
        pos = next;
    }
    return r;
}

Vec3 predictGrenade(const World& world, Vec3 pos, Vec3 vel, int type, std::vector<Vec3>* path) {
    const int fuseTicks = int(grenadeFuse(type) * kTickRate + 0.5);
    for (int t = 1; t < kTickRate * 10; ++t) {
        const NadeStep st = stepGrenade(world, pos, vel);
        if (path) path->push_back(pos);
        const bool smokeReady = type != 0 || length(vel) < 1.0f || t > fuseTicks + 4 * kTickRate;
        if ((t >= fuseTicks && smokeReady) || (type == 3 && st.landed)) break;
    }
    return pos;
}
