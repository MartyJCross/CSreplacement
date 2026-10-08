// Deterministic, tick-based kinematic player movement (Quake/Source lineage, tuned toward CS:GO).
#pragma once
#include "vecmath.h"
#include "world.h"

constexpr int kTickRate = 128;
constexpr float kTickDt = 1.0f / kTickRate;

struct MoveParams {
    float gravity = 1000.0f;         // snappier than CS:GO's 800: less hang time
    float accelerate = 6.48f;        // 10% under the original 7.2 (owner: a touch heavier)
    float airAccelerate = 12.0f;
    float airWishCap = 30.0f;        // max wishspeed used for air acceleration (air strafing)
    float friction = 5.6f;
    float stopSpeed = 80.0f;
    float jumpImpulse = 337.64f;     // sqrt(2 * 1000 * 57): same 57-unit apex, ~675 ms airtime
    float stepSize = 18.0f;
    float walkScale = 0.52f;         // shift-walk fraction of max speed
    float duckScale = 0.34f;         // crouch fraction of max speed
    float duckAirLift = 9.0f;        // feet pulled up when crouching in the air (crouch-jump)
    float duckTime = 0.125f;         // eye-height transition time
    float staminaJumpCost = 20.0f;   // stamina model discourages bunny-hopping
    float staminaLandCost = 12.0f;
    float staminaMax = 80.0f;
    float staminaRecover = 60.0f;    // per second
};

struct MoveInput {
    float forward = 0;  // -1..1
    float side = 0;     // -1..1 (positive = right)
    bool jumpPressed = false;  // edge-triggered: one jump per key press (no auto-hop)
    bool duck = false;
    bool walk = false;
};

struct PlayerState {
    Vec3 origin;     // bottom-center of the hull
    Vec3 velocity;
    bool onGround = false;
    bool ducked = false;
    float duckAmount = 0;  // 0 standing .. 1 fully crouched (eye height only)
    float stamina = 0;
};

constexpr float kHullHalfWidth = 16.0f;
constexpr float kStandHeight = 72.0f;
constexpr float kDuckHeight = 54.0f;
constexpr float kStandEye = 64.0f;
constexpr float kDuckEye = 46.0f;

inline Vec3 hullMins() { return {-kHullHalfWidth, -kHullHalfWidth, 0}; }
inline Vec3 hullMaxs(bool ducked) { return {kHullHalfWidth, kHullHalfWidth, ducked ? kDuckHeight : kStandHeight}; }
inline float eyeHeight(const PlayerState& p) { return kStandEye + (kDuckEye - kStandEye) * p.duckAmount; }

// Advances the player by exactly one fixed tick.
void playerMove(PlayerState& ps, const MoveInput& in, float yawDeg, float weaponMaxSpeed, const World& world,
                const MoveParams& mp = MoveParams{});
