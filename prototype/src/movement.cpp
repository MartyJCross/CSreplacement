#include "movement.h"
#include <algorithm>

namespace {

constexpr float kDt = kTickDt;

void applyFriction(PlayerState& ps, const MoveParams& mp) {
    float speed = length(ps.velocity);
    if (speed < 0.1f) { ps.velocity = {}; return; }
    float control = speed < mp.stopSpeed ? mp.stopSpeed : speed;
    float drop = control * mp.friction * kDt;
    float newSpeed = std::max(0.0f, speed - drop);
    ps.velocity *= newSpeed / speed;
}

// Source-style accelerate. `capSpeed` limits how much speed may be added along wishDir (air strafing).
void accelerate(PlayerState& ps, const Vec3& wishDir, float wishSpeed, float capSpeed, float accel) {
    float current = dot(ps.velocity, wishDir);
    float add = capSpeed - current;
    if (add <= 0) return;
    float accelSpeed = std::min(accel * kDt * wishSpeed, add);
    ps.velocity += wishDir * accelSpeed;
}

Vec3 clipVelocity(const Vec3& v, const Vec3& n) {
    Vec3 out = v - n * dot(v, n);
    // Remove tiny residual motion into the plane.
    float into = dot(out, n);
    if (into < 0) out -= n * into;
    return out;
}

// Collide-and-slide. Moves ps.origin by ps.velocity * dt, sliding along up to 4 planes.
void tryPlayerMove(PlayerState& ps, const World& world) {
    Vec3 mins = hullMins(), maxs = hullMaxs(ps.ducked);
    Vec3 planes[5];
    int numPlanes = 0;
    Vec3 originalVel = ps.velocity;
    float timeLeft = kDt;

    for (int bump = 0; bump < 4 && timeLeft > 0; ++bump) {
        if (dot(ps.velocity, ps.velocity) < 1e-6f) break;
        Vec3 end = ps.origin + ps.velocity * timeLeft;
        TraceResult tr = world.traceBox(ps.origin, end, mins, maxs);
        if (tr.fraction > 0) { ps.origin = tr.endpos; numPlanes = 0; }
        if (tr.fraction >= 1.0f) break;
        timeLeft -= timeLeft * tr.fraction;

        if (numPlanes >= 5) { ps.velocity = {}; break; }
        planes[numPlanes++] = tr.normal;

        // Find a velocity that satisfies all touched planes.
        int i;
        Vec3 newVel;
        for (i = 0; i < numPlanes; ++i) {
            newVel = clipVelocity(ps.velocity, planes[i]);
            int j;
            for (j = 0; j < numPlanes; ++j)
                if (j != i && dot(newVel, planes[j]) < 0) break;
            if (j == numPlanes) break;
        }
        if (i < numPlanes) {
            ps.velocity = newVel;
        } else {
            if (numPlanes != 2) { ps.velocity = {}; break; }
            Vec3 dir = normalize(cross(planes[0], planes[1]));
            ps.velocity = dir * dot(dir, ps.velocity);
        }
        // Never bounce back against the original direction (prevents jitter in corners).
        if (dot(ps.velocity, originalVel) <= 0) { ps.velocity = {}; break; }
    }
}

bool traceGround(const PlayerState& ps, const World& world, float dist, Vec3* groundPos) {
    Vec3 down = ps.origin - Vec3{0, 0, dist};
    TraceResult tr = world.traceBox(ps.origin, down, hullMins(), hullMaxs(ps.ducked));
    if (tr.fraction < 1.0f && tr.normal.z >= 0.7f) {
        if (groundPos) *groundPos = tr.endpos;
        return true;
    }
    return false;
}

// Walk with step-up: try a flat move and a "step up, move, step down" move; keep whichever went further.
void walkMove(PlayerState& ps, const World& world, const MoveParams& mp) {
    Vec3 mins = hullMins(), maxs = hullMaxs(ps.ducked);
    PlayerState flat = ps;
    tryPlayerMove(flat, world);

    PlayerState stepped = ps;
    TraceResult up = world.traceBox(stepped.origin, stepped.origin + Vec3{0, 0, mp.stepSize}, mins, maxs);
    stepped.origin = up.endpos;
    tryPlayerMove(stepped, world);
    TraceResult down = world.traceBox(stepped.origin, stepped.origin - Vec3{0, 0, mp.stepSize + 2.0f}, mins, maxs);
    bool steppedOnGround = down.fraction < 1.0f && down.normal.z >= 0.7f;
    stepped.origin = down.endpos;

    Vec3 flatDelta = flat.origin - ps.origin, stepDelta = stepped.origin - ps.origin;
    float flatDist = flatDelta.x * flatDelta.x + flatDelta.y * flatDelta.y;
    float stepDist = stepDelta.x * stepDelta.x + stepDelta.y * stepDelta.y;
    if (steppedOnGround && stepDist > flatDist + 1e-3f) {
        stepped.velocity.z = 0;
        ps = stepped;
    } else {
        ps = flat;
    }
}

void updateDuck(PlayerState& ps, bool wantDuck, const World& world, const MoveParams& mp) {
    if (wantDuck && !ps.ducked) {
        ps.ducked = true;
        if (!ps.onGround) {
            // Pull the feet up in the air: this is what makes the crouch-jump reach higher.
            Vec3 lifted = ps.origin + Vec3{0, 0, mp.duckAirLift};
            if (world.boxFits(lifted, hullMins(), hullMaxs(true))) ps.origin = lifted;
        }
    } else if (!wantDuck && ps.ducked) {
        Vec3 target = ps.origin;
        if (!ps.onGround) target.z -= mp.duckAirLift;
        if (world.boxFits(target, hullMins(), hullMaxs(false))) {
            ps.origin = target;
            ps.ducked = false;
        } else if (world.boxFits(ps.origin, hullMins(), hullMaxs(false))) {
            ps.ducked = false;
        }
    }
    float rate = kDt / mp.duckTime;
    ps.duckAmount = ps.ducked ? std::min(1.0f, ps.duckAmount + rate) : std::max(0.0f, ps.duckAmount - rate);
}

}  // namespace

void playerMove(PlayerState& ps, const MoveInput& in, float yawDeg, float weaponMaxSpeed, const World& world,
                const MoveParams& mp) {
    updateDuck(ps, in.duck, world, mp);
    ps.stamina = std::max(0.0f, ps.stamina - mp.staminaRecover * kDt);

    // Wish direction from keys (yaw only; pitch never affects ground movement).
    float y = yawDeg * kDegToRad;
    Vec3 fwd{std::cos(y), std::sin(y), 0}, right{std::sin(y), -std::cos(y), 0};
    Vec3 wish = fwd * in.forward + right * in.side;
    float wishLen = length(wish);
    Vec3 wishDir = wishLen > 0 ? wish * (1.0f / wishLen) : Vec3{};
    float wishSpeed = wishLen > 0 ? weaponMaxSpeed : 0;
    if (ps.ducked) wishSpeed *= mp.duckScale;
    else if (in.walk) wishSpeed *= mp.walkScale;

    // Jump.
    if (in.jumpPressed && ps.onGround) {
        ps.velocity.z = mp.jumpImpulse;
        ps.onGround = false;
        ps.stamina = std::min(mp.staminaMax, ps.stamina + mp.staminaJumpCost);
    }

    if (ps.onGround) {
        wishSpeed *= 1.0f - ps.stamina / 100.0f;
        ps.velocity.z = 0;
        applyFriction(ps, mp);
        accelerate(ps, wishDir, wishSpeed, wishSpeed, mp.accelerate);
        walkMove(ps, world, mp);
    } else {
        accelerate(ps, wishDir, wishSpeed, std::min(wishSpeed, mp.airWishCap), mp.airAccelerate);
        // Half gravity before and after the move gives an exact parabola at any tick rate.
        ps.velocity.z -= mp.gravity * 0.5f * kDt;
        tryPlayerMove(ps, world);
        ps.velocity.z -= mp.gravity * 0.5f * kDt;
    }

    // Categorize position.
    bool wasOnGround = ps.onGround;
    Vec3 groundPos;
    // While walking, stick to the ground down steps/stairs instead of briefly going airborne.
    float snap = wasOnGround ? mp.stepSize + 2.0f : 2.0f;
    if (ps.velocity.z <= 140.0f && traceGround(ps, world, snap, &groundPos)) {
        ps.origin = groundPos;
        if (!wasOnGround) {
            // Landing: lose some horizontal speed according to stamina (anti bunny-hop).
            ps.stamina = std::min(mp.staminaMax, ps.stamina + mp.staminaLandCost);
            float keep = 1.0f - ps.stamina / 100.0f;
            ps.velocity.x *= keep;
            ps.velocity.y *= keep;
        }
        ps.velocity.z = 0;
        ps.onGround = true;
    } else {
        ps.onGround = false;
    }
}
