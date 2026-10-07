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

void testRayVsBoxes() {
    std::printf("ray traces\n");
    TraceResult tr = lab().traceRay({0, 0, 64}, {4000, 0, 64});
    float hitX = tr.endpos.x;
    std::printf("  ray down the range hits far wall at x %.2f\n", hitX);
    CHECK(tr.fraction < 1 && hitX > 2559 && hitX < 2560.01f, "x %.2f", hitX);
}

// ---- Dust (Dust2 at a scale of the real map; 60% by default) ----

const World& dust() {
    static World w = buildDust();
    return w;
}

// The bots' navigation grid for Dust (the same code deathmatch bots use to find their way).
const NavGrid& dustNav() {
    static const NavGrid nav = [] {
        NavGrid n;
        n.build(dustGrid(), dust(), dustSpawn().pos);
        return n;
    }();
    return nav;
}

bool dustRoute(Vec3 from, Vec3 to, std::vector<Vec3>& out) { return dustNav().findPath(from, to, out); }

// A point given in real-Dust2 coordinates, at the map's current scale, on the floor.
Vec3 dpt(float x, float y) {
    float s = dustScale();
    return {x * s, y * s, dustGrid().floorAt(x * s, y * s)};
}

// Runs a simulated player along `path` (holding W, steering at a point a little ahead) and returns
// the time taken, or -1 if they got stuck.
float runRoute(const std::vector<Vec3>& path, float speed, float* distance) {
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
        playerMove(ps, in, std::atan2(aim.y, aim.x) / kDegToRad, speed, dust());
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

    MapSpawn sp = dustSpawn();
    CHECK(w.boxFits(sp.pos + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false)), "T spawn is clear");
    CHECK(std::string(dustCallout(sp.pos)) == "T SPAWN", "callout %s", dustCallout(sp.pos));

    // Every bot spot is standing room on solid floor.
    for (const PeekSpot& s : dustPeekSpots())
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
        badSpawns += !(nav.roamable(p) && dust().boxFits(p + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false)));
        spawnAreas.insert(dustCallout(p));
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
            seen[size_t(i)].insert(dustCallout(d.pos));
            all.insert(dustCallout(d.pos));
            worstSink = std::max(worstSink, dustGrid().floorAt(d.pos.x, d.pos.y) - d.pos.z);
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
    bool routed = dustNav().findPath(dpt(-1700, 2300), dpt(1750, 420), path);
    CHECK(routed, "bot route");
    if (!routed) return;
    Vec3 pos = path.front();
    size_t next = 1;
    int ticks = 0;
    float worstZ = 0;
    while (!followPath(pos, path, next, 215.0f * kTickDt) && ticks < kTickRate * 60) {
        ++ticks;
        worstZ = std::max(worstZ, std::fabs(pos.z - dustGrid().floorAt(pos.x, pos.y)));
    }
    std::printf("  bot B site -> pit: %.1f s at rifle speed, feet at most %.1f units off the floor\n",
                double(ticks) * kTickDt, double(worstZ));
    CHECK(next >= path.size() && worstZ <= 16.5f, "arrived %d, worst z %.1f", int(next >= path.size()), double(worstZ));
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

// Walk the main routes with a simulated player at knife speed (250 u/s) and print the run times.
void testDustRoutes() {
    std::printf("dust routes (knife, 250 u/s)\n");
    const Landmark tSpawn{"T spawn", dustSpawn().pos}, ctSpawn{"CT spawn", dpt(-150, 2750)},
        longDoors{"long doors", dpt(775, 380)}, aSite{"A site", dpt(1300, 2900)}, bSite{"B site", dpt(-1850, 2400)},
        midDoors{"mid doors", dpt(-176, 1896)}, cat{"catwalk", dpt(300, 1700)}, pit{"pit", dpt(1700, 350)},
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
        const float sc = dustScale();  // the windows are for real-size Dust2; the map can be smaller
        CHECK(secs >= rt.minS * sc && secs <= rt.maxS * sc, "%s -> %s took %.1f s (stuck = -1)", rt.a.name, rt.b.name,
              double(secs));
    }
}

// Every Dust size the menu offers still has all its routes and valid bot spots.
void testDustScales() {
    std::printf("dust sizes\n");
    const float keep = dustScale();
    for (float sc : {0.5f, 0.6f, 0.75f, 1.0f}) {
        setDustScale(sc);
        World w = buildDust();
        NavGrid nav;
        nav.build(dustGrid(), w, dustSpawn().pos);
        const float pts[][2] = {{-150, 2750}, {775, 380}, {1300, 2900}, {-1850, 2400}, {-176, 1896},
                                {300, 1700}, {1750, 420}, {-1100, 1150}};
        std::vector<Vec3> path;
        int missing = 0, badSpots = 0;
        for (const auto& pt : pts) missing += !nav.findPath(dustSpawn().pos, dpt(pt[0], pt[1]), path);
        for (const PeekSpot& sp : dustPeekSpots())
            for (Vec3 p : {sp.cover, sp.peek})
                badSpots += !(p.z > MapGrid::kNoFloor && w.boxFits(p + Vec3{0, 0, 0.5f}, hullMins(), hullMaxs(false)));
        // Retakes: every hold spot and entry is standing room, and every entry can walk onto its site.
        for (const RetakeSite& site : dustRetakeSites()) {
            for (const RetakeSpot& h : site.holds)
                if (!nav.standable(dustPoint(h.x, h.y))) {
                    std::printf("    %s hold (%.0f, %.0f) is not standing room\n", site.name, double(h.x), double(h.y));
                    ++badSpots;
                }
            for (const RetakeSpot& e : site.entries) {
                if (!nav.standable(dustPoint(e.x, e.y))) {
                    std::printf("    %s entry (%.0f, %.0f) is not standing room\n", site.name, double(e.x), double(e.y));
                    ++badSpots;
                } else if (!std::any_of(site.holds.begin(), site.holds.end(), [&](const RetakeSpot& h) {
                               return nav.findPath(dustPoint(e.x, e.y), dustPoint(h.x, h.y), path);
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
    testWallbang();
    testRayVsBoxes();
    testDustMap();
    testDustBroadphase();
    testDustRoutes();
    testDustDeathmatch();
    testTurnedHitboxes();
    testDustScales();
    if (g_failures) {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
