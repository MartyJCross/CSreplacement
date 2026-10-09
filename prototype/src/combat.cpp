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
    "AK-47", true, 215.0f, 36.0f, 0.98f, 0.1f, 30, 1.92f,
    0.34f, 5.0f, 8.0f, 0.12f, kRiflePattern, 30, true, 24.0f, kWRifle, 1, 0.0f, false, true, 2700, 300,
};

// Semi-auto pistol: strong per-shot kick that climbs fast and recovers fast. Tap, don't spam.
const RecoilStep kPistolPattern[12] = {
    {0.00f, 0.00f}, {1.60f, 0.15f}, {1.50f, -0.25f}, {1.40f, 0.30f}, {1.30f, -0.30f}, {1.20f, 0.25f},
    {1.10f, -0.20f}, {1.00f, 0.20f}, {0.90f, -0.20f}, {0.80f, 0.15f}, {0.70f, -0.15f}, {0.60f, 0.10f},
};

// 30 damage: a headshot kills without a helmet up close (120) but not through one (93), like CS's starting pistols.
const WeaponDef kPistol = {
    "PISTOL", true, 240.0f, 30.0f, 0.91f, 0.15f, 12, 1.76f,
    0.34f, 3.5f, 6.0f, 0.25f, kPistolPattern, 12, false, 0.0f, kWPistol, 1, 0.0f, false, false, 200, 300,
};

// Bolt-action sniper: one-shot body kill, big single kick, slow cycle. Semi-auto.
const RecoilStep kSniperPattern[2] = {{0.0f, 0.0f}, {2.2f, 0.0f}};

const WeaponDef kSniper = {
    "AWP", true, 200.0f, 115.0f, 0.99f, 1.46f, 5, 2.88f,
    0.34f, 8.0f, 12.0f, 0.0f, kSniperPattern, 2, false, 40.0f, kWSniper, 1, 0.0f, false, true, 4750, 100,
    0.975f, true, 4.5f,  // a body shot kills through kevlar (112), like CS
};

const WeaponDef kKnife = {
    "KNIFE", false, 250.0f, 0, 1, 1, 0, 0, 0.34f, 0, 0, 0, nullptr, 0, false, 0.0f, kWKnife, 1, 0.0f, false, false, 0, 1500,
};

// Dual Berettas: two pistols, 30 rounds, fast taps with a small kick each, alternating hands. 32 damage: no
// one-tap through a helmet (99 at point blank), and it falls off quickly with range.
const RecoilStep kBerettasPattern[15] = {
    {0.00f, 0.00f}, {0.60f, 0.12f}, {0.55f, -0.18f}, {0.55f, 0.20f}, {0.50f, -0.20f}, {0.50f, 0.18f},
    {0.45f, -0.18f}, {0.45f, 0.16f}, {0.40f, -0.16f}, {0.40f, 0.14f}, {0.35f, -0.14f}, {0.35f, 0.12f},
    {0.30f, -0.12f}, {0.30f, 0.10f}, {0.25f, -0.10f},
};
const WeaponDef kBerettas = {
    "DUAL BERETTAS", true, 240.0f, 32.0f, 0.79f, 0.12f, 30, 3.04f,
    0.34f, 3.5f, 6.0f, 0.20f, kBerettasPattern, 15, false, 6.0f, kWBerettas, 1, 0.0f, false, false, 300, 300,
};

// Deagle: 63 damage, a headshot through a helmet kills from anywhere on the map (195 up close, still over
// 100 at 6,500 units), slow to fire and a big kick: hit the first one.
const RecoilStep kDeaglePattern[7] = {
    {0.00f, 0.00f}, {3.20f, 0.30f}, {3.00f, -0.35f}, {2.80f, 0.40f}, {2.60f, -0.40f}, {2.40f, 0.30f}, {2.20f, -0.30f},
};
const WeaponDef kDeagle = {
    "DEAGLE", true, 230.0f, 63.0f, 0.95f, 0.225f, 7, 1.76f,
    0.34f, 6.0f, 9.0f, 0.60f, kDeaglePattern, 7, false, 20.0f, kWDeagle, 1, 0.0f, false, false, 700, 300,
};

// Nova: pump shotgun, 9 pellets of 26 in a fixed pattern, deadly close and weak far; loads shell by shell.
const RecoilStep kNovaPattern[2] = {{0.0f, 0.0f}, {3.0f, 0.2f}};
const WeaponDef kNova = {
    "NOVA", true, 220.0f, 26.0f, 0.70f, 0.88f, 8, 0.4f,
    0.34f, 6.0f, 9.0f, 0.0f, kNovaPattern, 2, false, 0.0f, kWNova, 9, 2.7f, true, true, 1050, 900,
};

// XM1014: the auto shotgun. 6 pellets of 20, a shot every 0.35 s while you hold it, 7 shells loaded one by
// one; a little wider than the Nova.
const RecoilStep kXmPattern[7] = {
    {0.0f, 0.0f}, {1.9f, 0.15f}, {1.7f, -0.2f}, {1.6f, 0.2f}, {1.5f, -0.2f}, {1.4f, 0.15f}, {1.3f, -0.15f},
};
const WeaponDef kXm1014 = {
    "XM1014", true, 215.0f, 20.0f, 0.70f, 0.35f, 7, 0.36f,
    0.34f, 6.0f, 9.0f, 0.0f, kXmPattern, 7, true, 0.0f, kWXm1014, 6, 2.9f, true, true, 2000, 900, 0.80f,
};

// MAC-10: fast-firing SMG (800 rounds a minute), modest damage, the spray climbs then wanders.
const RecoilStep kMac10Pattern[30] = {
    {0.00f, 0.00f}, {0.55f, 0.05f}, {0.65f, 0.10f}, {0.70f, 0.10f}, {0.70f, 0.15f}, {0.65f, 0.20f},
    {0.55f, 0.25f}, {0.45f, 0.25f}, {0.35f, 0.30f}, {0.25f, -0.40f}, {0.20f, -0.55f}, {0.15f, -0.60f},
    {0.15f, -0.55f}, {0.10f, -0.45f}, {0.10f, 0.35f}, {0.10f, 0.55f}, {0.05f, 0.60f}, {0.05f, 0.55f},
    {0.05f, 0.45f}, {0.05f, -0.30f}, {0.00f, -0.45f}, {0.05f, -0.45f}, {0.00f, 0.30f}, {0.05f, 0.40f},
    {0.00f, 0.35f}, {0.05f, -0.30f}, {0.00f, -0.35f}, {0.05f, 0.25f}, {0.00f, 0.30f}, {0.05f, -0.20f},
};
const WeaponDef kMac10 = {
    "MAC-10", true, 240.0f, 29.0f, 0.80f, 0.075f, 30, 2.08f,
    0.34f, 5.0f, 8.0f, 0.15f, kMac10Pattern, 30, true, 10.0f, kWMac10, 1, 0.0f, false, true, 1050, 600,
};

// M4A1-S: the suppressed CT rifle. 38 damage, 600 rounds a minute, 20 rounds, the gentlest spray of the
// rifles; a headshot kills through a helmet up close (106) but not from far away; kevlar keeps 70%. Quiet:
// bots hear it from 1,100 units, not 2,200.
const RecoilStep kM4Pattern[20] = {
    {0.00f, 0.00f}, {0.65f, 0.03f}, {0.78f, 0.07f}, {0.86f, 0.03f}, {0.90f, 0.07f}, {0.86f, 0.10f},
    {0.72f, 0.14f}, {0.60f, 0.14f}, {0.46f, 0.17f}, {0.26f, -0.34f}, {0.21f, -0.47f}, {0.17f, -0.53f},
    {0.13f, -0.51f}, {0.10f, -0.42f}, {0.07f, -0.34f}, {0.07f, 0.26f}, {0.07f, 0.44f}, {0.03f, 0.53f},
    {0.03f, 0.52f}, {0.03f, 0.46f},
};
const WeaponDef kM4A1S = {
    "M4A1-S", true, 225.0f, 38.0f, 0.99f, 0.1f, 20, 2.48f,
    0.34f, 4.0f, 8.0f, 0.09f, kM4Pattern, 20, true, 24.0f, kWM4A1S, 1, 0.0f, false, true, 2900, 300, 0.70f,
};

// Galil AR: the cheap rifle. 30 damage, 35 rounds, the AK's climb a little softer and longer.
const RecoilStep kGalilPattern[35] = {
    {0.00f, 0.00f}, {0.85f, 0.05f}, {1.00f, 0.08f}, {1.15f, 0.05f}, {1.20f, 0.10f}, {1.10f, 0.14f},
    {0.95f, 0.18f}, {0.80f, 0.18f}, {0.60f, 0.22f}, {0.30f, -0.50f}, {0.25f, -0.68f}, {0.20f, -0.76f},
    {0.18f, -0.72f}, {0.12f, -0.62f}, {0.10f, -0.50f}, {0.10f, 0.36f}, {0.08f, 0.62f}, {0.05f, 0.76f},
    {0.05f, 0.76f}, {0.05f, 0.66f}, {0.05f, 0.52f}, {0.00f, 0.36f}, {0.05f, -0.32f}, {0.00f, -0.45f},
    {0.05f, -0.40f}, {0.00f, 0.28f}, {0.05f, 0.40f}, {0.00f, 0.32f}, {0.05f, -0.28f}, {0.00f, -0.28f},
    {0.04f, 0.24f}, {0.00f, 0.30f}, {0.04f, -0.22f}, {0.00f, -0.26f}, {0.04f, 0.20f},
};
const WeaponDef kGalil = {
    "GALIL AR", true, 215.0f, 30.0f, 0.98f, 0.09f, 35, 2.4f,
    0.34f, 5.0f, 8.0f, 0.12f, kGalilPattern, 35, true, 24.0f, kWGalil, 1, 0.0f, false, true, 1800, 300,
};

// SSG 08 (the scout): a light bolt-action sniper. 88 damage: a headshot kills through a helmet anywhere, a body
// shot doesn't; you run with it nearly as fast as with a knife.
const RecoilStep kSsgPattern[2] = {{0.0f, 0.0f}, {1.6f, 0.0f}};
const WeaponDef kSsg08 = {
    "SSG 08", true, 230.0f, 88.0f, 0.98f, 1.25f, 10, 2.96f,
    0.34f, 6.0f, 3.0f, 0.0f, kSsgPattern, 2, false, 30.0f, kWSsg08, 1, 0.0f, false, true, 1700, 300,
    0.85f, true, 3.0f,
};

// UMP-45: a heavy, slower SMG. 35 damage that falls off fast, 25 rounds; kevlar keeps 65%.
const RecoilStep kUmpPattern[25] = {
    {0.00f, 0.00f}, {0.60f, 0.05f}, {0.70f, 0.08f}, {0.75f, 0.05f}, {0.75f, 0.10f}, {0.70f, 0.12f},
    {0.60f, 0.15f}, {0.50f, 0.20f}, {0.40f, 0.25f}, {0.30f, 0.30f}, {0.20f, -0.35f}, {0.15f, -0.50f},
    {0.15f, -0.55f}, {0.10f, -0.50f}, {0.10f, -0.40f}, {0.10f, 0.35f}, {0.05f, 0.50f}, {0.05f, 0.50f},
    {0.05f, 0.40f}, {0.05f, -0.30f}, {0.00f, -0.40f}, {0.05f, -0.35f}, {0.00f, 0.30f}, {0.05f, 0.35f},
    {0.00f, -0.25f},
};
const WeaponDef kUmp45 = {
    "UMP-45", true, 230.0f, 35.0f, 0.85f, 0.09f, 25, 2.8f,
    0.34f, 5.0f, 8.0f, 0.15f, kUmpPattern, 25, true, 10.0f, kWUmp45, 1, 0.0f, false, true, 1200, 600, 0.65f,
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
    static const WeaponDef kGrenade = {"SMOKE", false, 245.0f, 0, 1, 1, 0, 0, 0.34f, 0, 0, 0, nullptr, 0, false,
                                       0.0f, kWGrenade, 1, 0.0f, false, false, 0, 300};
    return kGrenade;
}
const WeaponDef& pistolDef() { return kPistol; }
const WeaponDef& sniperDef() { return kSniper; }
const WeaponDef& berettasDef() { return kBerettas; }
const WeaponDef& deagleDef() { return kDeagle; }
const WeaponDef& novaDef() { return kNova; }
const WeaponDef& mac10Def() { return kMac10; }
const WeaponDef& weaponDef(int id) {
    switch (id) {
        case kWPistol: return kPistol;
        case kWKnife: return kKnife;
        case kWGrenade: return grenadeDef();
        case kWSniper: return kSniper;
        case kWBerettas: return kBerettas;
        case kWDeagle: return kDeagle;
        case kWNova: return kNova;
        case kWMac10: return kMac10;
        case kWM4A1S: return kM4A1S;
        case kWXm1014: return kXm1014;
        case kWGalil: return kGalil;
        case kWSsg08: return kSsg08;
        case kWUmp45: return kUmp45;
        default: return kRifle;
    }
}

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
    if (w.scope && !ws.scoped) inacc += w.noscopeInaccuracy;  // a noscope, like CS
    if (ducked && onGround) inacc *= 0.8f;
    return inacc;
}

void decayRecoil(WeaponState& ws, float dt) {
    // dI/dt = -(3 + 3.5 I): one tap recovers in ~0.2 s, a full spray in ~1 s.
    ws.recoilIndex = std::max(0.0f, ws.recoilIndex - dt * (3.0f + 3.5f * ws.recoilIndex));
}

namespace {
float g_modelScale = 1.0f;
std::vector<Hitbox> g_scaledBoxes;
}  // namespace

float modelScale() { return g_modelScale; }
float dummyEyeZ(float crouch) { return crouchZ(64.0f * g_modelScale, crouch); }

const std::vector<Hitbox>& baseHitboxes();
void setModelScale(float scale) {
    g_modelScale = std::clamp(scale, 0.5f, 2.0f);
    g_scaledBoxes = baseHitboxes();
    for (Hitbox& b : g_scaledBoxes) {
        b.mins = b.mins * g_modelScale;
        b.maxs = b.maxs * g_modelScale;
    }
}

const std::vector<Hitbox>& dummyHitboxes() {
    if (g_scaledBoxes.empty()) setModelScale(g_modelScale);
    return g_scaledBoxes;
}

const std::vector<Hitbox>& baseHitboxes() {
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
    const float kHip = 34.0f * g_modelScale, kDrop = 18.0f * g_modelScale;
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

float armoredDamage(float damage, HitGroup group, float armor, bool helmet, float ratio) {
    if (armor <= 0 || group == kLegs || (group == kHead && !helmet)) return damage;
    return damage * ratio;
}

float damageAt(const WeaponDef& w, float dist) { return w.damage * std::pow(w.rangeModifier, dist / 500.0f); }

void updateDummy(Dummy& d, float dt) {
    d.prevPos = d.pos;
    d.prevYaw = d.yaw;
    d.prevCrouch = d.crouch;
    d.prevPitch = d.pitch;
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

namespace {

// One bullet along `dir`: walls (and wallbangs), the first dummy it meets, and its damage on that dummy.
ShotResult traceShot(const WeaponDef& w, const Vec3& eye, const Vec3& dir, const World& world,
                     std::vector<Dummy>& dummies, const std::vector<Vec3>& dummyRenderPos) {
    ShotResult res;
    // Trace in segments: hit a wall -> if it is thin enough for this weapon, pass through (losing damage) and
    // keep going; up to two walls. Hit a body -> it takes the bullet, and a gun that goes through walls goes
    // through bodies too (a body counts as kBodyThickness of wall), up to three people in a line: a collat.
    const float kRange = 8192.0f;
    float bestT = kRange, travelled = 0, dmgScale = 1.0f, penLeft = w.penetration;
    Vec3 segStart = eye;
    int bodies = 0, hitIdx[3] = {-1, -1, -1};
    for (int seg = 0; seg < 6; ++seg) {
        float remaining = kRange - travelled;
        TraceResult wt = world.traceRay(segStart, segStart + dir * remaining);
        float segT = wt.fraction * remaining;
        int who = -1;
        HitGroup group = kChest;
        Vec3 normal;
        for (size_t i = 0; i < dummies.size(); ++i) {
            if (!dummies[i].alive() || dummies[i].friendly) continue;  // teammates: your bullets pass through
            if (int(i) == hitIdx[0] || int(i) == hitIdx[1] || int(i) == hitIdx[2]) continue;  // already through them
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
                    who = int(i);
                    group = hb.group;
                    normal = Vec3{c * n.x + s * n.y, -s * n.x + c * n.y, n.z};  // back to world space
                }
            }
        }
        if (who >= 0) {
            const float at = travelled + segT;
            Dummy& d = dummies[size_t(who)];
            const float dmg = armoredDamage(
                w.damage * hitGroupMultiplier(group) * std::pow(w.rangeModifier, at / 500.0f) * dmgScale, group, d.armor,
                d.helmet, w.armorRatio);
            d.hp -= dmg;
            d.flash[group] = 0.15f;
            d.hitDir = dir;
            const bool kill = d.hp <= 0;
            if (kill) {
                d.lostHelmet = group == kHead;
                // Drill: varied respawn delay so you can't pre-time it.
                d.respawnLeft = d.randomRespawn ? 0.5f + rand01(d.respawns * 31u + 7u) * 0.9f : 1.0f;
            }
            if (bodies == 0) {
                bestT = at;
                res.dummyIndex = who;
                res.group = group;
                res.normal = normal;
                res.damage = dmg;
                res.kill = kill;
                res.hitWorld = false;
            } else {
                res.collat[res.collats++] = {who, group, dmg, kill, eye + dir * at, normal, at};
            }
            hitIdx[bodies++] = who;
            // Through the body? Only guns that go through walls, while there's penetration left.
            if (bodies >= 3 || w.penetration <= 0 || penLeft < kBodyThickness) break;
            dmgScale *= 1.0f - 0.5f * kBodyThickness / w.penetration;
            penLeft -= kBodyThickness;
            travelled += segT + 0.1f;
            segStart = eye + dir * travelled;
            continue;
        }
        if (bodies == 0) {
            bestT = travelled + segT;
            res.normal = wt.normal;
            res.hitWorld = wt.fraction < 1.0f;
            res.worldBox = res.hitWorld ? wt.box : -1;
        }
        if (wt.fraction >= 1.0f || wt.box < 0 || res.penCount >= 2) break;

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
    return res;
}

// The aim (view + recoil + deterministic spread) for the shot about to be fired.
void shotAim(const WeaponState& ws, float viewPitch, float viewYaw, float horizSpeed, bool onGround, bool ducked,
             float& pitch, float& yaw) {
    const WeaponDef& w = *ws.def;
    RecoilStep punch = recoilAt(w, ws.recoilIndex);
    float inacc = currentInaccuracy(ws, horizSpeed, onGround, ducked);
    uint32_t seed = ws.shotCounter * 2654435761u;
    float theta = rand01(seed) * 2.0f * kPi;
    float radius = rand01(seed ^ 0x9e3779b9u) * inacc;
    pitch = viewPitch - punch.up + std::sin(theta) * radius;
    yaw = viewYaw - punch.right + std::cos(theta) * radius;
}

void afterShot(WeaponState& ws) {
    ws.shotCounter++;
    ws.recoilIndex = std::min(ws.recoilIndex + 1.0f, float(ws.def->patternLen - 1));
}

}  // namespace

int collatResults(const ShotResult& r, ShotResult* out, int n, int cap) {
    for (int k = 0; k < r.collats && n < cap; ++k) {
        const ShotResult::BodyHit& h = r.collat[k];
        ShotResult c;
        c.start = r.start;
        c.end = h.at;
        c.normal = h.normal;
        c.dummyIndex = h.dummyIndex;
        c.group = h.group;
        c.damage = h.damage;
        c.kill = h.kill;
        c.distance = h.distance;
        c.sprayIndex = r.sprayIndex;
        c.penCount = r.penCount;  // (walls before it count as a wallbang)
        c.isCollat = true;
        out[n++] = c;
    }
    return n;
}

ShotResult fireBullet(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed,
                      bool onGround, bool ducked, const World& world, std::vector<Dummy>& dummies,
                      const std::vector<Vec3>& dummyRenderPos) {
    float pitch = 0, yaw = 0;
    shotAim(ws, viewPitch, viewYaw, horizSpeed, onGround, ducked, pitch, yaw);
    ShotResult res = traceShot(*ws.def, eye, anglesToForward(pitch, yaw), world, dummies, dummyRenderPos);
    res.sprayIndex = int(ws.recoilIndex);
    afterShot(ws);
    return res;
}

RecoilStep pelletOffset(const WeaponDef& w, int k, uint32_t seed) {
    if (w.pellets <= 1) return {0, 0};
    // A random direction, and a random distance that favours the middle (uniform in radius, not in area).
    const uint32_t base = seed * 2654435761u + uint32_t(k) * 40503u + 0x9e37u;
    const float ang = rand01(base) * 2.0f * kPi, r = w.pelletSpread * rand01(base ^ 0x5bd1e995u);
    return {std::sin(ang) * r, std::cos(ang) * r};
}

int firePellets(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed, bool onGround,
                bool ducked, const World& world, std::vector<Dummy>& dummies, const std::vector<Vec3>& dummyRenderPos,
                ShotResult (&out)[kMaxPellets]) {
    float pitch = 0, yaw = 0;
    shotAim(ws, viewPitch, viewYaw, horizSpeed, onGround, ducked, pitch, yaw);
    const int n = std::clamp(ws.def->pellets, 1, kMaxPellets);
    const uint32_t seed = ws.shotCounter;
    for (int k = 0; k < n; ++k) {
        const RecoilStep o = pelletOffset(*ws.def, k, seed);
        out[k] = traceShot(*ws.def, eye, anglesToForward(pitch - o.up, yaw + o.right), world, dummies, dummyRenderPos);
        out[k].sprayIndex = int(ws.recoilIndex);
    }
    afterShot(ws);
    return n;
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
