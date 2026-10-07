// Headless tests for the simulation core: movement feel numbers, collision, weapon determinism.
// Prints the measured values so tuning changes are visible in CI logs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "combat.h"
#include "movement.h"
#include "world.h"

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
            ++g_failures;                                             \
        }                                                             \
    } while (0)

const World& lab() {
    static World w = buildFeelLab();
    return w;
}

PlayerState spawnAt(Vec3 p) {
    PlayerState ps;
    ps.origin = p;
    ps.onGround = true;
    return ps;
}

void run(PlayerState& ps, const MoveInput& in, float yaw, float maxSpeed, int ticks) {
    for (int i = 0; i < ticks; ++i) playerMove(ps, in, yaw, maxSpeed, lab());
}

void testMaxSpeed() {
    std::printf("max speed\n");
    PlayerState ps = spawnAt({-400, 0, 0});
    MoveInput in;
    in.forward = 1;
    run(ps, in, 0, 215, kTickRate);  // 1 second running in +X along an open strip
    float sp = length2d(ps.velocity);
    std::printf("  rifle run speed after 1s: %.2f\n", sp);
    CHECK(sp > 214.5f && sp <= 215.01f, "speed %.2f", sp);

    PlayerState w = spawnAt({-400, 0, 0});
    in.walk = true;
    run(w, in, 0, 215, kTickRate);
    std::printf("  rifle walk speed: %.2f\n", length2d(w.velocity));
    CHECK(length2d(w.velocity) < 0.53f * 215 && length2d(w.velocity) > 0.5f * 215, "walk %.2f", length2d(w.velocity));
}

void testJumpHeight() {
    std::printf("jump\n");
    PlayerState ps = spawnAt({0, 0, 0});
    MoveInput in;
    in.jumpPressed = true;
    playerMove(ps, in, 0, 215, lab());
    in.jumpPressed = false;
    float apex = 0;
    int airTicks = 1;
    while (!ps.onGround && airTicks < 1000) {
        playerMove(ps, in, 0, 215, lab());
        apex = std::max(apex, ps.origin.z);
        ++airTicks;
    }
    std::printf("  apex %.2f units, airtime %.0f ms\n", apex, airTicks * kTickDt * 1000);
    CHECK(apex > 55.5f && apex < 57.5f, "apex %.2f", apex);
    CHECK(ps.onGround && ps.origin.z > -0.01f && ps.origin.z < 0.1f, "landed z %.3f", ps.origin.z);
}

// Run sideways at full speed, then either release or press the opposite key. Returns ms until accurate.
float stopTimeMs(bool counter) {
    PlayerState ps = spawnAt({0, -100, 0});
    MoveInput in;
    in.side = 1;  // run right (-Y)
    run(ps, in, 0, 215, kTickRate / 2);
    in.side = counter ? -1.0f : 0.0f;
    float threshold = 215 * 0.34f;
    for (int t = 1; t < kTickRate; ++t) {
        playerMove(ps, in, 0, 215, lab());
        if (length2d(ps.velocity) <= threshold) return t * kTickDt * 1000;
    }
    return 9999;
}

void testCounterStrafe() {
    std::printf("counter-strafe\n");
    float release = stopTimeMs(false), counter = stopTimeMs(true);
    std::printf("  time to accurate: release %.1f ms, counter-strafe %.1f ms\n", release, counter);
    CHECK(counter < release, "counter %.1f release %.1f", counter, release);
    CHECK(counter < 120, "counter-strafe should be fast: %.1f ms", counter);
}

void testWallCollision() {
    std::printf("collision\n");
    // Walk into the back wall (x = -512) and make sure we stop at it.
    PlayerState ps = spawnAt({-300, 0, 0});
    MoveInput in;
    in.forward = 1;
    run(ps, in, 180, 250, kTickRate * 2);
    std::printf("  stopped at x = %.3f (wall face -512, hull half-width 16)\n", ps.origin.x);
    CHECK(ps.origin.x >= -496.1f && ps.origin.x < -495.9f, "x %.3f", ps.origin.x);
    CHECK(ps.onGround, "still on ground");

    // Slide along the wall diagonally: should keep moving along Y.
    PlayerState s = spawnAt({-490, 0, 0});
    MoveInput diag;
    diag.forward = 1;
    diag.side = -1;  // forward (-X) + left
    run(s, diag, 180, 250, kTickRate);
    std::printf("  wall slide moved %.1f units along wall\n", std::fabs(s.origin.y));
    CHECK(std::fabs(s.origin.y) > 100, "slide %.1f", s.origin.y);
}

void testStairs() {
    std::printf("stairs\n");
    PlayerState ps = spawnAt({600, 464, 0});
    MoveInput in;
    in.forward = 1;
    run(ps, in, 0, 250, kTickRate * 3 / 2);
    std::printf("  after 1.5s walking up stairs: x %.1f z %.2f\n", ps.origin.x, ps.origin.z);
    CHECK(ps.origin.z > 127.9f && ps.origin.z < 128.1f, "should be on platform, z %.2f", ps.origin.z);

    // And back down, staying on the ground the whole way.
    int airborneTicks = 0;
    for (int i = 0; i < kTickRate * 2; ++i) {
        playerMove(ps, in, 180, 250, lab());
        if (!ps.onGround) ++airborneTicks;
    }
    std::printf("  walked down: z %.2f, airborne ticks %d\n", ps.origin.z, airborneTicks);
    CHECK(ps.origin.z < 0.1f, "back on floor z %.2f", ps.origin.z);
    CHECK(airborneTicks < 10, "should stick to stairs, airborne %d", airborneTicks);
}

// Run at a crate and jump (optionally crouching in the air) from a range of take-off distances.
// Returns true if any take-off point ends with the player standing on top.
bool reachesCrateTop(float crateX, float crateHeight, bool crouchJump) {
    const float face = 480;  // crates span y 480..544
    for (float takeoff = 0; takeoff <= 200; takeoff += 4) {
        PlayerState ps = spawnAt({crateX + 32, 200, 0});
        MoveInput in;
        in.forward = 1;
        const float yaw = 90;  // face +Y
        for (int t = 0; t < 1000 && ps.origin.y + kHullHalfWidth < face - takeoff; ++t)
            playerMove(ps, in, yaw, 250, lab());
        in.jumpPressed = true;
        playerMove(ps, in, yaw, 250, lab());
        in.jumpPressed = false;
        in.duck = crouchJump;
        run(ps, in, yaw, 250, kTickRate);
        if (ps.onGround && ps.origin.z > crateHeight - 0.1f) return true;
    }
    return false;
}

void testCrates() {
    std::printf("crates\n");
    bool c32 = reachesCrateTop(128, 32, false);
    bool c64 = reachesCrateTop(256, 64, false);
    bool c64c = reachesCrateTop(256, 64, true);
    bool c80c = reachesCrateTop(384, 80, true);
    std::printf("  32: jump %d | 64: jump %d, crouch-jump %d | 80: crouch-jump %d\n", c32, c64, c64c, c80c);
    CHECK(c32, "32-unit crate is jumpable");
    CHECK(!c64, "64-unit crate needs a crouch-jump");
    CHECK(c64c, "64-unit crate reachable with crouch-jump");
    CHECK(!c80c, "80-unit crate is out of reach");
}

void testNoAutoBhop() {
    std::printf("bunny-hop dampening\n");
    PlayerState ps = spawnAt({-400, 0, 0});
    MoveInput in;
    in.forward = 1;
    run(ps, in, 0, 250, kTickRate);
    float start = length2d(ps.velocity);
    // Perfectly timed re-jumps with no air strafing.
    for (int hop = 0; hop < 5; ++hop) {
        in.jumpPressed = true;
        playerMove(ps, in, 0, 250, lab());
        in.jumpPressed = false;
        while (!ps.onGround) playerMove(ps, in, 0, 250, lab());
    }
    float end = length2d(ps.velocity);
    std::printf("  speed before %.1f, after 5 hops %.1f\n", start, end);
    CHECK(end < start, "chained hops should lose speed");
}

void testDeterminism() {
    std::printf("determinism\n");
    auto simulate = [] {
        PlayerState ps = spawnAt({0, 0, 0});
        for (int t = 0; t < kTickRate * 5; ++t) {
            MoveInput in;
            in.forward = (t / 50) % 2 ? 1.0f : 0.0f;
            in.side = (t / 37) % 3 == 0 ? -1.0f : ((t / 37) % 3 == 1 ? 1.0f : 0.0f);
            in.jumpPressed = t % 97 == 0;
            in.duck = (t / 200) % 2 == 1;
            playerMove(ps, in, float(t % 360), 215, lab());
        }
        return ps;
    };
    PlayerState a = simulate(), b = simulate();
    bool same = std::memcmp(&a.origin, &b.origin, sizeof(Vec3)) == 0 &&
                std::memcmp(&a.velocity, &b.velocity, sizeof(Vec3)) == 0;
    std::printf("  final origin %.4f %.4f %.4f\n", a.origin.x, a.origin.y, a.origin.z);
    CHECK(same, "identical inputs must give bit-identical results");
}

void testWeapon() {
    std::printf("weapon\n");
    World w = lab();
    std::vector<Dummy> dummies = buildDummies();
    std::vector<Vec3> renderPos;
    for (const Dummy& d : dummies) renderPos.push_back(d.pos);

    // First dummy stands at (512, 0, 0). Aim at its head center (z 64) from eye height 64 at origin.
    WeaponState ws;
    ws.def = &rifleDef();
    ws.ammo = 30;
    Vec3 eye{0, 0, 64};
    ShotResult r = fireBullet(ws, eye, 0, 0, 0, true, false, w, dummies, renderPos);
    std::printf("  standing first shot: dummy %d group %s dmg %.1f kill %d\n", r.dummyIndex, hitGroupName(r.group),
                r.damage, r.kill);
    CHECK(r.dummyIndex == 0 && r.group == kHead && r.kill, "first shot standing still must headshot");

    // Moving at full speed: spread is large.
    WeaponState moving;
    moving.def = &rifleDef();
    float inacc = currentInaccuracy(moving, 215, true, false);
    float accurate = currentInaccuracy(moving, 215 * 0.34f, true, false);
    std::printf("  spread: running %.2f deg, at accuracy threshold %.2f deg, airborne %.2f deg\n", inacc, accurate,
                currentInaccuracy(moving, 0, false, false));
    CHECK(accurate == 0.0f, "accurate at threshold");
    CHECK(inacc > 4.0f, "running is inaccurate");

    // Spray pattern is deterministic: two identical sprays land identically.
    auto spray = [&]() {
        WeaponState s;
        s.def = &rifleDef();
        s.ammo = 30;
        std::vector<Dummy> none;
        std::vector<Vec3> nonePos;
        std::vector<Vec3> ends;
        for (int i = 0; i < 30; ++i) {
            ShotResult sr = fireBullet(s, Vec3{0, -700, 64}, 0, 0, 0, true, false, w, none, nonePos);
            ends.push_back(sr.end);
        }
        return ends;
    };
    std::vector<Vec3> s1 = spray(), s2 = spray();
    bool same = std::memcmp(s1.data(), s2.data(), s1.size() * sizeof(Vec3)) == 0;
    CHECK(same, "spray must be deterministic");
    RecoilStep total = recoilAt(rifleDef(), 29);
    std::printf("  full spray recoil: up %.1f deg, right %.1f deg\n", total.up, total.right);

    // Recoil recovery time after a full spray and after a single tap.
    auto recover = [](float index) {
        WeaponState s;
        s.def = &rifleDef();
        s.recoilIndex = index;
        int t = 0;
        while (s.recoilIndex > 0 && t < 10000) { decayRecoil(s, kTickDt); ++t; }
        return t * kTickDt * 1000;
    };
    std::printf("  recoil reset: 1 shot %.0f ms, 30 shots %.0f ms\n", recover(1), recover(29));
    CHECK(recover(1) < 300, "tap recovery");
    CHECK(recover(29) < 1300, "spray recovery");
}

void testRayVsBoxes() {
    std::printf("ray traces\n");
    TraceResult tr = lab().traceRay({0, 0, 64}, {4000, 0, 64});
    float hitX = tr.endpos.x;
    std::printf("  ray down the range hits far wall at x %.2f\n", hitX);
    CHECK(tr.fraction < 1 && hitX > 2559 && hitX < 2560.01f, "x %.2f", hitX);
}

}  // namespace

int main() {
    testMaxSpeed();
    testJumpHeight();
    testCounterStrafe();
    testWallCollision();
    testStairs();
    testCrates();
    testNoAutoBhop();
    testDeterminism();
    testWeapon();
    testRayVsBoxes();
    if (g_failures) {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
