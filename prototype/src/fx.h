// Cosmetic-only visuals: first-person weapon model + animation, tracers, impact debris.
// Nothing here affects gameplay or hit registration.
#pragma once
#include <cstdint>
#include <vector>
#include "render.h"
#include "vecmath.h"

enum class ViewWeapon { Rifle, Pistol, Sniper, Knife, Grenade };

struct ViewModelInput {
    float dt;                  // frame time
    float mouseYawDelta;       // degrees turned this frame (+ = left)
    float mousePitchDelta;     // degrees this frame (+ = down)
    float horizSpeed;
    bool onGround;
    float reloadProgress;      // seconds into the reload, < 0 if not reloading
    float reloadTime;
};

struct ModelDraw {
    Mat4 model;
    std::vector<BoxInstance> boxes;
};

class ViewModel {
public:
    void onShot(uint32_t seed);
    void onLand(float fallSpeed);
    void onDraw(ViewWeapon w);
    void update(const ViewModelInput& in);
    // Builds the weapon in world space for the given camera basis.
    void build(const Vec3& eye, float pitchDeg, float yawDeg, float offX, float offY, float offZ, float bobScale,
               std::vector<ModelDraw>& out) const;
    // World-space muzzle point (approximate, for tracers).
    Vec3 muzzleWorld(const Vec3& eye, float pitchDeg, float yawDeg) const;

private:
    ViewWeapon weapon_ = ViewWeapon::Rifle;
    float kickBack_ = 0, kickPitch_ = 0, kickYaw_ = 0;
    float swayYaw_ = 0, swayPitch_ = 0;
    float bobPhase_ = 0, bobAmount_ = 0;
    float landDip_ = 0, landVel_ = 0;
    float drawT_ = 1;          // 0..1 raise animation
    float kickRoll_ = 0;       // roll wobble per shot
    float sinceShot_ = 1;      // seconds since the last shot
    int shotsInRow_ = 0;       // consecutive shots in this spray (kick builds up)
    float flashLeft_ = 0;
    uint32_t flashSeed_ = 0;
    float reloadT_ = -1, reloadTime_ = 1;
};

struct Particle {
    Vec3 pos, vel;
    float life, maxLife, size;
    uint32_t color;
};

struct Tracer {
    Vec3 start, dir;
    float length, travelled;
};

class Effects {
public:
    // `scale` > 1 makes far impacts bigger so you can still read where a spray lands.
    void impact(const Vec3& pos, const Vec3& normal, uint32_t color, float scale = 1.0f);
    void shell(const Vec3& pos, const Vec3& vel);  // ejected brass
    // Floor height under (x, y) for particles to land on (nullptr = flat floor at 0).
    void setGround(float (*groundAt)(float x, float y)) { ground_ = groundAt; }
    void blood(const Vec3& pos, const Vec3& dir);
    void tracer(const Vec3& from, const Vec3& to);
    void update(float dt);
    void appendParticles(std::vector<BoxInstance>& out) const;
    void appendTracers(std::vector<ModelDraw>& out) const;

private:
    float rnd();  // -1..1
    std::vector<Particle> particles_;
    std::vector<Tracer> tracers_;
    uint32_t rng_ = 0xC0FFEEu;
    float (*ground_)(float, float) = nullptr;
};
