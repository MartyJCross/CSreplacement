// Weapons (hitscan, fixed recoil pattern, deterministic spread) and target dummies.
#pragma once
#include <cstdint>
#include <vector>
#include "vecmath.h"
#include "world.h"

enum HitGroup : uint8_t { kHead, kChest, kStomach, kLegs, kNumHitGroups };
const char* hitGroupName(HitGroup g);

struct RecoilStep { float up, right; };  // degrees per shot

struct WeaponDef {
    const char* name;
    bool canFire;
    float maxSpeed;          // movement speed while holding it
    float damage;
    float rangeModifier;     // damage *= rangeModifier ^ (distance / 500)
    float fireInterval;      // seconds between shots
    int magSize;
    float reloadTime;
    float accurateSpeedFrac; // at or below this fraction of maxSpeed you are "accurate"
    float moveInaccuracy;    // degrees of spread at full speed
    float airInaccuracy;     // degrees of spread while airborne
    float sprayInaccuracy;   // degrees added per shot index (capped)
    const RecoilStep* pattern;
    int patternLen;
};

const WeaponDef& rifleDef();
const WeaponDef& knifeDef();

struct WeaponState {
    const WeaponDef* def = nullptr;
    int ammo = 0;
    double nextFireTime = 0;
    double reloadEndTime = -1;  // < 0 when not reloading
    float recoilIndex = 0;      // continuous; integer part = shots into the spray
    uint32_t shotCounter = 0;   // seeds deterministic spread
};

// Cumulative recoil (aim punch) in degrees at a given (fractional) spray index.
RecoilStep recoilAt(const WeaponDef& w, float index);

struct Hitbox { Vec3 mins, maxs; HitGroup group; };  // relative to dummy feet

enum class DummyMotion { Static, Strafe };

struct Dummy {
    Vec3 pos, prevPos;
    DummyMotion motion = DummyMotion::Static;
    Vec3 a, b;           // strafe endpoints
    float speed = 0;     // units/sec while strafing
    float pause = 0;     // seconds to stand still at each end
    float pauseLeft = 0;
    bool towardB = true;
    float hp = 100;
    float respawnLeft = 0;  // > 0 while dead
    float flash[kNumHitGroups] = {};
    // Aim drill: respawn somewhere random inside this area instead of at the same spot.
    bool randomRespawn = false;
    Vec3 areaMin, areaMax;
    uint32_t respawns = 0;
    float stepDist = 0;  // footstep accumulator (cosmetic)
    bool alive() const { return respawnLeft <= 0; }
};

const std::vector<Hitbox>& dummyHitboxes();
std::vector<Dummy> buildDummies();
void updateDummy(Dummy& d, float dt);

struct ShotResult {
    Vec3 start, end, normal;
    int dummyIndex = -1;     // -1 = hit world or nothing
    HitGroup group = kChest;
    float damage = 0;
    bool kill = false;
    bool hitWorld = false;
    float distance = 0;
    int sprayIndex = 0;
};

// Fires one bullet. `dummyRenderPos` are the dummy positions the player was looking at (what you see
// is what you hit). Spread and recoil are fully deterministic.
ShotResult fireBullet(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed,
                      bool onGround, bool ducked, const World& world, std::vector<Dummy>& dummies,
                      const std::vector<Vec3>& dummyRenderPos);

// Current spread radius in degrees for HUD/debugging.
float currentInaccuracy(const WeaponState& ws, float horizSpeed, bool onGround, bool ducked);

// Recoil recovery while not firing.
void decayRecoil(WeaponState& ws, float dt);
