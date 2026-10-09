// Headless tests for the simulation core: movement feel numbers, collision, weapon determinism.
// Prints the measured values so tuning changes are visible in CI logs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "combat.h"
#include "items.h"
#include "stats.h"
#include "replay.h"

#include "bots.h"
#include "movement.h"
#include "nav.h"
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
    static World w = buildLab();
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
    PlayerState a = spawnAt({-400, 0, 0});
    int ticks = 0;
    while (length2d(a.velocity) < 0.95f * 215 && ticks < kTickRate) { run(a, in, 0, 215, 1); ++ticks; }
    std::printf("  standing start to 95%% of rifle speed: %.0f ms\n", double(ticks) * kTickDt * 1000.0);
    CHECK(ticks * kTickDt > 0.30f && ticks * kTickDt < 0.35f, "ticks %d", ticks);  // ~290 ms at the old 7.2

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
        for (int t = 0; t < kTickRate; ++t) {  // did we ever stand on top?
            playerMove(ps, in, yaw, 250, lab());
            if (ps.onGround && ps.origin.z > crateHeight - 0.1f) return true;
        }
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

void testBhop() {
    std::printf("bunny hop (bhop 1)\n");
    MoveParams bh;
    bh.staminaJumpCost = 0;
    bh.staminaLandCost = 0;
    auto hops = [&](bool strafe) {
        PlayerState ps = spawnAt({-400, 0, 0});
        MoveInput in;
        in.forward = 1;
        for (int t = 0; t < kTickRate; ++t) playerMove(ps, in, 0, 250, lab(), bh);
        float yaw = 0;
        in.forward = 0;
        in.side = strafe ? -1.0f : 0.0f;  // hold A while turning left: classic air strafe
        if (!strafe) in.forward = 1;
        int landings = 0;
        for (int t = 0; t < kTickRate * 6 && landings < 6; ++t) {
            in.jumpPressed = true;  // holding jump = auto-hop
            bool wasGround = ps.onGround;
            playerMove(ps, in, yaw, 250, lab(), bh);
            if (strafe && !ps.onGround) yaw += 1.1f;
            if (!wasGround && ps.onGround) ++landings;
        }
        return length2d(ps.velocity);
    };
    float straight = hops(false), strafed = hops(true);
    std::printf("  speed after 6 hops: straight %.1f, air-strafing %.1f (start 250)\n", straight, strafed);
    CHECK(straight > 249.0f, "perfect hops keep speed: %.1f", straight);
    CHECK(strafed > 275.0f, "air strafing gains speed: %.1f", strafed);
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

// Bot skill: each level up reacts faster, aims tighter, tracks you more closely and goes for the head more.
void testBotSkills() {
    std::printf("bot skill\n");
    int wrong = 0;
    for (int l = 0; l < 4; ++l) {
        const BotSkill& s = botSkill(l);
        std::printf("  %-6s reaction %.2f-%.2f s, aim error x%.2f, fire x%.2f, aims %.0f ms behind, head %.0f%%\n", s.name,
                    double(s.reactMin), double(s.reactMin + s.reactRange), double(s.aimError), double(s.fireScale),
                    s.lagTicks * 1000.0 / kTickRate, double(s.headChance) * 100.0);
        if (l > 0) {
            const BotSkill& e = botSkill(l - 1);
            wrong += !(s.reactMin < e.reactMin && s.aimError < e.aimError && s.lagTicks < e.lagTicks &&
                       s.headChance >= e.headChance && s.fireScale <= e.fireScale);
        }
    }
    CHECK(wrong == 0 && botSkill(1).aimError == 1.0f, "%d", wrong);
    // Skill variance: skills between the levels blend them, get steadily better, and hit the levels exactly.
    int blendWrong = 0;
    float lastAim = 1e9f, lastReact = 1e9f;
    for (int k = 0; k <= 30; ++k) {
        const BotSkill s = skillAt(float(k) * 0.1f);
        blendWrong += !(s.aimError <= lastAim && s.reactMin <= lastReact);
        lastAim = s.aimError;
        lastReact = s.reactMin;
    }
    for (int l = 0; l < 4; ++l) blendWrong += skillAt(float(l)).aimError != botSkill(l).aimError;
    const BotSkill hardMinus = skillAt(1.5f), hardPlus = skillAt(2.5f);
    std::printf("  variance: HARD -0.5 aim error x%.2f, HARD +0.5 x%.2f (HARD x%.2f)\n", double(hardMinus.aimError),
                double(hardPlus.aimError), double(botSkill(2).aimError));
    CHECK(blendWrong == 0 && skillAt(-1).aimError == botSkill(0).aimError && skillAt(9).aimError == botSkill(3).aimError,
          "blend %d", blendWrong);
}

// Fire timing: a held trigger keeps the exact cadence; taps and clicks, however fast or badly timed,
// never get two shots closer together than one fire interval.
void testFireTiming() {
    std::printf("fire timing\n");
    for (int id = 0; id < kWeaponCount; ++id) {
        const WeaponDef* def = &weaponDef(id);
        if (!def->canFire) continue;
        WeaponState ws;
        ws.def = def;
        // Held for 30 shots.
        std::vector<double> shots;
        for (int t = 0; shots.size() < 30; ++t)
            if (takeShotTiming(ws, double(t) * kTickDt)) shots.push_back(double(t) * kTickDt);
        const double held = shots.back() - shots.front(), want = 29.0 * double(def->fireInterval);
        // Taps: the trigger pressed for a few ticks, released, pressed again after gaps of 1.0-2.5 intervals.
        ws = WeaponState{};
        ws.def = def;
        double t = 0, last = -1, closest = 1e9;
        for (int k = 0; k < 400; ++k) {
            const int hold = 1 + k % 7;  // ticks held each press
            for (int h = 0; h < hold; ++h, t += kTickDt)
                if (takeShotTiming(ws, t)) {
                    if (last >= 0) closest = std::min(closest, t - last);
                    last = t;
                }
            t += double(def->fireInterval) * (1.0 + 0.015 * double(k % 100));  // the gap before the next press
        }
        std::printf("  %s: 30 held shots in %.3f s (exact %.3f); tapping, closest two shots %.0f ms (interval %.0f ms)\n",
                    def->name, held, want, closest * 1000.0, double(def->fireInterval) * 1000.0);
        CHECK(std::fabs(held - want) <= double(kTickDt) && closest >= double(def->fireInterval) - 1e-9,
              "%s held %.4f closest %.4f", def->name, held, closest);
    }
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

    // Default: no random spread at all, even running or airborne.
    WeaponState plain;
    plain.def = &rifleDef();
    plain.recoilIndex = 10;
    CHECK(currentInaccuracy(plain, 215, false, false) == 0.0f, "spread must be off by default");

    // With movement spread enabled: running is inaccurate, counter-strafed is perfect.
    WeaponState moving;
    moving.def = &rifleDef();
    moving.moveSpread = true;
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

void testWallbang() {
    std::printf("wallbang\n");
    World w = lab();
    std::vector<Dummy> dummies = buildDummies();
    std::vector<Vec3> pos;
    for (const Dummy& d : dummies) pos.push_back(d.pos);
    // Dummy 6 stands behind the solid part of the peek wall (x 1280..1296, 16 units thick).
    Vec3 eye{1000, 700, 64}, target = pos[6] + Vec3{0, 0, 52};
    Vec3 d = normalize(target - eye);
    float pitch = -std::asin(d.z) / kDegToRad, yaw = std::atan2(d.y, d.x) / kDegToRad;
    for (const WeaponDef* def : {&rifleDef(), &pistolDef()}) {
        WeaponState ws;
        ws.def = def;
        std::vector<Dummy> dd = dummies;
        ShotResult r = fireBullet(ws, eye, pitch, yaw, 0, true, false, w, dd, pos);
        std::printf("  %s through 16u wall: dummy %d walls %d dmg %.1f\n", def->name, r.dummyIndex, r.penCount,
                    r.damage);
        if (def == &rifleDef()) CHECK(r.dummyIndex == 6 && r.penCount == 1 && r.damage < 36.0f, "rifle wallbang");
        else CHECK(r.dummyIndex < 0, "pistol cannot wallbang");
    }
}

// Ramps (wedges): walk up and down smoothly, rays land on the slope, jumping onto one doesn't snag.
void testRamps() {
    std::printf("ramps\n");
    World w;
    w.solids.push_back({{-800, -400, -16}, {1200, 400, 0}, 0x808080});           // ground
    Box ramp{{0, -200, -16}, {400, 200, 100}, 0x808080};                          // rises 100 over 400 (14 deg)
    ramp.slope = kRisePosX;
    ramp.lowZ = 0;
    w.solids.push_back(ramp);
    w.solids.push_back({{400, -200, -16}, {700, 200, 100}, 0x808080});            // platform at the top
    MoveParams bhop;  // no landing slowdown, so a snag on the ramp would show up as lost speed
    bhop.staminaJumpCost = bhop.staminaLandCost = 0;
    auto runOn = [&](PlayerState& ps, float yaw, int ticks, int& airborne, float& minSpeed, bool jumpAtRamp) {
        MoveInput in;
        in.forward = 1;
        airborne = 0;
        minSpeed = 1e9f;
        for (int t = 0; t < ticks; ++t) {
            in.jumpPressed = jumpAtRamp && ps.onGround && ps.origin.x > -40 && ps.origin.x < -20;
            playerMove(ps, in, yaw, 250, w, bhop);
            if (!ps.onGround) ++airborne;
            if (ps.origin.x > 20 && ps.origin.x < 380) minSpeed = std::min(minSpeed, length2d(ps.velocity));
        }
    };
    int air;
    float minSpeed;
    PlayerState up = spawnAt({-300, 0, 0});
    runOn(up, 0, kTickRate * 3, air, minSpeed, false);
    std::printf("  walk up: ended at x %.0f z %.1f, slowest on the ramp %.0f u/s, airborne ticks %d\n",
                double(up.origin.x), double(up.origin.z), double(minSpeed), air);
    CHECK(up.origin.x > 400 && std::fabs(up.origin.z - 100) < 1 && minSpeed > 225 && air == 0, "up");
    PlayerState down = spawnAt({650, 0, 100});
    runOn(down, 180, kTickRate * 3, air, minSpeed, false);
    std::printf("  walk down: ended at x %.0f z %.1f, airborne ticks %d\n", double(down.origin.x),
                double(down.origin.z), air);
    CHECK(down.origin.x < -50 && std::fabs(down.origin.z) < 1 && air == 0, "down");
    TraceResult tr = w.traceRay({200, 0, 300}, {200, 0, -50});
    std::printf("  ray down at x 200: z %.2f, normal z %.3f\n", double(tr.endpos.z), double(tr.normal.z));
    CHECK(std::fabs(tr.endpos.z - 50) < 0.1f && tr.normal.z > 0.96f && tr.normal.z < 0.98f, "ray");
    PlayerState hop = spawnAt({-300, 0, 0});
    runOn(hop, 0, kTickRate * 3, air, minSpeed, true);
    std::printf("  jump onto the ramp: slowest on it %.0f u/s\n", double(minSpeed));
    // Landing on an upward slope turns some speed into the slope (Source does the same); a snag on a
    // step edge would stop you dead.
    CHECK(minSpeed > 150 && hop.origin.x > 300, "jump onto ramp kept speed %.0f", double(minSpeed));
}

// Materials: the rifle (24 units of penetration) goes through 40 units of wood but not 40 of stone.
void testMaterialWallbang() {
    std::printf("material wallbang\n");
    std::vector<Dummy> dummies(1);
    dummies[0].pos = dummies[0].prevPos = {300, 0, 0};
    std::vector<Vec3> pos{dummies[0].pos};
    for (uint8_t mat : {uint8_t(kMatWood), uint8_t(kMatStone), uint8_t(kMatMetal)}) {
        World w;
        w.solids.push_back({{-512, -512, -16}, {512, 512, 0}, 0x808080});
        w.solids.push_back({{150, -64, 0}, {190, 64, 128}, 0x7a5230, mat});  // 40 units thick
        WeaponState ws;
        ws.def = &rifleDef();
        std::vector<Dummy> dd = dummies;
        ShotResult r = fireBullet(ws, {0, 0, 52}, 0, 0, 0, true, false, w, dd, pos);
        const char* name = mat == kMatWood ? "wood" : mat == kMatMetal ? "metal" : "stone";
        std::printf("  rifle through 40u of %-5s: %s (dmg %.1f)\n", name, r.dummyIndex == 0 ? "hit" : "stopped",
                    double(r.damage));
        CHECK((r.dummyIndex == 0) == (mat == kMatWood), "%s", name);
    }
}

void testRayVsBoxes() {
    std::printf("ray traces\n");
    TraceResult tr = lab().traceRay({0, 0, 64}, {4000, 0, 64});
    float hitX = tr.endpos.x;
    std::printf("  ray down the range hits far wall at x %.2f\n", hitX);
    CHECK(tr.fraction < 1 && hitX > 2559 && hitX < 2560.01f, "x %.2f", hitX);
}

// ---- Dust (Dust2 at a scale of the real map; 60% by default) ----

const World& dust() {
    static World w = buildTown();
    return w;
}

// The bots' navigation grid for Dust (the same code deathmatch bots use to find their way).
const NavGrid& dustNav() {
    static const NavGrid nav = [] {
        NavGrid n;
        n.build(townGrid(), dust(), townSpawn().pos);
        return n;
    }();
    return nav;
}

bool dustRoute(Vec3 from, Vec3 to, std::vector<Vec3>& out) { return dustNav().findPath(from, to, out); }

// A point given in real-Dust2 coordinates, at the map's current scale, on the floor.
Vec3 dpt(float x, float y) {
    float s = townScale();
    return {x * s, y * s, townGrid().floorAt(x * s, y * s)};
}

// Runs a simulated player along `path` (holding W, steering at a point a little ahead) and returns
// the time taken, or -1 if they got stuck.
float runRoute(const std::vector<Vec3>& path, float speed, float* distance, const World* world = nullptr) {
    PlayerState ps = spawnAt(path.front());
    size_t wp = 1;
    int ticks = 0, lastProgress = 0;
    float travelled = 0;
    while (wp < path.size()) {
        Vec3 d = path[wp] - ps.origin;
        d.z = 0;
        if (length(d) < 40.0f) { ++wp; lastProgress = ticks; continue; }
        Vec3 aim = path[std::min(wp + 2, path.size() - 1)] - ps.origin;
        MoveInput in;
        in.forward = 1;
        Vec3 before = ps.origin;
        playerMove(ps, in, std::atan2(aim.y, aim.x) / kDegToRad, speed, world ? *world : dust());
        travelled += length2d(ps.origin - before);
        if (++ticks - lastProgress > kTickRate * 3) return -1.0f;  // no waypoint for 3 s: stuck
    }
    if (distance) *distance = travelled;
    return float(ticks) * kTickDt;
}

struct Landmark { const char* name; Vec3 pos; };

void testDustMap() {
    std::printf("dust map\n");
    const World& w = dust();
    std::printf("  %zu boxes, broadphase %s\n", w.solids.size(), w.indexed() ? "on" : "off");
    CHECK(w.indexed() && w.solids.size() < 3000, "boxes %zu", w.solids.size());

    MapSpawn sp = townSpawn();
    CHECK(w.boxFits(sp.pos + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false)), "T spawn is clear");
    CHECK(std::string(townCallout(sp.pos)) == "T SPAWN", "callout %s", townCallout(sp.pos));

    // Every bot spot is standing room on solid floor.
    for (const PeekSpot& s : townPeekSpots())
        for (Vec3 p : {s.cover, s.peek}) {
            bool clear = p.z > MapGrid::kNoFloor && w.boxFits(p + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false));
            TraceResult tr = w.traceBox(p + Vec3{0, 0, 0.5f}, p - Vec3{0, 0, 4}, hullMins(), hullMaxs(false));
            CHECK(clear && tr.fraction < 1.0f, "bot spot (%.0f, %.0f) clear %d on floor %d", p.x, p.y, clear,
                  tr.fraction < 1.0f);
        }
}

// The broadphase must give exactly the same answers as testing every box.
void testDustBroadphase() {
    std::printf("dust broadphase\n");
    World brute;
    brute.solids = dust().solids;  // same boxes, no index
    uint32_t rng = 12345u;
    auto r = [&](float lo, float hi) {
        rng = rng * 1664525u + 1013904223u;
        return lo + (hi - lo) * float(rng >> 8) / float(1u << 24);
    };
    int mismatches = 0;
    for (int k = 0; k < 4000; ++k) {
        Vec3 a{r(-2400, 1950), r(-1200, 3250), r(-150, 300)};
        Vec3 b = k % 2 ? Vec3{r(-2400, 1950), r(-1200, 3250), r(-150, 300)}
                       : a + Vec3{r(-64, 64), r(-64, 64), r(-32, 32)};
        Vec3 mn = k % 3 ? hullMins() : Vec3{}, mx = k % 3 ? hullMaxs(false) : Vec3{};
        TraceResult x = dust().traceBox(a, b, mn, mx), y = brute.traceBox(a, b, mn, mx);
        if (x.fraction != y.fraction || x.startSolid != y.startSolid) ++mismatches;
        if (dust().boxFits(a, mn, mx) != brute.boxFits(a, mn, mx)) ++mismatches;
    }
    std::printf("  4000 random traces + fits: %d mismatches\n", mismatches);
    CHECK(mismatches == 0, "%d", mismatches);
}

// Deathmatch: spawns land all over the map, bots left alone roam the whole map, and a bot walking a
// route with followPath keeps its feet on the floor.
void testDustDeathmatch() {
    std::printf("dust deathmatch\n");
    const NavGrid& nav = dustNav();
    uint32_t rng = 777u;
    const std::vector<Vec3> none;
    std::set<std::string> spawnAreas;
    int badSpawns = 0;
    for (int k = 0; k < 300; ++k) {
        Vec3 p = randomSpawnPoint(nav, dust(), none, 0, none, rng);
        // Like the game: stand on the real ground under the spot (ramps are smooth, the grid is stepped).
        TraceResult down = dust().traceBox(p + Vec3{0, 0, 24}, p - Vec3{0, 0, 24}, hullMins(), hullMaxs(false));
        badSpawns += !(nav.roamable(p) && down.fraction < 1.0f && !down.startSolid &&
                       dust().boxFits(down.endpos, hullMins(), hullMaxs(false)));
        spawnAreas.insert(townCallout(p));
    }
    std::printf("  %zu roamable cells; 300 spawns landed in %zu different areas, %d bad\n", nav.roamCount(),
                spawnAreas.size(), badSpawns);
    CHECK(badSpawns == 0 && spawnAreas.size() >= 16, "areas %zu bad %d", spawnAreas.size(), badSpawns);

    // 10 bots roam for two minutes with nobody to fight.
    const int kBots = 10;
    std::vector<Dummy> dd(kBots);
    std::vector<BotBrain> bb(kBots);
    std::vector<Vec3> taken;
    for (int i = 0; i < kBots; ++i) {
        spawnDeathmatchBot(dd[size_t(i)], bb[size_t(i)], randomSpawnPoint(nav, dust(), none, 0, taken, rng), rng);
        taken.push_back(dd[size_t(i)].pos);
    }
    BotSenses sense;
    sense.world = &dust();
    sense.nav = &nav;
    std::vector<float> walked(kBots, 0.0f);
    std::vector<std::set<std::string>> seen(kBots);
    std::set<std::string> all;
    float worstSink = 0;  // feet below the floor (off a ledge they glide down, so above it is fine)
    for (int t = 0; t < kTickRate * 120; ++t) {
        sense.now = double(t) * kTickDt;
        for (int i = 0; i < kBots; ++i) {
            Dummy& d = dd[size_t(i)];
            d.prevPos = d.pos;
            d.prevYaw = d.yaw;
            updateDeathmatchBot(d, bb[size_t(i)], sense, rng);
            walked[size_t(i)] += length2d(d.pos - d.prevPos);
            seen[size_t(i)].insert(townCallout(d.pos));
            all.insert(townCallout(d.pos));
            worstSink = std::max(worstSink, townGrid().floorAt(d.pos.x, d.pos.y) - d.pos.z);
        }
    }
    float minWalk = *std::min_element(walked.begin(), walked.end());
    size_t minAreas = 1000;
    for (const auto& s : seen) minAreas = std::min(minAreas, s.size());
    std::printf("  10 bots, 2 min: each walked at least %.0f units and saw at least %zu areas; %zu areas in all; "
                "feet at most %.1f into the floor\n", double(minWalk), minAreas, all.size(), double(worstSink));
    CHECK(minWalk > 12000.0f && minAreas >= 6 && all.size() >= 20 && worstSink <= 8.5f, "walk %.0f areas %zu/%zu",
          double(minWalk), minAreas, all.size());

    // Sight: a bot looking away doesn't see you; once you shoot it, it turns on you.
    Dummy d;
    BotBrain b;
    spawnDeathmatchBot(d, b, dpt(-350, -800), rng);
    d.yaw = d.prevYaw = -90;  // facing south, you're to the north
    sense.playerUp = true;
    sense.playerOrigin = dpt(-350, -300);
    sense.playerEye = sense.playerOrigin + Vec3{0, 0, kStandEye};
    bool aimedBehind = false;
    for (int t = 0; t < kTickRate; ++t) {
        sense.now = double(t) * kTickDt;
        d.prevPos = d.pos;
        updateDeathmatchBot(d, b, sense, rng);
        aimedBehind |= b.aimed;
        if (b.state == 0) b.path.clear(), b.state = 1, b.timer = 10;  // keep it standing still
    }
    b.alertUntil = sense.now + 2.0;  // you shot it
    int ticksToAim = -1;
    for (int t = 0; t < kTickRate && ticksToAim < 0; ++t) {
        sense.now += kTickDt;
        d.prevPos = d.pos;
        updateDeathmatchBot(d, b, sense, rng);
        if (b.aimed) ticksToAim = t;
    }
    std::printf("  bot facing away saw you: %s; after being shot it faced you in %.0f ms\n", aimedBehind ? "yes" : "no",
                double(ticksToAim) * kTickDt * 1000.0);
    CHECK(!aimedBehind && ticksToAim >= 0 && ticksToAim < kTickRate / 2, "behind %d aim %d", int(aimedBehind), ticksToAim);

    // A bot walks from the CT end of B site to the pit: through doors, tunnels, ramps and stairs.
    std::vector<Vec3> path;
    bool routed = dustNav().findPath(dpt(-1700, 2300), dpt(1700, 420), path);
    CHECK(routed, "bot route");
    if (!routed) return;
    Vec3 pos = path.front();
    size_t next = 1;
    int ticks = 0;
    float worstZ = 0;
    while (!followPath(pos, path, next, 215.0f * kTickDt) && ticks < kTickRate * 60) {
        ++ticks;
        worstZ = std::max(worstZ, std::fabs(pos.z - townGrid().floorAt(pos.x, pos.y)));
    }
    std::printf("  bot B site -> pit: %.1f s at rifle speed, feet at most %.1f units off the floor\n",
                double(ticks) * kTickDt, double(worstZ));
    // Off a ledge (the side of the pit ramp) it falls, so mid-fall frames sit up to ~20 units above the floor.
    CHECK(next >= path.size() && worstZ <= 24.0f, "arrived %d, worst z %.1f", int(next >= path.size()), double(worstZ));
}

// Deathmatch with the bots fighting each other: a bot picks another bot it can see (never itself), and still you
// when you're the nearer one.
void testBotsFightEachOther() {
    std::printf("bots fight each other\n");
    const NavGrid& nav = dustNav();
    BotSenses sense;
    sense.world = &dust();
    sense.nav = &nav;
    uint32_t rng = 99u;
    Dummy a, b;
    BotBrain ba, bb;
    spawnDeathmatchBot(a, ba, dpt(-100, 900), rng);   // mid, looking up towards...
    spawnDeathmatchBot(b, bb, dpt(-100, 1500), rng);  // ...another bot further up mid
    a.yaw = 90;
    std::vector<BotTarget> everyone = {{0, a.pos, a.pos + Vec3{0, 0, 64}}, {1, b.pos, b.pos + Vec3{0, 0, 64}}};
    sense.targets = &everyone;
    sense.self = 0;
    sense.huntYou = true;
    bool pickedOther = false, pickedSelf = false;
    for (int t = 0; t < 64; ++t) {
        sense.now = t * kTickDt;
        everyone[0] = {0, a.pos, a.pos + Vec3{0, 0, 64}};
        updateDeathmatchBot(a, ba, sense, rng);
        pickedOther = pickedOther || ba.target == 1;
        pickedSelf = pickedSelf || ba.target == 0;
    }
    // You, much closer: it turns on you instead.
    sense.playerUp = true;
    sense.playerOrigin = a.pos + Vec3{0, 200, 0};
    sense.playerEye = sense.playerOrigin + Vec3{0, 0, kStandEye};
    everyone.push_back({-1, sense.playerOrigin, sense.playerEye});
    bool pickedYou = false;
    for (int t = 64; t < 256 && !pickedYou; ++t) {
        sense.now = t * kTickDt;
        updateDeathmatchBot(a, ba, sense, rng);
        pickedYou = ba.target == -1;
    }
    std::printf("  saw the other bot: %s, itself: %s, then you: %s\n", pickedOther ? "yes" : "no", pickedSelf ? "yes" : "no",
                pickedYou ? "yes" : "no");
    CHECK(pickedOther && !pickedSelf && pickedYou, "other %d self %d you %d", int(pickedOther), int(pickedSelf), int(pickedYou));
}

// Cover: the finder's spots really hide a bot (head and chest) from the threat and have a peek spot
// beside them it can shoot from; and a bot in a gunfight with cover nearby ducks out of sight
// between shots instead of standing in the open.
void testBotCover() {
    std::printf("bot cover\n");
    const NavGrid& nav = dustNav();
    BotSenses sense;
    sense.world = &dust();
    sense.nav = &nav;
    // Spots next to props (a prop's centre scales with the map; its size doesn't), threats in real coords.
    auto rel = [](float ax, float ay, float dx, float dy) {
        Vec3 p{ax * townScale() + dx, ay * townScale() + dy, 0};
        p.z = townGrid().floorAt(p.x, p.y);
        return p;
    };
    struct Case { const char* name; Vec3 at; float tx, ty; };
    const Case cases[] = {{"A default box vs ramp", rel(1245, 2695, 80, -60), 1550, 2150},
                          {"B default box vs tunnels", rel(-1890, 2540, 0, -90), -2080, 1900},
                          {"mid vs top mid", dpt(-100, 1200), -100, 250},
                          {"long crates vs long", rel(1290, 1200, 100, -80), 1500, 1900},
                          {"CT mid crate vs mid-to-B", rel(-470, 2130, 0, -70), -1000, 2200},
                          {"short vs catwalk", dpt(450, 2500), 200, 1700}};
    int found = 0, wrong = 0;
    for (const Case& c : cases) {
        const Vec3 around = c.at, eye = dpt(c.tx, c.ty) + Vec3{0, 0, 64};
        Vec3 cover, peek;
        if (!findCover(sense, around, eye, 200.0f, cover, peek)) {
            std::printf("  %-22s no cover\n", c.name);
            continue;
        }
        ++found;
        const bool hidden = dust().traceRay(eye, cover + Vec3{0, 0, 62}).fraction < 1.0f &&
                            dust().traceRay(eye, cover + Vec3{0, 0, 40}).fraction < 1.0f;
        const bool canShoot = dust().traceRay(eye, peek + Vec3{0, 0, 62}).fraction >= 1.0f;
        wrong += !hidden || !canShoot || !nav.standable(cover) || !nav.standable(peek);
        std::printf("  %-22s cover %3.0f units away, peek %2.0f from it%s\n", c.name, double(length2d(cover - around)),
                    double(length2d(peek - cover)), hidden && canShoot ? "" : "  WRONG");
    }
    CHECK(found >= 4 && wrong == 0, "found %d wrong %d", found, wrong);

    // A fight: you stand at the top of the A ramp; a bot beside the A default box sees you. Over 6 s it should spend
    // time hidden (in cover) and time aimed at you (peeking).
    uint32_t rng = 4242u;
    Dummy d;
    BotBrain b;
    spawnDeathmatchBot(d, b, rel(1245, 2695, 80, -60), rng);
    sense.playerUp = true;
    sense.playerOrigin = dpt(1550, 2150);
    sense.playerEye = sense.playerOrigin + Vec3{0, 0, kStandEye};
    d.yaw = d.prevYaw = std::atan2(sense.playerOrigin.y - d.pos.y, sense.playerOrigin.x - d.pos.x) / kDegToRad;
    int hiddenTicks = 0, aimedTicks = 0;
    for (int t = 0; t < kTickRate * 6; ++t) {
        sense.now = double(t) * kTickDt;
        d.prevPos = d.pos;
        updateDeathmatchBot(d, b, sense, rng);
        hiddenTicks += dust().traceRay(sense.playerEye, d.pos + Vec3{0, 0, 62}).fraction < 1.0f;
        aimedTicks += b.aimed;
    }
    std::printf("  6 s fight from A site: hidden %.1f s, aimed at you %.1f s, used cover: %s\n",
                double(hiddenTicks) * kTickDt, double(aimedTicks) * kTickDt, b.hasCover ? "yes" : "no");
    CHECK(hiddenTicks > kTickRate / 2 && aimedTicks > kTickRate, "hidden %d aimed %d", hiddenTicks, aimedTicks);
}

// Grenades fly like CS:GO's (675 u/s, aim lifted 10 degrees, 0.4x gravity), and the trajectory preview
// (predictGrenade) ends exactly where the real grenade, stepped tick by tick, goes off.
void testGrenades() {
    std::printf("grenades\n");
    World flat;
    flat.solids.push_back({{-5000, -5000, -64}, {5000, 5000, 0}, 0x808080});
    const Vec3 eye{0, 0, kStandEye};
    auto firstBounce = [&](float pitch, bool lob) {
        Vec3 v = grenadeThrowVelocity(pitch, 0, lob ? kNadeLob : 1.0f, Vec3{}), p = eye + normalize(v) * 16.0f;
        for (int t = 0; t < kTickRate * 10; ++t)
            if (stepGrenade(flat, p, v).bounced) break;
        return p.x;
    };
    const float far = firstBounce(-45, false), level = firstBounce(0, false), lob = firstBounce(-45, true);
    std::printf("  first bounce: 45 deg up %.0f units, looking level %.0f, 45 deg lob %.0f\n", double(far), double(level),
                double(lob));
    CHECK(far > 1300 && far < 1550 && level > 450 && level < 850 && lob < 200, "far %.0f level %.0f lob %.0f",
          double(far), double(level), double(lob));
    // Preview vs the real thing, for each type, thrown at an angle into a wall.
    flat.solids.push_back({{600, -5000, 0}, {700, 5000, 300}, 0x808080});
    int mismatches = 0;
    for (int type = 0; type < 4; ++type) {
        Vec3 v = grenadeThrowVelocity(-20, 15, 1.0f, Vec3{60, 0, 0}), p = eye + normalize(v) * 16.0f;
        const Vec3 predicted = predictGrenade(flat, p, v, type);
        const int fuse = int(grenadeFuse(type) * kTickRate + 0.5);
        for (int t = 1; t < kTickRate * 10; ++t) {
            const NadeStep st = stepGrenade(flat, p, v);
            const bool smokeReady = type != 0 || length(v) < 1.0f || t > fuse + 4 * kTickRate;
            if ((t >= fuse && smokeReady) || (type == 3 && st.landed)) break;
        }
        mismatches += length(p - predicted) > 1e-4f;
    }
    std::printf("  preview vs real flight (4 grenade types): %d mismatches\n", mismatches);
    CHECK(mismatches == 0, "%d", mismatches);
}

// Dummies turn (yaw); hitboxes turn with them. Facing you they're wider than side-on.
void testTurnedHitboxes() {
    std::printf("turned hitboxes\n");
    World empty;
    auto widthSeen = [&](float yaw) {
        std::vector<Dummy> dd(1);
        dd[0].shownYaw = yaw;
        std::vector<Vec3> pos{Vec3{}};
        int hits = 0;
        for (float y = -20; y <= 20; y += 0.5f) {  // sweep shots across the chest at z 52 from -X
            WeaponState ws;
            ws.def = &pistolDef();
            dd[0].hp = 1e9f;
            ShotResult r = fireBullet(ws, {-300, y, 52}, 0, 0, 0, true, false, empty, dd, pos);
            hits += r.dummyIndex == 0;
        }
        return float(hits) * 0.5f;
    };
    float facing = widthSeen(180), side = widthSeen(90), diag = widthSeen(135);
    std::printf("  chest-high width seen: facing %.1f, side-on %.1f, 45 deg %.1f units\n", double(facing), double(side),
                double(diag));
    CHECK(facing > 24.0f && side < 23.0f && diag > side, "facing %.1f side %.1f", double(facing), double(side));
}

// The added guns: the starting pistol and the Berettas can't one-tap a helmet, the Deagle can from across the
// map; the Nova's pellets go in the same fixed pattern every time and kill up close but not far away.
void testNewGuns() {
    std::printf("new guns\n");
    World empty;
    // One shot at the head (z 64) of a dummy `dist` units away, with or without a helmet.
    auto headshot = [&](const WeaponDef& def, float dist, bool helmet, float& dmg) {
        std::vector<Dummy> dd(1);
        dd[0].pos = dd[0].prevPos = {dist, 0, 0};
        dd[0].armor = helmet ? 100.0f : 0.0f;
        dd[0].helmet = helmet;
        std::vector<Vec3> pos{dd[0].pos};
        WeaponState ws;
        ws.def = &def;
        ShotResult r = fireBullet(ws, {0, 0, 64}, 0, 0, 0, true, false, empty, dd, pos);
        dmg = r.dummyIndex == 0 && r.group == kHead ? r.damage : 0.0f;
        return r.kill;
    };
    float d = 0;
    const bool pistolClose = headshot(pistolDef(), 64, true, d);
    std::printf("  pistol, helmet, 64 units: %.0f damage%s\n", double(d), pistolClose ? " KILL" : "");
    CHECK(!pistolClose && d > 80.0f, "pistol must not one-tap a helmet (%.0f)", double(d));
    CHECK(headshot(pistolDef(), 64, false, d), "pistol one-taps a bare head up close (%.0f)", double(d));
    const bool dualies = headshot(berettasDef(), 32, true, d);
    std::printf("  berettas, helmet, 32 units: %.0f damage%s\n", double(d), dualies ? " KILL" : "");
    CHECK(!dualies, "berettas must not one-tap a helmet (%.0f)", double(d));
    for (float dist : {64.0f, 2000.0f, 4000.0f, 6000.0f}) {
        const bool kill = headshot(deagleDef(), dist, true, d);
        std::printf("  deagle, helmet, %4.0f units: %.0f damage%s\n", double(dist), double(d), kill ? " KILL" : "");
        CHECK(kill, "deagle must one-tap a helmet at %.0f (%.0f)", double(dist), double(d));
    }
    // The shotguns into a chest (z 50): pellets spread at random (the owner's call), seeded by the shot, so the
    // same shot repeats exactly but the next one differs. Averaged over 40 shots: deadly close, weak far.
    auto pelletShot = [&](const WeaponDef& def, float dist, uint32_t shot, int& hits, float& total) {
        std::vector<Dummy> dd(1);
        dd[0].pos = dd[0].prevPos = {dist, 0, 0};
        dd[0].hp = 1e9f;
        std::vector<Vec3> pos{dd[0].pos};
        WeaponState ws;
        ws.def = &def;
        ws.shotCounter = shot;
        ShotResult out[kMaxPellets];
        const int n = firePellets(ws, {0, 0, 50}, 0, 0, 0, true, false, empty, dd, pos, out);
        hits = 0;
        total = 0;
        for (int k = 0; k < n; ++k)
            if (out[k].dummyIndex == 0) { ++hits; total += out[k].damage; }
        return n;
    };
    for (int id : {int(kWNova), int(kWXm1014)}) {
        const WeaponDef& def = weaponDef(id);
        int h1 = 0, h2 = 0, pellets = 0;
        float d1 = 0, d2 = 0, nearSum = 0, farSum = 0, widest = 0;
        pellets = pelletShot(def, 160, 7, h1, d1);
        pelletShot(def, 160, 7, h2, d2);
        CHECK(h1 == h2 && d1 == d2, "%s: the same shot must repeat exactly", def.name);
        bool differ = false;
        for (uint32_t shot = 0; shot < 40; ++shot) {
            int h = 0;
            float dmg = 0;
            pelletShot(def, 160, shot, h, dmg);
            nearSum += dmg;
            pelletShot(def, 1500, shot, h, dmg);
            farSum += dmg;
            for (int k = 0; k < def.pellets; ++k) {
                const RecoilStep o = pelletOffset(def, k, shot);
                widest = std::max(widest, std::sqrt(o.up * o.up + o.right * o.right));
                differ = differ || pelletOffset(def, k, shot).up != pelletOffset(def, k, shot + 1).up;
            }
        }
        std::printf("  %s: %d pellets, spread %.1f deg (widest %.2f); 160 units avg %.0f, 1500 units avg %.0f\n", def.name,
                    pellets, double(def.pelletSpread), double(widest), double(nearSum / 40), double(farSum / 40));
        CHECK(pellets == def.pellets && differ && widest <= def.pelletSpread + 1e-4f && nearSum / 40 >= 85.0f &&
                  farSum / 40 < 40.0f,
              "%s pellets %d near %.0f far %.0f", def.name, pellets, double(nearSum / 40), double(farSum / 40));
    }

    // The rifles, like CS: the AK-47 and (up close) the M4A1-S one-tap a helmet, the Galil doesn't.
    for (int id : {int(kWRifle), int(kWM4A1S)}) {
        const bool kill = headshot(weaponDef(id), 500, true, d);
        std::printf("  %s, helmet, 500 units: %.0f damage%s\n", weaponDef(id).name, double(d), kill ? " KILL" : "");
        CHECK(kill, "the %s must one-tap a helmet at 500 (%.0f)", weaponDef(id).name, double(d));
    }
    for (int id : {int(kWGalil)}) {
        const bool kill = headshot(weaponDef(id), 64, true, d);
        std::printf("  %s, helmet, 64 units: %.0f damage%s\n", weaponDef(id).name, double(d), kill ? " KILL" : "");
        CHECK(!kill && d > 85.0f, "%s must not one-tap a helmet (%.0f)", weaponDef(id).name, double(d));
    }
    // The snipers through kevlar: the AWP kills with a body shot anywhere, the SSG 08 needs the head (which
    // it gets through a helmet from anywhere).
    auto bodyShot = [&](const WeaponDef& def, float dist, float& dmg) {
        std::vector<Dummy> dd(1);
        dd[0].pos = dd[0].prevPos = {dist, 0, 0};
        dd[0].armor = 100.0f;
        dd[0].helmet = true;
        std::vector<Vec3> pos{dd[0].pos};
        WeaponState ws;
        ws.def = &def;
        ShotResult r = fireBullet(ws, {0, 0, 50}, 0, 0, 0, true, false, empty, dd, pos);
        dmg = r.dummyIndex == 0 && r.group == kChest ? r.damage : 0.0f;
        return r.kill;
    };
    for (float dist : {500.0f, 4000.0f}) {
        const bool awp = bodyShot(sniperDef(), dist, d);
        std::printf("  AWP, kevlar, chest, %4.0f units: %.0f damage%s\n", double(dist), double(d), awp ? " KILL" : "");
        CHECK(awp, "the AWP must kill through kevlar at %.0f (%.0f)", double(dist), double(d));
        const bool ssgBody = bodyShot(weaponDef(kWSsg08), dist, d);
        CHECK(!ssgBody && d > 60.0f, "an SSG 08 body shot must not kill through kevlar (%.0f)", double(d));
        const bool ssgHead = headshot(weaponDef(kWSsg08), dist, true, d);
        std::printf("  SSG 08, helmet, %4.0f units: %.0f damage%s\n", double(dist), double(d), ssgHead ? " KILL" : "");
        CHECK(ssgHead, "the SSG 08 must one-tap a helmet at %.0f (%.0f)", double(dist), double(d));
    }
    // The UMP-45 hits hard up close and falls off fast.
    float umpNear = 0, umpFar = 0;
    bodyShot(weaponDef(kWUmp45), 200, umpNear);
    bodyShot(weaponDef(kWUmp45), 2000, umpFar);
    std::printf("  UMP-45 through kevlar: %.0f at 200 units, %.0f at 2000\n", double(umpNear), double(umpFar));
    CHECK(umpNear > 20.0f && umpFar < umpNear * 0.65f, "ump %.0f / %.0f", double(umpNear), double(umpFar));
    // Model size (PLAYER MODEL SIZE): at 120% the head is 20% higher and the eye moves with it; a shot at the
    // bigger head's height hits the head, one at the old head height now hits the chest. Back to 100% after.
    {
        auto hitAt = [&](float z) {
            std::vector<Dummy> dd(1);
            dd[0].pos = dd[0].prevPos = {400, 0, 0};
            dd[0].hp = 1e9f;
            std::vector<Vec3> pos{dd[0].pos};
            WeaponState ws;
            ws.def = &rifleDef();
            return fireBullet(ws, {0, 0, z}, 0, 0, 0, true, false, empty, dd, pos).group;
        };
        setModelScale(1.2f);
        const HitGroup big = hitAt(78.0f), old = hitAt(63.0f);
        const float eye = dummyEyeZ(), eyeCrouched = dummyEyeZ(1.0f);
        setModelScale(1.0f);
        std::printf("  model size 120%%: z 78 hits %s, z 63 hits %s; eye %.1f (crouched %.1f)\n", hitGroupName(big),
                    hitGroupName(old), double(eye), double(eyeCrouched));
        CHECK(big == kHead && old != kHead && std::fabs(eye - 76.8f) < 0.01f && eyeCrouched < eye - 20.0f &&
                  hitAt(64.0f) == kHead && std::fabs(dummyEyeZ() - 64.0f) < 0.01f,
              "model scale");
    }

    // Collats: a bullet that goes through walls goes through bodies too, into the next one in line.
    auto lineShot = [&](const WeaponDef& def, float z, int& kills, int& hits) {
        std::vector<Dummy> dd(3);
        for (size_t k = 0; k < dd.size(); ++k) dd[k].pos = dd[k].prevPos = {400.0f + 60.0f * float(k), 0, 0};
        std::vector<Vec3> pos;
        for (const Dummy& x : dd) pos.push_back(x.pos);
        WeaponState ws;
        ws.def = &def;
        const ShotResult r = fireBullet(ws, {0, 0, z}, 0, 0, 0, true, false, empty, dd, pos);
        hits = (r.dummyIndex >= 0 ? 1 : 0) + r.collats;
        kills = int(r.kill);
        for (int k = 0; k < r.collats; ++k) kills += r.collat[k].kill;
        ShotResult out[4];
        CHECK(collatResults(r, out, 0, 4) == r.collats && (r.collats == 0 || (out[0].isCollat && out[0].dummyIndex == 1)),
              "collat results");
    };
    int awpKills = 0, awpHits = 0, akKills = 0, akHits = 0, pistolKills = 0, pistolHits = 0;
    lineShot(sniperDef(), 50, awpKills, awpHits);   // chest high
    lineShot(rifleDef(), 64, akKills, akHits);      // head high
    lineShot(pistolDef(), 64, pistolKills, pistolHits);
    std::printf("  collats, three in a line: AWP chest %d hit %d killed, AK-47 head %d hit %d killed, pistol %d hit\n",
                awpHits, awpKills, akHits, akKills, pistolHits);
    CHECK(awpKills >= 2 && akKills >= 2 && pistolHits == 1, "collats awp %d ak %d pistol %d", awpKills, akKills, pistolHits);

    // Only the snipers scope; a noscope is only inaccurate with the moving-spread option on.
    WeaponState awp;
    awp.def = &sniperDef();
    const float exact = currentInaccuracy(awp, 0, true, false);
    awp.moveSpread = true;
    const float noscope = currentInaccuracy(awp, 0, true, false);
    awp.scoped = true;
    const float scoped = currentInaccuracy(awp, 0, true, false);
    int scopes = 0;
    for (int id = 0; id < kWeaponCount; ++id) scopes += weaponDef(id).scope ? 1 : 0;
    CHECK(exact == 0.0f && noscope > 3.0f && scoped == 0.0f && scopes == 2 && weaponDef(kWSsg08).scope,
          "scope %.1f %.1f %.1f (%d scoped guns)", double(exact), double(noscope), double(scoped), scopes);
}

// Career: beating bots raises your rating (more for better bots), losing lowers it, and it all saves and loads.
void testCareer() {
    std::printf("career\n");
    Career c;
    const int winHard = ratingChange(1000, 2, 1.0f), winEasy = ratingChange(1000, 0, 1.0f), loseHard = ratingChange(1000, 2, 0.0f);
    const int loseEasy = ratingChange(1000, 0, 0.0f);
    std::printf("  at 1000: beat HARD %+d, beat EASY %+d, lose to HARD %+d, lose to EASY %+d\n", winHard, winEasy, loseHard, loseEasy);
    MatchRecord m;
    m.mode = 3;
    m.score = 1;
    m.kills = 20;
    m.deaths = 10;
    m.hsKills = 9;
    m.damage = 2100;
    m.rounds = 22;
    m.botLevel = 2;
    m.when = "2026-10-09 12:00";
    m.result = "WON 13-9";
    c.add(m);
    m.mode = 1;
    m.score = 0.25f;
    m.rounds = 0;
    m.result = "7TH OF 9";
    c.add(m);
    const std::string path = "sim_tests_career.txt";
    Career back;
    const bool io = c.save(path) && back.load(path);
    std::remove(path.c_str());
    std::printf("  two matches: rating %d (best %d), %s, then %s; ranks %s / %s / %s\n", c.rating, c.best,
                c.matches[0].result.c_str(), c.matches[1].result.c_str(), rankName(800), rankName(1000), rankName(1800));
    CHECK(winHard > winEasy && winEasy > 0 && loseHard < 0 && loseEasy < loseHard && c.matches[0].ratingAfter > 1000 &&
              io && back.rating == c.rating && back.matches.size() == 2 && back.matches[0].result == "WON 13-9" &&
              back.matches[1].when == m.when && std::string(rankName(1800)) == "GENERAL" && c.wins() == 1,
          "career");
}

// Replays: frames in, the moment between two frames comes out interpolated, old frames and shots fall out of the
// ring once it's full, and the shots in a span come back.
void testReplay() {
    std::printf("replay\n");
    Replay r;
    const int total = int(Replay::kRate * Replay::kSeconds) + 640;  // 10 s more than it keeps
    for (int k = 0; k < total; ++k) {
        const double t = k / Replay::kRate;
        ReplayFrame& f = r.next(t);
        ReplayAgent a;
        a.pos = {float(k) * 2.0f, 0, 0};
        a.yaw = 170.0f + float(k % 2) * 20.0f;  // flips across 180: must turn the short way
        f.agents.push_back(a);
        if (k % 64 == 0) r.shot({t, a.pos, a.pos + Vec3{100, 0, 0}, 0, 0});
    }
    ReplayFrame out;
    const double mid = (total - 2 + 0.5) / Replay::kRate;
    const bool ok = r.sample(mid, out);
    std::vector<ReplayShot> shots;
    r.shotsBetween(r.end() - 10.0, r.end(), shots);
    std::printf("  %zu frames kept (%.0f s), from %.1f s; between frames: x %.1f, yaw %.1f; %zu shots in the last 10 s\n",
                r.frames(), r.end() - r.start(), r.start(), double(out.agents.empty() ? 0 : out.agents[0].pos.x),
                double(out.agents.empty() ? 0 : out.agents[0].yaw), shots.size());
    CHECK(ok && r.frames() == size_t(Replay::kRate * Replay::kSeconds) && std::fabs(r.start() - 10.0) < 0.02 &&
              std::fabs(out.agents[0].pos.x - float(total - 2) * 2.0f - 1.0f) < 0.01f &&
              (std::fabs(out.agents[0].yaw - 180.0f) < 0.01f || std::fabs(out.agents[0].yaw + 180.0f) < 0.01f) &&
              shots.size() == 10,
          "replay");
}

// Cases: 100,000 openings land on the CS odds, every rarity has skins, a case comes every N kills, and the
// inventory file round-trips.
void testCases() {
    std::printf("cases\n");
    const std::vector<SkinDef>& skins = allSkins();
    int perRarity[kRarities] = {}, knives = 0;
    for (const SkinDef& s : skins) {
        perRarity[s.rarity]++;
        knives += s.weapon == kWKnife;
    }
    uint32_t rng = 12345;
    const int n = 100000;
    int got[kRarities] = {};
    for (int k = 0; k < n; ++k) got[skins[size_t(rollCase(rng).skin)].rarity]++;
    std::printf("  %zu skins (%d knife skins); 100k cases:", skins.size(), knives);
    bool ok = true;
    for (int r = 0; r < kRarities; ++r) {
        const double want = double(rarityOdds(r)) / 10000.0, have = double(got[r]) / double(n);
        std::printf(" %s %.2f%%", rarityName(r), have * 100.0);
        ok = ok && perRarity[r] > 0 && std::fabs(have - want) < want * 0.15 + 0.0005;
    }
    std::printf("\n");
    CHECK(ok, "case odds");
    Inventory inv;
    int earned = 0;
    for (int k = 0; k < 75; ++k) earned += inv.addKill(25);
    CHECK(earned == 3 && inv.cases == 3 && inv.progress == 0, "a case every 25 kills (%d)", earned);
    const int got1 = inv.open(rng);
    inv.equip[kWDeagle] = {findSkin("DEAGLE_BLAZE"), 0.031f};
    const std::string path = "sim_tests_inventory.txt";
    CHECK(inv.save(path), "save");
    Inventory back;
    CHECK(back.load(path) && back.cases == 2 && back.items.size() == 1 && back.items[0].skin == inv.items[size_t(got1)].skin &&
              back.equip[kWDeagle].skin == findSkin("DEAGLE_BLAZE") && std::fabs(back.equip[kWDeagle].wear - 0.031f) < 1e-5f,
          "inventory round trip");
    std::remove(path.c_str());
    CHECK(std::string(wearName(0.01f)) == "FACTORY NEW" && std::string(wearName(0.5f)) == "BATTLE-SCARRED", "wear names");
}

// Online players crouch: the head drops with the eye (64 -> 46), so a shot at standing head height goes
// over a crouched player, one at crouched head height hits the head, and the bots' check agrees.
void testCrouchedHitboxes() {
    std::printf("crouched hitboxes\n");
    World empty;
    auto shotAt = [&](float z, float crouch) {
        std::vector<Dummy> dd(1);
        dd[0].shownCrouch = dd[0].crouch = crouch;
        dd[0].hp = 1e9f;
        std::vector<Vec3> pos{Vec3{}};
        WeaponState ws;
        ws.def = &pistolDef();
        ShotResult r = fireBullet(ws, {-300, 0, z}, 0, 0, 0, true, false, empty, dd, pos);
        return r.dummyIndex == 0 ? int(r.group) : -1;
    };
    const int standHead = shotAt(kStandEye, 0), crouchOver = shotAt(kStandEye, 1), crouchHead = shotAt(kDuckEye, 1);
    std::printf("  shot at %.0f: standing %s, crouched %s; at %.0f crouched: %s\n", double(kStandEye),
                standHead == kHead ? "head" : "-", crouchOver < 0 ? "over" : "hit", double(kDuckEye),
                crouchHead == kHead ? "head" : "-");
    CHECK(standHead == kHead && crouchOver < 0 && crouchHead == kHead, "stand %d over %d crouch %d", standHead, crouchOver,
          crouchHead);
    float t = 0;
    HitGroup grp = kChest;
    const bool botHit = rayHitsDummy({}, 180, 1, {-300, 0, kDuckEye}, {1, 0, 0}, 1000, t, grp);
    CHECK(botHit && grp == kHead, "bot ray vs crouched");
    CHECK(std::fabs(crouchZ(34, 1) - 16) < 0.01f && std::fabs(crouchZ(69, 1) - 51) < 0.01f && crouchZ(10, 0) == 10,
          "crouchZ");
}

// Bots sent to every competitive spot (each retake hold and bomb spot, every CT role spot, the T staging
// points) from both spawns get there, even where the spot itself sits in a blocked cell at this map size
// (they go to the nearest cell they can reach instead of giving up and freezing where they are).
void testBotGoals() {
    std::printf("bot goals\n");
    const NavGrid& nav = dustNav();
    BotSenses sense;
    sense.world = &dust();
    sense.nav = &nav;
    std::vector<Vec3> spots;
    for (const RetakeSite& s : townRetakeSites()) {
        spots.push_back(dpt(s.bombX, s.bombY));
        for (const RetakeSpot& h : s.holds) spots.push_back(dpt(h.x, h.y));
    }
    for (int r = 0; r < kCtRoles; ++r)
        for (const RetakeSpot& h : townCtSpots(r)) spots.push_back(dpt(h.x, h.y));
    for (const Vec3& p : {dpt(1500, 1100), dpt(170, 1350), dpt(-2070, 1150)}) spots.push_back(p);
    int reached = 0, blocked = 0, failed = 0;
    for (int side = 0; side < 2; ++side)
        for (const Vec3& spot : spots) {
            std::vector<Vec3> probe;
            blocked += !nav.findPath(townTeamSpawns(side)[0], spot, probe);
            uint32_t rng = 99u;
            Dummy d;
            BotBrain b;
            spawnDeathmatchBot(d, b, townTeamSpawns(side)[0], rng);
            b.holdOnly = true;
            b.goal = spot;
            b.hasGoal = true;
            for (int t = 0; t < kTickRate * 60 && b.hasGoal; ++t) {
                sense.now = double(t) * kTickDt;
                d.prevPos = d.pos;
                updateDeathmatchBot(d, b, sense, rng);
            }
            if (length2d(d.pos - spot) < 130.0f) ++reached;
            else {
                ++failed;
                std::printf("  from %s: stopped %.0f units short of (%.0f, %.0f)\n", side ? "CT" : "T",
                            double(length2d(d.pos - spot)), double(spot.x), double(spot.y));
            }
        }
    std::printf("  %d of %d trips reached their spot (%d spots in a blocked cell: reached the nearest open one)\n", reached,
                reached + failed, blocked);
    CHECK(failed == 0, "%d bots stopped short", failed);
}

// No mid fights from spawn (owner, many times): on both maps no T staging point and no CT spot or early push is in
// mid, no route there from the spawns crosses mid, and the CT who holds mid can't be seen from mid (it watches the
// doors from back in CT mid). Checked at three Dust sizes.
bool midArea(const char* name) {
    static const char* const kMid[] = {"MID", "TOP MID", "MID DOORS", "CATWALK", "MARKET"};
    for (const char* m : kMid)
        if (std::strcmp(name, m) == 0) return true;
    return false;
}
void testNoMidFights() {
    std::printf("no mid fights from spawn\n");
    const float keep = townScale();
    int bad = 0;
    for (int town = 0; town < 2; ++town)
        for (float sc : {0.6f, 0.8f, 0.95f}) {
            if (town == 1 && sc != 0.6f) continue;  // (Harbor has one size)
            setTownMap(town);
            setDustScale(sc);
            const World w = buildTown();
            NavGrid nav;
            nav.build(townGrid(), w, townSpawn().pos);
            nav.setAvoid(townMidCells());  // (as competitive does)
            const TownTactics& tac = townTactics();
            std::vector<Vec3> path;
            auto midCells = [&](const Vec3& from, const Vec3& to) {
                Vec3 goal = to;
                if (!nav.findPath(from, goal, path)) {
                    Vec3 alt;
                    if (!nav.nearestRoamable(goal, 6, alt) || !nav.findPath(from, alt, path)) return -1;
                }
                int n = 0;
                for (const Vec3& c : path) n += midArea(townCallout(c));
                return n;
            };
            auto report = [&](const char* what, const Vec3& from, const Vec3& to) {
                const int n = midCells(from, to);
                if (n != 0) {
                    ++bad;
                    std::printf("    %s %.0f%%: %s to %s (%s) %s\n", townMapName(town), double(sc * 100), what, townCallout(from), townCallout(to),
                                n < 0 ? "no route" : (std::to_string(n) + " cells in mid").c_str());
                }
            };
            for (int k = 0; k < 5; ++k)
                for (const Vec3& sp : townTeamSpawns(0)) report("T stage", sp, townPoint(tac.stages[k][0], tac.stages[k][1]));
            for (int role = 0; role < kCtRoles; ++role)
                for (const RetakeSpot& h : townCtSpots(role))
                    for (const Vec3& sp : townTeamSpawns(1)) report("CT spot", sp, townPoint(h.x, h.y));
            for (int k : {0, 2})
                for (const Vec3& sp : townTeamSpawns(1)) report("CT push", sp, townPoint(tac.pushes[k].x, tac.pushes[k].y));
            // The mid holder: out of sight of everyone in mid.
            const MapGrid& m = townGrid();
            for (const RetakeSpot& h : townCtSpots(kCtMid)) {
                const Vec3 eye = townPoint(h.x, h.y) + Vec3{0, 0, 64};
                int seen = 0;
                for (int j = 0; j < m.h; ++j)
                    for (int i = 0; i < m.w; ++i) {
                        const Vec3 c = m.center(i, j);
                        if (!m.walkable(i, j) || !midArea(townCallout(c)) || std::strcmp(townCallout(c), "CATWALK") == 0) continue;
                        if (w.traceRay(c + Vec3{0, 0, 64}, eye).fraction >= 1.0f) ++seen;
                    }
                if (seen) {
                    ++bad;
                    std::printf("    %s %.0f%%: the CT mid spot (%.0f, %.0f) is seen from %d mid cells\n", townMapName(town), double(sc * 100),
                                double(h.x), double(h.y), seen);
                }
            }
        }
    setTownMap(0);
    setDustScale(keep);
    std::printf("  stages, CT spots, pushes and their routes checked on both maps: %d go into mid\n", bad);
    CHECK(bad == 0, "%d plans or routes go into mid", bad);
}

// Fire: a bot caught in a molotov steps out within a second (holding an angle or walking through), and one walking
// to a goal past the fire waits at its edge instead of burning.
void testBotFire() {
    std::printf("bots and fire\n");
    const NavGrid& nav = dustNav();
    BotSenses sense;
    sense.world = &dust();
    sense.nav = &nav;
    std::vector<Vec3> fires;
    sense.fires = &fires;
    const float radius = 110.0f;
    sense.fireRadius = radius;
    int slow = 0, burnt = 0, tried = 0;
    float worst = 0;
    for (const RetakeSite& site : townRetakeSites())
        for (const RetakeSpot& h : site.holds) {
            const Vec3 at = dpt(h.x, h.y);
            uint32_t rng = 7u;
            Dummy d;
            BotBrain b;
            spawnDeathmatchBot(d, b, at, rng);
            b.state = 1;
            b.holdOnly = true;
            fires.assign(1, at + Vec3{20, 10, 0});  // the molotov lands at its feet
            int t = 0;
            for (; t < kTickRate * 2 && length2d(d.pos - fires[0]) < radius; ++t) {
                sense.now = double(t) * kTickDt;
                d.prevPos = d.pos;
                updateDeathmatchBot(d, b, sense, rng);
            }
            ++tried;
            worst = std::max(worst, float(t) * kTickDt);
            if (length2d(d.pos - fires[0]) < radius) {
                ++slow;
                std::printf("    stuck in the fire at hold (%.0f, %.0f): now (%.0f, %.0f, %.0f), %.0f from its centre\n", double(h.x),
                            double(h.y), double(d.pos.x), double(d.pos.y), double(d.pos.z), double(length2d(d.pos - fires[0])));
            }
            // Then it stays out while the fire burns (6 s), however it wants to go back.
            for (int k = 0; k < kTickRate * 6; ++k) {
                sense.now = double(t + k) * kTickDt;
                d.prevPos = d.pos;
                updateDeathmatchBot(d, b, sense, rng);
                if (length2d(d.pos - fires[0]) < radius - 4.0f) { ++burnt; break; }
            }
        }
    std::printf("  %d bots set on fire at their spot: out in %.2f s at worst; %d stayed in, %d walked back in\n", tried,
                double(worst), slow, burnt);
    CHECK(slow == 0 && worst < 1.0f, "%d bots didn't get out of the fire within 2 s", slow);
    CHECK(burnt == 0, "%d bots walked back into the fire", burnt);
}

// No holes: from standing eye height anywhere you can walk, every ray at or below the horizon hits the map
// (a floor, a wall, a roof) - none slips out between boxes.
void testMapGaps() {
    std::printf("map has no holes\n");
    const NavGrid& nav = dustNav();
    int escapes = 0, total = 0;
    const float pitches[5] = {0, 2, 5, 15, 45};  // degrees down
    for (int s = 0; s < 800; ++s) {
        const Vec3 eye = nav.roamPoint((float(s) + 0.5f) / 800.0f, false) + Vec3{0, 0, kStandEye};
        for (int yi = 0; yi < 36; ++yi)
            for (float pitch : pitches) {
                ++total;
                escapes += dust().traceRay(eye, eye + anglesToForward(pitch, float(yi) * 10.0f) * 12000.0f).fraction >= 1.0f;
            }
    }
    std::printf("  %d rays from %d standing spots, %d got out of the map\n", total, 800, escapes);
    CHECK(escapes == 0, "%d rays escaped", escapes);
}

// Walk the main routes with a simulated player at knife speed (250 u/s) and print the run times.
void testDustRoutes() {
    std::printf("dust routes (knife, 250 u/s)\n");
    const Landmark tSpawn{"T spawn", townSpawn().pos}, ctSpawn{"CT spawn", dpt(-150, 2750)},
        longDoors{"long doors", dpt(775, 380)}, aSite{"A site", dpt(1300, 2900)}, bSite{"B site", dpt(-1850, 2400)},
        midDoors{"mid doors", dpt(-180, 1950)}, cat{"catwalk", dpt(170, 1700)}, pit{"pit", dpt(1700, 350)},
        lower{"lower tunnels", dpt(-1100, 1150)};
    struct Route { Landmark a, b; float minS, maxS; };
    const Route routes[] = {
        {tSpawn, longDoors, 4, 10},  {tSpawn, aSite, 10, 22},  {tSpawn, bSite, 11, 22},
        {tSpawn, midDoors, 7, 15},   {tSpawn, cat, 5, 14},     {tSpawn, lower, 5, 14},
        {ctSpawn, aSite, 2.5f, 8},   {ctSpawn, bSite, 6, 14},  {ctSpawn, midDoors, 2, 8},
        {pit, aSite, 6, 15},         {bSite, tSpawn, 11, 22},  {lower, bSite, 4, 12},
    };
    for (const Route& rt : routes) {
        std::vector<Vec3> path;
        bool found = dustRoute(rt.a.pos, rt.b.pos, path);
        float dist = 0, secs = found ? runRoute(path, 250.0f, &dist) : -1.0f;
        std::printf("  %-9s -> %-13s %5.1f s  (%4.0f units)\n", rt.a.name, rt.b.name, double(secs), double(dist));
        CHECK(found, "no route %s -> %s", rt.a.name, rt.b.name);
        const float sc = townScale();  // the windows are for real-size Dust2; the map can be smaller
        CHECK(secs >= rt.minS * sc && secs <= rt.maxS * sc, "%s -> %s took %.1f s (stuck = -1)", rt.a.name, rt.b.name,
              double(secs));
    }
}

// Harbor, Crisp's own map: every bot spot is standing room the bots can walk to from both spawns, the spawns
// can't see each other, it has no holes, and the lanes take sensible times.
void testHarbor() {
    std::printf("harbor\n");
    setTownMap(1);
    const World w = buildTown();
    NavGrid nav;
    nav.build(townGrid(), w, townSpawn().pos);
    std::printf("  %zu boxes, %s\n", w.solids.size(), townMapName(1));
    CHECK(std::string(townCallout(townSpawn().pos)) == "T SPAWN", "callout %s", townCallout(townSpawn().pos));
    auto P = [](const RetakeSpot& h) { return townPoint(h.x, h.y); };
    struct Spot { const char* what; Vec3 p; };
    std::vector<Spot> spots;
    for (int side = 0; side < 2; ++side)
        for (const Vec3& p : townTeamSpawns(side)) spots.push_back({side ? "CT spawn" : "T spawn", p});
    for (const RetakeSite& site : townRetakeSites()) {
        spots.push_back({"bomb", townPoint(site.bombX, site.bombY)});
        for (const RetakeSpot& h : site.holds) spots.push_back({"retake hold", P(h)});
        for (const RetakeSpot& h : site.entries) spots.push_back({"retake entry", P(h)});
    }
    for (int r = 0; r < kCtRoles; ++r)
        for (const RetakeSpot& h : townCtSpots(r)) spots.push_back({"CT spot", P(h)});
    for (const PrefireRoute& rt : townPrefireRoutes()) {
        spots.push_back({rt.name, P(rt.start)});
        for (const RetakeSpot& h : rt.bots) spots.push_back({rt.name, P(h)});
    }
    for (const PeekSpot& ps : townPeekSpots()) {
        spots.push_back({"peek cover", ps.cover});
        spots.push_back({"peek", ps.peek});
    }
    const TownTactics& tac = townTactics();
    for (const auto& st : tac.stages) spots.push_back({"stage", townPoint(st[0], st[1])});
    for (const RetakeSpot& h : tac.pushes) spots.push_back({"push", P(h)});
    int bad = 0, unreachable = 0;
    std::vector<Vec3> path;
    for (const Spot& sp : spots) {
        // (+6: on a ramp the grid's floor height is a few units under the slope's surface; spawning drops you onto it)
        const bool clear = sp.p.z > MapGrid::kNoFloor && w.boxFits(sp.p + Vec3{0, 0, 6.0f}, hullMins(), hullMaxs(false)) &&
                           w.traceBox(sp.p + Vec3{0, 0, 6.0f}, sp.p - Vec3{0, 0, 12}, hullMins(), hullMaxs(false)).fraction < 1.0f;
        const bool route = nav.findPath(townTeamSpawns(0)[0], sp.p, path);
        if (!clear) std::printf("    %s (%.0f, %.0f) is not standing room\n", sp.what, double(sp.p.x), double(sp.p.y));
        if (!route) std::printf("    %s (%.0f, %.0f) has no route from T spawn\n", sp.what, double(sp.p.x), double(sp.p.y));
        bad += !clear;
        unreachable += !route;
    }
    std::printf("  %zu bot spots: %d not standing room, %d unreachable\n", spots.size(), bad, unreachable);
    CHECK(bad == 0 && unreachable == 0, "harbor spots: %d bad, %d unreachable", bad, unreachable);

    // Real bots walk to the competitive spots from both spawns.
    BotSenses sense;
    sense.world = &w;
    sense.nav = &nav;
    int reached = 0, failed = 0;
    for (int side = 0; side < 2; ++side)
        for (const Spot& sp : spots) {
            if (std::string(sp.what) != "retake hold" && std::string(sp.what) != "CT spot" && std::string(sp.what) != "stage") continue;
            uint32_t rng = 7u;
            Dummy d;
            BotBrain b;
            spawnDeathmatchBot(d, b, townTeamSpawns(side)[0], rng);
            b.holdOnly = true;
            b.goal = sp.p;
            b.hasGoal = true;
            for (int t = 0; t < kTickRate * 60 && b.hasGoal; ++t) {
                sense.now = double(t) * kTickDt;
                d.prevPos = d.pos;
                updateDeathmatchBot(d, b, sense, rng);
            }
            if (length2d(d.pos - sp.p) < 130.0f) ++reached;
            else {
                ++failed;
                std::printf("    from %s: stopped %.0f short of %s (%.0f, %.0f)\n", side ? "CT" : "T", double(length2d(d.pos - sp.p)),
                            sp.what, double(sp.p.x), double(sp.p.y));
            }
        }
    std::printf("  %d of %d bot trips reached their spot\n", reached, reached + failed);
    CHECK(failed == 0, "%d harbor trips failed", failed);

    // Nobody sees the other team's spawn when the round starts.
    int seen = 0;
    for (const Vec3& a : townTeamSpawns(0))
        for (const Vec3& b : townTeamSpawns(1))
            seen += w.traceRay(a + Vec3{0, 0, kStandEye}, b + Vec3{0, 0, kStandEye}).fraction >= 1.0f;
    CHECK(seen == 0, "harbor: %d spawn pairs see each other", seen);

    // No holes.
    int escapes = 0, total = 0;
    for (int k = 0; k < 400; ++k) {
        const Vec3 eye = nav.roamPoint((float(k) + 0.5f) / 400.0f, false) + Vec3{0, 0, kStandEye};
        for (int yi = 0; yi < 36; ++yi)
            for (float pitch : {0.0f, 2.0f, 5.0f, 15.0f, 45.0f}) {
                ++total;
                escapes += w.traceRay(eye, eye + anglesToForward(pitch, float(yi) * 10.0f) * 12000.0f).fraction >= 1.0f;
            }
    }
    std::printf("  %d rays, %d got out of the map; spawns that see each other: %d\n", total, escapes, seen);
    CHECK(escapes == 0, "harbor: %d rays escaped", escapes);

    // The lanes at knife speed.
    struct Leg { const char* a; Vec3 from; const char* b; Vec3 to; float minS, maxS; };
    const Vec3 tSp = townSpawn().pos, ctSp = townPoint(0, 1500), aSite = townPoint(1000, 1150), bSite = townPoint(-1000, 880),
               doors = townPoint(0, 440), docks = townPoint(1050, 450), under = townPoint(-1050, 250);
    const Leg legs[] = {{"T spawn", tSp, "A site", aSite, 8, 22},   {"T spawn", tSp, "B site", bSite, 8, 22},
                        {"T spawn", tSp, "mid doors", doors, 5, 15}, {"T spawn", tSp, "top of docks", docks, 5, 16},
                        {"T spawn", tSp, "underpass", under, 5, 16}, {"CT spawn", ctSp, "A site", aSite, 2, 9},
                        {"CT spawn", ctSp, "B site", bSite, 2, 9},   {"CT spawn", ctSp, "mid doors", townPoint(0, 640), 2, 8}};
    for (const Leg& l : legs) {
        const bool found = nav.findPath(l.from, l.to, path);
        float dist = 0, secs = found ? runRoute(path, 250.0f, &dist, &w) : -1.0f;
        std::printf("  %-8s -> %-12s %5.1f s  (%4.0f units)\n", l.a, l.b, double(secs), double(dist));
        CHECK(found && secs >= l.minS && secs <= l.maxS, "harbor %s -> %s: %.1f s", l.a, l.b, double(secs));
    }
    setTownMap(0);
}

// Bots use the whole arsenal: the team's buy call, each role's buy (AWPer, rifles by side, force guns), kept guns,
// deathmatch picks, fire cadence by gun and damage falloff.
void testBotArsenal() {
    std::printf("bot arsenal (buys, cadence, falloff)\n");
    CHECK(teamBuyRound(800, true, false) == kBuyPistol, "pistol round");
    CHECK(teamBuyRound(1500, false, false) == kBuyEco, "a poor team saves");
    CHECK(teamBuyRound(1500, false, true) == kBuyForce, "no next round: force");
    CHECK(teamBuyRound(3000, false, false) == kBuyForce, "a half-rich team forces");
    CHECK(teamBuyRound(4500, false, false) == kBuyFull, "a rich team buys");
    auto show = [](const char* what, const BotBuy& b) {
        std::printf("  %-28s %-13s %s%s  $%d\n", what, weaponDef(b.gun).name, b.armor ? "kevlar" : "", b.helmet ? "+helmet" : "",
                    b.spent);
    };
    BotBuy b = botBuy(kBuyFull, 6000, 0, 0, kWPistol, false, false);
    show("T AWPer, $6000", b);
    CHECK(b.gun == kWSniper && b.armor && b.helmet && b.spent == 5750, "the AWPer buys the AWP and full armor");
    b = botBuy(kBuyFull, 4000, 0, 1, kWPistol, false, false);
    show("T rifler, $4000", b);
    CHECK(b.gun == kWRifle && b.helmet && b.spent == 3700, "a T buys the AK and full armor");
    b = botBuy(kBuyFull, 4000, 1, 2, kWPistol, false, false);
    show("CT rifler, $4000", b);
    CHECK(b.gun == kWM4A1S && b.helmet && b.spent == 3900, "a CT buys the M4A1-S and full armor");
    b = botBuy(kBuyFull, 4000, 1, 0, kWPistol, false, false);
    show("CT AWPer, $4000 (short)", b);
    CHECK(b.gun == kWM4A1S, "an AWPer short of money rifles up");
    b = botBuy(kBuyForce, 2600, 0, 1, kWPistol, false, false);
    show("T force, $2600", b);
    CHECK(b.gun == kWGalil && b.armor && b.spent == 2450, "a T force buys a Galil and kevlar");
    b = botBuy(kBuyForce, 2000, 1, 4, kWPistol, false, false);
    show("CT force role 4, $2000", b);
    CHECK(b.gun == kWUmp45 && b.armor, "a CT who can't afford the XM takes the UMP");
    b = botBuy(kBuyForce, 2700, 1, 4, kWPistol, false, false);
    CHECK(b.gun == kWXm1014, "the CT shotgunner buys the XM1014 when it can");
    b = botBuy(kBuyForce, 2400, 0, 0, kWPistol, false, false);
    show("T force AWPer, $2400", b);
    CHECK(b.gun == kWSsg08, "the AWPer forces with the scout");
    b = botBuy(kBuyEco, 1300, 0, 2, kWPistol, false, false);
    CHECK(b.spent == 0 && b.gun == kWPistol, "eco: save");
    b = botBuy(kBuyEco, 1500, 1, 1, kWPistol, false, false);
    CHECK(b.gun == kWDeagle, "eco: one Deagle");
    b = botBuy(kBuyEco, 6000, 0, 2, kWPistol, false, false);
    CHECK(b.gun == kWRifle, "a rich bot buys even on an eco");
    b = botBuy(kBuyPistol, 800, 0, 1, kWPistol, false, false);
    CHECK(b.gun == kWDeagle && b.spent == 700, "pistol round: a Deagle");
    b = botBuy(kBuyPistol, 800, 0, 2, kWPistol, false, false);
    CHECK(b.gun == kWPistol && b.armor && b.spent == 650, "pistol round: kevlar");
    b = botBuy(kBuyEco, 1200, 1, 3, kWSniper, true, false);
    CHECK(b.gun == kWSniper && b.helmet && b.spent == 350, "a kept AWP stays, the helmet gets bought");
    b = botBuy(kBuyFull, 5000, 1, 2, kWUmp45, true, true);
    CHECK(b.gun == kWM4A1S && b.spent == 2900, "a kept UMP gets swapped for the M4A1-S on a full buy");
    b = botBuy(kBuyFull, 7000, 1, 0, kWM4A1S, true, true);
    CHECK(b.gun == kWSniper, "the AWPer swaps a kept rifle for the AWP when rich");
    b = botBuy(kBuyFull, 2000, 0, 2, kWGalil, true, true);
    CHECK(b.gun == kWGalil && b.spent == 0, "a kept Galil stays when it can't afford better");
    // Never more than it has, always a gun that fires.
    int bad = 0;
    for (int r = 0; r < 4; ++r)
        for (int money = 0; money <= 9000; money += 150)
            for (int side = 0; side < 2; ++side)
                for (int role = 0; role < 5; ++role) {
                    const BotBuy x = botBuy(BuyRound(r), money, side, role, kWPistol, false, false);
                    if (x.spent > money || x.spent < 0 || !weaponDef(x.gun).canFire) ++bad;
                }
    CHECK(bad == 0, "%d buys overspent or picked a gun that can't fire", bad);
    // Deathmatch: rifles mostly, every pick a real gun, the AWP among them.
    int counts[kWeaponCount] = {};
    for (int k = 0; k < 1000; ++k) counts[deathmatchBotGun((float(k) + 0.5f) / 1000.0f)]++;
    std::printf("  deathmatch picks /1000: AK %d, M4A1-S %d, AWP %d, Deagle %d, MAC-10 %d, XM1014 %d\n", counts[kWRifle],
                counts[kWM4A1S], counts[kWSniper], counts[kWDeagle], counts[kWMac10], counts[kWXm1014]);
    CHECK(counts[kWRifle] + counts[kWM4A1S] > 450 && counts[kWSniper] > 40, "mostly rifles, some AWPs");
    CHECK(counts[kWKnife] == 0 && counts[kWGrenade] == 0 && counts[kWPistol] == 0, "only real guns");
    // Cadence: never faster than the gun, bolt guns at their own pace, SMGs faster than rifles.
    for (int w = 0; w < kWeaponCount; ++w)
        if (weaponDef(w).canFire) CHECK(botShotGap(w) >= weaponDef(w).fireInterval, "%s fires faster than it can", weaponDef(w).name);
    CHECK(botShotGap(kWSniper) >= 1.46f && botShotGap(kWMac10) < botShotGap(kWRifle), "bolt guns slow, SMGs fast");
    std::printf("  shot gaps: AK %.2f, MAC-10 %.2f, AWP %.2f, Nova %.2f, Deagle %.2f s\n", double(botShotGap(kWRifle)),
                double(botShotGap(kWMac10)), double(botShotGap(kWSniper)), double(botShotGap(kWNova)), double(botShotGap(kWDeagle)));
    // Damage by range: the AK barely drops, the Nova's pellets halve by 1000 units.
    CHECK(std::fabs(damageAt(weaponDef(kWRifle), 0) - 36.0f) < 0.01f, "AK point blank 36");
    const float nova = damageAt(weaponDef(kWNova), 1000.0f);
    std::printf("  AK at 1000u %.1f, Nova pellet at 1000u %.1f\n", double(damageAt(weaponDef(kWRifle), 1000.0f)), double(nova));
    CHECK(nova < 13.5f && nova > 12.0f, "Nova pellet at 1000u %.1f", double(nova));
    CHECK(armoredDamage(damageAt(weaponDef(kWSniper), 2000.0f), kChest, 100, true, weaponDef(kWSniper).armorRatio) >= 100.0f,
          "a bot's AWP body shot kills through kevlar at 2000u");
}

// Dust's walls hold at every size: from mid and CT mid you can't see A site (short's wall), and nowhere in CT mid
// can you see onto catwalk. (Thin walls between areas used to vanish at some sizes when they were under a grid cell.)
void testDustSightlines() {
    std::printf("dust sightlines (mid can't see A site)\n");
    const float keep = townScale();
    int seen = 0, pairs = 0;
    for (float sc : {0.5f, 0.6f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f}) {
        setDustScale(sc);
        const World w = buildTown();
        const MapGrid& m = townGrid();
        std::vector<Vec3> from, to, cat;
        for (int j = 0; j < m.h; ++j)
            for (int i = 0; i < m.w; ++i) {
                const int a = m.area[size_t(m.index(i, j))];
                if (a < 0 || (i + j) % 2) continue;
                const std::string name = townCallout(m.center(i, j));
                const Vec3 c = m.center(i, j) + Vec3{0, 0, 64};
                if (name == "MID" || name == "CT MID") from.push_back(c);
                if (name == "A SITE") to.push_back(c);
                if (name == "CATWALK") cat.push_back(c);
            }
        int here = 0;
        for (size_t f = 0; f < from.size(); f += 3)
            for (size_t t = 0; t < to.size(); t += 3) {
                ++pairs;
                if (w.traceRay(from[f], to[t]).fraction >= 1.0f) {
                    if (here++ < 3)
                        std::printf("    at %.0f%%: %s (%.0f, %.0f) sees A site (%.0f, %.0f)\n", double(sc * 100),
                                    townCallout(from[f]), double(from[f].x / sc), double(from[f].y / sc), double(to[t].x / sc),
                                    double(to[t].y / sc));
                }
            }
        seen += here;
    }
    setDustScale(keep);
    std::printf("  %d mid -> A site sightlines checked over 9 sizes: %d clear\n", pairs, seen);
    CHECK(seen == 0, "%d sightlines from mid into A site", seen);
}

// Every Dust size the menu offers still has all its routes and valid bot spots.
void testDustScales() {
    std::printf("dust sizes\n");
    const float keep = townScale();
    for (float sc : {0.5f, 0.6f, 0.75f, 1.0f}) {
        setDustScale(sc);
        World w = buildTown();
        NavGrid nav;
        nav.build(townGrid(), w, townSpawn().pos);
        const float pts[][2] = {{-150, 2750}, {775, 380}, {1300, 2900}, {-1850, 2400}, {-180, 1950},
                                {170, 1700}, {1700, 420}, {-1100, 1150}};
        std::vector<Vec3> path;
        int missing = 0, badSpots = 0;
        for (const auto& pt : pts) missing += !nav.findPath(townSpawn().pos, dpt(pt[0], pt[1]), path);
        for (const PeekSpot& sp : townPeekSpots())
            for (Vec3 p : {sp.cover, sp.peek})
                if (!(p.z > MapGrid::kNoFloor && w.boxFits(p + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false)))) {
                    std::printf("    peek spot (%.0f, %.0f) blocked\n", double(p.x / sc), double(p.y / sc));
                    ++badSpots;
                }
        // Competitive: both teams' spawn spots are standing room and can reach both sites.
        for (int side = 0; side < 2; ++side)
            for (const Vec3& sp : townTeamSpawns(side)) {
                if (!nav.standable(sp)) { std::printf("    team %d spawn (%.0f, %.0f) blocked\n", side, double(sp.x), double(sp.y)); ++badSpots; }
                for (const RetakeSite& site : townRetakeSites())
                    missing += !nav.findPath(sp, townPoint(site.bombX, site.bombY), path);
            }
        // Nobody can see the other team's spawn spots when a round starts (head to head, both ways).
        for (const Vec3& t : townTeamSpawns(0))
            for (const Vec3& ct : townTeamSpawns(1))
                if (w.traceRay(t + Vec3{0, 0, 64}, ct + Vec3{0, 0, 64}).fraction >= 1.0f) {
                    std::printf("    T spawn (%.0f, %.0f) sees CT spawn (%.0f, %.0f)\n", double(t.x / sc), double(t.y / sc),
                                double(ct.x / sc), double(ct.y / sc));
                    ++badSpots;
                }
        // Competitive: every CT role spot is standing room the CTs can walk to from their spawn.
        for (int role = 0; role < kCtRoles; ++role)
            for (const RetakeSpot& h : townCtSpots(role)) {
                const Vec3 p = townPoint(h.x, h.y);
                if (!nav.standable(p)) {
                    std::printf("    CT spot (%.0f, %.0f) is not standing room\n", double(h.x), double(h.y));
                    ++badSpots;
                } else if (!nav.findPath(townTeamSpawns(1)[0], p, path)) {
                    std::printf("    CT spot (%.0f, %.0f) can't be reached from CT spawn\n", double(h.x), double(h.y));
                    ++missing;
                }
            }
        // Prefire: every route's start and bot spots are standing room, and you can walk from the start to each.
        for (const PrefireRoute& r : townPrefireRoutes()) {
            const Vec3 start = townPoint(r.start.x, r.start.y);
            if (!nav.standable(start)) { std::printf("    prefire %s start blocked\n", r.name); ++badSpots; }
            for (const RetakeSpot& b : r.bots) {
                const Vec3 p = townPoint(b.x, b.y);
                if (!nav.standable(p) || !w.boxFits(p + Vec3{0, 0, 8.5f}, hullMins(), hullMaxs(false))) {  // (ramps: up to 8 under)
                    std::printf("    prefire %s bot (%.0f, %.0f) is not standing room\n", r.name, double(b.x), double(b.y));
                    ++badSpots;
                } else if (w.traceRay(start + Vec3{0, 0, 64}, p + Vec3{0, 0, 64}).fraction >= 1.0f) {
                    std::printf("    prefire %s bot (%.0f, %.0f) can see the start\n", r.name, double(b.x), double(b.y));
                    ++badSpots;
                } else if (!nav.findPath(start, p, path) && !nav.findPath(start, townPoint(b.lookX, b.lookY), path)) {
                    // (raised spots like goose need a jump: then the place it watches must be reachable)
                    std::printf("    prefire %s bot (%.0f, %.0f) can't be reached\n", r.name, double(b.x), double(b.y));
                    ++missing;
                }
            }
        }
        // Retakes: every hold spot and entry is standing room, and every entry can walk onto its site.
        for (const RetakeSite& site : townRetakeSites()) {
            if (!nav.standable(townPoint(site.bombX, site.bombY))) {
                std::printf("    %s bomb spot is not standing room\n", site.name);
                ++badSpots;
            }
            for (const RetakeSpot& h : site.holds)
                if (!nav.standable(townPoint(h.x, h.y))) {
                    std::printf("    %s hold (%.0f, %.0f) is not standing room\n", site.name, double(h.x), double(h.y));
                    ++badSpots;
                }
            for (const RetakeSpot& e : site.entries) {
                if (!nav.standable(townPoint(e.x, e.y))) {
                    std::printf("    %s entry (%.0f, %.0f) is not standing room\n", site.name, double(e.x), double(e.y));
                    ++badSpots;
                } else if (!std::any_of(site.holds.begin(), site.holds.end(), [&](const RetakeSpot& h) {
                               return nav.findPath(townPoint(e.x, e.y), townPoint(h.x, h.y), path);
                           })) {
                    std::printf("    %s entry (%.0f, %.0f) can't walk to the site\n", site.name, double(e.x), double(e.y));
                    ++missing;
                }
            }
        }
        std::printf("  %3.0f%%: %zu boxes, %zu roamable cells, %d unreachable landmarks, %d bad bot spots\n",
                    double(sc * 100), w.solids.size(), nav.roamCount(), missing, badSpots);
        CHECK(missing == 0 && badSpots == 0, "scale %.2f", double(sc));
    }
    setDustScale(keep);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: a crash still shows how far we got
    testMaxSpeed();
    testJumpHeight();
    testCounterStrafe();
    testWallCollision();
    testStairs();
    testCrates();
    testNoAutoBhop();
    testBhop();
    testDeterminism();
    testWeapon();
    testFireTiming();
    testBotSkills();
    testWallbang();
    testRayVsBoxes();
    testMaterialWallbang();
    testRamps();
    testDustMap();
    testDustBroadphase();
    testDustRoutes();
    testDustDeathmatch();
    testTurnedHitboxes();
    testCrouchedHitboxes();
    testNewGuns();
    testCases();


    testCareer();
    testDustSightlines();
    testBotArsenal();
    testBotFire();
    testNoMidFights();
    testHarbor();
    testReplay();
    testBotCover();
    testBotsFightEachOther();
    testBotGoals();
    testMapGaps();


    testGrenades();
    testDustScales();
    if (g_failures) {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
