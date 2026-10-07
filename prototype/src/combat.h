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
    bool automatic;          // false = one shot per click
    float penetration = 0;   // units of wall a bullet can pass through (0 = none)
};

const WeaponDef& rifleDef();
const WeaponDef& pistolDef();
const WeaponDef& sniperDef();
const WeaponDef& knifeDef();
const WeaponDef& grenadeDef();  // smoke grenade slot: thrown, never fires bullets

struct WeaponState {
    const WeaponDef* def = nullptr;
    int ammo = 0;
    double nextFireTime = 0;
    double reloadEndTime = -1;  // < 0 when not reloading
    float recoilIndex = 0;      // continuous; integer part = shots into the spray
    uint32_t shotCounter = 0;   // seeds deterministic spread
    // Random spread is OFF by default: bullets go exactly to crosshair + fixed recoil pattern.
    bool spraySpread = false;   // random spread that grows during a spray
    bool moveSpread = false;    // random spread while moving / airborne
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
    float deadFor = 0;      // seconds since death (death animation)
    float flash[kNumHitGroups] = {};
    // Aim drill: respawn somewhere random inside this area instead of at the same spot.
    bool randomRespawn = false;
    Vec3 areaMin, areaMax;
    uint32_t respawns = 0;
    int lastSpot = -1;
    float stepDist = 0;  // footstep accumulator (cosmetic)
    // Facing in degrees (0 = +X, like the player's yaw); hitboxes turn with it. 180 = facing -X, the
    // Feel Lab default. shownYaw is the facing drawn on the last frame: shots test against that.
    float yaw = 180, prevYaw = 180, shownYaw = 180;
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
    // Wallbangs: walls the bullet passed through (entry/exit points for decals).
    int penCount = 0;
    Vec3 penEntry[2], penExit[2], penNormal[2];
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
