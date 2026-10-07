#include "fx.h"
#include <algorithm>
#include <cmath>

namespace {

// Colours.
constexpr uint32_t kMetal = 0x2b2e33, kMetalLight = 0x41454c, kWood = 0x7a5230, kWoodDark = 0x5a3c23,
                   kGlove = 0x2a2a2a, kSleeve = 0x4f5b3f, kBlade = 0xb4b9c0;

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
const Part kKnife[] = {
    {{-0.5f, -0.7f, -1.0f}, {0.5f, 0.7f, 4.0f}, 0x222222},        // handle
    {{-0.9f, -1.0f, -1.4f}, {0.9f, 1.0f, -1.0f}, kMetal},         // guard
    {{-0.12f, -0.6f, -9.0f}, {0.12f, 0.9f, -1.4f}, kBlade},       // blade
    {{-0.1f, -0.2f, -10.5f}, {0.1f, 0.7f, -9.0f}, kBlade},        // tip
    {{-1.2f, -1.4f, -0.5f}, {1.2f, 1.4f, 3.5f}, kGlove},          // hand
    {{-1.6f, -2.2f, 3.5f}, {1.6f, 1.6f, 13.0f}, kSleeve},         // forearm
};

template <size_t N>
std::vector<BoxInstance> toBoxes(const Part (&parts)[N]) {
    std::vector<BoxInstance> out;
    for (const Part& p : parts) out.push_back(makeBox(p.mins, p.maxs, p.color, false));
    return out;
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
    kickBack_ = std::min(kickBack_ + 1.1f, 2.6f);
    kickPitch_ = std::min(kickPitch_ + 1.7f, 4.5f);
    kickYaw_ += (float(seed % 1000) / 1000.0f - 0.5f) * 0.6f;
    flashLeft_ = 0.035f;
    flashSeed_ = seed;
}

void ViewModel::onLand(float fallSpeed) { landVel_ -= std::min(fallSpeed / 400.0f, 1.0f) * 28.0f; }

void ViewModel::onDraw(ViewWeapon w) {
    weapon_ = w;
    drawT_ = 0;
}

void ViewModel::update(const ViewModelInput& in) {
    float dt = std::min(in.dt, 0.05f);
    kickBack_ *= std::exp(-dt * 14.0f);
    kickPitch_ *= std::exp(-dt * 11.0f);
    kickYaw_ *= std::exp(-dt * 11.0f);

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

    drawT_ = std::min(1.0f, drawT_ + dt / 0.35f);
    flashLeft_ -= dt;
    reloadT_ = in.reloadProgress;
    reloadTime_ = in.reloadTime;
}

void ViewModel::build(const Vec3& eye, float pitchDeg, float yawDeg, float offX, float offY, float offZ,
                      float bobScale, std::vector<ModelDraw>& out) const {
    const bool rifle = weapon_ == ViewWeapon::Rifle;
    // Camera-local placement (x right, y up, z back). Tuned so the rifle sits lower-right like CS.
    Vec3 pos = rifle ? Vec3{10.9f, -6.9f, -22.0f} : Vec3{9.5f, -7.5f, -18.0f};
    pos += Vec3{offX, offY, -offZ};
    const float modelScale = rifle ? 0.75f : 0.8f;
    float pitch = rifle ? 2.0f : 10.0f, yaw = rifle ? 4.0f : 6.0f, roll = rifle ? 0.0f : -15.0f;

    // Walk bob (figure-eight) and landing dip.
    pos.x += std::cos(bobPhase_) * 0.32f * bobAmount_ * bobScale;
    pos.y += std::sin(2.0f * bobPhase_) * 0.16f * bobAmount_ * bobScale + landDip_ * 0.08f;

    // Recoil kick.
    pos.z += kickBack_;
    pitch += kickPitch_;
    yaw += kickYaw_ + swayYaw_;
    pitch += swayPitch_;

    // Draw (raise from below).
    float e = 1.0f - smooth01(drawT_);
    pos.y -= 7.0f * e;
    pitch -= 30.0f * e;

    // Reload: tilt the rifle, drop the old mag, insert a new one, rack the bolt.
    Vec3 magOffset{};
    bool magVisible = true;
    if (rifle && reloadT_ >= 0) {
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

    Mat4 world = cameraBasis(eye, pitchDeg, yawDeg) * translation(pos) * rotationY(yaw) * rotationX(pitch) *
                 rotationZ(roll) * scaling(modelScale);

    if (rifle) {
        out.push_back({world, toBoxes(kRifleBody)});
        if (magVisible) out.push_back({world * translation(magOffset), toBoxes(kRifleMag)});
        if (flashLeft_ > 0) {
            float spin = float(flashSeed_ % 90);
            float s = 0.8f + float((flashSeed_ / 90) % 50) / 100.0f;
            ModelDraw flash{world * translation({0, 0.2f, -24.8f}) * rotationZ(spin), {}};
            flash.boxes.push_back(makeEmissive({-0.9f * s, -0.9f * s, -1.6f}, {0.9f * s, 0.9f * s, 1.0f}, 0xfff4c0));
            flash.boxes.push_back(makeEmissive({-2.8f * s, -0.22f, -0.6f}, {2.8f * s, 0.22f, 0.6f}, 0xffc24a));
            flash.boxes.push_back(makeEmissive({-0.22f, -2.8f * s, -0.6f}, {0.22f, 2.8f * s, 0.6f}, 0xffc24a));
            flash.boxes.push_back(makeEmissive({-0.5f, -0.5f, -4.0f * s}, {0.5f, 0.5f, -1.0f}, 0xffe08a));
            out.push_back(flash);
        }
    } else {
        out.push_back({world, toBoxes(kKnife)});
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

void Effects::impact(const Vec3& pos, const Vec3& normal, uint32_t color) {
    for (int i = 0; i < 6; ++i) {
        Vec3 v = normal * (90.0f + 120.0f * (rnd() * 0.5f + 0.5f)) + Vec3{rnd(), rnd(), rnd() + 0.6f} * 90.0f;
        particles_.push_back({pos + normal * 0.5f, v, 0, 0.35f + 0.25f * (rnd() * 0.5f + 0.5f),
                              0.5f + 0.4f * (rnd() * 0.5f + 0.5f), color});
    }
    for (int i = 0; i < 3; ++i) {  // dust puff
        Vec3 v = normal * 35.0f + Vec3{rnd(), rnd(), rnd()} * 15.0f + Vec3{0, 0, 260.0f};
        particles_.push_back({pos + normal * 1.0f, v, 0, 0.22f, 1.6f + rnd() * 0.5f, 0xa9aeb5});
    }
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
        if (p.pos.z < 0.3f) { p.pos.z = 0.3f; p.vel = {}; }
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
        out.push_back(makeBox(p.pos - Vec3{h, h, h}, p.pos + Vec3{h, h, h}, p.color, false));
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
