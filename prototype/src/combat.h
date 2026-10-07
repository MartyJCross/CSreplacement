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
    // Lab default. shownYaw is the facing drawn on the last frame: shots test against that.
    float yaw = 180, prevYaw = 180, shownYaw = 180;
    // Competitive: kevlar (body/arms take 77.5%) and helmet (the same for the head); your teammates
    // are `friendly` - your bullets pass through them.
    float armor = 0;
    bool helmet = false, friendly = false;
    bool alive() const { return respawnLeft <= 0; }
};

const std::vector<Hitbox>& dummyHitboxes();

// A ray against a dummy standing at `pos` facing `yaw`: true with the distance and hit group of the
// first hitbox it meets within maxT.
bool rayHitsDummy(const Vec3& pos, float yaw, const Vec3& start, const Vec3& dir, float maxT, float& t, HitGroup& group);

// Damage multiplier for where a bullet lands (head x4, stomach x1.25, legs x0.75).
float hitGroupDamageScale(HitGroup g);

// Damage after armor (CS: kevlar takes 77.5% on body/arms/stomach, a helmet on the head; legs unarmored).
float armoredDamage(float damage, HitGroup group, float armor, bool helmet);
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

// ---- Grenades: CS:GO's throw and flight. Pure and deterministic: the game, the trajectory preview and
// the bots' throws all use these, so a preview shows exactly where the grenade will go. ----
constexpr float kNadeGravity = 320.0f;     // 0.4 x 800, like CS
constexpr float kNadeThrowSpeed = 675.0f;  // a full throw; the underhand lob is 0.3 x
// Velocity of a throw from view angles (pitch + = down): CS lifts the aim (10 degrees at the horizon,
// none straight up or down) and adds 1.25x the thrower's own velocity.
Vec3 grenadeThrowVelocity(float viewPitch, float viewYaw, bool lob, const Vec3& throwerVelocity);
// One tick of flight: gravity, bounces off walls (45% speed kept), coming to rest on floors.
struct NadeStep { bool bounced = false, landed = false; float impactSpeed = 0; };
NadeStep stepGrenade(const World& world, Vec3& pos, Vec3& vel);
// Where a grenade thrown now goes off: flash and HE on their fuse, molotov on its first landing (or
// fuse), smoke once it has stopped (or fuse + 4 s). `path` (optional) gets the position every tick.
Vec3 predictGrenade(const World& world, Vec3 pos, Vec3 vel, int type, std::vector<Vec3>* path = nullptr);
constexpr double grenadeFuse(int type) { return type == 3 ? 2.0 : 1.5; }  // type: 0 smoke, 1 flash, 2 HE, 3 molotov
