// Headless tests for the simulation core: movement feel numbers, collision, weapon determinism.
// Prints the measured values so tuning changes are visible in CI logs.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "combat.h"
#include "items.h"
#include "stats.h"
#include "replay.h"

#include "bots.h"
#include "config.h"
#include "console.h"
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

    // Hopping on the landing tick (a scroll-wheel bhop) keeps far more than landing and standing: easy to chain.
    auto afterLanding = [&](bool hop) {
        PlayerState p = spawnAt({-400, 0, 0});
        MoveInput m;
        m.forward = 1;
        run(p, m, 0, 250, kTickRate);
        m.jumpPressed = true;
        playerMove(p, m, 0, 250, lab());
        m.jumpPressed = false;
        while (!p.onGround) playerMove(p, m, 0, 250, lab());
        m.jumpPressed = hop;  // the first tick on the ground
        playerMove(p, m, 0, 250, lab());
        return length2d(p.velocity);
    };
    const float hopped = afterLanding(true), stayed = afterLanding(false);
    std::printf("  one landing: hop on the landing tick %.1f, stay on the ground %.1f\n", hopped, stayed);
    CHECK(hopped > 235.0f && hopped > stayed + 15.0f, "a perfect hop keeps most of its speed");
    // A scroll-wheel hop lands a few ticks late: inside the grace window it still keeps its speed; well after, the
    // ground has slowed you. A chain of grace-window hops with air strafing keeps going.
    auto hopAfter = [&](int ticks) {
        PlayerState p = spawnAt({-400, 0, 0});
        MoveInput m;
        m.forward = 1;
        run(p, m, 0, 250, kTickRate);
        m.jumpPressed = true;
        playerMove(p, m, 0, 250, lab());
        p.velocity.x = 320.0f;  // (going faster than you can run, as after air-strafing)
        m.jumpPressed = false;
        while (!p.onGround) playerMove(p, m, 0, 250, lab());
        for (int k = 0; k < ticks; ++k) playerMove(p, m, 0, 250, lab());
        m.jumpPressed = true;
        playerMove(p, m, 0, 250, lab());
        return length2d(p.velocity);
    };
    const float late4 = hopAfter(4), late20 = hopAfter(20);
    std::printf("  landing at 320: hop 4 ticks later %.1f, 20 ticks later %.1f\n", late4, late20);
    CHECK(late4 > 300.0f && late20 < 270.0f, "the hop grace window: %.1f %.1f", late4, late20);
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

    // Standing still (owner, v0.23.3): the AK, M4A1-S, Deagle and the scoped snipers are dead on, even mid-spray (the
    // recoil pattern is fixed); the cheaper guns scatter a little.
    for (int id = 0; id < kWeaponCount; ++id) {
        const WeaponDef& def = weaponDef(id);
        if (!def.canFire || def.pellets > 1) continue;
        WeaponState still;
        still.def = &def;
        still.scoped = def.scope;
        still.recoilIndex = 10;
        const float spread = currentInaccuracy(still, 0, true, false);
        const bool exact = id == kWRifle || id == kWM4A1S || id == kWDeagle || id == kWSniper || id == kWSsg08;
        std::printf("  %-14s standing spread %.2f deg\n", def.name, double(spread));
        CHECK(exact ? spread == 0.0f : spread > 0.1f && spread < 0.6f, "%s standing spread %.2f", def.name, double(spread));
    }

    // Moving: running is inaccurate, counter-strafed is perfect (always on: no option to turn it off).
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
    // Semi-auto: clicking faster than the gun cycles still fires at its full rate (a click waits for it,
    // TriggerBuffer), one shot per click, never faster than fireInterval.
    for (WeaponId gun : {kWPistol, kWDeagle, kWBerettas}) {
        WeaponState semi;
        semi.def = &weaponDef(gun);
        TriggerBuffer trig;
        int shots = 0, clicks = 0;
        double last = -1, closest = 1e9;
        const double clickEvery = double(semi.def->fireInterval) * 0.7;  // spamming faster than it can fire
        double nextClick = 0;
        for (int t = 0; t < int(2.0 / double(kTickDt)); ++t) {
            const double now = double(t) * double(kTickDt);
            if (now >= nextClick) {
                trig.click(now);
                ++clicks;
                nextClick += clickEvery;
            }
            if (trig.pending(now) && takeShotTiming(semi, now)) {
                trig.clear();
                if (last >= 0) closest = std::min(closest, now - last);
                last = now;
                ++shots;
            }
        }
        const int most = int(2.0 / double(semi.def->fireInterval)) + 1;
        std::printf("  %s spam-clicked: %d shots in 2 s (most %d, %d clicks), closest gap %.0f ms\n", semi.def->name, shots, most,
                    clicks, closest * 1000.0);
        CHECK(shots >= most - 1 && shots <= most && closest >= double(semi.def->fireInterval) - double(kTickDt),
              "%s: clicking fast should reach its fire rate", semi.def->name);
    }
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
Vec3 g_stuckAt;  // where the last runRoute got stuck
Vec3 g_stallAt;  // where the last runRoute stalled longest (for g_stallTicks ticks)
int g_stallTicks = 0;
float runRoute(const std::vector<Vec3>& path, float speed, float* distance, const World* world = nullptr) {
    PlayerState ps = spawnAt(path.front());
    size_t wp = 1;
    int ticks = 0, lastProgress = 0;
    float travelled = 0;
    g_stallTicks = 0;
    while (wp < path.size()) {
        Vec3 d = path[wp] - ps.origin;
        d.z = 0;
        if (length(d) < 40.0f) { ++wp; lastProgress = ticks; continue; }
        if (ticks - lastProgress > g_stallTicks) { g_stallTicks = ticks - lastProgress; g_stallAt = ps.origin; }
        // (round a tight corner: after half a second without a waypoint, aim at the next one, not two on)
        Vec3 aim = path[ticks - lastProgress > kTickRate / 2 ? wp : std::min(wp + 2, path.size() - 1)] - ps.origin;
        MoveInput in;
        in.forward = 1;
        // Caught on a corner for a second (a 64-wide doorway the route hugs): strafe left and right a little, like
        // a player would, until it's through.
        if (ticks - lastProgress > kTickRate) in.side = (ticks / 24) % 2 ? 1.0f : -1.0f;
        Vec3 before = ps.origin;
        playerMove(ps, in, std::atan2(aim.y, aim.x) / kDegToRad, speed, world ? *world : dust());
        travelled += length2d(ps.origin - before);
        if (++ticks - lastProgress > kTickRate * 3) {  // no waypoint for 3 s: stuck
            g_stuckAt = ps.origin;
            return -1.0f;
        }
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
    // What the index buys on long sight lines and bullets (a bot's shot traces 5000 units): time per trace.
    auto timeRays = [&](const World& w) {
        uint32_t seed = 999u;
        auto q = [&](float lo, float hi) {
            seed = seed * 1664525u + 1013904223u;
            return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
        };
        float sum = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20000; ++k) {
            const Vec3 a{q(-2400, 1950), q(-1200, 3250), q(-50, 150)}, b{q(-2400, 1950), q(-1200, 3250), q(-50, 150)};
            sum += w.traceRay(a, b).fraction;
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20000.0;
        return std::make_pair(us, sum);
    };
    const auto fast = timeRays(dust()), slow = timeRays(brute);
    std::printf("  long rays: %.2f us each indexed, %.2f us brute force\n", fast.first, slow.first);
    CHECK(fast.second == slow.second, "indexed and brute-force long rays disagree");
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
    // (on a ramp the grid keeps steps, up to 26 a cell since v0.29, and a bot follows the smooth slope between them:
    // up to half a step under the stepped floor is still on the ramp you see)
    CHECK(minWalk > 12000.0f && minAreas >= 6 && all.size() >= 20 && worstSink <= 13.0f, "walk %.0f areas %zu/%zu",
          double(minWalk), minAreas, all.size());

    // Sight: a bot looking away doesn't see you; once you shoot it, it turns on you.
    Dummy d;
    BotBrain b;
    spawnDeathmatchBot(d, b, dpt(-700, -1080), rng);
    d.yaw = d.prevYaw = -90;  // facing south, you're to the north-east (both in T spawn, out of its 250-unit reach)
    sense.playerUp = true;
    sense.playerOrigin = dpt(-200, -660);
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
// The knife (owner, v0.28): CS:GO's damage and reach, backstabs from behind only, the fanned rays forgive a little.
void testKnife() {
    std::printf("knife\n");
    World empty;
    // One attack from the origin facing +X at a dummy `dist` units away, facing `yaw` (180: towards you).
    auto attack = [&](bool stab, bool followUp, float dist, float yaw, float aimYaw, bool armor, ShotResult& r) {
        std::vector<Dummy> dd(1);
        dd[0].pos = dd[0].prevPos = {dist, 0, 0};
        dd[0].yaw = dd[0].prevYaw = dd[0].shownYaw = yaw;
        dd[0].armor = armor ? 100.0f : 0.0f;
        std::vector<Vec3> pos{dd[0].pos};
        r = knifeAttack(stab, followUp, {0, 0, 50}, 0, aimYaw, empty, dd, pos);
        return r.dummyIndex == 0;
    };
    ShotResult r;
    CHECK(attack(false, false, 40, 180, 0, false, r) && std::fabs(r.damage - 40.0f) < 0.01f && !r.backstab, "slash 40 (%.0f)", double(r.damage));
    CHECK(attack(false, true, 40, 180, 0, false, r) && std::fabs(r.damage - 25.0f) < 0.01f, "follow-up slash 25 (%.0f)", double(r.damage));
    CHECK(attack(false, false, 40, 0, 0, false, r) && r.backstab && std::fabs(r.damage - 90.0f) < 0.01f, "backstab slash 90 (%.0f)", double(r.damage));
    CHECK(attack(true, false, 30, 180, 0, false, r) && std::fabs(r.damage - 65.0f) < 0.01f, "stab 65 (%.0f)", double(r.damage));
    CHECK(attack(true, false, 30, 0, 0, true, r) && r.kill && r.backstab, "backstab stab kills through kevlar (%.0f)", double(r.damage));
    CHECK(attack(false, false, 40, 180, 0, true, r) && std::fabs(r.damage - 34.0f) < 0.01f, "kevlar takes 15%% off (%.0f)", double(r.damage));
    CHECK(!attack(true, false, 50, 180, 0, false, r) && attack(false, false, 50, 180, 0, false, r), "a stab is shorter than a slash");
    CHECK(!attack(false, false, 80, 0, 0, false, r), "out of reach: a miss");
    CHECK(attack(false, false, 40, 180, 9, false, r), "aimed a little beside the body still hits");
    CHECK(!attack(false, false, 40, 180, 55, false, r), "aimed well away misses");
    CHECK(attack(false, false, 40, 90, 0, false, r) && !r.backstab, "from the side is not a backstab");
    World wall;
    wall.solids.push_back({{20, -50, 0}, {30, 50, 100}, 0x808080});
    std::vector<Dummy> none;
    std::vector<Vec3> nopos;
    r = knifeAttack(false, false, {0, 0, 50}, 0, 0, wall, none, nopos);
    CHECK(r.hitWorld && r.dummyIndex < 0 && std::fabs(r.distance - 20.0f) < 0.5f, "a slash at a wall hits it (%.1f)", double(r.distance));
    std::printf("  slash 40 / 25 / backstab 90, stab 65 / backstab 180, reach 48 / 32\n");
}

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
        ws.scoped = def.scope;  // (snipers scoped: a noscope scatters)
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
        ws.scoped = def.scope;
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
        ws.scoped = def.scope;
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

    // Only the snipers scope; a noscope scatters, scoped and still is dead on.
    WeaponState awp;
    awp.def = &sniperDef();
    const float noscope = currentInaccuracy(awp, 0, true, false);
    awp.scoped = true;
    const float scoped = currentInaccuracy(awp, 0, true, false);
    int scopes = 0;
    for (int id = 0; id < kWeaponCount; ++id) scopes += weaponDef(id).scope ? 1 : 0;
    // Run speeds: scoped, the AWP halves (100) and the SSG 08 doesn't (230); crouched, scoping costs nothing more.
    const float awpScoped = moveSpeedFor(sniperDef(), true, false), ssgScoped = moveSpeedFor(weaponDef(kWSsg08), true, false);
    const float awpCrouchScoped = moveSpeedFor(sniperDef(), true, true);
    std::printf("  scoped run: AWP %.0f, SSG 08 %.0f; AWP crouched and scoped %.0f (x crouch)\n", double(awpScoped),
                double(ssgScoped), double(awpCrouchScoped));
    CHECK(awpScoped == 100.0f && ssgScoped == 230.0f && awpCrouchScoped == 200.0f, "scoped / crouched speeds");
    CHECK(noscope > 3.0f && scoped == 0.0f && scopes == 2 && weaponDef(kWSsg08).scope,
          "scope %.1f %.1f (%d scoped guns)", double(noscope), double(scoped), scopes);

    // The SSG 08 at the top of a jump, scoped, even moving sideways: dead on. Rising or falling it isn't, nor is the
    // AWP at the apex.
    WeaponState scout;
    scout.def = &weaponDef(kWSsg08);
    scout.scoped = true;
    scout.vertSpeed = 12.0f;
    const float apex = currentInaccuracy(scout, 230.0f, false, false);
    scout.vertSpeed = 150.0f;
    const float rising = currentInaccuracy(scout, 230.0f, false, false);
    awp.vertSpeed = 12.0f;
    const float awpApex = currentInaccuracy(awp, 0.0f, false, false);
    std::printf("  jump shots: SSG 08 at the apex %.2f deg, rising %.2f deg; AWP at the apex %.2f deg\n", double(apex),
                double(rising), double(awpApex));
    CHECK(apex == 0.0f && rising > 2.0f && awpApex > 2.0f, "the SSG 08 must be dead on at the apex only");
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
    // v0.28 (owner): every skin factory new; StatTrak about one in ten, its kills saved with it.
    {
        uint32_t r2 = 99;
        int st = 0, worn = 0;
        for (int k = 0; k < 20000; ++k) {
            const Item it = k % 2 ? rollCase(r2) : rollKnifeCase(r2);
            st += it.st;
            worn += it.wear >= 0.07f;
        }
        std::printf("  StatTrak %.1f%% of 20000 rolls, %d not factory new\n", st / 200.0, worn);
        CHECK(worn == 0 && st > 1700 && st < 2300, "factory new, StatTrak one in ten");
        Inventory sinv;
        sinv.items.push_back({findSkin("DEAGLE_BLAZE"), 0.02f, true, 42});
        sinv.equip[kWDeagle] = sinv.items[0];
        CHECK(sinv.save(path), "save StatTrak");
        Inventory sback;
        CHECK(sback.load(path) && sback.items.size() == 1 && sback.items[0].st && sback.items[0].kills == 42 &&
                  sback.equip[kWDeagle].st && sback.equip[kWDeagle].kills == 42,
              "StatTrak round trip");
        std::remove(path.c_str());
    }
    // Knife cases (owner, v0.27): one every 100 kills, only knives, every kind of knife comes up; saved.
    Inventory kinv;
    int knifeCases = 0;
    for (int k = 0; k < 250; ++k) knifeCases += kinv.addKnifeKill();
    const int opened = kinv.openKnife(rng);
    bool allKnives = opened >= 0 && skins[size_t(kinv.items[size_t(opened)].skin)].weapon == kWKnife;
    std::set<int> types;
    for (int k = 0; k < 2000; ++k) {
        const SkinDef& s = skins[size_t(rollKnifeCase(rng).skin)];
        allKnives = allKnives && s.weapon == kWKnife;
        types.insert(s.knife);
    }
    std::printf("  knife cases: %d from 250 kills; 2000 rolls: %zu kinds of knife, all knives %d\n", knifeCases, types.size(), int(allKnives));
    CHECK(knifeCases == 2 && kinv.knifeCases == 1 && kinv.knifeProgress == 50 && allKnives && types.size() == size_t(kKnifeTypes - 1),
          "knife cases");
    CHECK(kinv.save(path), "save");
    Inventory kback;
    CHECK(kback.load(path) && kback.knifeCases == 1 && kback.knifeProgress == 50, "knife cases round trip");
    std::remove(path.c_str());
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
// PROBE (temporary): how exposed each team's spawn and its walk to each site are to mid.
// Spawns in their own area (owner, v0.24): on both maps, at every Dust size, nobody in mid can see any team spawn
// point, and getting to mid from one is a run (the walls between spawn and mid, the dog-leg paths).
void testSpawnsOffMid() {
    std::printf("spawns off mid (every competitive spawn spot, v0.32: ten a side, shuffled each round)\n");
    int seen = 0, few = 0, cramped = 0, facing = 0;
    float closest = 1e9f;
    for (int town = 0; town < kTownMaps; ++town)
        for (float sc : {0.5f, 0.6f, 0.8f, 0.95f, 1.0f}) {
            if (town >= 1 && sc != 0.6f) continue;  // (Harbor and Inferno have one size)
            setTownMap(town);
            setDustScale(sc);
            const World w = buildTown();
            NavGrid nav;
            nav.build(townGrid(), w, townSpawn().pos);
            const MapGrid& m = townGrid();
            std::vector<Vec3> mid;
            for (int j = 0; j < m.h; ++j)
                for (int i = 0; i < m.w; ++i)
                    if (m.walkable(i, j) && midArea(townCallout(m.center(i, j)))) mid.push_back(m.center(i, j));
            std::vector<Vec3> path;
            std::vector<Vec3> spots[2];
            spawnSpots(w, nav, 10, spots);
            for (int side = 0; side < 2; ++side) {
                few += spots[side].size() < 7 && (town != 0 || sc >= 0.8f);  // (the game plays Dust at 90%)
                for (size_t a = 0; a < spots[side].size(); ++a) {
                    if (a >= 5) {  // (the new ones: the map's five are hand-placed)
                        cramped += !nav.standable(spots[side][a]);
                        for (size_t b = 0; b < a; ++b) cramped += length2d(spots[side][a] - spots[side][b]) < 89.0f;
                    }
                    if (side == 0)
                        for (const Vec3& o : spots[1])
                            facing += w.traceRay(spots[0][a] + Vec3{0, 0, 64}, o + Vec3{0, 0, 64}).fraction >= 1.0f;
                }
            }
            if (town != 0 || sc == 0.95f)
                std::printf("  %s: %zu T spawn spots, %zu CT\n", townMapName(town), spots[0].size(), spots[1].size());
            for (int side = 0; side < 2; ++side)
                for (const Vec3& sp : spots[side]) {
                    for (const Vec3& c : mid)
                        if (w.traceRay(c + Vec3{0, 0, 64}, sp + Vec3{0, 0, 64}).fraction >= 1.0f) {
                            ++seen;
                            std::printf("    %s %.0f%%: spawn (%.0f, %.0f) seen from mid (%.0f, %.0f)\n", townMapName(town),
                                        double(sc * 100), double(sp.x), double(sp.y), double(c.x), double(c.y));
                            break;
                        }
                    float best = 1e9f;  // the shortest run to mid, in real (unscaled) units
                    for (size_t q = 0; q < mid.size(); q += 2) {
                        Vec3 goal = mid[q];
                        if (!nav.findPath(sp, goal, path)) continue;
                        float len = 0;
                        for (size_t z = 1; z < path.size(); ++z) len += length2d(path[z] - path[z - 1]);
                        best = std::min(best, len);
                    }
                    closest = std::min(closest, best / (town == 0 ? sc : 1.0f));
                }
        }
    setTownMap(0);
    setDustScale(0.6f);
    std::printf("  team spawns seen from mid: %d; the shortest run from a spawn to mid: %.0f units (real size)\n", seen,
                double(closest));
    std::printf("  maps short of seven spots a side: %d; spots without room or too close together: %d; T and CT spots that see each other: %d\n",
                few, cramped, facing);
    CHECK(seen == 0 && closest > 500.0f, "spawns must be out of mid's sight and a run away (%d seen, %.0f)", seen, double(closest));
    CHECK(few == 0 && cramped == 0 && facing == 0, "few %d cramped %d facing %d", few, cramped, facing);
}

// The Ts' ways onto the sites (owner, v0.26: short, lowers and mid to B are plays now that mid is open): every
// staging point has a route from T spawn, and the route from it to its site crosses the way's own area (short goes
// over catwalk, lowers through the lower tunnels, mid to B through B doors); the mid-control spots are reachable.
void testCompWays() {
    std::printf("competitive ways in\n");
    const float keep = townScale();
    int bad = 0;
    for (int town = 0; town < kTownMaps; ++town)
        for (float sc : {0.6f, 0.8f, 0.9f, 0.95f}) {
            if (town >= 1 && sc != 0.6f) continue;  // (Harbor and Inferno have one size)
            setTownMap(town);
            setDustScale(sc);
            const World w = buildTown();
            NavGrid nav;
            nav.build(townGrid(), w, townSpawn().pos);
            const TownTactics& tac = townTactics();
            const Vec3 spawn = townTeamSpawns(0)[0];
            std::vector<Vec3> path;
            auto len = [&](const std::vector<Vec3>& p) {
                float l = 0;
                for (size_t k = 1; k < p.size(); ++k) l += length2d(p[k] - p[k - 1]);
                return l;
            };
            for (const TownWay& way : tac.ways) {
                const Vec3 stage = townPoint(way.stage[0], way.stage[1]);
                const RetakeSite& site = townRetakeSites()[size_t(way.site)];
                const Vec3 bomb = townPoint(site.bombX, site.bombY);
                if (!nav.findPath(spawn, stage, path)) {
                    ++bad;
                    std::printf("    %s %.0f%%: no route from T spawn to the %s stage\n", townMapName(town), double(sc * 100), way.name);
                    continue;
                }
                const float toStage = len(path);
                if (!nav.findPath(stage, bomb, path)) {
                    ++bad;
                    std::printf("    %s %.0f%%: no route from the %s stage to %s\n", townMapName(town), double(sc * 100), way.name, site.name);
                    continue;
                }
                bool crosses = false;
                for (const Vec3& c : path) crosses |= std::strcmp(townCallout(c), way.through) == 0;
                if (!crosses) {
                    ++bad;
                    std::printf("    %s %.0f%%: %s's route to %s misses %s\n", townMapName(town), double(sc * 100), way.name, site.name, way.through);
                }
                if (sc == 0.9f || town == 1)
                    std::printf("    %s: %-9s spawn -> stage %4.1f s, stage -> %s %4.1f s\n", townMapName(town), way.name,
                                double(toStage / kBotRunSpeed), site.name, double(len(path) / kBotRunSpeed));
            }
            for (const auto& m : tac.mid)
                if (!nav.findPath(spawn, townPoint(m[0], m[1]), path)) {
                    ++bad;
                    std::printf("    %s %.0f%%: a mid-control spot has no route\n", townMapName(town), double(sc * 100));
                }
        }
    setTownMap(0);
    setDustScale(keep);
    CHECK(bad == 0, "%d ways in are broken", bad);
}

// Fire: a bot caught in a molotov steps out within a second (holding an angle or walking through), and one walking
// to a goal past the fire waits at its edge instead of burning.
// No stacking (owner, v0.26): five bots walking from T spawn to A site together keep a gap (kBotGap) behind the one
// ahead instead of walking on its back: no two of them within 64 units, both moving, for 2 s in all. Without their
// teammates' positions (the old way) they bunch up.
void testBotSpacing() {
    std::printf("bots keep a gap\n");
    const NavGrid& nav = dustNav();
    auto run = [&](bool gaps, float& worstPair, float& slowest) {
        BotSenses sense;
        sense.world = &dust();
        sense.nav = &nav;
        const std::vector<BotTarget> none;
        std::vector<BotTarget> mates;
        sense.targets = &none;
        sense.mates = gaps ? &mates : nullptr;
        const Vec3 starts[5] = {dpt(-300, -700), dpt(-700, -760), dpt(-80, -800), dpt(-200, -960), dpt(-600, -1020)};
        const Vec3 goal = dpt(1380, 2560);
        Dummy d[5];
        BotBrain b[5];
        uint32_t rng = 99u;
        for (int k = 0; k < 5; ++k) {
            spawnDeathmatchBot(d[k], b[k], starts[k], rng);
            b[k].goal = goal;
            b[k].hasGoal = b[k].holdOnly = true;
            b[k].state = 0;
        }
        float together[5][5] = {};
        slowest = 0;
        for (int t = 0; t < kTickRate * 40; ++t) {
            sense.now = double(t) * kTickDt;
            mates.clear();
            for (int k = 0; k < 5; ++k) mates.push_back({k, d[k].pos, d[k].pos});
            Vec3 before[5];
            for (int k = 0; k < 5; ++k) {
                before[k] = d[k].pos;
                d[k].prevPos = d[k].pos;
                sense.self = k;
                updateDeathmatchBot(d[k], b[k], sense, rng);
            }
            for (int i = 0; i < 5; ++i)
                for (int j = i + 1; j < 5; ++j)
                    if (length2d(d[i].pos - before[i]) > 0.3f && length2d(d[j].pos - before[j]) > 0.3f && length2d(d[i].pos - d[j].pos) < 64.0f)
                        together[i][j] += float(kTickDt);
            for (int k = 0; k < 5; ++k)
                if (length2d(d[k].pos - goal) > 120.0f) slowest = float(t + 1) * kTickDt;
        }
        worstPair = 0;
        for (int i = 0; i < 5; ++i)
            for (int j = i + 1; j < 5; ++j) worstPair = std::max(worstPair, together[i][j]);
    };
    float stackedOld, slowOld, stacked, slow;
    run(false, stackedOld, slowOld);
    run(true, stacked, slow);
    std::printf("  5 bots T spawn -> A: the closest pair walked together %.1f s (without gaps %.1f s); all there in %.1f s (%.1f)\n",
                double(stacked), double(stackedOld), double(slow), double(slowOld));
    CHECK(stacked < 2.0f, "two bots walked on top of each other for %.1f s", double(stacked));
    CHECK(slow < 39.0f, "the bots didn't all get there (keeping gaps held them up)");
}

// Retakes from different sides (owner, v0.26: two CTs retaking A on Dust shouldn't both come through CT): one from CT
// spawn and one from CT mid take different ways onto A (CT spawn, catwalk); on Harbor, from its CT spawn and CT mid.
void testRetakeSpread() {
    std::printf("retakes from different sides\n");
    const float keep = townScale();
    int same = 0;
    for (int town = 0; town < kTownMaps; ++town) {
        setTownMap(town);
        setDustScale(0.9f);
        const World w = buildTown();
        NavGrid nav;
        nav.build(townGrid(), w, townSpawn().pos);
        const RetakeSite& a = townRetakeSites()[0];
        std::vector<Vec3> entries;
        for (const RetakeSpot& e : a.entries) entries.push_back(townPoint(e.x, e.y));
        const RetakeSpot& midSpot = townCtSpots(kCtMid)[0];  // (Inferno: top mid)
        const std::vector<Vec3> cts = town == 0   ? std::vector<Vec3>{townPoint(-180, 2820), townPoint(-620, 2000)}
                                      : town == 1 ? std::vector<Vec3>{townPoint(0, 1620), townPoint(-370, 600)}
                                                  : std::vector<Vec3>{townTeamSpawns(1)[0], townPoint(midSpot.x, midSpot.y)};
        const std::vector<int> way = spreadEntries(nav, cts, entries, townPoint(a.bombX, a.bombY), 650.0f * townScale(), 2.5f, 1500.0f);
        std::printf("  %s: CT spawn -> %s, CT mid -> %s\n", townMapName(town), way[0] >= 0 ? townCallout(entries[size_t(way[0])]) : "straight in",
                    way[1] >= 0 ? townCallout(entries[size_t(way[1])]) : "straight in");
        same += way[0] == way[1];
    }
    setTownMap(0);
    setDustScale(keep);
    CHECK(same == 0, "%d retakes all came the same way", same);
}

// Preaim (owner, v0.26): a bot walking to A (by long, then by short) keeps its crosshair on the CT angles it can see
// ahead: of the moments a CT spot or A hold is in view ahead of it, 90% and more find it already looking within 15
// degrees of one (a bot that just looks where it walks: about three in four).
void testBotPreaim() {
    std::printf("bots pre-aim\n");
    const NavGrid& nav = dustNav();
    std::vector<Vec3> angles;
    for (int role = 0; role < kCtRoles; ++role)
        for (const RetakeSpot& h : townCtSpots(role)) angles.push_back(dpt(h.x, h.y));
    for (const RetakeSpot& h : townRetakeSites()[0].holds) angles.push_back(dpt(h.x, h.y));
    auto run = [&](bool preaim, const Vec3& start) {
        BotSenses sense;
        sense.world = &dust();
        sense.nav = &nav;
        const std::vector<BotTarget> none;
        sense.targets = &none;
        sense.angles = preaim ? &angles : nullptr;
        Dummy d;
        BotBrain b;
        uint32_t rng = 5u;
        spawnDeathmatchBot(d, b, start, rng);
        d.yaw = d.prevYaw = 90.0f;
        b.goal = dpt(1380, 2560);
        b.hasGoal = b.holdOnly = true;
        b.state = 0;
        int inView = 0, onIt = 0;
        for (int t = 0; t < kTickRate * 30 && b.state == 0; ++t) {
            sense.now = double(t) * kTickDt;
            d.prevPos = d.pos;
            updateDeathmatchBot(d, b, sense, rng);
            if ((t & 7) != 0) continue;
            const Vec3 eye = d.pos + Vec3{0, 0, dummyEyeZ()};
            float closest = 1e30f;
            bool any = false;
            for (const Vec3& p : angles) {
                const float dist = length2d(p - d.pos);
                if (dist < 150.0f || dist > 1600.0f) continue;
                const float yaw = std::atan2(p.y - d.pos.y, p.x - d.pos.x) / kDegToRad;
                float off = yaw - d.yaw;
                while (off > 180.0f) off -= 360.0f;
                while (off < -180.0f) off += 360.0f;
                if (dust().traceRay(eye, p + Vec3{0, 0, dummyEyeZ()}).fraction < 1.0f) continue;
                any = true;
                closest = std::min(closest, std::fabs(off));
            }
            inView += any;
            onIt += any && closest < 15.0f;
        }
        return inView ? float(onIt) / float(inView) : 0.0f;
    };
    const Vec3 starts[2] = {dpt(1500, 1100), dpt(150, 1150)};  // long, the bottom of catwalk
    float with = 0, without = 0;
    for (const Vec3& st : starts) {
        with += run(true, st) * 0.5f;
        without += run(false, st) * 0.5f;
    }
    std::printf("  walking to A with a CT angle in view: on one %.0f%% of the time (looking where it walks: %.0f%%)\n",
                double(with * 100), double(without * 100));
    CHECK(with > 0.9f && with > without + 0.15f, "pre-aim %.0f%% vs %.0f%%", double(with * 100), double(without * 100));
}

// Real aim (v0.31): a bot's bullets go where its crosshair is. A duel in the open: a bot of each skill spots you 40
// degrees off its aim at three ranges while you stand, ADAD (counter-strafing at the ends) or run across it; it
// shoots an AK at you (kevlar + helmet, like deathmatch). Prints time to first hit, hit rate and time to kill, next to
// the old model (where you were a moment ago plus random error) for comparison.
struct DuelResult { float firstHit = 0, hitRate = 0, kill = 0, headRate = 0; };

// Your position sideways at time t: 0 still, 1 ADAD (0.35 s strafes at full speed, a counter-strafed stop of 0.15 s
// between), 2 running straight across at 250 u/s.
float duelSide(int move, float t) {
    if (move == 0) return 0;
    if (move == 2) return -250.0f + 250.0f * t;
    const float cycle = 0.5f, ph = std::fmod(t, 2 * cycle);  // right 0.35 s, stop 0.15, left 0.35, stop 0.15
    auto leg = [](float u) { return 250.0f * std::min(u, 0.35f); };
    return ph < cycle ? leg(ph) - 45.0f : leg(cycle) - leg(ph - cycle) - 45.0f;
}

DuelResult duel(int level, float dist, int move, bool oldModel, int trials) {
    static const World open;  // nothing in the way
    const NavGrid& nav = dustNav();
    const BotSkill sk = botSkill(level);
    const WeaponDef& gd = weaponDef(kWRifle);
    DuelResult r;
    int shots = 0, hits = 0, heads = 0, firstHits = 0, kills = 0;
    float firstSum = 0, killSum = 0;
    for (int trial = 0; trial < trials; ++trial) {
        uint32_t rng = 1234u + uint32_t(trial) * 7919u + uint32_t(level) * 31u + uint32_t(move) * 101u;
        Dummy d;
        BotBrain b;
        spawnDeathmatchBot(d, b, Vec3{0, 0, 0}, rng);
        d.yaw = d.prevYaw = 40.0f;  // you appear 40 degrees off its aim
        b.frozen = true;
        b.state = 1;
        BotSenses s;
        s.world = &open;
        s.nav = &nav;
        s.playerUp = true;
        s.skill = sk;
        float cooldown = 0, dealt = 0, first = -1, react = sk.reactMin + botRand(rng) * sk.reactRange;
        Vec3 history[64];
        for (int t = 0; t < kTickRate * 3; ++t) {
            const float now = float(t) * float(kTickDt);
            s.now = now;
            const Vec3 you{dist, duelSide(move, now), 0};
            history[t & 63] = you + Vec3{0, 0, kStandEye};
            s.playerOrigin = you;
            s.playerEye = you + Vec3{0, 0, kStandEye};
            d.prevPos = d.pos;
            updateDeathmatchBot(d, b, s, rng);
            cooldown -= float(kTickDt);
            const Vec3 eye = d.pos + Vec3{0, 0, dummyEyeZ()};
            Vec3 dir;
            if (oldModel) {
                if (now < react || cooldown > 0 || !b.sees) continue;
                cooldown = (botShotGap(kWRifle) + botRand(rng) * 0.16f) * sk.fireScale;
                Vec3 aim = history[(t - sk.lagTicks + 64 * 8) & 63] - Vec3{0, 0, botRand(rng) < sk.headChance ? 2.0f : 16.0f};
                if (t < sk.lagTicks) aim = history[0] - Vec3{0, 0, 16.0f};
                const float err = length(aim - eye) * 0.014f * sk.aimError;
                aim = aim + Vec3{(botRand(rng) - 0.5f) * 2 * err, (botRand(rng) - 0.5f) * 2 * err, (botRand(rng) - 0.5f) * err};
                dir = normalize(aim - eye);
            } else {
                if (!botReady(b) || cooldown > 0) continue;
                const float r1 = botRand(rng), r2 = botRand(rng);
                dir = botShotDir(d, b, gd, sk, r1, r2);
                cooldown = botNextShot(kWRifle, dist, b, botRand(rng), sk.fireScale);
                d.shotAt = now;
            }
            ++shots;
            float best = 1e9f, tt;
            int hit = 0;
            if (rayHitsBox(eye, dir, best, you + Vec3{-13, -13, 0}, you + Vec3{13, 13, kStandHeight - 10}, tt) && tt >= 0) { best = tt; hit = 1; }
            if (rayHitsBox(eye, dir, best, you + Vec3{-5, -5, kStandHeight - 10}, you + Vec3{5, 5, kStandHeight}, tt) && tt >= 0) { best = tt; hit = 2; }
            if (!hit) continue;
            ++hits;
            heads += hit == 2;
            const HitGroup grp = hit == 2 ? kHead : kChest;
            dealt += armoredDamage(damageAt(gd, best) * hitGroupDamageScale(grp), grp, 100.0f, true, gd.armorRatio);
            if (first < 0) { first = now; firstSum += now; ++firstHits; }
            if (dealt >= 100.0f) { killSum += now; ++kills; break; }
        }
        if (dealt < 100.0f) killSum += 3.0f;  // no kill in 3 s: counts as 3
    }
    r.firstHit = firstHits ? firstSum / float(firstHits) : 9.0f;
    r.hitRate = shots ? float(hits) / float(shots) : 0;
    r.headRate = hits ? float(heads) / float(hits) : 0;
    r.kill = killSum / float(trials);
    (void)kills;
    return r;
}

// Peeking a bot that holds the angle (v0.32, owner: "their reaction time and adjustment speed at close range is
// hard to even counter... especially when they've seen you at the angle and are holding it"). The bot is aimed at
// the corner; you swing out from behind it at full speed and stop (a wide peek), at `dist`. `repeek`: you showed
// yourself for 0.3 s first, stepped back behind the corner for 0.4 s and peek again. Times from the moment it could first see you:
// its first shot, its first hit, and the kill (AK, you in kevlar + helmet; 3 s at most).
struct PeekResult { float shot = 0, hit = 0, kill = 0; };

PeekResult peekBot(int level, float dist, bool repeek, int trials) {
    World w;
    Box wall;
    wall.mins = {dist - 60.0f, 30.0f, 0};
    wall.maxs = {dist - 20.0f, 600.0f, 200.0f};
    w.solids.push_back(wall);
    const BotSkill sk = botSkill(level);
    const WeaponDef& gd = weaponDef(kWRifle);
    PeekResult r;
    float shotSum = 0, hitSum = 0, killSum = 0;
    int shots = 0, hits = 0;
    for (int trial = 0; trial < trials; ++trial) {
        uint32_t rng = 777u + uint32_t(trial) * 7919u + uint32_t(level) * 31u;
        Dummy d;
        BotBrain b;
        spawnDeathmatchBot(d, b, Vec3{0, 0, 0}, rng);
        d.yaw = d.prevYaw = 0.0f;  // on the corner
        b.frozen = true;
        b.state = 1;
        BotSenses s;
        s.world = &w;
        s.nav = &dustNav();
        s.playerUp = true;
        s.skill = sk;
        // Your sideways position over time: just behind the corner at 50, swing out to -60 at 250 u/s and stop; a
        // re-peek shows you for 0.3 s first, steps back behind the corner and waits 0.4 s.
        const float out = 0.44f;  // seconds from 50 to -60
        const float start = repeek ? out + 0.3f + out + 0.4f : 0.0f;
        auto side = [&](float t) {
            if (repeek && t < start) {
                if (t < out) return 50.0f - 250.0f * t;
                if (t < out + 0.3f) return -60.0f;
                if (t < out + 0.3f + out) return -60.0f + 250.0f * (t - out - 0.3f);
                return 50.0f;
            }
            const float u = t - start;
            return u < out ? 50.0f - 250.0f * u : -60.0f;
        };
        float cooldown = 0, dealt = 0, seenAt = -1, firstShot = -1, firstHit = -1;
        for (int t = 0; t < kTickRate * 6; ++t) {
            const float now = float(t) * float(kTickDt);
            s.now = now;
            const Vec3 you{dist, side(now), 0};
            s.playerOrigin = you;
            s.playerEye = you + Vec3{0, 0, kStandEye};
            d.prevPos = d.pos;
            updateDeathmatchBot(d, b, s, rng);
            cooldown -= float(kTickDt);
            const Vec3 eye = d.pos + Vec3{0, 0, dummyEyeZ()};
            if (now >= start && seenAt < 0 && w.traceRay(eye, s.playerEye).fraction >= 1.0f) seenAt = now;
            if (!botReady(b) || cooldown > 0) continue;
            const float r1 = botRand(rng), r2 = botRand(rng);
            const Vec3 dir = botShotDir(d, b, gd, sk, r1, r2);
            cooldown = botNextShot(kWRifle, dist, b, botRand(rng), sk.fireScale);
            d.shotAt = now;
            if (seenAt < 0) continue;  // (the first look of a re-peek: not counted)
            if (firstShot < 0) firstShot = now - seenAt;
            float best = w.traceRay(eye, eye + dir * 5000.0f).fraction * 5000.0f, tt;
            int hit = 0;
            if (rayHitsBox(eye, dir, best, you + Vec3{-13, -13, 0}, you + Vec3{13, 13, kStandHeight - 10}, tt) && tt >= 0) { best = tt; hit = 1; }
            if (rayHitsBox(eye, dir, best, you + Vec3{-5, -5, kStandHeight - 10}, you + Vec3{5, 5, kStandHeight}, tt) && tt >= 0) { best = tt; hit = 2; }
            if (!hit) continue;
            if (firstHit < 0) firstHit = now - seenAt;
            const HitGroup grp = hit == 2 ? kHead : kChest;
            dealt += armoredDamage(damageAt(gd, best) * hitGroupDamageScale(grp), grp, 100.0f, true, gd.armorRatio);
            if (dealt >= 100.0f) break;
        }
        if (firstShot >= 0) { shotSum += firstShot; ++shots; }
        if (firstHit >= 0) { hitSum += firstHit; ++hits; }
        killSum += dealt >= 100.0f && seenAt >= 0 ? std::min(3.0f, float(s.now) - seenAt) : 3.0f;
    }
    r.shot = shots ? shotSum / float(shots) : 9.0f;
    r.hit = hits ? hitSum / float(hits) : 9.0f;
    r.kill = killSum / float(trials);
    return r;
}

void testBotPeek() {
    std::printf("peeking a bot that holds the angle (times from when it can first see you)\n");
    for (int level = 0; level < 4; ++level)
        for (float dist : {300.0f, 800.0f}) {
            const PeekResult p = peekBot(level, dist, false, 60), q = peekBot(level, dist, true, 60);
            std::printf("  %-6s %4.0f u: first shot %.2f s, first hit %.2f s, kill %.2f s   re-peek: %.2f, %.2f, %.2f\n",
                        botSkill(level).name, double(dist), double(p.shot), double(p.hit), double(p.kill), double(q.shot),
                        double(q.hit), double(q.kill));
        }
}

void testBotAim() {
    std::printf("bots aim for real (an AK duel in the open, %s)\n", "you in kevlar + helmet, 3 s at most");
    const char* moves[3] = {"still", "ADAD", "running"};
    const float dists[3] = {400.0f, 1000.0f, 1800.0f};
    int wrong = 0, calib = 0;
    for (int level = 0; level < 4; ++level) {
        std::printf("  %s\n", botSkill(level).name);
        for (float dist : dists)
            for (int move = 0; move < 3; ++move) {
                const DuelResult n = duel(level, dist, move, false, 60), o = duel(level, dist, move, true, 60);
                std::printf("    %4.0f u %-7s first hit %.2f s, hits %3.0f%% (head %2.0f%%), kill %.2f s   (old: %.2f s, %3.0f%%, kill %.2f s)\n",
                            double(dist), moves[move], double(n.firstHit), double(n.hitRate * 100), double(n.headRate * 100),
                            double(n.kill), double(o.firstHit), double(o.hitRate * 100), double(o.kill));
                if (move > 0) wrong += !(n.hitRate < duel(level, dist, 0, false, 60).hitRate);  // moving is harder to hit
                if (move == 0 && dist == 1000.0f) calib += !(n.kill > o.kill * 0.6f && n.kill < o.kill * 1.5f);
            }
    }
    CHECK(wrong == 0, "%d cases where moving was easier to hit than standing", wrong);
    CHECK(calib == 0, "%d skills far off the old time to kill at mid range", calib);
    // Better skill, quicker kills.
    int order = 0;
    for (float dist : dists)
        for (int level = 1; level < 4; ++level) order += !(duel(level, dist, 0, false, 60).kill < duel(level - 1, dist, 0, false, 60).kill);
    CHECK(order == 0, "%d", order);
    // Its hand: a 40 degree flick overshoots a little and settles; its crosshair is what it shoots along.
    {
        static const World open;
        Dummy d;
        BotBrain b;
        uint32_t rng = 3u;
        spawnDeathmatchBot(d, b, Vec3{0, 0, 0}, rng);
        d.yaw = d.prevYaw = 40.0f;
        b.frozen = true;
        b.state = 1;
        BotSenses s;
        s.world = &open;
        s.nav = &dustNav();
        s.playerUp = true;
        s.playerOrigin = Vec3{1000, 0, 0};
        s.playerEye = s.playerOrigin + Vec3{0, 0, kStandEye};
        float most = 40.0f, settledAt = -1;
        for (int t = 0; t < kTickRate; ++t) {
            s.now = double(t) * kTickDt;
            updateDeathmatchBot(d, b, s, rng);
            most = std::min(most, d.yaw);
            if (settledAt < 0 && std::fabs(d.yaw) < 3.0f) settledAt = float(s.now);
        }
        const Vec3 dir = botShotDir(d, b, weaponDef(kWRifle), s.skill, 0, 0);
        const Vec3 view = anglesToForward(d.pitch, d.yaw);
        std::printf("  NORMAL flicks 40 degrees: within 3 degrees after %.2f s, overshoots by %.1f degrees\n", double(settledAt),
                    double(-most));
        CHECK(settledAt > 0.05f && settledAt < 0.5f && -most > 0.2f && -most < 6.0f, "settled %.2f overshoot %.1f",
              double(settledAt), double(-most));
        CHECK(dot(dir, view) > 0.99999f, "first shot off the crosshair");
    }
}

// Checking spots (v0.31): walking somewhere, a bot clears the angles it passes one after another (the known holds,
// then corners someone could be tucked into) instead of keeping its crosshair on one; two teammates walking together
// split them (one covers an angle, the other checks the next).
void testBotChecks() {
    std::printf("bots check spots as they walk\n");
    const NavGrid& nav = dustNav();
    std::vector<Vec3> angles;  // what a T checks: the CT spots and the site holds, then the corners
    for (int role = 0; role < kCtRoles; ++role)
        for (const RetakeSpot& h : townCtSpots(role)) angles.push_back(dpt(h.x, h.y));
    for (const RetakeSite& site : townRetakeSites())
        for (const RetakeSpot& h : site.holds) angles.push_back(dpt(h.x, h.y));
    const size_t keys = angles.size();
    const std::vector<Vec3> corners = nav.hidingSpots(dust(), 160.0f, angles);
    angles.insert(angles.end(), corners.begin(), corners.end());
    std::printf("  Dust: %zu known holds, %zu corners\n", keys, corners.size());
    CHECK(corners.size() > 40 && corners.size() < 600, "%zu corners", corners.size());
    if (std::ifstream("CRISP_DUMP").good()) {  // (debugging: the spots, for a picture over the nav grid)
        std::ofstream f("dust_spots.txt");
        for (size_t k = 0; k < angles.size(); ++k) f << (k < keys ? "K " : "C ") << angles[k].x << ' ' << angles[k].y << '\n';
        const MapGrid& m = townGrid();
        std::ofstream gf("dust_grid.txt");
        gf << m.x0 << ' ' << m.y0 << ' ' << m.cell << ' ' << m.w << ' ' << m.h << '\n';
        for (int j = 0; j < m.h; ++j) {
            for (int i = 0; i < m.w; ++i)
                gf << (!m.walkable(i, j) ? '#' : nav.roamable(m.center(i, j)) ? '.' : nav.standable(m.center(i, j)) ? ',' : 'x');
            gf << '\n';
        }
    }
    // Bots walking from `starts` to A at once (a group), sharing what they cover or not.
    struct Walk { int checked = 0, corners = 0; float same = 0; };
    auto walk = [&](const std::vector<Vec3>& starts, bool share, const std::vector<Vec3>& list, size_t keyCount) {
        const size_t n = starts.size();
        std::vector<Dummy> d(n);
        std::vector<BotBrain> b(n);
        std::vector<SpotClaim> looked(list.size());
        std::vector<BotTarget> mates(n);
        uint32_t rng = 11u;
        for (size_t i = 0; i < n; ++i) {
            spawnDeathmatchBot(d[i], b[i], starts[i], rng);
            d[i].yaw = d[i].prevYaw = 90.0f;
            b[i].goal = dpt(1380, 2560);
            b[i].hasGoal = b[i].holdOnly = true;
            b[i].state = 0;
        }
        BotSenses s;
        s.world = &dust();
        s.nav = &nav;
        const std::vector<BotTarget> none;
        s.targets = &none;
        s.angles = &list;
        s.keyAngles = keyCount;
        s.looked = share ? &looked : nullptr;
        s.mates = &mates;
        std::set<int> seen[8];
        int both = 0, sameSpot = 0;
        for (int t = 0; t < kTickRate * 30; ++t) {
            s.now = double(t) * kTickDt;
            for (size_t i = 0; i < n; ++i) mates[i] = {int(i), d[i].pos, d[i].pos};
            bool walking = false;
            for (size_t i = 0; i < n; ++i) {
                if (b[i].state != 0) continue;
                walking = true;
                s.self = int(i);
                d[i].prevPos = d[i].pos;
                updateDeathmatchBot(d[i], b[i], s, rng);
                for (int q = 0; q < 8; ++q)
                    if (b[i].checkedSpot[q] >= 0) seen[i].insert(b[i].checkedSpot[q]);
            }
            if (!walking) break;
            if (n > 1 && b[0].state == 0 && b[1].state == 0 && b[0].lookSpot >= 0 && b[1].lookSpot >= 0) {
                ++both;
                sameSpot += b[0].lookSpot == b[1].lookSpot;
            }
        }
        Walk w;
        for (int k : seen[0]) {
            ++w.checked;
            w.corners += size_t(k) >= keyCount;
        }
        w.same = both ? float(sameSpot) / float(both) : 0.0f;
        return w;
    };
    const Walk alone = walk({dpt(1500, 1100)}, false, angles, keys);
    const Walk cornersOnly = walk({dpt(1500, 1100)}, false, corners, 0);
    std::printf("  walking up long to A: checked %d spots (knowing only the corners: %d)\n", alone.checked, cornersOnly.checked);
    CHECK(alone.checked >= 5 && cornersOnly.checked >= 2, "checked %d, corners only %d", alone.checked, cornersOnly.checked);
    const std::vector<Vec3> pair = {dpt(1500, 1100), dpt(1500, 950)};
    const Walk apart = walk(pair, true, angles, keys), together = walk(pair, false, angles, keys);
    std::printf("  two walking together: on the same angle %.0f%% of the time (not sharing: %.0f%%)\n",
                double(apart.same * 100), double(together.same * 100));
    CHECK(apart.same < 0.3f && apart.same < together.same, "%.2f vs %.2f", double(apart.same), double(together.same));
}

// Plays by where they spawned (v0.32): on Dust's T spawn, four Ts from random spots split two and two between long
// and tunnels; the ones assigned by where they stand walk less than the old split (in the order they were listed).
void testSpawnGroups() {
    std::printf("plays by where the bots spawn\n");
    const NavGrid& nav = dustNav();
    std::vector<Vec3> spots[2];
    spawnSpots(dust(), nav, 10, spots);
    const TownTactics& tac = townTactics();
    std::vector<Vec3> goals;
    for (const TownWay& w : tac.ways)
        if (std::string(w.name) == "LONG" || std::string(w.name) == "TUNNELS") goals.push_back(dpt(w.stage[0], w.stage[1]));
    CHECK(goals.size() == 2, "%zu", goals.size());
    if (goals.size() != 2) return;
    std::vector<Vec3> path;
    auto len = [&](const Vec3& a, const Vec3& b) {
        if (!nav.findPath(a, b, path)) return 1e6f;
        float l = 0;
        for (size_t k = 1; k < path.size(); ++k) l += length2d(path[k] - path[k - 1]);
        return l;
    };
    uint32_t rng = 21u;
    float byOrder = 0, bySpawn = 0;
    int better = 0, rounds = 0;
    for (int round = 0; round < 40; ++round) {
        std::vector<Vec3> from = spots[0];
        for (size_t k = from.size(); k > 1; --k) std::swap(from[k - 1], from[size_t(botRand(rng) * float(k)) % k]);
        from.resize(4);
        const std::vector<int> got = assignNearest(nav, from, goals, {2, 2}, {});
        float a = 0, b = 0;
        for (size_t k = 0; k < from.size(); ++k) {
            a += len(from[k], goals[k % 2]);
            b += len(from[k], goals[size_t(got[k])]);
        }
        byOrder += a;
        bySpawn += b;
        better += b <= a + 1.0f;
        ++rounds;
    }
    std::printf("  four Ts, two to long and two to tunnels, 40 random spawns: %.0f units walked a round (in order: %.0f)\n",
                double(bySpawn / float(rounds)), double(byOrder / float(rounds)));
    CHECK(better == rounds && bySpawn < byOrder, "%d of %d, %.0f vs %.0f", better, rounds, double(bySpawn), double(byOrder));
}

// The console (v0.33): lines split into commands and words, Tab completes, history, and every setting can be read and
// set by its config name (and the config text reads back the same).
void testConsole() {
    std::printf("console\n");
    int bad = 0;
    const std::vector<std::string> cmds = splitCommands("give ak47; echo \"a;b\" ;  ;god");
    bad += !(cmds.size() == 3 && cmds[0] == "give ak47" && cmds[1] == " echo \"a;b\" " && cmds[2] == "god");
    const std::vector<std::string> args = splitArgs("  connect  \"10.0.0.1:27015\"  now ");
    bad += !(args.size() == 3 && args[0] == "connect" && args[1] == "10.0.0.1:27015" && args[2] == "now");
    Console con;
    const std::vector<std::string> names = {"bot_add", "bot_add_t", "bot_kick", "god", "give"};
    con.line = "bot_a";
    con.complete(names);
    bad += con.line != "bot_add";  // (as far as bot_add and bot_add_t agree)
    con.line = "gi";
    con.complete(names);
    bad += con.line != "give ";
    con.line = "god";
    con.submit();
    con.line = "give awp";
    con.submit();
    con.historyUp();
    bad += con.line != "give awp";
    con.historyUp();
    bad += con.line != "god";
    con.historyDown();
    con.historyDown();
    bad += !con.line.empty();
    // Settings by name: every one reads back, a set sticks, the text round-trips.
    Config c;
    const std::vector<std::string> keys = configKeys();
    int unreadable = 0;
    for (const std::string& k : keys) {
        std::string v;
        unreadable += !configValue(c, k, v);
    }
    bad += !setConfigValue(c, "fov", "100") || c.fov != 100.0f;
    bad += !setConfigValue(c, "player_name", "MARTY") || c.player_name != "MARTY";
    bad += setConfigValue(c, "no_such_thing", "1") || setConfigValue(c, "fov", "wide");
    Config back;
    std::istringstream in(configText(c));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string k, v;
        if (!(ls >> k) || k.rfind("//", 0) == 0) continue;
        std::getline(ls >> std::ws, v);
        setConfigValue(back, k, v);
    }
    bad += configText(back) != configText(c);
    std::printf("  %zu settings by name (%d unreadable), commands, completion, history: %d wrong\n", keys.size(), unreadable, bad);
    CHECK(bad == 0 && unreadable == 0 && keys.size() > 60, "%d wrong, %d unreadable, %zu keys", bad, unreadable, keys.size());
}

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
    {  // the spots bots check as they walk (v0.31)
        const size_t hiding = nav.hidingSpots(w, 160.0f, {}).size();
        std::printf("  %zu corners and spots behind props for bots to check\n", hiding);
        CHECK(hiding > 30, "%zu", hiding);
    }
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
    for (const TownWay& wy : tac.ways) spots.push_back({wy.name, townPoint(wy.stage[0], wy.stage[1])});
    for (const auto& m : tac.mid) spots.push_back({"mid control", townPoint(m[0], m[1])});
    for (const RetakeSpot& h : tac.pushes) spots.push_back({"push", P(h)});
    int bad = 0, unreachable = 0;
    std::vector<Vec3> path;
    for (const Spot& sp : spots) {
        // (+6: on a ramp the grid's floor height is a few units under the slope's surface; spawning drops you onto it)
        const bool clear = sp.p.z > MapGrid::kNoFloor && w.boxFits(sp.p + Vec3{0, 0, 6.0f}, hullMins(), hullMaxs(false)) &&
                           w.traceBox(sp.p + Vec3{0, 0, 6.0f}, sp.p - Vec3{0, 0, 24}, hullMins(), hullMaxs(false)).fraction < 1.0f;
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

// Inferno (v0.29, owner: CS2's layout as accurately as possible): every bot spot standable and reachable, real bot
// trips, spawns out of each other's sight, no holes, and the lanes' run times against CS2's.
void testInferno() {
    std::printf("inferno\n");
    setTownMap(2);
    const World w = buildTown();
    NavGrid nav;
    nav.build(townGrid(), w, townSpawn().pos);
    {  // the spots bots check as they walk (v0.31)
        const size_t hiding = nav.hidingSpots(w, 160.0f, {}).size();
        std::printf("  %zu corners and spots behind props for bots to check\n", hiding);
        CHECK(hiding > 30, "%zu", hiding);
    }
    if (std::ifstream("CRISP_DUMP").good()) {  // (debugging: the nav grid, one character a cell, north up)
        const MapGrid& g = townGrid();
        std::ofstream f("inferno_nav.txt");
        f << g.x0 << " " << g.y0 << " " << g.w << " " << g.h << "\n";
        for (int j = g.h - 1; j >= 0; --j) {
            for (int i = 0; i < g.w; ++i) {
                const Vec3 c = g.center(i, j);
                const bool ramp = g.slope.size() == g.floor.size() && g.slope[size_t(g.index(i, j))];
                f << (!g.walkable(i, j) ? '#' : nav.roamable(c) ? (ramp ? ',' : '.') : nav.standable(c) ? 'x' : 'o');
            }
            f << "\n";
        }
    }
    std::printf("  %zu boxes, %s\n", w.solids.size(), townMapName(2));
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
    for (const TownWay& wy : tac.ways) spots.push_back({wy.name, townPoint(wy.stage[0], wy.stage[1])});
    for (const auto& m : tac.mid) spots.push_back({"mid control", townPoint(m[0], m[1])});
    for (const RetakeSpot& h : tac.pushes) spots.push_back({"push", P(h)});
    int bad = 0, unreachable = 0;
    std::vector<Vec3> path;
    for (const Spot& sp : spots) {
        // (+6: on a ramp the grid's floor height is a few units under the slope's surface; spawning drops you onto it)
        const bool clear = sp.p.z > MapGrid::kNoFloor && w.boxFits(sp.p + Vec3{0, 0, 6.0f}, hullMins(), hullMaxs(false)) &&
                           w.traceBox(sp.p + Vec3{0, 0, 6.0f}, sp.p - Vec3{0, 0, 24}, hullMins(), hullMaxs(false)).fraction < 1.0f;
        const bool route = nav.findPath(townTeamSpawns(0)[0], sp.p, path);
        if (!clear) std::printf("    %s (%.0f, %.0f) is not standing room\n", sp.what, double(sp.p.x), double(sp.p.y));
        if (!route) std::printf("    %s (%.0f, %.0f) has no route from T spawn\n", sp.what, double(sp.p.x), double(sp.p.y));
        bad += !clear;
        unreachable += !route;
    }
    std::printf("  %zu bot spots: %d not standing room, %d unreachable\n", spots.size(), bad, unreachable);
    CHECK(bad == 0 && unreachable == 0, "inferno spots: %d bad, %d unreachable", bad, unreachable);

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
    CHECK(failed == 0, "%d inferno trips failed", failed);

    // Nobody sees the other team's spawn when the round starts.
    int seen = 0;
    for (const Vec3& a : townTeamSpawns(0))
        for (const Vec3& b : townTeamSpawns(1))
            seen += w.traceRay(a + Vec3{0, 0, kStandEye}, b + Vec3{0, 0, kStandEye}).fraction >= 1.0f;
    CHECK(seen == 0, "inferno: %d spawn pairs see each other", seen);

    // No holes.
    int escapes = 0, total = 0;
    for (int k = 0; k < 400; ++k) {
        const Vec3 eye = nav.roamPoint((float(k) + 0.5f) / 400.0f, false) + Vec3{0, 0, kStandEye};
        for (int yi = 0; yi < 36; ++yi)
            for (float pitch : {0.0f, 2.0f, 5.0f, 15.0f, 45.0f}) {
                ++total;
                const bool out = w.traceRay(eye, eye + anglesToForward(pitch, float(yi) * 10.0f) * 12000.0f).fraction >= 1.0f;
                if (out)
                    std::printf("    a ray from (%.0f, %.0f, %.0f) %s, yaw %d pitch %.0f, got out\n", double(eye.x), double(eye.y),
                                double(eye.z), townCallout(eye), yi * 10, double(pitch));
                escapes += out;
            }
    }
    std::printf("  %d rays, %d got out of the map; spawns that see each other: %d\n", total, escapes, seen);
    CHECK(escapes == 0, "inferno: %d rays escaped", escapes);

    // Every floor cell has floor under it (no hole to fall through).
    {
        const MapGrid& g = townGrid();
        int holes = 0;
        for (int j = 0; j < g.h; ++j)
            for (int i = 0; i < g.w; ++i) {
                if (!g.walkable(i, j)) continue;
                const Vec3 c = g.center(i, j);
                if (w.traceRay(c + Vec3{0, 0, 30}, c - Vec3{0, 0, 30}).fraction >= 1.0f) {
                    if (holes < 8) std::printf("    no floor under (%.0f, %.0f, %.0f) %s\n", double(c.x), double(c.y), double(c.z), townCallout(c));
                    ++holes;
                }
            }
        std::printf("  %d floor cells with nothing under them\n", holes);
        CHECK(holes == 0, "inferno: %d holes in the floor", holes);
        // Clean (owner, v0.29: the generated map was all spikes and gaps): no floor cell squeezed between walls on
        // opposite sides (a 16-unit sliver nobody fits through), no lone cell standing apart from all its neighbours,
        // and the small steps between level floors counted (a few are real: kerbs, a step up by the car).
        int slivers = 0, lone = 0, small = 0;
        auto wall = [&](int a, int b) { return !g.walkable(a, b); };
        for (int j = 1; j + 1 < g.h; ++j)
            for (int i = 1; i + 1 < g.w; ++i) {
                if (!g.walkable(i, j)) continue;
                const float z = g.floor[size_t(g.index(i, j))];
                if ((wall(i - 1, j) && wall(i + 1, j)) || (wall(i, j - 1) && wall(i, j + 1))) {
                    if (slivers < 6) std::printf("    a sliver at (%.0f, %.0f) %s\n", double(g.center(i, j).x), double(g.center(i, j).y), townCallout(g.center(i, j)));
                    ++slivers;
                }
                int near = 0;
                for (int k = 0; k < 4; ++k) {
                    const int a = i + (k == 0) - (k == 1), b = j + (k == 2) - (k == 3);
                    if (!g.walkable(a, b)) continue;
                    const float dz = std::fabs(g.floor[size_t(g.index(a, b))] - z);
                    near += dz <= 18.0f;
                    const bool ramps = g.slope[size_t(g.index(i, j))] || g.slope[size_t(g.index(a, b))];
                    if (k % 2 == 0 && !ramps && dz > 1.0f && dz < 12.0f) ++small;
                }
                if (near == 0) {
                    if (lone < 6) std::printf("    a lone cell at (%.0f, %.0f) %s\n", double(g.center(i, j).x), double(g.center(i, j).y), townCallout(g.center(i, j)));
                    ++lone;
                }
            }
        std::printf("  %d slivers, %d lone cells, %d small steps between level floors\n", slivers, lone, small);
        CHECK(slivers == 0 && lone == 0, "inferno: %d slivers, %d lone cells", slivers, lone);
    }

    // The lanes at knife speed.
    struct Leg { const char* a; Vec3 from; const char* b; Vec3 to; float minS, maxS; };
    const RetakeSite &sa = townRetakeSites()[0], &sb = townRetakeSites()[1];
    const Vec3 tSp = townSpawn().pos, ctSp = townTeamSpawns(1)[0], aSite = townPoint(sa.bombX, sa.bombY),
               bSite = townPoint(sb.bombX, sb.bombY), banana = townPoint(tac.ways[2].stage[0], tac.ways[2].stage[1]),
               topMid = P(townCtSpots(kCtMid)[0]), apps = P(tac.pushes[0]);
    std::printf("  T spawn start (%.0f, %.0f, %.0f) standable %d roamable %d\n", double(tSp.x), double(tSp.y), double(tSp.z),
                int(nav.standable(tSp)), int(nav.roamable(tSp)));
    const Leg legs[] = {{"T spawn", tSp, "A site", aSite, 12, 26},  {"T spawn", tSp, "B site", bSite, 10, 24},
                        {"T spawn", tSp, "banana", banana, 4, 14},  {"T spawn", tSp, "top mid", topMid, 6, 18},
                        {"T spawn", tSp, "close apps", apps, 7, 20}, {"CT spawn", ctSp, "A site", aSite, 2, 10},
                        {"CT spawn", ctSp, "B site", bSite, 4, 14},  {"CT spawn", ctSp, "top mid", topMid, 3, 15},
                        // the player's walks through the tight bits: into T apps, out of close apps over the balcony
                        // (a drop) onto A, through the underpass, to construction and library
                        {"T spawn", tSp, "T apps", townPoint(-200, 320), 4, 16},
                        {"close apps", apps, "A site", aSite, 2, 10},
                        {"bottom mid", townPoint(120, 640), "second mid", townPoint(520, -64), 1, 8},
                        {"CT spawn", ctSp, "construction", townPoint(1000, 3300), 4, 14},
                        {"CT spawn", ctSp, "library", townPoint(2400, 1400), 1, 8}};
    for (const Leg& l : legs) {
        const bool found = nav.findPath(l.from, l.to, path);
        float dist = 0, secs = found ? runRoute(path, 250.0f, &dist, &w) : -1.0f;
        std::printf("  %-8s -> %-12s %5.1f s  (%4.0f units)\n", l.a, l.b, double(secs), double(dist));
        if (found && secs > l.maxS)
            std::printf("    stalled %.1f s at (%.0f, %.0f, %.0f) %s\n", double(g_stallTicks) * kTickDt, double(g_stallAt.x),
                        double(g_stallAt.y), double(g_stallAt.z), townCallout(g_stallAt));
        if (found && secs < 0) {
            std::printf("    stuck at (%.0f, %.0f, %.0f) %s\n", double(g_stuckAt.x), double(g_stuckAt.y), double(g_stuckAt.z),
                        townCallout(g_stuckAt));
            for (size_t q = 0; q < path.size(); ++q)  // (debugging: the route)
                if (length2d(path[q] - g_stuckAt) < 160.0f)
                    std::printf("      waypoint %zu (%.0f, %.0f, %.0f) %s\n", q, double(path[q].x), double(path[q].y), double(path[q].z),
                                townCallout(path[q]));
            for (const Box& b : w.solids)  // (debugging: what's in the way)
                if (b.mins.x < g_stuckAt.x + 30 && b.maxs.x > g_stuckAt.x - 30 && b.mins.y < g_stuckAt.y + 30 &&
                    b.maxs.y > g_stuckAt.y - 30 && b.maxs.z > g_stuckAt.z - 40 && b.mins.z < g_stuckAt.z + 72)
                    std::printf("      box (%.0f %.0f %.0f)-(%.0f %.0f %.0f) slope %d low %.0f\n", double(b.mins.x), double(b.mins.y),
                                double(b.mins.z), double(b.maxs.x), double(b.maxs.y), double(b.maxs.z), int(b.slope), double(b.lowZ));
        }
        CHECK(found && secs >= l.minS && secs <= l.maxS, "inferno %s -> %s: %.1f s", l.a, l.b, double(secs));
        // ... and never caught on anything on the way (a snag on a corner, a step, a doorway)
        if (found && secs > 0 && g_stallTicks > kTickRate * 3 / 4)
            std::printf("    caught %.2f s at (%.0f, %.0f, %.0f) %s\n", double(g_stallTicks) * kTickDt, double(g_stallAt.x),
                        double(g_stallAt.y), double(g_stallAt.z), townCallout(g_stallAt));
        CHECK(!found || secs < 0 || g_stallTicks <= kTickRate * 3 / 4, "inferno %s -> %s: caught on the way", l.a, l.b);
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
    testKnife();
    testCases();


    testCareer();
    testDustSightlines();
    testBotArsenal();
    testBotFire();
    testSpawnsOffMid();
    testCompWays();
    testBotSpacing();
    testRetakeSpread();
    testBotPreaim();
    testBotAim();
    testBotPeek();
    testBotChecks();
    testSpawnGroups();
    testConsole();
    testHarbor();
    testInferno();
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
