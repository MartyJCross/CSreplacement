// Cosmetic-only visuals: first-person weapon model + animation, tracers, impact debris.
// Nothing here affects gameplay or hit registration.
#pragma once
#include <cstdint>
#include <vector>
#include "render.h"
#include "vecmath.h"

enum class ViewWeapon { Rifle, Pistol, Sniper, Knife, Grenade, Berettas, Deagle, Nova, Mac10, M4A4, Galil, Ssg08, Ump45 };
constexpr int kViewWeapons = 13;

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
    PaintParams paint;  // the skin on its painted parts (pattern -1: none)
};

class ViewModel {
public:
    void onShot(uint32_t seed);
    void onLand(float fallSpeed);
    void onDraw(ViewWeapon w);
    // Inspect (F): show the weapon off. Cancelled by firing, reloading or switching.
    void inspect();
    // Cosmetics: the knife (items.h KnifeType) and the skin on each weapon (pattern -1 = plain).
    void setKnife(int k) { knife_ = k; }
    void setSkin(ViewWeapon w, const PaintParams& p) { skins_[int(w)] = p; }
    const PaintParams& skin(ViewWeapon w) const { return skins_[int(w)]; }
    // A weapon on its own, turning slowly in front of the camera (the inventory and case screens), centred
    // at `centre` in camera space (x right, y up, -z ahead), `size` = its length on screen in those units.
    static void buildShowcase(ViewWeapon w, int knife, const PaintParams& paint, const Vec3& eye, float pitchDeg,
                              float yawDeg, float spinDeg, const Vec3& centre, float size, std::vector<ModelDraw>& out);
    void setGrenade(int type) { grenade_ = type; }  // 0 smoke, 1 flash, 2 HE, 3 molotov
    // Grenade pin pulled, waiting for the release: 0 no, 1 overhand (Mouse 1), 2 underhand lob (Mouse 2), 3 both.
    void setPrimed(int how) { primed_ = how; if (how) primedPose_ = how; }
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
    float inspectT_ = -1;      // seconds into the inspect animation, < 0 when not inspecting
    int knife_ = 0, grenade_ = 0;
    PaintParams skins_[kViewWeapons];

    int primed_ = 0, primedPose_ = 1;
    float primeT_ = 0;         // 0..1 ease into the wind-up pose
    float inspectLength() const;
    float flashLeft_ = 0;
    uint32_t flashSeed_ = 0;
    uint32_t shotIndex_ = 0;  // the Berettas fire left and right in turn
    float reloadT_ = -1, reloadTime_ = 1;
};

struct Particle {
    Vec3 pos, vel;
    float life, maxLife, size;
    uint32_t color;
    bool glow = false;  // unlit (fire, sparks)
};

// A solid piece that tumbles through the air, bounces once and lies there (a helmet knocked off).
struct Debris {
    Vec3 pos, vel;
    float life = 0, spin = 0, spinRate = 0, tilt = 0;
    uint32_t color = 0;
    bool resting = false;
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
    void burst(const Vec3& pos, uint32_t color, float scale);  // grenade going off
    // Floor height under (x, y) for particles to land on (nullptr = flat floor at 0).
    void setGround(float (*groundAt)(float x, float y)) { ground_ = groundAt; }
    void blood(const Vec3& pos, const Vec3& dir);
    void tracer(const Vec3& from, const Vec3& to);
    // A headshot kill: the helmet flies off along `dir`, spinning.
    void helmet(const Vec3& pos, const Vec3& dir, uint32_t color);
    void update(float dt);
    void appendParticles(std::vector<BoxInstance>& out) const;
    void appendTracers(std::vector<ModelDraw>& out) const;  // (and the debris)

private:
    float rnd();  // -1..1
    std::vector<Particle> particles_;
    std::vector<Tracer> tracers_;
    std::vector<Debris> debris_;
    uint32_t rng_ = 0xC0FFEEu;
    float (*ground_)(float, float) = nullptr;
};
