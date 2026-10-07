// Feel Lab: single-player movement + shooting prototype.
// Fixed 128 Hz simulation, uncapped rendering with interpolation, raw mouse input.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include "audio.h"
#include "bots.h"
#include "combat.h"
#include "config.h"
#include "fx.h"
#include "gl.h"
#include "movement.h"
#include "nav.h"
#include "render.h"
#include "world.h"

#if defined(_WIN32)
// Ask laptop drivers to run us on the discrete GPU (if there is one).
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

struct Options {
    std::string screenshotPath;  // --screenshot out.bmp : save a frame and quit
    int screenshotFrame = 300;   // --frames N
    bool spawnOverride = false;  // --spawn x y yaw
    float spawnX = 0, spawnY = 0, spawnYaw = 0;
    float autofireStart = -1, autofireEnd = -1;  // --autofire start end (sim seconds)
    int windowW = 0, windowH = 0;                // --windowed W H
    int startWeapon = 0;                         // --weapon 1|2|3|4 (4 = sniper as primary)
    int startZoom = 0;                           // --zoom 1|2 (sniper scope, for screenshots)
    bool showMenu = false;                       // --menu (settings menu, for screenshots)
    bool throwSmoke = false, bots = false;       // --smoke, --bots (for screenshots)
    float benchSeconds = 0;                      // --bench S: timed run at real speed, writes bench.txt
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (a == "--screenshot") o.screenshotPath = next();
        else if (a == "--frames") o.screenshotFrame = std::atoi(next());
        else if (a == "--spawn") {
            o.spawnOverride = true;
            o.spawnX = float(std::atof(next()));
            o.spawnY = float(std::atof(next()));
            o.spawnYaw = float(std::atof(next()));
        } else if (a == "--autofire") {
            o.autofireStart = float(std::atof(next()));
            o.autofireEnd = float(std::atof(next()));
        } else if (a == "--smoke") {
            o.throwSmoke = true;
        } else if (a == "--bots") {
            o.bots = true;
        } else if (a == "--bench") {
            o.benchSeconds = float(std::atof(next()));
        } else if (a == "--menu") {
            o.showMenu = true;
        } else if (a == "--zoom") {
            o.startZoom = std::atoi(next());
        } else if (a == "--weapon") {
            o.startWeapon = std::atoi(next());
        } else if (a == "--windowed") {
            o.windowW = std::atoi(next());
            o.windowH = std::atoi(next());
        }
    }
    return o;
}

struct HitLogEntry {
    std::string text;
    uint32_t color;
    double time;
};

struct Game {
    World world;
    MoveParams moveParams;
    PlayerState player, prevPlayer;
    WeaponState rifle, pistol, sniper, knife, grenade;
    // CS-style slots: 1 = primary (rifle or sniper, picked in the buy menu), 2 pistol, 3 knife,
    // 4 smoke. Q = previous weapon.
    WeaponState* primary = &rifle;
    WeaponState* lastWeapon = &knife;
    bool buyMenu = false;
    bool throwLob = false;        // right click with the smoke out: short underhand throw
    double grenadeReturnAt = -1;  // after a throw, switch back to the previous weapon
    // Sniper scope: 0 = unscoped, 1 = 40 FOV, 2 = 15 FOV. Unscopes on shot, re-scopes after the bolt.
    int zoom = 0, resumeZoom = 0;
    double resumeZoomAt = -1, boltAt = -1;
    bool zoomLatch = false;
    bool autoHop = true;
    Vec3 spawn;
    float spawnYaw = 0;
    bool noclip = false;
    WeaponState* weapon = &rifle;
    std::vector<Dummy> dummies;

    double simTime = 0;
    double viewYaw = 0, viewPitch = 0;  // degrees; updated per mouse event (never waits for a tick)
    float recoilIndexPrev = 0;

    // Input latches so quick taps between ticks are never lost.
    bool fireHeld = false, fireLatch = false, jumpLatch = false, reloadLatch = false;
    int switchTo = 0;

    // What was on screen last frame: shots are tested against exactly this (what you see is what you hit).
    Vec3 lastRenderEye;
    std::vector<Vec3> lastDummyRenderPos;

    // Counter-strafe meter.
    enum class StopState { Idle, Running, Stopping } stopState = StopState::Idle;
    bool stopIsCounter = false;
    double stopStart = 0;
    float lastStopMs = -1;
    bool lastStopCounter = false;

    // Stats & feedback.
    int shots = 0, hits = 0, headshots = 0;
    std::deque<HitLogEntry> hitLog;
    double hitMarkerUntil = 0, hitMarkerStart = 0;
    bool hitMarkerHead = false, hitMarkerKill = false;
    bool hitSound = true, showHitMarker = true;
    std::vector<BoxInstance> pendingDecals;
    bool hudDirty = true;

    // Cosmetics: sound, first-person weapon, particles. Never affect the simulation.
    Audio* audio = nullptr;
    ViewModel vm;
    Effects fx;
    int reloadStage = 0;
    float stepDist = 0;
    bool stepLeft = false;
    bool drill = false;
    // Aim drill stats: time from a dummy (re)appearing to you killing it.
    std::vector<double> aliveSince;
    int drillKills = 0;
    double drillTtkSum = 0, lastTtk = -1;

    // Bots shoot back (F4). They aim at where you were 0.2 s ago: move and they miss.
    bool botsFire = false;
    float hp = 100;
    int deaths = 0;
    double hurtUntil = 0, deadUntil = -1;
    std::vector<float> botSeen, botCooldown;
    Vec3 eyeHistory[64];
    int histHead = 0;

    // Smoke grenades (G). Deterministic bounces, so lineups repeat exactly.
    struct Nade { Vec3 pos, vel; double detonateAt; };
    struct Smoke { Vec3 pos; double start; };
    std::vector<Nade> nades;
    std::vector<Smoke> smokes;
    bool throwLatch = false;

    // Map + bot AI (Dust: hide behind cover, peek, hold, return).
    int mapId = 0;
    uint32_t rng = 0x9E3779B9u;
    std::vector<int> botState, botSpot;  // state: 0 hidden, 1 peeking out, 2 holding, 3 returning
    std::vector<float> botTimer, botReact;

    // Deathmatch (mode 1, Dust only): bots roam the map on the nav grid and respawn around it.
    int mode = 0, dmBots = 6, dmMinutes = 5;
    NavGrid nav;
    std::vector<BotBrain> bots;
    // Retakes (mode 2, Dust only): bots hold a random site, you clear it from a random entry.
    int rtBots = 4, rtSite = 0, rtWon = 0, rtLost = 0;
    double rtRoundEnd = 0, rtResultUntil = -1;
    bool rtResultWin = false;
    const char* rtResultText = "";
    double dmEnd = 0, dmOverUntil = -1, spawnProtectUntil = 0;
    int dmKills = 0, dmDeaths = 0, dmShownSecs = -1;
    Vec3 noisePos;                // last sound you made (footsteps, shots) that bots can hear
    double noiseAt = -100;
    float noiseRadius = 0;
    bool showScores = false;      // Tab held

    // Cosmetic: camera height offset that eases the view over stairs and stepped ramps.
    float stepSmooth = 0;
    float camRoll = 0;        // spray feedback: camera roll in degrees (around the crosshair)
    bool viewShake = true;
    const char* callout = "";  // Dust area name under the player (HUD)

    // KZ course timer.
    int kzState = 0;  // 0 idle, 1 on start pad, 2 running
    double kzStart = 0, kzLast = -1, kzBest = -1;
};

constexpr double kSmokeLife = 15.0;
constexpr float kSmokeRadius = 140.0f;

float smokeGrow(const Game::Smoke& s, double now) {
    double age = now - s.start;
    if (age < 0 || age > kSmokeLife) return 0.0f;
    float grow = float(std::min(1.0, age / 0.6));
    float fade = float(std::clamp((kSmokeLife - age) / 1.5, 0.0, 1.0));
    return grow * fade;
}

// True if the segment a->b passes through any active smoke cloud.
bool smokeBlocks(const Game& g, const Vec3& a, const Vec3& b) {
    for (const Game::Smoke& s : g.smokes) {
        float r = kSmokeRadius * smokeGrow(s, g.simTime);
        if (r < 40.0f) continue;
        Vec3 c = s.pos + Vec3{0, 0, 60}, ab = b - a;
        float t = std::clamp(dot(c - a, ab) / std::max(dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        Vec3 p = a + ab * t;
        if (dot(c - p, c - p) < r * r) return true;
    }
    return false;
}

bool inRect(const Vec3& p, float x0, float x1, float y0, float y1) {
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
}

float rnd(Game& g) {  // bots only: the player's shots stay deterministic
    g.rng ^= g.rng << 13; g.rng ^= g.rng >> 17; g.rng ^= g.rng << 5;
    return float(g.rng & 0xFFFFFF) / float(0x1000000);
}

ViewWeapon viewWeaponOf(const Game& g) {
    return g.weapon == &g.rifle     ? ViewWeapon::Rifle
           : g.weapon == &g.pistol  ? ViewWeapon::Pistol
           : g.weapon == &g.sniper  ? ViewWeapon::Sniper
           : g.weapon == &g.grenade ? ViewWeapon::Grenade
                                    : ViewWeapon::Knife;
}

float wrapDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}
float yawTo(const Vec3& from, const Vec3& to) { return std::atan2(to.y - from.y, to.x - from.x) / kDegToRad; }
// Turns `yaw` toward `target` by at most `maxStep` degrees.
float turnToward(float yaw, float target, float maxStep) {
    return wrapDeg(yaw + std::clamp(wrapDeg(target - yaw), -maxStep, maxStep));
}

// Something bots can hear: footsteps (~1100 units) or gunshots (~2200).
void makeNoise(Game& g, const Vec3& pos, float radius) {
    if (g.noiseAt == g.simTime && g.noiseRadius >= radius) return;
    g.noisePos = pos;
    g.noiseAt = g.simTime;
    g.noiseRadius = radius;
}

// Respawn: full magazines, no reload in progress, recoil reset, unscoped.
void refillAmmo(Game& g) {
    for (WeaponState* w : {&g.rifle, &g.pistol, &g.sniper}) {
        w->ammo = w->def->magSize;
        w->reloadEndTime = -1;
        w->recoilIndex = 0;
    }
    g.zoom = 0;
    g.resumeZoomAt = -1;
}

float zoomFov(int level, float baseFov) { return level == 1 ? 40.0f : level == 2 ? 15.0f : baseFov; }

// Scales mouse sensitivity so a scoped flick covers the same screen distance per count, like CS.
float zoomSensScale(const Game& g, const Config& cfg) {
    if (g.zoom == 0) return 1.0f;
    float a = std::tan(zoomFov(g.zoom, cfg.fov) * 0.5f * kDegToRad), b = std::tan(cfg.fov * 0.5f * kDegToRad);
    return a / b * cfg.zoom_sensitivity_ratio;
}

void applyConfig(Game& g, const Config& cfg) {
    g.moveParams = MoveParams{};
    g.dmBots = std::clamp(cfg.dm_bots, 1, 16);       // takes effect at the next match
    g.dmMinutes = std::clamp(cfg.dm_minutes, 1, 60);
    g.rtBots = std::clamp(cfg.rt_bots, 1, 6);  // next round
    g.viewShake = cfg.view_shake != 0;
    g.hitSound = cfg.hitsound != 0;
    g.showHitMarker = cfg.hitmarker != 0;
    g.autoHop = cfg.bhop != 0;
    if (g.autoHop) {
        // Bunny hopping: no stamina slowdown on jump/land; air-strafing keeps and builds speed.
        g.moveParams.staminaJumpCost = 0;
        g.moveParams.staminaLandCost = 0;
    }
    for (WeaponState* w : {&g.rifle, &g.pistol, &g.sniper}) {
        w->spraySpread = cfg.spread_spray != 0;
        w->moveSpread = cfg.spread_movement != 0;
    }
    if (g.audio) g.audio->setVolume(std::clamp(cfg.volume, 0.0f, 1.0f));
}

void resetPosition(Game& g) {
    g.player = {};
    g.player.origin = g.spawn;
    g.player.onGround = true;
    g.prevPlayer = g.player;
    g.viewYaw = g.spawnYaw;
    g.viewPitch = 0;
    g.stepSmooth = 0;
}

constexpr float kStepSpeed = 135.0f;   // at or below this you are silent (shift-walk is ~112)
constexpr float kStepStride = 76.0f;   // units between footsteps

void sound(Game& g, Sfx s, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f) {
    if (g.audio) g.audio->play(s, gain, pan, pitch);
}

void setDrill(Game& g, bool on) {
    g.drill = on;
    for (size_t i = 0; i < 4 && i < g.dummies.size(); ++i) {  // the four static range dummies
        g.dummies[i].randomRespawn = on;
        g.dummies[i].areaMin = {400, -260, 0};
        g.dummies[i].areaMax = {2200, 260, 0};
    }
}

// Deathmatch spawn anywhere on the map. For you: away from the bots and out of their sight. For a bot:
// out of your sight and not on top of another bot.
Vec3 pickDmSpawn(Game& g, bool forPlayer, size_t self = SIZE_MAX) {
    std::vector<Vec3> watchers, occupied;
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        const Dummy& d = g.dummies[i];
        if (!d.alive() || i == self || g.bots[i].state < 0) continue;
        (forPlayer ? watchers : occupied).push_back(forPlayer ? d.pos + Vec3{0, 0, 64} : d.pos);
    }
    if (!forPlayer) watchers.push_back(g.player.origin + Vec3{0, 0, kStandEye});
    return randomSpawnPoint(g.nav, g.world, watchers, forPlayer ? 900.0f : 700.0f, occupied, g.rng);
}

// (Re)starts a deathmatch: fresh scores, everyone respawns.
void startDeathmatch(Game& g) {
    g.dmEnd = g.simTime + 60.0 * g.dmMinutes;
    g.dmOverUntil = -1;
    g.dmKills = g.dmDeaths = 0;
    g.shots = g.hits = g.headshots = 0;
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        g.dummies[i] = Dummy{};
        g.dummies[i].respawnLeft = 0.01f;  // they spawn on the first tick, after you
        g.bots[i] = BotBrain{};
    }
    g.spawn = pickDmSpawn(g, true);
    g.spawnYaw = rnd(g) * 360.0f - 180.0f;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    g.spawnProtectUntil = g.simTime + 1.0;
    g.hudDirty = true;
}

constexpr double kRetakeRoundTime = 40.0;
void pushHitLog(Game& g, const std::string& text, uint32_t color);

// A new retake round: random site, its bots on random hold spots facing the way you'll come, you at
// a random entry with full ammo and HP.
void startRetakeRound(Game& g) {
    const std::vector<RetakeSite>& sites = dustRetakeSites();
    g.rtSite = int(rnd(g) * float(sites.size())) % int(sites.size());
    const RetakeSite& site = sites[size_t(g.rtSite)];
    const RetakeSpot& entry = site.entries[size_t(rnd(g) * float(site.entries.size())) % site.entries.size()];
    g.spawn = dustPoint(entry.x, entry.y);
    g.spawnYaw = std::atan2(entry.lookY - entry.y, entry.lookX - entry.x) / kDegToRad;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    std::vector<size_t> order(site.holds.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    for (size_t k = order.size(); k > 1; --k) std::swap(order[k - 1], order[size_t(rnd(g) * float(k)) % k]);
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        const RetakeSpot& h = site.holds[order[i % order.size()]];
        Dummy& d = g.dummies[i];
        d = Dummy{};
        d.pos = d.prevPos = dustPoint(h.x, h.y);
        d.yaw = d.prevYaw = std::atan2(h.lookY - h.y, h.lookX - h.x) / kDegToRad;
        BotBrain b;
        b.state = 1;  // already placed: hold
        b.holdOnly = true;
        b.holdYaw = d.yaw;
        g.bots[i] = b;
    }
    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
    g.rtRoundEnd = g.simTime + kRetakeRoundTime;
    g.rtResultUntil = -1;
    g.dmShownSecs = -1;
    char msg[48];
    std::snprintf(msg, sizeof(msg), "RETAKE %s", site.name);
    pushHitLog(g, msg, 0x80ff80);
    g.hudDirty = true;
}

void endRetakeRound(Game& g, bool won, const char* why) {
    if (g.rtResultUntil >= 0) return;
    (won ? g.rtWon : g.rtLost)++;
    g.rtResultWin = won;
    g.rtResultText = why;
    g.rtResultUntil = g.simTime + 3.0;
    g.hudDirty = true;
}

void loadMap(Game& g, Renderer& r, int id) {
    g.mapId = id;
    g.world = id == 1 ? buildDust() : buildFeelLab();
    if (id == 1) {
        g.dummies.assign(g.mode == 1 ? size_t(g.dmBots) : g.mode == 2 ? size_t(g.rtBots) : 4, Dummy{});
        for (Dummy& d : g.dummies) d.respawnLeft = 0.01f;  // spawn at a spot on the first tick
        g.spawn = dustSpawn().pos;
        g.spawnYaw = dustSpawn().yaw;
        g.botsFire = true;
    } else {
        g.dummies = buildDummies();
        g.spawn = {0, 0, 0};
        g.spawnYaw = 0;
    }
    size_t n = g.dummies.size();
    g.lastDummyRenderPos.assign(n, Vec3{});
    for (size_t i = 0; i < n; ++i) g.lastDummyRenderPos[i] = g.dummies[i].pos;
    g.aliveSince.assign(n, 0.0);
    g.botSeen.assign(n, 0.0f);
    g.botCooldown.assign(n, 0.0f);
    g.botReact.assign(n, 0.4f);
    g.botTimer.assign(n, 0.0f);
    g.botState.assign(n, -1);
    g.botSpot.assign(n, -1);
    g.bots.assign(n, BotBrain{});
    g.drill = false;
    g.kzState = 0;
    g.nades.clear();
    g.smokes.clear();
    g.hp = 100;
    resetPosition(g);
    std::vector<BoxInstance> statics;
    for (const Box& b : g.world.solids) statics.push_back(makeBox(b.mins, b.maxs, b.color, true));
    r.setStaticBoxes(statics);
    r.clearDecals();
    if (id == 1) g.nav.build(dustGrid(), g.world, dustSpawn().pos);
    if (id == 1)  // particles land on Dust's floors (which aren't all at height 0)
        g.fx.setGround([](float x, float y) {
            float z = dustGrid().floorAt(x, y);
            return z > MapGrid::kNoFloor ? z : -1e9f;
        });
    else
        g.fx.setGround(nullptr);
    if (id == 1 && g.mode == 1) startDeathmatch(g);
    if (id == 1 && g.mode == 2) {
        g.rtWon = g.rtLost = 0;
        startRetakeRound(g);
    }
    g.hudDirty = true;
}

void resetGame(Game& g, const Options& opt) {
    g.world = buildFeelLab();
    g.dummies = buildDummies();
    g.player = {};
    g.player.origin = opt.spawnOverride ? Vec3{opt.spawnX, opt.spawnY, 0} : Vec3{0, 0, 0};
    g.player.onGround = true;
    g.prevPlayer = g.player;
    g.viewYaw = opt.spawnOverride ? opt.spawnYaw : 0;
    g.rifle.def = &rifleDef();
    g.rifle.ammo = rifleDef().magSize;
    g.knife.def = &knifeDef();
    g.pistol.def = &pistolDef();
    g.pistol.ammo = pistolDef().magSize;
    g.sniper.def = &sniperDef();
    g.sniper.ammo = sniperDef().magSize;
    g.grenade.def = &grenadeDef();
    g.spawn = g.player.origin;
    g.spawnYaw = float(g.viewYaw);
    g.aliveSince.assign(g.dummies.size(), 0.0);
    g.botSeen.assign(g.dummies.size(), 0.0f);
    g.botCooldown.assign(g.dummies.size(), 0.0f);
    if (opt.startWeapon == 4) g.primary = &g.sniper;
    g.switchTo = opt.startWeapon == 4 ? 1 : opt.startWeapon;
    g.weapon = &g.rifle;
    g.lastRenderEye = g.player.origin + Vec3{0, 0, kStandEye};
    g.lastDummyRenderPos.clear();
    for (const Dummy& d : g.dummies) g.lastDummyRenderPos.push_back(d.pos);
}

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        float x = float((a >> s) & 0xFF), y = float((b >> s) & 0xFF);
        return uint32_t(x + (y - x) * t) << s;
    };
    return ch(16) | ch(8) | ch(0);
}

void pushHitLog(Game& g, const std::string& text, uint32_t color) {
    g.hitLog.push_front({text, color, g.simTime});
    while (g.hitLog.size() > 5) g.hitLog.pop_back();
}

void simTick(Game& g, const Options& opt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    MoveInput in;
    in.forward = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
    in.side = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
    in.walk = keys[SDL_SCANCODE_LSHIFT];
    in.duck = keys[SDL_SCANCODE_LCTRL];
    in.jumpPressed = g.jumpLatch || (g.autoHop && keys[SDL_SCANCODE_SPACE]);
    g.jumpLatch = false;

    // Weapon switching / reload.
    if (g.switchTo) {
        WeaponState* target = g.switchTo == 1   ? g.primary
                              : g.switchTo == 2 ? &g.pistol
                              : g.switchTo == 4 ? &g.grenade
                              : g.switchTo == 5 ? g.lastWeapon  // Q
                                                : &g.knife;
        g.grenadeReturnAt = -1;
        g.zoom = 0;
        g.resumeZoomAt = -1;
        if (target != g.weapon) {
            g.weapon->reloadEndTime = -1;
            g.lastWeapon = g.weapon;
            g.weapon = target;
            g.weapon->nextFireTime = std::max(g.weapon->nextFireTime, g.simTime + 0.25);  // draw time
            g.vm.onDraw(viewWeaponOf(g));
            sound(g, Sfx::Draw, 0.7f);
            g.hudDirty = true;
        }
        g.switchTo = 0;
    }
    WeaponState& ws = *g.weapon;
    const WeaponDef& wd = *ws.def;
    if (g.reloadLatch && wd.canFire && ws.reloadEndTime < 0 && ws.ammo < wd.magSize) {
        ws.reloadEndTime = g.simTime + wd.reloadTime;
        g.reloadStage = 0;
        g.zoom = 0;
        g.resumeZoomAt = -1;
        g.hudDirty = true;
    }
    g.reloadLatch = false;

    // Scope (right click) and the sniper's bolt cycle.
    const bool sniperOut = &ws == &g.sniper;
    if (g.zoomLatch && sniperOut && ws.reloadEndTime < 0) {
        g.zoom = (g.zoom + 1) % 3;
        g.resumeZoomAt = -1;
        sound(g, Sfx::DryFire, 0.25f, 0.0f, 1.6f);
        g.hudDirty = true;
    }
    const bool lobLatch = g.zoomLatch && &ws == &g.grenade;
    g.zoomLatch = false;
    if (g.resumeZoomAt >= 0 && g.simTime >= g.resumeZoomAt) {
        if (sniperOut && ws.reloadEndTime < 0) g.zoom = g.resumeZoom;
        g.resumeZoomAt = -1;
        g.hudDirty = true;
    }
    if (g.boltAt >= 0 && g.simTime >= g.boltAt) {
        sound(g, Sfx::Bolt, 0.8f, 0.0f, 0.9f);
        g.boltAt = -1;
    }
    if (ws.reloadEndTime >= 0) {
        // Reload sounds keyed to the animation: mag out, mag in, bolt.
        double progress = g.simTime - (ws.reloadEndTime - wd.reloadTime);
        const double cues[3] = {0.3, 1.4, 2.0};
        const Sfx sfx[3] = {Sfx::MagOut, Sfx::MagIn, Sfx::Bolt};
        while (g.reloadStage < 3 && progress >= cues[g.reloadStage]) sound(g, sfx[g.reloadStage++], 0.8f);
    }
    if (ws.reloadEndTime >= 0 && g.simTime >= ws.reloadEndTime) {
        ws.ammo = wd.magSize;
        ws.reloadEndTime = -1;
        g.hudDirty = true;
    }

    // Smoke (slot 4): left click throws, right click lobs, then it's back to the previous weapon.
    if (&ws == &g.grenade && (g.fireLatch || lobLatch) && g.simTime >= ws.nextFireTime && g.grenadeReturnAt < 0) {
        g.throwLatch = true;
        g.throwLob = !g.fireLatch;
        g.grenadeReturnAt = g.simTime + 0.4;
        g.fireLatch = false;
    }
    if (g.grenadeReturnAt >= 0 && g.simTime >= g.grenadeReturnAt) {
        g.grenadeReturnAt = -1;
        if (g.weapon == &g.grenade) g.switchTo = 5;
    }

    bool autofire = opt.autofireStart >= 0 && g.simTime >= opt.autofireStart && g.simTime < opt.autofireEnd;
    // Semi-auto weapons fire once per click; automatic ones keep firing while held.
    bool wantFire = (wd.automatic && g.fireHeld) || g.fireLatch || autofire;
    if (g.fireLatch && wd.canFire && (ws.ammo == 0 || ws.reloadEndTime >= 0)) sound(g, Sfx::DryFire, 0.6f);
    g.fireLatch = false;
    g.recoilIndexPrev = ws.recoilIndex;

    bool fired = false;
    if (wantFire && wd.canFire && ws.reloadEndTime < 0 && ws.ammo > 0 && g.simTime >= ws.nextFireTime) {
        // Keep exact cadence: schedule from the previous shot unless we were idle.
        if (g.simTime - ws.nextFireTime > wd.fireInterval) ws.nextFireTime = g.simTime;
        ws.nextFireTime += wd.fireInterval;

        float hspeed = length2d(g.player.velocity);
        ShotResult r = fireBullet(ws, g.lastRenderEye, float(g.viewPitch), float(g.viewYaw), hspeed,
                                  g.player.onGround, g.player.ducked, g.world, g.dummies, g.lastDummyRenderPos);
        fired = true;
        ws.ammo--;
        g.shots++;
        makeNoise(g, g.player.origin, 2200.0f);

        // Cosmetics: shot sound, weapon kick, tracer, impacts.
        bool isPistol = &ws == &g.pistol;
        if (sniperOut) {
            sound(g, Sfx::SniperShot, 1.0f);
            g.boltAt = g.simTime + 0.55;  // stays scoped through the bolt cycle
        } else {
            sound(g, isPistol ? Sfx::PistolShot : Sfx::RifleShot, isPistol ? 0.8f : 0.9f);
        }
        g.vm.onShot(ws.shotCounter * 2654435761u);
        // Spray feedback (cosmetic): a camera roll that builds through the spray, and brass flying out.
        float wob = float((ws.shotCounter * 2246822519u) >> 16 & 0xFFFF) / 65535.0f - 0.5f;
        if (g.viewShake && !sniperOut)
            g.camRoll = std::clamp(g.camRoll + wob * (0.5f + 0.06f * float(std::min(r.sprayIndex, 12))), -1.5f, 1.5f);
        if (!sniperOut) {
            Vec3 fw = anglesToForward(float(g.viewPitch), float(g.viewYaw)), rt = yawToRight(float(g.viewYaw));
            g.fx.shell(g.lastRenderEye + fw * 20.0f + rt * 7.0f - Vec3{0, 0, 6},
                       rt * (110.0f + 50.0f * wob) + Vec3{0, 0, 120.0f} - fw * 25.0f + g.player.velocity);
        }
        g.fx.tracer(g.vm.muzzleWorld(g.lastRenderEye, float(g.viewPitch), float(g.viewYaw)), r.end);
        Vec3 shotDir = normalize(r.end - r.start);
        if (r.dummyIndex >= 0) {
            sound(g, r.group == kHead ? Sfx::HitHead : Sfx::HitBody, r.group == kHead ? 0.9f : 0.75f);
            g.fx.blood(r.end, shotDir);
        } else if (r.hitWorld) {
            // Far impacts are drawn bigger so you can see where a spray lands at range.
            g.fx.impact(r.end, r.normal, 0x5c6168, std::clamp(r.distance / 450.0f, 1.0f, 4.0f));
        }
        for (int k = 0; k < r.penCount; ++k) {  // wallbang: debris on both sides of each wall
            g.fx.impact(r.penEntry[k], r.penNormal[k], 0x5c6168);
            g.fx.impact(r.penExit[k], -r.penNormal[k], 0x5c6168);
        }

        if (r.dummyIndex >= 0) {
            g.hits++;
            if (r.group == kHead) g.headshots++;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s %d%s%s  %.0fM", hitGroupName(r.group), int(r.damage + 0.5f),
                          r.kill ? "  KILL" : "", r.penCount ? "  WALLBANG" : "", r.distance * 0.0254f);
            pushHitLog(g, buf, r.group == kHead ? 0xff6060 : 0xffffff);
            if (r.kill && (g.botsFire || g.mapId == 1))  // no insta-respawn when they fight back
                g.dummies[size_t(r.dummyIndex)].respawnLeft = 2.0f + rnd(g) * 2.0f;
            if (r.kill && g.mode == 2) g.dummies[size_t(r.dummyIndex)].respawnLeft = 1e9f;  // retakes: dead for the round
            if (!r.kill && g.mode == 2) {  // a retake anchor you hit turns on you
                BotBrain& b = g.bots[size_t(r.dummyIndex)];
                b.alertUntil = g.simTime + 2.0;
                b.lastSeen = g.player.origin;
            }
            if (r.kill && g.botsFire && g.deadUntil < 0 && g.hp < 100.0f) {  // a kill heals you
                g.hp = std::min(100.0f, g.hp + 40.0f);
                pushHitLog(g, "+40 HP", 0x60ff60);
            }
            if (g.mode == 1) {
                BotBrain& b = g.bots[size_t(r.dummyIndex)];
                if (r.kill) {
                    g.dmKills++;
                    b.deaths++;
                } else {  // hit but alive: they turn on you
                    b.alertUntil = g.simTime + 2.0;
                    b.lastSeen = g.player.origin;
                    if (b.state != 2) { b.state = 3; b.path.clear(); }
                }
            }
            if (r.kill && g.drill && g.dummies[size_t(r.dummyIndex)].respawns > 0) {
                g.lastTtk = g.simTime - g.aliveSince[size_t(r.dummyIndex)];
                g.drillTtkSum += g.lastTtk;
                g.drillKills++;
            }
            // Hit feedback: a tick you can hear over the gunfire and an X that pops on the crosshair
            // (red for the head, bigger and longer on a kill).
            g.hitMarkerStart = g.simTime;
            g.hitMarkerUntil = g.simTime + (r.kill ? 0.4 : 0.2);
            g.hitMarkerHead = r.group == kHead;
            g.hitMarkerKill = r.kill;
            if (g.hitSound) sound(g, Sfx::HitMarker, r.kill ? 1.0f : 0.75f, 0.0f, r.kill ? 0.8f : 1.0f);
        } else if (r.hitWorld) {
            // Decal color follows the spray index (yellow first shot -> red late spray).
            float t = float(r.sprayIndex) / float(std::max(1, wd.patternLen - 1));
            uint32_t col = lerpColor(0xffe650, 0xe02828, t);
            float hs = 1.4f * std::clamp(r.distance / 700.0f, 1.0f, 3.0f);  // readable far away too
            Vec3 c = r.end + r.normal * 0.6f, h{hs, hs, hs};
            g.pendingDecals.push_back(makeBox(c - h, c + h, col, false));
        }
        for (int k = 0; k < r.penCount; ++k) {
            Vec3 h{1.4f, 1.4f, 1.4f}, c = r.penEntry[k] + r.penNormal[k] * 0.6f;
            g.pendingDecals.push_back(makeBox(c - h, c + h, 0xffe650, false));
        }
        if (ws.ammo == 0) {
            ws.reloadEndTime = g.simTime + wd.reloadTime;
            g.reloadStage = 0;
            g.zoom = 0;
            g.resumeZoomAt = -1;
        }
        g.hudDirty = true;
    }
    if (!fired && !(wantFire && ws.ammo > 0 && ws.reloadEndTime < 0)) decayRecoil(ws, kTickDt);

    // Movement.
    g.prevPlayer = g.player;
    if (g.noclip) {
        // Fly where you look; Shift = slow. No collision.
        Vec3 f = anglesToForward(float(g.viewPitch), float(g.viewYaw)), r = yawToRight(float(g.viewYaw));
        Vec3 wish = f * in.forward + r * in.side;
        float len = length(wish);
        g.player.velocity = len > 0 ? wish * ((in.walk ? 150.0f : 600.0f) / len) : Vec3{};
        g.player.origin += g.player.velocity * kTickDt;
        g.player.onGround = false;
    } else {
        float maxSpeed = wd.maxSpeed * (g.zoom > 0 ? 0.5f : 1.0f);  // scoped = half speed
        playerMove(g.player, in, float(g.viewYaw), maxSpeed, g.world, g.moveParams);
        // Walking up or down a step pops the hull by up to stepSize in one tick. The camera eases
        // over it instead (cosmetic only; the simulation is unchanged).
        float dz = g.player.origin.z - g.prevPlayer.origin.z;
        if (g.prevPlayer.onGround && g.player.onGround && std::fabs(dz) > 0.01f &&
            std::fabs(dz) <= g.moveParams.stepSize + 2.0f)
            g.stepSmooth = std::clamp(g.stepSmooth - dz, -32.0f, 32.0f);
    }
    if (g.mapId == 1) {
        const char* c = dustCallout(g.player.origin);
        if (c != g.callout) { g.callout = c; g.hudDirty = true; }
    }

    for (size_t i = 0; i < g.dummies.size(); ++i) {
        bool wasAlive = g.dummies[i].alive();
        updateDummy(g.dummies[i], kTickDt);
        if (!wasAlive && g.dummies[i].alive()) g.aliveSince[i] = g.simTime;
    }

    // Movement sounds: footsteps above walking speed, jump, landing.
    {
        const PlayerState& p = g.player;
        float hs = length2d(p.velocity);
        if (!g.prevPlayer.onGround && p.onGround) {
            float fall = -g.prevPlayer.velocity.z;
            sound(g, Sfx::Land, std::clamp(fall / 500.0f, 0.35f, 1.0f));
            g.vm.onLand(fall);
            g.stepDist = 0;
        } else if (g.prevPlayer.onGround && !p.onGround && p.velocity.z > 100.0f) {
            sound(g, Sfx::Footstep, 0.45f);
        }
        if (p.onGround && hs > kStepSpeed && !p.ducked) {
            g.stepDist += hs * kTickDt;
            if (g.stepDist >= kStepStride) {
                g.stepDist -= kStepStride;
                g.stepLeft = !g.stepLeft;
                float pitch = 0.92f + float((g.shots + int(g.simTime * 7)) % 16) * 0.01f;
                sound(g, Sfx::Footstep, 0.5f, g.stepLeft ? -0.15f : 0.15f, pitch);
                makeNoise(g, p.origin, 1100.0f);
            }
        } else {
            g.stepDist = std::min(g.stepDist, kStepStride * 0.6f);  // first step comes quickly
        }
        // Strafing dummies make positional footsteps: practise hearing direction.
        for (Dummy& d : g.dummies) {
            if (!d.alive()) continue;
            float ds = length(d.pos - d.prevPos) / kTickDt;
            if (ds <= kStepSpeed) continue;
            d.stepDist += ds * kTickDt;
            if (d.stepDist >= kStepStride) {
                d.stepDist -= kStepStride;
                if (g.audio) g.audio->play3D(Sfx::Footstep, d.pos, g.lastRenderEye, float(g.viewYaw), 1600.0f, 0.9f);
            }
        }
    }

    // Counter-strafe meter: time from letting go / reversing until you're accurate again (rifle threshold).
    float threshold = rifleDef().maxSpeed * rifleDef().accurateSpeedFrac;
    Vec3 hv{g.player.velocity.x, g.player.velocity.y, 0};
    float speed = length(hv);
    float y = float(g.viewYaw) * kDegToRad;
    Vec3 wish = Vec3{std::cos(y), std::sin(y), 0} * in.forward + Vec3{std::sin(y), -std::cos(y), 0} * in.side;
    bool hasInput = length(wish) > 0;
    bool aligned = hasInput && dot(wish, hv) > 0;
    switch (g.stopState) {
        case Game::StopState::Idle:
        case Game::StopState::Running:
            if (speed > threshold && aligned) g.stopState = Game::StopState::Running;
            else if (g.stopState == Game::StopState::Running && speed > threshold) {
                g.stopState = Game::StopState::Stopping;
                g.stopIsCounter = hasInput;  // pressing the opposite key vs. just releasing
                g.stopStart = g.simTime;
            } else if (speed <= threshold) g.stopState = Game::StopState::Idle;
            break;
        case Game::StopState::Stopping:
            if (aligned) g.stopState = Game::StopState::Running;
            else if (speed <= threshold) {
                g.lastStopMs = float((g.simTime + kTickDt - g.stopStart) * 1000.0);
                g.lastStopCounter = g.stopIsCounter;
                g.stopState = Game::StopState::Idle;
                g.hudDirty = true;
            }
            break;
    }

    // ---- Smoke grenades ----
    if (g.throwLatch) {
        Vec3 f = anglesToForward(float(g.viewPitch), float(g.viewYaw));
        float throwSpeed = g.throwLob ? 380.0f : 750.0f;
        g.nades.push_back({g.lastRenderEye + f * 16.0f, f * throwSpeed + g.player.velocity, g.simTime + 1.6});
        g.throwLob = false;
        sound(g, Sfx::Draw, 0.6f, 0.0f, 1.3f);
        g.throwLatch = false;
    }
    for (size_t k = 0; k < g.nades.size();) {
        Game::Nade& n = g.nades[k];
        n.vel.z -= 800.0f * kTickDt;
        Vec3 next = n.pos + n.vel * kTickDt;
        TraceResult tr = g.world.traceRay(n.pos, next);
        if (tr.fraction < 1.0f) {
            float into = dot(n.vel, tr.normal);
            n.vel = (n.vel - tr.normal * (2.0f * into)) * 0.45f;
            n.pos = tr.endpos + tr.normal * 0.1f;
            if (-into > 80.0f && g.audio)
                g.audio->play3D(Sfx::Footstep, n.pos, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.4f, 1.8f);
        } else {
            n.pos = next;
        }
        if (g.simTime >= n.detonateAt) {
            g.smokes.push_back({n.pos, g.simTime});
            if (g.audio) g.audio->play3D(Sfx::Land, n.pos, g.lastRenderEye, float(g.viewYaw), 2500.0f, 1.0f, 0.55f);
            g.nades.erase(g.nades.begin() + long(k));
        } else {
            ++k;
        }
    }
    g.smokes.erase(std::remove_if(g.smokes.begin(), g.smokes.end(),
                                  [&](const Game::Smoke& s) { return g.simTime - s.start > kSmokeLife; }),
                   g.smokes.end());

    // ---- Dust bots: hide, peek, hold an angle, return; respawn at a free spot ----
    if (g.mapId == 1 && g.mode == 0) {
        const auto& spots = dustPeekSpots();
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            Dummy& d = g.dummies[i];
            if (!d.alive()) { g.botState[i] = -1; continue; }
            if (g.botState[i] < 0) {  // just respawned: pick a random free spot
                int s = int(rnd(g) * float(spots.size())) % int(spots.size());
                for (size_t tries = 0; tries < spots.size(); ++tries) {
                    bool used = false;
                    for (size_t j = 0; j < g.dummies.size(); ++j) used |= j != i && g.botSpot[j] == s;
                    if (!used) break;
                    s = (s + 1) % int(spots.size());
                }
                g.botSpot[i] = s;
                d.pos = d.prevPos = spots[size_t(s)].cover;
                g.botState[i] = 0;
                g.botTimer[i] = 0.8f + rnd(g) * 2.0f;
            }
            const PeekSpot& sp = spots[size_t(g.botSpot[i])];
            g.botTimer[i] -= kTickDt;
            auto moveTo = [&](const Vec3& target) {
                Vec3 dlt = target - d.pos;
                float dist = length(dlt), step = 250.0f * kTickDt;
                if (dist <= step) { d.pos = target; return true; }
                d.pos += dlt * (step / dist);
                return false;
            };
            switch (g.botState[i]) {
                case 0: if (g.botTimer[i] <= 0) g.botState[i] = 1; break;
                case 1: if (moveTo(sp.peek)) { g.botState[i] = 2; g.botTimer[i] = 0.5f + rnd(g) * 1.2f; } break;
                case 2: if (g.botTimer[i] <= 0) g.botState[i] = 3; break;
                case 3: if (moveTo(sp.cover)) { g.botState[i] = 0; g.botTimer[i] = 0.6f + rnd(g) * 2.0f; } break;
            }
            d.yaw = turnToward(d.yaw, yawTo(d.pos, g.player.origin), 540.0f * kTickDt);
        }
    }

    // ---- Deathmatch: match clock, then the bots roam, react to what they see and hear, and chase ----
    if (g.mapId == 1 && g.mode == 1) {
        if (g.dmOverUntil < 0 && g.simTime >= g.dmEnd) {
            g.dmOverUntil = g.simTime + 8.0;  // results screen, then a new match
            g.hudDirty = true;
        } else if (g.dmOverUntil >= 0 && g.simTime >= g.dmOverUntil) {
            startDeathmatch(g);
        }
        int secs = int(std::max(0.0, g.dmEnd - g.simTime));
        if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
    }
    // ---- Retakes: round clock, win when the site is clear, lose on time or death ----
    if (g.mapId == 1 && g.mode == 2) {
        bool anyAlive = false;
        for (const Dummy& d : g.dummies) anyAlive |= d.alive();
        if (g.rtResultUntil < 0) {
            if (!anyAlive) endRetakeRound(g, true, "SITE CLEARED");
            else if (g.simTime >= g.rtRoundEnd) endRetakeRound(g, false, "OUT OF TIME");
        } else if (g.simTime >= g.rtResultUntil) {
            startRetakeRound(g);
        }
        int secs = int(std::max(0.0, g.rtRoundEnd - g.simTime));
        if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
    }
    if (g.mapId == 1 && g.mode != 0 && g.nav.ready()) {
        BotSenses sense;
        sense.world = &g.world;
        sense.nav = &g.nav;
        sense.now = g.simTime;
        sense.playerOrigin = g.player.origin;
        sense.playerEye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)};
        sense.playerUp = g.deadUntil < 0 && !g.noclip && g.dmOverUntil < 0 && g.rtResultUntil < 0;
        sense.noiseFresh = g.simTime - g.noiseAt < 1.5 * kTickDt;
        sense.noisePos = g.noisePos;
        sense.noiseRadius = g.noiseRadius;
        sense.blocked = [](const void* ctx, const Vec3& a, const Vec3& b) {
            return smokeBlocks(*static_cast<const Game*>(ctx), a, b);
        };
        sense.blockCtx = &g;
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            Dummy& d = g.dummies[i];
            BotBrain& b = g.bots[i];
            if (!d.alive()) { b.state = -1; b.sees = b.aimed = false; continue; }
            if (g.mode == 1 && needsSpawn(d, b)) spawnDeathmatchBot(d, b, pickDmSpawn(g, false, i), g.rng);
            updateDeathmatchBot(d, b, sense, g.rng);
        }
    }

    // ---- Bots shoot back ----
    Vec3 simEye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)};
    g.eyeHistory[g.histHead] = simEye;
    g.histHead = (g.histHead + 1) & 63;
    if (g.deadUntil >= 0 && g.simTime >= g.deadUntil) g.deadUntil = -1;
    if (g.botsFire && g.deadUntil < 0 && !g.noclip) {
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            Vec3 head = d.pos + Vec3{0, 0, 64};
            // Deathmatch bots need to see you (view cone) and have turned to face you first.
            bool los = g.mode != 0 && g.mapId == 1
                           ? d.alive() && g.bots[i].aimed
                           : d.alive() && length(simEye - head) < 4000.0f &&
                                 g.world.traceRay(head, simEye).fraction >= 1.0f && !smokeBlocks(g, head, simEye);
            if (!los) { g.botSeen[i] = 0; continue; }
            if (g.botSeen[i] == 0) g.botReact[i] = 0.25f + rnd(g) * 0.3f;  // human-ish reaction time
            g.botSeen[i] += kTickDt;
            g.botCooldown[i] -= kTickDt;
            if (g.botSeen[i] < g.botReact[i] || g.botCooldown[i] > 0) continue;  // reaction time, fire rate
            g.botCooldown[i] = 0.22f + rnd(g) * 0.16f;

            Vec3 aim = g.eyeHistory[(g.histHead - 1 - 26 + 64) & 63] - Vec3{0, 0, 16};  // your chest 0.2 s ago
            float err = length(aim - head) * 0.014f;  // ~0.8 deg of random aim error
            aim += Vec3{(rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * err};
            Vec3 dir = normalize(aim - head);
            TraceResult wt = g.world.traceRay(head, head + dir * 5000.0f);
            float maxT = wt.fraction * 5000.0f, bestT = maxT;
            float hh = g.player.ducked ? kDuckHeight : kStandHeight;
            Vec3 o = g.player.origin;
            int hit = 0;  // 1 body, 2 head
            float t;
            if (rayHitsBox(head, dir, bestT, o + Vec3{-13, -13, 0}, o + Vec3{13, 13, hh - 10}, t) && t >= 0) {
                bestT = t; hit = 1;
            }
            if (rayHitsBox(head, dir, bestT, o + Vec3{-5, -5, hh - 10}, o + Vec3{5, 5, hh}, t) && t >= 0) {
                bestT = t; hit = 2;
            }
            if (g.audio) {  // far away a gunshot is mostly echo: muffled, no crack
                bool far = length(d.pos - simEye) > 1400.0f;
                g.audio->play3D(far ? Sfx::RifleShotFar : Sfx::RifleShot, d.pos, simEye, float(g.viewYaw),
                                far ? 6500.0f : 4000.0f, far ? 1.0f : 0.75f);
            }
            g.fx.tracer(head + dir * 20.0f, head + dir * bestT);
            if (hit && g.simTime < g.spawnProtectUntil) hit = 0;  // deathmatch spawn protection
            if (hit) {
                g.hp -= hit == 2 ? 100.0f : 26.0f;
                g.hurtUntil = g.simTime + 0.25;
                sound(g, Sfx::HitBody, 0.9f, 0.0f, 0.7f);
                if (g.hp <= 0) {
                    g.deaths++;
                    g.hp = 100;
                    g.deadUntil = g.simTime + 1.2;
                    if (g.mode == 1 && g.mapId == 1) {
                        g.dmDeaths++;
                        g.bots[i].kills++;
                        char kb[48];
                        std::snprintf(kb, sizeof(kb), "KILLED BY BOT %d%s", int(i) + 1, hit == 2 ? "  HEADSHOT" : "");
                        pushHitLog(g, kb, 0xff4040);
                        g.spawn = pickDmSpawn(g, true);
                        g.spawnYaw = rnd(g) * 360.0f - 180.0f;
                        g.spawnProtectUntil = g.deadUntil + 1.0;
                        for (BotBrain& b : g.bots)
                            if (b.state == 2) { b.state = 1; b.timer = 1.0f; }
                    } else {
                        pushHitLog(g, "YOU DIED", 0xff4040);
                        if (g.mode == 2 && g.mapId == 1) endRetakeRound(g, false, "YOU DIED");
                    }
                    refillAmmo(g);  // respawn with full magazines, like CS
                    resetPosition(g);
                    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
                    g.hudDirty = true;
                    break;  // nobody else shoots at your new spawn this tick
                }
                g.hudDirty = true;
            }
        }
    }

    // ---- KZ course ----
    if (g.mapId == 0) {
        const PlayerState& p = g.player;
        bool onStart = p.onGround && p.origin.z > kKzPadHeight - 1 &&
                       inRect(p.origin, kKzStartMinX, kKzStartMaxX, kKzMinY, kKzMaxY);
        bool onEnd = p.onGround && p.origin.z > kKzPadHeight - 1 &&
                     inRect(p.origin, kKzEndMinX, kKzEndMaxX, kKzMinY, kKzMaxY);
        bool inLava = p.onGround && p.origin.z < 1.0f && inRect(p.origin, kKzLavaMinX, kKzLavaMaxX, kKzMinY, kKzMaxY);
        if (inLava) {
            g.player = {};
            g.player.origin = {(kKzStartMinX + kKzStartMaxX) / 2, (kKzMinY + kKzMaxY) / 2, kKzPadHeight};
            g.player.onGround = true;
            g.prevPlayer = g.player;
            g.kzState = 1;
            sound(g, Sfx::Land, 0.8f, 0.0f, 0.6f);
            g.hudDirty = true;
        } else if (onStart) {
            g.kzState = 1;
        } else if (g.kzState == 1) {
            g.kzState = 2;
            g.kzStart = g.simTime;
        } else if (g.kzState == 2 && onEnd) {
            g.kzLast = g.simTime - g.kzStart;
            if (g.kzBest < 0 || g.kzLast < g.kzBest) g.kzBest = g.kzLast;
            g.kzState = 0;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "KZ FINISH %.3f S", g.kzLast);
            pushHitLog(g, buf, 0x80ff80);
            sound(g, Sfx::HitHead, 0.8f);
            g.hudDirty = true;
        }
    }

    g.simTime += kTickDt;
}

struct FrameStats {
    std::vector<float> window;  // frame times (s) since last report
    double accum = 0;
    float avgFps = 0, lowFps = 0, avgMs = 0;

    void push(float dt) {
        window.push_back(dt);
        accum += dt;
        if (accum >= 0.5) {
            std::vector<float> sorted = window;
            size_t idx = std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.99));
            std::nth_element(sorted.begin(), sorted.begin() + long(idx), sorted.end());
            avgFps = float(double(window.size()) / accum);
            avgMs = float(accum * 1000.0 / double(window.size()));
            lowFps = sorted[idx] > 0 ? 1.0f / sorted[idx] : 0;
            window.clear();
            accum = 0;
        }
    }
};

// ---- In-game settings menu (pause screen) ----
struct MenuItem {
    const char* name;
    float* f;      // float setting, or
    int* i;        // int setting
    float step, lo, hi;
    const char* const* labels = nullptr;  // optional names for int values
};

const char* const kOnOff[] = {"OFF", "ON"};
const uint32_t kCrosshairColors[] = {0x00FF00, 0xFFFF00, 0x00FFFF, 0xFFFFFF, 0xFF3030, 0xFF40FF};
const char* const kCrosshairColorNames[] = {"GREEN", "YELLOW", "CYAN", "WHITE", "RED", "PINK"};
int g_crosshairPreset = 0;  // menu-side index into kCrosshairColors

std::vector<MenuItem> menuItems(Config& c) {
    return {
        {"SENSITIVITY", &c.sensitivity, nullptr, 0.02f, 0.05f, 20.0f},
        {"SCOPED SENSITIVITY (RATIO)", &c.zoom_sensitivity_ratio, nullptr, 0.05f, 0.1f, 3.0f},
        {"FOV (4:3, CS = 90)", &c.fov, nullptr, 1.0f, 60.0f, 120.0f},
        {"VIEWMODEL FOV", &c.viewmodel_fov, nullptr, 1.0f, 50.0f, 90.0f},
        {"VIEWMODEL BOB", &c.viewmodel_bob, nullptr, 0.1f, 0.0f, 2.0f},
        {"SHOW VIEWMODEL", nullptr, &c.show_viewmodel, 1, 0, 1, kOnOff},
        {"VOLUME", &c.volume, nullptr, 0.05f, 0.0f, 1.0f},
        {"CROSSHAIR SIZE", nullptr, &c.crosshair_size, 1, 0, 30},
        {"CROSSHAIR GAP", nullptr, &c.crosshair_gap, 1, -5, 20},
        {"CROSSHAIR THICKNESS", nullptr, &c.crosshair_thickness, 1, 1, 8},
        {"CROSSHAIR COLOR", nullptr, &g_crosshairPreset, 1, 0, 5, kCrosshairColorNames},
        {"CROSSHAIR DOT", nullptr, &c.crosshair_dot, 1, 0, 1, kOnOff},
        {"CROSSHAIR OUTLINE", nullptr, &c.crosshair_outline, 1, 0, 1, kOnOff},
        {"FPS CAP (0 = UNLIMITED)", nullptr, &c.fps_max, 30, 0, 1000},
        {"BUNNY HOP", nullptr, &c.bhop, 1, 0, 1, kOnOff},
        {"ZERO-LAG CAMERA", nullptr, &c.camera_extrapolate, 1, 0, 1, kOnOff},
        {"SMOOTH STAIRS (CAMERA)", nullptr, &c.view_smooth_steps, 1, 0, 1, kOnOff},
        {"SPRAY CAMERA SHAKE", nullptr, &c.view_shake, 1, 0, 1, kOnOff},
        {"HITMARKER", nullptr, &c.hitmarker, 1, 0, 1, kOnOff},
        {"HIT SOUND", nullptr, &c.hitsound, 1, 0, 1, kOnOff},
        {"ANTI-ALIASING (RESTART)", nullptr, &c.msaa, 2, 0, 8},
        {"DUST SIZE (% OF REAL DUST2)", nullptr, &c.dust_scale, 5, 50, 100},
        {"DEATHMATCH BOTS", nullptr, &c.dm_bots, 1, 1, 16},
        {"DEATHMATCH MINUTES", nullptr, &c.dm_minutes, 1, 1, 30},
        {"RETAKE BOTS", nullptr, &c.rt_bots, 1, 1, 6},
        {"RANDOM SPRAY SPREAD", nullptr, &c.spread_spray, 1, 0, 1, kOnOff},
        {"RANDOM MOVING SPREAD", nullptr, &c.spread_movement, 1, 0, 1, kOnOff},
    };
}

// Adjusts item `sel` by `dir` steps (shift = x5). Returns true if something changed.
bool adjustMenu(Config& c, int sel, int dir, bool big) {
    std::vector<MenuItem> items = menuItems(c);
    if (sel < 0 || sel >= int(items.size())) return false;
    const MenuItem& it = items[size_t(sel)];
    float mult = big ? 5.0f : 1.0f;
    if (it.f) {
        float v = std::clamp(*it.f + it.step * mult * float(dir), it.lo, it.hi);
        *it.f = std::round(v * 1000.0f) / 1000.0f;
    } else {
        int range = int(it.hi - it.lo) + 1;
        int v = *it.i + int(it.step * (it.labels ? 1.0f : mult)) * dir;
        if (it.labels) v = int(it.lo) + ((v - int(it.lo)) % range + range) % range;  // wrap toggles/lists
        *it.i = std::clamp(v, int(it.lo), int(it.hi));
    }
    if (it.i == &g_crosshairPreset) {
        uint32_t col = kCrosshairColors[g_crosshairPreset];
        c.crosshair_r = int(col >> 16);
        c.crosshair_g = int((col >> 8) & 255);
        c.crosshair_b = int(col & 255);
    }
    return true;
}

void buildHud(HudBatch& hud, const Game& g, const Config& cfg, const FrameStats& st, int w, int h, bool paused,
              bool showHelp, int menuSel) {
    hud.clear();
    int s = cfg.hud_scale > 0 ? cfg.hud_scale : std::max(1, h / 540);
    hud.fontScale = s;
    const float lh = 10.0f * s;
    char buf[160];

    // Crosshair (CS-style: four lines with a gap), pixel-aligned at screen center.
    float cx = float(w / 2), cy = float(h / 2);
    float gap = float(cfg.crosshair_gap), len = float(cfg.crosshair_size), th = float(cfg.crosshair_thickness);
    uint32_t xc = (uint32_t(cfg.crosshair_r & 255) << 24) | (uint32_t(cfg.crosshair_g & 255) << 16) |
                  (uint32_t(cfg.crosshair_b & 255) << 8) | 0xFF;
    auto crossRects = [&](float grow, uint32_t col) {
        float t0 = std::floor(th / 2);
        hud.rect(cx + gap - grow, cy - t0 - grow, len + 2 * grow, th + 2 * grow, col);
        hud.rect(cx - gap - len - grow, cy - t0 - grow, len + 2 * grow, th + 2 * grow, col);
        hud.rect(cx - t0 - grow, cy + gap - grow, th + 2 * grow, len + 2 * grow, col);
        hud.rect(cx - t0 - grow, cy - gap - len - grow, th + 2 * grow, len + 2 * grow, col);
        if (cfg.crosshair_dot) hud.rect(cx - t0 - grow, cy - t0 - grow, th + 2 * grow, th + 2 * grow, col);
    };
    const bool sniper = g.weapon == &g.sniper;
    if (sniper && g.zoom > 0) {
        // Scope: black outside a circle (drawn as horizontal strips) + thin full-screen crosshair.
        float r = float(h) * 0.47f;
        hud.rect(0, 0, cx - r, float(h), 0x000000FF);
        hud.rect(cx + r, 0, float(w) - (cx + r), float(h), 0x000000FF);
        hud.rect(cx - r, 0, 2 * r, cy - r, 0x000000FF);
        hud.rect(cx - r, cy + r, 2 * r, float(h) - (cy + r), 0x000000FF);
        for (float yy = -r; yy < r; yy += 2.0f) {
            float half = std::sqrt(std::max(0.0f, r * r - (yy + 1.0f) * (yy + 1.0f)));
            hud.rect(cx - r, cy + yy, r - half, 2.0f, 0x000000FF);
            hud.rect(cx + half, cy + yy, r - half, 2.0f, 0x000000FF);
        }
        hud.rect(0, cy, float(w), 1, 0x000000FF);
        hud.rect(cx, 0, 1, float(h), 0x000000FF);
    } else if (!sniper) {  // like CS: the sniper has no crosshair unscoped
        if (cfg.crosshair_outline) crossRects(1, 0x000000C0);
        crossRects(0, xc);
    }

    // Hit marker.
    if (g.showHitMarker && g.simTime < g.hitMarkerUntil) {
        // Pops in a little big, settles, then fades out.
        float age = float(g.simTime - g.hitMarkerStart), life = float(g.hitMarkerUntil - g.hitMarkerStart);
        float pop = 1.0f + 0.5f * std::max(0.0f, 1.0f - age / 0.06f);
        float fade = std::clamp((life - age) / (life * 0.5f), 0.0f, 1.0f);
        uint32_t alpha = uint32_t(255.0f * fade);
        uint32_t hc = (g.hitMarkerHead ? 0xFF3C3C00u : 0xFFFFFF00u) | alpha;
        float size = (g.hitMarkerKill ? 1.6f : 1.0f) * pop * float(s);
        float a = float(gap) + 4.0f * size, b = float(gap) + 11.0f * size, lw = 2.0f * float(s);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                hud.line(cx + float(sx) * a, cy + float(sy) * a, cx + float(sx) * b, cy + float(sy) * b, lw + 2.0f,
                         alpha * 3 / 4);  // dark outline
                hud.line(cx + float(sx) * a, cy + float(sy) * a, cx + float(sx) * b, cy + float(sy) * b, lw, hc);
            }
    }

    // Top-left: performance + movement.
    float x = 12.0f * s, y = 10.0f * s;
    std::snprintf(buf, sizeof(buf), "FPS %4.0f   1%% LOW %4.0f   %.2f MS", st.avgFps, st.lowFps, st.avgMs);
    hud.text(x, y, buf, 0xFFFFFFFF);
    y += lh;
    const WeaponState& ws = *g.weapon;
    float speed = length2d(g.player.velocity);
    float threshold = rifleDef().maxSpeed * rifleDef().accurateSpeedFrac;
    float inacc = ws.def->canFire ? currentInaccuracy(ws, speed, g.player.onGround, g.player.ducked) : 0;
    std::snprintf(buf, sizeof(buf), "SPEED %3.0f   SPREAD %.2f DEG   %s", speed, inacc,
                  speed <= threshold && g.player.onGround ? "ACCURATE" : "");
    hud.text(x, y, buf, speed <= threshold && g.player.onGround ? 0x7CFC7CFF : 0xFFFFFFFF);
    y += lh;
    if (g.lastStopMs >= 0) {
        std::snprintf(buf, sizeof(buf), "LAST STOP %3.0f MS (%s)", g.lastStopMs,
                      g.lastStopCounter ? "COUNTER-STRAFE" : "RELEASE");
        hud.text(x, y, buf, 0xFFFFFFFF);
    }
    y += lh;
    std::snprintf(buf, sizeof(buf), "SHOTS %d   HITS %d   HEADSHOTS %d%s", g.shots, g.hits, g.headshots,
                  g.noclip ? "   NOCLIP" : "");
    hud.text(x, y, buf, 0xD0D0D0FF);
    y += lh;
    if (g.drill) {
        if (g.drillKills > 0)
            std::snprintf(buf, sizeof(buf), "AIM DRILL   KILLS %d   LAST %.0f MS   AVG %.0f MS", g.drillKills,
                          g.lastTtk * 1000.0, g.drillTtkSum / g.drillKills * 1000.0);
        else
            std::snprintf(buf, sizeof(buf), "AIM DRILL   KILL A RANGE DUMMY TO START");
        hud.text(x, y, buf, 0xFFD060FF);
    }
    y += lh * 1.5f;
    if (g.botsFire) {
        if (g.mode == 1 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "DEATHMATCH   TAB SCORES   F7 RETAKES");
        else if (g.mode == 2 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "RETAKES   F7 PRACTICE");
        else std::snprintf(buf, sizeof(buf), "BOTS SHOOT BACK   DEATHS %d", g.deaths);
        hud.text(x, y, buf, 0xFF8060FF);
        y += lh;
        std::snprintf(buf, sizeof(buf), "HP %.0f", double(g.hp));
        hud.text(16.0f * s, float(h) - 24.0f * s, buf, g.hp > 30 ? 0xFFFFFFFF : 0xFF5050FF, s * 2);
        if (g.simTime < g.hurtUntil) hud.rect(0, 0, float(w), float(h), 0xC0000040);
        if (g.deadUntil >= 0) {
            const char* dead = "YOU DIED";
            hud.text(cx - hud.textWidth(dead, s * 3) / 2, cy - 60.0f * s, dead, 0xFF4040FF, s * 3);
        }
    }
    if (g.mapId == 1 && g.callout[0]) hud.text(cx - hud.textWidth(g.callout, s * 2) / 2, 12.0f * s, g.callout, 0xFFFFFFD0, s * 2);
    if (g.mapId == 1 && g.mode == 1) {
        int left = int(std::max(0.0, g.dmEnd - g.simTime));
        std::snprintf(buf, sizeof(buf), "%d:%02d   KILLS %d   DEATHS %d", left / 60, left % 60, g.dmKills, g.dmDeaths);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, 0xFFFFFFFF);
        if (g.showScores || g.dmOverUntil >= 0) {
            // Scoreboard (Tab), and the results screen at the end of a match.
            float rowH = 11.0f * s, panelW = 60.0f * 6 * s, panelH = rowH * float(g.bots.size() + 7);
            float px = cx - panelW / 2, py = cy - panelH / 2 - 40.0f * s;
            hud.rect(px - 10 * s, py - 10 * s, panelW + 20 * s, panelH + 20 * s, 0x15181CE0);
            if (g.dmOverUntil >= 0) std::snprintf(buf, sizeof(buf), "MATCH OVER");
            else std::snprintf(buf, sizeof(buf), "DEATHMATCH   %d:%02d LEFT", left / 60, left % 60);
            hud.text(px, py, buf, 0xFFD060FF, s * 2);
            float ry = py + rowH * 2.5f;
            auto row = [&](const char* name, int k, int d, const char* extra, uint32_t col) {
                char line[96];
                std::snprintf(line, sizeof(line), "%-10s  KILLS %3d   DEATHS %3d   %s", name, k, d, extra);
                hud.text(px, ry, line, col);
                ry += rowH;
            };
            char extra[64];
            std::snprintf(extra, sizeof(extra), "HS %d%%  ACC %d%%", g.dmKills ? g.headshots * 100 / std::max(1, g.hits) : 0,
                          g.shots ? g.hits * 100 / g.shots : 0);
            row("YOU", g.dmKills, g.dmDeaths, extra, 0xFFFFFFFF);
            ry += rowH * 0.5f;
            for (size_t i = 0; i < g.bots.size(); ++i) {
                char name[16];
                std::snprintf(name, sizeof(name), "BOT %d", int(i) + 1);
                row(name, g.bots[i].kills, g.bots[i].deaths, "", 0xC8C8C8FF);
            }
            if (g.dmOverUntil >= 0) hud.text(px, ry + rowH, "NEXT MATCH STARTS IN A FEW SECONDS", 0xA0A0A0FF);
        }
    }
    if (g.mapId == 1 && g.mode == 2) {
        int left = int(std::max(0.0, g.rtRoundEnd - g.simTime)), alive = 0;
        for (const Dummy& d : g.dummies) alive += d.alive();
        std::snprintf(buf, sizeof(buf), "RETAKE %s   0:%02d   BOTS LEFT %d   WON %d  LOST %d",
                      dustRetakeSites()[size_t(g.rtSite)].name, left, alive, g.rtWon, g.rtLost);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, left <= 10 ? 0xFF8060FF : 0xFFFFFFFF);
        if (g.rtResultUntil >= 0) {
            uint32_t col = g.rtResultWin ? 0x60FF60FF : 0xFF5050FF;
            hud.text(cx - hud.textWidth(g.rtResultText, s * 3) / 2, cy - 90.0f * s, g.rtResultText, col, s * 3);
        }
    }
    if (g.buyMenu) {
        float rowH = 11.0f * s, px = cx - 90.0f * s, py = cy + 40.0f * s;
        hud.rect(px - 10 * s, py - 10 * s, 200.0f * s, rowH * 5 + 20 * s, 0x15181CE0);
        hud.text(px, py, "BUY: PRIMARY WEAPON", 0xFFD060FF);
        hud.text(px, py + rowH * 1.5f, g.primary == &g.rifle ? "1  RIFLE  <" : "1  RIFLE", 0xFFFFFFFF);
        hud.text(px, py + rowH * 2.5f, g.primary == &g.sniper ? "2  SNIPER  <" : "2  SNIPER", 0xFFFFFFFF);
        hud.text(px, py + rowH * 4.0f, "B OR ESC TO CLOSE", 0xA0A0A0FF);
    }
    if (g.kzState == 2 || g.kzLast >= 0) {
        if (g.kzState == 2) std::snprintf(buf, sizeof(buf), "KZ %.2f", g.simTime - g.kzStart);
        else std::snprintf(buf, sizeof(buf), "KZ LAST %.3f   BEST %.3f", g.kzLast, g.kzBest);
        hud.text(cx - hud.textWidth(buf, s * 2) / 2, 12.0f * s, buf, g.kzState == 2 ? 0xFFFFFFFF : 0x80FF80FF, s * 2);
    }
    if (showHelp) {
        const char* help[] = {
            "WASD MOVE   SPACE/WHEEL JUMP   CTRL CROUCH   SHIFT WALK",
            "MOUSE1 FIRE   MOUSE2 SCOPE   R RELOAD   1 PRIMARY   2 PISTOL   3 KNIFE   4 SMOKE   Q LAST WEAPON",
            "B BUY MENU (RIFLE / SNIPER)   TAB SCORES   G QUICK SMOKE (SMOKE OUT: MOUSE1 THROW, MOUSE2 LOB)",
            "HOLD SPACE TO BUNNY HOP - AIR STRAFE (A/D + TURN) TO GAIN SPEED",
            "V NOCLIP   F6 RESET POSITION   F5 RELOAD CONFIG.CFG   F4 BOTS SHOOT BACK",
            "KZ COURSE: GREEN PAD BEHIND THE SPRAY WALL - HOP THE BLUE PADS, AVOID THE LAVA",
            "F8 SWITCH MAP: FEEL LAB / DUST   F7 MODE: PRACTICE / DEATHMATCH / RETAKES   F INSPECT",
            "C CLEAR DECALS   F3 AIM DRILL   F1 HIDE HELP   ALT+ENTER FULLSCREEN   ESC PAUSE",
            "LEFT: CRATES + STAIRS + DOOR   AHEAD: RANGE   RIGHT: SPRAY WALL",
        };
        for (const char* l : help) { hud.text(x, y, l, 0xE0E0E0D0); y += lh; }
    }

    // Top-right: hit log.
    float ry = 10.0f * s;
    for (const HitLogEntry& e : g.hitLog) {
        float age = float(g.simTime - e.time);
        if (age > 4) continue;
        uint32_t a = uint32_t(255 * std::clamp(1.0f - (age - 3.0f), 0.0f, 1.0f));
        hud.text(float(w) - hud.textWidth(e.text) - 12.0f * s, ry, e.text, (e.color << 8) | a);
        ry += lh;
    }

    // Bottom-right: weapon + ammo.
    if (ws.def->canFire) {
        if (ws.reloadEndTime >= 0) std::snprintf(buf, sizeof(buf), "%s  RELOADING", ws.def->name);
        else std::snprintf(buf, sizeof(buf), "%s  %d / %d", ws.def->name, ws.ammo, ws.def->magSize);
    } else {
        std::snprintf(buf, sizeof(buf), "%s", ws.def->name);
    }
    hud.text(float(w) - hud.textWidth(buf, s * 2) - 16.0f * s, float(h) - 24.0f * s, buf, 0xFFFFFFFF, s * 2);

    if (paused) {
        hud.rect(0, 0, float(w), float(h), 0x000000A0);
        Config view = cfg;  // menuItems needs non-const pointers; we only read here
        std::vector<MenuItem> items = menuItems(view);
        float rowH = 11.0f * s, panelW = 70.0f * 6 * s, panelH = rowH * float(items.size() + 5);
        float px = cx - panelW / 2, py = cy - panelH / 2;
        hud.rect(px - 10 * s, py - 10 * s, panelW + 20 * s, panelH + 20 * s, 0x15181CE0);
        hud.text(px, py, "SETTINGS", 0xFFD060FF, s * 2);
        float my = py + rowH * 2;
        for (size_t k = 0; k < items.size(); ++k) {
            const MenuItem& it = items[k];
            bool selRow = int(k) == menuSel;
            if (selRow) hud.rect(px - 4 * s, my - 2 * s, panelW + 8 * s, rowH, 0x3A5F9AC0);
            char val[48];
            if (it.f) std::snprintf(val, sizeof(val), it.step < 0.1f ? "%.2f" : "%.1f", double(*it.f));
            else if (it.labels) std::snprintf(val, sizeof(val), "%s", it.labels[*it.i - int(it.lo)]);
            else std::snprintf(val, sizeof(val), "%d", *it.i);
            hud.text(px, my, it.name, selRow ? 0xFFFFFFFF : 0xC8C8C8FF);
            std::string v = selRow ? std::string("< ") + val + " >" : std::string(val);
            hud.text(px + panelW - hud.textWidth(v), my, v, selRow ? 0xFFFFFFFF : 0xC8C8C8FF);
            my += rowH;
        }
        my += rowH;
        hud.text(px, my, "UP/DOWN SELECT   LEFT/RIGHT CHANGE (SHIFT = x5)   SAVED AUTOMATICALLY", 0xA0A0A0FF);
        hud.text(px, my + rowH, "CLICK OR ESC TO RESUME   Q TO QUIT", 0xFFFFFFFF);
    }
}

int fatal(const std::string& msg, SDL_Window* window, bool showBox) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    if (showBox) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Feel Lab", msg.c_str(), window);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--dump-sounds") return Audio::dumpWavs(argv[i + 1]) ? 0 : 1;
    const bool showErrors = opt.screenshotPath.empty();
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");  // low audio latency
    if (!SDL_Init(SDL_INIT_VIDEO)) return fatal(SDL_GetError(), nullptr, showErrors);

    const char* base = SDL_GetBasePath();
    std::string cfgPath = std::string(base ? base : "") + "config.cfg";
    Config cfg = loadConfig(cfgPath);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    bool fullscreen = cfg.fullscreen && opt.windowW == 0;
    int winW = opt.windowW ? opt.windowW : (cfg.width ? cfg.width : 1280);
    int winH = opt.windowH ? opt.windowH : (cfg.height ? cfg.height : 720);
    // High pixel density: render at native resolution even with Windows display scaling (125%/150%).
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    const int msaa = std::clamp(cfg.msaa, 0, 8);
    if (msaa > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
    }
    SDL_Window* window = SDL_CreateWindow("Feel Lab", winW, winH, flags);
    if (!window && msaa > 0) {  // the driver can't do it: carry on without anti-aliasing
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Feel Lab", winW, winH, flags);
    }
    if (!window) return fatal(SDL_GetError(), nullptr, showErrors);
    if (fullscreen && cfg.width && cfg.height) {
        // Exclusive mode at the requested resolution (e.g. 4:3 stretched).
        SDL_DisplayMode mode;
        if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window), cfg.width, cfg.height, 0, true, &mode))
            SDL_SetWindowFullscreenMode(window, &mode);
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) return fatal(std::string("OpenGL 3.3 context: ") + SDL_GetError(), window, showErrors);
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);

    const char* missing = loadGL([](const char* name) -> void* {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    });
    if (missing) return fatal(std::string("OpenGL 3.3 function missing: ") + missing, window, showErrors);
    std::fprintf(stderr, "GL: %s | %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Renderer renderer;
    std::string err;
    if (!renderer.init(err)) return fatal(err, window, showErrors);

    const bool bench = opt.benchSeconds > 0;
    const bool automated = !opt.screenshotPath.empty() || bench;
    // Benchmark: where each frame's time goes (CPU sections + GPU via glFinish), written to bench.txt.
    struct BenchStats {
        double sim = 0, scene = 0, draw = 0, hud = 0, gpu = 0, swap = 0, elapsed = 0;
        std::vector<float> frames;
        size_t dynBoxes = 0;
    } benchStats;
    Game g;
    resetGame(g, opt);
    Audio audio;
    if (!automated && SDL_InitSubSystem(SDL_INIT_AUDIO) && audio.init(std::clamp(cfg.volume, 0.0f, 1.0f)))
        g.audio = &audio;
    else if (!automated)
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    applyConfig(g, cfg);
    g.mode = cfg.mode >= 1 && cfg.mode <= 2 ? cfg.mode : 0;
    renderer.setDepthPrepass(cfg.depth_prepass != 0);
    setDustScale(float(cfg.dust_scale) / 100.0f);
    loadMap(g, renderer, cfg.map == 1 || g.mode != 0 ? 1 : 0);
    // Settings changed (menu, F5): a new Dust size rebuilds the map right away.
    auto settingsChanged = [&]() {
        applyConfig(g, cfg);
        if (setDustScale(float(cfg.dust_scale) / 100.0f) && g.mapId == 1) loadMap(g, renderer, 1);
    };
    if (opt.spawnOverride) {
        float floorZ = g.mapId == 1 ? dustGrid().floorAt(opt.spawnX, opt.spawnY) : 0.0f;
        g.spawn = {opt.spawnX, opt.spawnY, floorZ > MapGrid::kNoFloor ? floorZ : 0.0f};
        g.spawnYaw = opt.spawnYaw;
        resetPosition(g);
    }

    bool paused = false, showHelp = true, running = true;
    int menuSel = 0;
    for (int k = 0; k < 6; ++k)  // match the crosshair colour preset to the loaded config
        if (kCrosshairColors[k] == (uint32_t(cfg.crosshair_r) << 16 | uint32_t(cfg.crosshair_g) << 8 | uint32_t(cfg.crosshair_b)))
            g_crosshairPreset = k;
    if (!automated) SDL_SetWindowRelativeMouseMode(window, true);
    auto setPaused = [&](bool p) {
        paused = p;
        if (!automated) SDL_SetWindowRelativeMouseMode(window, !p);
        g.fireHeld = false;
        g.hudDirty = true;
    };

    int pixW = 0, pixH = 0;
    SDL_GetWindowSizeInPixels(window, &pixW, &pixH);

    const double freq = double(SDL_GetPerformanceFrequency());
    uint64_t last = SDL_GetPerformanceCounter();
    double tickAcc = 0, lastHudBuild = -1;
    FrameStats stats;
    HudBatch hud;
    std::vector<BoxInstance> dynamicBoxes;
    int frame = 0;
    double hitMarkerShownUntil = 0;
    std::vector<ModelDraw> modelDraws;

    while (running) {
        uint64_t frameStart = SDL_GetPerformanceCounter();
        double dt = double(frameStart - last) / freq;
        last = frameStart;
        if (automated && !bench) dt = 1.0 / 240.0;  // deterministic steps for screenshots/tests
        if (bench) {  // a slow turn with the rifle firing on and off, like play
            g.viewYaw = wrapDeg(float(g.viewYaw) + float(dt) * 720.0f / opt.benchSeconds);
            g.fireHeld = std::fmod(benchStats.elapsed, 3.0) < 1.2;
            if (g.fireHeld && g.weapon->ammo == 0) g.reloadLatch = true;
        }
        dt = std::min(dt, 0.25);

        float frameYawDelta = 0, framePitchDelta = 0;  // for weapon sway
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: running = false; break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (!automated) setPaused(true);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    pixW = e.window.data1;
                    pixH = e.window.data2;
                    g.hudDirty = true;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (!paused) {
                        double sens = cfg.sensitivity * zoomSensScale(g, cfg);
                        double dyaw = -double(e.motion.xrel) * sens * cfg.m_yaw;
                        double dpitch = double(e.motion.yrel) * sens * cfg.m_pitch;
                        g.viewYaw += dyaw;
                        g.viewPitch += dpitch;
                        frameYawDelta += float(dyaw);
                        framePitchDelta += float(dpitch);
                        g.viewPitch = std::clamp(g.viewPitch, -89.0, 89.0);
                        if (g.viewYaw > 180) g.viewYaw -= 360;
                        if (g.viewYaw < -180) g.viewYaw += 360;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (paused) { setPaused(false); break; }
                    if (e.button.button == SDL_BUTTON_LEFT) { g.fireHeld = true; g.fireLatch = true; }
                    if (e.button.button == SDL_BUTTON_RIGHT) g.zoomLatch = true;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) g.fireHeld = false;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (!paused) {
                        g.jumpLatch = true;
                    } else if (e.wheel.y != 0 && adjustMenu(cfg, menuSel, e.wheel.y > 0 ? 1 : -1, false)) {
                        settingsChanged();
                        saveConfig(cfgPath, cfg);
                        g.hudDirty = true;
                    }
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    {
                        SDL_Scancode k = e.key.scancode;
                        bool arrow = k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_RIGHT || k == SDL_SCANCODE_UP ||
                                     k == SDL_SCANCODE_DOWN;
                        if (e.key.repeat && !(paused && arrow)) break;  // held arrows repeat in the menu
                    }
                    SDL_Scancode sc = e.key.scancode;
                    if (sc == SDL_SCANCODE_ESCAPE && g.buyMenu && !paused) { g.buyMenu = false; g.hudDirty = true; }
                    else if (sc == SDL_SCANCODE_ESCAPE) setPaused(!paused);
                    else if (paused && sc == SDL_SCANCODE_Q) running = false;
                    else if (paused && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
                        int n = int(menuItems(cfg).size());
                        menuSel = (menuSel + (sc == SDL_SCANCODE_DOWN ? 1 : n - 1)) % n;
                        g.hudDirty = true;
                    } else if (paused && (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT)) {
                        if (adjustMenu(cfg, menuSel, sc == SDL_SCANCODE_RIGHT ? 1 : -1, (e.key.mod & SDL_KMOD_SHIFT) != 0)) {
                            settingsChanged();
                            saveConfig(cfgPath, cfg);
                            g.hudDirty = true;
                        }
                    }
                    else if (sc == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
                        bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(window, !fs);
                    } else if (!paused) {
                        if (sc == SDL_SCANCODE_SPACE) g.jumpLatch = true;
                        else if (sc == SDL_SCANCODE_R) g.reloadLatch = true;
                        else if (g.buyMenu && (sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_2)) {
                            g.primary = sc == SDL_SCANCODE_1 ? &g.rifle : &g.sniper;
                            g.switchTo = 1;  // like buying in CS: you're holding it straight away
                            g.buyMenu = false;
                            g.hudDirty = true;
                        }
                        else if (sc == SDL_SCANCODE_1) g.switchTo = 1;
                        else if (sc == SDL_SCANCODE_2) g.switchTo = 2;
                        else if (sc == SDL_SCANCODE_3) g.switchTo = 3;
                        else if (sc == SDL_SCANCODE_4) g.switchTo = 4;
                        else if (sc == SDL_SCANCODE_Q) g.switchTo = 5;
                        else if (sc == SDL_SCANCODE_F) g.vm.inspect();
                        else if (sc == SDL_SCANCODE_B) { g.buyMenu = !g.buyMenu; g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_F7) {
                            g.mode = (g.mode + 1) % 3;  // practice -> deathmatch -> retakes
                            loadMap(g, renderer, 1);
                            cfg.mode = g.mode;
                            cfg.map = 1;
                            saveConfig(cfgPath, cfg);
                            pushHitLog(g, g.mode == 1 ? "DEATHMATCH" : g.mode == 2 ? "RETAKES" : "PRACTICE", 0x80ff80);
                        }
                        else if (sc == SDL_SCANCODE_G) g.throwLatch = true;
                        else if (sc == SDL_SCANCODE_F4) {
                            g.botsFire = !g.botsFire;
                            g.hp = 100;
                            std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
                            g.hudDirty = true;
                        }
                        else if (sc == SDL_SCANCODE_V) { g.noclip = !g.noclip; g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_F6) resetPosition(g);
                        else if (sc == SDL_SCANCODE_F5) {
                            cfg = loadConfig(cfgPath);
                            settingsChanged();
                            pushHitLog(g, "CONFIG RELOADED", 0x80ff80);
                            g.hudDirty = true;
                        }
                        else if (sc == SDL_SCANCODE_C) renderer.clearDecals();
                        else if (sc == SDL_SCANCODE_F1) { showHelp = !showHelp; g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_F3 && g.mapId == 0) { setDrill(g, !g.drill); g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_F8) {
                            g.mode = 0;  // deathmatch is Dust-only; F8 always goes to practice
                            cfg.mode = 0;
                            loadMap(g, renderer, 1 - g.mapId);
                            cfg.map = g.mapId;
                            saveConfig(cfgPath, cfg);
                        }
                    }
                    break;
                }
                default: break;
            }
        }

        {
            bool tab = !paused && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB];
            if (tab != g.showScores) { g.showScores = tab; g.hudDirty = true; }
        }
        if (!paused) {
            tickAcc += dt;
            while (tickAcc >= kTickDt) {
                simTick(g, opt);
                tickAcc -= kTickDt;
            }
        }
        const uint64_t tSim = SDL_GetPerformanceCounter();
        float alpha = float(tickAcc / kTickDt);
        if (automated && opt.startZoom && frame == 60) { g.zoom = opt.startZoom; g.hudDirty = true; }
        if (automated && opt.throwSmoke && frame == 30) g.throwLatch = true;
        if (automated && opt.bots && frame == 1) g.botsFire = true;
        if (automated && opt.showMenu && frame == 60) { paused = true; menuSel = 2; g.hudDirty = true; }

        for (const BoxInstance& d : g.pendingDecals) renderer.addDecal(d);
        g.pendingDecals.clear();

        // Camera: interpolated position, latest mouse angles, plus partial recoil view punch.
        Vec3 origin = lerp(g.prevPlayer.origin, g.player.origin, alpha);
        float eyeZ = eyeHeight(g.prevPlayer) + (eyeHeight(g.player) - eyeHeight(g.prevPlayer)) * alpha;
        if (cfg.camera_extrapolate && !paused) {
            // Zero-lag camera: draw yourself where you are *now* (latest tick + elapsed time), not
            // interpolated up to one tick (7.8 ms) in the past. Collision-checked, with step-up.
            const PlayerState& p = g.player;
            float t = alpha * kTickDt;
            Vec3 target = p.origin + p.velocity * t;
            if (g.noclip) {
                origin = target;
            } else {
                if (!p.onGround) target.z -= 0.5f * g.moveParams.gravity * t * t;
                Vec3 mins = hullMins(), maxs = hullMaxs(p.ducked);
                TraceResult tr = g.world.traceBox(p.origin, target, mins, maxs);
                if (tr.fraction < 1.0f && p.onGround) {
                    Vec3 up{0, 0, g.moveParams.stepSize};
                    TraceResult a = g.world.traceBox(p.origin + up, target + up, mins, maxs);
                    TraceResult d = g.world.traceBox(a.endpos, a.endpos - up - Vec3{0, 0, 2}, mins, maxs);
                    origin = a.fraction > tr.fraction ? d.endpos : tr.endpos;
                } else {
                    origin = tr.endpos;
                }
            }
            eyeZ = eyeHeight(p);
        }
        if (cfg.view_smooth_steps && !g.noclip && g.player.onGround && g.prevPlayer.onGround) {
            // Ground height comes from the latest tick only; stepSmooth eases the change in.
            origin.z = g.player.origin.z;
            if (!paused) g.stepSmooth *= std::exp(-float(dt) / 0.045f);
        } else {
            g.stepSmooth = 0;
        }
        Vec3 eye = origin + Vec3{0, 0, eyeZ + g.stepSmooth};
        float recoilIdx = g.recoilIndexPrev + (g.weapon->recoilIndex - g.recoilIndexPrev) * alpha;
        RecoilStep punch = g.weapon->def->canFire ? recoilAt(*g.weapon->def, recoilIdx) : RecoilStep{0, 0};
        float camPitch = float(g.viewPitch) - punch.up * cfg.view_recoil_tracking;
        float camYaw = float(g.viewYaw) - punch.right * cfg.view_recoil_tracking;

        float aspect = pixH > 0 ? float(pixW) / float(pixH) : 1.0f;
        float vfov = 2.0f * std::atan(std::tan(zoomFov(g.zoom, cfg.fov) * 0.5f * kDegToRad) * 0.75f);
        if (!paused) g.camRoll *= std::exp(-float(dt) / 0.07f);
        const Mat4 roll = rotationZ(g.camRoll);  // about the view axis: the crosshair stays put
        Mat4 viewProj = perspective(vfov, aspect, 2.0f, 16384.0f) * roll * viewFromAngles(eye, camPitch, camYaw);

        // Dummies at their interpolated positions; remember exactly what we drew for hit tests.
        dynamicBoxes.clear();
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            Vec3 p = lerp(d.prevPos, d.pos, alpha);
            g.lastDummyRenderPos[i] = p;
            const float shownYaw = wrapDeg(d.prevYaw + wrapDeg(d.yaw - d.prevYaw) * alpha);
            g.dummies[i].shownYaw = shownYaw;  // shots test against exactly this facing
            const float turn = (shownYaw - 180.0f) * kDegToRad;
            // Dead dummies collapse to the floor (cosmetic; they are no longer hittable).
            float squash = 1.0f;
            if (!d.alive()) {
                float t = d.deadFor;
                if (t > 0.6f) continue;
                squash = std::max(0.06f, 1.0f - t / 0.22f);
            }
            for (const Hitbox& hb : dummyHitboxes()) {
                uint32_t tint = hb.group == kHead ? 0xe8b98c : hb.group == kChest ? 0x2f4f8a
                              : hb.group == kStomach ? 0x24365e : 0x22252b;
                uint32_t col = lerpColor(tint, 0xffffff, std::min(1.0f, d.flash[hb.group] / 0.15f));
                Vec3 mn = hb.mins, mx = hb.maxs;
                mn.z *= squash;
                mx.z *= squash;
                dynamicBoxes.push_back(makeBox(p + mn, p + mx, col, false));
                yawBox(dynamicBoxes.back(), p, turn);
            }
            auto part = [&](Vec3 mn, Vec3 mx, uint32_t c) {
                mn.z *= squash;
                mx.z *= squash;
                dynamicBoxes.push_back(makeBox(p + mn, p + mx, c, false));
                yawBox(dynamicBoxes.back(), p, turn);
            };
            part({-4.8f, -4.8f, 65.5f}, {4.8f, 4.8f, 69.6f}, 0x3d4a2c);   // helmet
            part({-5.0f, -3.5f, 63.0f}, {-4.0f, 3.5f, 65.0f}, 0x1a1c20);   // visor band (faces -X)
            part({-6.9f, -7.5f, 47.0f}, {-5.6f, 7.5f, 57.0f}, 0x3a4530);   // vest plate
            part({-6.2f, -9.2f, 44.0f}, {6.2f, 9.2f, 46.0f}, 0x2b2117);    // belt
            part({-5.4f, -8.3f, 0.0f}, {5.4f, 8.3f, 6.0f}, 0x1d1a17);      // boots
            part({-16.0f, -1.4f, 46.0f}, {-6.6f, 1.4f, 49.5f}, 0x1e2024);  // rifle body
            part({-27.0f, -0.6f, 47.2f}, {-16.0f, 0.6f, 48.4f}, 0x111214); // rifle barrel
            part({-13.3f, -13.2f, 46.0f}, {-10.0f, -10.3f, 49.5f}, 0xe8b98c);  // right hand
            part({-15.3f, 10.3f, 46.0f}, {-12.0f, 13.2f, 49.5f}, 0xe8b98c);    // left hand
        }
        g.lastRenderEye = eye;

        // Cosmetic updates at frame rate.
        float fdt = float(dt);
        g.fx.update(paused ? 0.0f : fdt);
        g.fx.appendParticles(dynamicBoxes);
        for (const Game::Nade& n : g.nades)
            dynamicBoxes.push_back(makeBox(n.pos - Vec3{1.5f, 1.5f, 1.5f}, n.pos + Vec3{1.5f, 1.5f, 2.5f}, 0x3b4a2f, false));
        for (const Game::Smoke& sm : g.smokes) {
            // Deterministic puffs; grows in over 0.6 s and fades over the last 1.5 s.
            float k = smokeGrow(sm, g.simTime + tickAcc);
            if (k <= 0) continue;
            for (uint32_t pi = 0; pi < 44; ++pi) {
                uint32_t hsh = (pi + 1) * 2654435761u;
                float a = float(hsh % 6283) / 1000.0f, rr = float((hsh >> 8) % 1000) / 1000.0f;
                float zz = float((hsh >> 16) % 1000) / 1000.0f;
                float rad = kSmokeRadius * 0.8f * std::sqrt(rr) * k;
                Vec3 c = sm.pos + Vec3{std::cos(a) * rad, std::sin(a) * rad, 20.0f + zz * 110.0f * k};
                float sz = (34.0f + float((hsh >> 4) % 26)) * k;
                uint32_t shade = 0xbcc0c6 + ((hsh >> 12) % 3) * 0x060606;
                dynamicBoxes.push_back(makeBox(c - Vec3{sz, sz, sz * 0.8f}, c + Vec3{sz, sz, sz * 0.8f}, shade, false));
            }
        }
        {
            const WeaponState& ws = *g.weapon;
            double reloadProgress =
                ws.reloadEndTime >= 0 ? g.simTime + tickAcc - (ws.reloadEndTime - ws.def->reloadTime) : -1.0;
            g.vm.update({paused ? 0.0f : fdt, frameYawDelta, framePitchDelta, length2d(g.player.velocity),
                         g.player.onGround, float(reloadProgress), ws.def->reloadTime});
        }

        const uint64_t tScene = SDL_GetPerformanceCounter();
        renderer.beginFrame(pixW, pixH);
        renderer.drawBoxes(viewProj, eye, dynamicBoxes);
        modelDraws.clear();
        g.fx.appendTracers(modelDraws);
        for (const ModelDraw& md : modelDraws) renderer.drawModel(viewProj, md.model, md.boxes);

        // First-person weapon: own FOV and fresh depth so it never clips into walls.
        if (cfg.show_viewmodel && g.zoom == 0) {
            float vmVfov = 2.0f * std::atan(std::tan(cfg.viewmodel_fov * 0.5f * kDegToRad) * 0.75f);
            Mat4 vmViewProj = perspective(vmVfov, aspect, 0.5f, 256.0f) * roll * viewFromAngles(eye, camPitch, camYaw);
            modelDraws.clear();
            g.vm.build(eye, camPitch, camYaw, cfg.viewmodel_offset_x, cfg.viewmodel_offset_y, cfg.viewmodel_offset_z,
                       cfg.viewmodel_bob, modelDraws);
            renderer.clearDepth();
            for (const ModelDraw& md : modelDraws) renderer.drawModel(vmViewProj, md.model, md.boxes);
        }

        const uint64_t tDraw = SDL_GetPerformanceCounter();
        // HUD: rebuild at most ~60 Hz unless something changed (keeps uploads tiny at 1000+ FPS).
        double nowSec = double(frameStart) / freq;
        bool markerExpired = hitMarkerShownUntil > 0 && g.simTime >= g.hitMarkerUntil;
        bool rebuild = g.hudDirty || markerExpired || nowSec - lastHudBuild > 1.0 / 60.0;
        if (rebuild) {
            buildHud(hud, g, cfg, stats, pixW, pixH, paused, showHelp, menuSel);
            lastHudBuild = nowSec;
            g.hudDirty = false;
            hitMarkerShownUntil = g.simTime < g.hitMarkerUntil ? g.hitMarkerUntil : 0;
        }
        renderer.drawHud(hud, rebuild);

        const uint64_t tHud = SDL_GetPerformanceCounter();
        if (bench) glFinish();  // so GPU time shows up as GPU, not inside the next frame
        const uint64_t tGpu = SDL_GetPerformanceCounter();
        if (!opt.screenshotPath.empty() && frame == opt.screenshotFrame) {
            bool ok = renderer.screenshot(opt.screenshotPath);
            std::fprintf(stderr, "screenshot %s: %s (shots %d, hits %d)\n", opt.screenshotPath.c_str(),
                         ok ? "ok" : SDL_GetError(), g.shots, g.hits);
            running = false;
        }

        SDL_GL_SwapWindow(window);
        ++frame;
        if (bench && frame > 30) {  // skip warm-up frames
            const uint64_t tSwap = SDL_GetPerformanceCounter();
            auto ms = [&](uint64_t a, uint64_t b) { return double(b - a) * 1000.0 / freq; };
            BenchStats& b = benchStats;
            b.sim += ms(frameStart, tSim);
            b.scene += ms(tSim, tScene);
            b.draw += ms(tScene, tDraw);
            b.hud += ms(tDraw, tHud);
            b.gpu += ms(tHud, tGpu);
            b.swap += ms(tGpu, tSwap);
            b.frames.push_back(float(ms(frameStart, tSwap)));
            b.dynBoxes += dynamicBoxes.size();
            b.elapsed += dt;
            if (b.elapsed >= opt.benchSeconds) {
                size_t n = b.frames.size();
                std::vector<float> sorted = b.frames;
                std::sort(sorted.begin(), sorted.end());
                double total = 0;
                for (float f : b.frames) total += f;
                std::string out = std::string(base ? base : "") + "bench.txt";
                char report[640];
                std::snprintf(report, sizeof(report),
                              "GPU: %s\nresolution %dx%d  msaa %d  map %d  mode %d  bots %zu  static boxes %zu\n"
                              "frames %zu  avg %.0f fps (%.3f ms)  1%% low %.0f fps (%.3f ms)  dynamic boxes/frame %.0f\n"
                              "ms per frame:  input+sim %.3f  scene %.3f  draw calls %.3f  hud %.3f  gpu %.3f  swap %.3f\n",
                              reinterpret_cast<const char*>(glGetString(GL_RENDERER)), pixW, pixH, std::clamp(cfg.msaa, 0, 8),
                              g.mapId, g.mode, g.dummies.size(), g.world.solids.size(), n, 1000.0 * double(n) / total,
                              total / double(n), 1000.0 / double(sorted[n * 99 / 100]), double(sorted[n * 99 / 100]),
                              double(b.dynBoxes) / double(n), b.sim / double(n), b.scene / double(n), b.draw / double(n),
                              b.hud / double(n), b.gpu / double(n), b.swap / double(n));
                std::ofstream(out) << report;
                running = false;
            }
        }

        if (cfg.fps_max > 0 && !automated) {
            double target = 1.0 / cfg.fps_max;
            double spent = double(SDL_GetPerformanceCounter() - frameStart) / freq;
            if (spent < target) SDL_DelayPrecise(uint64_t((target - spent) * 1e9));
        }
        stats.push(float(dt));
    }

    audio.shutdown();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
