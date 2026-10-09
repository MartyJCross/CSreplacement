// Weapons (hitscan, fixed recoil pattern, deterministic spread) and target dummies.
#pragma once
#include <cstdint>
#include <vector>
#include "vecmath.h"
#include "world.h"

enum HitGroup : uint8_t { kHead, kChest, kStomach, kLegs, kNumHitGroups };
const char* hitGroupName(HitGroup g);

struct RecoilStep { float up, right; };  // degrees per shot

// Every weapon has an id (also its byte online): the first five are the original ones. kWRifle is the AK-47,
// kWSniper the AWP-like bolt action.
enum WeaponId : uint8_t {
    kWRifle, kWPistol, kWKnife, kWGrenade, kWSniper, kWBerettas, kWDeagle, kWNova, kWMac10, kWM4A1S, kWGalil, kWSsg08,
    kWUmp45, kWXm1014, kWeaponCount
};

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
    int id = kWRifle;
    int pellets = 1;         // shotgun: bullets per shot, in a fixed pattern (firePellets)
    float pelletSpread = 0;  // degrees: the widest a pellet goes from the aim
    bool shellReload = false;  // reloads one shell per reloadTime; firing stops it
    bool primary = true;     // slot 1 (else the pistol slot)
    int price = 0;           // competitive buy menu
    int killReward = 300;    // competitive money per kill
    float armorRatio = 0.775f;  // damage kept through kevlar / a helmet (CS: the AWP 97.5%, the M4A1-S 70%)
    bool scope = false;      // Mouse 2 zooms (the snipers)
    float noscopeInaccuracy = 0;  // degrees of spread unscoped (only with the moving-spread option on)
};

const WeaponDef& rifleDef();
const WeaponDef& pistolDef();
const WeaponDef& sniperDef();
const WeaponDef& knifeDef();
const WeaponDef& grenadeDef();  // smoke grenade slot: thrown, never fires bullets
const WeaponDef& berettasDef();
const WeaponDef& deagleDef();
const WeaponDef& novaDef();
const WeaponDef& mac10Def();
const WeaponDef& weaponDef(int id);  // by WeaponId

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
    bool scoped = false;        // the game sets it: a sniper's noscope spread is off while zoomed
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
    float pitch = 0, prevPitch = 0;  // where they look up / down (cosmetic: spectating through their eyes)
    // Competitive: kevlar (body/arms take 77.5%) and helmet (the same for the head); your teammates
    // are `friendly` - your bullets pass through them.
    float armor = 0;
    bool helmet = false, friendly = false;
    // Crouch: 0 standing .. 1 fully crouched (online players; the model and its hitboxes squat together).
    // shownCrouch is what was drawn on the last frame, like shownYaw.
    float crouch = 0, prevCrouch = 0, shownCrouch = 0;
    uint8_t weapon = 0;  // what they hold (cosmetic): a WeaponId
    // Cosmetic: the direction of the last bullet that hit them (they fall that way when it kills), and a
    // headshot kill knocks the helmet off.
    Vec3 hitDir;
    bool lostHelmet = false;
    bool alive() const { return respawnLeft <= 0; }
};

const std::vector<Hitbox>& dummyHitboxes();
// A height on the standing model, crouched by `crouch`: the legs fold (0..34 shrinks to 0..16) and
// everything above drops 18 units, like the player's eye (64 -> 46).
float crouchZ(float z, float crouch);

// A ray against a dummy standing at `pos` facing `yaw` (crouched by `crouch`): true with the distance and hit group of the
// first hitbox it meets within maxT.
bool rayHitsDummy(const Vec3& pos, float yaw, float crouch, const Vec3& start, const Vec3& dir, float maxT, float& t,
                  HitGroup& group);

// Damage multiplier for where a bullet lands (head x4, stomach x1.25, legs x0.75).
float hitGroupDamageScale(HitGroup g);

// Damage after armor: kevlar keeps `ratio` of it on body/arms/stomach, a helmet on the head; legs unarmored.
// The ratio is the weapon's armorRatio (CS: 77.5% for most guns, the AWP 97.5%).
float armoredDamage(float damage, HitGroup group, float armor, bool helmet, float ratio = 0.775f);
std::vector<Dummy> buildDummies();
void updateDummy(Dummy& d, float dt);

struct ShotResult {
    Vec3 start, end, normal;
    int dummyIndex = -1;     // -1 = hit world or nothing
    HitGroup group = kChest;
    float damage = 0;
    bool kill = false;
    bool hitWorld = false;
    int worldBox = -1;       // the World::solids box it ended in (material for the impact sound)
    float distance = 0;
    int sprayIndex = 0;
    // Wallbangs: walls the bullet passed through (entry/exit points for decals).
    int penCount = 0;
    Vec3 penEntry[2], penExit[2], penNormal[2];
    // Collaterals: guns that go through walls also go through bodies (losing some damage each time), up to
    // two more people behind the first. Their hits, in order (already applied to them like the first).
    struct BodyHit { int dummyIndex; HitGroup group; float damage; bool kill; Vec3 at, normal; float distance; };
    int collats = 0;
    BodyHit collat[2];
    bool isCollat = false;   // (the game's own copy of a collateral hit, made by collatResults)
};
constexpr float kBodyThickness = 8.0f;  // how much wall a body counts as, for a bullet going through it

// Each collateral hit of `r` as a result of its own (dummyIndex, group, damage, kill, end, isCollat), so the
// game can treat it like any other hit. Appends to out[n..] up to `cap`; returns the new count.
int collatResults(const ShotResult& r, ShotResult* out, int n, int cap);

// Fires one bullet. `dummyRenderPos` are the dummy positions the player was looking at (what you see
// is what you hit). Spread and recoil are fully deterministic.
ShotResult fireBullet(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed,
                      bool onGround, bool ducked, const World& world, std::vector<Dummy>& dummies,
                      const std::vector<Vec3>& dummyRenderPos);
// A shotgun shot: def.pellets bullets spread at random round the aim (the owner asked for random shotgun
// spread: the one exception to "no random bullets"), each traced and applied like fireBullet. The spread is
// seeded by the shot counter, so a replay or a test gets the same shot. Recoil and the counter advance once.
// `out` gets one result per pellet.
constexpr int kMaxPellets = 9;
int firePellets(WeaponState& ws, const Vec3& eye, float viewPitch, float viewYaw, float horizSpeed, bool onGround,
                bool ducked, const World& world, std::vector<Dummy>& dummies, const std::vector<Vec3>& dummyRenderPos,
                ShotResult (&out)[kMaxPellets]);
// Pellet k of shot `seed`: its offset from the aim in degrees (up, right), within def.pelletSpread and
// gathered towards the middle (half the pellets land inside 40% of the spread).
RecoilStep pelletOffset(const WeaponDef& w, int k, uint32_t seed);


// Fire timing for one tick: true (and the next shot scheduled) if the weapon may fire now. A held
// trigger keeps an exact cadence (each shot lands on the first tick at or after its time, without
// drift); after any pause the next shot is timed from now, so a quick tap is never followed by a
// shot sooner than one interval.
bool takeShotTiming(WeaponState& ws, double now);

// Current spread radius in degrees for HUD/debugging.
float currentInaccuracy(const WeaponState& ws, float horizSpeed, bool onGround, bool ducked);

// Recoil recovery while not firing.
void decayRecoil(WeaponState& ws, float dt);

// ---- Grenades: CS:GO's throw and flight. Pure and deterministic: the game, the trajectory preview and
// the bots' throws all use these, so a preview shows exactly where the grenade will go. ----
constexpr float kNadeGravity = 320.0f;     // 0.4 x 800, like CS
constexpr float kNadeThrowSpeed = 675.0f;  // a full throw (see grenadeThrowVelocity for the others)
// Velocity of a throw from view angles (pitch + = down): CS lifts the aim (10 degrees at the horizon,
// none straight up or down) and adds 1.25x the thrower's own velocity. `strength`: 1 full throw (Mouse 1),
// kNadeLob underhand (Mouse 2), kNadeMedium both buttons.
constexpr float kNadeLob = 0.3f, kNadeMedium = 0.6f;
Vec3 grenadeThrowVelocity(float viewPitch, float viewYaw, float strength, const Vec3& throwerVelocity);
// One tick of flight: gravity, bounces off walls (45% speed kept), coming to rest on floors.
struct NadeStep { bool bounced = false, landed = false; float impactSpeed = 0; };
NadeStep stepGrenade(const World& world, Vec3& pos, Vec3& vel);
// Where a grenade thrown now goes off: flash and HE on their fuse, molotov on its first landing (or
// fuse), smoke once it has stopped (or fuse + 4 s). `path` (optional) gets the position every tick.
Vec3 predictGrenade(const World& world, Vec3 pos, Vec3 vel, int type, std::vector<Vec3>* path = nullptr);
// Fuse in seconds by type (0 smoke, 1 flash, 2 HE, 3 molotov): smoke and molotov like CS, flash and HE a
// little longer (owner: time to react to them).
constexpr double grenadeFuse(int type) { return type == 3 ? 2.0 : type == 0 ? 1.5 : 1.8; }
