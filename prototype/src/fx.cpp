#include "fx.h"
#include <algorithm>
#include <cmath>

namespace {

// Colours.
constexpr uint32_t kMetal = 0x2b2e33, kMetalLight = 0x41454c, kWood = 0x7a5230, kWoodDark = 0x5a3c23,
                   kGlove = 0x2a2a2a, kSleeve = 0x4f5b3f;

struct Part { Vec3 mins, maxs; uint32_t color; };

// Rifle in gun-local space: x right, y up, z back (muzzle points to -z). Units ~ inches.
const Part kRifleBody[] = {
    {{-0.9f, -1.1f, -6.0f}, {0.9f, 1.1f, 6.0f}, kMetal},          // receiver
    {{-0.8f, 1.1f, -5.0f}, {0.8f, 1.6f, 5.5f}, kMetalLight},      // dust cover
    {{-0.5f, 1.6f, -4.5f}, {0.5f, 2.2f, -3.5f}, kMetal},          // rear sight
    {{-1.05f, -1.2f, -13.0f}, {1.05f, 0.6f, -6.0f}, kWood},       // lower handguard
    {{-0.7f, 0.6f, -12.5f}, {0.7f, 1.3f, -6.0f}, kWoodDark},      // upper handguard
    {{-0.32f, -0.1f, -22.0f}, {0.32f, 0.55f, -13.0f}, kMetal},    // barrel
    {{-0.45f, 0.55f, -20.5f}, {0.45f, 2.0f, -19.5f}, kMetal},     // front sight
    {{-0.5f, -0.3f, -23.5f}, {0.5f, 0.75f, -22.0f}, kMetalLight}, // muzzle brake
    {{-0.2f, -2.0f, 0.2f}, {0.2f, -1.1f, 2.8f}, kMetal},          // trigger guard
    {{-0.65f, -4.6f, 2.6f}, {0.65f, -1.1f, 4.6f}, kWoodDark},     // pistol grip
    {{-0.75f, -1.6f, 6.0f}, {0.75f, 1.0f, 15.0f}, kWood},         // stock
    {{-0.75f, -3.2f, 10.0f}, {0.75f, -1.6f, 15.0f}, kWood},       // stock heel
    {{-1.3f, -4.2f, 2.2f}, {1.4f, -1.4f, 5.0f}, kGlove},          // right hand
    {{0.6f, -6.5f, 5.0f}, {3.6f, -3.2f, 14.0f}, kSleeve},         // right forearm
    {{-1.5f, -2.4f, -11.5f}, {1.5f, -0.6f, -8.0f}, kGlove},       // left hand
    {{-5.5f, -6.5f, -9.5f}, {-1.2f, -2.6f, -3.0f}, kSleeve},      // left forearm
};
const Part kRifleMag[] = {
    {{-0.65f, -5.0f, -4.2f}, {0.65f, -1.1f, -1.4f}, kMetal},
    {{-0.65f, -8.2f, -5.6f}, {0.65f, -4.6f, -2.6f}, kMetal},
};
constexpr uint32_t kPolymer = 0x26282b;
const Part kPistolBody[] = {
    {{-0.55f, 0.2f, -8.5f}, {0.55f, 1.5f, 1.0f}, kMetal},         // slide
    {{-0.5f, -0.6f, -7.5f}, {0.5f, 0.2f, 0.5f}, kPolymer},        // frame
    {{-0.25f, 0.5f, -9.0f}, {0.25f, 1.1f, -8.5f}, kMetalLight},   // barrel crown
    {{-0.1f, 1.5f, -8.0f}, {0.1f, 1.8f, -7.6f}, kMetal},          // front sight
    {{-0.4f, 1.5f, 0.2f}, {0.4f, 1.8f, 0.8f}, kMetal},            // rear sight
    {{-0.55f, -4.2f, -1.0f}, {0.55f, -0.6f, 1.2f}, kPolymer},     // grip
    {{-0.15f, -1.4f, -3.0f}, {0.15f, -0.6f, -0.8f}, kPolymer},    // trigger guard
    {{-1.2f, -4.0f, -1.4f}, {1.3f, -1.0f, 1.6f}, kGlove},         // right hand
    {{-1.6f, -4.2f, -2.0f}, {-0.4f, -1.2f, 1.2f}, kGlove},        // left hand (support grip)
    {{0.4f, -6.5f, 1.6f}, {3.2f, -3.5f, 11.0f}, kSleeve},         // right forearm
    {{-4.5f, -6.8f, 1.0f}, {-1.0f, -3.8f, 10.0f}, kSleeve},       // left forearm
};
const Part kPistolMag[] = {
    {{-0.45f, -4.5f, -0.8f}, {0.45f, -0.7f, 1.0f}, kMetalLight},
};

constexpr uint32_t kOlive = 0x4c5a35, kOliveDark = 0x3a4528;
const Part kSniperBody[] = {
    {{-0.8f, -1.0f, -5.0f}, {0.8f, 1.0f, 6.0f}, kOlive},             // receiver
    {{-1.0f, -1.2f, -14.0f}, {1.0f, 0.4f, -5.0f}, kOlive},           // forend
    {{-0.35f, -0.1f, -30.0f}, {0.35f, 0.55f, -14.0f}, kMetal},       // barrel
    {{-0.55f, -0.3f, -32.0f}, {0.55f, 0.75f, -30.0f}, kMetalLight},  // muzzle brake
    {{-0.9f, -2.6f, 6.0f}, {0.9f, 1.2f, 17.0f}, kOlive},             // stock
    {{-0.65f, -4.2f, 5.0f}, {0.65f, -1.0f, 7.0f}, kOliveDark},       // grip
    {{-0.75f, 1.6f, -6.0f}, {0.75f, 3.0f, 5.0f}, kMetal},            // scope tube
    {{-1.0f, 1.4f, -8.0f}, {1.0f, 3.2f, -6.0f}, kMetal},             // objective bell
    {{-0.9f, 1.5f, 5.0f}, {0.9f, 3.1f, 6.5f}, kMetal},               // eyepiece
    {{-0.4f, 1.0f, -3.0f}, {0.4f, 1.6f, -2.0f}, kMetalLight},        // front mount
    {{-0.4f, 1.0f, 2.0f}, {0.4f, 1.6f, 3.0f}, kMetalLight},          // rear mount
    {{0.8f, 0.2f, 2.0f}, {2.2f, 0.6f, 2.6f}, kMetalLight},           // bolt handle
    {{-1.3f, -4.0f, 4.5f}, {1.4f, -1.2f, 7.5f}, kGlove},             // right hand
    {{0.6f, -6.5f, 7.5f}, {3.6f, -3.2f, 16.0f}, kSleeve},            // right forearm
    {{-1.5f, -2.4f, -12.0f}, {1.5f, -0.6f, -8.5f}, kGlove},          // left hand
    {{-5.5f, -6.5f, -10.0f}, {-1.2f, -2.6f, -3.5f}, kSleeve},        // left forearm
};
const Part kSniperMag[] = {
    {{-0.6f, -3.2f, -3.0f}, {0.6f, -1.0f, 0.5f}, kMetal},
};

// Butterfly knife, built around the pivot pin at the origin: open, the blade points -z and both
// handles point back (+z). Each piece pivots on that pin, which is what makes the flips work.
const Part kButterflyBlade[] = {  // a "fade" finish: violet at the base, pink, then gold at the tip
    {{-0.09f, -0.5f, -3.0f}, {0.09f, 0.75f, -0.2f}, 0x6f5bd0},
    {{-0.09f, -0.45f, -5.8f}, {0.09f, 0.75f, -3.0f}, 0xc95fb0},
    {{-0.09f, -0.35f, -8.0f}, {0.09f, 0.72f, -5.8f}, 0xf0b85a},
    {{-0.08f, -0.05f, -9.3f}, {0.08f, 0.62f, -8.0f}, 0xf6d878},
    {{-0.12f, -0.25f, -0.2f}, {0.12f, 0.35f, 0.25f}, kMetalLight},  // tang / pivot
};
const Part kButterflySafe[] = {  // the handle that stays in the hand
    {{-0.48f, -0.62f, 0.15f}, {-0.1f, 0.62f, 5.9f}, 0x2b2e34},
    {{-0.5f, -0.2f, 0.4f}, {-0.08f, 0.2f, 0.9f}, kMetalLight},
    {{-0.5f, -0.2f, 5.2f}, {-0.08f, 0.2f, 5.7f}, kMetalLight},
};
const Part kButterflyBite[] = {  // the handle that swings round
    {{0.1f, -0.62f, 0.15f}, {0.48f, 0.62f, 5.9f}, 0x3c4048},
    {{0.08f, -0.28f, 5.9f}, {0.5f, 0.28f, 6.5f}, kMetalLight},  // latch
};
const Part kButterflyHand[] = {
    {{-1.25f, -1.4f, 1.6f}, {0.9f, 1.4f, 4.8f}, kGlove},  // hand around the safe handle
    {{-1.6f, -2.2f, 4.8f}, {1.6f, 1.6f, 14.0f}, kSleeve}, // forearm
};

const Part kGrenade[] = {
    {{-1.3f, -1.3f, -2.6f}, {1.3f, 1.3f, 2.4f}, 0x4f5a4a},      // canister
    {{-1.35f, -1.35f, -1.0f}, {1.35f, 1.35f, -0.4f}, 0x2a2e28}, // band
    {{-0.6f, -0.6f, -3.6f}, {0.6f, 0.6f, -2.6f}, kMetal},       // fuse
    {{-0.25f, 0.6f, -3.4f}, {0.25f, 1.6f, 1.2f}, kMetal},       // spoon
    {{-1.6f, -1.8f, -0.4f}, {1.6f, 1.2f, 3.0f}, kGlove},        // hand
    {{-1.6f, -2.2f, 3.0f}, {1.6f, 1.6f, 13.0f}, kSleeve},       // forearm
};

// Karambit / talon: a claw blade curving down from the handle, with a finger ring at the back. The
// ring centre (0, -0.1, 5.4) is what they spin around.
const Part kKarambit[] = {
    {{-0.35f, -0.6f, 0.0f}, {0.35f, 0.6f, 4.5f}, 0x2d2a26},     // handle
    {{-0.3f, 0.3f, 4.5f}, {0.3f, 0.7f, 6.3f}, 0x2d2a26},        // ring
    {{-0.3f, -0.9f, 4.5f}, {0.3f, -0.5f, 6.3f}, 0x2d2a26},
    {{-0.3f, -0.9f, 5.9f}, {0.3f, 0.7f, 6.3f}, 0x2d2a26},
    {{-0.3f, -0.9f, 4.5f}, {0.3f, 0.7f, 4.9f}, kMetalLight},
    {{-0.08f, -0.3f, -2.2f}, {0.08f, 0.7f, 0.0f}, 0x3c5be0},    // blade, sapphire fading darker
    {{-0.08f, -1.0f, -3.8f}, {0.08f, 0.4f, -2.2f}, 0x2f49c0},
    {{-0.08f, -1.9f, -5.0f}, {0.08f, -0.3f, -3.8f}, 0x2638a0},
    {{-0.07f, -2.6f, -5.8f}, {0.07f, -1.4f, -5.0f}, 0x1c2a80},
};
const Part kTalon[] = {
    {{-0.4f, -0.65f, 0.0f}, {0.4f, 0.65f, 5.0f}, 0xd8cfb8},     // ivory handle
    {{-0.3f, 0.3f, 5.0f}, {0.3f, 0.75f, 7.0f}, 0xb8a46a},       // brass ring
    {{-0.3f, -1.0f, 5.0f}, {0.3f, -0.55f, 7.0f}, 0xb8a46a},
    {{-0.3f, -1.0f, 6.55f}, {0.3f, 0.75f, 7.0f}, 0xb8a46a},
    {{-0.09f, -0.3f, -2.6f}, {0.09f, 0.8f, 0.0f}, 0xc3c8cf},    // long steel claw
    {{-0.09f, -1.1f, -4.6f}, {0.09f, 0.5f, -2.6f}, 0xb3b8c0},
    {{-0.09f, -2.2f, -6.2f}, {0.09f, -0.4f, -4.6f}, 0xa6abb3},
    {{-0.08f, -3.1f, -7.2f}, {0.08f, -1.7f, -6.2f}, 0x9aa0a8},
};
const Part kM9[] = {
    {{-0.5f, -0.7f, 0.2f}, {0.5f, 0.7f, 5.2f}, 0x1e1f22},       // handle
    {{-0.55f, -0.6f, 5.2f}, {0.55f, 0.6f, 5.8f}, kMetalLight},  // pommel
    {{-0.6f, -1.2f, -0.2f}, {0.6f, 1.3f, 0.2f}, kMetalLight},   // guard
    {{-0.12f, -0.6f, -8.5f}, {0.12f, 0.8f, -0.2f}, 0xb9c0c8},   // blade
    {{-0.1f, 0.8f, -6.0f}, {0.1f, 1.05f, -1.0f}, 0x8a9098},     // saw-back
    {{-0.1f, -0.2f, -10.0f}, {0.1f, 0.6f, -8.5f}, 0xb9c0c8},    // tip
};
const Part kKnifeHand[] = {
    {{-1.25f, -1.4f, 1.0f}, {1.25f, 1.4f, 4.3f}, kGlove},
    {{-1.6f, -2.2f, 4.3f}, {1.6f, 1.6f, 14.0f}, kSleeve},
};

// Gun finishes: recolour the metal / light metal / furniture / dark furniture / polymer roles.
// 0 = factory colours.
struct Finish { uint32_t metal, light, wood, woodDark, polymer; };
const Finish kFinishes[] = {
    {0, 0, 0, 0, 0},
    {0x1b1b1e, 0x34343a, 0x9e1a22, 0x5e0f14, 0x9e1a22},  // crimson
    {0xdedfe2, 0xf08a2a, 0xeceef0, 0x2a2b2e, 0xeceef0},  // arctic
    {0x3b4a2c, 0x5c6b3a, 0x6f7f42, 0x2f3a22, 0x4c5a35},  // jungle
    {0x2a2418, 0xd8b24a, 0xc9a23e, 0x3a2f18, 0xc9a23e},  // gold
};
constexpr int kFinishCount = int(sizeof(kFinishes) / sizeof(kFinishes[0]));

uint32_t refinish(uint32_t c, int finish) {
    if (finish <= 0 || finish >= kFinishCount) return c;
    const Finish& f = kFinishes[finish];
    if (c == kMetal) return f.metal;
    if (c == kMetalLight) return f.light;
    if (c == kWood || c == kOlive) return f.wood;
    if (c == kWoodDark || c == kOliveDark) return f.woodDark;
    if (c == kPolymer) return f.polymer;
    return c;  // gloves, sleeves
}

template <size_t N>
std::vector<BoxInstance> toBoxes(const Part (&parts)[N], int finish = 0) {
    std::vector<BoxInstance> out;
    for (const Part& p : parts) out.push_back(makeBox(p.mins, p.maxs, refinish(p.color, finish), false));
    return out;
}

// Inspect keyframes: offsets from the resting pose, eased between keys.
struct Pose { float t, x, y, z, yaw, pitch, roll; };
const Pose kRifleInspect[] = {
    {0.00f, 0, 0, 0, 0, 0, 0},
    {0.55f, -2.8f, 2.0f, 1.8f, 42, 6, -38},    // swing up to show the left side
    {1.35f, -2.4f, 2.2f, 1.6f, 34, 3, -30},    // ...and admire it
    {1.95f, -1.2f, 1.4f, 0.8f, -22, 12, 42},   // roll over to the right side
    {2.55f, -1.0f, 1.6f, 0.6f, -18, 9, 36},
    {3.05f, -0.6f, 0.4f, 0.4f, 4, -16, 10},    // tip down: look at the mag
    {3.60f, 0, 0, 0, 0, 0, 0},
};
const Pose kPistolInspect[] = {
    {0.00f, 0, 0, 0, 0, 0, 0},
    {0.45f, -2.0f, 1.8f, 1.4f, 40, 4, -34},
    {1.10f, -1.8f, 2.0f, 1.2f, 30, 2, -28},
    {1.60f, -1.0f, 1.2f, 0.6f, -24, 10, 44},
    {2.20f, -0.8f, 1.4f, 0.5f, -18, 8, 38},
    {2.60f, -0.4f, 0.3f, 0.2f, 2, -12, 8},
    {3.00f, 0, 0, 0, 0, 0, 0},
};

float smoother(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * (x * (x * 6 - 15) + 10);
}

template <size_t N>
Pose samplePose(const Pose (&keys)[N], float t) {
    if (t <= keys[0].t) return keys[0];
    for (size_t k = 1; k < N; ++k) {
        if (t > keys[k].t) continue;
        const Pose &a = keys[k - 1], &b = keys[k];
        float s = smoother((t - a.t) / (b.t - a.t));
        auto mix = [&](float p, float q) { return p + (q - p) * s; };
        return {t, mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.yaw, b.yaw), mix(a.pitch, b.pitch), mix(a.roll, b.roll)};
    }
    return keys[N - 1];
}

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}
float ramp(float t, float a, float b) { return smooth01((t - a) / (b - a)); }

Mat4 cameraBasis(const Vec3& eye, float pitchDeg, float yawDeg) {
    Vec3 f = anglesToForward(pitchDeg, yawDeg), r = yawToRight(yawDeg), u = cross(r, f);
    return fromBasis(r, u, -f, eye);  // camera-local: x right, y up, z back
}

}  // namespace

void ViewModel::onShot(uint32_t seed) {
    // The kick builds over a spray: the gun climbs and shakes harder the longer you hold it.
    shotsInRow_ = sinceShot_ < 0.25f ? shotsInRow_ + 1 : 1;
    sinceShot_ = 0;
    float build = std::min(float(shotsInRow_), 10.0f) / 10.0f;
    float r1 = float(seed % 1000) / 1000.0f - 0.5f, r2 = float((seed / 1000) % 1000) / 1000.0f - 0.5f;
    kickBack_ = std::min(kickBack_ + 2.0f + 0.6f * build, 4.4f);
    kickPitch_ = std::min(kickPitch_ + 2.4f + 1.0f * build, 7.5f);
    kickYaw_ += r1 * (0.7f + 0.8f * build);
    kickRoll_ += r2 * (2.0f + 3.0f * build);
    flashLeft_ = 0.045f;
    flashSeed_ = seed;
    inspectT_ = -1;
}

void ViewModel::onLand(float fallSpeed) { landVel_ -= std::min(fallSpeed / 400.0f, 1.0f) * 28.0f; }

void ViewModel::onDraw(ViewWeapon w) {
    weapon_ = w;
    drawT_ = 0;
    inspectT_ = -1;
}

float ViewModel::inspectLength() const {
    switch (weapon_) {
        case ViewWeapon::Knife: return knife_ == 2 ? 2.8f : 2.6f;
        case ViewWeapon::Grenade: return 1.4f;
        case ViewWeapon::Pistol: return 3.0f;
        default: return 3.6f;
    }
}

void ViewModel::inspect() {
    if (drawT_ >= 1.0f && reloadT_ < 0) inspectT_ = 0;
}

void ViewModel::update(const ViewModelInput& in) {
    float dt = std::min(in.dt, 0.05f);
    kickBack_ *= std::exp(-dt * 14.0f);
    kickPitch_ *= std::exp(-dt * 11.0f);
    kickYaw_ *= std::exp(-dt * 11.0f);
    kickRoll_ *= std::exp(-dt * 12.0f);
    sinceShot_ += dt;

    // Sway: the weapon lags slightly behind camera turns.
    swayYaw_ = std::clamp(swayYaw_ - in.mouseYawDelta * 0.5f, -3.0f, 3.0f) * std::exp(-dt * 10.0f);
    swayPitch_ = std::clamp(swayPitch_ - in.mousePitchDelta * 0.5f, -3.0f, 3.0f) * std::exp(-dt * 10.0f);

    // Bob scales with ground speed.
    float target = in.onGround ? std::min(in.horizSpeed / 250.0f, 1.0f) : 0.0f;
    bobAmount_ += (target - bobAmount_) * (1.0f - std::exp(-dt * 8.0f));
    bobPhase_ += dt * 2.0f * kPi * 1.6f * std::max(bobAmount_, 0.0f);

    // Landing dip: damped spring.
    float acc = -220.0f * landDip_ - 22.0f * landVel_;
    landVel_ += acc * dt;
    landDip_ += landVel_ * dt;

    // The butterfly flips open (slower draw); everything else just raises.
    drawT_ = std::min(1.0f, drawT_ + dt / (weapon_ == ViewWeapon::Knife ? 0.8f : 0.35f));
    if (inspectT_ >= 0) inspectT_ += dt;
    if (inspectT_ > inspectLength() || in.reloadProgress >= 0) inspectT_ = -1;
    flashLeft_ -= dt;
    reloadT_ = in.reloadProgress;
    reloadTime_ = in.reloadTime;
}

void ViewModel::build(const Vec3& eye, float pitchDeg, float yawDeg, float offX, float offY, float offZ,
                      float bobScale, std::vector<ModelDraw>& out) const {
    const bool sniper = weapon_ == ViewWeapon::Sniper;
    const bool rifle = weapon_ == ViewWeapon::Rifle || sniper, pistol = weapon_ == ViewWeapon::Pistol;
    const bool gun = rifle || pistol;
    // Camera-local placement (x right, y up, z back). Tuned so the guns sit lower-right like CS.
    Vec3 pos = rifle ? Vec3{10.9f, -6.9f, -22.0f} : pistol ? Vec3{7.5f, -6.0f, -16.0f} : Vec3{9.5f, -7.5f, -18.0f};
    pos += Vec3{offX, offY, -offZ};
    const float modelScale = rifle ? 0.75f : 0.85f;
    float pitch = gun ? 2.0f : 10.0f, yaw = rifle ? 4.0f : pistol ? 3.0f : 6.0f, roll = gun ? 0.0f : -15.0f;

    // Walk bob (figure-eight) and landing dip.
    pos.x += std::cos(bobPhase_) * 0.32f * bobAmount_ * bobScale;
    pos.y += std::sin(2.0f * bobPhase_) * 0.16f * bobAmount_ * bobScale + landDip_ * 0.08f;

    // Recoil kick.
    pos.z += kickBack_;
    pitch += kickPitch_;
    roll += kickRoll_;
    yaw += kickYaw_ + swayYaw_;
    pitch += swayPitch_;

    // Draw (raise from below).
    float e = 1.0f - smooth01(weapon_ == ViewWeapon::Knife ? drawT_ * 3.0f : drawT_);
    pos.y -= 7.0f * e;
    pitch -= 30.0f * e;

    // Reload: tilt the rifle, drop the old mag, insert a new one, rack the bolt.
    Vec3 magOffset{};
    bool magVisible = true;
    if (gun && reloadT_ >= 0) {
        float t = reloadT_, T = reloadTime_;
        float env = ramp(t, 0.0f, 0.35f) * (1.0f - ramp(t, T - 0.45f, T - 0.1f));
        roll -= 28.0f * env;
        pitch += 9.0f * env;
        pos.x -= 1.0f * env;
        pos.y += 0.6f * env;
        if (t < 0.3f) {
        } else if (t < 0.75f) {
            magOffset.y = -14.0f * ramp(t, 0.3f, 0.75f);
        } else if (t < 1.0f) {
            magVisible = false;
        } else if (t < 1.4f) {
            magOffset.y = -10.0f * (1.0f - ramp(t, 1.0f, 1.4f));
        }
        float bolt = ramp(t, 1.95f, 2.05f) * (1.0f - ramp(t, 2.1f, 2.25f));
        pos.z += 1.2f * bolt;
    }

    // Inspect (F). Guns follow keyframes: left side, roll over to the right side, look at the mag.
    // A breathing sway rides on top so the hold never looks frozen. Smoke: a toss and catch. Knives:
    // the butterfly does aerials, the karambit and talon spin round the finger ring, the M9 is tossed.
    float bladeAngle = 0, biteAngle = 0;  // butterfly pieces, degrees about the pivot (x axis)
    float ringSpin = 0;                   // karambit/talon: degrees about the finger ring
    if (inspectT_ >= 0) {
        const float t = inspectT_;
        if (gun) {
            Pose p = pistol ? samplePose(kPistolInspect, t) : samplePose(kRifleInspect, t);
            float hold = ramp(t, 0.3f, 0.8f) * (1.0f - ramp(t, inspectLength() - 0.6f, inspectLength()));
            pos += Vec3{p.x, p.y + 0.15f * std::sin(t * 3.1f) * hold, p.z};
            yaw += p.yaw + 2.0f * std::sin(t * 1.7f) * hold;
            pitch += p.pitch + 1.5f * std::sin(t * 2.3f + 1.0f) * hold;
            roll += p.roll;
        } else if (weapon_ == ViewWeapon::Grenade) {
            float up = std::sin(kPi * std::clamp((t - 0.25f) / 0.8f, 0.0f, 1.0f));
            pos.y += 5.0f * up;
            roll += 360.0f * ramp(t, 0.25f, 1.05f);
            pitch += 20.0f * up;
        } else {
            float in = ramp(t, 0.0f, 0.35f) * (1.0f - ramp(t, inspectLength() - 0.4f, inspectLength()));
            pos += Vec3{-3.0f * in, 2.0f * in, 2.0f * in};
            float look = ramp(t, 1.6f, 1.9f) * (1.0f - ramp(t, 2.1f, 2.4f));
            if (knife_ == 0) {  // butterfly: two aerials, then a look at the blade
                roll += 15.0f * in + 360.0f * ramp(t, 0.4f, 1.6f);
                biteAngle = 720.0f * ramp(t, 0.4f, 1.6f);
                bladeAngle = -720.0f * ramp(t, 0.45f, 1.55f);
                yaw += 70.0f * look;
                pitch -= 10.0f * look;
            } else if (knife_ == 2) {  // M9: tossed up end over end, caught, then turned to the light
                float air = std::sin(kPi * std::clamp((t - 0.4f) / 0.8f, 0.0f, 1.0f));
                pos.y += 7.0f * air;
                pitch += 360.0f * ramp(t, 0.4f, 1.2f);
                float turn = ramp(t, 1.4f, 1.8f) * (1.0f - ramp(t, 2.3f, 2.7f));
                yaw -= 55.0f * turn;
                roll += 35.0f * turn;
            } else {  // karambit / talon: spins round the finger, then shows the claw
                ringSpin = (knife_ == 1 ? 720.0f : 540.0f) * ramp(t, 0.35f, 1.45f);
                roll += 10.0f * in;
                yaw += 60.0f * look;
                roll -= 25.0f * look;
            }
        }
    }
    if (weapon_ == ViewWeapon::Knife && drawT_ < 1.0f) {
        if (knife_ == 0) {
            // Flip open: the bite handle fans a full turn while the blade spins out of the handles.
            biteAngle += 360.0f * ramp(drawT_, 0.1f, 0.85f);
            bladeAngle += 180.0f - 540.0f * ramp(drawT_, 0.15f, 0.9f);
        } else if (knife_ == 2) {
            roll += 360.0f * (1.0f - ramp(drawT_, 0.1f, 0.85f));
        } else {
            ringSpin += 360.0f * ramp(drawT_, 0.1f, 0.85f);
        }
    }

    Mat4 world = cameraBasis(eye, pitchDeg, yawDeg) * translation(pos) * rotationY(yaw) * rotationX(pitch) *
                 rotationZ(roll) * scaling(modelScale);

    if (gun) {
        const int f = finish_;
        out.push_back({world, sniper ? toBoxes(kSniperBody, f) : rifle ? toBoxes(kRifleBody, f) : toBoxes(kPistolBody, f)});
        if (magVisible)
            out.push_back({world * translation(magOffset),
                           sniper ? toBoxes(kSniperMag, f) : rifle ? toBoxes(kRifleMag, f) : toBoxes(kPistolMag, f)});
        if (flashLeft_ > 0) {
            float spin = float(flashSeed_ % 90);
            float s = (1.0f + float((flashSeed_ / 90) % 50) / 100.0f) * (rifle ? 1.0f : 0.75f);
            Vec3 muzzle = sniper ? Vec3{0, 0.2f, -33.3f} : rifle ? Vec3{0, 0.2f, -24.8f} : Vec3{0, 0.8f, -10.0f};
            ModelDraw flash{world * translation(muzzle) * rotationZ(spin), {}};
            flash.boxes.push_back(makeEmissive({-0.9f * s, -0.9f * s, -1.6f}, {0.9f * s, 0.9f * s, 1.0f}, 0xfff4c0));
            flash.boxes.push_back(makeEmissive({-2.8f * s, -0.22f, -0.6f}, {2.8f * s, 0.22f, 0.6f}, 0xffc24a));
            flash.boxes.push_back(makeEmissive({-0.22f, -2.8f * s, -0.6f}, {0.22f, 2.8f * s, 0.6f}, 0xffc24a));
            flash.boxes.push_back(makeEmissive({-0.5f, -0.5f, -4.0f * s}, {0.5f, 0.5f, -1.0f}, 0xffe08a));
            out.push_back(flash);
        }
    } else {
        if (weapon_ == ViewWeapon::Grenade) {
            // One body, tinted per type: smoke grey-green, flash pale grey, HE olive, molotov bottle brown.
            const uint32_t body[4] = {0x4f5a4a, 0xc9ccd0, 0x55602e, 0x7a4a1a};
            std::vector<BoxInstance> boxes = toBoxes(kGrenade);
            boxes[0] = makeBox(kGrenade[0].mins, kGrenade[0].maxs, body[std::clamp(grenade_, 0, 3)], false);
            if (grenade_ == 3) {  // molotov: taller bottle with a burning rag
                boxes[0] = makeBox({-1.1f, -1.1f, -4.6f}, {1.1f, 1.1f, 2.4f}, body[3], false);
                boxes.push_back(makeEmissive({-0.5f, -0.5f, -6.2f}, {0.5f, 0.5f, -4.6f}, 0xffa030));
            }
            out.push_back({world, boxes});
        } else if (knife_ == 0) {
            out.push_back({world, toBoxes(kButterflyHand)});
            out.push_back({world, toBoxes(kButterflySafe)});
            out.push_back({world * rotationX(biteAngle), toBoxes(kButterflyBite)});
            out.push_back({world * rotationX(bladeAngle), toBoxes(kButterflyBlade)});
        } else if (knife_ == 2) {
            out.push_back({world, toBoxes(kKnifeHand)});
            out.push_back({world, toBoxes(kM9)});
        } else {
            const Vec3 ring{0, -0.1f, knife_ == 1 ? 5.4f : 5.8f};
            out.push_back({world, toBoxes(kKnifeHand)});
            out.push_back({world * translation(ring) * rotationX(ringSpin) * translation(-ring),
                           knife_ == 1 ? toBoxes(kKarambit) : toBoxes(kTalon)});
        }
    }
}

Vec3 ViewModel::muzzleWorld(const Vec3& eye, float pitchDeg, float yawDeg) const {
    // Chosen so the tracer starts where the muzzle appears on screen (the weapon uses its own FOV).
    return transformPoint(cameraBasis(eye, pitchDeg, yawDeg), {14.4f, -9.3f, -40.0f});
}

float Effects::rnd() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return float(rng_) / 2147483648.0f - 1.0f;
}

void Effects::impact(const Vec3& pos, const Vec3& normal, uint32_t color, float scale) {
    for (int i = 0; i < 6; ++i) {
        Vec3 v = normal * (90.0f + 120.0f * (rnd() * 0.5f + 0.5f)) + Vec3{rnd(), rnd(), rnd() + 0.6f} * 90.0f;
        particles_.push_back({pos + normal * 0.5f, v * std::sqrt(scale), 0, 0.35f + 0.25f * (rnd() * 0.5f + 0.5f),
                              (0.5f + 0.4f * (rnd() * 0.5f + 0.5f)) * scale, color});
    }
    for (int i = 0; i < 4; ++i) {  // dust puff (bigger far away so a spray's landing spot stays readable)
        Vec3 v = normal * 35.0f + Vec3{rnd(), rnd(), rnd()} * 15.0f + Vec3{0, 0, 260.0f};
        particles_.push_back({pos + normal * 1.0f, v, 0, 0.26f, (1.8f + rnd() * 0.6f) * scale, 0xb9b2a3});
    }
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::burst(const Vec3& pos, uint32_t color, float scale) {
    for (int i = 0; i < 10; ++i) {  // fireball: big glowing blobs that shrink fast
        Vec3 v = Vec3{rnd(), rnd(), rnd() * 0.5f + 0.6f} * (90.0f * scale);
        particles_.push_back({pos + Vec3{0, 0, 16}, v, 0, 0.25f, 26.0f * scale, i % 2 ? color : 0xfff0c0, true});
    }
    for (int i = 0; i < 30; ++i) {  // hot sparks
        Vec3 v = Vec3{rnd(), rnd(), rnd() * 0.5f + 0.7f} * (300.0f * scale);
        particles_.push_back({pos + Vec3{0, 0, 6}, v, 0, 0.45f + 0.3f * (rnd() * 0.5f + 0.5f), 4.0f * scale, color, true});
    }
    for (int i = 0; i < 12; ++i) {  // smoke left behind
        Vec3 v = Vec3{rnd() * 60.0f, rnd() * 60.0f, 120.0f + rnd() * 40.0f} * scale;
        particles_.push_back({pos + Vec3{0, 0, 10}, v, 0, 1.4f, 18.0f * scale, 0x6b6f75});
    }
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::shell(const Vec3& pos, const Vec3& vel) {
    particles_.push_back({pos, vel, 0, 1.4f, 0.32f, 0xc9a13b});
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::blood(const Vec3& pos, const Vec3& dir) {
    for (int i = 0; i < 9; ++i) {
        Vec3 v = dir * 60.0f + Vec3{rnd(), rnd(), rnd() + 0.8f} * 70.0f;
        uint32_t c = i % 2 ? 0x8a1010 : 0xb01c1c;
        particles_.push_back({pos, v, 0, 0.3f + 0.2f * (rnd() * 0.5f + 0.5f), 0.6f + 0.6f * (rnd() * 0.5f + 0.5f), c});
    }
}

void Effects::tracer(const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 64.0f) return;
    tracers_.push_back({from, d * (1.0f / len), len, 0});
}

void Effects::update(float dt) {
    dt = std::min(dt, 0.05f);
    for (Particle& p : particles_) {
        p.life += dt;
        p.vel.z -= 800.0f * dt;
        p.pos += p.vel * dt;
        float floorZ = ground_ ? ground_(p.pos.x, p.pos.y) : 0.0f;
        if (p.pos.z < floorZ + 0.3f && p.pos.z > floorZ - 48.0f) { p.pos.z = floorZ + 0.3f; p.vel = {}; }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life >= p.maxLife; }),
                     particles_.end());
    for (Tracer& t : tracers_) t.travelled += 9000.0f * dt;
    tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(),
                                  [](const Tracer& t) { return t.travelled - 160.0f > t.length; }),
                   tracers_.end());
}

void Effects::appendParticles(std::vector<BoxInstance>& out) const {
    for (const Particle& p : particles_) {
        float h = p.size * 0.5f * std::sqrt(1.0f - p.life / p.maxLife);
        out.push_back(p.glow ? makeEmissive(p.pos - Vec3{h, h, h}, p.pos + Vec3{h, h, h}, p.color)
                             : makeBox(p.pos - Vec3{h, h, h}, p.pos + Vec3{h, h, h}, p.color, false));
    }
}

void Effects::appendTracers(std::vector<ModelDraw>& out) const {
    for (const Tracer& t : tracers_) {
        float head = std::min(t.travelled, t.length), tail = std::max(0.0f, t.travelled - 160.0f);
        if (head <= tail) continue;
        Vec3 up = std::fabs(t.dir.z) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        Vec3 r = normalize(cross(t.dir, up)), u = cross(t.dir, r);
        Mat4 m = fromBasis(r, u, t.dir, t.start + t.dir * tail);
        out.push_back({m, {makeEmissive({-0.16f, -0.16f, 0}, {0.16f, 0.16f, head - tail}, 0xffe39a)}});
    }
}
