// Crisp: single-player movement + shooting prototype.
// Fixed 128 Hz simulation, uncapped rendering with interpolation, raw mouse input.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
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
#include "items.h"
#include "stats.h"
#include "replay.h"
#include "movement.h"
#include "net.h"
#include "upnp.h"
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
    int startWeapon = 0;                         // --weapon 1..14 (4 AWP, 5 grenade, 6 Berettas, 7 Deagle, 8 Nova, 9 MAC-10,
                                                 // 10 M4A1-S, 11 Galil AR, 12 SSG 08, 13 UMP-45, 14 XM1014)
    int startZoom = 0;                           // --zoom 1|2 (sniper scope, for screenshots)
    int menuScreen = 0;                          // --menu N: open menu screen N (screenshots)
    int menuRow = 0;                             // --menu-row N: with the row N highlighted
    bool throwSmoke = false, bots = false;       // --smoke [frame N: --throw-frame N], --bots (for screenshots)
    int throwFrame = 30;
    float benchSeconds = 0;                      // --bench S: timed run at real speed, writes bench.txt
    bool benchRaw = false;
    std::string careerFile;                      // --career FILE: show this career (screenshots; never written)                       // --bench-raw S: the same without glFinish (frames queue like play)
    int inspectFrame = -1;                       // --inspect N: start an inspect on frame N (screenshots)
    int nadeType = 0;                            // --nade T: grenade type for --smoke (0 smoke .. 3 molotov)
    int dieFrame = -1;                           // --die N: you die on frame N (screenshots of death / spectating)
    int dieBy = -2;                              // --killer I: bot I killed you (--die; shows the killcam)
    int replayFrame = -1;                        // --replay-frame N: open the replay viewer on frame N
    int giveCases = 0;                           // --give-cases N: N cases to open (tests)
    int buyCat = -1;                             // --buy N: the buy wheel open on category N (0 = the categories)
    bool startCT = false;                        // --ct: competitive starts with you on CT (testing)
    bool netHost = false;                        // --host: host an online game straight away
    std::string netJoin;                         // --join ADDR: join one
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
        } else if (a == "--throw-frame") {
            o.throwFrame = std::atoi(next());
        } else if (a == "--bots") {
            o.bots = true;
        } else if (a == "--nade") {
            o.nadeType = std::atoi(next());
        } else if (a == "--host") {
            o.netHost = true;
        } else if (a == "--join") {
            o.netJoin = next();
        } else if (a == "--ct") {
            o.startCT = true;
        } else if (a == "--die") {
            o.dieFrame = std::atoi(next());
        } else if (a == "--killer") {
            o.dieBy = std::atoi(next());
        } else if (a == "--replay-frame") {
            o.replayFrame = std::atoi(next());
        } else if (a == "--give-cases") {
            o.giveCases = std::atoi(next());
        } else if (a == "--buy") {
            o.buyCat = std::atoi(next());

        } else if (a == "--inspect") {
            o.inspectFrame = std::atoi(next());
        } else if (a == "--career") {
            o.careerFile = next();
        } else if (a == "--bench" || a == "--bench-raw") {
            o.benchRaw = a == "--bench-raw";
            o.benchSeconds = float(std::atof(next()));
        } else if (a == "--menu") {
            o.menuScreen = std::atoi(next());
        } else if (a == "--menu-row") {
            o.menuRow = std::atoi(next());
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
    WeaponState guns[kWeaponCount];  // by WeaponId
    // CS-style slots: 1 = primary (rifle, sniper, Nova or MAC-10, picked in the buy menu), 2 pistol (pistol,
    // Berettas or Deagle), 3 knife, 4 grenades. Q = previous weapon.
    WeaponState* primary = &guns[kWRifle];
    WeaponState* secondary = &guns[kWPistol];
    int buyCategory = 0;          // buy menu: 0 the categories, else the one open (BuyCategory: 1 pistols .. 7 grenades)
    float buyMouseX = 0, buyMouseY = 0;  // buy wheel: the mouse (pixels; it's free while the wheel is open)
    WeaponState* lastWeapon = &guns[kWKnife];
    bool buyMenu = false;
    float throwStrength = 1.0f;   // the next throw: 1 full, kNadeMedium, kNadeLob (see grenadeThrowVelocity)
    int nadeHold = 0;             // grenade out, pin pulled: buttons held so far (1 Mouse 1, 2 Mouse 2)
    bool zoomHeld = false;        // Mouse 2 is down
    double grenadeReturnAt = -1;  // after a throw, switch back to the previous weapon
    // Sniper scope: 0 = unscoped, 1 = 40 FOV, 2 = 15 FOV. Like CS, a shot takes the scope down and it comes
    // back up by itself once the bolt has cycled (resumeZoom at resumeZoomAt).
    int zoom = 0, resumeZoom = 0;
    float shownFov = 0;  // the FOV drawn: eases into a zoom over a few frames, snaps straight out
    double resumeZoomAt = -1, boltAt = -1;
    bool zoomLatch = false;
    bool autoHop = true;
    Vec3 spawn;
    float spawnYaw = 0;
    bool noclip = false;
    bool jumpNeedsRelease = false;  // just spawned: Space has to come up before it jumps again
    WeaponState* weapon = &guns[kWRifle];
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

    // Bots shoot back (Esc menu). They aim at where you were 0.2 s ago: move and they miss.
    bool botsFire = false;
    float hp = 100;
    int deaths = 0;
    double hurtUntil = 0, deadUntil = -1;
    int spec = -1;  // competitive, dead: the teammate (bot index) you're watching, -1 = flying free
    // Where your last few hits came from (the red arcs round the crosshair).
    Vec3 hurtFrom[4];
    double hurtAt[4] = {-1, -1, -1, -1};
    int hurtNext = 0;
    std::vector<float> botSeen, botCooldown;
    Vec3 eyeHistory[64];
    int histHead = 0;

    // Grenades. Slot 4 holds one type at a time (press 4 again to cycle, like CS); G quick-throws it.
    enum NadeType { kSmokeNade = 0, kFlashNade, kHeNade, kMolotov, kNadeTypes };
    // ticks in flight (fuse); owner: who threw it (-1 you, else a dummy: a bot, or online a player)
    struct Nade { Vec3 pos, vel; int type = kSmokeNade; int ticks = 0; int owner = -1; };
    struct Smoke { Vec3 pos; double start; };
    struct Fire { Vec3 pos; double start, nextTick; int owner = -1; };
    std::vector<Fire> fires;
    int nadeType = kSmokeNade;
    double flashFull = 0, flashEnd = 0;  // flashed: white until flashFull, fading out until flashEnd
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
    int compMates = 4, compEnemies = 5, mateSkill = 1, enemySkill = 2;  // competitive teams, bot skill (config)
    int skillVariance = 0;        // 0 off, 1 slight, 2 wide (config skill_variance)
    bool dmBotFights = true;      // deathmatch: the bots fight each other (config dm_bot_fights)
    double freezeTime = 15;                 // competitive freeze time, seconds (config freeze_time)
    // Online: deathmatch (mode 5) or competitive (mode 3 with `online`). Players are dummies[id] (id = their
    // net id; yours stays hidden); in competitive the host's bots are dummies 8..17. Their states are played
    // back a few ticks behind the newest, interpolated, so they move smoothly through jitter.
    Net net;
    PortOpener ports;  // hosting: the router's port, and the addresses to give friends
    bool online = false;
    struct Remote {
        NetState snaps[32];
        bool present = false;
        uint32_t latest = 0;
        double play = -1;  // the tick being shown
    };
    Remote remotes[kNetSlots];
    uint32_t netTick = 0;
    int netTeam[kNetSlots] = {};       // online competitive: each slot's team (0 A = the host's, 1 B, -1 not playing)
    int netSpawn[kNetMaxPlayers] = {}; // host: each player's spawn spot this round
    bool plantSent = false, defuseSent = false;  // joined: you told the host (waiting for it to say so)
    int noiseSide = 0;                 // the side that made the last noise (competitive: only enemies react)
    bool netJoining = false;
    uint16_t netPort = kNetDefaultPort;
    bool inputBlocked = false;  // a menu is open over a game that keeps running (online)
    double rtRoundEnd = 0, rtResultUntil = -1;
    bool rtResultWin = false;
    const char* rtResultText = "";
    // Competitive (mode 3): you + 4 bots against 5 bots on Dust, MR12 (first to 13, sides swap after 12),
    // CS economy, buy menu, the bomb. Bots 0..3 are your team, 4..8 the enemy.
    struct Comp {
        int youTeam = 0;                    // 0 = T, 1 = CT
        int youScore = 0, themScore = 0, round = 0;
        int phase = 0;                      // 0 freeze/buy, 1 live, 2 round over, 3 match over
        double phaseEnd = 0, buyUntil = 0;
        double roundStartAt = 0;  // (the replay viewer starts a round's replay here)
        int money = 800;
        std::vector<int> botMoney;
        std::vector<uint8_t> botGun;        // the gun each bot holds (a WeaponId; kWPistol when it has no primary)
        std::vector<int> ctNade;            // a CT bot's utility for the execute (molotov / HE), -1 none
        std::vector<double> nadeTry;        // (when it may next try to find a throw)
        std::vector<char> saving;           // a bot whose round is lost: back to spawn to keep its gun
        bool defuseHeard = false;           // the Ts heard the defuse start and came for it
        int lossStreak[2] = {0, 0};
        float armor = 0;
        bool helmet = false, kit = false, youDead = false;
        int ownPrimary = -1;                // the primary you bought (WeaponId), -1 none; the pistol slot is
                                            // g.secondary (the starting pistol unless you bought another)
        int nades[4] = {0, 0, 0, 0};
        int siteTarget = 0;                 // the site the Ts go for this round
        Vec3 stagePoint;                    // where the T bots gather before they execute
        bool executing = false, rotated = false;  // rotated: the CTs heard the site get hit
        double executeAt = 0;
        const char* lastSpot = "";          // teammate radio: the last place an enemy was called out
        double lastSpotAt = -100;
        int route = 0;                      // the main group's way in: 0 A long, 1 A short (catwalk), 2 B tunnels, 3 mid to B
        // The round's plays. Ts: a staging point per bot (stages, stageOf), then the execute. A rush goes as
        // soon as anyone reaches their point; a fake has the fakers throw utility at the other site first and
        // the team goes a few seconds later (goAt); a default spreads out for map control and executes late.
        // CTs: now and then one pushes for an early pick (pushers) and falls back to its spot (pushBack) at
        // pushUntil or when the Ts execute.
        int play = 0;
        std::vector<Vec3> stages;
        std::vector<int> stageOf;
        std::vector<char> faker;
        bool rush = false, sent = false;
        int fakeRoute = -1;
        double goAt = 0;
        std::vector<int> pushers;
        std::vector<RetakeSpot> pushBack;
        double pushUntil = -1;
        // The execute's utility: a T bot throws `type` at `target` once it has a throw that gets there.
        struct Throw { int bot; int type; Vec3 target; double from, until, nextTry; };
        std::vector<Throw> throws;
        int carrier = -3;                   // bomb: -1 you, i = bot i, -2 dropped on the floor, -3 nobody
        Vec3 dropped;
        int planter = -3, defuser = -3;     // who is planting / defusing (-1 you, bot index)
        double plantStart = -1, botDefuseStart = -1;
        bool planted = false;
        const char* resultText = "";
        bool resultWin = false;
    } comp;
    std::vector<int> team;                  // per bot: 0 = T, 1 = CT (competitive)

    // The planted bomb (retakes): 40 s fuse, hold E beside it for 5 s to defuse (you have a kit).
    Vec3 bombPos;
    bool bombActive = false, defuseHeld = false;
    double bombExplodeAt = 0, defuseStart = -1, nextBeep = 0;
    double dmEnd = 0, dmOverUntil = -1, spawnProtectUntil = 0;
    int dmShownSecs = -1, dmShownPhase = -1;

    // Combat record: kill feed, damage report and scoreboard. Agent ids: -1 = you, i = bot i.
    struct Stats { int kills = 0, deaths = 0, assists = 0, hsKills = 0, mvps = 0, roundKills = 0; float damage = 0; };
    Stats you;
    std::vector<Stats> botStats;
    struct FeedEntry { std::string text; uint32_t color; double time; };
    std::deque<FeedEntry> feed;
    std::vector<float> dmgGiven, dmgTaken;  // this life, per bot (the damage report when you die)
    std::vector<int> hitsGiven, hitsTaken;
    std::vector<float> dmgTable;            // [(victim + 1) * (n + 1) + attacker + 1]: damage this life (assists)
    std::vector<std::string> report;
    double reportUntil = -1;

    // Radar: the map's floor merged into rectangles (shaded by height), and when each bot was last spotted.
    struct RadarRect { float x0, y0, x1, y1; uint32_t rgba; };
    std::vector<RadarRect> radarRects;
    Vec3 radarMin, radarMax;
    std::vector<double> spottedUntil;
    bool showRadar = true;
    Vec3 noisePos;                // last sound you made (footsteps, shots) that bots can hear
    double noiseAt = -100;
    float noiseRadius = 0;
    bool showScores = false;      // Tab held

    // Cosmetic: camera height offset that eases the view over stairs and stepped ramps.
    float stepSmooth = 0;
    float camRoll = 0;        // spray feedback: camera roll in degrees (around the crosshair)
    // Replays (replay.h): the last few minutes, 64 frames a second, and every shot. `rv` is what's playing: the
    // killcam (the last moments through your killer's eyes, while the game goes on) or the replay viewer (pause
    // menu: the game waits; seek, speed, anyone's eyes or a free camera).
    Replay replay;
    struct ReplayView {
        bool on = false, killcam = false, paused = false;
        double t = 0, end = 0;   // the time playing; the killcam stops at `end`
        float speed = 1;
        int pov = -1;            // whose eyes: an agent (you = the last one), or -1: the free camera
        Vec3 camPos;
        float camYaw = 0, camPitch = 0;
        double shotsDone = 0;    // the shots up to here have been played
        int killer = -1;
    } rv;
    int killcamKiller = -1;      // a bot just killed you: its killcam starts at killcamAt
    double killcamAt = -1;
    bool killcamOn = true;       // (config killcam)
    int replayTicks = 0;
    size_t replayAgents = 0;
    Effects replayFx;            // the replay's tracers (the live game's keep going under a killcam)
    Vec3 flashPos;            // muzzle light: where the last (brightest) shot was, and how bright it still is
    float flashLight = 0;
    float fovPunch = 0;       // shot thump: the view widens a touch for a moment (centre stays put)
    bool viewShake = true;
    const char* callout = "";  // Dust area name under the player (HUD)

    // KZ course timer.
    int kzState = 0;  // 0 idle, 1 on start pad, 2 running
    double kzStart = 0, kzLast = -1, kzBest = -1;
    // Prefire practice (mode 4): clear a route's bots as fast as you can. The clock starts when you move
    // or shoot; a result shows for a few seconds, then the route resets.
    struct Prefire {
        int route = 0;
        double start = -1, resultUntil = -1;
        float last = 0;
        bool won = false;
        float best[8] = {};  // best time per route this session (0 = none yet)
    } pf;
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

// Online roles. The host runs the bots and the match; a joined game shows them and runs only its player.
bool netHost(const Game& g) { return g.online && g.net.isHost(); }
bool netClient(const Game& g) { return g.online && !g.net.isHost(); }
// A dummy this game's bot AI drives: every one offline; online, only the host's bots (8..17), not players.
bool isBot(const Game& g, size_t i) { return !g.online || i >= size_t(kNetMaxPlayers); }
// Net ids <-> this game's agent ids (-1 is you; everyone else keeps their slot number).
int netToLocal(const Game& g, int id) { return id == g.net.myId() ? -1 : id; }
uint8_t localToNet(const Game& g, int id) { return uint8_t(id < 0 ? g.net.myId() : id); }

// Each gun's shot: its own recording (assets/sounds; without one, a shared sound pitched: audio.cpp), the pitch it
// plays at, and how loud your own is.
struct GunSound { Sfx sfx; float pitch, gain; };
GunSound gunSound(int id) {
    switch (id) {
        case kWPistol: return {Sfx::SuppressedShot, 1.08f, 1.3f};
        case kWM4A1S: return {Sfx::SuppressedRifle, 0.96f, 1.3f};
        case kWSniper: return {Sfx::SniperShot, 1.0f, 1.9f};
        case kWSsg08: return {Sfx::SsgShot, 1.0f, 1.7f};
        case kWNova: return {Sfx::ShotgunShot, 1.0f, 1.9f};
        case kWXm1014: return {Sfx::XmShot, 1.0f, 1.8f};
        case kWDeagle: return {Sfx::DeagleShot, 0.88f, 1.9f};  // a revolver's boom, a touch deeper
        case kWBerettas: return {Sfx::BerettasShot, 1.0f, 1.45f};
        case kWMac10: return {Sfx::Mac10Shot, 1.0f, 1.6f};
        case kWUmp45: return {Sfx::UmpShot, 1.0f, 1.6f};
        case kWGalil: return {Sfx::GalilShot, 1.0f, 1.8f};
        default: return {Sfx::RifleShot, 1.0f, 1.8f};
    }
}
bool suppressedGun(int id) { return id == kWPistol || id == kWM4A1S; }

// Someone else's shot, heard at `ear`: far away a gunshot is mostly echo (muffled, no crack); the suppressed guns
// don't carry that far.
void gunshot3D(Game& g, int w, const Vec3& at, const Vec3& ear, float earYaw, float gain = 1.1f) {
    if (!g.audio) return;
    const GunSound gs = gunSound(w);
    const bool far = !suppressedGun(w) && length(at - ear) > 1400.0f;
    g.audio->play3D(far ? Sfx::RifleShotFar : gs.sfx, at, ear, earYaw, suppressedGun(w) ? 2200.0f : far ? 6500.0f : 4000.0f,
                    far ? gain * 1.2f : gain, gs.pitch);
}

// The view model for a weapon id (and back).
ViewWeapon viewWeaponFor(int id) {
    switch (id) {
        case kWPistol: return ViewWeapon::Pistol;
        case kWSniper: return ViewWeapon::Sniper;
        case kWGrenade: return ViewWeapon::Grenade;
        case kWKnife: return ViewWeapon::Knife;
        case kWBerettas: return ViewWeapon::Berettas;
        case kWDeagle: return ViewWeapon::Deagle;
        case kWNova: return ViewWeapon::Nova;
        case kWMac10: return ViewWeapon::Mac10;
        case kWM4A1S: return ViewWeapon::M4A1S;
        case kWXm1014: return ViewWeapon::Xm1014;
        case kWGalil: return ViewWeapon::Galil;
        case kWSsg08: return ViewWeapon::Ssg08;
        case kWUmp45: return ViewWeapon::Ump45;
        default: return ViewWeapon::Rifle;
    }
}
ViewWeapon viewWeaponOf(const Game& g) { return viewWeaponFor(g.weapon->def->id); }

WeaponState& weaponState(Game& g, int id) { return g.guns[id >= 0 && id < kWeaponCount ? id : kWRifle]; }

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
// Footstep sound for whatever is under these feet: wood (crates, doors), metal (container, car) or grit.
Sfx stepSoundAt(const Game& g, const Vec3& feet) {
    TraceResult tr = g.world.traceBox(feet + Vec3{0, 0, 2}, feet - Vec3{0, 0, 6}, hullMins(), hullMaxs(false));
    if (tr.box < 0) return Sfx::Footstep;
    uint8_t m = g.world.solids[size_t(tr.box)].material;
    return m == kMatWood ? Sfx::FootstepWood : m == kMatMetal ? Sfx::FootstepMetal : Sfx::Footstep;
}

// `side`: who made it (competitive: only the other side reacts); -1 = you.
void makeNoise(Game& g, const Vec3& pos, float radius, int side = -1) {
    if (g.noiseAt == g.simTime && g.noiseRadius >= radius) return;
    g.noisePos = pos;
    g.noiseAt = g.simTime;
    g.noiseRadius = radius;
    g.noiseSide = side < 0 ? g.comp.youTeam : side;
}

// ---- Combat record ----
void addMoney(Game& g, int id, int amount);
void countCaseKill(Game& g);
int nextTeammate(const Game& g, int from, int dir);

bool g_onlineNames = false;  // online: dummies are players, not bots
std::string g_playerNames[kNetMaxPlayers];  // online: everyone's names (by net id)
// Bots get generic first names; a different set each match (the offset is picked when a map loads).
const char* const kBotNames[] = {"ALEX", "BLAKE", "CASEY", "DANNY", "ELLIOT", "FINN", "GRANT", "HUGO",
                                 "IVAN", "JACK", "KYLE", "LEO", "MAX", "NOAH", "OSCAR", "PETE",
                                 "QUINN", "RYAN", "SAM", "TOMMY", "VINCE", "WADE", "ZACK", "BEN",
                                 "COLE", "DEAN", "EVAN", "FRANK", "GUS", "HANK", "JOE", "LUKE"};
constexpr int kBotNameCount = int(sizeof(kBotNames) / sizeof(kBotNames[0]));
int g_botNameOffset = 0;

// A bot's skill: its side's level (your competitive teammates' or everyone else's), plus, with skill variance,
// its own fixed bit for the match: a little better or a little worse (slight: up to half a level either way,
// wide: up to a whole level). Seeded by the bot and the match, so it doesn't change mid-match.
BotSkill botSkillOf(const Game& g, size_t i) {
    const bool comp = g.mode == 3 && g.mapId == 1;
    const int level = comp && i < g.team.size() && g.team[i] == g.comp.youTeam ? g.mateSkill : g.enemySkill;
    float off = 0;
    if (g.skillVariance > 0) {
        uint32_t h = uint32_t(i + 1) * 2654435761u ^ uint32_t(g_botNameOffset + 7) * 2246822519u;
        h = (h ^ (h >> 15)) * 0x2c1b3c6du;
        h ^= h >> 12;
        off = (float(h & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * (g.skillVariance == 1 ? 0.5f : 1.0f);
    }
    return skillAt(float(level) + off);
}

std::string agentName(int id) {
    if (id < 0) return "YOU";
    if (g_onlineNames && id < kNetMaxPlayers) {  // (online competitive: 8.. are the host's bots)
        if (id < kNetMaxPlayers && !g_playerNames[id].empty()) return g_playerNames[id];
        char b[16];
        std::snprintf(b, sizeof(b), "PLAYER %d", id + 1);
        return b;
    }
    return kBotNames[(id + g_botNameOffset) % kBotNameCount];
}

// Online: weapons as a byte on the wire (the WeaponId; grenade damage is "GRENADE").
uint8_t netWeaponCode(const char* name) {
    const std::string n = name ? name : "";
    for (int id = 0; id < kWeaponCount; ++id)
        if (id != kWGrenade && n == weaponDef(id).name) return uint8_t(id);
    return kWGrenade;
}
const char* netWeaponName(uint8_t code) {
    return code == kWGrenade || code >= kWeaponCount ? "GRENADE" : weaponDef(code).name;
}

Game::Stats& statsOf(Game& g, int id) { return id < 0 ? g.you : g.botStats[size_t(id)]; }
const Game::Stats& statsOf(const Game& g, int id) { return id < 0 ? g.you : g.botStats[size_t(id)]; }

// Fresh scores and per-life tallies, sized for the current bots.
void resetRecord(Game& g) {
    size_t n = g.dummies.size();
    g.you = {};
    g.botStats.assign(n, {});
    g.dmgGiven.assign(n, 0.0f);
    g.dmgTaken.assign(n, 0.0f);
    g.hitsGiven.assign(n, 0);
    g.hitsTaken.assign(n, 0);
    g.dmgTable.assign((n + 1) * (n + 1), 0.0f);
    g.feed.clear();
    g.reportUntil = -1;
}

// Every hit goes through here: damage (ADR), the damage report, assists (41+ damage, like CS), kills
// (kill feed, HS%). `amount` is what the victim actually lost (capped at their remaining HP).
void recordDamage(Game& g, int attacker, int victim, float amount, bool head, const char* weapon, bool wallbang,
                  bool kill) {
    const size_t n = g.dummies.size() + 1;
    if (g.botStats.size() + 1 != n) resetRecord(g);
    statsOf(g, attacker).damage += amount;
    if (attacker < 0 && victim >= 0) { g.dmgGiven[size_t(victim)] += amount; g.hitsGiven[size_t(victim)]++; }
    if (victim < 0 && attacker >= 0) { g.dmgTaken[size_t(attacker)] += amount; g.hitsTaken[size_t(attacker)]++; }
    float* row = &g.dmgTable[size_t(victim + 1) * n];
    row[attacker + 1] += amount;
    if (!kill) return;
    Game::Stats& k = statsOf(g, attacker);
    k.kills++;
    k.roundKills++;
    if (head) k.hsKills++;
    statsOf(g, victim).deaths++;
    for (size_t a = 0; a < n; ++a)
        if (int(a) - 1 != attacker && row[a] >= 41.0f) statsOf(g, int(a) - 1).assists++;
    std::fill(row, row + n, 0.0f);
    if (attacker == -1 && victim >= 0) countCaseKill(g);  // towards your next case
    if (netHost(g) && victim >= kNetMaxPlayers)  // the host's bots: everyone hears how they died
        g.net.sendDeath(uint8_t(victim), localToNet(g, attacker), head, netWeaponCode(weapon));
    if (g.mode == 3) {  // kill reward by weapon (CS: sniper 100, SMG 600, shotgun 900, the rest 300)
        const uint8_t code = netWeaponCode(weapon);
        addMoney(g, attacker, code == kWGrenade ? 300 : weaponDef(code).killReward);
    }
    std::string t = agentName(attacker) + "  [" + weapon + "]  " + agentName(victim);
    if (head) t += "  HS";
    if (wallbang) t += "  WALLBANG";
    g.feed.push_back({t, attacker < 0 ? 0xFFFFFFu : victim < 0 ? 0xFF6060u : 0xB4B4B4u, g.simTime});
    while (g.feed.size() > 5) g.feed.pop_front();
}

// When you die: who you damaged and who damaged you this life (CS prints this in the console).
void buildDamageReport(Game& g) {
    g.report.clear();
    auto section = [&](const char* title, const std::vector<float>& dmg, const std::vector<int>& hits) {
        g.report.push_back(title);
        bool any = false;
        for (size_t i = 0; i < dmg.size(); ++i) {
            if (!hits[i]) continue;
            char line[64];
            std::snprintf(line, sizeof(line), "  %-8s %3d IN %d HIT%s", agentName(int(i)).c_str(), int(dmg[i] + 0.5f),
                          hits[i], hits[i] == 1 ? "" : "S");
            g.report.push_back(line);
            any = true;
        }
        if (!any) g.report.push_back("  NONE");
    };
    section("DAMAGE GIVEN", g.dmgGiven, g.hitsGiven);
    section("DAMAGE TAKEN", g.dmgTaken, g.hitsTaken);
    g.reportUntil = g.simTime + 4.0;
    std::fill(g.dmgGiven.begin(), g.dmgGiven.end(), 0.0f);
    std::fill(g.dmgTaken.begin(), g.dmgTaken.end(), 0.0f);
    std::fill(g.hitsGiven.begin(), g.hitsGiven.end(), 0);
    std::fill(g.hitsTaken.begin(), g.hitsTaken.end(), 0);
}

// Respawn: full magazines, no reload in progress, recoil reset, unscoped.
void refillAmmo(Game& g) {
    for (WeaponState& w : g.guns) {
        if (!w.def || !w.def->canFire) continue;
        w.ammo = w.def->magSize;
        w.reloadEndTime = -1;
        w.recoilIndex = 0;
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
    g.rtBots = std::clamp(cfg.rt_bots, 1, 6);
    g.compMates = std::clamp(cfg.comp_mates, 0, 4);  // next match
    g.freezeTime = std::clamp(cfg.freeze_time, 3, 30);  // next round
    g.compEnemies = std::clamp(cfg.comp_enemies, 1, 5);
    g.mateSkill = std::clamp(cfg.mate_skill, 0, 3);  // straight away
    g.enemySkill = std::clamp(cfg.enemy_skill, 0, 3);  // next round
    g.skillVariance = std::clamp(cfg.skill_variance, 0, 2);
    g.dmBotFights = cfg.dm_bot_fights != 0;
    g.killcamOn = cfg.killcam != 0;
    setModelScale(float(std::clamp(cfg.model_scale, 80, 150)) / 100.0f);
    g.viewShake = cfg.view_shake != 0;
    // (knife and skins: the inventory, applySkins)
    g.hitSound = cfg.hitsound != 0;
    g.showRadar = cfg.radar != 0;
    g.showHitMarker = cfg.hitmarker != 0;
    g.autoHop = cfg.bhop != 0;
    if (g.autoHop) {
        // Bunny hopping: no stamina slowdown on jump/land; air-strafing keeps and builds speed.
        g.moveParams.staminaJumpCost = 0;
        g.moveParams.staminaLandCost = 0;
    }
    for (WeaponState& w : g.guns) {
        w.spraySpread = cfg.spread_spray != 0;
        w.moveSpread = cfg.spread_movement != 0;
    }
    if (g.audio) g.audio->setVolume(std::clamp(cfg.volume, 0.0f, 1.0f));
}

void resetPosition(Game& g) {
    g.jumpLatch = false;
    g.jumpNeedsRelease = true;  // a jump key still held from before (skipping a killcam...) doesn't jump you
    g.player = {};
    // Stand on the real ground under the spawn (spawn heights come from a stepped grid; ramps are smooth).
    TraceResult down = g.world.traceBox(g.spawn + Vec3{0, 0, 24}, g.spawn - Vec3{0, 0, 24}, hullMins(), hullMaxs(false));
    g.player.origin = down.fraction < 1.0f && !down.startSolid ? down.endpos : g.spawn;
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
        (forPlayer ? watchers : occupied).push_back(forPlayer ? d.pos + Vec3{0, 0, dummyEyeZ()} : d.pos);
        if (!forPlayer && g.dmBotFights) watchers.push_back(d.pos + Vec3{0, 0, dummyEyeZ()});  // bots fight: not in each other's sight
    }
    if (!forPlayer) {  // you, and you a moment from now (so they don't appear round the corner you're taking)
        watchers.push_back(g.player.origin + Vec3{0, 0, kStandEye});
        watchers.push_back(g.player.origin + g.player.velocity * 0.6f + Vec3{0, 0, kStandEye});
    }
    return randomSpawnPoint(g.nav, g.world, watchers, forPlayer ? 900.0f : 800.0f, occupied, g.rng);
}

// (Re)starts a deathmatch: fresh scores, everyone respawns.
void startDeathmatch(Game& g) {
    g.replay.clear();
    g.dmEnd = g.simTime + 60.0 * g.dmMinutes;
    g.dmOverUntil = -1;
    g.shots = g.hits = g.headshots = 0;
    resetRecord(g);
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

// Prefire: everyone back to the start of the route, every bot back on its spot.
void startPrefire(Game& g) {
    const std::vector<PrefireRoute>& routes = townPrefireRoutes();
    g.pf.route = std::clamp(g.pf.route, 0, int(routes.size()) - 1);
    const PrefireRoute& r = routes[size_t(g.pf.route)];
    g.spawn = townPoint(r.start.x, r.start.y);
    g.spawnYaw = std::atan2(r.start.lookY - r.start.y, r.start.lookX - r.start.x) / kDegToRad;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    for (size_t i = 0; i < g.dummies.size() && i < r.bots.size(); ++i) {
        const RetakeSpot& b = r.bots[i];
        Dummy& d = g.dummies[i];
        d = Dummy{};
        d.pos = d.prevPos = townPoint(b.x, b.y);
        d.yaw = d.prevYaw = std::atan2(b.lookY - b.y, b.lookX - b.x) / kDegToRad;
        BotBrain brain;
        brain.state = 1;
        brain.holdOnly = brain.frozen = true;
        brain.holdYaw = d.yaw;
        g.bots[i] = brain;
    }
    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
    g.pf.start = g.pf.resultUntil = -1;
    g.shots = g.hits = g.headshots = 0;
    g.nades.clear();
    g.smokes.clear();
    g.fires.clear();
    g.hudDirty = true;
}

void endPrefire(Game& g, bool won) {
    if (g.pf.resultUntil >= 0) return;
    g.pf.won = won;
    g.pf.last = g.pf.start >= 0 ? float(g.simTime - g.pf.start) : 0.0f;
    float& best = g.pf.best[g.pf.route % 8];
    if (won && (best <= 0 || g.pf.last < best)) best = g.pf.last;
    g.pf.resultUntil = g.simTime + (won ? 3.5 : 2.0);
    g.hudDirty = true;
}

constexpr double kRetakeRoundTime = 40.0;
void pushHitLog(Game& g, const std::string& text, uint32_t color);

// Online: a deathmatch with no bots and no clock; the dummies are the other players (hidden until they
// show up). You spawn away from them, like deathmatch.
void startOnline(Game& g) {
    g_onlineNames = true;
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        g.dummies[i] = Dummy{};
        g.dummies[i].respawnLeft = 1e9f;
        g.dummies[i].deadFor = 10.0f;
        g.dummies[i].hp = 1e6f;  // never dies here: the victim's own game decides that
        g.bots[i] = BotBrain{};
    }
    for (Game::Remote& r : g.remotes) r = Game::Remote{};
    g.botsFire = true;  // (no bots: this shows HP and deaths on the HUD)
    g.dmEnd = 1e18;
    g.dmOverUntil = -1;
    resetRecord(g);
    g.spawn = pickDmSpawn(g, true);
    g.spawnYaw = rnd(g) * 360.0f - 180.0f;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    g.spawnProtectUntil = g.simTime + 1.0;
    g.hudDirty = true;
}

// A new retake round: random site, its bots on random hold spots facing the way you'll come, you at
// a random entry with full ammo and HP.
void startRetakeRound(Game& g) {
    const std::vector<RetakeSite>& sites = townRetakeSites();
    g.rtSite = int(rnd(g) * float(sites.size())) % int(sites.size());
    const RetakeSite& site = sites[size_t(g.rtSite)];
    const RetakeSpot& entry = site.entries[size_t(rnd(g) * float(site.entries.size())) % site.entries.size()];
    g.spawn = townPoint(entry.x, entry.y);
    g.spawnYaw = std::atan2(entry.lookY - entry.y, entry.lookX - entry.x) / kDegToRad;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    std::vector<size_t> order(site.holds.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = k;
    for (size_t k = order.size(); k > 1; --k) std::swap(order[k - 1], order[size_t(rnd(g) * float(k)) % k]);
    // Holds you can see from where you start go last: no bot is in view when the round begins.
    const Vec3 eye = g.spawn + Vec3{0, 0, kStandEye};
    std::stable_partition(order.begin(), order.end(), [&](size_t k) {
        const Vec3 p = townPoint(site.holds[k].x, site.holds[k].y);
        return g.world.traceRay(eye, p + Vec3{0, 0, 62}).fraction < 1.0f && g.world.traceRay(eye, p + Vec3{0, 0, 30}).fraction < 1.0f;
    });
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        const RetakeSpot& h = site.holds[order[i % order.size()]];
        Dummy& d = g.dummies[i];
        d = Dummy{};
        d.pos = d.prevPos = townPoint(h.x, h.y);
        d.yaw = d.prevYaw = std::atan2(h.lookY - h.y, h.lookX - h.x) / kDegToRad;
        BotBrain b;
        b.state = 1;  // already placed: hold
        b.holdOnly = true;
        b.holdYaw = d.yaw;
        b.holdLook = townPoint(h.lookX, h.lookY);
        b.hasHoldLook = true;
        b.home = d.pos;
        b.hasHome = true;
        g.bots[i] = b;

    }
    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
    g.rtRoundEnd = g.simTime + kRetakeRoundTime;  // = the bomb's fuse
    g.rtResultUntil = -1;
    g.bombPos = townPoint(site.bombX, site.bombY);
    g.bombActive = true;
    g.bombExplodeAt = g.rtRoundEnd;
    g.defuseStart = -1;
    g.nextBeep = g.simTime;
    g.dmShownSecs = -1;
    char msg[48];
    std::snprintf(msg, sizeof(msg), "RETAKE %s", site.name);
    pushHitLog(g, msg, 0x80ff80);
    g.hudDirty = true;
}

void endRetakeRound(Game& g, bool won, const char* why) {
    if (g.rtResultUntil >= 0) return;
    (won ? g.rtWon : g.rtLost)++;
    if (won) g.you.mvps++;  // you're the only one retaking: a win is your MVP
    g.rtResultWin = won;
    g.rtResultText = why;
    g.rtResultUntil = g.simTime + 3.0;
    g.hudDirty = true;
}

// ---- Damage: bullets, grenades and fire all come through these ----

// You take damage from `attacker` (bot id, or -2 for your own grenade / the world). Returns true if
// it killed you: you respawn per the mode's rules.
bool hurtPlayer(Game& g, int attacker, float dmg, bool head, const char* weapon) {
    if (g.deadUntil >= 0 || g.noclip || g.simTime < g.spawnProtectUntil || dmg <= 0) return false;
    const bool dies = g.hp - dmg <= 0;
    if (attacker >= 0) recordDamage(g, attacker, -1, std::min(dmg, std::max(0.0f, g.hp)), head, weapon, false, dies);
    g.hp -= dmg;
    g.hurtUntil = g.simTime + 0.25;
    if (attacker >= 0 && size_t(attacker) < g.dummies.size()) {  // damage direction: an arc pointing at the shooter
        g.hurtFrom[g.hurtNext] = g.dummies[size_t(attacker)].pos;
        g.hurtAt[g.hurtNext] = g.simTime;
        g.hurtNext = (g.hurtNext + 1) % 4;
    }
    sound(g, Sfx::HitBody, 0.9f, 0.0f, 0.7f);
    g.hudDirty = true;
    if (!dies) return false;
    if (attacker >= 0 && size_t(attacker) < g.dummies.size() && isBot(g, size_t(attacker)) && !g.online && g.killcamOn &&
        g.mapId == 1 && (g.mode == 1 || g.mode == 3)) {
        g.killcamKiller = attacker;  // the killcam starts a moment from now
        g.killcamAt = g.simTime;
    }
    g.zoom = g.resumeZoom = 0;  // dead: no scope left up (spectating, or the next life)
    g.resumeZoomAt = g.boltAt = -1;
    g.buyMenu = false;
    if (attacker < 0) {  // your own HE / molotov
        g.you.deaths++;
        g.feed.push_back({std::string("YOU  [") + weapon + "]", 0xFF6060u, g.simTime});
    }
    if (g.online && g.net.ready())  // online: everyone hears it from you (your own grenade: you killed yourself)
        g.net.sendDeath(uint8_t(g.net.myId()), localToNet(g, attacker >= 0 ? attacker : -1), head, netWeaponCode(weapon));
    if (g.mode == 3 && g.mapId == 1) {  // competitive: out for the round - spectate (fly) until the next one
        g.deaths++;
        g.hp = 0;
        g.comp.youDead = true;
        g.deadUntil = 1e18;
        g.noclip = true;
        g.spec = nextTeammate(g, -1, 1);
        g.defuseStart = -1;
        g.flashFull = g.flashEnd = 0;
        buildDamageReport(g);
        return true;
    }
    g.deaths++;
    g.hp = 100;
    g.deadUntil = g.simTime + (g.killcamAt == g.simTime ? 4.3 : 1.2);  // (the killcam first: Space skips it)
    g.flashFull = g.flashEnd = 0;
    buildDamageReport(g);
    if ((g.mode == 1 || g.mode == 5) && g.mapId == 1) {
        g.spawn = pickDmSpawn(g, true);
        g.spawnYaw = rnd(g) * 360.0f - 180.0f;
        g.spawnProtectUntil = g.deadUntil + 1.0;
        for (BotBrain& b : g.bots)
            if (b.state == 2) { b.state = 1; b.timer = 1.0f; }
    } else if (g.mode == 2 && g.mapId == 1) {
        endRetakeRound(g, false, "YOU DIED");
    } else if (g.mode == 4 && g.mapId == 1) {
        endPrefire(g, false);
    }
    refillAmmo(g);  // respawn with full magazines, like CS
    resetPosition(g);
    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
    return true;
}

// A bot that died: when (and whether) it comes back depends on the mode.
// Starts the replay viewer (pause menu): the game waits while you watch.
void startReplayViewer(Game& g) {
    if (g.replay.empty()) return;
    g.rv = Game::ReplayView{};
    g.rv.on = true;
    const double mark = g.replay.markTime();
    g.rv.t = mark >= g.replay.start() && mark < g.replay.end() - 1.0 ? mark : g.replay.start();
    g.rv.end = g.replay.end();
    g.rv.pov = int(g.dummies.size());  // you
    g.rv.shotsDone = g.rv.t;
    g.replayFx = Effects{};
}

// Steps whatever's playing by dt (wall clock): the shots crossed are heard and their tracers drawn.
void stepReplay(Game& g, double dt) {
    if (g.killcamAt >= 0 && !g.rv.on && g.simTime >= g.killcamAt + 0.6) {  // the killcam: the last 3 s, its eyes
        g.rv = Game::ReplayView{};
        g.rv.on = g.rv.killcam = true;
        g.rv.killer = g.rv.pov = g.killcamKiller;
        g.rv.t = std::max(g.replay.start(), g.killcamAt - 3.0);
        g.rv.end = g.killcamAt + 0.35;
        g.rv.shotsDone = g.rv.t;
        g.replayFx = Effects{};
        g.killcamAt = -1;
        g.hudDirty = true;
    }
    if (!g.rv.on) return;
    if (!g.rv.paused) g.rv.t += dt * g.rv.speed;
    if (!g.rv.killcam) g.rv.end = g.replay.end();
    if (g.rv.t >= g.rv.end) {
        g.rv.t = g.rv.end;
        if (g.rv.killcam) {  // done: deathmatch respawns now, competitive goes back to spectating
            g.rv.on = false;
            if (g.deadUntil > g.simTime + 0.3 && g.deadUntil < 1e17) g.deadUntil = g.simTime + 0.3;
            g.hudDirty = true;
            return;
        }
        g.rv.paused = true;
    }
    g.replayFx.setGround(g.fx.ground());
    g.replayFx.update(float(dt * g.rv.speed));
    static std::vector<ReplayShot> shots;
    if (g.rv.t > g.rv.shotsDone) {
        g.replay.shotsBetween(g.rv.shotsDone, g.rv.t, shots);
        ReplayFrame now;
        const bool have = g.audio && g.replay.sample(g.rv.t, now);
        Vec3 ear = g.rv.camPos;
        float earYaw = g.rv.camYaw;
        if (have && g.rv.pov >= 0 && size_t(g.rv.pov) < now.agents.size()) {
            ear = now.agents[size_t(g.rv.pov)].pos + Vec3{0, 0, dummyEyeZ()};
            earYaw = now.agents[size_t(g.rv.pov)].yaw;
        }
        for (const ReplayShot& sh : shots) {
            g.replayFx.tracer(sh.from, sh.to);
            if (have) gunshot3D(g, sh.weapon, sh.from, ear, earYaw, sh.shooter == g.rv.pov ? 1.5f : 1.1f);
        }
    }
    g.rv.shotsDone = g.rv.t;
}

// One replay frame: every dummy as drawn, then you. Every other tick (64 a second), on Dust, offline or online.
void recordReplay(Game& g) {
    if (g.mapId != 1 || (g.rv.on && !g.rv.killcam)) return;
    if (++g.replayTicks & 1) return;
    if (g.replayAgents != g.dummies.size() + 1) {  // the roster changed (a new match, players joining): start over
        g.replay.clear();
        g.replayAgents = g.dummies.size() + 1;
    }
    ReplayFrame& f = g.replay.next(g.simTime);
    for (const Dummy& d : g.dummies) {
        ReplayAgent a;
        a.pos = d.pos;
        a.hitDir = d.hitDir;
        a.yaw = d.yaw;
        a.pitch = d.pitch;
        a.crouch = d.crouch;
        a.deadFor = d.deadFor;
        a.stepDist = d.stepDist;
        a.weapon = d.weapon;
        a.alive = d.alive();
        a.friendly = d.friendly;
        a.lostHelmet = d.lostHelmet;
        f.agents.push_back(a);
    }
    ReplayAgent you;
    you.pos = g.player.origin;
    you.yaw = float(g.viewYaw);
    you.pitch = float(g.viewPitch);
    you.crouch = g.player.ducked ? 1.0f : 0.0f;
    you.weapon = uint8_t(g.weapon->def->id);
    you.alive = g.deadUntil < 0 && !(g.mode == 3 && g.comp.youDead);
    f.agents.push_back(you);
    for (const Game::Smoke& sm : g.smokes) f.smokes.push_back({sm.pos, sm.start});
}

// A shot for the replay (shooter: a dummy index, or -1 for you).
void replayShot(Game& g, const Vec3& from, const Vec3& to, int shooter, int weapon) {
    if (g.mapId == 1) g.replay.shot({g.simTime, from, to, shooter < 0 ? int(g.dummies.size()) : shooter, uint8_t(weapon)});
}

// A gunshot lights the walls round it for a moment (warm, about 400 units; fades in ~50 ms). One light at a
// time: a brighter shot takes it over.
void muzzleLight(Game& g, const Vec3& pos, float strength) {
    if (strength < g.flashLight) return;
    g.flashPos = pos;
    g.flashLight = strength;
}

// A headshot kill knocks the helmet off (CS:GO does this): it flies along the shot, spins, bounces and lies
// there. Only on models that wear one (CTs; in competitive the Ts wear a beanie).
void knockHelmet(Game& g, size_t i, const Vec3& dir) {
    if (i >= g.dummies.size() || (g.mode == 3 && i < g.team.size() && g.team[i] == 0)) return;
    Dummy& d = g.dummies[i];
    d.lostHelmet = true;
    g.fx.helmet(d.pos + Vec3{0, 0, crouchZ(67.0f * modelScale(), d.crouch)}, dir, 0x2c3a24);
}

void botDied(Game& g, size_t i) {
    Dummy& d = g.dummies[i];
    if (g.botsFire || g.mapId == 1) d.respawnLeft = 2.0f + rnd(g) * 2.0f;  // no insta-respawn when they fight back
    if (g.mode >= 2) d.respawnLeft = 1e9f;  // retakes, competitive, prefire: dead for the round
}

// A bot takes non-bullet damage (HE, fire) from `attacker` (-1 = you).
void hurtBot(Game& g, size_t i, int attacker, float dmg, const char* weapon) {
    Dummy& d = g.dummies[i];
    if (!d.alive() || dmg <= 0) return;
    float applied = std::min(dmg, d.hp);
    d.hp -= dmg;
    d.flash[kChest] = 0.15f;
    const bool kill = d.hp <= 0;
    recordDamage(g, attacker, int(i), applied, false, weapon, false, kill);
    if (kill) {
        d.respawnLeft = 1.0f;
        botDied(g, i);
        if (attacker < 0 && g.botsFire && g.hp < 100.0f && g.mode != 3 && g.mode != 4) g.hp = std::min(100.0f, g.hp + 40.0f);
    } else if (attacker < 0 && i < g.bots.size()) {
        g.bots[i].alertUntil = g.simTime + 2.0;
        g.bots[i].lastSeen = g.player.origin;
    }
}

constexpr float kHeRadius = 350.0f, kFireRadius = 110.0f;
constexpr double kFireLife = 7.0;

// Flashbang: blinds whoever can see it, fully if they're looking at it. Your ears ring.
void flashBang(Game& g, const Vec3& pos) {
    if (g.audio) g.audio->play3D(Sfx::FlashBang, pos, g.lastRenderEye, float(g.viewYaw), 5000.0f, 1.0f);
    g.fx.burst(pos, 0xfffbe8, 1.0f);
    auto strength = [&](const Vec3& eye, const Vec3& look) {
        Vec3 to = pos - eye;
        float dist = length(to);
        if (dist > 2400.0f || g.world.traceRay(eye, pos).fraction < 1.0f) return 0.0f;
        float facing = dot(to * (1.0f / std::max(dist, 1.0f)), look);
        float f = facing > 0.55f ? 1.0f : facing > -0.2f ? 0.5f : 0.15f;  // looking away still stings a bit
        return f * std::clamp(1.0f - (dist - 350.0f) / 2050.0f, 0.15f, 1.0f);
    };
    if (g.deadUntil < 0) {
        float k = strength(g.lastRenderEye, anglesToForward(float(g.viewPitch), float(g.viewYaw)));
        if (k > 0.1f) {
            g.flashFull = std::max(g.flashFull, g.simTime + 1.9 * k);
            g.flashEnd = std::max(g.flashEnd, g.simTime + 4.9 * k);
            sound(g, Sfx::FlashRing, 0.6f * k);
        }
    }
    for (size_t i = 0; i < g.dummies.size() && i < g.bots.size(); ++i) {
        const Dummy& d = g.dummies[i];
        if (!d.alive()) continue;
        float y = d.yaw * kDegToRad;
        float k = strength(d.pos + Vec3{0, 0, dummyEyeZ(d.crouch)}, {std::cos(y), std::sin(y), 0});
        if (k > 0.1f) g.bots[i].blindUntil = std::max(g.bots[i].blindUntil, g.simTime + 3.6 * k);
    }
}

// HE: up to 98 damage, falling off to nothing at kHeRadius; walls stop it. `owner` threw it (-1 you).
// Online, each game hurts only its own: its player, and the host its bots.
void heExplode(Game& g, const Vec3& pos, int owner) {
    if (g.audio) g.audio->play3D(Sfx::Explosion, pos, g.lastRenderEye, float(g.viewYaw), 6000.0f, 1.0f);
    g.fx.burst(pos, 0xffa040, 1.4f);
    auto damageAt = [&](const Vec3& c) {
        float d = length(c - pos);
        if (d > kHeRadius || g.world.traceRay(pos + Vec3{0, 0, 4}, c).fraction < 1.0f) return 0.0f;
        return 98.0f * std::pow(1.0f - d / kHeRadius, 1.4f);
    };
    for (size_t i = 0; i < g.dummies.size(); ++i)
        if (g.dummies[i].alive() && isBot(g, i) && !netClient(g))
            hurtBot(g, i, owner, damageAt(g.dummies[i].pos + Vec3{0, 0, 40}), "HE");
    hurtPlayer(g, owner < 0 ? -2 : owner, damageAt(g.player.origin + Vec3{0, 0, 40}), false, "HE");
}

bool insideSmoke(const Game& g, const Vec3& p, float extra) {
    for (const Game::Smoke& s : g.smokes) {
        float r = kSmokeRadius * smokeGrow(s, g.simTime) + extra;
        if (r > extra && length2d(s.pos - p) < r && std::fabs(s.pos.z - p.z) < 160.0f) return true;
    }
    return false;
}

// Molotov: a patch of fire where it lands - unless it lands in a smoke.
void igniteMolotov(Game& g, const Vec3& at, int owner) {
    TraceResult down = g.world.traceRay(at + Vec3{0, 0, 8}, at - Vec3{0, 0, 400});
    Vec3 p = down.endpos;
    if (insideSmoke(g, p, 0)) {
        if (g.audio) g.audio->play3D(Sfx::Fire, p, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.6f, 1.6f);
        return;  // fizzles
    }
    g.fires.push_back({p, g.simTime, g.simTime, owner});
    if (g.audio) g.audio->play3D(Sfx::Explosion, p, g.lastRenderEye, float(g.viewYaw), 2500.0f, 0.45f, 1.7f);
}

// Radar picture of the Dust grid: walkable cells merged into rectangles, lighter = higher.
void buildRadar(Game& g) {
    g.radarRects.clear();
    const MapGrid& m = townGrid();
    std::vector<int> band(size_t(m.w * m.h), -1);
    float lo = 1e9f, hi = -1e9f;
    for (size_t c = 0; c < band.size(); ++c)
        if (m.floor[c] > MapGrid::kNoFloor) { lo = std::min(lo, m.floor[c]); hi = std::max(hi, m.floor[c]); }
    for (size_t c = 0; c < band.size(); ++c)
        if (m.floor[c] > MapGrid::kNoFloor) band[c] = int((m.floor[c] - lo) / std::max(1.0f, hi - lo) * 5.99f);
    std::vector<char> used(band.size(), 0);
    g.radarMin = {1e9f, 1e9f, 0};
    g.radarMax = {-1e9f, -1e9f, 0};
    for (int j = 0; j < m.h; ++j)
        for (int i = 0; i < m.w; ++i) {
            size_t c = size_t(m.index(i, j));
            if (used[c] || band[c] < 0) continue;
            int i1 = i, j1 = j;
            while (i1 + 1 < m.w && !used[size_t(m.index(i1 + 1, j))] && band[size_t(m.index(i1 + 1, j))] == band[c]) ++i1;
            for (bool grow = true; grow && j1 + 1 < m.h;) {
                for (int x = i; x <= i1 && grow; ++x)
                    grow = !used[size_t(m.index(x, j1 + 1))] && band[size_t(m.index(x, j1 + 1))] == band[c];
                if (grow) ++j1;
            }
            for (int y = j; y <= j1; ++y)
                for (int x = i; x <= i1; ++x) used[size_t(m.index(x, y))] = 1;
            uint32_t v = 0x50 + uint32_t(band[c]) * 0x14;
            Game::RadarRect rr{m.x0 + float(i) * m.cell, m.y0 + float(j) * m.cell, m.x0 + float(i1 + 1) * m.cell,
                               m.y0 + float(j1 + 1) * m.cell, (v << 24) | ((v - 8) << 16) | ((v - 24) << 8) | 0xE0};
            g.radarRects.push_back(rr);
            g.radarMin = {std::min(g.radarMin.x, rr.x0), std::min(g.radarMin.y, rr.y0), 0};
            g.radarMax = {std::max(g.radarMax.x, rr.x1), std::max(g.radarMax.y, rr.y1), 0};
        }
}

// ---- Competitive ----
constexpr int kCompRoundsToWin = 13;
constexpr double kCompBuyTime = 20.0, kCompRoundTime = 115.0, kCompPlantTime = 3.2;  // (freeze: Game::freezeTime)

int teamOf(const Game& g, int id) { return id < 0 ? g.comp.youTeam : g.team[size_t(id)]; }
bool youAlive(const Game& g) { return !g.comp.youDead; }

// Dead in competitive: the next living teammate after `from` going `dir` (+1 / -1), or -1 if none.
int nextTeammate(const Game& g, int from, int dir) {
    const int n = int(g.dummies.size());
    for (int k = 1; k <= n; ++k) {
        const int i = ((from < 0 ? (dir > 0 ? -1 : 0) : from) + dir * k + n * 2) % n;
        if (g.dummies[size_t(i)].alive() && g.dummies[size_t(i)].friendly) return i;
    }
    return -1;
}

int& moneyOf(Game& g, int id) { return id < 0 ? g.comp.money : g.comp.botMoney[size_t(id)]; }
void addMoney(Game& g, int id, int amount) { moneyOf(g, id) = std::clamp(moneyOf(g, id) + amount, 0, 16000); }

std::string g_compLog;  // automated runs: one line per competitive round (testing)

// Bots buy at the start of a round like a CS team: each side calls the round (pistol, eco, force, full buy) from
// its bots' money, then every bot buys for its role (botBuy: the first is the AWPer). A gun it survived with, it
// keeps.
void compBotsBuy(Game& g) {
    Game::Comp& c = g.comp;
    BuyRound call[2];
    for (int side = 0; side < 2; ++side) {
        int sum = 0, n = 0;
        for (size_t i = 0; i < g.dummies.size(); ++i)
            if (isBot(g, i) && g.dummies[i].alive() && g.team[i] == side) { sum += c.botMoney[i]; ++n; }
        const int other = side == c.youTeam ? c.themScore : c.youScore;  // 12: their match point
        const bool lastOfHalf = c.round == 11 || c.round == 23;
        call[side] = teamBuyRound(n ? sum / n : 0, c.round == 0 || c.round == 12, lastOfHalf || other == 12);
    }
    int role[2] = {0, 0};
    std::string bought[2];
    c.ctNade.assign(g.dummies.size(), -1);
    c.nadeTry.assign(g.dummies.size(), 0.0);
    c.saving.assign(g.dummies.size(), 0);
    c.defuseHeard = false;
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        Dummy& d = g.dummies[i];
        if (!isBot(g, i) || !d.alive()) continue;
        const int side = g.team[i];
        const BotBuy b = botBuy(call[side], c.botMoney[i], side, role[side]++, c.botGun[i], d.armor > 0, d.helmet);
        c.botMoney[i] -= b.spent;
        c.botGun[i] = uint8_t(b.gun);
        if (b.armor && d.armor <= 0) d.armor = 100;
        d.helmet = b.helmet;
        d.weapon = uint8_t(b.gun);  // what it holds (and you see)
        // CTs with money to spare carry something for the execute: a molotov on a full buy, an HE on a force.
        if (side == 1 && call[side] == kBuyFull && c.botMoney[i] >= 400) { c.ctNade[i] = Game::kMolotov; c.botMoney[i] -= 400; }
        else if (side == 1 && call[side] == kBuyForce && c.botMoney[i] >= 300) { c.ctNade[i] = Game::kHeNade; c.botMoney[i] -= 300; }
        bought[side] += std::string(bought[side].empty() ? "" : ", ") + weaponDef(b.gun).name;
    }
    if (!g_compLog.empty()) {
        const char* const kCall[] = {"pistol", "eco", "force", "full buy"};
        std::ofstream(g_compLog, std::ios::app) << "  T " << kCall[call[0]] << ": " << bought[0] << " | CT "
                                                << kCall[call[1]] << ": " << bought[1] << "\n";
    }
}

// Online competitive, the host at a round start: who plays on which team. Team A is the host's. New players
// join a team (split: the one with fewer players; together: A); bots fill each team up to its size
// (comp_mates + 1 and comp_enemies, like offline) in slots 8.. ; the rest of the slots sit out.
int g_netTeamsTogether = 0;  // config net_teams
void netRoster(Game& g) {
    int players[2] = {0, 0};
    for (int id = 0; id < kNetMaxPlayers; ++id) {
        if (!g.net.connected(id)) g.netTeam[id] = -1;
        else if (g.netTeam[id] >= 0) players[g.netTeam[id]]++;
    }
    if (g.netTeam[0] < 0) { g.netTeam[0] = 0; players[0]++; }
    for (int id = 1; id < kNetMaxPlayers; ++id)
        if (g.net.connected(id) && g.netTeam[id] < 0) {
            g.netTeam[id] = g_netTeamsTogether ? 0 : players[1] < players[0] ? 1 : 0;
            players[g.netTeam[id]]++;
        }
    const int bots[2] = {std::max(0, g.compMates + 1 - players[0]), std::max(0, g.compEnemies - players[1])};
    for (int k = 0; k < kNetBots; ++k)
        g.netTeam[kNetMaxPlayers + k] = k < bots[0] ? 0 : k < bots[0] + bots[1] ? 1 : -1;
    for (int k = 0; k < kNetSlots && size_t(k) < g.team.size(); ++k)  // sides: team A is where you (the host) are
        g.team[size_t(k)] = g.netTeam[k] == 1 ? 1 - g.comp.youTeam : g.comp.youTeam;
}

// Why a round ended, as a byte on the wire.
const char* const kRoundWhy[] = {"COUNTER-TERRORISTS ELIMINATED", "TERRORISTS ELIMINATED", "TIME RAN OUT",
                                 "THE BOMB EXPLODED", "THE BOMB HAS BEEN DEFUSED"};
uint8_t roundWhyCode(const char* why) {
    for (uint8_t k = 0; k < 5; ++k)
        if (std::strcmp(why, kRoundWhy[k]) == 0) return k;
    return 2;
}

// The T side's plays (Comp::play): a plain execute, a rush, a split of A or B, a fake, a default (map control,
// then a late execute).
enum CompPlay { kPlayExecute, kPlayRush, kPlaySplitA, kPlaySplitB, kPlayFake, kPlayDefault };

// A line on the team radio (top right) from a bot on your side.
void teamRadio(Game& g, int bot, const std::string& msg) {
    if (bot >= 0 && size_t(bot) < g.team.size() && g.team[size_t(bot)] == g.comp.youTeam)
        pushHitLog(g, agentName(bot) + ": " + msg, 0x90e0a0);
}

// A new round: everyone to their spawn, survivors keep their kit, bots buy and get their plan.
void startCompRound(Game& g) {
    Game::Comp& c = g.comp;
    c.roundStartAt = g.simTime;
    const bool halfTime = c.round == 12;
    if (halfTime) {  // swap sides: fresh economy
        c.youTeam = 1 - c.youTeam;
        for (size_t i = 0; i < g.team.size(); ++i) g.team[i] = 1 - g.team[i];
        c.money = 800;
        std::fill(c.botMoney.begin(), c.botMoney.end(), 800);
        std::fill(c.botGun.begin(), c.botGun.end(), uint8_t(kWPistol));
        for (Dummy& d : g.dummies) { d.armor = 0; d.helmet = false; d.respawnLeft = 1.0f; }
        c.youDead = true;  // treat everyone as freshly respawned (no carried kit)
        c.lossStreak[0] = c.lossStreak[1] = 0;
    }
    if (c.youDead) {  // you died last round: start over with a pistol
        c.armor = 0;
        c.helmet = c.kit = false;
        c.ownPrimary = -1;
        for (int& n : c.nades) n = 0;
        g.primary = &g.guns[kWRifle];
        g.secondary = &g.guns[kWPistol];
    }
    c.youDead = false;
    g.noclip = false;
    g.spec = -1;
    if (netHost(g)) netRoster(g);
    const int youSide = c.youTeam;
    g.spawn = townTeamSpawns(youSide)[0];
    g.spawnYaw = youSide == 0 ? 90.0f : -90.0f;
    g.hp = 100;
    g.deadUntil = -1;
    refillAmmo(g);
    resetPosition(g);
    g.switchTo = c.ownPrimary >= 0 ? 1 : 2;
    int slot[2] = {1, 0};  // next free spawn spot per side (you took your side's first one)
    if (youSide == 1) { slot[0] = 0; slot[1] = 1; }
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        Dummy& d = g.dummies[i];
        const int side = g.team[i];
        if (g.online && (g.netTeam[i] < 0 || int(i) == g.net.myId())) {  // sitting this one out (or it's you)
            d = Dummy{};
            d.respawnLeft = 1e9f;
            d.deadFor = 10.0f;
            continue;
        }
        const bool died = !d.alive();
        const float armor = died ? 0.0f : d.armor;
        const bool helmet = !died && d.helmet;
        if (died) g.comp.botGun[i] = kWPistol;
        const int spot = slot[side]++;
        const Vec3 sp = townTeamSpawns(side)[size_t(spot) % 5];
        if (!isBot(g, i)) {  // another player: alive at their spawn (their game puts them there and keeps their kit)
            g.netSpawn[i] = spot;
            d = Dummy{};
            d.hp = 1e6f;  // their game decides when they die
            d.friendly = side == youSide;
            d.pos = d.prevPos = sp;
            continue;
        }
        d = Dummy{};
        d.armor = armor;
        d.helmet = helmet;
        d.friendly = side == youSide;
        d.pos = d.prevPos = sp;
        d.yaw = d.prevYaw = side == 0 ? 90.0f : -90.0f;
        g.bots[i] = BotBrain{};
        g.bots[i].state = 1;
        g.bots[i].holdOnly = true;
    }
    compBotsBuy(g);
    // The plan: Ts take one site (the carrier heads for the bomb spot), CTs split between the sites.
    c.siteTarget = rnd(g) < 0.5f ? 0 : 1;
    // Like CS, a random T gets the bomb (you, if you're T and it's your turn).
    std::vector<int> ts;
    if (youSide == 0 && g_compLog.empty()) ts.push_back(-1);  // (automated runs: the idle you never gets it)
    for (size_t i = 0; i < g.dummies.size(); ++i)
        if (g.team[i] == 0 && g.dummies[i].alive()) ts.push_back(int(i));
    c.carrier = ts.empty() ? -3 : ts[size_t(rnd(g) * float(ts.size())) % ts.size()];
    if (c.carrier == -1) pushHitLog(g, "YOU HAVE THE BOMB", 0xffd060);
    // Ts: the round's play. Staging points on the way in: 0 A long, 1 A short (catwalk), 2 B tunnels, 3 mid
    // by the doors (mid to B), 4 lower mid. Then the execute (compTick).
    const TownTactics& tac = townTactics();
    const auto& stages = tac.stages;
    const float pr = rnd(g);
    const int play = pr < 0.30f ? kPlayExecute : pr < 0.45f ? kPlayRush : pr < 0.58f ? kPlaySplitA
                   : pr < 0.68f ? kPlaySplitB : pr < 0.84f ? kPlayFake : kPlayDefault;
    if (play == kPlaySplitA) c.siteTarget = 0;
    if (play == kPlaySplitB) c.siteTarget = 1;
    const int route = c.siteTarget == 1 ? 2 : (rnd(g) < 0.5f ? 0 : 1);
    c.play = play;
    c.route = route;
    c.rush = play == kPlayRush;
    c.sent = false;
    c.goAt = 0;
    c.fakeRoute = play == kPlayFake ? (c.siteTarget == 1 ? (rnd(g) < 0.5f ? 0 : 1) : 2) : -1;
    c.stages.clear();
    auto addStage = [&](int r) { c.stages.push_back(townPoint(stages[r][0], stages[r][1])); };
    switch (play) {
        case kPlaySplitA: addStage(0); addStage(1); break;          // long and short together
        case kPlaySplitB: addStage(2); addStage(3); break;          // tunnels and through mid doors
        case kPlayFake: addStage(route); addStage(c.fakeRoute); break;  // group 1: the fakers
        case kPlayDefault: addStage(route); addStage(4); addStage(route == 2 ? 0 : 2); addStage(1); break;  // map control
        default: addStage(route); break;                            // execute, rush: one group
    }
    c.stagePoint = c.stages[0];
    c.stageOf.assign(g.dummies.size(), 0);
    c.faker.assign(g.dummies.size(), 0);
    c.throws.clear();
    c.executing = c.rotated = false;
    c.executeAt = g.simTime + g.freezeTime + (play == kPlayRush ? 0.0 : play == kPlayDefault ? 32.0 : 14.0);
    c.pushers.clear();
    c.pushBack.clear();
    c.pushUntil = -1;
    // CTs: the default setup (one mid, one short, one long, two B), now and then one heavier on a site;
    // each role picks one of its spots, so it's familiar but never quite the same.
    const float roll = rnd(g);
    const int roles[3][5] = {{kCtMid, kCtShort, kCtLong, kCtB, kCtB},   // default
                             {kCtMid, kCtShort, kCtLong, kCtA, kCtB},   // A-heavy
                             {kCtMid, kCtLong, kCtB, kCtB, kCtB}};      // B-heavy
    const int* setup = roles[roll < 0.7f ? 0 : roll < 0.85f ? 1 : 2];
    std::vector<int> spotOrder[kCtRoles];
    for (int r = 0; r < kCtRoles; ++r) {  // a shuffled spot list per role
        const size_t n = townCtSpots(r).size();
        for (size_t k = 0; k < n; ++k) spotOrder[r].push_back(int(k));
        for (size_t k = n; k > 1; --k) std::swap(spotOrder[r][k - 1], spotOrder[r][size_t(rnd(g) * float(k)) % k]);
    }
    int roleUsed[kCtRoles] = {};
    int tCount = 0, ctCount = 0, fakers = 0;
    std::vector<int> inGroup(c.stages.size(), 0);
    std::vector<int> ctRole(g.dummies.size(), -1);
    std::vector<RetakeSpot> ctSpot(g.dummies.size(), RetakeSpot{0, 0, 0, 0});
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        BotBrain& b = g.bots[i];
        Dummy& d = g.dummies[i];
        if (!isBot(g, i) || !d.alive()) continue;
        if (g.team[i] == 0) {
            // Its group: the carrier with the main one; the rest spread over the play's groups (a fake: two fakers).
            int group = 0;
            if (int(i) != c.carrier && c.stages.size() > 1) {
                if (play == kPlayFake) group = fakers < 2 ? (++fakers, 1) : 0;
                else group = (tCount + 1) % int(c.stages.size());
            }
            ++tCount;
            c.stageOf[i] = group;
            c.faker[i] = play == kPlayFake && group == 1;
            const int k = inGroup[size_t(group)]++;
            const Vec3 base = c.stages[size_t(group)];
            Vec3 p = base + Vec3{float(k % 3 - 1) * 48.0f, float(k / 3) * 48.0f, 0};
            b.goal = g.nav.standable(p) ? p : base;
        } else {
            const int role = setup[ctCount++ % 5];
            const std::vector<RetakeSpot>& spots = townCtSpots(role);
            const RetakeSpot& h = spots[size_t(spotOrder[role][size_t(roleUsed[role]++) % spots.size()])];
            ctRole[i] = role;
            ctSpot[i] = h;
            b.goal = townPoint(h.x, h.y);
            b.holdYaw = std::atan2(h.lookY - h.y, h.lookX - h.x) / kDegToRad;
            b.holdLook = townPoint(h.lookX, h.lookY);
            b.hasHoldLook = true;
        }
        b.hasGoal = true;
        b.home = b.goal;  // (and back here after a chase)
        b.hasHome = true;
        d.yaw = d.prevYaw = yawTo(d.pos, b.goal);  // face where they're heading, not each other
    }
    // CTs, now and then: one pushes for an early pick (long, mid or B tunnels) and falls back to its spot.
    if (rnd(g) < 0.4f) {
        struct Push { int role; RetakeSpot at; const char* call; };
        const Push pushes[3] = {{kCtLong, tac.pushes[0], tac.pushCalls[0]},
                                {kCtMid, tac.pushes[1], tac.pushCalls[1]},
                                {kCtB, tac.pushes[2], tac.pushCalls[2]}};
        const Push& p = pushes[size_t(rnd(g) * 3.0f) % 3];
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            if (ctRole[i] != p.role) continue;
            BotBrain& b = g.bots[i];
            b.goal = b.home = townPoint(p.at.x, p.at.y);
            b.holdYaw = std::atan2(p.at.lookY - p.at.y, p.at.lookX - p.at.x) / kDegToRad;
            b.holdLook = townPoint(p.at.lookX, p.at.lookY);
            c.pushers.push_back(int(i));
            c.pushBack.push_back(ctSpot[i]);
            c.pushUntil = g.simTime + g.freezeTime + 18.0 + 8.0 * double(rnd(g));
            teamRadio(g, int(i), p.call);
            break;
        }
    }
    // Ts: call the play.
    {
        int caller = -1;
        for (size_t i = 0; i < g.dummies.size() && caller < 0; ++i)
            if (isBot(g, i) && g.dummies[i].alive() && g.team[i] == 0) caller = int(i);
        const char* site = c.siteTarget == 0 ? "A" : "B";
        std::string call = play == kPlayRush     ? std::string("RUSH ") + site
                           : play == kPlaySplitA  ? townTactics().splitA
                           : play == kPlaySplitB  ? townTactics().splitB
                           : play == kPlayFake    ? std::string("FAKE ") + (c.siteTarget == 0 ? "B" : "A") + ", GO " + site
                           : play == kPlayDefault ? std::string("DEFAULT, THEN ") + site
                                                  : std::string("EXECUTE ") + site;
        if (caller >= 0) teamRadio(g, caller, call);
        if (!g_compLog.empty()) std::ofstream(g_compLog, std::ios::app) << "  play: " << call << "\n";
    }
    c.planted = false;
    c.planter = c.defuser = -3;
    c.plantStart = c.botDefuseStart = -1;
    g.bombActive = false;
    g.defuseStart = -1;
    g.nades.clear();
    g.smokes.clear();
    g.fires.clear();
    g.flashFull = g.flashEnd = 0;
    for (Game::Stats& st : g.botStats) st.roundKills = 0;
    g.you.roundKills = 0;
    c.phase = 0;
    c.phaseEnd = g.simTime + g.freezeTime;
    c.buyUntil = g.simTime + g.freezeTime + kCompBuyTime;
    std::fill(g.botSeen.begin(), g.botSeen.end(), 0.0f);
    g.hudDirty = true;
    if (netHost(g)) {  // tell everyone: who plays where, and go
        NetRound r;
        r.kind = 0;
        r.flags = uint8_t((halfTime ? kRoundHalf : 0) | (c.round == 0 ? kRoundNewMatch : 0));
        for (int k = 0; k < kNetSlots; ++k) {
            r.team[k] = g.netTeam[k] < 0 ? 255 : uint8_t(g.netTeam[k]);
            const bool alive = k == 0 || (size_t(k) < g.dummies.size() && g.dummies[size_t(k)].alive());
            if (alive && g.netTeam[k] >= 0) r.alive |= 1u << k;
        }
        for (int k = 0; k < kNetMaxPlayers; ++k) r.spawn[k] = uint8_t(k == 0 ? 0 : g.netSpawn[k]);
        r.sideA = uint8_t(c.youTeam);
        r.nameOffset = uint8_t(g_botNameOffset);
        g.net.sendRound(r);
    }
}

int g_compStartSide = 0;  // --ct (testing): which side you start a competitive match on

void startCompMatch(Game& g) {
    Game::Comp& c = g.comp;
    c = Game::Comp{};
    c.youTeam = g_compStartSide;
    g.team.assign(g.dummies.size(), 0);
    for (size_t i = 0; i < g.team.size(); ++i) g.team[i] = int(i) < g.compMates ? c.youTeam : 1 - c.youTeam;
    c.botMoney.assign(g.dummies.size(), 800);
    c.botGun.assign(g.dummies.size(), uint8_t(kWPistol));
    c.youDead = true;
    resetRecord(g);
    for (Dummy& d : g.dummies) d.respawnLeft = 1.0f;  // "died": no kit to carry over
    startCompRound(g);
}


// The round is decided: money, MVP, score; then the next round or the end of the match.
void recordMatch(Game& g, float score, const std::string& result);  // (your career, below)

void endCompRound(Game& g, int winner, const char* why, bool bombReason) {
    g.replay.mark(g.comp.roundStartAt);  // the viewer starts at the round that just ended
    Game::Comp& c = g.comp;
    if (c.phase >= 2) return;
    if (!g_compLog.empty()) {
        int alive[2] = {0, 0};
        for (size_t i = 0; i < g.dummies.size(); ++i) alive[g.team[i]] += g.dummies[i].alive();
        char line[200];
        std::snprintf(line, sizeof(line), "round %2d  %s wins: %-30s planted %d  t=%.0fs  you %s  bots alive %dT %dCT  bomb %s\n",
                      c.round + 1, winner == 0 ? "T " : "CT", why, int(c.planted), g.simTime, c.youTeam == 0 ? "T" : "CT",
                      alive[0], alive[1], c.carrier == -1 ? "you" : c.carrier == -2 ? "dropped" : c.carrier >= 0 ? "bot" : "-");
        std::ofstream(g_compLog, std::ios::app) << line;
        if (c.carrier >= 0 && !c.planted) {  // a bot had the bomb and didn't plant: where, doing what
            const Dummy& d = g.dummies[size_t(c.carrier)];
            const BotBrain& b = g.bots[size_t(c.carrier)];
            char info[160];
            std::snprintf(info, sizeof(info), "  carrier at %s (%.0f, %.0f) state %d sees %d goal %d path %zu urgent %d\n",
                          townCallout(d.pos), double(d.pos.x), double(d.pos.y), b.state, int(b.sees), int(b.hasGoal),
                          b.path.size(), int(b.urgent));
            std::ofstream(g_compLog, std::ios::app) << info;
        }
    }
    const int loser = 1 - winner;
    c.lossStreak[winner] = 0;
    const int lossBonus = std::min(1400 + 500 * c.lossStreak[loser], 3400);
    c.lossStreak[loser] = std::min(c.lossStreak[loser] + 1, 4);
    const int sidePay[2] = {winner == 0 ? (bombReason ? 3500 : 3250) : lossBonus + (c.planted ? 800 : 0),
                            winner == 1 ? (bombReason ? 3500 : 3250) : lossBonus};
    auto pay = [&](int id) { addMoney(g, id, sidePay[teamOf(g, id)]); };
    pay(-1);
    for (size_t i = 0; i < g.dummies.size(); ++i) pay(int(i));
    // MVP: the winning side's top fragger this round.
    int mvp = -2, best = -1;
    if (teamOf(g, -1) == winner) { mvp = -1; best = g.you.roundKills; }
    for (size_t i = 0; i < g.dummies.size(); ++i)
        if (g.team[i] == winner && g.botStats[i].roundKills > best) { best = g.botStats[i].roundKills; mvp = int(i); }
    if (mvp >= -1) statsOf(g, mvp).mvps++;
    if (netHost(g)) {  // everyone: who won, why, their pay and the MVP
        NetRound r;
        r.kind = 1;
        r.winnerSide = uint8_t(winner);
        r.why = roundWhyCode(why);
        r.mvp = mvp >= -1 ? localToNet(g, mvp) : 255;
        r.pay[0] = uint16_t(sidePay[0]);
        r.pay[1] = uint16_t(sidePay[1]);
        r.sideA = uint8_t(c.youTeam);
        g.net.sendRound(r);
    }
    (winner == c.youTeam ? c.youScore : c.themScore)++;
    c.resultWin = winner == c.youTeam;
    c.resultText = why;
    c.round++;
    const bool over = c.youScore >= kCompRoundsToWin || c.themScore >= kCompRoundsToWin || c.round >= 24;
    c.phase = over ? 3 : 2;
    c.phaseEnd = g.simTime + (over ? 10.0 : 5.0);
    if (over) {
        const bool won = c.youScore > c.themScore, draw = c.youScore == c.themScore;
        recordMatch(g, won ? 1.0f : draw ? 0.5f : 0.0f,
                    std::string(won ? "WON " : draw ? "DRAW " : "LOST ") + std::to_string(c.youScore) + "-" + std::to_string(c.themScore));
    }
    g.hudDirty = true;
}

// The buy menu, like CS: 1 pistols, 2 shotguns, 3 SMGs, 4 rifles, 5 snipers, 6 gear, 7 grenades; then the item's
// number. Outside competitive only the guns (1-5).
// Competitive: during buy time in your spawn, for money. Everywhere else: guns for free, any time.
struct BuyEntry { const char* name; int weapon; int gear; int price; };  // a gun (WeaponId) or gear (0..7)
enum BuyCategory { kCatPistols = 1, kCatShotguns, kCatSmgs, kCatRifles, kCatSnipers, kCatGear, kCatGrenades };
std::vector<BuyEntry> buyEntries(int category) {
    auto gun = [](int id) { return BuyEntry{weaponDef(id).name, id, -1, weaponDef(id).price}; };
    switch (category) {
        case kCatPistols: return {gun(kWPistol), gun(kWBerettas), gun(kWDeagle)};
        case kCatShotguns: return {gun(kWNova), gun(kWXm1014)};
        case kCatSmgs: return {gun(kWMac10), gun(kWUmp45)};
        case kCatRifles: return {gun(kWGalil), gun(kWRifle), gun(kWM4A1S)};
        case kCatSnipers: return {gun(kWSsg08), gun(kWSniper)};
        case kCatGear: return {{"KEVLAR", -1, 0, 650}, {"KEVLAR + HELMET", -1, 1, 1000}, {"DEFUSE KIT (CT)", -1, 2, 400}};
        case kCatGrenades: return {{"SMOKE", -1, 3, 300}, {"FLASHBANG", -1, 4, 200}, {"HE GRENADE", -1, 5, 300}, {"MOLOTOV", -1, 6, 400}};
        default: return {};
    }
}
const char* const kBuyCategories[] = {"PISTOLS", "SHOTGUNS", "SMGS", "RIFLES", "SNIPERS", "GEAR", "GRENADES"};

// Where gun `id` sits in a buy category (-1 if it isn't there).
int buyIndexOf(int category, int id) {
    const std::vector<BuyEntry> e = buyEntries(category);
    for (size_t k = 0; k < e.size(); ++k)
        if (e[k].weapon == id) return int(k);
    return -1;
}

// Takes gun `id` into its slot (and out), full magazine.
void takeGun(Game& g, int id) {
    WeaponState& w = weaponState(g, id);
    w.ammo = w.def->magSize;
    w.reloadEndTime = -1;
    w.recoilIndex = 0;
    if (w.def->primary) {
        g.primary = &w;
        g.switchTo = 1;
    } else {
        g.secondary = &w;
        g.switchTo = 2;
    }
}

// Is entry `e` already yours (can't carry more)?
bool buyOwned(const Game& g, const BuyEntry& e) {
    const Game::Comp& c = g.comp;
    if (e.weapon >= 0) return weaponDef(e.weapon).primary ? c.ownPrimary == e.weapon : g.secondary->def->id == e.weapon;
    switch (e.gear) {
        case 0: return c.armor > 0;
        case 1: return c.armor > 0 && c.helmet;
        case 2: return c.kit || c.youTeam != 1;
        case 3: return c.nades[0] >= 1;
        case 4: return c.nades[1] >= 2;
        case 5: return c.nades[2] >= 1;
        default: return c.nades[3] >= 1;
    }
}

int buyPrice(const Game& g, const BuyEntry& e) {
    return e.gear == 1 && g.comp.armor > 0 && !g.comp.helmet ? 350 : e.price;  // just the helmet
}

// Buys entry `item` (0-based) of the open category. Returns a message for the hit log.
const char* compBuy(Game& g, int category, int item) {
    Game::Comp& c = g.comp;
    const std::vector<BuyEntry> entries = buyEntries(category);
    if (item < 0 || item >= int(entries.size())) return "";
    const BuyEntry& e = entries[size_t(item)];
    if (buyOwned(g, e)) return "CAN'T CARRY MORE";
    const int price = buyPrice(g, e);
    if (c.money < price) return "NOT ENOUGH MONEY";
    c.money -= price;
    if (e.weapon >= 0) {
        if (weaponDef(e.weapon).primary) c.ownPrimary = e.weapon;
        takeGun(g, e.weapon);
        return e.name;
    }
    switch (e.gear) {
        case 0: c.armor = 100; break;
        case 1: c.armor = 100; c.helmet = true; break;
        case 2: c.kit = true; break;
        default: c.nades[e.gear - 3]++; break;
    }
    return e.name;
}

// Sends a bot somewhere (unless it's already on its way there). `h`: a hold spot to take there, facing
// its look-at point (it holds from beside cover).
std::vector<int> g_sendCount;  // automated runs: goals given to each bot (stuck-bot log)
void sendBot(Game& g, size_t i, const Vec3& to, bool hold, const RetakeSpot* h = nullptr) {
    BotBrain& b = g.bots[i];
    if (b.hasGoal && length2d(b.goal - to) < 1.0f) return;
    if (g_sendCount.size() <= i) g_sendCount.resize(i + 1, 0);
    g_sendCount[i]++;
    b.goal = to;
    b.hasGoal = true;
    b.holdOnly = hold;
    if (hold) {  // its spot: it comes back here after a chase
        b.home = to;
        b.hasHome = true;
    }
    b.hasHoldLook = h != nullptr;
    if (h) {
        b.holdLook = townPoint(h->lookX, h->lookY);
        b.holdYaw = std::atan2(h->lookY - h->y, h->lookX - h->x) / kDegToRad;
    }
    b.holdCoverChecked = false;
    b.path.clear();
    if (b.state != 2) b.state = 0;
}

// A bot throws a grenade of `type` to go off at `target`: it tries throws round the straight line (the same
// flight as the real thing) and takes the one that ends closest. False if none gets within 150 units.
bool botThrow(Game& g, size_t i, int type, const Vec3& target) {
    Dummy& d = g.dummies[i];
    const Vec3 eye = d.pos + Vec3{0, 0, dummyEyeZ(d.crouch)};
    const float yaw0 = yawTo(eye, target), dist = length2d(target - eye);
    float bestErr = 1e30f;
    Vec3 bestStart, bestVel;
    for (int lob = 0; lob < (dist < 500.0f ? 2 : 1); ++lob)
        for (int yi = -1; yi <= 1; ++yi)
            for (int pi = 0; pi < 8; ++pi) {
                const float pitch = -72.0f + float(pi) * 10.0f, yaw = yaw0 + float(yi) * 2.5f;
                const Vec3 v = grenadeThrowVelocity(pitch, yaw, lob == 1 ? kNadeLob : 1.0f, Vec3{});
                const Vec3 start = eye + normalize(v) * 16.0f;
                const Vec3 end = predictGrenade(g.world, start, v, type);
                const float err = length2d(end - target) + std::fabs(end.z - target.z) * 0.5f;
                if (err < bestErr) { bestErr = err; bestStart = start; bestVel = v; }
            }
    if (bestErr > 150.0f) return false;
    g.nades.push_back({bestStart, bestVel, type, 0, int(i)});
    if (netHost(g)) g.net.sendNade(type, bestStart, bestVel, int(i));
    d.yaw = d.prevYaw = yawTo(eye, target);
    if (g.audio) g.audio->play3D(Sfx::Draw, eye, g.lastRenderEye, float(g.viewYaw), 1200.0f, 0.5f, 1.3f);
    return true;
}

// The bomb goes down at `at` (a bot, you, or online a player who planted it): 40 s, and the CTs retake.
void compPlanted(Game& g, const Vec3& at) {
    Game::Comp& c = g.comp;
    const std::vector<RetakeSite>& sites = townRetakeSites();
    c.planted = true;
    c.carrier = c.planter = -3;
    g.bombActive = true;
    g.bombPos = at;
    g.bombExplodeAt = g.simTime + 40.0;
    g.nextBeep = g.simTime;
    float best = 1e30f;  // whichever site it went down on
    for (size_t k = 0; k < sites.size(); ++k) {
        float dd = length2d(at - townPoint(sites[k].bombX, sites[k].bombY));
        if (dd < best) { best = dd; c.siteTarget = int(k); }
    }
    pushHitLog(g, "THE BOMB HAS BEEN PLANTED", 0xff6060);
    const RetakeSite& s = sites[size_t(c.siteTarget)];
    int k = 0;
    for (size_t i = 0; i < g.dummies.size(); ++i) {  // CT bots: retake the site
        if (!g.dummies[i].alive() || g.team[i] != 1 || !isBot(g, i) || (i < c.saving.size() && c.saving[i])) continue;
        const RetakeSpot& h = s.holds[size_t(k++) % s.holds.size()];
        sendBot(g, i, townPoint(h.x, h.y), true, &h);
    }
    // T bots: post-plant. Off the bomb (the planter too) and onto the site's holding spots, each watching a way
    // the CTs come in; the ones elsewhere come over.
    int t = int(s.holds.size()) - 1;
    for (size_t i = 0; i < g.dummies.size(); ++i) {
        if (!g.dummies[i].alive() || g.team[i] != 0 || !isBot(g, i) || g.bots[i].state == 2) continue;
        const RetakeSpot& h = s.holds[size_t(t-- + int(s.holds.size()) * 4) % s.holds.size()];
        sendBot(g, i, townPoint(h.x, h.y), true, &h);
    }
    g.hudDirty = true;
}

// You carry the bomb: hold E on either site for kCompPlantTime. True once it's planted.
bool youPlant(Game& g) {
    Game::Comp& c = g.comp;
    bool onSite = false;
    for (const RetakeSite& s : townRetakeSites())
        onSite |= length2d(g.player.origin - townPoint(s.bombX, s.bombY)) < 340.0f * townScale() + 60.0f;
    if (onSite && g.defuseHeld && g.player.onGround) {
        if (c.planter != -1) { c.planter = -1; c.plantStart = g.simTime; sound(g, Sfx::Defuse, 0.7f, 0.0f, 1.3f); }
        return g.simTime - c.plantStart >= kCompPlantTime;
    }
    if (c.planter == -1) {
        c.planter = -3;
        c.plantStart = -1;
    }
    return false;
}

// You're a CT by the planted bomb: hold E (5 s with a kit, 10 without). True once it's defused.
bool youDefuse(Game& g) {
    const bool near = length2d(g.player.origin - g.bombPos) < 72.0f && std::fabs(g.player.origin.z - g.bombPos.z) < 64.0f;
    if (near && g.defuseHeld && g.player.onGround) {
        if (g.defuseStart < 0) {
            g.defuseStart = g.simTime;
            if (g.audio) g.audio->play3D(Sfx::Defuse, g.bombPos, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.9f);
            makeNoise(g, g.bombPos, 1800.0f);
        }
        return g.simTime - g.defuseStart >= (g.comp.kit ? 5.0 : 10.0);
    }
    g.defuseStart = -1;
    return false;
}

// The planted bomb's beeps, faster as the fuse runs down.
void bombBeeps(Game& g) {
    if (g.simTime < g.nextBeep) return;
    g.nextBeep = g.simTime + std::clamp((g.bombExplodeAt - g.simTime) / 40.0, 0.1, 1.0);
    if (g.audio) g.audio->play3D(Sfx::BombBeep, g.bombPos, g.lastRenderEye, float(g.viewYaw), 2600.0f, 0.8f);
}

void bombExplodes(Game& g) {
    g.bombActive = false;
    g.defuseStart = -1;
    if (g.audio) g.audio->play3D(Sfx::Explosion, g.bombPos, g.lastRenderEye, float(g.viewYaw), 9000.0f, 1.0f, 0.7f);
    g.fx.burst(g.bombPos, 0xffa040, 3.0f);
}

// Online competitive, a joined game: the host runs the match; this one plants or defuses for its player (and
// tells the host), beeps the bomb and keeps the clock on screen.
void compClientTick(Game& g) {
    Game::Comp& c = g.comp;
    if (c.phase == 1 && !c.planted && c.carrier == -1 && youAlive(g) && !g.plantSent) {
        if (youPlant(g)) {
            g.net.sendPlant(g.player.origin);
            g.plantSent = true;
            c.planter = -3;
        }
    } else if (c.planter == -1) {
        c.planter = -3;
    }
    if (c.planted && g.bombActive) {
        bombBeeps(g);
        if (g.simTime >= g.bombExplodeAt) bombExplodes(g);
        else if (c.youTeam == 1 && youAlive(g) && !g.defuseSent && youDefuse(g)) {
            g.net.sendDefused();
            g.defuseSent = true;
            g.defuseStart = -1;
        }
    }
    int secs = int(std::max(0.0, (c.planted ? g.bombExplodeAt : c.phaseEnd) - g.simTime));
    if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
}

// Competitive, every tick: the phases, the bomb (carried, dropped, planted, defused, exploding) and who
// won the round. (Online, the host's game runs this for everyone.)
void compTick(Game& g) {
    Game::Comp& c = g.comp;
    const double now = g.simTime;
    // Teammate radio: what the bots on your side see and do, as short lines in the top-right feed.
    auto radio = [&](int bot, const std::string& msg) {
        if (bot >= 0 && size_t(bot) < g.team.size() && g.team[size_t(bot)] == c.youTeam)
            pushHitLog(g, agentName(bot) + ": " + msg, 0x90e0a0);
    };
    auto firstAlive = [&](int side) {
        for (size_t i = 0; i < g.dummies.size(); ++i)
            if (g.dummies[i].alive() && g.team[i] == side) return int(i);
        return -1;
    };
    if (c.phase == 1)  // trading: a bot in a fight makes its teammates nearby look that way too
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const BotBrain& b = g.bots[i];
            if (!g.dummies[i].alive() || !isBot(g, i) || !b.sees || b.target < 0) continue;
            for (size_t j = 0; j < g.dummies.size(); ++j) {
                BotBrain& mate = g.bots[j];
                if (j == i || !isBot(g, j) || g.team[j] != g.team[i] || !g.dummies[j].alive() || mate.state == 2) continue;
                if (length2d(g.dummies[j].pos - g.dummies[i].pos) > 600.0f) continue;
                mate.alertUntil = std::max(mate.alertUntil, now + 1.0);
                mate.lastSeen = g.dummies[size_t(b.target)].pos;
            }
        }
    if (c.phase == 1)  // enemy spotted: once per place, and not more than every few seconds
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const BotBrain& b = g.bots[i];
            if (!g.dummies[i].alive() || !isBot(g, i) || g.team[i] != c.youTeam || !b.sees || b.target < 0) continue;
            const char* where = townCallout(g.dummies[size_t(b.target)].pos);
            const bool fresh = std::strcmp(where, c.lastSpot) != 0 ? now - c.lastSpotAt > 2.0 : now - c.lastSpotAt > 8.0;
            if (where[0] && fresh) {
                radio(int(i), std::string("ENEMY SPOTTED: ") + where);
                c.lastSpot = where;
                c.lastSpotAt = now;
            }
            break;
        }
    if (c.phase == 0) {
        if (now >= c.phaseEnd) { c.phase = 1; c.phaseEnd = now + kCompRoundTime; g.hudDirty = true; }
        return;
    }
    if (c.phase >= 2) {
        if (now >= c.phaseEnd) c.phase == 3 ? startCompMatch(g) : startCompRound(g);
        return;
    }
    const std::vector<RetakeSite>& sites = townRetakeSites();
    const Vec3 bombSpot = townPoint(sites[size_t(c.siteTarget)].bombX, sites[size_t(c.siteTarget)].bombY);
    // The carrier dies: the bomb drops where they fell.
    if (c.carrier >= 0 && !g.dummies[size_t(c.carrier)].alive()) { c.dropped = g.dummies[size_t(c.carrier)].pos; c.carrier = -2; }
    if (c.carrier == -1 && c.youDead) { c.dropped = g.player.origin; c.carrier = -2; }
    // Urgency: with 45 s left and no plant every T commits (no hiding, straight back on the way after a
    // fight); the carrier always does once they go; after a plant the CTs retake the same way.
    const bool late = !c.planted && c.phaseEnd - now < 45.0;
    if (late && !c.executing) c.executeAt = now;
    for (size_t i = 0; i < g.dummies.size(); ++i)
        g.bots[i].urgent = g.team[i] == 0 ? (late || (c.executing && int(i) == c.carrier)) : c.planted;
    // Saving: a bot whose round is lost (no time left to plant or to defuse, or alone against three) runs back to its
    // spawn and keeps its gun for the next round, like CS players - if the gun is worth keeping.
    {
        int alive[2] = {0, 0};
        for (size_t i = 0; i < g.dummies.size(); ++i) alive[g.team[i]] += g.dummies[i].alive();
        if (youAlive(g)) alive[c.youTeam]++;
        const double left = (c.planted ? g.bombExplodeAt : c.phaseEnd) - now;
        // Ts: how long the bomb needs to get onto a site and go down.
        float bombTrip = c.carrier >= -2 ? 1e9f : 0.0f;  // (nobody has it: no call)
        const Vec3 bombAt = c.carrier >= 0 ? g.dummies[size_t(c.carrier)].pos : c.carrier == -1 ? g.player.origin : c.dropped;
        if (c.carrier >= -2)
            for (const RetakeSite& site : sites)
                bombTrip = std::min(bombTrip, length2d(bombAt - townPoint(site.bombX, site.bombY)) / kBotRunSpeed * 1.3f + float(kCompPlantTime));
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            if (!g.dummies[i].alive() || !isBot(g, i) || i >= c.saving.size() || c.saving[i]) continue;
            if (weaponDef(c.botGun[i]).price < 1700) continue;  // a pistol or an SMG: play it out
            const int side = g.team[i];
            bool lost = alive[side] == 1 && alive[1 - side] >= 3 && left < 50.0;
            if (side == 0 && !c.planted) lost = lost || bombTrip > float(left);
            if (side == 1 && c.planted)  // can't reach the bomb and defuse in time
                lost = lost || length2d(g.dummies[i].pos - g.bombPos) / kBotRunSpeed * 1.2f + 5.0f > float(left);
            if (!lost || int(i) == c.carrier || int(i) == c.planter || int(i) == c.defuser) continue;
            c.saving[i] = 1;
            const std::vector<Vec3>& home = townTeamSpawns(side);
            sendBot(g, i, home[i % home.size()], true);
            radio(int(i), "SAVING MY " + std::string(weaponDef(c.botGun[i]).name));
            if (!g_compLog.empty())
                std::ofstream(g_compLog, std::ios::app) << "  " << (side == 0 ? "T" : "CT") << " bot " << i << " saves its "
                                                        << weaponDef(c.botGun[i]).name << " t=" << int(now) << "s\n";
        }
        for (size_t i = 0; i < g.dummies.size() && i < c.saving.size(); ++i)
            if (c.saving[i]) g.bots[i].urgent = false;
    }
    // CT utility: once the Ts are coming onto a site, a CT who sees them throws its molotov or HE at them (into the
    // way they come), then fights.
    if (c.executing && !c.planted)
        for (size_t i = 0; i < g.dummies.size() && i < c.ctNade.size(); ++i) {
            const BotBrain& b = g.bots[i];
            if (c.ctNade[i] < 0 || !g.dummies[i].alive() || !isBot(g, i) || g.team[i] != 1 || !b.sees || now < c.nadeTry[i]) continue;
            const Vec3 at = b.target >= 0 && size_t(b.target) < g.dummies.size() ? g.dummies[size_t(b.target)].pos
                            : b.target == -1 ? g.player.origin : b.lastSeen;
            const float dist = length2d(at - g.dummies[i].pos);
            c.nadeTry[i] = now + 0.5;
            if (dist < 350.0f || dist > 1300.0f) continue;  // too close to throw at, or too far to reach
            if (!botThrow(g, i, c.ctNade[i], at)) continue;
            if (!g_compLog.empty())
                std::ofstream(g_compLog, std::ios::app) << "  CT bot " << i << " threw a "
                                                        << (c.ctNade[i] == Game::kMolotov ? "molotov" : "HE") << " at the execute t="
                                                        << int(now) << "s\n";
            radio(int(i), c.ctNade[i] == Game::kMolotov ? "MOLLY OUT" : "HE OUT");
            c.ctNade[i] = -1;
        }
    // Utility for a way in (`route`): a smoke to cut the defenders' view, then a flash over where they hold,
    // thrown by the two T bots nearest that way's staging point (fakers only, or everyone but the fakers).
    auto siteUtility = [&](int route, bool byFakers) {
        const auto& kUtil = townTactics().util;
        const int r = route >= 2 ? 2 : route;
        const Vec3 from0 = townPoint(townTactics().stages[r][0], townTactics().stages[r][1]);
        std::vector<std::pair<float, size_t>> near;
        for (size_t i = 0; i < g.dummies.size(); ++i)
            if (g.dummies[i].alive() && isBot(g, i) && g.team[i] == 0 && int(i) != c.carrier &&
                (i >= c.faker.size() || (c.faker[i] != 0) == byFakers))
                near.push_back({length2d(g.dummies[i].pos - from0), i});
        std::sort(near.begin(), near.end());
        for (size_t q = 0; q < near.size() && q < 2; ++q) {
            const float* u = kUtil[r][q];
            const Vec3 target = townPoint(u[0], u[1]) + Vec3{0, 0, u[2]};
            const double from = now + 0.3 + 0.7 * double(q);
            c.throws.push_back({int(near[q].second), q == 0 ? Game::kSmokeNade : Game::kFlashNade, target, from, from + 10.0, from});
        }
    };
    // CT pushers fall back to their spots once their time's up or the Ts go.
    if (c.pushUntil >= 0 && (now >= c.pushUntil || c.executing)) {
        c.pushUntil = -1;
        for (size_t k = 0; k < c.pushers.size(); ++k) {
            const size_t i = size_t(c.pushers[k]);
            if (!g.dummies[i].alive()) continue;
            const RetakeSpot& h = c.pushBack[k];
            sendBot(g, i, townPoint(h.x, h.y), true, &h);
            radio(int(i), "FALLING BACK");
        }
    }
    // The T execute: once every T bot is at its staging point (a rush: once anyone is; or the timer), the
    // fakers throw their utility at the other site and a few seconds later (or straight away) everyone
    // pushes onto the site together - the carrier to the bomb spot, the rest to the site's holding spots.
    if (!c.planted && !c.executing) {
        bool gathered = true, anyThere = false;
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            if (!g.dummies[i].alive() || !isBot(g, i) || g.team[i] != 0) continue;
            const size_t group = i < c.stageOf.size() && c.stageOf[i] < int(c.stages.size()) ? size_t(c.stageOf[i]) : 0;
            const float dist = c.stages.empty() ? 0.0f : length2d(g.dummies[i].pos - c.stages[group]);
            if (dist > 200.0f) gathered = false;
            if (dist < 300.0f) anyThere = true;
        }
        if (c.rush) gathered = gathered || anyThere;
        if (c.play == kPlayDefault) gathered = false;  // map control until the clock says go
        if (gathered || now >= c.executeAt) {
            c.executing = true;
            if (!g_compLog.empty())
                std::ofstream(g_compLog, std::ios::app)
                    << "  execute " << (c.siteTarget == 0 ? "A" : "B") << (gathered ? " (gathered)" : " (timer)")
                    << " t=" << int(g.simTime) << "s\n";
            c.goAt = now;
            if (c.fakeRoute >= 0) {  // the fake: their utility goes in on the other site, the team goes after
                siteUtility(c.fakeRoute, true);
                for (size_t i = 0; i < g.dummies.size(); ++i)
                    if (i < c.faker.size() && c.faker[i] && g.dummies[i].alive()) {
                        radio(int(i), "FAKING");
                        break;
                    }
                c.goAt = now + 4.5;
            }
        }
    }
    if (c.executing && !c.sent && !c.planted && now >= c.goAt) {
        c.sent = true;
        radio(c.carrier >= 0 ? c.carrier : firstAlive(0), c.siteTarget == 0 ? "GOING A" : "GOING B");
        siteUtility(c.route, false);
        {
            const RetakeSite& s = sites[size_t(c.siteTarget)];
            int k = 0;
            for (size_t i = 0; i < g.dummies.size(); ++i) {
                if (!g.dummies[i].alive() || !isBot(g, i) || g.team[i] != 0) continue;
                if (int(i) == c.carrier) {
                    sendBot(g, i, bombSpot, true);
                } else {
                    const RetakeSpot& h = s.holds[size_t(k++) % s.holds.size()];
                    sendBot(g, i, townPoint(h.x, h.y), true, &h);
                }
            }
        }
    }
    for (size_t q = 0; q < c.throws.size();) {
        Game::Comp::Throw& t = c.throws[q];
        const size_t i = size_t(t.bot);
        const bool gone = now > t.until || c.planted || !g.dummies[i].alive();
        bool done = gone;
        if (!gone && now >= t.nextTry && g.bots[i].state != 2) {
            done = botThrow(g, i, t.type, t.target);
            t.nextTry = now + 0.4;  // no throw from here yet: try again in a moment (it's still walking)
            if (done && !g_compLog.empty())
                std::ofstream(g_compLog, std::ios::app) << "  bot threw a " << (t.type == Game::kSmokeNade ? "smoke" : "flash")
                                                        << " t=" << int(now) << "s\n";
        }
        if (done) c.throws.erase(c.throws.begin() + long(q));
        else ++q;
    }
    // CT rotation: once a T is on the site being hit, the CT bots away from it come over to help.
    if (!c.planted && c.executing && !c.rotated) {
        const float onSite = 700.0f * townScale();
        bool hit = c.youTeam == 0 && youAlive(g) && length2d(g.player.origin - bombSpot) < onSite;
        for (size_t i = 0; i < g.dummies.size(); ++i)
            hit |= g.dummies[i].alive() && g.team[i] == 0 && length2d(g.dummies[i].pos - bombSpot) < onSite;
        if (hit) {
            c.rotated = true;
            radio(firstAlive(1), c.siteTarget == 0 ? "THEY'RE ON A, ROTATING" : "THEY'RE ON B, ROTATING");
            const RetakeSite& s = sites[size_t(c.siteTarget)];
            int k = 0;
            for (size_t i = 0; i < g.dummies.size(); ++i) {
                if (!g.dummies[i].alive() || !isBot(g, i) || g.team[i] != 1 || length2d(g.dummies[i].pos - bombSpot) < 2.0f * onSite)
                    continue;
                const RetakeSpot& h = s.holds[size_t(k++) % s.holds.size()];
                sendBot(g, i, townPoint(h.x, h.y), true, &h);
            }
        }
    }
    if (!c.planted) {
        if (c.carrier == -2) {  // dropped: the first T over it picks it up; the nearest T bot goes for it
            if (c.youTeam == 0 && youAlive(g) && length2d(g.player.origin - c.dropped) < 48.0f) {
                c.carrier = -1;
                pushHitLog(g, "YOU PICKED UP THE BOMB", 0xffd060);
            } else {
                int nearest = -1;
                float best = 1e30f;
                for (size_t i = 0; i < g.dummies.size(); ++i) {
                    if (!g.dummies[i].alive() || g.team[i] != 0) continue;
                    float dd = length2d(g.dummies[i].pos - c.dropped);
                    if (dd < 48.0f) { c.carrier = int(i); break; }  // (online players pick it up too)
                    if (dd < best && isBot(g, i)) { best = dd; nearest = int(i); }
                }
                if (c.carrier == -2 && nearest >= 0) {
                    // Go for the nearest standing room beside it (it can lie where nobody stands: by a crate,
                    // off a ledge); pickup reaches 48 units.
                    Vec3 to = c.dropped;
                    float bestD = 1e30f;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            Vec3 p = c.dropped + Vec3{float(dx) * 32.0f, float(dy) * 32.0f, 0};
                            const float dd = length2d(p - c.dropped);
                            if (dd < bestD && g.nav.standable(p)) { bestD = dd; to = p; to.z = g.nav.floorAt(p); }
                        }
                    sendBot(g, size_t(nearest), to, true);
                }
            }
        }
        if (c.carrier >= 0 && isBot(g, size_t(c.carrier))) {  // a bot carrier walks to the bomb spot and plants there
            const Dummy& d = g.dummies[size_t(c.carrier)];
            if (length2d(d.pos - bombSpot) < 40.0f) {
                if (c.planter != c.carrier) {
                    c.planter = c.carrier;
                    c.plantStart = now;
                    radio(c.carrier, "PLANTING THE BOMB");
                }
            } else if (c.executing && g.bots[size_t(c.carrier)].state != 2) {
                sendBot(g, size_t(c.carrier), bombSpot, true);
            }
        } else if (c.carrier == -1 && youAlive(g)) {  // you: hold E on either site
            if (youPlant(g)) {
                compPlanted(g, g.player.origin);
                return;
            }
        }
        if (c.planter >= 0 && !g.dummies[size_t(c.planter)].alive()) { c.planter = -3; c.plantStart = -1; }
        if (c.planter >= 0 && now - c.plantStart >= kCompPlantTime) compPlanted(g, g.dummies[size_t(c.planter)].pos);
    } else if (g.bombActive) {
        bombBeeps(g);
        if (now >= g.bombExplodeAt) {
            bombExplodes(g);
            endCompRound(g, 0, "THE BOMB EXPLODED", true);
            return;
        }
        if (c.youTeam == 1 && youAlive(g) && youDefuse(g)) {  // you defuse with E
            g.bombActive = false;
            g.defuseStart = -1;
            endCompRound(g, 1, "THE BOMB HAS BEEN DEFUSED", true);
            return;
        }
        // A CT bot defuses: the nearest one that isn't fighting walks to the bomb, then takes 5 s.
        if (c.defuser >= 0 && (!g.dummies[size_t(c.defuser)].alive() || g.bots[size_t(c.defuser)].sees)) {
            c.defuser = -3;
            c.botDefuseStart = -1;
        }
        if (c.defuser < 0) {
            int nearest = -1;
            float best = 1e30f;
            for (size_t i = 0; i < g.dummies.size(); ++i) {
                if (!g.dummies[i].alive() || !isBot(g, i) || g.team[i] != 1 || g.bots[i].state == 2) continue;
                if (i < c.saving.size() && c.saving[i]) continue;
                float dd = length2d(g.dummies[i].pos - g.bombPos);

                if (dd < best) { best = dd; nearest = int(i); }
            }
            if (nearest >= 0 && best < 40.0f) {
                c.defuser = nearest;
                radio(nearest, "DEFUSING");
                c.botDefuseStart = now;
                if (g.audio) g.audio->play3D(Sfx::Defuse, g.bombPos, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.9f);
            } else if (nearest >= 0) {
                sendBot(g, size_t(nearest), g.bombPos, true);
            }
        } else if (now - c.botDefuseStart >= 5.0) {
            g.bombActive = false;
            endCompRound(g, 1, "THE BOMB HAS BEEN DEFUSED", true);
            return;
        }
        // The Ts hear the defuse start (a bot's, or yours) and come for the bomb.
        const bool defusing = c.defuser >= 0 || (c.youTeam == 1 && g.defuseStart >= 0);
        if (defusing && !c.defuseHeard) {
            c.defuseHeard = true;
            for (size_t i = 0; i < g.dummies.size(); ++i)
                if (g.dummies[i].alive() && isBot(g, i) && g.team[i] == 0 && g.bots[i].state != 2 &&
                    !(i < c.saving.size() && c.saving[i]) && length2d(g.dummies[i].pos - g.bombPos) < 2000.0f)
                    sendBot(g, i, g.bombPos, false);
            radio(firstAlive(0), "THEY'RE DEFUSING");
        } else if (!defusing) {
            c.defuseHeard = false;
        }
    }
    // Automated runs: a bot that hasn't moved for 8 s while it has somewhere to be (or is chasing a lead) is
    // logged once a round, with what its brain is doing (tracking down bots that freeze).
    if (!g_compLog.empty() && c.phase == 1) {
        static std::vector<Vec3> lastPos;
        static std::vector<double> stillSince;
        static std::vector<int> loggedRound;
        lastPos.resize(g.dummies.size());
        stillSince.resize(g.dummies.size(), now);
        loggedRound.resize(g.dummies.size(), -1);
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            const BotBrain& b = g.bots[i];
            // (walking somewhere or chasing a lead; holding an angle or fighting doesn't count)
            const bool shouldMove = (b.state == 0 && b.hasGoal) || b.state == 3 || (b.state == 0 && !b.holdOnly);
            if (!d.alive() || !shouldMove || length2d(d.pos - lastPos[i]) > 24.0f) {
                lastPos[i] = d.pos;
                stillSince[i] = now;
                continue;
            }
            if (now - stillSince[i] > 8.0 && loggedRound[i] != c.round && int(i) != c.planter && int(i) != c.defuser) {
                loggedRound[i] = c.round;
                std::vector<Vec3> probe;
                const bool pathOk = g.nav.findPath(d.pos, b.hasGoal ? b.goal : b.lastSeen, probe);
                char info[320];
                std::snprintf(info, sizeof(info),
                              "  STUCK bot %zu %s at %s (%.0f, %.0f, %.0f) state %d goal %d (%.0f, %.0f) %.0f away, goal standable %d, "
                              "path %zu/%zu hold %d urgent %d sees %d standable %d path-from-here %d sends %d carrier %d dropped %d "
                              "planted %d lastSaw %.1fs ago coverFor %d cover %d/%d t=%.0fs\n",
                              i, g.team[i] == 0 ? "T" : "CT", townCallout(d.pos), double(d.pos.x), double(d.pos.y), double(d.pos.z),
                              b.state, int(b.hasGoal), double(b.goal.x), double(b.goal.y), double(length2d(b.goal - d.pos)),
                              int(g.nav.standable(b.goal)), b.next, b.path.size(), int(b.holdOnly), int(b.urgent), int(b.sees),
                              int(g.nav.standable(d.pos)), int(pathOk), i < g_sendCount.size() ? g_sendCount[i] : 0, c.carrier,
                              int(c.carrier == -2), int(c.planted), now - b.lastSawAt, b.coverFor, int(b.hasCover), int(b.inCover), now);

                std::ofstream(g_compLog, std::ios::app) << info;
            }
        }
    }
    // Eliminations and the clock.
    int alive[2] = {0, 0};
    for (size_t i = 0; i < g.dummies.size(); ++i) alive[g.team[i]] += g.dummies[i].alive();
    if (youAlive(g)) alive[c.youTeam]++;
    if (alive[1] == 0) endCompRound(g, 0, "COUNTER-TERRORISTS ELIMINATED", false);
    else if (alive[0] == 0 && !c.planted) endCompRound(g, 1, "TERRORISTS ELIMINATED", false);
    else if (!c.planted && now >= c.phaseEnd) endCompRound(g, 1, "TIME RAN OUT", false);
    int secs = int(std::max(0.0, (c.planted ? g.bombExplodeAt : c.phaseEnd) - now));
    if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
}

// ---- Online competitive, a joined game: what the host says ----

// Who's on which side, who's alive, and which of them are on your team.
void netApplyRoster(Game& g, const NetRound& r) {
    const int me = g.net.myId();
    g_botNameOffset = r.nameOffset;
    for (int k = 0; k < kNetSlots; ++k) g.netTeam[k] = r.team[k] == 255 ? -1 : int(r.team[k] & 1);
    const int myTeam = g.netTeam[me] >= 0 ? g.netTeam[me] : 0;
    g.comp.youTeam = myTeam == 0 ? r.sideA : 1 - r.sideA;
    for (size_t k = 0; k < g.dummies.size() && k < size_t(kNetSlots); ++k) {
        Dummy& d = g.dummies[k];
        g.team[k] = g.netTeam[k] == 1 ? 1 - r.sideA : r.sideA;
        if (int(k) == me || g.netTeam[k] < 0 || !((r.alive >> k) & 1)) {
            d.respawnLeft = 1e9f;
            d.deadFor = 10.0f;
            continue;
        }
        if (!d.alive()) {
            d.respawnLeft = 0;
            d.deadFor = 0;
            d.prevPos = d.pos;
        }
        d.hp = 1e6f;  // the host (bots) or their own game (players) decides when they die
        d.friendly = g.team[k] == g.comp.youTeam;
        for (float& f : d.flash) f = 0;
    }
}

// A round starts: you at your spawn with your kit (or a pistol if you died), unless you're waiting to get in.
void netCompRoundStart(Game& g, const NetRound& r) {
    Game::Comp& c = g.comp;
    const int me = g.net.myId();
    if (r.flags & kRoundNewMatch) {
        resetRecord(g);
        c = Game::Comp{};
        c.youDead = true;
    }
    if (r.flags & kRoundHalf) {  // swapped sides: everyone starts over
        c.money = 800;
        c.youDead = true;
    }
    netApplyRoster(g, r);
    const bool playing = g.netTeam[me] >= 0;
    if (c.youDead || !playing) {
        c.armor = 0;
        c.helmet = c.kit = false;
        c.ownPrimary = -1;
        for (int& n : c.nades) n = 0;
        g.primary = &g.guns[kWRifle];
        g.secondary = &g.guns[kWPistol];
    }
    c.youDead = !playing;
    c.planted = false;
    c.carrier = c.planter = c.defuser = -3;
    c.plantStart = -1;
    c.phase = 0;
    c.phaseEnd = g.simTime + g.freezeTime;  // (the host's match messages set the real clock)
    c.buyUntil = g.simTime + g.freezeTime + kCompBuyTime;
    g.plantSent = g.defuseSent = false;
    g.bombActive = false;
    g.defuseStart = -1;
    g.nades.clear();
    g.smokes.clear();
    g.fires.clear();
    g.flashFull = g.flashEnd = 0;
    for (Game::Stats& st : g.botStats) st.roundKills = 0;
    g.you.roundKills = 0;
    const int side = c.youTeam;
    g.spawn = townTeamSpawns(side)[size_t(r.spawn[me]) % 5];
    g.spawnYaw = side == 0 ? 90.0f : -90.0f;
    if (playing) {
        g.noclip = false;
        g.spec = -1;
        g.hp = 100;
        g.deadUntil = -1;
        refillAmmo(g);
        resetPosition(g);
        g.switchTo = c.ownPrimary >= 0 ? 1 : 2;
    } else {  // joined mid-match: watch until the next round
        g.hp = 0;
        g.deadUntil = 1e18;
        g.noclip = true;
        g.spec = nextTeammate(g, -1, 1);
    }
    g.hudDirty = true;
}

// A round is over: the result, your pay and the MVP.
void netCompRoundEnd(Game& g, const NetRound& r) {
    Game::Comp& c = g.comp;
    if (g.netTeam[g.net.myId()] >= 0) c.money = std::clamp(c.money + int(r.pay[c.youTeam]), 0, 16000);
    c.resultWin = r.winnerSide == c.youTeam;
    c.resultText = kRoundWhy[std::min<int>(r.why, 4)];
    if (r.mvp < kNetSlots) {
        const int mvp = netToLocal(g, r.mvp);
        if (mvp < 0 || size_t(mvp) < g.botStats.size()) statsOf(g, mvp).mvps++;
    }
    if (r.why == 4) g.bombActive = false;  // defused
    c.phase = 2;
    g.hudDirty = true;
}

// The match as the host has it: the phase and clock, the score, where the bomb is.
void netCompMatch(Game& g, const NetMatch& m) {
    Game::Comp& c = g.comp;
    const int me = g.net.myId();
    const int myTeam = g.netTeam[me] == 1 ? 1 : 0;
    if (m.phase != c.phase) g.hudDirty = true;
    c.phase = m.phase;
    c.round = m.round;
    c.youTeam = myTeam == 0 ? m.sideA : 1 - m.sideA;
    c.youScore = m.score[myTeam];
    c.themScore = m.score[1 - myTeam];
    c.phaseEnd = g.simTime + double(m.phaseLeft);
    c.buyUntil = g.simTime + double(m.buyLeft);
    const int carrier = m.carrier == 254 ? -2 : m.carrier >= kNetSlots ? -3 : netToLocal(g, m.carrier);
    if (carrier == -1 && c.carrier != -1 && !c.planted) pushHitLog(g, "YOU HAVE THE BOMB", 0xffd060);
    c.carrier = carrier;
    c.dropped = m.dropped;
    if (m.planted && !c.planted) {  // it's just gone down
        g.bombActive = true;
        g.nextBeep = g.simTime;
        pushHitLog(g, "THE BOMB HAS BEEN PLANTED", 0xff6060);
    }
    c.planted = m.planted != 0;
    if (!m.bombActive) g.bombActive = false;  // (it only goes off or gets defused once: no coming back)
    if (g.bombActive) {
        g.bombPos = m.bombPos;
        g.bombExplodeAt = g.simTime + double(m.bombLeft);
    }
}

// Joined an online competitive game: everyone hidden, you watching, until the host's first round message.
void startNetCompClient(Game& g) {
    Game::Comp& c = g.comp;
    c = Game::Comp{};
    c.youDead = true;
    g.team.assign(g.dummies.size(), 0);
    c.botMoney.assign(g.dummies.size(), 0);
    c.botGun.assign(g.dummies.size(), uint8_t(kWPistol));
    for (int& t : g.netTeam) t = -1;
    for (Dummy& d : g.dummies) {
        d = Dummy{};
        d.respawnLeft = 1e9f;
        d.deadFor = 10.0f;
    }
    resetRecord(g);
    g.hp = 0;
    g.deadUntil = 1e18;
    g.noclip = true;
    g.spawn = townTeamSpawns(1)[0];
    resetPosition(g);
    g.hudDirty = true;
}

void loadMap(Game& g, Renderer& r, int id) {
    g.mapId = id;
    g.rv = Game::ReplayView{};  // no killcam or replay carried over from the last game
    g.killcamAt = -1;
    g.replay.clear();
    g.world = id == 1 ? buildTown() : buildLab();
    if (id == 1) {
        g.dummies.assign(g.mode == 1   ? size_t(g.dmBots)
                         : g.mode == 2 ? size_t(g.rtBots)
                         : g.mode == 3 ? size_t(g.online ? kNetSlots : g.compMates + g.compEnemies)
                         : g.mode == 5 ? size_t(kNetMaxPlayers)
                         : g.mode == 4 ? townPrefireRoutes()[size_t(std::clamp(g.pf.route, 0, int(townPrefireRoutes().size()) - 1))].bots.size()
                                       : 4,
                         Dummy{});
        for (Dummy& d : g.dummies) d.respawnLeft = 0.01f;  // spawn at a spot on the first tick
        g.spawn = townSpawn().pos;
        g.spawnYaw = townSpawn().yaw;
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
    resetRecord(g);
    g.drill = false;
    g.kzState = 0;
    g.nades.clear();
    g.smokes.clear();
    g.fires.clear();
    g.flashFull = g.flashEnd = 0;
    g.hp = 100;
    g.deadUntil = g.dmOverUntil = g.rtResultUntil = g.pf.resultUntil = -1;  // no result screen left over from another mode
    resetPosition(g);
    // The Lab keeps its dev grid; Dust gets surfaces by material (render.cpp: stone, wood, metal, plain).
    std::vector<BoxInstance> statics;
    auto addStatic = [&](const Box& b) {
        statics.push_back(makeBox(b.mins, b.maxs, b.color, true));
        if (id == 1) {
            static const uint8_t kSurface[5] = {240, 224, 208, 0, 236};  // stone, wood, metal, plain, paving
            statics.back().rgba[3] = kSurface[std::min<int>(b.material, 4)];
        }
        statics.back().slope[0] = float(b.slope);
        statics.back().slope[1] = b.lowZ;
    };
    for (const Box& b : g.world.solids) addStatic(b);
    for (const Box& b : g.world.decor) addStatic(b);
    r.setStaticBoxes(statics);
    r.clearDecals();
    if (id == 1) g.nav.build(townGrid(), g.world, townSpawn().pos);
    if (id == 1) buildRadar(g);
    g.spottedUntil.assign(g.dummies.size(), -1.0);
    if (id == 1)  // particles land on Dust's floors (which aren't all at height 0)
        g.fx.setGround([](float x, float y) {
            float z = townGrid().floorAt(x, y);
            return z > MapGrid::kNoFloor ? z : -1e9f;
        });
    else
        g.fx.setGround(nullptr);
    if (id == 1 && g.mode == 1) startDeathmatch(g);
    if (id == 1 && g.mode == 2) {
        g.rtWon = g.rtLost = 0;
        startRetakeRound(g);
    }
    g_botNameOffset = int(rnd(g) * float(kBotNameCount)) % kBotNameCount;  // new names every match
    for (Game::Remote& rm : g.remotes) rm = Game::Remote{};
    for (int& t : g.netTeam) t = -1;
    if (id == 1 && g.mode == 3) netClient(g) ? startNetCompClient(g) : startCompMatch(g);
    if (id == 1 && g.mode == 4) startPrefire(g);
    if (id == 1 && g.mode == 5) startOnline(g);
    g_onlineNames = g.online && id == 1;

    g.hudDirty = true;
}

void resetGame(Game& g, const Options& opt) {
    g.world = buildLab();
    g.dummies = buildDummies();
    g.player = {};
    g.player.origin = opt.spawnOverride ? Vec3{opt.spawnX, opt.spawnY, 0} : Vec3{0, 0, 0};
    g.player.onGround = true;
    g.prevPlayer = g.player;
    g.viewYaw = opt.spawnOverride ? opt.spawnYaw : 0;
    for (int id = 0; id < kWeaponCount; ++id) {
        g.guns[id].def = &weaponDef(id);
        g.guns[id].ammo = weaponDef(id).magSize;
    }
    g.spawn = g.player.origin;
    g.spawnYaw = float(g.viewYaw);
    g.aliveSince.assign(g.dummies.size(), 0.0);
    g.botSeen.assign(g.dummies.size(), 0.0f);
    g.botCooldown.assign(g.dummies.size(), 0.0f);
    if (opt.startWeapon == 4) g.primary = &g.guns[kWSniper];
    g.switchTo = opt.startWeapon == 4 ? 1 : opt.startWeapon == 5 ? 4 : opt.startWeapon;
    if (opt.startWeapon >= 6 && opt.startWeapon <= 14) {  // 6 Berettas, 7 Deagle, 8 Nova, 9 MAC-10, 10.. the newer ones
        const int ids[9] = {kWBerettas, kWDeagle, kWNova, kWMac10, kWM4A1S, kWGalil, kWSsg08, kWUmp45, kWXm1014};
        WeaponState& w = weaponState(g, ids[opt.startWeapon - 6]);
        if (w.def->primary) g.primary = &w;
        else g.secondary = &w;
        g.switchTo = w.def->primary ? 1 : 2;
    }
    g.weapon = &g.guns[kWRifle];
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

// ---- Inventory: skins, knives and cases (items.h). Saved in inventory.txt next to the game. ----
Inventory g_inv;
std::string g_invPath;    // empty: nothing is saved
int g_caseKills = 25;     // config case_kills
bool g_allSkins = false;  // config all_skins: every skin to pick from (testing)
double g_uiTime = 0;      // real seconds (menus animate while the game is paused)

// ---- Your career: finished matches against bots, your stats and rating (stats.h, stats.txt) ----
Career g_career;
std::string g_careerPath;    // empty in automated runs: tests never touch your stats
std::string g_ratingNote;    // shown on the result screen: "+16 RATING   1046   CORPORAL"

std::string nowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// A finished match against bots (offline deathmatch or competitive on Dust): your stats, how it went (0..1)
// and the rating change, saved straight away.
void recordMatch(Game& g, float score, const std::string& result) {
    g_ratingNote.clear();
    if (g.online || g.mapId != 1 || (g.mode != 1 && g.mode != 3)) return;
    MatchRecord m;
    m.mode = g.mode;
    m.score = score;
    m.kills = g.you.kills;
    m.deaths = g.you.deaths;
    m.assists = g.you.assists;
    m.hsKills = g.you.hsKills;
    m.damage = g.you.damage;
    m.rounds = g.mode == 3 ? std::max(1, g.comp.round) : 0;
    m.botLevel = float(g.enemySkill);
    m.when = nowText();
    m.result = result;
    const MatchRecord& r = g_career.add(m);
    if (!g_careerPath.empty()) g_career.save(g_careerPath);
    char buf[96];
    const int d = r.ratingAfter - r.ratingBefore;
    std::snprintf(buf, sizeof(buf), "%s%d RATING   %d   %s", d >= 0 ? "+" : "", d, r.ratingAfter, rankName(r.ratingAfter));
    g_ratingNote = buf;
}

// Deathmatch over: your place among everyone by kills (1st of 9 = 1.0, last = 0).
void recordDeathmatch(Game& g) {
    int above = 0;
    for (const Game::Stats& b : g.botStats) above += b.kills > g.you.kills;
    const int n = int(g.botStats.size()) + 1;
    const char* suffix = above == 0 ? "ST" : above == 1 ? "ND" : above == 2 ? "RD" : "TH";
    recordMatch(g, n > 1 ? 1.0f - float(above) / float(n - 1) : 1.0f,
                std::to_string(above + 1) + suffix + " OF " + std::to_string(n));
}

void saveInventory() {
    if (!g_invPath.empty()) g_inv.save(g_invPath);
}

// The paint for an equipped skin (pattern -1 = plain). The pattern's offset comes from the wear, so two of
// the same skin rarely look exactly alike.
PaintParams paintFor(const Equipped& e) {
    PaintParams p;
    if (e.skin < 0 || size_t(e.skin) >= allSkins().size()) return p;
    const SkinDef& s = allSkins()[size_t(e.skin)];
    auto rgb = [](uint32_t c, float (&out)[3]) {
        out[0] = float((c >> 16) & 0xFF) / 255.0f;
        out[1] = float((c >> 8) & 0xFF) / 255.0f;
        out[2] = float(c & 0xFF) / 255.0f;
    };
    p.pattern = s.pattern;
    rgb(s.a, p.a);
    rgb(s.b, p.b);
    rgb(s.c, p.c);
    p.wear = e.wear;
    p.gloss = s.gloss;
    p.seed = std::fmod(e.wear * 977.0f, 10.0f);
    return p;
}

// Puts what you have equipped on your weapons (and your knife in your hand).
void applySkins(Game& g) {
    for (int id = 0; id < kWeaponCount; ++id)
        if (id != kWGrenade) g.vm.setSkin(viewWeaponFor(id), paintFor(g_inv.equip[id]));
    const Equipped& k = g_inv.equip[kWKnife];
    g.vm.setKnife(k.skin >= 0 && size_t(k.skin) < allSkins().size() ? allSkins()[size_t(k.skin)].knife : kKnifeDefault);
}

bool ownsSkin(int skin) {
    if (g_allSkins) return true;
    for (const Item& it : g_inv.items)
        if (it.skin == skin) return true;
    return false;
}

// With every skin unlocked switched off, take off anything you don't own.
void validateEquips(Game& g) {
    for (Equipped& e : g_inv.equip)
        if (e.skin >= 0 && !ownsSkin(e.skin)) e = Equipped{};
    applySkins(g);
}

// A kill of yours in a mode that counts (deathmatch, retakes, competitive, online; not the range, the aim
// drill or prefire): towards the next case.
void countCaseKill(Game& g) {
    if (g.mapId != 1 || !(g.mode == 1 || g.mode == 2 || g.mode == 3 || g.mode == 5)) return;
    if (g_inv.addKill(g_caseKills)) {
        pushHitLog(g, "+1 CASE   ESC, INVENTORY TO OPEN IT", 0xe4ae39);
        sound(g, Sfx::UiClick, 0.8f, 0.0f, 0.7f);
    }
    saveInventory();
}

// The inventory screen: a row per weapon, cycling through the skins you can put on it.
const int kInvWeapons[] = {kWRifle, kWM4A1S, kWGalil, kWSniper, kWSsg08, kWNova, kWXm1014, kWMac10, kWUmp45, kWPistol,
                           kWBerettas, kWDeagle, kWKnife};
const char* const kInvRowNames[] = {"AK-47", "M4A1-S", "GALIL AR", "AWP", "SSG 08", "NOVA", "XM1014", "MAC-10", "UMP-45",
                                    "PISTOL", "DUAL BERETTAS", "DEAGLE", "KNIFE"};
constexpr int kInvRows = 13;
struct SkinChoice {
    std::vector<Equipped> opts;
    std::vector<std::string> names;
    std::vector<const char*> labels;
    int sel = 0;
};
SkinChoice g_choices[kInvRows];

// "WILDFIRE (FT)", or for knives "KARAMBIT | FADE (FN)".
std::string choiceName(const Equipped& e) {
    if (e.skin < 0) return "DEFAULT";
    const SkinDef& s = allSkins()[size_t(e.skin)];
    std::string n = s.name;
    if (s.weapon != kWKnife) n = n.substr(n.find("| ") + 2);
    return n + " (" + wearShort(e.wear) + ")";
}

// Rebuilds every row's choices from what you own (or every skin, unlocked), selecting what's equipped.
void refreshChoices() {
    const std::vector<SkinDef>& skins = allSkins();
    for (int r = 0; r < kInvRows; ++r) {
        SkinChoice& ch = g_choices[r];
        const int w = kInvWeapons[r];
        ch.opts.assign(1, Equipped{});
        if (g_allSkins) {
            for (size_t k = 0; k < skins.size(); ++k)
                if (skins[k].weapon == w) ch.opts.push_back({int(k), 0.02f});
        } else {
            for (const Item& it : g_inv.items)
                if (it.skin >= 0 && skins[size_t(it.skin)].weapon == w) ch.opts.push_back({it.skin, it.wear});
        }
        const Equipped& on = g_inv.equip[w];
        ch.sel = 0;
        for (size_t k = 0; k < ch.opts.size(); ++k)
            if (ch.opts[k].skin == on.skin && (g_allSkins || ch.opts[k].wear == on.wear)) ch.sel = int(k);
        if (g_allSkins && on.skin >= 0 && ch.sel > 0) ch.opts[size_t(ch.sel)].wear = on.wear;
        ch.names.clear();
        for (const Equipped& e : ch.opts) ch.names.push_back(choiceName(e));
        ch.labels.clear();
        for (const std::string& n : ch.names) ch.labels.push_back(n.c_str());
    }
}

// A row changed: equip its choice, show it, save.
void choicesChanged(Game& g) {
    for (int r = 0; r < kInvRows; ++r) {
        const SkinChoice& ch = g_choices[r];
        if (ch.sel >= 0 && ch.sel < int(ch.opts.size())) g_inv.equip[kInvWeapons[r]] = ch.opts[size_t(ch.sel)];
    }
    applySkins(g);
    saveInventory();
}

// Opening a case: a reel of items spins under a marker and slows to a stop on what you got (rolled up front,
// and saved straight away).
struct CaseOpening {
    bool active = false, revealed = false;
    double start = 0;
    std::vector<Item> reel;
    int win = 38, item = -1, lastCard = -1;
    float landing = 0.5f;  // where in the winning card the marker stops
};
CaseOpening g_case;
uint32_t g_caseRng = 0x2545F491u;
constexpr double kCaseSpin = 6.0;

bool startCase() {
    const int idx = g_inv.open(g_caseRng);
    if (idx < 0) return false;
    saveInventory();
    g_case = CaseOpening{};
    g_case.active = true;
    g_case.item = idx;
    g_case.start = g_uiTime;
    for (int k = 0; k < 45; ++k) g_case.reel.push_back(rollCase(g_caseRng));
    g_case.reel[size_t(g_case.win)] = g_inv.items[size_t(idx)];
    g_case.landing = 0.15f + 0.7f * rollUnit(g_caseRng);
    return true;
}

// The reel's position in cards (the marker is over card int(pos)): fast, then a long slow-down.
float caseReelPos(double now) {
    const double t = std::clamp((now - g_case.start) / kCaseSpin, 0.0, 1.0);
    return float(1.0 - std::pow(1.0 - t, 4.0)) * (float(g_case.win) + g_case.landing);
}

// Every frame on the case screen: a tick per card going past, the reveal at the end.
void caseTick(Game& g) {
    if (!g_case.active || g_case.revealed) return;
    const int card = int(caseReelPos(g_uiTime));
    if (card != g_case.lastCard) {
        g_case.lastCard = card;
        sound(g, Sfx::UiClick, 0.3f, 0.0f, 1.5f);
    }
    if (g_uiTime - g_case.start >= kCaseSpin) {
        g_case.revealed = true;
        const int rarity = allSkins()[size_t(g_inv.items[size_t(g_case.item)].skin)].rarity;
        sound(g, Sfx::UiClick, 0.9f, 0.0f, 0.6f);
        sound(g, Sfx::HitHead, rarity >= kClassified ? 0.9f : 0.5f, 0.0f, rarity >= kCovert ? 0.7f : 1.0f);
    }
    g.hudDirty = true;
}

void simTick(Game& g, const Options& opt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    MoveInput in;
    in.forward = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
    in.side = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
    in.walk = keys[SDL_SCANCODE_LSHIFT];
    in.duck = keys[SDL_SCANCODE_LCTRL];
    if (g.inputBlocked) in = MoveInput{};  // online with the menu open: you stand still, the game goes on
    if (g.jumpNeedsRelease && !keys[SDL_SCANCODE_SPACE]) g.jumpNeedsRelease = false;
    in.jumpPressed = !g.inputBlocked && !g.jumpNeedsRelease && (g.jumpLatch || (g.autoHop && keys[SDL_SCANCODE_SPACE]));
    g.jumpLatch = false;
    g.defuseHeld = keys[SDL_SCANCODE_E];
    if (g.defuseStart >= 0) in = MoveInput{};  // like CS: you can't move while defusing
    const bool compLive = g.mode == 3 && g.mapId == 1;
    if (compLive && !g.comp.youDead && (g.comp.phase == 0 || g.comp.planter == -1)) in = MoveInput{};  // freeze time, planting

    // Competitive: you only have what you bought (primary, grenades).
    if (compLive && g.switchTo == 1 && g.comp.ownPrimary < 0) g.switchTo = 0;
    if (compLive && g.switchTo == 4) {
        int owned = 0;
        for (int n : g.comp.nades) owned += n > 0;
        if (owned == 0) {
            g.switchTo = 0;
        } else if (g.weapon == &g.guns[kWGrenade] || g.comp.nades[g.nadeType] <= 0) {  // next type you actually have
            int t = g.nadeType;
            for (int k = 0; k < Game::kNadeTypes; ++k) {
                t = (t + 1) % Game::kNadeTypes;
                if (g.comp.nades[t] > 0) break;
            }
            if (g.weapon != &g.guns[kWGrenade]) g.nadeType = t;
            else g.nadeType = (t + Game::kNadeTypes - 1) % Game::kNadeTypes;  // the cycle below adds one
        }
    }

    // Weapon switching / reload.
    if (g.switchTo) {
        WeaponState* target = g.switchTo == 1   ? g.primary
                              : g.switchTo == 2 ? g.secondary
                              : g.switchTo == 4 ? &g.guns[kWGrenade]
                              : g.switchTo == 5 ? g.lastWeapon  // Q
                                                : &g.guns[kWKnife];
        g.grenadeReturnAt = -1;
        g.zoom = 0;
        g.resumeZoomAt = -1;
        if (g.switchTo == 4 && g.weapon == &g.guns[kWGrenade]) {  // 4 again: next grenade type, like CS
            g.nadeType = (g.nadeType + 1) % Game::kNadeTypes;
            g.guns[kWGrenade].nextFireTime = std::max(g.guns[kWGrenade].nextFireTime, g.simTime + 0.25);
            g.vm.setGrenade(g.nadeType);
            g.vm.onDraw(ViewWeapon::Grenade);
            sound(g, Sfx::Draw, 0.6f, 0.0f, 1.2f);
            g.hudDirty = true;
        } else if (target != g.weapon) {
            g.weapon->reloadEndTime = -1;
            g.lastWeapon = g.weapon;
            g.weapon = target;
            g.weapon->nextFireTime = std::max(g.weapon->nextFireTime, g.simTime + 0.25);  // draw time
            g.vm.setGrenade(g.nadeType);
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

    // Scope (right click) and the snipers' bolt cycle.
    const bool sniperOut = wd.scope;
    if (g.zoomLatch && sniperOut && ws.reloadEndTime < 0) {
        g.zoom = (g.zoom + 1) % 3;
        g.resumeZoomAt = -1;
        sound(g, Sfx::Zoom, g.zoom ? 0.55f : 0.4f, 0.0f, g.zoom == 2 ? 1.12f : g.zoom == 1 ? 1.0f : 0.9f);
        g.hudDirty = true;
    }
    const bool lobLatch = g.zoomLatch && &ws == &g.guns[kWGrenade];
    g.zoomLatch = false;
    if (g.resumeZoomAt >= 0 && g.simTime >= g.resumeZoomAt) {
        if (sniperOut && ws.reloadEndTime < 0 && g.deadUntil < 0) {
            g.zoom = g.resumeZoom;
            if (g.zoom) sound(g, Sfx::Zoom, 0.45f);
        }
        g.resumeZoomAt = -1;
        g.hudDirty = true;
    }
    if (g.boltAt >= 0 && g.simTime >= g.boltAt) {
        sound(g, Sfx::Bolt, 0.8f, 0.0f, 0.9f);
        g.boltAt = -1;
    }
    if (ws.reloadEndTime >= 0 && !wd.shellReload) {
        // Reload sounds keyed to the animation: mag out, mag in, bolt.
        double progress = g.simTime - (ws.reloadEndTime - wd.reloadTime);
        const double k = wd.reloadTime / 2.4;  // (timed for a 2.4 s reload, stretched to this gun's)
        const double cues[3] = {0.3 * k, 1.4 * k, 2.0 * k};
        const Sfx sfx[3] = {Sfx::MagOut, Sfx::MagIn, Sfx::Bolt};
        while (g.reloadStage < 3 && progress >= cues[g.reloadStage]) sound(g, sfx[g.reloadStage++], 0.8f);
    }
    if (ws.reloadEndTime >= 0 && g.simTime >= ws.reloadEndTime) {
        if (wd.shellReload) {  // one more shell, and on to the next until it's full
            ws.ammo = std::min(ws.ammo + 1, wd.magSize);
            sound(g, Sfx::MagIn, 0.7f, 0.0f, 1.35f);
            ws.reloadEndTime = ws.ammo < wd.magSize ? g.simTime + wd.reloadTime : -1;
        } else {
            ws.ammo = wd.magSize;
            ws.reloadEndTime = -1;
        }
        g.hudDirty = true;
    }

    // Grenade (slot 4), like CS: pressing Mouse 1 / Mouse 2 pulls the pin; it leaves your hand when you let go
    // of everything you pressed: Mouse 1 a full throw, Mouse 2 an underhand lob, both a medium throw. Then
    // it's back to the previous weapon. (A click shorter than a tick still counts: the latches.)
    if (&ws == &g.guns[kWGrenade] && g.simTime >= ws.nextFireTime && g.grenadeReturnAt < 0 && g.deadUntil < 0) {
        const int held = (g.fireHeld ? 1 : 0) | (g.zoomHeld ? 2 : 0);
        g.nadeHold |= held | (g.fireLatch ? 1 : 0) | (lobLatch ? 2 : 0);
        if (g.nadeHold && !held) {
            g.throwLatch = true;
            g.throwStrength = g.nadeHold == 1 ? 1.0f : g.nadeHold == 2 ? kNadeLob : kNadeMedium;
            g.grenadeReturnAt = g.simTime + 0.4;
            g.nadeHold = 0;
        }
        g.fireLatch = false;
    } else if (&ws != &g.guns[kWGrenade]) {
        g.nadeHold = 0;
    }
    if (g.grenadeReturnAt >= 0 && g.simTime >= g.grenadeReturnAt) {
        g.grenadeReturnAt = -1;
        if (g.weapon == &g.guns[kWGrenade]) g.switchTo = 5;
    }

    bool autofire = opt.autofireStart >= 0 && g.simTime >= opt.autofireStart && g.simTime < opt.autofireEnd;
    // Semi-auto weapons fire once per click; automatic ones keep firing while held.
    bool wantFire = ((wd.automatic && g.fireHeld) || g.fireLatch || autofire) && g.deadUntil < 0;  // not while dead
    if (g.fireLatch && wd.canFire && (ws.ammo == 0 || ws.reloadEndTime >= 0)) sound(g, Sfx::DryFire, 0.6f);
    g.fireLatch = false;
    g.recoilIndexPrev = ws.recoilIndex;

    bool fired = false;
    // The Nova loads shell by shell: firing with shells in it stops the reload.
    if (wantFire && wd.shellReload && ws.reloadEndTime >= 0 && ws.ammo > 0) ws.reloadEndTime = -1;
    ws.scoped = g.zoom > 0;  // (a noscope's spread, with the moving-spread option)
    if (wantFire && wd.canFire && ws.reloadEndTime < 0 && ws.ammo > 0 && takeShotTiming(ws, g.simTime)) {
        float hspeed = length2d(g.player.velocity);
        ShotResult pellets[kMaxPellets * 3];  // the bullets (pellets), then any collateral hits
        int shotCount = 1;
        if (wd.pellets > 1) {
            ShotResult shot[kMaxPellets];
            shotCount = firePellets(ws, g.lastRenderEye, float(g.viewPitch), float(g.viewYaw), hspeed, g.player.onGround,
                                    g.player.ducked, g.world, g.dummies, g.lastDummyRenderPos, shot);
            std::copy(shot, shot + shotCount, pellets);
        } else {
            pellets[0] = fireBullet(ws, g.lastRenderEye, float(g.viewPitch), float(g.viewYaw), hspeed, g.player.onGround,
                                    g.player.ducked, g.world, g.dummies, g.lastDummyRenderPos);
        }
        const int bullets = shotCount;
        for (int k = 0; k < bullets; ++k) shotCount = collatResults(pellets[k], pellets, shotCount, kMaxPellets * 3);
        fired = true;
        ws.ammo--;
        g.shots++;
        const uint8_t code = uint8_t(wd.id);
        if (g.online && g.net.ready()) g.net.sendFire(g.lastRenderEye, pellets[0].end, code);
        // The suppressed guns are heard less far, and barely light the room.
        const bool suppressed = wd.id == kWPistol || wd.id == kWM4A1S;
        makeNoise(g, g.player.origin, wd.id == kWPistol ? 900.0f : wd.id == kWM4A1S ? 1100.0f : 2200.0f);
        muzzleLight(g, g.vm.muzzleWorld(g.lastRenderEye, float(g.viewPitch), float(g.viewYaw)),
                    suppressed ? 0.25f : wd.pellets > 1 || sniperOut || wd.id == kWDeagle ? 1.3f : 1.0f);

        // Cosmetics, once per shot: the sound, the weapon kick, brass.
        const bool isPistol = !wd.primary;
        if (sniperOut && g.zoom > 0) {
            // Like CS: the shot takes the scope down, you see the bolt cycle, and the scope comes back up by
            // itself just before the next shot is ready (unless you switch away).
            g.resumeZoom = g.zoom;
            g.resumeZoomAt = g.simTime + wd.fireInterval - 0.12;
            g.zoom = 0;
        }
        // Your own gun: loud (the mixer soft-limits). The Berettas fire left, right.
        const GunSound gs = gunSound(wd.id);
        sound(g, gs.sfx, gs.gain, wd.id == kWBerettas ? ((g.shots & 1) ? -0.15f : 0.15f) : 0.0f, gs.pitch);
        if (wd.id == kWSniper) g.boltAt = g.simTime + 0.55;
        else if (wd.id == kWSsg08) g.boltAt = g.simTime + 0.47;
        else if (wd.id == kWNova) g.boltAt = g.simTime + 0.3;  // the pump
        g.vm.onShot(ws.shotCounter * 2654435761u);
        // Spray feedback (cosmetic): a camera roll that builds through the spray, and brass flying out.
        float wob = float((ws.shotCounter * 2246822519u) >> 16 & 0xFFFF) / 65535.0f - 0.5f;
        if (g.viewShake && !sniperOut) {
            g.camRoll = std::clamp(g.camRoll + wob * (0.5f + 0.06f * float(std::min(pellets[0].sprayIndex, 12))), -1.5f, 1.5f);
            g.fovPunch = wd.pellets > 1 || wd.id == kWDeagle ? 1.4f : isPistol ? 0.7f : 1.0f;
        } else if (g.viewShake) {
            g.fovPunch = wd.id == kWSniper ? 2.0f : 1.5f;  // a sniper's shot lands like a hammer
        }
        if (!sniperOut && wd.id != kWNova) {  // (the XM1014 throws its shells out too)
            Vec3 fw = anglesToForward(float(g.viewPitch), float(g.viewYaw)), rt = yawToRight(float(g.viewYaw));
            g.fx.shell(g.lastRenderEye + fw * 20.0f + rt * 7.0f - Vec3{0, 0, 6},
                       rt * (110.0f + 50.0f * wob) + Vec3{0, 0, 120.0f} - fw * 25.0f + g.player.velocity);
        }

        // Each bullet (the Nova: each pellet): tracer, impacts, hits.
        int hitCount = 0, hitHeads = 0;
        float hitDamage = 0;
        bool anyKill = false;
        for (int k = 0; k < shotCount; ++k) {
            const ShotResult& r = pellets[k];
            if (k == 0 || k % 3 == 0 || r.isCollat) {  // (a collat: the tracer carries on to the next one)
                g.fx.tracer(g.vm.muzzleWorld(g.lastRenderEye, float(g.viewPitch), float(g.viewYaw)), r.end);
                replayShot(g, g.lastRenderEye + anglesToForward(float(g.viewPitch), float(g.viewYaw)) * 20.0f, r.end, -1, wd.id);
            }
            if (g.online && g.net.ready() && k > 0 && k % 3 == 0) g.net.sendFire(g.lastRenderEye, r.end, code);
            // A player (their game applies it), or a bot when you joined (the host's): tell them.
            if (g.online && g.net.ready() && r.dummyIndex >= 0 && !(netHost(g) && isBot(g, size_t(r.dummyIndex))))
                g.net.sendHit(uint8_t(r.dummyIndex), r.damage, uint8_t(r.group), code);
            Vec3 shotDir = normalize(r.end - r.start);
            if (r.dummyIndex >= 0 && r.group == kHead && !r.kill && g.dummies[size_t(r.dummyIndex)].helmet) {
                if (k == 0 || shotCount == 1) sound(g, Sfx::HelmetHit, 0.8f);  // a headshot the helmet stopped
                g.fx.blood(r.end, shotDir);
            } else if (r.dummyIndex >= 0) {
                if (hitCount == 0) sound(g, r.group == kHead ? Sfx::HitHead : Sfx::HitBody, r.group == kHead ? 0.9f : 0.75f);
                g.fx.blood(r.end, shotDir);
            } else if (r.hitWorld) {
                if (g.audio && r.worldBox >= 0 && (k == 0 || k % 4 == 0)) {  // the impact, by what it hit
                    const uint8_t m = g.world.solids[size_t(r.worldBox)].material;
                    g.audio->play3D(m == kMatWood ? Sfx::ImpactWood : m == kMatMetal ? Sfx::ImpactMetal : Sfx::ImpactStone, r.end,
                                    g.lastRenderEye, float(g.viewYaw), 1800.0f, 0.55f);
                }
                // Far impacts are drawn bigger so you can see where a spray lands at range.
                g.fx.impact(r.end, r.normal, 0x5c6168, std::clamp(r.distance / 450.0f, 1.0f, 4.0f));
            }
            for (int p = 0; p < r.penCount; ++p) {  // wallbang: debris on both sides of each wall
                g.fx.impact(r.penEntry[p], r.penNormal[p], 0x5c6168);
                g.fx.impact(r.penExit[p], -r.penNormal[p], 0x5c6168);
            }

            if (r.dummyIndex >= 0) {
                ++hitCount;
                hitHeads += r.group == kHead;
                hitDamage += r.damage;
                anyKill = anyKill || r.kill;
                if (wd.pellets <= 1) {
                    char buf[112];
                    std::snprintf(buf, sizeof(buf), "%s %d%s%s%s  %.0fM", hitGroupName(r.group), int(r.damage + 0.5f),
                                  r.kill ? "  KILL" : "", r.penCount ? "  WALLBANG" : "", r.isCollat ? "  COLLAT" : "",
                                  r.distance * 0.0254f);
                    pushHitLog(g, buf, r.group == kHead ? 0xff6060 : 0xffffff);
                }
                if (r.kill && r.group == kHead) knockHelmet(g, size_t(r.dummyIndex), shotDir);
                if (r.kill) botDied(g, size_t(r.dummyIndex));
                if (!r.kill && g.mode >= 2) {  // a retake / competitive bot you hit turns on you
                    BotBrain& b = g.bots[size_t(r.dummyIndex)];
                    b.alertUntil = g.simTime + 2.0;
                    b.lastSeen = g.player.origin;
                }
                if (r.kill && g.mode == 1 && g.mapId == 1 && ws.ammo < wd.magSize) {  // deathmatch: a kill gives 10 rounds
                    ws.ammo = std::min(ws.ammo + 10, wd.magSize);
                    pushHitLog(g, "+10 ROUNDS", 0x60c0ff);
                }
                if (r.kill && g.botsFire && g.deadUntil < 0 && g.hp < 100.0f && g.mode != 3 && g.mode != 4) {  // a kill heals you
                    g.hp = std::min(100.0f, g.hp + 40.0f);
                    pushHitLog(g, "+40 HP", 0x60ff60);
                }
                {
                    const Dummy& hitDummy = g.dummies[size_t(r.dummyIndex)];
                    float hpBefore = hitDummy.hp + r.damage;  // fireBullet already took it off
                    recordDamage(g, -1, r.dummyIndex, std::min(r.damage, std::max(0.0f, hpBefore)), r.group == kHead,
                                 wd.name, r.penCount > 0, r.kill);
                }
                if (g.mode == 1) {
                    BotBrain& b = g.bots[size_t(r.dummyIndex)];
                    if (!r.kill) {  // hit but alive: they turn on you
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
            } else if (r.hitWorld) {
                // Decal color follows the spray index (yellow first shot -> red late spray).
                float t = float(r.sprayIndex) / float(std::max(1, wd.patternLen - 1));
                uint32_t col = lerpColor(0xffe650, 0xe02828, t);
                float hs = 1.4f * std::clamp(r.distance / 700.0f, 1.0f, 3.0f);  // readable far away too
                Vec3 c = r.end + r.normal * 0.6f, h{hs, hs, hs};
                g.pendingDecals.push_back(makeBox(c - h, c + h, col, false));
            }
            for (int p = 0; p < r.penCount; ++p) {
                Vec3 h{1.4f, 1.4f, 1.4f}, c = r.penEntry[p] + r.penNormal[p] * 0.6f;
                g.pendingDecals.push_back(makeBox(c - h, c + h, 0xffe650, false));
            }
        }
        if (wd.pellets > 1 && hitCount > 0) {  // the shotgun: one line for the whole shot
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%d/%d PELLETS %d%s%s", hitCount, shotCount, int(hitDamage + 0.5f),
                          hitHeads ? "  HEAD" : "", anyKill ? "  KILL" : "");
            pushHitLog(g, buf, hitHeads ? 0xff6060 : 0xffffff);
        }
        if (hitCount > 0) {
            g.hits++;
            if (hitHeads) g.headshots++;
            // Hit feedback: a tick you can hear over the gunfire and an X that pops on the crosshair
            // (red for the head, bigger and longer on a kill).
            g.hitMarkerStart = g.simTime;
            g.hitMarkerUntil = g.simTime + (anyKill ? 0.22 : 0.14);
            g.hitMarkerHead = hitHeads > 0;
            g.hitMarkerKill = anyKill;
            if (g.hitSound) sound(g, Sfx::HitMarker, anyKill ? 0.5f : 0.35f, 0.0f, anyKill ? 0.85f : 1.0f);
        }
        if (ws.ammo == 0) {
            ws.reloadEndTime = g.simTime + wd.reloadTime + (wd.shellReload ? 0.3 : 0.0);
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
        const char* c = townCallout(g.player.origin);
        if (c != g.callout) { g.callout = c; g.hudDirty = true; }
    }

    for (size_t i = 0; i < g.dummies.size(); ++i) {
        bool wasAlive = g.dummies[i].alive();
        updateDummy(g.dummies[i], kTickDt);
        if (!wasAlive && g.dummies[i].alive()) g.aliveSince[i] = g.simTime;
    }
    if (g.online && g.mapId == 1 && g.dummies.size() >= size_t(kNetMaxPlayers)) {
        // Online: each other player (and, joined to a competitive game, each of the host's bots) shown 6 ticks
        // (~47 ms) behind their newest state, interpolated; the playback drifts gently to stay that far
        // behind, and snaps if it falls far out. Deathmatch: their state says if they're alive; competitive:
        // the round starts and the deaths do (they're reliable, and a dead player stays dead).
        const bool comp = g.mode == 3;
        const int slots = std::min(int(g.dummies.size()), comp ? kNetSlots : kNetMaxPlayers);
        for (int id = 0; id < slots; ++id) {
            Game::Remote& rm = g.remotes[id];
            Dummy& d = g.dummies[size_t(id)];
            if (netHost(g) && isBot(g, size_t(id))) continue;  // your own bots
            if (!rm.present || id == g.net.myId()) {
                if (!comp || id == g.net.myId()) {  // (competitive: round starts, deaths and leaving decide)
                    d.respawnLeft = 1e9f;
                    d.deadFor = 10.0f;
                }
                continue;
            }
            const double target = double(rm.latest) - 6.0;
            if (rm.play < 0 || std::fabs(rm.play - target) > 32.0) rm.play = target;
            else rm.play = std::min(rm.play + 1.0 + (target - rm.play) * 0.05, double(rm.latest));
            auto snap = [&](int64_t t) -> const NetState* {
                if (t < 0) return nullptr;
                const NetState& s = rm.snaps[size_t(t) % 32];
                return s.tick == uint32_t(t) ? &s : nullptr;
            };
            const int64_t t0 = int64_t(std::floor(rm.play));
            const NetState *a = nullptr, *b = nullptr;
            for (int64_t t = t0; t > t0 - 16 && !a; --t) a = snap(t);
            for (int64_t t = t0 + 1; t <= int64_t(rm.latest) && t < t0 + 16 && !b; ++t) b = snap(t);
            if (!a) a = b;
            if (!a) continue;
            Vec3 pos = a->pos;
            float yaw = a->yaw, crouch = float(a->duck) / 255.0f, pitch = a->pitch;
            if (b && b != a && b->tick > a->tick) {
                const float k = std::clamp(float((rm.play - double(a->tick)) / double(b->tick - a->tick)), 0.0f, 1.0f);
                pos = lerp(a->pos, b->pos, k);
                yaw = wrapDeg(a->yaw + wrapDeg(b->yaw - a->yaw) * k);
                crouch += (float(b->duck) / 255.0f - crouch) * k;
                pitch += (b->pitch - pitch) * k;
            }
            const bool aliveNow = (a->flags & kNetAlive) != 0;
            if (!comp && aliveNow && !d.alive()) {  // (re)spawned: no smear from where they died
                d.respawnLeft = 0;
                d.deadFor = 0;
                d.prevPos = pos;
                d.prevYaw = yaw;
            } else if (!comp && !aliveNow && d.alive()) {
                d.respawnLeft = 1e9f;
                d.deadFor = 0;
            }
            if (length2d(pos - d.pos) > 200.0f) { d.prevPos = pos; d.prevYaw = yaw; d.prevCrouch = crouch; }  // a teleport
            d.pos = pos;
            d.yaw = yaw;
            d.crouch = crouch;
            d.pitch = pitch;
            d.weapon = a->weapon;
            d.hp = 1e6f;
            if (id >= kNetMaxPlayers) {  // a bot's kit (its gun is weapon)
                d.armor = (a->flags & kNetArmor) ? 100.0f : 0.0f;
                d.helmet = (a->flags & kNetHelmet) != 0;
            }
            g.bots[size_t(id)].state = 1;  // (spawn picking keeps away from players it counts as placed)
        }
        if (g.net.ready()) {
            NetState me;
            me.tick = ++g.netTick;
            me.pos = g.player.origin;
            me.yaw = float(g.viewYaw);
            me.pitch = float(g.viewPitch);
            me.flags = uint8_t(g.deadUntil < 0 ? kNetAlive : 0);
            me.weapon = uint8_t(g.weapon->def->id);
            me.duck = uint8_t(std::lround(std::clamp(g.player.duckAmount, 0.0f, 1.0f) * 255.0f));
            g.net.sendState(me);
        }
        if (netHost(g) && comp && (g.netTick & 1) == 0) {  // your bots, 64 times a second
            NetBot list[kNetBots];
            int n = 0;
            for (int k = 0; k < kNetBots && size_t(kNetMaxPlayers + k) < g.dummies.size(); ++k) {
                const size_t i = size_t(kNetMaxPlayers + k);
                const Dummy& d = g.dummies[i];
                if (g.netTeam[i] < 0) continue;
                NetBot& b = list[n++];
                b.id = uint8_t(i);
                b.pos = d.pos;
                b.yaw = d.yaw;
                b.flags = uint8_t((d.alive() ? kNetAlive : 0) | (d.armor > 0 ? kNetArmor : 0) | (d.helmet ? kNetHelmet : 0));
                b.pitch = d.pitch;
                b.weapon = d.weapon;
            }
            g.net.sendBots(g.netTick, list, n);
        }
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
                sound(g, stepSoundAt(g, p.origin), 0.5f, g.stepLeft ? -0.15f : 0.15f, pitch);
                makeNoise(g, p.origin, 1100.0f);
            }
        } else {
            g.stepDist = std::min(g.stepDist, kStepStride * 0.6f);  // first step comes quickly
        }
        // Strafing dummies make positional footsteps: practise hearing direction.
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            Dummy& d = g.dummies[i];
            if (!d.alive()) continue;
            float ds = length(d.pos - d.prevPos) / kTickDt;
            if (ds <= kStepSpeed || ds > 2000.0f) continue;  // (not a teleport)
            d.stepDist += ds * kTickDt;
            if (d.stepDist >= kStepStride) {
                d.stepDist -= kStepStride;
                if (g.audio)
                    g.audio->play3D(stepSoundAt(g, d.pos), d.pos, g.lastRenderEye, float(g.viewYaw), 1600.0f, 0.9f);
                if (netHost(g) && !isBot(g, i) && i < g.team.size()) makeNoise(g, d.pos, 1100.0f, g.team[i]);  // your bots hear players
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

    // ---- Grenades: smoke, flash, HE, molotov ----
    if (g.throwLatch && g.mode == 3 && g.mapId == 1) {  // competitive: only what you bought
        if (g.comp.youDead || g.comp.nades[g.nadeType] <= 0) g.throwLatch = false;
        else g.comp.nades[g.nadeType]--;
    }
    if (g.throwLatch) {
        // CS:GO's throw (combat.cpp): lifted aim, 675 u/s (x0.3 for the lob), plus 1.25x your velocity.
        const Vec3 v = grenadeThrowVelocity(float(g.viewPitch), float(g.viewYaw), g.throwStrength, g.player.velocity);
        const Vec3 f = normalize(v - g.player.velocity * 1.25f);
        g.nades.push_back({g.lastRenderEye + f * 16.0f, v, g.nadeType});
        if (g.online) g.net.sendNade(g.nadeType, g.lastRenderEye + f * 16.0f, v);  // everyone flies the same throw
        g.throwStrength = 1.0f;  // G quick-throws a full one
        sound(g, Sfx::Draw, 0.6f, 0.0f, 1.3f);
        g.throwLatch = false;
    }
    for (size_t k = 0; k < g.nades.size();) {
        Game::Nade& n = g.nades[k];
        const NadeStep st = stepGrenade(g.world, n.pos, n.vel);  // same flight as predictGrenade()
        const bool landed = st.landed;
        if (st.impactSpeed > 80.0f && g.audio)
            g.audio->play3D(Sfx::Footstep, n.pos, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.4f, 1.8f);
        ++n.ticks;
        // Smokes pop once they've stopped rolling (like CS); flash and HE on their fuse; molotovs on landing.
        const int fuseTicks = int(grenadeFuse(n.type) * kTickRate + 0.5);
        const bool smokeReady = n.type != Game::kSmokeNade || length(n.vel) < 1.0f || n.ticks > fuseTicks + 4 * kTickRate;
        if ((n.ticks >= fuseTicks && smokeReady) || (n.type == Game::kMolotov && landed)) {
            switch (n.type) {
                case Game::kSmokeNade:
                    g.smokes.push_back({n.pos, g.simTime});
                    if (g.audio) g.audio->play3D(Sfx::Land, n.pos, g.lastRenderEye, float(g.viewYaw), 2500.0f, 1.0f, 0.55f);
                    break;
                case Game::kFlashNade: flashBang(g, n.pos); break;
                case Game::kHeNade: heExplode(g, n.pos, n.owner); break;
                default: igniteMolotov(g, n.pos, n.owner); break;
            }
            g.nades.erase(g.nades.begin() + long(k));
        } else {
            ++k;
        }
    }
    g.smokes.erase(std::remove_if(g.smokes.begin(), g.smokes.end(),
                                  [&](const Game::Smoke& s) { return g.simTime - s.start > kSmokeLife; }),
                   g.smokes.end());
    // Fire: burns for kFireLife, 10 damage every 0.25 s to anyone standing in it; a smoke puts it out.
    for (size_t k = 0; k < g.fires.size();) {
        Game::Fire& f = g.fires[k];
        if (g.simTime - f.start > kFireLife || insideSmoke(g, f.pos, kFireRadius * 0.6f)) {
            g.fires.erase(g.fires.begin() + long(k));
            continue;
        }
        if (g.simTime >= f.nextTick) {
            f.nextTick = g.simTime + 0.25;
            auto inFire = [&](const Vec3& feet) {
                return length2d(feet - f.pos) < kFireRadius && feet.z - f.pos.z > -24.0f && feet.z - f.pos.z < 48.0f;
            };
            for (size_t i = 0; i < g.dummies.size(); ++i)
                if (g.dummies[i].alive() && isBot(g, i) && !netClient(g) && inFire(g.dummies[i].pos))
                    hurtBot(g, i, f.owner, 10.0f, "MOLOTOV");
            if (inFire(g.player.origin)) hurtPlayer(g, f.owner < 0 ? -2 : f.owner, 10.0f, false, "MOLOTOV");
            if (g.audio && rnd(g) < 0.6f)
                g.audio->play3D(Sfx::Fire, f.pos, g.lastRenderEye, float(g.viewYaw), 1800.0f, 0.7f);
        }
        ++k;
    }

    // ---- Dust bots: hide, peek, hold an angle, return; respawn at a free spot ----
    if (g.mapId == 1 && g.mode == 0) {
        const auto& spots = townPeekSpots();
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
            recordDeathmatch(g);
            g.hudDirty = true;
        } else if (g.dmOverUntil >= 0 && g.simTime >= g.dmOverUntil) {
            startDeathmatch(g);
        }
        int secs = int(std::max(0.0, g.dmEnd - g.simTime));
        if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
    }
    // ---- Retakes: the bomb's planted. Defuse it (hold E for 5 s) to win; lose if it blows or you die ----
    if (g.mapId == 1 && g.mode == 2) {
        if (g.rtResultUntil < 0 && g.bombActive) {
            const bool near = length2d(g.player.origin - g.bombPos) < 72.0f &&
                              std::fabs(g.player.origin.z - g.bombPos.z) < 64.0f && g.player.onGround && g.deadUntil < 0;
            if (near && g.defuseHeld) {
                if (g.defuseStart < 0) {
                    g.defuseStart = g.simTime;
                    if (g.audio) g.audio->play3D(Sfx::Defuse, g.bombPos, g.lastRenderEye, float(g.viewYaw), 1500.0f, 0.9f);
                    makeNoise(g, g.bombPos, 1800.0f);  // they hear the kit
                }
                if (g.simTime - g.defuseStart >= 5.0) {
                    g.bombActive = false;
                    endRetakeRound(g, true, "BOMB DEFUSED");
                }
            } else {
                g.defuseStart = -1;
            }
            if (g.bombActive && g.simTime >= g.nextBeep) {  // beeps speed up as the fuse runs down
                double left = g.bombExplodeAt - g.simTime;
                g.nextBeep = g.simTime + std::clamp(left / kRetakeRoundTime, 0.1, 1.0);
                if (g.audio) g.audio->play3D(Sfx::BombBeep, g.bombPos, g.lastRenderEye, float(g.viewYaw), 2600.0f, 0.8f);
            }
            if (g.bombActive && g.simTime >= g.bombExplodeAt) {
                g.bombActive = false;
                g.defuseStart = -1;
                if (g.audio) g.audio->play3D(Sfx::Explosion, g.bombPos, g.lastRenderEye, float(g.viewYaw), 9000.0f, 1.0f, 0.7f);
                g.fx.burst(g.bombPos, 0xffa040, 3.0f);
                endRetakeRound(g, false, "BOMB EXPLODED");
            }
        } else if (g.rtResultUntil >= 0 && g.simTime >= g.rtResultUntil) {
            startRetakeRound(g);
        }
        int secs = int(std::max(0.0, g.rtRoundEnd - g.simTime));
        if (secs != g.dmShownSecs) { g.dmShownSecs = secs; g.hudDirty = true; }
    }
    if (g.mapId == 1 && g.mode == 3) {
        if (netClient(g)) {
            compClientTick(g);
        } else {
            compTick(g);
            if (netHost(g) && ((g.netTick & 15) == 0 || g.comp.phase != g.dmShownPhase)) {  // the match, 8 times a second
                const Game::Comp& c = g.comp;
                NetMatch m;
                m.phase = uint8_t(c.phase);
                m.round = uint8_t(c.round);
                m.sideA = uint8_t(c.youTeam);
                m.score[0] = uint8_t(c.youScore);
                m.score[1] = uint8_t(c.themScore);
                m.phaseLeft = float(std::max(0.0, c.phaseEnd - g.simTime));
                m.buyLeft = float(std::max(0.0, c.buyUntil - g.simTime));
                m.bombLeft = float(std::max(0.0, g.bombExplodeAt - g.simTime));
                m.carrier = c.carrier == -2 ? 254 : c.carrier < -1 ? 255 : localToNet(g, c.carrier);
                m.dropped = c.dropped;
                m.planted = c.planted ? 1 : 0;
                m.bombActive = g.bombActive ? 1 : 0;
                m.bombPos = g.bombPos;
                g.net.sendMatch(m);
                g.dmShownPhase = c.phase;
            }
        }
    }
    if (g.mapId == 1 && g.mode == 4) {
        Game::Prefire& pf = g.pf;
        if (pf.resultUntil >= 0) {
            if (g.simTime >= pf.resultUntil) startPrefire(g);
        } else {
            if (pf.start < 0 && (length2d(g.player.velocity) > 5.0f || g.shots > 0)) pf.start = g.simTime;
            bool anyAlive = false;
            for (const Dummy& d : g.dummies) anyAlive |= d.alive();
            if (!anyAlive && pf.start >= 0) endPrefire(g, true);
        }
    }
    if (g.mapId == 1 && g.mode != 0 && g.mode != 5 && g.nav.ready() && !netClient(g)) {
        BotSenses sense;
        sense.world = &g.world;
        sense.nav = &g.nav;
        sense.now = g.simTime;
        sense.playerOrigin = g.player.origin;
        sense.playerEye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)};
        const bool resultScreen = (g.mode == 1 && g.dmOverUntil >= 0) || (g.mode == 2 && g.rtResultUntil >= 0) ||
                                  (g.mode == 4 && g.pf.resultUntil >= 0);
        const bool prefireWaiting = g.mode == 4 && g.pf.start < 0;  // prefire: nobody fights until your clock runs
        sense.playerUp = g.deadUntil < 0 && !g.noclip && !resultScreen && !prefireWaiting;
        sense.noiseFresh = g.simTime - g.noiseAt < 1.5 * kTickDt;
        sense.noisePos = g.noisePos;
        sense.noiseRadius = g.noiseRadius;
        sense.blocked = [](const void* ctx, const Vec3& a, const Vec3& b) {
            return smokeBlocks(*static_cast<const Game*>(ctx), a, b);
        };
        sense.blockCtx = &g;
        static std::vector<Vec3> fireSpots;
        fireSpots.clear();
        for (const Game::Fire& f : g.fires) fireSpots.push_back(f.pos);
        sense.fires = &fireSpots;
        sense.fireRadius = kFireRadius;
        // Competitive: each side fights the other side's bots, and you if you're on the other side. Deathmatch
        // (with dm_bot_fights): everyone fights everyone, and the bots still come looking for you half the time.
        std::vector<BotTarget> enemiesOf[2], everyone;
        if (g.mode == 1 && g.dmBotFights) {
            for (size_t i = 0; i < g.dummies.size(); ++i)
                if (g.dummies[i].alive() && g.bots[i].state >= 0)
                    everyone.push_back({int(i), g.dummies[i].pos, g.dummies[i].pos + Vec3{0, 0, dummyEyeZ(g.dummies[i].crouch)}});
            if (sense.playerUp) everyone.push_back({-1, sense.playerOrigin, sense.playerEye});
            sense.targets = &everyone;
            sense.huntYou = true;
        }
        if (g.mode == 3) {
            sense.playerUp = sense.playerUp && youAlive(g) && g.comp.phase == 1;
            for (int side = 0; side < 2; ++side) {
                for (size_t i = 0; i < g.dummies.size(); ++i)
                    if (g.dummies[i].alive() && g.team[i] != side)
                        enemiesOf[side].push_back(
                            {int(i), g.dummies[i].pos, g.dummies[i].pos + Vec3{0, 0, dummyEyeZ(g.dummies[i].crouch)}});
                if (sense.playerUp && g.comp.youTeam != side) enemiesOf[side].push_back({-1, sense.playerOrigin, sense.playerEye});
            }
        }
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            Dummy& d = g.dummies[i];
            BotBrain& b = g.bots[i];
            if (!d.alive() || !isBot(g, i)) { b.state = -1; b.sees = b.aimed = false; continue; }
            if (g.mode == 1 && needsSpawn(d, b)) {
                spawnDeathmatchBot(d, b, pickDmSpawn(g, false, i), g.rng);
                d.weapon = uint8_t(deathmatchBotGun(rnd(g)));  // a new gun every life, like CS deathmatch
            }
            sense.self = int(i);
            if (g.mode == 3) {
                if (g.comp.phase != 1 || g.comp.planter == int(i) || g.comp.defuser == int(i)) {
                    b.sees = b.aimed = false;  // frozen, planting or defusing: hands busy
                    continue;
                }
                sense.targets = &enemiesOf[g.team[i]];
                sense.noiseFresh = sense.noiseFresh && g.team[i] != g.noiseSide;  // only enemies react to a noise
            }
            updateDeathmatchBot(d, b, sense, g.rng);
            {  // where it looks up or down (seen when you spectate it): at whoever it's fighting, else level
                float want = 0;
                if (b.sees && b.target >= -1) {
                    const Vec3 eye = d.pos + Vec3{0, 0, dummyEyeZ(d.crouch)};
                    const Vec3 at = b.target == -1 ? sense.playerEye
                                    : size_t(b.target) < g.dummies.size()
                                        ? g.dummies[size_t(b.target)].pos + Vec3{0, 0, crouchZ(58.0f * modelScale(), g.dummies[size_t(b.target)].crouch)}
                                        : eye;
                    want = -std::atan2(at.z - eye.z, std::max(1.0f, length2d(at - eye))) / kDegToRad;
                }
                d.pitch += std::clamp(want - d.pitch, -360.0f * kTickDt, 360.0f * kTickDt);
            }
            if (g.mode == 3) sense.noiseFresh = g.simTime - g.noiseAt < 1.5 * kTickDt;
        }
    }

    // ---- Radar spotting: a bot shows while you can see it (in front of you, nothing in between) ----
    if (g.mapId == 1 && g.spottedUntil.size() == g.dummies.size() && (g.histHead & 7) == 0) {  // ~16 Hz is plenty
        const Vec3 eye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)};
        const Vec3 look = anglesToForward(float(g.viewPitch), float(g.viewYaw));
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            const float chestZ = 56.0f * modelScale();
            Vec3 to = d.pos + Vec3{0, 0, chestZ} - eye;
            float dist = length(to);
            if (d.alive() && dist > 1 && dot(to * (1.0f / dist), look) > 0.5f &&
                g.world.traceRay(eye, d.pos + Vec3{0, 0, chestZ}).fraction >= 1.0f && !smokeBlocks(g, eye, d.pos + Vec3{0, 0, chestZ}))
                g.spottedUntil[i] = g.simTime + 0.6;
        }
        // Competitive: whatever your teammates see shows on your radar too.
        if (g.mode == 3)
            for (size_t i = 0; i < g.dummies.size(); ++i) {
                const BotBrain& b = g.bots[i];
                if (g.dummies[i].alive() && g.dummies[i].friendly && b.sees && b.target >= 0)
                    g.spottedUntil[size_t(b.target)] = g.simTime + 0.6;
            }
    }

    recordReplay(g);

    // ---- Bots shoot back ----
    Vec3 simEye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)};
    g.eyeHistory[g.histHead] = simEye;
    g.histHead = (g.histHead + 1) & 63;
    if (g.deadUntil >= 0 && g.simTime >= g.deadUntil) g.deadUntil = -1;
    const bool comp = g.mode == 3 && g.mapId == 1;
    // A bot's bullet from `from` along `dir` (to distance `len`) that missed you but passed close: you hear it go by.
    auto nearMiss = [&](const Vec3& from, const Vec3& dir, float len) {
        if (!g.audio || g.deadUntil >= 0 || g.noclip) return;
        const float t = dot(simEye - from, dir);
        if (t < 60.0f || t > len) return;
        const Vec3 p = from + dir * t;
        if (length(p - simEye) < 56.0f) g.audio->play3D(Sfx::Whiz, p, simEye, float(g.viewYaw), 300.0f, 0.8f);
    };
    const bool dmFights = g.mode == 1 && g.mapId == 1 && g.dmBotFights;  // deathmatch bots shoot each other too
    if (g.botsFire && (comp || dmFights || (g.deadUntil < 0 && !g.noclip)) && !netClient(g)) {
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            if (!isBot(g, i)) continue;
            const Dummy& d = g.dummies[i];
            Vec3 head = d.pos + Vec3{0, 0, dummyEyeZ(d.crouch)};
            // Deathmatch bots need to see you (view cone) and have turned to face you first.
            bool los = g.mode != 0 && g.mapId == 1
                           ? d.alive() && g.bots[i].aimed
                           : d.alive() && length(simEye - head) < 4000.0f && g.simTime >= g.bots[i].blindUntil &&
                                 g.world.traceRay(head, simEye).fraction >= 1.0f && !smokeBlocks(g, head, simEye);
            const int tgt = comp || dmFights ? g.bots[i].target : -1;  // competitive, deathmatch: whoever it's fighting
            if (comp && (tgt < -1 || (tgt == -1 && !youAlive(g)) || g.comp.phase != 1)) los = false;
            if (dmFights && (tgt < -1 || (tgt == -1 && (g.deadUntil >= 0 || g.noclip)))) los = false;
            if (!los) {  // reaction time only starts over once it loses sight (a jiggle doesn't reset it)
                if (!(g.mode != 0 && g.mapId == 1 && g.bots[i].sees && d.alive())) g.botSeen[i] = 0;
                continue;
            }
            // Skill: your competitive teammates use theirs, every other bot the enemy skill (give or take its own
            // bit, with skill variance).
            const BotSkill sk = botSkillOf(g, i);
            if (g.botSeen[i] == 0)  // human-ish reaction time, a bit slower after a quiet spell (see BotBrain::surprise)
                g.botReact[i] = sk.reactMin + rnd(g) * sk.reactRange + (g.mapId == 1 ? 0.2f * g.bots[i].surprise : 0.0f);
            g.botSeen[i] += kTickDt;
            g.botCooldown[i] -= kTickDt;
            if (g.botSeen[i] < g.botReact[i] || g.botCooldown[i] > 0) continue;  // reaction time, fire rate
            // Its gun: damage (falling off with range), armor penetration, fire rate, pellets, sound, all as the gun.
            const int gun = weaponDef(d.weapon).canFire ? int(d.weapon) : int(kWRifle);
            const WeaponDef& gd = weaponDef(gun);
            g.botCooldown[i] = (botShotGap(gun) + rnd(g) * 0.16f) * sk.fireScale;
            // A shotgun's pellets spread at random around the aim (bots may use randomness; you don't).
            auto pelletDir = [&](const Vec3& dir, int k) {
                if (k == 0 || gd.pelletSpread <= 0) return dir;
                const Vec3 side = normalize(cross(dir, Vec3{0, 0, 1})), up = cross(side, dir);
                const float a = rnd(g) * 6.2831853f, r = std::sqrt(rnd(g)) * gd.pelletSpread * kDegToRad;
                return normalize(dir + side * (std::cos(a) * r) + up * (std::sin(a) * r));
            };
            gunshot3D(g, gun, d.pos, simEye, float(g.viewYaw), tgt >= 0 ? 1.0f : 1.1f);
            if (tgt >= 0) {  // competitive: shooting another bot (online: or another player)
                const Dummy& v = g.dummies[size_t(tgt)];
                Vec3 aim = v.pos + Vec3{0, 0, crouchZ((rnd(g) < sk.headChance ? 63.0f : 50.0f) * modelScale(), v.crouch)};  // head or chest
                float err = length(aim - head) * 0.014f * sk.aimError;
                aim += Vec3{(rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * 1.5f * err};
                const Vec3 aimDir = normalize(aim - head);
                float total = 0;  // damage before armor (players apply their own), and after (bots)
                float armored = 0;
                bool headHit = false, any = false;
                HitGroup worst = kChest;
                for (int k = 0; k < gd.pellets; ++k) {
                    const Vec3 dir = pelletDir(aimDir, k);
                    TraceResult wt = g.world.traceRay(head, head + dir * 5000.0f);
                    float maxT = wt.fraction * 5000.0f, t = maxT;
                    HitGroup grp = kChest;
                    const bool hitIt = rayHitsDummy(v.pos, v.yaw, v.crouch, head, dir, maxT, t, grp) && !smokeBlocks(g, head, head + dir * t);
                    const Vec3 end = head + dir * (hitIt ? t : maxT);
                    g.fx.tracer(head + dir * 20.0f, end);
                    if (k == 0) {
                        muzzleLight(g, head + dir * 20.0f, suppressedGun(gun) ? 0.2f : 0.9f);
                        replayShot(g, head + dir * 20.0f, end, int(i), gun);
                        if (g.online) g.net.sendFire(head + dir * 20.0f, end, uint8_t(gun), int(i));
                    }
                    nearMiss(head, dir, hitIt ? t : maxT);
                    if (!hitIt) continue;
                    const float dmg = damageAt(gd, t) * hitGroupDamageScale(grp);
                    total += dmg;
                    armored += armoredDamage(dmg, grp, v.armor, v.helmet, gd.armorRatio);
                    if (grp == kHead) headHit = true;
                    if (!any || grp == kHead) worst = grp;
                    any = true;
                }
                if (!any) continue;
                if (!isBot(g, size_t(tgt))) {  // a player: their game takes it (and their armor)
                    g.net.sendHit(uint8_t(tgt), total, uint8_t(worst), uint8_t(gun), int(i));
                    continue;
                }
                Dummy& vm = g.dummies[size_t(tgt)];
                const float applied = std::min(armored, vm.hp);
                vm.hp -= armored;
                vm.flash[worst] = 0.15f;
                vm.hitDir = aimDir;
                const bool kill = vm.hp <= 0;
                if (kill && headHit) knockHelmet(g, size_t(tgt), aimDir);
                recordDamage(g, int(i), tgt, applied, headHit, gd.name, false, kill);
                if (kill) { vm.respawnLeft = 1.0f; botDied(g, size_t(tgt)); }
                else { g.bots[size_t(tgt)].alertUntil = g.simTime + 2.0; g.bots[size_t(tgt)].lastSeen = d.pos; }
                continue;
            }

            // Where you were a moment ago (0.1-0.25 s by skill): your chest, or now and then your head.
            Vec3 aim = g.eyeHistory[(g.histHead - 1 - sk.lagTicks + 64) & 63] - Vec3{0, 0, rnd(g) < sk.headChance ? 2.0f : 16.0f};
            float err = length(aim - head) * 0.014f * sk.aimError;  // ~0.8 deg of random aim error at normal
            aim += Vec3{(rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * 2 * err, (rnd(g) - 0.5f) * err};
            const Vec3 aimDir = normalize(aim - head);
            const float hh = g.player.ducked ? kDuckHeight : kStandHeight;
            const Vec3 o = g.player.origin;
            // Your armor: competitive, what you bought; everywhere else kevlar and a helmet, like CS deathmatch.
            const float yourArmor = comp ? g.comp.armor : 100.0f;
            const bool yourHelmet = comp ? g.comp.helmet : true;
            float dmg = 0;
            bool headHit = false;
            for (int k = 0; k < gd.pellets; ++k) {
                const Vec3 dir = pelletDir(aimDir, k);
                TraceResult wt = g.world.traceRay(head, head + dir * 5000.0f);
                float maxT = wt.fraction * 5000.0f, bestT = maxT;
                int hit = 0;  // 1 body, 2 head
                float t;
                if (rayHitsBox(head, dir, bestT, o + Vec3{-13, -13, 0}, o + Vec3{13, 13, hh - 10}, t) && t >= 0) {
                    bestT = t; hit = 1;
                }
                if (rayHitsBox(head, dir, bestT, o + Vec3{-5, -5, hh - 10}, o + Vec3{5, 5, hh}, t) && t >= 0) {
                    bestT = t; hit = 2;
                }
                g.fx.tracer(head + dir * 20.0f, head + dir * bestT);
                if (k == 0) {
                    muzzleLight(g, head + dir * 20.0f, suppressedGun(gun) ? 0.2f : 0.9f);
                    replayShot(g, head + dir * 20.0f, head + dir * bestT, int(i), gun);
                    if (g.online) g.net.sendFire(head + dir * 20.0f, head + dir * bestT, uint8_t(gun), int(i));
                }
                if (!hit) { nearMiss(head, dir, bestT); continue; }
                const HitGroup grp = hit == 2 ? kHead : kChest;
                dmg += armoredDamage(damageAt(gd, bestT) * hitGroupDamageScale(grp), grp, yourArmor, yourHelmet, gd.armorRatio);
                if (hit == 2) headHit = true;
            }
            if (i < g.spottedUntil.size()) g.spottedUntil[i] = g.simTime + 1.0;  // shooting gives you away
            if (dmg > 0 && hurtPlayer(g, int(i), dmg, headHit, wd.name) && !comp)
                break;  // you died: nobody else shoots at your new spawn this tick
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

// ---- Menus: the main menu at launch, the pause menu on Esc, settings split into pages ----
// A row is either a setting (f or i points at the value) or a button (action).
struct MenuItem {
    const char* name;
    float* f = nullptr;  // float setting, or
    int* i = nullptr;    // int setting
    float step = 1, lo = 0, hi = 1;
    const char* const* labels = nullptr;  // optional names for int values
    int action = 0;                        // kAct*: a button instead of a setting
    std::string* text = nullptr;           // a text field (type to edit) instead of a setting
};

enum MenuScreen {
    kMenuNone, kMenuMain, kMenuPause, kMenuPlay, kMenuSettings, kMenuControls,
    kMenuMouse, kMenuCrosshair, kMenuWeapon, kMenuVideo, kMenuGameplay,  // settings pages, in order
    kMenuInventory, kMenuCase,
    kMenuStats,  // your career: rating, totals, recent matches
};
enum MenuAction { kActNone, kActResume, kActStart, kActReset, kActReload, kActQuit, kActBack, kActMainMenu, kActHost, kActJoin,
                  kActReplay,
                  kActOpenCase, kActSkipCase, kActEquipNew, kActGoto = 100 };
constexpr int goTo(MenuScreen m) { return int(kActGoto) + int(m); }  // a button that opens screen m

// Which screen is up (kMenuNone = playing), the highlighted row, and where Back ends up:
// kMenuMain before a game has started, kMenuPause once one has.
struct MenuState { int screen = kMenuNone, sel = 0, root = kMenuMain; };
MenuState g_menu;
bool g_replayAvailable = false;  // the pause menu offers WATCH REPLAY (something's recorded, offline)

const char* const kOnOff[] = {"OFF", "ON"};
const uint32_t kCrosshairColors[] = {0x00FF00, 0xFFFF00, 0x00FFFF, 0xFFFFFF, 0xFF3030, 0xFF40FF};
const char* const kCrosshairColorNames[] = {"GREEN", "YELLOW", "CYAN", "WHITE", "RED", "PINK"};
int g_crosshairPreset = 0;  // menu-side index into kCrosshairColors

// The PLAY screen's choices; they only take effect on START.
struct GameMenu { int map = 0, mode = 0, bots = 0, drill = 0, route = 0, pfBots = 1; };
GameMenu g_gameMenu;
const char* const kMapNames[] = {"THE LAB", "DUST", "HARBOR"};
const char* const kModeNames[] = {"PRACTICE", "DEATHMATCH", "RETAKES", "COMPETITIVE 5V5", "PREFIRE", "ONLINE"};
const char* const kSkillNames[] = {"EASY", "NORMAL", "HARD", "EXPERT"};
const char* const kVarianceNames[] = {"OFF (ALL THE SAME)", "SLIGHT (+/- HALF A LEVEL)", "WIDE (+/- A LEVEL)"};
const char* const kPreviewNames[] = {"OFF", "NOT IN COMPETITIVE", "ALWAYS"};
const char* const kNetGameNames[] = {"DEATHMATCH", "COMPETITIVE"};
const char* const kNetTeamNames[] = {"SPLIT BETWEEN THE SIDES", "ALL ON ONE SIDE VS BOTS"};
const char* const kControls[][2] = {
    {"W A S D", "MOVE"}, {"SPACE / WHEEL", "JUMP (HOLD TO BUNNY HOP)"}, {"CTRL", "CROUCH"}, {"SHIFT", "WALK"},
    {"MOUSE 1", "FIRE / THROW"}, {"MOUSE 2", "SCOPE / LOB A GRENADE"}, {"R", "RELOAD"},
    {"1  2  3", "PRIMARY, PISTOL, KNIFE"}, {"4", "GRENADES (AGAIN: NEXT ONE)"}, {"G", "QUICK THROW"},
    {"Q", "LAST WEAPON"}, {"F", "INSPECT"}, {"B", "BUY MENU"}, {"E", "PLANT / DEFUSE"}, {"TAB", "SCORES"},
    {"V", "NOCLIP"}, {"C", "CLEAR BULLET HOLES"}, {"ALT + ENTER", "FULLSCREEN"}, {"ESC", "MENU"},
};
constexpr size_t kControlLines = sizeof(kControls) / sizeof(kControls[0]);

const char* menuTitle(int screen) {
    switch (screen) {
        case kMenuMain: return "CRISP";
        case kMenuPause: return "PAUSED";
        case kMenuPlay: return "PLAY";
        case kMenuSettings: return "SETTINGS";
        case kMenuControls: return "CONTROLS";
        case kMenuMouse: return "MOUSE + VIEW";
        case kMenuCrosshair: return "CROSSHAIR + HUD";
        case kMenuWeapon: return "WEAPONS + SKINS";
        case kMenuVideo: return "VIDEO + SOUND";
        case kMenuGameplay: return "GAMEPLAY";
        case kMenuInventory: return "INVENTORY";
        case kMenuCase: return "CASE";
        case kMenuStats: return "STATS";
        default: return "";
    }
}

std::vector<MenuItem> menuRows(int screen, Config& c, int mode) {
    auto button = [](const char* name, int action) {
        MenuItem m{name};
        m.action = action;
        return m;
    };
    const MenuItem back = button("BACK", kActBack);
    switch (screen) {
        case kMenuMain:
            return {button("PLAY", goTo(kMenuPlay)), button("INVENTORY", goTo(kMenuInventory)),
                    button("STATS", goTo(kMenuStats)), button("SETTINGS", goTo(kMenuSettings)), button("CONTROLS", goTo(kMenuControls)), button("QUIT", kActQuit)};
        case kMenuPause: {
            std::vector<MenuItem> r = {button("RESUME", kActResume), button("CHANGE MODE OR MAP", goTo(kMenuPlay))};
            if (mode == 0) r.push_back(button("RESET POSITION", kActReset));
            if (mode == 4) r.push_back(button("RESTART ROUTE", kActReset));
            if (g_replayAvailable) r.push_back(button(mode == 3 ? "WATCH REPLAY (FROM THIS ROUND)" : "WATCH REPLAY (LAST 2.5 MIN)", kActReplay));
            r.insert(r.end(), {button("INVENTORY", goTo(kMenuInventory)), button("STATS", goTo(kMenuStats)),
                               button("SETTINGS", goTo(kMenuSettings)),
                               button("CONTROLS", goTo(kMenuControls)), button("MAIN MENU", kActMainMenu),
                               button("QUIT", kActQuit)});
            return r;
        }
        case kMenuPlay: {
            const GameMenu& m = g_gameMenu;
            std::vector<MenuItem> r = {{"MODE", nullptr, &g_gameMenu.mode, 1, 0, 5, kModeNames}};
            if (m.mode == 5) {  // online: host, or join someone's game
                MenuItem name{"YOUR NAME"};
                name.text = &c.player_name;
                r.push_back(name);
                r.push_back({"GAME (WHEN YOU HOST)", nullptr, &c.net_game, 1, 0, 1, kNetGameNames});
                if (c.net_game == 1) {
                    r.push_back({"PLAYERS", nullptr, &c.net_teams, 1, 0, 1, kNetTeamNames});
                    r.push_back({"TEAMMATES (PLAYERS + BOTS)", nullptr, &c.comp_mates, 1, 0, 4});
                    r.push_back({"ENEMIES (PLAYERS + BOTS)", nullptr, &c.comp_enemies, 1, 1, 5});
                    r.push_back({"YOUR TEAM'S BOTS", nullptr, &c.mate_skill, 1, 0, 3, kSkillNames});
                    r.push_back({"OTHER TEAM'S BOTS", nullptr, &c.enemy_skill, 1, 0, 3, kSkillNames});
                    r.push_back({"SKILL VARIANCE", nullptr, &c.skill_variance, 1, 0, 2, kVarianceNames});
                    r.push_back({"FREEZE TIME (BUY), SECONDS", nullptr, &c.freeze_time, 1, 3, 30});
                }
                r.push_back(button("HOST A GAME", kActHost));
                MenuItem addr{"JOIN ADDRESS"};
                addr.text = &c.net_address;
                r.push_back(addr);
                r.push_back(button("JOIN", kActJoin));
                r.push_back({"PORT", nullptr, &c.net_port, 1, 1024, 65535});
                r.push_back({"DUST SIZE (HOST'S IS USED)", nullptr, &c.dust_scale, 5, 50, 100});
                r.push_back(back);
                return r;
            }
            if (m.mode == 4) {
                static const char* routeNames[8];
                const std::vector<PrefireRoute>& routes = townPrefireRoutes();
                for (size_t k = 0; k < routes.size() && k < 8; ++k) routeNames[k] = routes[k].name;
                r.push_back({"ROUTE", nullptr, &g_gameMenu.route, 1, 0, float(std::min<size_t>(routes.size(), 8) - 1), routeNames});
                r.push_back({"BOTS SHOOT BACK", nullptr, &c.prefire_bots_shoot, 1, 0, 1, kOnOff});
            }
            if (m.mode == 0) {
                r.push_back({"MAP", nullptr, &g_gameMenu.map, 1, 0, 2, kMapNames});
                r.push_back({"BOTS SHOOT BACK", nullptr, &g_gameMenu.bots, 1, 0, 1, kOnOff});
                if (m.map == 0) r.push_back({"AIM DRILL", nullptr, &g_gameMenu.drill, 1, 0, 1, kOnOff});
            }
            if (m.mode == 1) {
                r.push_back({"BOTS", nullptr, &c.dm_bots, 1, 1, 16});
                r.push_back({"MINUTES", nullptr, &c.dm_minutes, 1, 1, 30});
            }
            if (m.mode == 2) r.push_back({"BOTS ON THE SITE", nullptr, &c.rt_bots, 1, 1, 6});
            if (m.mode == 3) {
                r.push_back({"TEAMMATES", nullptr, &c.comp_mates, 1, 0, 4});
                r.push_back({"ENEMIES", nullptr, &c.comp_enemies, 1, 1, 5});
                r.push_back({"TEAMMATE SKILL", nullptr, &c.mate_skill, 1, 0, 3, kSkillNames});
                r.push_back({"ENEMY SKILL", nullptr, &c.enemy_skill, 1, 0, 3, kSkillNames});
                r.push_back({"SKILL VARIANCE", nullptr, &c.skill_variance, 1, 0, 2, kVarianceNames});
                r.push_back({"FREEZE TIME (BUY), SECONDS", nullptr, &c.freeze_time, 1, 3, 30});
            } else if (m.mode != 0 || m.bots) {
                r.push_back({"BOT SKILL", nullptr, &c.enemy_skill, 1, 0, 3, kSkillNames});
                r.push_back({"SKILL VARIANCE", nullptr, &c.skill_variance, 1, 0, 2, kVarianceNames});
            }
            if (m.mode == 1) r.push_back({"BOTS FIGHT EACH OTHER", nullptr, &c.dm_bot_fights, 1, 0, 1, kOnOff});
            if (m.mode != 0 && m.mode != 5) {  // the bot modes play on a town map: which one
                if (g_gameMenu.map == 0) g_gameMenu.map = 1;
                r.push_back({"MAP", nullptr, &g_gameMenu.map, 1, 1, 2, kMapNames});
            }
            if ((m.mode != 0 || m.map >= 1) && m.map != 2) r.push_back({"DUST SIZE (% OF REAL)", nullptr, &c.dust_scale, 5, 50, 100});
            r.push_back(button("START", kActStart));
            r.push_back(back);
            return r;
        }
        case kMenuSettings:
            return {button("MOUSE + VIEW", goTo(kMenuMouse)), button("CROSSHAIR + HUD", goTo(kMenuCrosshair)),
                    button("WEAPONS + SKINS", goTo(kMenuWeapon)), button("VIDEO + SOUND", goTo(kMenuVideo)),
                    button("GAMEPLAY", goTo(kMenuGameplay)),
                    button("RELOAD CONFIG.CFG", kActReload), back};
        case kMenuControls: return {back};
        case kMenuStats: return {back};
        case kMenuMouse:
            return {{"SENSITIVITY", &c.sensitivity, nullptr, 0.02f, 0.05f, 20.0f},
                    {"SCOPED SENSITIVITY RATIO", &c.zoom_sensitivity_ratio, nullptr, 0.05f, 0.1f, 3.0f},
                    {"FOV (4:3, CS = 90)", &c.fov, nullptr, 1.0f, 60.0f, 120.0f},
                    {"ZERO-LAG CAMERA", nullptr, &c.camera_extrapolate, 1, 0, 1, kOnOff},
                    {"SMOOTH STAIRS", nullptr, &c.view_smooth_steps, 1, 0, 1, kOnOff},
                    {"SPRAY CAMERA SHAKE", nullptr, &c.view_shake, 1, 0, 1, kOnOff},
                    back};
        case kMenuCrosshair:
            return {{"CROSSHAIR SIZE", nullptr, &c.crosshair_size, 1, 0, 30},
                    {"CROSSHAIR GAP", nullptr, &c.crosshair_gap, 1, -5, 20},
                    {"CROSSHAIR THICKNESS", nullptr, &c.crosshair_thickness, 1, 1, 8},
                    {"CROSSHAIR COLOR", nullptr, &g_crosshairPreset, 1, 0, 5, kCrosshairColorNames},
                    {"CROSSHAIR DOT", nullptr, &c.crosshair_dot, 1, 0, 1, kOnOff},
                    {"CROSSHAIR OUTLINE", nullptr, &c.crosshair_outline, 1, 0, 1, kOnOff},
                    {"HITMARKER", nullptr, &c.hitmarker, 1, 0, 1, kOnOff},
                    {"RADAR", nullptr, &c.radar, 1, 0, 1, kOnOff},
                    back};
        case kMenuWeapon:
            return {button("SKINS, KNIVES AND CASES", goTo(kMenuInventory)),
                    {"VIEWMODEL FOV", &c.viewmodel_fov, nullptr, 1.0f, 50.0f, 90.0f},
                    {"VIEWMODEL BOB", &c.viewmodel_bob, nullptr, 0.1f, 0.0f, 2.0f},
                    {"SHOW VIEWMODEL", nullptr, &c.show_viewmodel, 1, 0, 1, kOnOff},
                    back};
        case kMenuVideo:
            return {{"VOLUME", &c.volume, nullptr, 0.05f, 0.0f, 1.0f},
                    {"MENU MUSIC", &c.music_volume, nullptr, 0.05f, 0.0f, 1.0f},
                    {"HIT SOUND", nullptr, &c.hitsound, 1, 0, 1, kOnOff},
                    {"FPS CAP (0 = NONE)", nullptr, &c.fps_max, 30, 0, 1000},
                    {"LOW LATENCY MODE", nullptr, &c.low_latency, 1, 0, 1, kOnOff},
                    {"MUZZLE FLASH LIGHT (0 = OFF)", &c.muzzle_brightness, nullptr, 0.05f, 0.0f, 1.0f},
                    {"ANTI-ALIASING (RESTART)", nullptr, &c.msaa, 2, 0, 8},
                    back};
        case kMenuGameplay:
            return {{"BUNNY HOP", nullptr, &c.bhop, 1, 0, 1, kOnOff},
                    {"KILLCAM", nullptr, &c.killcam, 1, 0, 1, kOnOff},
                    {"PLAYER MODEL SIZE (%)", nullptr, &c.model_scale, 5, 80, 150},
                    {"GRENADE TRAJECTORY", nullptr, &c.nade_preview, 1, 0, 2, kPreviewNames},
                    {"RANDOM SPRAY SPREAD", nullptr, &c.spread_spray, 1, 0, 1, kOnOff},
                    {"RANDOM MOVING SPREAD", nullptr, &c.spread_movement, 1, 0, 1, kOnOff},
                    back};
        case kMenuInventory: {  // a row per weapon: the skins you own for it (left / right to change)
            static std::string caseLabel;
            caseLabel = g_inv.cases > 0 ? "OPEN A CASE (" + std::to_string(g_inv.cases) + ")" : std::string("OPEN A CASE (NONE YET)");
            std::vector<MenuItem> r = {button(caseLabel.c_str(), kActOpenCase)};
            for (int k = 0; k < kInvRows; ++k) {
                SkinChoice& ch = g_choices[k];
                r.push_back({kInvRowNames[k], nullptr, &ch.sel, 1, 0, float(std::max(0, int(ch.labels.size()) - 1)),
                             ch.labels.data()});
            }
            r.push_back({"ALL SKINS UNLOCKED (TESTING)", nullptr, &c.all_skins, 1, 0, 1, kOnOff});
            r.push_back(back);
            return r;
        }
        case kMenuCase: {
            if (!g_case.revealed) return {button("SKIP", kActSkipCase)};
            static std::string again;
            again = "OPEN ANOTHER (" + std::to_string(g_inv.cases) + " LEFT)";
            return {button("EQUIP IT", kActEquipNew), button(again.c_str(), kActOpenCase),
                    button("BACK TO INVENTORY", goTo(kMenuInventory))};
        }
        default: return {};
    }
}

// Changes setting row `it` by `dir` steps (shift = x5). Returns true if it's a setting.
bool adjustMenu(Config& c, const MenuItem& it, int dir, bool big) {
    if (it.action || it.text) return false;
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

int hudScale(const Config& cfg, int h) { return cfg.hud_scale > 0 ? cfg.hud_scale : std::max(1, h / 540); }

// Where a menu screen's panel and rows go (rows start at `rowsY`, one every `rowH`).
struct MenuLayout { float x, top, w, rowH, rowsY, bottom; };
MenuLayout menuLayout(int screen, size_t rows, int w, int h, int s) {
    MenuLayout L;
    L.rowH = 13.0f * float(s);
    L.w = 300.0f * float(s);
    const float extra = screen == kMenuControls ? float(kControlLines) * 10.0f * float(s) + L.rowH * 0.5f : 0.0f;
    const float panelH = L.rowH * (3.0f + float(rows) + 1.5f) + extra;
    L.x = float(w / 2) - L.w / 2;
    L.top = std::max(float(h) / 2 - panelH / 2, 16.0f * float(s));
    if (screen == kMenuInventory || screen == kMenuCase || screen == kMenuStats) {  // on the left: the weapon (or your stats) on the right
        L.x = 24.0f * float(s);
        if (screen == kMenuCase) L.top = std::max(float(h) * 0.5f, float(h) - panelH - 24.0f * float(s));
    }
    L.rowsY = L.top + L.rowH * 3.0f + extra;
    L.bottom = L.top + panelH;
    return L;
}

// Row k is drawn from rowsY + k * rowH - 3s, rowH tall.
int menuRowAt(const MenuLayout& L, size_t rows, int s, float mx, float my) {
    if (mx < L.x - 8.0f * float(s) || mx > L.x + L.w + 8.0f * float(s)) return -1;
    float k = (my - (L.rowsY - 3.0f * float(s))) / L.rowH;
    return k >= 0 && k < float(rows) ? int(k) : -1;
}

void drawMenu(HudBatch& hud, const Config& cfg, int mode, int w, int h, int s) {
    Config view = cfg;  // menuRows needs non-const pointers; we only read here
    const int screen = g_menu.screen;
    const std::vector<MenuItem> rows = menuRows(screen, view, mode);
    const MenuLayout L = menuLayout(screen, rows.size(), w, h, s);
    const float fs = float(s);
    hud.rect(0, 0, float(w), float(h), g_menu.root == kMenuMain ? 0x06080BD8 : 0x000000A0);
    hud.rect(L.x - 14 * fs, L.top - 14 * fs, L.w + 28 * fs, L.bottom - L.top + 28 * fs, 0x15181CE8);
    hud.text(L.x, L.top, menuTitle(screen), 0xFFD060FF, screen == kMenuMain ? s * 3 : s * 2);
    if (screen == kMenuMain) {
        const std::string ver = "V" CRISP_VERSION;
        hud.text(L.x + L.w - hud.textWidth(ver), L.top + 8 * fs, ver, 0x808080FF);
    }
    if (screen == kMenuControls) {
        float y = L.top + L.rowH * 3.0f;
        for (const auto& c : kControls) {
            hud.text(L.x, y, c[0], 0xFFFFFFFF);
            hud.text(L.x + 100 * fs, y, c[1], 0xB0B0B0FF);
            y += 10 * fs;
        }
    }
    for (size_t k = 0; k < rows.size(); ++k) {
        const MenuItem& it = rows[k];
        const float y = L.rowsY + float(k) * L.rowH;
        const bool sel = int(k) == g_menu.sel;
        if (sel) hud.rect(L.x - 6 * fs, y - 3 * fs, L.w + 12 * fs, L.rowH, 0x3A5F9AC0);
        const uint32_t col = sel ? 0xFFFFFFFF : it.action == kActStart ? 0x80FF80FF : 0xC8C8C8FF;
        hud.text(L.x, y, it.name, col);
        std::string v;
        if (it.action >= kActGoto) {
            v = ">";
        } else if (it.text) {  // shown in capitals (the font's), typed in any case
            std::string t = *it.text;
            for (char& ch : t) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            v = sel ? t + "_" : t;
        } else if (!it.action) {
            char val[48];
            if (it.f) std::snprintf(val, sizeof(val), it.step < 0.1f ? "%.2f" : "%.1f", double(*it.f));
            else if (it.labels) std::snprintf(val, sizeof(val), "%s", it.labels[*it.i - int(it.lo)]);
            else std::snprintf(val, sizeof(val), "%d", *it.i);
            v = sel ? std::string("< ") + val + " >" : std::string(val);
        }
        if (!v.empty()) hud.text(L.x + L.w - hud.textWidth(v), y, v, col);
    }
    const bool typing = g_menu.sel >= 0 && g_menu.sel < int(rows.size()) && rows[size_t(g_menu.sel)].text;
    const bool setting = g_menu.sel >= 0 && g_menu.sel < int(rows.size()) && !rows[size_t(g_menu.sel)].action && !typing;
    const char* hint = typing ? "TYPE IT   BACKSPACE DELETES"
                       : setting ? "LEFT/RIGHT, CLICK OR WHEEL CHANGES   (SAVED)"
                               : screen == kMenuMain ? "ENTER OR CLICK TO SELECT" : "ENTER OR CLICK TO SELECT   ESC BACK";
    hud.text(L.x, L.rowsY + (float(rows.size()) + 0.5f) * L.rowH, hint, 0x909090FF);
}

// ---- The buy wheel (B), like CS:GO's: a ring of categories, then a ring of that category's items. The
// mouse is free while it's open (hover and click); number keys work too. Two full-buy buttons underneath. ----
constexpr int kBuyFullRifle = 100, kBuyFullSniper = 101, kBuyCentre = -1, kBuyNothing = -2;

struct BuyWheel { float cx, cy, r0, r1; };
BuyWheel buyWheel(int w, int h, int s) { return {float(w) * 0.5f, float(h) * 0.47f, 54.0f * float(s), 150.0f * float(s)}; }

int buySlotCount(const Game& g) {
    const bool comp = g.mode == 3 && g.mapId == 1;
    return g.buyCategory == 0 ? (comp ? kCatGrenades : kCatSnipers) : int(buyEntries(g.buyCategory).size());
}

// The full-buy buttons under the wheel: x, y, width, height of button k (0 rifle, 1 sniper).
void fullBuyButton(const BuyWheel& wh, int s, int k, float& x, float& y, float& bw, float& bh) {
    bw = 150.0f * float(s);
    bh = 22.0f * float(s);
    x = wh.cx + (k == 0 ? -bw - 6.0f * float(s) : 6.0f * float(s));
    y = wh.cy + wh.r1 + 18.0f * float(s);
}

// What's under (mx, my): a wedge (0..n-1), the centre, a full-buy button, or nothing.
int buySlotAt(const Game& g, int w, int h, int s, float mx, float my) {
    const BuyWheel wh = buyWheel(w, h, s);
    if (g.buyCategory == 0)
        for (int k = 0; k < 2; ++k) {
            float x, y, bw, bh;
            fullBuyButton(wh, s, k, x, y, bw, bh);
            if (mx >= x && mx <= x + bw && my >= y && my <= y + bh) return k == 0 ? kBuyFullRifle : kBuyFullSniper;
        }
    const float dx = mx - wh.cx, dy = my - wh.cy, r = std::sqrt(dx * dx + dy * dy);
    if (r < wh.r0) return kBuyCentre;
    if (r > wh.r1 + 30.0f * float(s)) return kBuyNothing;
    const int n = buySlotCount(g);
    if (n <= 0) return kBuyNothing;
    const float span = 2.0f * kPi / float(n);
    float a = std::atan2(dx, -dy);  // from straight up, clockwise
    if (a < 0) a += 2.0f * kPi;
    return int((a + span * 0.5f) / span) % n;
}

// Buys everything a full round needs: the gun (rifle or sniper), kevlar and helmet, a kit on CT, then
// smoke, flash, HE and molotov while the money lasts. Outside competitive: just the gun.
const char* fullBuy(Game& g, bool sniper) {
    g.buyCategory = 0;
    g.buyMenu = false;
    if (!(g.mode == 3 && g.mapId == 1)) {
        takeGun(g, sniper ? kWSniper : kWRifle);
        return weaponDef(sniper ? kWSniper : kWRifle).name;
    }
    const int before = g.comp.money;
    const int cat = sniper ? kCatSnipers : kCatRifles;
    compBuy(g, cat, buyIndexOf(cat, sniper ? kWSniper : kWRifle));
    compBuy(g, kCatGear, 1);
    if (g.comp.youTeam == 1) compBuy(g, kCatGear, 2);
    for (int k = 0; k < 4; ++k) compBuy(g, kCatGrenades, k);
    return g.comp.money < before ? (sniper ? "FULL BUY: AWP" : "FULL BUY: AK-47") : "NOT ENOUGH MONEY";
}

// Picks slot `slot` of the wheel (a wedge, the centre, a full buy). Returns a message for the hit log.
const char* buyPick(Game& g, int slot) {
    const bool comp = g.mode == 3 && g.mapId == 1;
    if (slot == kBuyFullRifle || slot == kBuyFullSniper) return fullBuy(g, slot == kBuyFullSniper);
    if (slot == kBuyCentre) {  // back to the categories (or shut it from there)
        if (g.buyCategory > 0) g.buyCategory = 0;
        else g.buyMenu = false;
        return "";
    }
    if (slot < 0 || slot >= buySlotCount(g)) return "";
    if (g.buyCategory == 0) {
        g.buyCategory = slot + 1;
        return "";
    }
    if (comp) return compBuy(g, g.buyCategory, slot);
    const std::vector<BuyEntry> entries = buyEntries(g.buyCategory);
    if (entries[size_t(slot)].weapon < 0) return "";
    takeGun(g, entries[size_t(slot)].weapon);
    g.buyMenu = false;
    g.buyCategory = 0;
    return entries[size_t(slot)].name;
}

void drawBuyWheel(HudBatch& hud, const Game& g, int w, int h, int s) {
    const bool comp = g.mode == 3 && g.mapId == 1;
    const Game::Comp& c = g.comp;
    const BuyWheel wh = buyWheel(w, h, s);
    const float fs = float(s);
    const int n = buySlotCount(g), hover = buySlotAt(g, w, h, s, g.buyMouseX, g.buyMouseY);
    const std::vector<BuyEntry> entries = buyEntries(g.buyCategory);
    hud.rect(0, 0, float(w), float(h), 0x00000060);  // dim the game a little
    const float span = 2.0f * kPi / float(std::max(n, 1)), gap = 0.012f;
    auto at = [&](float a, float r) { return std::pair<float, float>{wh.cx + std::sin(a) * r, wh.cy - std::cos(a) * r}; };
    for (int k = 0; k < n; ++k) {
        bool have = false, broke = false;
        if (g.buyCategory > 0) {
            const BuyEntry& e = entries[size_t(k)];
            have = comp ? buyOwned(g, e) : e.weapon >= 0 && (weaponDef(e.weapon).primary ? g.primary : g.secondary)->def->id == e.weapon;
            broke = comp && !have && c.money < buyPrice(g, e);
        }
        const uint32_t fill = k == hover ? 0x3A5F9AF0 : have ? 0x1E2A22E8 : broke ? 0x2A1A1AE8 : 0x15181CE8;
        const float a0 = float(k) * span - span * 0.5f + gap, a1 = float(k) * span + span * 0.5f - gap;
        const int segs = std::max(4, int(24.0f / float(std::max(n, 1))) + 2);
        for (int q = 0; q < segs; ++q) {  // the wedge, as a strip of quads
            const float t0 = a0 + (a1 - a0) * float(q) / float(segs), t1 = a0 + (a1 - a0) * float(q + 1) / float(segs);
            auto [ix0, iy0] = at(t0, wh.r0);
            auto [ox0, oy0] = at(t0, wh.r1);
            auto [ox1, oy1] = at(t1, wh.r1);
            auto [ix1, iy1] = at(t1, wh.r0);
            hud.quad(ix0, iy0, ox0, oy0, ox1, oy1, ix1, iy1, fill);
            if (k == hover) {  // a bright rim on the one under the mouse
                auto [rx0, ry0] = at(t0, wh.r1 + 3.0f * fs);
                auto [rx1, ry1] = at(t1, wh.r1 + 3.0f * fs);
                hud.quad(ox0, oy0, rx0, ry0, rx1, ry1, ox1, oy1, 0xFFD060FF);
            }
        }
        // Its label at the middle of the wedge: the key, the name, and (competitive) the price.
        auto [lx, ly] = at(float(k) * span, (wh.r0 + wh.r1) * 0.5f);
        std::string name = g.buyCategory == 0 ? kBuyCategories[k] : entries[size_t(k)].name;
        const std::string key = std::to_string(k + 1);
        hud.text(lx - hud.textWidth(key) * 0.5f, ly - 16.0f * fs, key, 0xA0A0A0FF);
        hud.text(lx - hud.textWidth(name) * 0.5f, ly - 4.0f * fs, name, have ? 0x80C080FF : broke ? 0xFF7070FF : 0xFFFFFFFF);
        if (g.buyCategory > 0 && comp) {
            const std::string price = have ? std::string("OWNED") : "$" + std::to_string(buyPrice(g, entries[size_t(k)]));
            hud.text(lx - hud.textWidth(price) * 0.5f, ly + 8.0f * fs, price, have ? 0x80C080FF : 0xFFD060FF);
        }
    }
    // The centre: your money; it takes you back (or shuts the wheel).
    for (int q = 0; q < 32; ++q) {
        const float t0 = float(q) / 32.0f * 2.0f * kPi, t1 = float(q + 1) / 32.0f * 2.0f * kPi;
        auto [x0, y0] = at(t0, wh.r0 - 4.0f * fs);
        auto [x1, y1] = at(t1, wh.r0 - 4.0f * fs);
        hud.quad(wh.cx, wh.cy, x0, y0, x1, y1, wh.cx, wh.cy, hover == kBuyCentre ? 0x2A3442F0 : 0x0E1014F0);
    }
    const std::string money = comp ? "$" + std::to_string(c.money) : std::string("FREE");
    hud.text(wh.cx - hud.textWidth(money, s * 2) * 0.5f, wh.cy - 14.0f * fs, money, 0xFFD060FF, s * 2);
    const char* back = g.buyCategory > 0 ? "BACK" : "CLOSE";
    hud.text(wh.cx - hud.textWidth(back) * 0.5f, wh.cy + 6.0f * fs, back, 0xA0A0A0FF);
    if (g.buyCategory > 0) {
        const char* cat = kBuyCategories[g.buyCategory - 1];
        hud.text(wh.cx - hud.textWidth(cat) * 0.5f, wh.cy + 16.0f * fs, cat, 0xFFFFFFFF);
    } else {  // full buys
        for (int k = 0; k < 2; ++k) {
            float x, y, bw, bh;
            fullBuyButton(wh, s, k, x, y, bw, bh);
            const bool on = hover == (k == 0 ? kBuyFullRifle : kBuyFullSniper);
            hud.rect(x, y, bw, bh, on ? 0x3A5F9AF0 : 0x15181CE8);
            hud.rect(x, y + bh - 2.0f * fs, bw, 2.0f * fs, 0xFFD060FF);
            const char* t = k == 0 ? "8  FULL BUY: AK-47" : "9  FULL BUY: AWP";
            hud.text(x + (bw - hud.textWidth(t)) * 0.5f, y + 7.0f * fs, t, 0xFFFFFFFF);
        }
    }
    const char* hint = "CLICK OR 1-9   RIGHT CLICK / ESC BACK   B CLOSE";
    hud.text(wh.cx - hud.textWidth(hint) * 0.5f, wh.cy + wh.r1 + 50.0f * fs, hint, 0x909090FF);
}

// The inventory and case screens: where the turning weapon is shown (pixels), and what goes there.
struct Stage {
    float x = 0, y = 0, w = 0, h = 0;
    bool show = false;
    int weapon = kWRifle;  // WeaponId
    Equipped item;
};
Stage showcaseStage(const Config& cfg, int w, int h) {
    Stage st;
    const int s = hudScale(cfg, h);
    const float fs = float(s);
    const float left = 24.0f * fs + 300.0f * fs + 30.0f * fs;  // right of the menu panel
    if (g_menu.screen == kMenuInventory) {
        st.x = left;
        st.y = 40.0f * fs;
        st.w = float(w) - st.x - 24.0f * fs;
        st.h = float(h) - st.y - 70.0f * fs;
        const int row = g_menu.sel - 1;  // row 0 opens a case; then one per weapon
        if (row >= 0 && row < kInvRows) {
            const SkinChoice& ch = g_choices[row];
            st.show = true;
            st.weapon = kInvWeapons[row];
            if (ch.sel >= 0 && ch.sel < int(ch.opts.size())) st.item = ch.opts[size_t(ch.sel)];
        }
    } else if (g_menu.screen == kMenuCase && g_case.revealed && g_case.item >= 0) {
        st.x = left;
        st.y = float(h) * 0.24f + 60.0f * fs;
        st.w = float(w) - st.x - 24.0f * fs;
        st.h = float(h) - st.y - 40.0f * fs;
        const Item& it = g_inv.items[size_t(g_case.item)];
        st.show = true;
        st.weapon = allSkins()[size_t(it.skin)].weapon;
        st.item = {it.skin, it.wear};
    }
    return st;
}

// The stage's backdrop and the item's name, rarity and wear (the weapon itself is 3D, drawn after the HUD).
void drawStageHud(HudBatch& hud, const Stage& st, int s) {
    const float fs = float(s);
    hud.rect(st.x, st.y, st.w, st.h, 0x0C0E12F0);
    hud.rect(st.x, st.y, st.w, 2.0f * fs, 0x2A2F38FF);
    if (!st.show) return;
    std::string name = st.item.skin >= 0 ? allSkins()[size_t(st.item.skin)].name
                       : st.weapon == kWKnife  ? std::string("KNIFE (DEFAULT)")
                                               : std::string(weaponDef(st.weapon).name) + " (DEFAULT)";
    const uint32_t col = st.item.skin >= 0 ? (rarityColor(allSkins()[size_t(st.item.skin)].rarity) << 8) | 0xFF : 0xC8C8C8FF;
    const float ty = st.y + st.h - 40.0f * fs;
    hud.rect(st.x, ty - 6.0f * fs, st.w, 46.0f * fs, 0x08090CF0);
    hud.rect(st.x, ty - 6.0f * fs, st.w, 3.0f * fs, col);
    hud.text(st.x + 12.0f * fs, ty, name, col, s * 2);
    if (st.item.skin >= 0) {
        char line[96];
        std::snprintf(line, sizeof(line), "%s   %s   FLOAT %.4f", rarityName(allSkins()[size_t(st.item.skin)].rarity),
                      wearName(st.item.wear), double(st.item.wear));
        hud.text(st.x + 12.0f * fs, ty + 20.0f * fs, line, 0xB0B0B0FF);
    }
}

// The case screen: the reel of cards sliding under the marker, then what you got.
void drawCaseHud(HudBatch& hud, int w, int h, int s) {
    const float fs = float(s), cy = float(h) * 0.24f, cardW = 104.0f * fs, cardH = 64.0f * fs, gap = 6.0f * fs;
    hud.rect(0, cy - cardH * 0.5f - 12.0f * fs, float(w), cardH + 24.0f * fs, 0x0A0C10F0);
    const float pos = g_case.revealed ? float(g_case.win) + g_case.landing : caseReelPos(g_uiTime);
    const std::vector<SkinDef>& skins = allSkins();
    for (size_t k = 0; k < g_case.reel.size(); ++k) {
        const float x = float(w) * 0.5f + (float(k) - pos) * (cardW + gap);
        if (x + cardW < 0 || x > float(w)) continue;
        const SkinDef& sk = skins[size_t(g_case.reel[k].skin)];
        const uint32_t rc = (rarityColor(sk.rarity) << 8) | 0xFF;
        const bool won = g_case.revealed && int(k) == g_case.win;
        hud.rect(x, cy - cardH * 0.5f, cardW, cardH, won ? 0x262B34FF : 0x181B21FF);
        hud.rect(x, cy + cardH * 0.5f - 6.0f * fs, cardW, 6.0f * fs, rc);
        hud.rect(x, cy - cardH * 0.5f, cardW, cardH * 0.55f, (rc & 0xFFFFFF00u) | 0x30);  // a wash of its colour
        if (sk.rarity == kRareSpecial && !won) {  // the gold: which knife stays a secret until it lands
            hud.text(x + 8.0f * fs, cy - 18.0f * fs, "RARE", rc);
            hud.text(x + 8.0f * fs, cy - 6.0f * fs, "SPECIAL ITEM", rc);
        } else {
            const std::string& n = sk.name;
            const size_t bar = n.find(" | ");
            const std::string top = bar == std::string::npos ? n : n.substr(0, bar);
            const std::string bottom = bar == std::string::npos ? "" : n.substr(bar + 3);
            hud.text(x + 8.0f * fs, cy - 18.0f * fs, top, 0xB8B8B8FF);
            hud.text(x + 8.0f * fs, cy - 6.0f * fs, bottom, 0xFFFFFFFF);
        }
    }
    hud.rect(float(w) * 0.5f - 1.0f * fs, cy - cardH * 0.5f - 10.0f * fs, 2.0f * fs, cardH + 20.0f * fs, 0xFFD060FF);  // marker
    if (!g_case.revealed) {
        const char* t = "OPENING...";
        hud.text(float(w) * 0.5f - hud.textWidth(t, s * 2) * 0.5f, cy + cardH * 0.5f + 20.0f * fs, t, 0xFFD060FF, s * 2);
    }
}

// The STATS page (right of the menu): rating and rank, a graph of it, career totals, the last matches.
void drawStatsPanel(HudBatch& hud, int w, int h, int s) {
    const float fs = float(s), x = 24.0f * fs + 300.0f * fs + 30.0f * fs, y = 40.0f * fs;
    const float pw = float(w) - x - 24.0f * fs, ph = float(h) - y - 70.0f * fs;
    hud.rect(x, y, pw, ph, 0x0E1116E8);
    const Career& c = g_career;
    char line[160];
    std::snprintf(line, sizeof(line), "%d", c.rating);
    hud.text(x + 16.0f * fs, y + 14.0f * fs, line, 0xFFD060FF, s * 4);
    const float rx = x + 16.0f * fs + hud.textWidth(line, s * 4) + 14.0f * fs;
    hud.text(rx, y + 16.0f * fs, rankName(c.rating), 0xFFFFFFFF, s * 2);
    std::snprintf(line, sizeof(line), "RATING (BEST %d)   NEXT RANK AT %d", c.best, std::max(825, (c.rating - 700) / 125 * 125 + 825));
    hud.text(rx, y + 36.0f * fs, line, 0x909090FF);
    float ty = y + 62.0f * fs;
    if (c.matches.empty()) {
        hud.text(x + 16.0f * fs, ty, "NO MATCHES YET: FINISH A DEATHMATCH OR A COMPETITIVE MATCH AGAINST BOTS.", 0xB0B0B0FF);
        hud.text(x + 16.0f * fs, ty + 14.0f * fs, "BEAT BETTER BOTS FOR MORE RATING: EASY 700, NORMAL 950, HARD 1200, EXPERT 1450.", 0x808080FF);
        return;
    }
    const int k = c.kills(), d = c.deaths();
    std::snprintf(line, sizeof(line), "MATCHES %zu   WINS %d   K/D %.2f   HS %d%%   ADR %.0f", c.matches.size(), c.wins(),
                  double(k) / double(std::max(1, d)), k ? c.hsKills() * 100 / k : 0, double(c.adr()));
    hud.text(x + 16.0f * fs, ty, line, 0xFFFFFFFF);
    ty += 20.0f * fs;
    // The rating over the last 40 matches.
    const size_t n = std::min<size_t>(c.matches.size(), 40), first = c.matches.size() - n;
    const float gx = x + 16.0f * fs, gw = pw - 32.0f * fs, gh = 70.0f * fs;
    hud.rect(gx, ty, gw, gh, 0x00000060);
    int lo = c.matches[first].ratingBefore, hi = lo;
    for (size_t i = first; i < c.matches.size(); ++i) { lo = std::min(lo, c.matches[i].ratingAfter); hi = std::max(hi, c.matches[i].ratingAfter); }
    if (hi - lo < 40) { lo -= 20; hi += 20; }
    auto py = [&](int r) { return ty + gh - 4.0f * fs - (gh - 8.0f * fs) * float(r - lo) / float(hi - lo); };
    for (size_t i = first; i < c.matches.size(); ++i) {
        const float x0 = gx + gw * float(i - first) / float(n), x1 = gx + gw * float(i - first + 1) / float(n);
        const MatchRecord& m = c.matches[i];
        hud.line(x0, py(m.ratingBefore), x1, py(m.ratingAfter), 2.0f * fs, m.ratingAfter >= m.ratingBefore ? 0x60E080FF : 0xFF6060FF);
    }
    std::snprintf(line, sizeof(line), "%d", hi);
    hud.text(gx + 4.0f * fs, ty + 3.0f * fs, line, 0x707070FF);
    std::snprintf(line, sizeof(line), "%d", lo);
    hud.text(gx + 4.0f * fs, ty + gh - 12.0f * fs, line, 0x707070FF);
    ty += gh + 14.0f * fs;
    hud.text(x + 16.0f * fs, ty, "LAST MATCHES", 0xFFD060FF);
    ty += 14.0f * fs;
    for (size_t i = c.matches.size(); i-- > 0 && ty < y + ph - 14.0f * fs;) {
        const MatchRecord& m = c.matches[i];
        const int dr = m.ratingAfter - m.ratingBefore;
        char adr[16] = "   ";
        if (m.mode == 3) std::snprintf(adr, sizeof(adr), "%3.0f", double(m.adr()));  // (rounds only)
        std::snprintf(line, sizeof(line), "%-16s %-12s %-11s %3d-%-3d HS %3d%%  %s%s   %s%d", m.when.c_str(),
                      m.mode == 3 ? "COMPETITIVE" : "DEATHMATCH", m.result.c_str(), m.kills, m.deaths,
                      m.kills ? m.hsKills * 100 / m.kills : 0, m.mode == 3 ? "ADR " : "    ", adr, dr >= 0 ? "+" : "", dr);
        hud.text(x + 16.0f * fs, ty, line, dr >= 0 ? 0xC8F0C8FF : 0xF0C8C8FF);
        ty += 12.0f * fs;
    }
}

// Over the menu on the inventory and case screens: the reel, the stage and your case count.
void drawMenuExtras(HudBatch& hud, const Config& cfg, int w, int h, int s) {
    if (g_menu.screen == kMenuStats) drawStatsPanel(hud, w, h, s);
    if (g_menu.screen != kMenuInventory && g_menu.screen != kMenuCase) return;
    if (g_menu.screen == kMenuCase) drawCaseHud(hud, w, h, s);
    const Stage st = showcaseStage(cfg, w, h);
    if (g_menu.screen == kMenuInventory || st.show) drawStageHud(hud, st, s);
    if (g_menu.screen == kMenuInventory) {
        char line[120];
        std::snprintf(line, sizeof(line), "CASES %d   NEXT CASE IN %d KILLS (DEATHMATCH, RETAKES, COMPETITIVE, ONLINE)",
                      g_inv.cases, std::max(1, g_caseKills) - g_inv.progress);
        hud.text(st.x + 12.0f * float(s), st.y + 10.0f * float(s), line, 0xFFD060FF);
        if (!st.show) {
            const char* t = "PICK A WEAPON ON THE LEFT, LEFT / RIGHT FOR ITS SKINS";
            hud.text(st.x + st.w * 0.5f - hud.textWidth(t) * 0.5f, st.y + st.h * 0.5f, t, 0x808080FF);
        }
    }
}

void buildHud(HudBatch& hud, const Game& g, const Config& cfg, const FrameStats& st, int w, int h) {
    hud.clear();
    int s = hudScale(cfg, h);
    hud.fontScale = s;
    if (g_menu.screen != kMenuNone && g_menu.root == kMenuMain) {  // main menu: no game HUD behind it
        drawMenu(hud, cfg, g.mode, w, h, s);
        drawMenuExtras(hud, cfg, w, h, s);
        return;
    }
    const float lh = 10.0f * s;
    char buf[160];

    if (g.rv.on) {  // a replay: its own HUD instead of the game's
        const float cx = float(w / 2), cy = float(h / 2), fs = float(s);
        const int you = int(g.dummies.size());
        auto who = [&](int id) { return id == you ? std::string("YOU") : id >= 0 ? agentName(id) : std::string("FREE CAMERA"); };
        hud.rect(cx - 1.0f * fs, cy - 1.0f * fs, 2.0f * fs, 2.0f * fs, 0xFFFFFFC0);  // where they aim
        if (g.rv.killcam) {
            const std::string title = "KILLCAM";
            hud.rect(0, 0, float(w), 46.0f * fs, 0x000000A0);
            hud.text(cx - hud.textWidth(title, s * 2) / 2, 8.0f * fs, title, 0xFF5A5AFF, s * 2);
            const std::string by = who(g.rv.killer) + " KILLED YOU";
            hud.text(cx - hud.textWidth(by) / 2, 30.0f * fs, by, 0xFFFFFFFF);
            const char* skip = "CLICK TO SKIP";
            hud.text(cx - hud.textWidth(skip) / 2, float(h) - 30.0f * fs, skip, 0xC0C0C0FF);
            return;
        }
        const double t0 = g.replay.start(), t1 = std::max(g.replay.end(), t0 + 0.01);
        const int at = int(g.rv.t - t0), len = int(t1 - t0);
        std::snprintf(buf, sizeof(buf), "REPLAY   %d:%02d / %d:%02d   x%g%s   WATCHING: %s", at / 60, at % 60, len / 60, len % 60,
                      double(g.rv.speed), g.rv.paused ? "   PAUSED" : "", who(g.rv.pov).c_str());
        hud.rect(0, 0, float(w), 26.0f * fs, 0x000000A0);
        hud.text(cx - hud.textWidth(buf) / 2, 8.0f * fs, buf, 0xFFD060FF);
        const float bx = 40.0f * fs, bw = float(w) - 80.0f * fs, by = float(h) - 44.0f * fs;
        hud.rect(bx, by, bw, 6.0f * fs, 0x000000C0);
        hud.rect(bx, by, bw * float((g.rv.t - t0) / (t1 - t0)), 6.0f * fs, 0xFFD060FF);
        const char* keys = g.rv.pov < 0
            ? "WASD + MOUSE FLY (SHIFT SLOW, SPACE/CTRL UP/DOWN)   F BACK TO PLAYERS   LEFT/RIGHT -5/+5 S   UP/DOWN SPEED   P PAUSE   ESC EXIT"
            : "MOUSE 1 / 2 NEXT / PREVIOUS PLAYER   F FREE CAMERA   LEFT/RIGHT -5/+5 S   UP/DOWN SPEED   SPACE PAUSE   ESC EXIT";
        hud.text(cx - hud.textWidth(keys) / 2, by + 14.0f * fs, keys, 0xC0C0C0FF);
        return;
    }

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
    const bool sniper = g.weapon->def->scope;
    if (sniper && g.zoom > 0) {
        // Scope, like CS's: black outside the lens (drawn as horizontal strips), a soft dark ring inside its
        // edge, thin cross hairs across the middle that thicken into posts towards the rim.
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
        constexpr int kSeg = 96, kRings = 12;
        for (int ring = 0; ring < kRings; ++ring) {  // the lens edge, darker towards the rim
            const float u0 = float(ring) / kRings, u1 = float(ring + 1) / kRings;
            const float ri = r * (0.82f + 0.18f * u0), ro = r * (0.82f + 0.18f * u1);
            const uint32_t col = uint32_t(12.0f + 190.0f * u1 * u1);  // black, alpha rising
            for (int k = 0; k < kSeg; ++k) {
                const float a0 = 2.0f * kPi * float(k) / kSeg, a1 = 2.0f * kPi * float(k + 1) / kSeg;
                hud.quad(cx + std::cos(a0) * ri, cy + std::sin(a0) * ri, cx + std::cos(a0) * ro, cy + std::sin(a0) * ro,
                         cx + std::cos(a1) * ro, cy + std::sin(a1) * ro, cx + std::cos(a1) * ri, cy + std::sin(a1) * ri, col);
            }
        }
        const float thin = std::max(1.0f, float(s) * 0.5f), post = 3.0f * float(s);
        hud.rect(cx - r, cy - std::floor(thin * 0.5f), 2 * r, thin, 0x000000FF);
        hud.rect(cx - std::floor(thin * 0.5f), cy - r, thin, 2 * r, 0x000000FF);
        hud.rect(cx - r, cy - post * 0.5f, r * 0.5f, post, 0x000000FF);
        hud.rect(cx + r * 0.5f, cy - post * 0.5f, r * 0.5f, post, 0x000000FF);
        hud.rect(cx - post * 0.5f, cy + r * 0.5f, post, r * 0.5f, 0x000000FF);
        hud.rect(cx - post * 0.5f, cy - r, post, r * 0.5f, 0x000000FF);
    } else if (!sniper) {  // like CS: the sniper has no crosshair unscoped
        if (cfg.crosshair_outline) crossRects(1, 0x000000C0);
        crossRects(0, xc);
    }

    // Hit marker.
    if (g.showHitMarker && g.simTime < g.hitMarkerUntil) {
        // Subtle: small thin ticks just outside the crosshair, a slight pop, quick fade.
        float age = float(g.simTime - g.hitMarkerStart), life = float(g.hitMarkerUntil - g.hitMarkerStart);
        float pop = 1.0f + 0.15f * std::max(0.0f, 1.0f - age / 0.04f);
        float fade = std::clamp((life - age) / (life * 0.6f), 0.0f, 1.0f);
        uint32_t alpha = uint32_t(200.0f * fade);
        uint32_t hc = (g.hitMarkerHead ? 0xFF5A5A00u : 0xFFFFFF00u) | alpha;
        float size = (g.hitMarkerKill ? 1.25f : 1.0f) * pop * float(s);
        float a = float(gap) + 3.0f * size, b = float(gap) + 7.0f * size, lw = float(s);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                hud.line(cx + float(sx) * a, cy + float(sy) * a, cx + float(sx) * b, cy + float(sy) * b, lw, hc);
    }

    // Radar (top-left, like CS): the whole map north-up, shaded by height; you as an arrow, enemies as
    // red dots while spotted, the bomb in retakes.
    float radarBottom = 0;
    if (g.showRadar && g.mapId == 1 && !g.radarRects.empty()) {
        const float size = 190.0f * float(s), ox = 10.0f * float(s), oy = 10.0f * float(s);
        const float spanX = g.radarMax.x - g.radarMin.x, spanY = g.radarMax.y - g.radarMin.y;
        const float k = size / std::max(spanX, spanY);
        auto toScreen = [&](float wx, float wy) {  // +y (north) is up
            return Vec3{ox + (wx - g.radarMin.x) * k + (size - spanX * k) * 0.5f,
                        oy + (g.radarMax.y - wy) * k + (size - spanY * k) * 0.5f, 0};
        };
        hud.rect(ox - 3 * s, oy - 3 * s, size + 6 * s, size + 6 * s, 0x0C0E10B8);
        for (const Game::RadarRect& rr : g.radarRects) {
            Vec3 a = toScreen(rr.x0, rr.y1), b = toScreen(rr.x1, rr.y0);
            hud.rect(a.x, a.y, std::max(1.0f, b.x - a.x), std::max(1.0f, b.y - a.y), rr.rgba);
        }
        if (g.mode == 2 && g.bombActive) {
            Vec3 bp = toScreen(g.bombPos.x, g.bombPos.y);
            hud.rect(bp.x - 3.0f * float(s), bp.y - 3.0f * float(s), 6.0f * float(s), 6.0f * float(s), 0xFF3030FF);
        }
        for (size_t i = 0; i < g.dummies.size() && i < g.spottedUntil.size(); ++i) {
            const Dummy& d = g.dummies[i];
            if (!d.alive() || (!d.friendly && g.simTime > g.spottedUntil[i])) continue;  // teammates always show
            Vec3 p = toScreen(d.pos.x, d.pos.y);
            hud.rect(p.x - 2.5f * float(s), p.y - 2.5f * float(s), 5.0f * float(s), 5.0f * float(s),
                     d.friendly ? 0x50A0FFFF : 0xFF4040FF);
        }
        if (g.mode == 3 && g.comp.carrier == -2 && !g.comp.planted) {  // the dropped bomb
            Vec3 bp = toScreen(g.comp.dropped.x, g.comp.dropped.y);
            hud.rect(bp.x - 3.0f * float(s), bp.y - 3.0f * float(s), 6.0f * float(s), 6.0f * float(s), 0xFFB030FF);
        }
        Vec3 me = toScreen(g.player.origin.x, g.player.origin.y);
        float yr = float(g.viewYaw) * kDegToRad;
        Vec3 fw{std::cos(yr), -std::sin(yr), 0}, rt{std::sin(yr), std::cos(yr), 0};  // screen y points down
        Vec3 tip = me + fw * (7.0f * s), l = me - fw * (4.0f * s) - rt * (4.0f * s), r = me - fw * (4.0f * s) + rt * (4.0f * s);
        for (auto [p, q] : {std::pair{tip, l}, std::pair{l, r}, std::pair{r, tip}})
            hud.line(p.x, p.y, q.x, q.y, 2.0f * s, 0xFFFFFFFF);
        radarBottom = oy + size + 8.0f * s;
    }

    // Top-left: performance + movement (under the radar when it's shown).
    float x = 12.0f * s, y = std::max(10.0f * s, radarBottom);
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
        if (g.mode == 1 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "DEATHMATCH   TAB SCORES");
        else if (g.mode == 2 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "RETAKES   TAB SCORES");
        else if (g.mode == 3 && g.mapId == 1 && g.online) {
            int n = 0;
            for (int id = 0; id < kNetMaxPlayers; ++id) n += g.netTeam[id] >= 0 || id == g.net.myId() || g.remotes[id].present;
            if (g.net.isHost()) std::snprintf(buf, sizeof(buf), "ONLINE COMPETITIVE   HOSTING   %d PLAYER%s   TAB SCORES   B BUY", n, n == 1 ? "" : "S");
            else std::snprintf(buf, sizeof(buf), "ONLINE COMPETITIVE   PING %d MS   TAB SCORES   B BUY", g.net.pingMs());
        }
        else if (g.mode == 3 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "COMPETITIVE   TAB SCORES   B BUY   E PLANT / DEFUSE");
        else if (g.mode == 4 && g.mapId == 1) std::snprintf(buf, sizeof(buf), "PREFIRE   BOTS SHOOT BACK");
        else if (g.mode == 5 && g.mapId == 1) {
            int n = 1;
            for (int id = 0; id < kNetMaxPlayers; ++id) n += g.remotes[id].present && id != g.net.myId();
            if (g.net.isHost()) std::snprintf(buf, sizeof(buf), "ONLINE   HOSTING   %d PLAYER%s   TAB SCORES", n, n == 1 ? "" : "S");
            else std::snprintf(buf, sizeof(buf), "ONLINE   %d PLAYERS   PING %d MS   TAB SCORES", n, g.net.pingMs());
        }
        else std::snprintf(buf, sizeof(buf), "BOTS SHOOT BACK   DEATHS %d", g.deaths);
        hud.text(x, y, buf, 0xFF8060FF);
        y += lh;
        std::snprintf(buf, sizeof(buf), "HP %.0f", double(std::max(0.0f, g.hp)));
        hud.text(16.0f * s, float(h) - 24.0f * s, buf, g.hp > 30 ? 0xFFFFFFFF : 0xFF5050FF, s * 2);
        if (g.simTime < g.hurtUntil) hud.rect(0, 0, float(w), float(h), 0xC0000040);
        // Damage direction (like CS): a red arc round the crosshair towards whoever shot you, fading over 1.2 s.
        for (int k = 0; k < 4; ++k) {
            const double age = g.simTime - g.hurtAt[k];
            if (g.hurtAt[k] < 0 || age > 1.2) continue;
            const float rel = (yawTo(g.player.origin, g.hurtFrom[k]) - float(g.viewYaw)) * kDegToRad;
            const uint32_t a = uint32_t(200.0f * (1.0f - float(age) / 1.2f));
            for (int q = -4; q <= 4; ++q) {  // the arc: dots 3 degrees apart, thicker in the middle
                const float ang = rel + float(q) * 3.0f * kDegToRad, r = 78.0f * float(s);
                const float sz = (std::abs(q) <= 1 ? 4.0f : std::abs(q) <= 3 ? 3.0f : 2.0f) * float(s);
                hud.rect(cx - std::sin(ang) * r - sz / 2, cy - std::cos(ang) * r - sz / 2, sz, sz, 0xE0202000u | a);
            }
        }
        if (g.deadUntil >= 0 && g.mode != 3) {
            const char* dead = "YOU DIED";
            hud.text(cx - hud.textWidth(dead, s * 3) / 2, cy - 60.0f * s, dead, 0xFF4040FF, s * 3);
        }
    }
    if (g.mapId == 1 && g.callout[0]) hud.text(cx - hud.textWidth(g.callout, s * 2) / 2, 12.0f * s, g.callout, 0xFFFFFFD0, s * 2);
    if (g.netJoining) {
        const char* c = "CONNECTING...";
        hud.text(cx - hud.textWidth(c, s * 2) / 2, cy - 60.0f * s, c, 0xFFD060FF, s * 2);
    }
    if (g.mapId == 1 && g.online) {
        if (g.mode == 5) {
            std::snprintf(buf, sizeof(buf), "ONLINE DEATHMATCH   KILLS %d   DEATHS %d", g.you.kills, g.you.deaths);
            hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, 0xFFFFFFFF);
        }
    }
    if (g.mapId == 1 && g.mode == 1) {
        int left = int(std::max(0.0, g.dmEnd - g.simTime));
        std::snprintf(buf, sizeof(buf), "%d:%02d   KILLS %d   DEATHS %d", left / 60, left % 60, g.you.kills, g.you.deaths);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, 0xFFFFFFFF);
    }
    if (g.mapId == 1 && g.mode == 4) {
        const Game::Prefire& pf = g.pf;
        int dead = 0;
        for (const Dummy& d : g.dummies) dead += !d.alive();
        const double t = pf.resultUntil >= 0 ? double(pf.last) : pf.start >= 0 ? g.simTime - pf.start : 0.0;
        const float best = pf.best[pf.route % 8];
        char bestText[24] = "-";
        if (best > 0) std::snprintf(bestText, sizeof(bestText), "%.2f", double(best));
        std::snprintf(buf, sizeof(buf), "PREFIRE %s   %d / %zu   %.2f S   BEST %s", townPrefireRoutes()[size_t(pf.route)].name,
                      dead, g.dummies.size(), t, bestText);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, 0xFFFFFFFF);
        if (pf.start < 0 && pf.resultUntil < 0) {
            const char* go = "MOVE OR SHOOT TO START THE CLOCK";
            hud.text(cx - hud.textWidth(go) / 2, 44.0f * s, go, 0xFFD060C0);
        }
        if (pf.resultUntil >= 0) {
            const char* head = pf.won ? "ROUTE CLEARED" : "YOU DIED";
            hud.text(cx - hud.textWidth(head, s * 3) / 2, cy - 90.0f * s, head, pf.won ? 0x60FF60FF : 0xFF5050FF, s * 3);
            std::snprintf(buf, sizeof(buf), "%.2f S   ACCURACY %d%%   HEADSHOTS %d%s", double(pf.last),
                          g.shots > 0 ? int(100.0f * float(g.hits) / float(g.shots) + 0.5f) : 0, g.headshots,
                          pf.won && best > 0 && std::fabs(best - pf.last) < 1e-4f ? "   NEW BEST" : "");
            hud.text(cx - hud.textWidth(buf, s * 2) / 2, cy - 60.0f * s, buf, 0xFFFFFFFF, s * 2);
        }
    }
    if (g.mapId == 1 && g.mode != 0 && g.mode != 4 && (g.showScores || g.dmOverUntil >= 0)) {
        // Scoreboard (Tab), and the results screen at the end of a deathmatch. Sorted by kills.
        // ADR = damage per round (retakes) or per life (deathmatch).
        int left = int(std::max(0.0, g.dmEnd - g.simTime));
        float rowH = 11.0f * s, panelW = 76.0f * 6 * s, panelH = rowH * float(g.bots.size() + 7);
        float px = cx - panelW / 2, py = cy - panelH / 2 - 40.0f * s;
        hud.rect(px - 10 * s, py - 10 * s, panelW + 20 * s, panelH + 20 * s, 0x15181CE0);
        if (g.mode == 1 && g.dmOverUntil >= 0) std::snprintf(buf, sizeof(buf), "MATCH OVER");
        else if (g.mode == 1) std::snprintf(buf, sizeof(buf), "DEATHMATCH   %d:%02d LEFT", left / 60, left % 60);
        else if (g.mode == 3) std::snprintf(buf, sizeof(buf), "COMPETITIVE   YOU %d : %d THEM   ROUND %d", g.comp.youScore,
                                            g.comp.themScore, g.comp.round + 1);
        else if (g.mode == 5) std::snprintf(buf, sizeof(buf), "ONLINE DEATHMATCH");
        else std::snprintf(buf, sizeof(buf), "RETAKES   WON %d   LOST %d", g.rtWon, g.rtLost);
        hud.text(px, py, buf, 0xFFD060FF, s * 2);
        float ry = py + rowH * 2.5f;
        const bool roundBased = g.mode == 2 || g.mode == 3;  // ADR means damage per round: not in deathmatch
        hud.text(px, ry, roundBased ? "NAME          K    A    D    ADR   HS%   MVP" : "NAME          K    A    D    HS%   MVP", 0xA0A0A0FF);
        ry += rowH * 1.3f;
        const int rounds = g.mode == 2 ? std::max(1, g.rtWon + g.rtLost) : g.mode == 3 ? std::max(1, g.comp.round) : 0;
        const bool teams = g.mode == 3 && g.team.size() == g.botStats.size();
        auto enemy = [&](int id) { return teams && teamOf(g, id) != g.comp.youTeam; };
        std::vector<int> order;
        for (int i = -1; i < int(g.botStats.size()); ++i) {
            bool here = true;  // online: who's here (competitive: who's playing)
            if (g.online && i >= 0)
                here = i != g.net.myId() && (g.mode == 3 ? i < kNetSlots && g.netTeam[i] >= 0
                                                         : i < kNetMaxPlayers && g.remotes[i].present);
            if (here) order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (enemy(a) != enemy(b)) return !enemy(a);  // your team first
            return statsOf(g, a).kills > statsOf(g, b).kills;
        });
        bool spaced = false;
        for (int id : order) {
            if (enemy(id) && !spaced) { ry += rowH * 0.6f; spaced = true; }
            const Game::Stats& ps = statsOf(g, id);
            int per = rounds ? rounds : ps.deaths + 1;
            char line[112];
            if (teams && !enemy(id) && (id < 0 || !g.online))  // your team's money, like CS (online: only yours)
                std::snprintf(line, sizeof(line), "%-10s %4d %4d %4d %6.0f %5d %5d   $%d", agentName(id).c_str(), ps.kills,
                              ps.assists, ps.deaths, double(ps.damage) / per, ps.kills ? ps.hsKills * 100 / ps.kills : 0,
                              ps.mvps, id < 0 ? g.comp.money : g.comp.botMoney[size_t(id)]);
            else if (roundBased)
                std::snprintf(line, sizeof(line), "%-10s %4d %4d %4d %6.0f %5d %5d", agentName(id).c_str(), ps.kills,
                              ps.assists, ps.deaths, double(ps.damage) / per, ps.kills ? ps.hsKills * 100 / ps.kills : 0,
                              ps.mvps);
            else
                std::snprintf(line, sizeof(line), "%-10s %4d %4d %4d %5d %5d", agentName(id).c_str(), ps.kills, ps.assists,
                              ps.deaths, ps.kills ? ps.hsKills * 100 / ps.kills : 0, ps.mvps);
            bool dead = teams && (id < 0 ? g.comp.youDead : !g.dummies[size_t(id)].alive());
            uint32_t col = id < 0 ? 0xFFFFFFFF : enemy(id) ? 0xFF9080FF : teams ? 0x90E0FFFF : 0xC8C8C8FF;
            if (dead) col = (col & 0xFFFFFF00u) | 0x80;
            hud.text(px, ry, line, col);
            ry += rowH;
        }
        if (g.mode == 1 && g.dmOverUntil >= 0) {
            hud.text(px, ry + rowH, "NEXT MATCH STARTS IN A FEW SECONDS", 0xA0A0A0FF);
            if (!g_ratingNote.empty()) hud.text(px, ry + rowH * 2.5f, g_ratingNote, 0xFFD060FF, s * 2);
        }
    }
    // Damage report after you die (bottom-left, like the console in CS).
    if (g.simTime < g.reportUntil) {
        float rx = 16.0f * s, rowH2 = 10.0f * s, top = float(h) - 60.0f * s - rowH2 * float(g.report.size());
        hud.rect(rx - 6 * s, top - 6 * s, 160.0f * s, rowH2 * float(g.report.size()) + 12 * s, 0x15181CC0);
        for (size_t k = 0; k < g.report.size(); ++k)
            hud.text(rx, top + rowH2 * float(k), g.report[k],
                     g.report[k][0] == ' ' ? 0xE0E0E0FFu : 0xFFD060FFu);
    }
    if (g.mapId == 1 && g.mode == 2) {
        int left = int(std::max(0.0, g.rtRoundEnd - g.simTime)), alive = 0;
        for (const Dummy& d : g.dummies) alive += d.alive();
        std::snprintf(buf, sizeof(buf), "BOMB PLANTED %s   0:%02d   BOTS LEFT %d   WON %d  LOST %d",
                      townRetakeSites()[size_t(g.rtSite)].name, left, alive, g.rtWon, g.rtLost);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, left <= 10 ? 0xFF8060FF : 0xFFFFFFFF);
        if (g.bombActive && g.rtResultUntil < 0) {
            if (g.defuseStart >= 0) {  // defuse bar
                float k = float(std::clamp((g.simTime - g.defuseStart) / 5.0, 0.0, 1.0)), bw = 160.0f * s;
                hud.rect(cx - bw / 2, cy + 40.0f * s, bw, 8.0f * s, 0x000000A0);
                hud.rect(cx - bw / 2, cy + 40.0f * s, bw * k, 8.0f * s, 0x60C0FFFF);
                hud.text(cx - hud.textWidth("DEFUSING") / 2, cy + 52.0f * s, "DEFUSING", 0xFFFFFFFF);
            } else if (length2d(g.player.origin - g.bombPos) < 72.0f) {
                hud.text(cx - hud.textWidth("HOLD E TO DEFUSE") / 2, cy + 40.0f * s, "HOLD E TO DEFUSE", 0xFFFFFFFF);
            } else if (alive == 0) {
                hud.text(cx - hud.textWidth("SITE CLEAR - DEFUSE THE BOMB") / 2, cy + 40.0f * s,
                         "SITE CLEAR - DEFUSE THE BOMB", 0x60FF60FF);
            }
        }
        if (g.rtResultUntil >= 0) {
            uint32_t col = g.rtResultWin ? 0x60FF60FF : 0xFF5050FF;
            hud.text(cx - hud.textWidth(g.rtResultText, s * 3) / 2, cy - 90.0f * s, g.rtResultText, col, s * 3);
        }
    }
    if (g.mapId == 1 && g.mode == 3) {
        const Game::Comp& c = g.comp;
        int alive[2] = {0, 0};
        for (size_t i = 0; i < g.dummies.size(); ++i) alive[g.team[i]] += g.dummies[i].alive();
        if (!c.youDead) alive[c.youTeam]++;
        const double clockEnd = c.planted && g.bombActive ? g.bombExplodeAt : c.phaseEnd;
        int left = int(std::max(0.0, clockEnd - g.simTime));
        const char* side = c.youTeam == 0 ? "T" : "CT";
        if (c.phase == 0)
            std::snprintf(buf, sizeof(buf), "YOU %d : %d THEM   BUY TIME 0:%02d   YOU ARE %s   B TO BUY", c.youScore,
                          c.themScore, left, side);
        else
            std::snprintf(buf, sizeof(buf), "YOU %d : %d THEM   %s%d:%02d   %d V %d   YOU ARE %s", c.youScore, c.themScore,
                          c.planted ? "BOMB " : "", left / 60, left % 60, alive[c.youTeam], alive[1 - c.youTeam], side);
        hud.text(cx - hud.textWidth(buf) / 2, 32.0f * s, buf, c.planted ? 0xFF8060FF : 0xFFFFFFFF);
        if (c.phase >= 2) {
            uint32_t col = c.resultWin ? 0x60FF60FF : 0xFF5050FF;
            hud.text(cx - hud.textWidth(c.resultText, s * 3) / 2, cy - 90.0f * s, c.resultText, col, s * 3);
            if (c.phase == 3) {
                const char* m = c.youScore > c.themScore ? "YOU WON THE MATCH" : c.youScore < c.themScore ? "YOU LOST THE MATCH" : "DRAW";
                hud.text(cx - hud.textWidth(m, s * 2) / 2, cy - 60.0f * s, m, 0xFFD060FF, s * 2);
                if (!g_ratingNote.empty())
                    hud.text(cx - hud.textWidth(g_ratingNote) / 2, cy - 36.0f * s, g_ratingNote, 0xFFFFFFFF);
            }
        }
        auto bar = [&](const char* label, float k) {
            float bw = 160.0f * s;
            hud.rect(cx - bw / 2, cy + 40.0f * s, bw, 8.0f * s, 0x000000A0);
            hud.rect(cx - bw / 2, cy + 40.0f * s, bw * std::clamp(k, 0.0f, 1.0f), 8.0f * s, 0x60C0FFFF);
            hud.text(cx - hud.textWidth(label) / 2, cy + 52.0f * s, label, 0xFFFFFFFF);
        };
        if (c.planter == -1) bar("PLANTING", float((g.simTime - c.plantStart) / kCompPlantTime));
        if (g.defuseStart >= 0 && c.planted) bar("DEFUSING", float((g.simTime - g.defuseStart) / (c.kit ? 5.0 : 10.0)));
        if (c.carrier == -1 && c.planter != -1 && c.phase == 1)
            hud.text(cx - hud.textWidth("YOU HAVE THE BOMB - HOLD E ON A SITE TO PLANT") / 2, cy + 64.0f * s,
                     "YOU HAVE THE BOMB - HOLD E ON A SITE TO PLANT", 0xFFD060C0);
        if (c.youDead && c.phase == 1) {
            char spec[96];
            if (g.spec >= 0 && size_t(g.spec) < g.dummies.size() && g.dummies[size_t(g.spec)].hp > 1000.0f)  // (online: not known here)
                std::snprintf(spec, sizeof(spec), "SPECTATING %s      MOUSE 1/2: NEXT   SPACE: FLY FREE", agentName(g.spec).c_str());
            else if (g.spec >= 0 && size_t(g.spec) < g.dummies.size())
                std::snprintf(spec, sizeof(spec), "SPECTATING %s  -  %d HP      MOUSE 1/2: NEXT   SPACE: FLY FREE", agentName(g.spec).c_str(),
                              int(std::max(0.0f, g.dummies[size_t(g.spec)].hp)));
            else
                std::snprintf(spec, sizeof(spec), "DEAD - FLYING FREE UNTIL THE NEXT ROUND      SPACE: WATCH A TEAMMATE");
            const float tw = hud.textWidth(spec);
            hud.rect(cx - tw / 2 - 8.0f * s, float(h) - 68.0f * s, tw + 16.0f * s, 16.0f * s, 0x101216C0);
            hud.text(cx - tw / 2, float(h) - 64.0f * s, spec, 0xFFFFFFFF);
        }
        char kit[96];
        std::snprintf(kit, sizeof(kit), "ARMOR %d%s   $%d%s%s", int(c.armor), c.helmet ? "+H" : "", c.money,
                      c.kit ? "   KIT" : "", c.carrier == -1 ? "   C4" : "");
        hud.text(16.0f * s, float(h) - 36.0f * s, kit, 0x80FF80FF);
    }
    if (g.buyMenu) drawBuyWheel(hud, g, w, h, s);
    if (g.kzState == 2 || g.kzLast >= 0) {
        if (g.kzState == 2) std::snprintf(buf, sizeof(buf), "KZ %.2f", g.simTime - g.kzStart);
        else std::snprintf(buf, sizeof(buf), "KZ LAST %.3f   BEST %.3f", g.kzLast, g.kzBest);
        hud.text(cx - hud.textWidth(buf, s * 2) / 2, 12.0f * s, buf, g.kzState == 2 ? 0xFFFFFFFF : 0x80FF80FF, s * 2);
    }
    if (g.simTime < 15.0) { hud.text(x, y, "ESC: MENU, SETTINGS AND CONTROLS", 0xFFD060D0); y += lh; }
    if (g.mapId == 0) {
        hud.text(x, y, "LEFT: CRATES + STAIRS + DOOR   AHEAD: RANGE   RIGHT: SPRAY WALL", 0xE0E0E0B0);
        hud.text(x, y + lh, "KZ COURSE: GREEN PAD BEHIND THE SPRAY WALL - HOP THE BLUE PADS", 0xE0E0E0B0);
    }

    // Top-right: kill feed (6 s), then the hit log under it.
    float ry = 10.0f * s;
    for (const Game::FeedEntry& e : g.feed) {
        if (g.simTime - e.time > 6.0) continue;
        float tw = hud.textWidth(e.text);
        hud.rect(float(w) - tw - 18.0f * s, ry - 2.0f * s, tw + 12.0f * s, lh, 0x101216B0);
        hud.text(float(w) - tw - 12.0f * s, ry, e.text, (e.color << 8) | 0xFF);
        ry += lh + 2.0f * s;
    }
    ry += lh * 0.5f;
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
    } else if (g.weapon == &g.guns[kWGrenade]) {
        const char* names[Game::kNadeTypes] = {"SMOKE", "FLASHBANG", "HE GRENADE", "MOLOTOV"};
        std::snprintf(buf, sizeof(buf), "%s  (4: NEXT)", names[g.nadeType]);
    } else {
        std::snprintf(buf, sizeof(buf), "%s", ws.def->name);
    }
    hud.text(float(w) - hud.textWidth(buf, s * 2) - 16.0f * s, float(h) - 24.0f * s, buf, 0xFFFFFFFF, s * 2);

    // Flashed: white over everything, holding full for a while, then fading.
    if (g.simTime < g.flashEnd) {
        float k = g.simTime < g.flashFull ? 1.0f : float((g.flashEnd - g.simTime) / std::max(0.01, g.flashEnd - g.flashFull));
        hud.rect(0, 0, float(w), float(h), 0xFFFFFF00u | uint32_t(255.0f * std::clamp(k, 0.0f, 1.0f)));
    }

    if (g_menu.screen != kMenuNone) drawMenu(hud, cfg, g.mode, w, h, s);
    drawMenuExtras(hud, cfg, w, h, s);
    if (g_menu.screen != kMenuNone && g.mapId == 1 && g.online && g.net.isHost()) {
        // Hosting, with the menu open: the address to give friends, once the router has (or hasn't) opened the
        // port. Over the menu, at the top (never during play).
        const PortStatus ps = g.ports.status();
        const std::string port = g.netPort == kNetDefaultPort ? "" : ":" + std::to_string(g.netPort);
        std::string a, b;
        if (ps.state == PortStatus::Working) {
            a = "OPENING PORT " + std::to_string(g.netPort) + " ON YOUR ROUTER...";
        } else if (ps.state == PortStatus::Opened && ps.note.empty()) {
            a = "FRIENDS JOIN: " + ps.publicIp + port + "     SAME HOUSE: " + ps.localIp + port;
        } else {
            a = "SAME HOUSE: " + (ps.localIp.empty() ? std::string("YOUR PC'S IP") : ps.localIp) + port +
                "     OVER THE INTERNET: ZEROTIER, OR FORWARD UDP " + std::to_string(g.netPort);
            b = ps.note;
        }
        auto line = [&](const std::string& t, float y, uint32_t col) {
            const float tw = hud.textWidth(t);
            hud.rect(cx - tw / 2 - 6.0f * s, y - 2.0f * s, tw + 12.0f * s, 12.0f * s, 0x101216E0);
            hud.text(cx - tw / 2, y, t, col);
        };
        line(a, 8.0f * s, 0xFFD060FF);
        if (!b.empty()) line(b, 21.0f * s, 0xFFA070FF);
    }
}


int fatal(const std::string& msg, SDL_Window* window, bool showBox) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    if (showBox) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Crisp", msg.c_str(), window);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    g_compStartSide = opt.startCT ? 1 : 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--dump-sounds") return Audio::dumpWavs(argv[i + 1]) ? 0 : 1;
        else if (std::string(argv[i]) == "--dump-spatial") {  // a footstep from each direction, as play3D hears it
            const char* base = SDL_GetBasePath();
            return Audio::dumpSpatial(argv[i + 1], std::string(base ? base : "") + "assets/sounds") ? 0 : 1;
        }
        else if (std::string(argv[i]) == "--dump-played-sounds") {  // with the recordings in assets/sounds
            const char* base = SDL_GetBasePath();
            return Audio::dumpWavs(argv[i + 1], std::string(base ? base : "") + "assets/sounds") ? 0 : 1;
        }
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
    SDL_Window* window = SDL_CreateWindow("Crisp v" CRISP_VERSION, winW, winH, flags);
    if (!window && msaa > 0) {  // the driver can't do it: carry on without anti-aliasing
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Crisp v" CRISP_VERSION, winW, winH, flags);
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
    {  // surface textures from assets/ (without them the world keeps its procedural surfaces)
        const char* basePath = SDL_GetBasePath();
        const int layers = renderer.loadTextures(std::string(basePath ? basePath : "") + "assets/textures");
        std::fprintf(stderr, "textures: %s\n", layers ? "loaded" : "not found, using procedural surfaces");
    }

    const bool bench = opt.benchSeconds > 0;
    const bool automated = !opt.screenshotPath.empty() || bench;
    if (automated) g_compLog = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "comp_log.txt";
    // Benchmark: where each frame's time goes (CPU sections + GPU via glFinish), written to bench.txt.
    struct BenchStats {
        double wait = 0, sim = 0, scene = 0, draw = 0, hud = 0, gpu = 0, swap = 0, elapsed = 0;
        std::vector<float> frames;
        size_t dynBoxes = 0;
    } benchStats;
    Game g;
    resetGame(g, opt);
    Audio audio;
    // (a benchmark plays the sounds too: the mixer's cost goes into bench.txt)
    if ((!automated || bench) && SDL_InitSubSystem(SDL_INIT_AUDIO) && audio.init(std::clamp(cfg.volume, 0.0f, 1.0f), std::string(base ? base : "") + "assets/sounds")) {
        g.audio = &audio;
        std::fprintf(stderr, "sounds: %d replaced by recordings from assets, the rest synthesized\n", audio.loadedFromAssets());
        // Sounds through walls are muffled: two rays from your head to the sound (low and high on it); each one a
        // wall stops counts half.
        audio.setOcclusion(
            [](const void* ctx, const Vec3& from, const Vec3& to) {
                const World& w = static_cast<const Game*>(ctx)->world;
                float occ = 0;
                for (float up : {12.0f, 50.0f})
                    if (w.traceRay(from, to + Vec3{0, 0, up}).fraction < 0.98f) occ += 0.5f;
                return occ;
            },
            &g);
    }
    else if (!automated)
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    applyConfig(g, cfg);
    // Your skins and cases (inventory.txt next to the game).
    g_invPath = std::string(base ? base : "") + "inventory.txt";
    if (!automated) {
        g_careerPath = std::string(base ? base : "") + "stats.txt";
        g_career.load(g_careerPath);
    } else if (!opt.careerFile.empty()) {
        g_career.load(opt.careerFile);
    }
    g_inv.load(g_invPath);
    g_caseKills = std::clamp(cfg.case_kills, 1, 1000);
    g_allSkins = cfg.all_skins != 0;
    g_caseRng ^= uint32_t(SDL_GetPerformanceCounter()) | 1u;
    for (int k = 0; k < opt.giveCases; ++k) g_inv.addKill(1);  // (tests: --give-cases N)
    validateEquips(g);
    g.mode = cfg.mode >= 1 && cfg.mode <= 4 ? cfg.mode : 0;
    renderer.setDepthPrepass(cfg.depth_prepass != 0);
    setDustScale(float(cfg.dust_scale) / 100.0f);
    setTownMap(cfg.map == 2 ? 1 : 0);
    loadMap(g, renderer, cfg.map >= 1 || g.mode != 0 ? 1 : 0);
    // Settings changed (menu, config reload): a new Dust size rebuilds the map right away.
    auto settingsChanged = [&]() {
        applyConfig(g, cfg);
        g_caseKills = std::clamp(cfg.case_kills, 1, 1000);
        g_allSkins = cfg.all_skins != 0;
        if (setDustScale(float(cfg.dust_scale) / 100.0f) && g.mapId == 1) loadMap(g, renderer, 1);
    };
    if (opt.spawnOverride) {
        float floorZ = g.mapId == 1 ? townGrid().floorAt(opt.spawnX, opt.spawnY) : 0.0f;
        g.spawn = {opt.spawnX, opt.spawnY, floorZ > MapGrid::kNoFloor ? floorZ : 0.0f};
        g.spawnYaw = opt.spawnYaw;
        resetPosition(g);
    }

    bool paused = false, running = true;
    for (int k = 0; k < 6; ++k)  // match the crosshair colour preset to the loaded config
        if (kCrosshairColors[k] == (uint32_t(cfg.crosshair_r) << 16 | uint32_t(cfg.crosshair_g) << 8 | uint32_t(cfg.crosshair_b)))
            g_crosshairPreset = k;
    // Menus: any open screen pauses the game and frees the mouse.
    auto setMenu = [&](int screen) {
        if (screen == kMenuPlay) {  // the play screen starts from what's running now
            g_gameMenu.map = g.mapId == 1 ? 1 + townMap() : 0;
            g_gameMenu.mode = g.online ? 5 : g.mode;
            g_gameMenu.bots = g.botsFire;
            g_gameMenu.drill = g.drill;
            g_gameMenu.route = g.pf.route;
        }
        if (screen == kMenuInventory) refreshChoices();
        g_menu.screen = screen;
        g_menu.sel = 0;
        paused = screen != kMenuNone;
        if (!automated) SDL_SetWindowRelativeMouseMode(window, !paused);
        g.fireHeld = g.zoomHeld = false;
        g.nadeHold = 0;  // a grenade you were holding stays in your hand
        g.hudDirty = true;
    };
    auto leaveOnline = [&]() {  // stop hosting / leave: the connection, the router's port, the names
        g.net.stop();
        g.ports.close();
        g.netJoining = false;
        g.online = false;
        for (std::string& n : g_playerNames) n.clear();
    };
    // "host:port" -> host, port (no colon: the port stays as it was).
    auto splitAddress = [](std::string& address, uint16_t& port) {
        const size_t colon = address.rfind(':');
        if (colon == std::string::npos) return;
        const int p = std::atoi(address.c_str() + colon + 1);
        if (p >= 1 && p <= 65535) port = uint16_t(p);
        address.resize(colon);
    };
    // What a text field can hold: letters, digits, . - _ and : (for address:port).
    auto typedChar = [](SDL_Keycode k, SDL_Keymod mod) -> char {
        const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
        if ((k >= SDLK_0 && k <= SDLK_9) || (k >= SDLK_A && k <= SDLK_Z) || k == SDLK_PERIOD) return char(k);
        if (k == SDLK_MINUS) return shift ? '_' : '-';
        if (k == SDLK_SEMICOLON && shift) return ':';
        return 0;
    };
    auto goOnline = [&](int game) {  // hosting, or just connected: online deathmatch (0) or competitive (1) on the town map
        g.online = true;
        g.mode = game == 1 ? 3 : 5;
        g_netTeamsTogether = cfg.net_teams;
        loadMap(g, renderer, 1);
        g_menu.root = kMenuPause;
        setMenu(kMenuNone);
        if (opt.spawnOverride) {  // (tests: start where told)
            g.spawn = {opt.spawnX, opt.spawnY, townGrid().floorAt(opt.spawnX, opt.spawnY)};
            g.spawnYaw = opt.spawnYaw;
            resetPosition(g);
        }
    };
    auto menuBack = [&]() {
        const int from = g_menu.screen;
        if (from == kMenuCase) {
            setMenu(kMenuInventory);
        } else if (from >= kMenuMouse && from <= kMenuGameplay) {
            setMenu(kMenuSettings);
            g_menu.sel = from - kMenuMouse;  // back on the page you came from
        } else if (from == kMenuPause) {
            setMenu(kMenuNone);
        } else if (from != kMenuMain) {
            setMenu(g_menu.root);
        }
    };
    // START on the play screen: the chosen mode and map, from scratch.
    auto startFromMenu = [&]() {
        const GameMenu& m = g_gameMenu;
        leaveOnline();  // (if you were in an online game)
        g.mode = m.mode;
        g.pf.route = m.route;
        const int map = m.mode != 0 && m.map == 0 ? 1 : m.map;
        setTownMap(map == 2 ? 1 : 0);
        loadMap(g, renderer, map >= 1 ? 1 : 0);
        if (g.mode == 4) g.botsFire = cfg.prefire_bots_shoot != 0;
        if (g.mode == 0) {
            g.botsFire = m.bots != 0;
            if (g.mapId == 0 && m.drill) setDrill(g, true);
        }
        cfg.map = map;
        cfg.mode = g.mode;
        saveConfig(cfgPath, cfg);
        g_menu.root = kMenuPause;
        setMenu(kMenuNone);
    };
    // Row `row` was pressed (Enter, click: press = true) or nudged (arrows, wheel, right click).
    // Settings change by `dir` steps either way and save straight away; buttons only react to presses.
    auto menuUse = [&](int row, int dir, bool big, bool press) {
        const std::vector<MenuItem> rows = menuRows(g_menu.screen, cfg, g.mode);
        if (row < 0 || row >= int(rows.size())) return;
        const MenuItem& it = rows[size_t(row)];
        g.hudDirty = true;
        if (adjustMenu(cfg, it, dir, big)) {
            sound(g, Sfx::UiClick, 0.35f, 0.0f, 1.15f);
            settingsChanged();
            saveConfig(cfgPath, cfg);
            if (g_menu.screen == kMenuInventory) {  // a skin picked (or every skin unlocked / locked again)
                if (it.i == &cfg.all_skins) {
                    validateEquips(g);
                    refreshChoices();
                } else {
                    choicesChanged(g);
                }
            }
            return;
        }
        if (!press) return;
        sound(g, Sfx::UiClick, 0.5f);
        switch (it.action) {
            case kActResume: setMenu(kMenuNone); break;
            case kActReplay:
                startReplayViewer(g);
                if (g.rv.on) setMenu(kMenuNone);
                break;
            case kActStart: startFromMenu(); break;
            case kActReset:
                if (g.mode == 4 && g.mapId == 1) startPrefire(g);
                else resetPosition(g);
                setMenu(kMenuNone);
                break;
            case kActReload:
                cfg = loadConfig(cfgPath);
                settingsChanged();
                pushHitLog(g, "CONFIG RELOADED", 0x80ff80);
                break;
            case kActQuit: running = false; break;
            case kActOpenCase:
                if (startCase()) {
                    setMenu(kMenuCase);
                } else {
                    char msg[80];
                    std::snprintf(msg, sizeof(msg), "NO CASES YET: %d MORE KILLS FOR THE NEXT ONE", g_caseKills - g_inv.progress);
                    pushHitLog(g, msg, 0xffd060);
                }
                break;
            case kActSkipCase: g_case.start = g_uiTime - kCaseSpin; break;
            case kActEquipNew:
                if (g_case.item >= 0 && size_t(g_case.item) < g_inv.items.size()) {
                    const Item& it2 = g_inv.items[size_t(g_case.item)];
                    g_inv.equip[allSkins()[size_t(it2.skin)].weapon] = {it2.skin, it2.wear};
                    applySkins(g);
                    saveInventory();
                    refreshChoices();
                    pushHitLog(g, "EQUIPPED " + allSkins()[size_t(it2.skin)].name, rarityColor(allSkins()[size_t(it2.skin)].rarity));
                }
                break;

            case kActBack: menuBack(); break;
            case kActMainMenu:
                if (g.online) {  // leaving an online game: back to an offline map behind the menu
                    leaveOnline();
                    g.mode = 0;
                    loadMap(g, renderer, 0);
                    g.botsFire = false;  // (the Lab's practice targets: they don't shoot unless you ask)
                }
                leaveOnline();
                g_menu.root = kMenuMain;
                setMenu(kMenuMain);
                break;
            case kActHost: {
                std::string err;
                const uint16_t port = uint16_t(std::clamp(cfg.net_port, 1024, 65535));
                g.net.setName(cfg.player_name);
                if (g.net.host(port, float(cfg.dust_scale) / 100.0f, uint8_t((cfg.net_game == 1 ? 1 : 0) | townMap() << 1), err)) {
                    g.netPort = port;
                    g.ports.open(port);  // ask the router, in the background
                    g_playerNames[0] = Net::cleanName(cfg.player_name);
                    goOnline(cfg.net_game == 1 ? 1 : 0);
                    char msg[64];
                    std::snprintf(msg, sizeof(msg), "HOSTING ON PORT %d", int(port));
                    pushHitLog(g, msg, 0x80ff80);
                } else {
                    pushHitLog(g, err, 0xff6060);
                }
                break;
            }
            case kActJoin: {
                std::string err;
                g.net.setName(cfg.player_name);
                std::string address = cfg.net_address;
                uint16_t port = uint16_t(std::clamp(cfg.net_port, 1024, 65535));
                splitAddress(address, port);
                if (g.net.join(address, port, err)) {
                    g.netJoining = true;
                    setMenu(kMenuNone);
                } else {
                    pushHitLog(g, err, 0xff6060);
                }
                break;
            }
            default: if (it.action >= kActGoto) setMenu(it.action - kActGoto); break;
        }
    };

    int pixW = 0, pixH = 0;
    SDL_GetWindowSizeInPixels(window, &pixW, &pixH);
    // The menu row under the mouse (window coordinates), or -1.
    auto menuRowUnder = [&](float mx, float my) {
        const float density = SDL_GetWindowPixelDensity(window);
        const int hs = hudScale(cfg, pixH);
        const size_t n = menuRows(g_menu.screen, cfg, g.mode).size();
        return menuRowAt(menuLayout(g_menu.screen, n, pixW, pixH, hs), n, hs, mx * density, my * density);
    };
    g_menu.root = kMenuMain;
    setMenu(automated ? kMenuNone : kMenuMain);  // launch on the main menu

    // ---- Online ----
    std::vector<NetEvent> netEvents;
    auto pumpNet = [&]() {
        if (!g.net.active()) return;
        netEvents.clear();
        g.net.poll(netEvents);
        for (const NetEvent& ev : netEvents) {
            const bool comp = g.mode == 3;
            const bool inGame = g.online && g.mapId == 1 && ev.from < g.dummies.size() &&
                                g.dummies.size() >= size_t(comp ? kNetSlots : kNetMaxPlayers);
            switch (ev.type) {
                case NetEvent::Connected:
                    g.netJoining = false;
                    if (int(std::lround(ev.townScale * 100.0f)) != cfg.dust_scale) {  // the host's map size
                        cfg.dust_scale = int(std::lround(ev.townScale * 100.0f));
                        setDustScale(ev.townScale);
                    }
                    setTownMap(ev.game >> 1);  // the host's map
                    goOnline(ev.game & 1);
                    pushHitLog(g, (ev.game & 1) == 1 ? "CONNECTED - YOU'RE IN FROM THE NEXT ROUND" : "CONNECTED", 0x80ff80);
                    if (automated)
                        std::fprintf(stderr, "net: connected as player %d (%s on %s)\n", g.net.myId() + 1,
                                     (ev.game & 1) ? "competitive" : "deathmatch", townMapName(ev.game >> 1));
                    break;
                case NetEvent::Failed:
                    g.netJoining = false;
                    pushHitLog(g, ev.text.empty() ? "COULDN'T CONNECT TO " + cfg.net_address : ev.text, 0xff6060);
                    if (automated) std::fprintf(stderr, "net: couldn't connect %s\n", ev.text.c_str());
                    break;
                case NetEvent::Joined:  // (announced once their name arrives)
                    if (automated) std::fprintf(stderr, "net: player %d joined\n", ev.from + 1);
                    if (netHost(g) && comp && g.mapId == 1) {  // competitive: who's playing now (they're in next round)
                        NetRound r;
                        r.kind = 2;
                        for (int k = 0; k < kNetSlots; ++k) {
                            r.team[k] = g.netTeam[k] < 0 ? 255 : uint8_t(g.netTeam[k]);
                            const bool alive = k == 0 ? !g.comp.youDead : size_t(k) < g.dummies.size() && g.dummies[size_t(k)].alive();
                            if (alive && g.netTeam[k] >= 0) r.alive |= 1u << k;
                        }
                        r.sideA = uint8_t(g.comp.youTeam);
                        r.nameOffset = uint8_t(g_botNameOffset);
                        g.net.sendRound(r, ev.from);
                    }
                    break;
                case NetEvent::Name:
                    if (ev.from >= kNetMaxPlayers) break;
                    if (g_playerNames[ev.from].empty() && ev.from != g.net.myId())
                        pushHitLog(g, ev.text + " IS HERE", 0x80ff80);
                    g_playerNames[ev.from] = ev.text;
                    if (automated) std::fprintf(stderr, "net: player %d is %s\n", ev.from + 1, ev.text.c_str());
                    g.hudDirty = true;
                    break;
                case NetEvent::Left:
                    if (ev.from == 0 && !g.net.isHost()) {  // the host is gone: back to an offline map
                        const bool wasOnline = g.online;
                        leaveOnline();
                        pushHitLog(g, "THE HOST LEFT", 0xff6060);
                        if (wasOnline) { g.mode = 0; loadMap(g, renderer, 0); g.botsFire = false; }
                    } else if (ev.from < kNetMaxPlayers) {
                        g.remotes[ev.from].present = false;
                        if (ev.from < g.dummies.size() && ev.from != g.net.myId()) {  // gone: out of the round too
                            g.dummies[ev.from].respawnLeft = 1e9f;
                            g.dummies[ev.from].deadFor = 10.0f;
                        }
                        if (netHost(g)) g.netTeam[ev.from] = -1;
                        pushHitLog(g, agentName(ev.from) + " LEFT", 0xffd060);
                        g_playerNames[ev.from].clear();
                    }
                    break;
                case NetEvent::State: {
                    if (!inGame || ev.from == g.net.myId() || ev.from >= kNetMaxPlayers) break;
                    Game::Remote& rm = g.remotes[ev.from];
                    if (!rm.present) { rm = Game::Remote{}; rm.present = true; }
                    rm.snaps[ev.state.tick % 32] = ev.state;
                    rm.latest = std::max(rm.latest, ev.state.tick);
                    break;
                }
                case NetEvent::Bots:  // the host's bots (competitive): played back like players
                    if (!netClient(g) || !comp || g.mapId != 1 || g.dummies.size() < size_t(kNetSlots)) break;
                    for (int k = 0; k < ev.botCount; ++k) {
                        const NetBot& b = ev.bots[k];
                        Game::Remote& rm = g.remotes[b.id];
                        if (!rm.present) { rm = Game::Remote{}; rm.present = true; }
                        NetState& st = rm.snaps[ev.tick % 32];
                        st = NetState{};
                        st.id = b.id;
                        st.tick = ev.tick;
                        st.pos = b.pos;
                        st.yaw = b.yaw;
                        st.pitch = b.pitch;
                        st.flags = b.flags;
                        st.weapon = b.weapon;
                        rm.latest = std::max(rm.latest, ev.tick);
                    }
                    break;
                case NetEvent::Match:
                    if (netClient(g) && comp && g.mapId == 1 && g.dummies.size() >= size_t(kNetSlots)) netCompMatch(g, ev.match);
                    break;
                case NetEvent::Round:
                    if (!netClient(g) || !comp || g.mapId != 1 || g.dummies.size() < size_t(kNetSlots)) break;
                    if (ev.round.kind == 0) netCompRoundStart(g, ev.round);
                    else if (ev.round.kind == 1) netCompRoundEnd(g, ev.round);
                    else netApplyRoster(g, ev.round);
                    if (automated) std::fprintf(stderr, "net: round %s\n", ev.round.kind == 0 ? "start" : ev.round.kind == 1 ? "end" : "roster");
                    break;
                case NetEvent::Plant:  // a player planted (competitive, you're the host)
                    if (netHost(g) && comp && g.mapId == 1 && g.comp.phase == 1 && !g.comp.planted && g.comp.carrier == int(ev.from))
                        compPlanted(g, ev.a);
                    break;
                case NetEvent::Defused:
                    if (netHost(g) && comp && g.mapId == 1 && g.comp.planted && g.bombActive && g.comp.phase == 1 &&
                        ev.from < g.team.size() && g.team[ev.from] == 1 && g.dummies[ev.from].alive()) {
                        g.bombActive = false;
                        endCompRound(g, 1, "THE BOMB HAS BEEN DEFUSED", true);
                    }
                    break;
                case NetEvent::Fire: {
                    if (!inGame) break;
                    if (g.audio) {
                        gunshot3D(g, ev.weapon, ev.a, g.lastRenderEye, float(g.viewYaw));
                        // Close past your head: you hear it go by.
                        const Vec3 eye = g.player.origin + Vec3{0, 0, eyeHeight(g.player)}, dir = normalize(ev.b - ev.a);
                        const float t = dot(eye - ev.a, dir);
                        if (g.deadUntil < 0 && t > 60.0f && t < length(ev.b - ev.a) && length(ev.a + dir * t - eye) < 56.0f)
                            g.audio->play3D(Sfx::Whiz, ev.a + dir * t, eye, float(g.viewYaw), 300.0f, 0.8f);
                    }
                    g.fx.tracer(ev.a, ev.b);
                    muzzleLight(g, ev.a, suppressedGun(ev.weapon) ? 0.2f : 0.9f);
                    if (netHost(g) && comp && ev.from < g.team.size() && !isBot(g, ev.from))
                        makeNoise(g, ev.a, 2200.0f, g.team[ev.from]);  // your bots hear the other players' shots
                    break;
                }
                case NetEvent::Nade:  // someone threw: it flies here exactly as it does for them
                    if (!inGame) break;
                    g.nades.push_back({ev.a, ev.b, ev.weapon, 0, netToLocal(g, ev.from)});
                    if (g.audio) g.audio->play3D(Sfx::Draw, ev.a, g.lastRenderEye, float(g.viewYaw), 1200.0f, 0.5f, 1.3f);
                    if (automated) std::fprintf(stderr, "net: grenade %d from %d\n", ev.weapon, ev.from + 1);
                    break;
                case NetEvent::Hit: {  // their game says their bullet hit you (or, hosting, one of your bots): it counts
                    if (!inGame) break;
                    const char* weapon = netWeaponName(ev.weapon);
                    const HitGroup grp = HitGroup(std::min<int>(ev.group, kLegs));
                    if (ev.other >= kNetMaxPlayers) {  // your bot (you're the host)
                        if (!netHost(g) || ev.other >= g.dummies.size()) break;
                        Dummy& d = g.dummies[ev.other];
                        if (!d.alive()) break;
                        const float applied = std::min(ev.damage, std::max(0.0f, d.hp));
                        d.hp -= ev.damage;
                        d.flash[grp] = 0.15f;
                        d.hitDir = d.pos - g.dummies[ev.from].pos;
                        const bool kill = d.hp <= 0;
                        if (kill && grp == kHead) knockHelmet(g, ev.other, d.hitDir);
                        recordDamage(g, ev.from, int(ev.other), applied, grp == kHead, weapon, false, kill);
                        if (kill) {
                            d.respawnLeft = 1.0f;
                            botDied(g, ev.other);
                        } else {
                            g.bots[ev.other].alertUntil = g.simTime + 2.0;
                            g.bots[ev.other].lastSeen = g.dummies[ev.from].pos;
                        }
                        break;
                    }
                    if (automated) std::fprintf(stderr, "net: hit by %d for %.0f%s\n", ev.from + 1, double(ev.damage), ev.head ? " (head)" : "");
                    float dmg = ev.damage;
                    if (comp) dmg = armoredDamage(dmg, grp, g.comp.armor, g.comp.helmet,
                                                  weaponDef(ev.weapon < kWeaponCount ? ev.weapon : kWRifle).armorRatio);  // your armor
                    hurtPlayer(g, ev.from, dmg, ev.head, weapon);
                    break;
                }
                case NetEvent::Death: {  // someone died: the kill feed, scores; deathmatch: your kill heals and reloads you
                    if (!inGame) break;
                    const int victim = netToLocal(g, ev.from), killer = netToLocal(g, ev.other);
                    if (victim < 0) break;  // (you said so yourself)
                    Dummy& d = g.dummies[size_t(victim)];
                    if (d.alive()) { d.respawnLeft = 1e9f; d.deadFor = 0; }
                    if (automated) std::fprintf(stderr, "net: %d killed by %d\n", ev.from + 1, ev.other + 1);
                    const char* weapon = netWeaponName(ev.weapon);
                    if (killer == -1) {
                        recordDamage(g, -1, victim, 0, ev.head, weapon, false, true);
                        if (g.mode == 5 && g.deadUntil < 0) {
                            g.hp = std::min(100.0f, g.hp + 40.0f);
                            if (g.weapon->def->canFire) g.weapon->ammo = std::min(g.weapon->ammo + 10, g.weapon->def->magSize);
                        }
                        sound(g, Sfx::HitMarker, 1.0f, 0.0f, 0.8f);
                        g.hitMarkerStart = g.simTime;  // the kill marker (your game didn't know it killed)
                        g.hitMarkerUntil = g.simTime + 0.22;
                        g.hitMarkerHead = ev.head;
                        g.hitMarkerKill = true;
                    } else if (killer != victim && size_t(killer) < g.dummies.size()) {
                        recordDamage(g, killer, victim, 0, ev.head, weapon, false, true);
                    } else {  // their own grenade
                        statsOf(g, victim).deaths++;
                        g.feed.push_back({agentName(victim) + "  [" + weapon + "]", 0xB4B4B4u, g.simTime});
                        while (g.feed.size() > 5) g.feed.pop_front();
                    }
                    g.hudDirty = true;
                    break;
                }
            }
        }
    };
    // The text field under the menu cursor, if it's on one (the join address).
    auto menuField = [&]() -> std::string* {
        if (!paused) return nullptr;
        std::vector<MenuItem> rows = menuRows(g_menu.screen, cfg, g.mode);
        return g_menu.sel >= 0 && g_menu.sel < int(rows.size()) ? rows[size_t(g_menu.sel)].text : nullptr;
    };

    if (opt.netHost) {  // tests: --host / --join ADDR
        std::string netErr;
        g.net.setName(cfg.player_name);
        g.netPort = uint16_t(std::clamp(cfg.net_port, 1024, 65535));
        if (g.net.host(g.netPort, float(cfg.dust_scale) / 100.0f, uint8_t((cfg.net_game == 1 ? 1 : 0) | townMap() << 1), netErr)) {
            g.ports.open(g.netPort);  // like the menu's HOST: ask the router
            g_playerNames[0] = Net::cleanName(cfg.player_name);
            goOnline(cfg.net_game == 1 ? 1 : 0);
        }
        else std::fprintf(stderr, "net: %s\n", netErr.c_str());
    } else if (!opt.netJoin.empty()) {
        std::string netErr;
        cfg.net_address = opt.netJoin;
        g.net.setName(cfg.player_name);
        std::string address = opt.netJoin;
        uint16_t port = uint16_t(std::clamp(cfg.net_port, 1024, 65535));
        splitAddress(address, port);
        if (g.net.join(address, port, netErr)) g.netJoining = true;
        else std::fprintf(stderr, "net: %s\n", netErr.c_str());
    }

    const double freq = double(SDL_GetPerformanceFrequency());
    uint64_t last = SDL_GetPerformanceCounter();
    double tickAcc = 0, lastHudBuild = -1;
    FrameStats stats;
    HudBatch hud;
    std::vector<BoxInstance> dynamicBoxes;
    std::vector<Vec3> nadePath;  // grenade preview, reused every frame
    int frame = 0;
    double hitMarkerShownUntil = 0;
    std::vector<ModelDraw> modelDraws;
    std::vector<ModelDraw> deadDraws;
    ReplayFrame replayNow;  // a replay: everyone at the moment playing (reused every frame)  // bodies falling over (each one model, so it can tilt)

    if (g.audio) g.audio->loadMusic(std::string(base ? base : "") + "assets/music/menu.ogg");  // (on a thread)
    GLsync frameFence = nullptr;  // low_latency: the last frame's GPU work
    while (running) {
        uint64_t frameStart = SDL_GetPerformanceCounter();
        double dt = double(frameStart - last) / freq;
        last = frameStart;
        // Low-latency mode (like NVIDIA Reflex / AMD Anti-Lag, by hand): when the GPU is the bottleneck the driver
        // queues up frames, and each queued frame is input you read that much earlier. Waiting here until the
        // GPU has finished the last frame keeps that queue at one, so the mouse and clicks below are as fresh
        // as they can be when this frame reaches the screen.
        if (frameFence) {
            glClientWaitSync(frameFence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);  // (at most 100 ms)
            glDeleteSync(frameFence);
            frameFence = nullptr;
        }
        const uint64_t tFence = SDL_GetPerformanceCounter();
        if (automated && !bench) dt = 1.0 / 240.0;  // deterministic steps for screenshots/tests
        g_uiTime += dt;
        if (g_menu.screen == kMenuCase) caseTick(g);
        // The soundtrack plays in the main menu and its pages (not the pause menu), and fades out in a game.
        if (g.audio)
            g.audio->setMusic(g_menu.screen != kMenuNone && g_menu.root == kMenuMain ? std::clamp(cfg.music_volume, 0.0f, 1.0f) * 0.7f : 0.0f);
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
                    if (!automated && !paused) setMenu(kMenuPause);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    pixW = e.window.data1;
                    pixH = e.window.data2;
                    g.hudDirty = true;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (!paused && g.rv.on) {  // a replay: the mouse turns the free camera, nothing else
                        if (g.rv.pov < 0 && !g.rv.killcam) {
                            g.rv.camYaw = wrapDeg(g.rv.camYaw - float(e.motion.xrel) * cfg.sensitivity * cfg.m_yaw);
                            g.rv.camPitch = std::clamp(g.rv.camPitch + float(e.motion.yrel) * cfg.sensitivity * cfg.m_pitch, -89.0f, 89.0f);
                        }
                    } else if (!paused && g.buyMenu) {
                        const float density = SDL_GetWindowPixelDensity(window);
                        g.buyMouseX = e.motion.x * density;
                        g.buyMouseY = e.motion.y * density;
                        g.hudDirty = true;
                    } else if (!paused) {
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
                    } else {
                        const int r = menuRowUnder(e.motion.x, e.motion.y);
                        if (r >= 0 && r != g_menu.sel) { g_menu.sel = r; g.hudDirty = true; }
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (paused) {  // left click presses a row, right click steps a setting back
                        const int r = menuRowUnder(e.button.x, e.button.y);
                        const bool left = e.button.button == SDL_BUTTON_LEFT;
                        if (r >= 0 && (left || e.button.button == SDL_BUTTON_RIGHT)) {
                            g_menu.sel = r;
                            menuUse(r, left ? 1 : -1, false, left);
                        }
                        break;
                    }
                    if (g.rv.on) {  // a replay: a click skips the killcam; in the viewer, whose eyes (next / previous)
                        const int agents = int(replayNow.agents.size());
                        if (g.rv.killcam) g.rv.t = g.rv.end;
                        else if (agents > 0) {
                            const int step = e.button.button == SDL_BUTTON_RIGHT ? agents - 1 : 1;
                            g.rv.pov = ((g.rv.pov < 0 ? agents - 1 : g.rv.pov) + step) % agents;
                        }
                        g.hudDirty = true;
                        break;
                    }
                    if (g.buyMenu) {  // the buy wheel: left click picks, right click goes back
                        const float density = SDL_GetWindowPixelDensity(window);
                        const int slot = e.button.button == SDL_BUTTON_RIGHT
                                             ? kBuyCentre
                                             : buySlotAt(g, pixW, pixH, hudScale(cfg, pixH), e.button.x * density, e.button.y * density);
                        const char* msg = buyPick(g, slot);
                        if (msg[0]) pushHitLog(g, msg, 0xffd060);
                        sound(g, Sfx::UiClick, 0.4f);
                        g.hudDirty = true;
                        break;
                    }
                    if (g.mode == 3 && g.mapId == 1 && g.comp.youDead) {  // spectating: next / previous teammate
                        const int dir = e.button.button == SDL_BUTTON_RIGHT ? -1 : 1;
                        const int nx = nextTeammate(g, g.spec, dir);
                        if (nx >= 0) g.spec = nx;
                        g.hudDirty = true;
                        break;
                    }
                    if (e.button.button == SDL_BUTTON_LEFT) { g.fireHeld = true; g.fireLatch = true; }
                    if (e.button.button == SDL_BUTTON_RIGHT) { g.zoomLatch = true; g.zoomHeld = true; }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) g.fireHeld = false;
                    if (e.button.button == SDL_BUTTON_RIGHT) g.zoomHeld = false;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (!paused) {
                        g.jumpLatch = true;
                    } else if (e.wheel.y != 0) {
                        menuUse(g_menu.sel, e.wheel.y > 0 ? 1 : -1, false, false);
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
                    if (g.rv.on && !paused) {  // a replay: its own keys (the game's are off)
                        ReplayFrame& now = replayNow;
                        const int agents = int(now.agents.size());
                        if (g.rv.killcam) {
                            if (sc == SDL_SCANCODE_ESCAPE) g.rv.t = g.rv.end;  // skip (or a click)
                        } else if (sc == SDL_SCANCODE_ESCAPE) {
                            g.rv.on = false;
                            setMenu(kMenuPause);
                        } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT) {
                            g.rv.t = std::clamp(g.rv.t + (sc == SDL_SCANCODE_LEFT ? -5.0 : 5.0), g.replay.start(), g.replay.end());
                            g.rv.shotsDone = g.rv.t;
                            g.replayFx = Effects{};
                            if (sc == SDL_SCANCODE_LEFT) g.rv.paused = false;
                        } else if (sc == SDL_SCANCODE_UP) {
                            g.rv.speed = std::min(4.0f, g.rv.speed * 2.0f);
                        } else if (sc == SDL_SCANCODE_DOWN) {
                            g.rv.speed = std::max(0.25f, g.rv.speed * 0.5f);
                        } else if (sc == SDL_SCANCODE_P || (sc == SDL_SCANCODE_SPACE && g.rv.pov >= 0)) {
                            g.rv.paused = !g.rv.paused;
                        } else if (sc == SDL_SCANCODE_F && agents > 0) {  // free camera <-> back to someone's eyes
                            if (g.rv.pov >= 0) {
                                const ReplayAgent& a = now.agents[size_t(std::min(g.rv.pov, agents - 1))];
                                g.rv.camPos = a.pos + Vec3{0, 0, dummyEyeZ()};
                                g.rv.camYaw = a.yaw;
                                g.rv.camPitch = a.pitch;
                                g.rv.pov = -1;
                            } else {
                                g.rv.pov = agents - 1;
                            }
                        }
                        g.hudDirty = true;
                        break;
                    }
                    if (sc == SDL_SCANCODE_ESCAPE && g.buyMenu && !paused) {  // back to the categories, then shut
                        if (g.buyCategory > 0) g.buyCategory = 0;
                        else g.buyMenu = false;
                        g.hudDirty = true;
                    }
                    else if (sc == SDL_SCANCODE_ESCAPE) {
                        if (paused) menuBack();
                        else setMenu(kMenuPause);
                    } else if (sc == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
                        bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(window, !fs);
                    } else if (paused && menuField() && (sc == SDL_SCANCODE_BACKSPACE || typedChar(e.key.key, e.key.mod))) {
                        std::string& t = *menuField();
                        if (sc == SDL_SCANCODE_BACKSPACE) {
                            if (!t.empty()) t.pop_back();
                        } else if (t.size() < (&t == &cfg.player_name ? 15u : 64u)) {
                            t.push_back(typedChar(e.key.key, e.key.mod));
                        }
                        if (&t == &cfg.player_name && g.net.active()) g.net.setName(t);  // (live, if you're online)
                        saveConfig(cfgPath, cfg);
                        g.hudDirty = true;
                    } else if (paused && (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)) {
                        int n = int(menuRows(g_menu.screen, cfg, g.mode).size());
                        g_menu.sel = (g_menu.sel + (sc == SDL_SCANCODE_DOWN ? 1 : n - 1)) % n;
                        g.hudDirty = true;
                    } else if (paused && (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT)) {
                        menuUse(g_menu.sel, sc == SDL_SCANCODE_RIGHT ? 1 : -1, (e.key.mod & SDL_KMOD_SHIFT) != 0, false);
                    } else if (paused && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE)) {
                        menuUse(g_menu.sel, 1, false, true);
                    } else if (!paused) {
                        if (sc == SDL_SCANCODE_SPACE && g.mode == 3 && g.mapId == 1 && g.comp.youDead) {
                            g.spec = g.spec >= 0 ? -1 : nextTeammate(g, -1, 1);  // spectating <-> flying free
                            g.hudDirty = true;
                        } else if (sc == SDL_SCANCODE_SPACE) g.jumpLatch = true;
                        else if (sc == SDL_SCANCODE_R) g.reloadLatch = true;
                        else if (g.buyMenu && sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {  // the buy wheel
                            const int k = int(sc - SDL_SCANCODE_1) + 1;
                            const int slot = g.buyCategory == 0 && k == 8 ? kBuyFullRifle
                                             : g.buyCategory == 0 && k == 9 ? kBuyFullSniper : k - 1;
                            const char* msg = buyPick(g, slot);
                            if (msg[0]) pushHitLog(g, msg, 0xffd060);
                            g.hudDirty = true;
                        }
                        else if (sc == SDL_SCANCODE_1) g.switchTo = 1;
                        else if (sc == SDL_SCANCODE_2) g.switchTo = 2;
                        else if (sc == SDL_SCANCODE_3) g.switchTo = 3;
                        else if (sc == SDL_SCANCODE_4) g.switchTo = 4;
                        else if (sc == SDL_SCANCODE_Q) g.switchTo = 5;
                        else if (sc == SDL_SCANCODE_F) g.vm.inspect();
                        else if (sc == SDL_SCANCODE_B) {
                            const bool comp = g.mode == 3 && g.mapId == 1;
                            const bool canBuy = !comp || (!g.comp.youDead && g.simTime < g.comp.buyUntil && g.comp.phase <= 1 &&
                                                          length2d(g.player.origin - g.spawn) < 700.0f);
                            if (canBuy) {
                                g.buyMenu = !g.buyMenu;
                                g.buyCategory = 0;
                                if (g.buyMenu) {  // the cursor starts in the middle of the wheel
                                    const BuyWheel wh = buyWheel(pixW, pixH, hudScale(cfg, pixH));
                                    g.buyMouseX = wh.cx;
                                    g.buyMouseY = wh.cy;
                                    const float density = SDL_GetWindowPixelDensity(window);
                                    if (!automated) {
                                        SDL_SetWindowRelativeMouseMode(window, false);
                                        SDL_WarpMouseInWindow(window, wh.cx / density, wh.cy / density);
                                    }
                                }
                            }

                            else pushHitLog(g, "YOU CAN ONLY BUY IN YOUR SPAWN DURING BUY TIME", 0xffd060);
                            g.hudDirty = true;
                        }
                        else if (sc == SDL_SCANCODE_G) g.throwLatch = true;
                        else if (sc == SDL_SCANCODE_V && !g.online) { g.noclip = !g.noclip; g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_C) renderer.clearDecals();
                        // Map, mode, bots, drill, help, reset and config reload live in the Esc menu.
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
        if (!automated) {  // the mouse aims while you play; it's a cursor in the menus and the buy wheel
            const bool capture = !paused && !g.buyMenu;
            if (SDL_GetWindowRelativeMouseMode(window) != capture) SDL_SetWindowRelativeMouseMode(window, capture);
        }
        pumpNet();
        g.inputBlocked = paused;
        const bool viewing = g.rv.on && !g.rv.killcam;  // the replay viewer: the game waits
        if ((!paused || g.online) && !viewing) {  // online never pauses

            tickAcc += dt;
            while (tickAcc >= kTickDt) {
                simTick(g, opt);
                tickAcc -= kTickDt;
            }
        }
        if (!paused) stepReplay(g, dt);
        g_replayAvailable = !g.online && g.mapId == 1 && g.replay.frames() > 64;
        if (g.rv.on && !g.rv.killcam && g.rv.pov < 0 && !paused) {  // the free camera flies (Shift slow)
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const Vec3 f = anglesToForward(g.rv.camPitch, g.rv.camYaw), r = yawToRight(g.rv.camYaw);
            Vec3 move = f * float(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) + r * float(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) +
                        Vec3{0, 0, float(keys[SDL_SCANCODE_SPACE] - keys[SDL_SCANCODE_LCTRL])};
            g.rv.camPos += move * float(dt * (keys[SDL_SCANCODE_LSHIFT] ? 150.0 : 600.0));
        }
        const uint64_t tSim = SDL_GetPerformanceCounter();
        float alpha = float(tickAcc / kTickDt);
        if (automated && opt.startZoom && frame == 60) { g.zoom = opt.startZoom; g.hudDirty = true; }
        if (automated && opt.throwSmoke && frame == opt.throwFrame) {
            g.nadeType = std::clamp(opt.nadeType, 0, Game::kNadeTypes - 1);
            g.throwLatch = true;
        }
        if (automated && frame == opt.inspectFrame) g.vm.inspect();
        if (automated && opt.buyCat >= 0 && frame == 60) {  // (screenshots: the wheel, the mouse on its first wedge)
            g.buyMenu = true;
            g.buyCategory = opt.buyCat;
            const BuyWheel wh = buyWheel(pixW, pixH, hudScale(cfg, pixH));
            g.buyMouseX = wh.cx;
            g.buyMouseY = wh.cy - (wh.r0 + wh.r1) * 0.5f;
            g.hudDirty = true;
        }
        if (automated && frame == opt.dieFrame) {
            g.spawnProtectUntil = 0;
            hurtPlayer(g, opt.dieBy, 1000.0f, false, "TEST");
        }
        if (automated && frame == opt.replayFrame) startReplayViewer(g);
        if (automated && opt.bots && frame == 1) g.botsFire = true;
        if (automated && opt.menuScreen > 0 && frame == 60) {
            g_menu.root = opt.menuScreen == kMenuMain ? kMenuMain : kMenuPause;
            if (opt.menuScreen == kMenuCase) startCase();  // (needs --give-cases)
            setMenu(opt.menuScreen);
            g_menu.sel = opt.menuRow;
        }

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
        // Competitive, dead: through a teammate's eyes (the next one, if they die too).
        if (g.mode == 3 && g.mapId == 1 && g.comp.youDead) {
            if (g.spec >= 0 && (size_t(g.spec) >= g.dummies.size() || !g.dummies[size_t(g.spec)].alive()))
                g.spec = nextTeammate(g, g.spec, 1);
        } else {
            g.spec = -1;
        }
        const bool spectating = g.spec >= 0;
        if (spectating) {
            const Dummy& d = g.dummies[size_t(g.spec)];
            eye = lerp(d.prevPos, d.pos, alpha) + Vec3{0, 0, dummyEyeZ(d.crouch)};
            camYaw = wrapDeg(d.prevYaw + wrapDeg(d.yaw - d.prevYaw) * alpha);
            camPitch = d.prevPitch + (d.pitch - d.prevPitch) * alpha;
        }

        // A replay: everyone as recorded, through someone's eyes (or the free camera).
        const bool replayView = g.rv.on && g.replay.sample(g.rv.t, replayNow);
        if (replayView) {
            if (g.rv.pov >= 0 && size_t(g.rv.pov) < replayNow.agents.size()) {
                const ReplayAgent& a = replayNow.agents[size_t(g.rv.pov)];
                const bool you = size_t(g.rv.pov) + 1 == replayNow.agents.size();  // (your own eyes: your height)
                eye = a.pos + Vec3{0, 0, you ? kStandEye + (kDuckEye - kStandEye) * a.crouch : dummyEyeZ(a.crouch)};
                camYaw = a.yaw;
                camPitch = a.pitch;
            } else {
                eye = g.rv.camPos;
                camYaw = g.rv.camYaw;
                camPitch = g.rv.camPitch;
            }
        }

        float aspect = pixH > 0 ? float(pixW) / float(pixH) : 1.0f;
        if (!paused) g.fovPunch *= std::exp(-float(dt) / 0.045f);
        const float thump = g.zoom == 0 ? 1.0f + 0.014f * g.fovPunch : 1.0f;  // shot thump: ~1.4% wider, 45 ms
        // Scoping in eases the zoom over ~50 ms (the scope "settles" onto the target); out is instant, like CS.
        const float targetFov = zoomFov(g.zoom, cfg.fov);
        if (g.shownFov <= 0 || targetFov >= g.shownFov || paused) g.shownFov = targetFov;
        else g.shownFov = targetFov + (g.shownFov - targetFov) * std::exp(-float(dt) / 0.018f);
        float vfov = 2.0f * std::atan(std::tan(g.shownFov * 0.5f * kDegToRad) * 0.75f * thump);
        if (!paused) g.camRoll *= std::exp(-float(dt) / 0.07f);
        const Mat4 roll = rotationZ(g.camRoll);  // about the view axis: the crosshair stays put
        Mat4 viewProj = perspective(vfov, aspect, 2.0f, 16384.0f) * roll * viewFromAngles(eye, camPitch, camYaw);

        // A replay draws the recorded players (you among them), smokes and tracers instead of the live ones; it's
        // all swapped back right after the world is drawn.
        static std::vector<Dummy> replayDummies;
        static std::vector<Game::Smoke> replaySmokes;
        static std::vector<Game::Nade> noNades;
        static std::vector<Game::Fire> noFires;
        static std::vector<Vec3> keepRenderPos;
        static std::vector<int> keepTeam;
        const int keepSpec = g.spec;
        const double keepTime = g.simTime;
        if (replayView) {
            replayDummies.resize(replayNow.agents.size());
            for (size_t i = 0; i < replayNow.agents.size(); ++i) {
                const ReplayAgent& a = replayNow.agents[i];
                Dummy& x = replayDummies[i];
                x = Dummy{};
                x.pos = x.prevPos = a.pos;
                x.yaw = x.prevYaw = x.shownYaw = a.yaw;
                x.pitch = x.prevPitch = a.pitch;
                x.crouch = x.prevCrouch = x.shownCrouch = a.crouch;
                x.weapon = a.weapon;
                x.respawnLeft = a.alive ? 0.0f : 1.0f;
                x.deadFor = a.deadFor;
                x.hitDir = a.hitDir;
                x.friendly = false;
                x.lostHelmet = a.lostHelmet;
                x.stepDist = a.stepDist;
            }
            replaySmokes.clear();
            for (const ReplayFrame::Smoke& sm : replayNow.smokes) replaySmokes.push_back({sm.pos, sm.start});
            std::swap(g.dummies, replayDummies);
            std::swap(g.smokes, replaySmokes);
            std::swap(g.nades, noNades);
            std::swap(g.fires, noFires);
            std::swap(g.fx, g.replayFx);
            keepRenderPos = g.lastDummyRenderPos;
            g.lastDummyRenderPos.resize(g.dummies.size());
            keepTeam = g.team;
            if (g.mode == 3 && !g.team.empty()) g.team.push_back(g.comp.youTeam);  // you, in your colours
            g.spec = g.rv.pov;  // (not their own body in front of their eyes)
            g.simTime = replayNow.t;
        }

        // The muzzle light fades out over a few frames.
        if (!paused) g.flashLight *= std::exp(-float(dt) / 0.022f);
        const float flash = g.flashLight * std::clamp(cfg.muzzle_brightness, 0.0f, 1.0f);
        renderer.setFlash(g.flashPos, flash > 0.01f ? flash : 0.0f);

        // Dummies at their interpolated positions; remember exactly what we drew for hit tests.
        dynamicBoxes.clear();
        deadDraws.clear();
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            Vec3 p = lerp(d.prevPos, d.pos, alpha);
            g.lastDummyRenderPos[i] = p;
            const float shownYaw = wrapDeg(d.prevYaw + wrapDeg(d.yaw - d.prevYaw) * alpha);
            g.dummies[i].shownYaw = shownYaw;  // shots test against exactly this facing
            const float crouch = d.prevCrouch + (d.crouch - d.prevCrouch) * alpha;
            g.dummies[i].shownCrouch = crouch;  // and this crouch
            if (int(i) == g.spec) continue;          // you're looking out of their eyes
            const float turn = (shownYaw - 180.0f) * kDegToRad;
            if (d.alive()) g.dummies[i].lostHelmet = false;
            // The dead fall over the way the killing shot pushed them (backwards if we don't know), like a
            // ragdoll: they tip faster and faster, slide a little, lie there, then sink away. Cosmetic: the dead
            // can't be hit. The whole body is then one model, so it can tilt.
            const float squash = 1.0f;
            float killFlash = 0;
            ModelDraw* body = nullptr;
            if (!d.alive()) {
                const float t = d.deadFor;
                if (t > 3.1f) continue;
                Vec3 hd{d.hitDir.x, d.hitDir.y, 0};
                const float hl = length(hd);
                hd = hl > 0.01f ? hd * (1.0f / hl)
                                : Vec3{-std::cos(shownYaw * kDegToRad), -std::sin(shownYaw * kDegToRad), 0};
                const float hYaw = std::atan2(hd.y, hd.x) / kDegToRad;
                killFlash = std::max(0.0f, 1.0f - t / 0.14f);  // bright for an instant, held still...
                const float fall = std::clamp((t - 0.08f) / 0.42f, 0.0f, 1.0f);  // ...then down
                const float tip = 84.0f * fall * fall, slide = 8.0f + 10.0f * fall * (2.0f - fall);
                const float sink = t > 2.6f ? (t - 2.6f) * 60.0f : 0.0f;
                deadDraws.push_back({translation(p + hd * slide - Vec3{0, 0, sink}) * rotationZ(hYaw) * rotationY(tip) *
                                         translation({-8.0f, 0, 0}) * rotationZ(shownYaw - 180.0f - hYaw),
                                     {}, {}});
                body = &deadDraws.back();
            }
            const float ms = modelScale();
            auto part = [&](Vec3 mn, Vec3 mx, uint32_t c) {
                mn = mn * ms;
                mx = mx * ms;
                mn.z = crouchZ(mn.z, crouch) * squash;
                mx.z = crouchZ(mx.z, crouch) * squash;
                if (body) {
                    if (killFlash > 0) c = lerpColor(c, 0xfff6ee, 0.85f * killFlash);
                    body->boxes.push_back(killFlash > 0.5f ? makeEmissive(mn, mx, c) : makeBox(mn, mx, c, false));
                    return;
                }
                dynamicBoxes.push_back(makeBox(p + mn, p + mx, c, false));
                yawBox(dynamicBoxes.back(), p, turn);
            };
            // The figure, every piece inside its hitbox (what you see is what you hit; only the gun sticks
            // out, like CS). Competitive: Ts in tan and brown with a balaclava, CTs in navy with a helmet and
            // goggles (everyone else dresses CT). A hit flashes the part that was hit.
            {
                const bool isT = g.mode == 3 && i < g.team.size() && g.team[i] == 0;
                const uint32_t shirt = isT ? 0x9a7a48 : 0x2f4f8a, pants = isT ? 0x6e5430 : 0x24365e;
                const uint32_t vest = isT ? 0x5c4a2e : 0x3a4530, gear = isT ? 0x3a3024 : 0x22262c;
                const uint32_t skin = 0xd9a87e, glove = 0x1c1d20, boot = 0x1d1a17;
                auto hit = [&](uint32_t c, HitGroup grp) { return lerpColor(c, 0xffffff, std::min(1.0f, d.flash[grp] / 0.15f)); };
                // Legs: two of them, stepping while they walk.
                const float speed = length2d(d.pos - d.prevPos) / kTickDt;
                const float swing = speed > 60.0f && d.alive() ? std::sin(d.stepDist / 76.0f * kPi) * 1.0f : 0.0f;
                for (int side = -1; side <= 1; side += 2) {
                    const float y0 = side < 0 ? -7.4f : 1.0f, y1 = side < 0 ? -1.0f : 7.4f, dx = swing * float(side);
                    part({-3.6f + dx, y0, 6.0f}, {3.6f + dx, y1, 34.0f}, hit(pants, kLegs));
                    part({-3.6f + dx, y0 - 0.2f, 0.0f}, {3.6f + dx, y1 + 0.2f, 6.0f}, hit(boot, kLegs));
                    part({-4.0f + dx, y0 + 1.0f, 16.0f}, {-3.4f + dx, y1 - 1.0f, 21.0f}, hit(gear, kLegs));  // knee pad
                }
                // Hips and belt, then the shirt.
                part({-5.6f, -8.6f, 34.0f}, {5.6f, 8.6f, 38.5f}, hit(pants, kStomach));
                part({-5.9f, -8.8f, 38.5f}, {5.9f, 8.8f, 40.5f}, hit(0x2b2117, kStomach));
                part({-6.0f, -1.3f, 38.7f}, {-5.8f, 1.3f, 40.3f}, hit(0x9a9a8a, kStomach));  // buckle
                part({-5.6f, -8.4f, 40.5f}, {5.6f, 8.4f, 46.0f}, hit(shirt, kStomach));
                // Chest: the shirt, a vest with pouches, shoulder pads and a small pack on the back.
                part({-5.8f, -9.6f, 46.0f}, {5.8f, 9.6f, 57.0f}, hit(shirt, kChest));
                part({-6.4f, -8.6f, 46.5f}, {6.2f, 8.6f, 56.0f}, hit(vest, kChest));
                for (int k = -1; k <= 1; ++k)
                    part({-6.5f, float(k) * 4.2f - 1.6f, 47.0f}, {-6.2f, float(k) * 4.2f + 1.6f, 51.0f}, hit(gear, kChest));
                part({-3.4f, -10.0f, 54.0f}, {3.4f, -7.8f, 58.0f}, hit(vest, kChest));
                part({-3.4f, 7.8f, 54.0f}, {3.4f, 10.0f, 58.0f}, hit(vest, kChest));
                part({-2.6f, -3.6f, 56.0f}, {2.6f, 3.6f, 58.0f}, hit(shirt, kChest));  // collar
                part({5.8f, -6.0f, 47.0f}, {6.5f, 6.0f, 56.0f}, hit(gear, kChest));    // pack
                // Arms: sleeves to the elbow, forearms, gloves on the gun (right at the grip, left on the handguard).
                part({-6.0f, -12.8f, 50.0f}, {1.0f, -10.2f, 56.5f}, hit(shirt, kChest));
                part({-12.0f, -13.2f, 45.5f}, {-6.0f, -10.4f, 50.5f}, hit(shirt, kChest));
                part({-13.0f, -13.4f, 45.5f}, {-10.0f, -10.2f, 49.5f}, hit(glove, kChest));
                part({-6.0f, 10.2f, 50.0f}, {1.0f, 12.8f, 56.5f}, hit(shirt, kChest));
                part({-14.0f, 10.4f, 45.5f}, {-6.0f, 13.2f, 50.5f}, hit(shirt, kChest));
                part({-15.0f, 10.2f, 45.5f}, {-12.0f, 13.4f, 49.5f}, hit(glove, kChest));
                // The head: CTs a face under a helmet with goggles; Ts a balaclava with an eye slit and a beanie.
                if (isT) {
                    part({-4.3f, -4.2f, 59.2f}, {4.3f, 4.2f, 67.0f}, hit(0x2a2622, kHead));
                    part({-4.45f, -3.2f, 62.8f}, {-4.25f, 3.2f, 64.8f}, hit(skin, kHead));       // eye slit
                    part({-4.5f, -2.4f, 63.4f}, {-4.45f, -1.2f, 64.2f}, hit(0x18120c, kHead));   // eyes
                    part({-4.5f, 1.2f, 63.4f}, {-4.45f, 2.4f, 64.2f}, hit(0x18120c, kHead));
                    part({-4.4f, -4.4f, 66.0f}, {4.4f, 4.4f, 69.0f}, hit(0x4a3a28, kHead));      // beanie
                } else {
                    part({-4.2f, -4.0f, 59.2f}, {4.2f, 4.0f, 66.0f}, hit(skin, kHead));
                    part({-4.3f, -1.6f, 60.2f}, {-4.15f, 1.6f, 60.8f}, hit(0x7a4a3a, kHead));    // mouth
                    if (!(d.lostHelmet && !d.alive()))
                        part({-4.5f, -4.5f, 65.0f}, {4.5f, 4.5f, 69.0f}, hit(0x2c3a24, kHead));  // helmet
                    part({-4.5f, -3.4f, 62.6f}, {-4.25f, 3.4f, 64.8f}, hit(0x14181e, kHead));    // goggles
                    part({-4.3f, -4.5f, 63.0f}, {4.3f, -4.25f, 64.4f}, hit(0x1a1a1a, kHead));    // the strap
                    part({-4.3f, 4.25f, 63.0f}, {4.3f, 4.5f, 64.4f}, hit(0x1a1a1a, kHead));
                }
            }
            // What they hold (weapons are the one thing allowed outside the hitboxes, like CS).
            switch (d.weapon) {
                case kWPistol:  // pistol: a short slide between the hands, the suppressor out front
                    part({-17.5f, -1.0f, 47.0f}, {-11.0f, 1.0f, 49.6f}, 0x1e2024);
                    part({-23.5f, -0.7f, 47.4f}, {-17.5f, 0.7f, 49.2f}, 0x15171a);
                    part({-13.0f, -0.8f, 44.5f}, {-11.2f, 0.8f, 47.0f}, 0x26282c);
                    break;
                case kWDeagle:  // a bigger, chunkier pistol
                    part({-19.0f, -1.3f, 47.0f}, {-10.8f, 1.3f, 50.4f}, 0x3a3d42);
                    part({-13.2f, -1.0f, 44.0f}, {-11.0f, 1.0f, 47.0f}, 0x26282c);
                    break;
                case kWBerettas:  // a pistol in each hand
                    part({-17.5f, -12.6f, 47.0f}, {-11.5f, -10.8f, 49.4f}, 0x8a9098);
                    part({-19.5f, 10.8f, 47.0f}, {-13.5f, 12.6f, 49.4f}, 0x8a9098);
                    break;
                case kWNova:  // a long pump shotgun
                    part({-17.0f, -1.5f, 45.5f}, {-6.6f, 1.5f, 49.5f}, 0x1e2024);
                    part({-33.0f, -0.7f, 47.0f}, {-17.0f, 0.7f, 48.6f}, 0x111214);
                    part({-24.0f, -1.2f, 45.2f}, {-17.0f, 1.2f, 47.2f}, 0x2a2c30);
                    break;
                case kWXm1014:  // a long auto shotgun, the tube under the barrel
                    part({-17.0f, -1.5f, 45.5f}, {-6.6f, 1.5f, 49.5f}, 0x232528);
                    part({-34.0f, -0.7f, 47.4f}, {-17.0f, 0.7f, 48.8f}, 0x111214);
                    part({-31.0f, -0.8f, 45.8f}, {-17.0f, 0.8f, 47.4f}, 0x2a2c30);
                    break;
                case kWMac10:  // a stubby box with a long mag
                    part({-17.0f, -1.6f, 46.0f}, {-9.0f, 1.6f, 50.0f}, 0x26282c);
                    part({-14.0f, -0.8f, 41.0f}, {-12.0f, 0.8f, 46.0f}, 0x1a1b1e);
                    break;
                case kWKnife:  // knife
                    part({-14.0f, -0.6f, 46.5f}, {-11.0f, 0.6f, 48.5f}, 0x2a2420);
                    part({-21.0f, -0.25f, 46.8f}, {-14.0f, 0.25f, 48.2f}, 0xb8bcc2);
                    break;
                case kWGrenade:  // grenade in the hand
                    part({-15.5f, -1.8f, 46.0f}, {-12.0f, 1.8f, 50.0f}, 0x3b4a2f);
                    break;
                case kWSniper:  // sniper: long barrel, scope on top
                    part({-17.0f, -1.4f, 46.0f}, {-6.6f, 1.4f, 49.5f}, 0x2c3326);

                    part({-36.0f, -0.6f, 47.2f}, {-17.0f, 0.6f, 48.4f}, 0x111214);
                    part({-15.0f, -1.0f, 49.5f}, {-8.0f, 1.0f, 51.5f}, 0x0e0f10);
                    break;
                case kWM4A1S:  // black, a square handguard, a long suppressor
                    part({-16.0f, -1.4f, 46.0f}, {-6.6f, 1.4f, 49.5f}, 0x18191c);
                    part({-21.0f, -1.6f, 45.8f}, {-14.0f, 1.6f, 49.4f}, 0x222327);
                    part({-24.0f, -0.5f, 47.3f}, {-21.0f, 0.5f, 48.3f}, 0x111214);
                    part({-33.0f, -1.0f, 46.8f}, {-24.0f, 1.0f, 48.8f}, 0x0e0f10);
                    part({-12.0f, -0.8f, 41.5f}, {-10.0f, 0.8f, 46.0f}, 0x111214);  // the mag
                    break;
                case kWGalil:  // dark, a wooden handguard, a curved mag
                    part({-16.0f, -1.4f, 46.0f}, {-6.6f, 1.4f, 49.5f}, 0x232528);
                    part({-21.0f, -1.5f, 45.8f}, {-15.0f, 1.5f, 48.8f}, 0x5a3c23);
                    part({-27.0f, -0.6f, 47.2f}, {-21.0f, 0.6f, 48.4f}, 0x111214);
                    part({-13.5f, -0.8f, 41.0f}, {-11.0f, 0.8f, 46.0f}, 0x18191c);
                    break;
                case kWSsg08:  // a slim bolt gun, a small scope
                    part({-16.0f, -1.2f, 46.2f}, {-6.6f, 1.2f, 49.0f}, 0x2a2c30);
                    part({-33.0f, -0.5f, 47.3f}, {-16.0f, 0.5f, 48.3f}, 0x111214);
                    part({-14.0f, -0.8f, 49.0f}, {-9.0f, 0.8f, 50.6f}, 0x0e0f10);
                    break;
                case kWUmp45:  // a boxy SMG, a straight mag
                    part({-18.0f, -1.6f, 45.8f}, {-8.0f, 1.6f, 49.8f}, 0x232528);
                    part({-21.0f, -0.6f, 47.2f}, {-18.0f, 0.6f, 48.4f}, 0x111214);
                    part({-15.0f, -0.9f, 40.0f}, {-13.0f, 0.9f, 45.8f}, 0x18191c);
                    break;
                default:  // the AK-47
                    part({-16.0f, -1.4f, 46.0f}, {-6.6f, 1.4f, 49.5f}, 0x1e2024);
                    part({-27.0f, -0.6f, 47.2f}, {-16.0f, 0.6f, 48.4f}, 0x111214);
                    break;
            }
            if (d.friendly && d.alive())  // teammate marker floating over their head (cosmetic, not hittable)
                dynamicBoxes.push_back(makeEmissive(p + Vec3{-2.5f, -2.5f, crouchZ(80 * ms, crouch)},
                                                    p + Vec3{2.5f, 2.5f, crouchZ(85 * ms, crouch)}, 0x60ff90));

        }
        if (!replayView) g.lastRenderEye = eye;

        // Cosmetic updates at frame rate.
        float fdt = float(dt);
        if (!replayView) g.fx.update(paused ? 0.0f : fdt);
        g.fx.appendParticles(dynamicBoxes);
        for (const Game::Nade& n : g.nades) {
            const uint32_t nadeColor[Game::kNadeTypes] = {0x3b4a2f, 0xd6d8da, 0x4a5a2a, 0x7a4a1a};
            dynamicBoxes.push_back(
                makeBox(n.pos - Vec3{1.5f, 1.5f, 1.5f}, n.pos + Vec3{1.5f, 1.5f, 2.5f}, nadeColor[n.type], false));
        }
        // Grenade trajectory preview (`nade_preview`): the exact flight a throw would take right now
        // (same code as the real grenade), as dots, with a cross where it goes off.
        const bool compMatch = g.mode == 3 && g.mapId == 1;
        if (g.weapon == &g.guns[kWGrenade] && !paused && g.deadUntil < 0 && (cfg.nade_preview == 2 || (cfg.nade_preview == 1 && !compMatch)) &&
            !(compMatch && (g.comp.youDead || g.comp.nades[g.nadeType] <= 0))) {
            const float strength = g.nadeHold == 2 ? kNadeLob : g.nadeHold == 3 ? kNadeMedium : 1.0f;  // what you're holding
            const Vec3 v = grenadeThrowVelocity(float(g.viewPitch), float(g.viewYaw), strength, g.player.velocity);
            const Vec3 f = normalize(v - g.player.velocity * 1.25f);
            nadePath.clear();
            const Vec3 end = predictGrenade(g.world, g.lastRenderEye + f * 16.0f, v, g.nadeType, &nadePath);
            for (size_t k = 14; k < nadePath.size(); k += 5) {
                const Vec3& p = nadePath[k];
                dynamicBoxes.push_back(makeEmissive(p - Vec3{0.7f, 0.7f, 0.7f}, p + Vec3{0.7f, 0.7f, 0.7f}, 0xf2ecd0));
            }
            const uint32_t mark[Game::kNadeTypes] = {0xd8d8d8, 0xffffff, 0xff6a40, 0xff9a20};
            dynamicBoxes.push_back(makeEmissive(end + Vec3{-12, -1, 0.5f}, end + Vec3{12, 1, 1.5f}, mark[g.nadeType]));
            dynamicBoxes.push_back(makeEmissive(end + Vec3{-1, -12, 0.5f}, end + Vec3{1, 12, 1.5f}, mark[g.nadeType]));
        }
        // The C4: taped bricks, a keypad with a screen, wires, and a red light that blinks with the beeps
        // once it's armed. Planted (retakes, competitive) or lying where the carrier dropped it.
        auto drawBomb = [&](const Vec3& b, bool armed, bool lightOn) {
            const float turn = std::fmod(std::fabs(b.x * 0.37f + b.y * 0.11f), 6.2832f);  // any angle, stable
            auto bit = [&](Vec3 mn, Vec3 mx, uint32_t col, bool glow) {
                mn = mn * 1.4f;  // a touch bigger than real so it reads from across a site
                mx = mx * 1.4f;
                dynamicBoxes.push_back(glow ? makeEmissive(b + mn, b + mx, col) : makeBox(b + mn, b + mx, col, false));
                yawBox(dynamicBoxes.back(), b, turn);
            };
            bit({-6.0f, -3.5f, 0.0f}, {6.0f, 3.5f, 3.0f}, 0x77704a, false);   // the charge
            bit({-4.5f, -3.6f, 0.0f}, {-3.3f, 3.6f, 3.1f}, 0x2a2b2e, false);  // tape
            bit({3.3f, -3.6f, 0.0f}, {4.5f, 3.6f, 3.1f}, 0x2a2b2e, false);
            bit({-2.5f, -2.6f, 3.0f}, {2.7f, 2.6f, 4.0f}, 0x2d3035, false);   // keypad
            bit({-2.0f, -1.8f, 4.0f}, {0.6f, 1.8f, 4.25f}, armed ? 0x7dff8a : 0x2e3f30, armed);  // screen
            bit({2.7f, 0.8f, 3.0f}, {5.2f, 1.5f, 3.6f}, 0xb02828, false);     // wires
            bit({2.7f, -1.5f, 3.0f}, {5.2f, -0.8f, 3.6f}, 0xc8a028, false);
            bit({1.4f, -0.5f, 4.0f}, {2.2f, 0.5f, 4.6f}, lightOn ? 0xff2020 : 0x401010, lightOn);  // light
        };
        if (g.mapId == 1 && (g.mode == 2 || g.mode == 3) && g.bombActive)
            drawBomb(g.bombPos, true,
                     g.nextBeep - g.simTime > 0.5 * std::clamp((g.bombExplodeAt - g.simTime) / kRetakeRoundTime, 0.1, 1.0));
        if (g.mapId == 1 && g.mode == 3 && g.comp.carrier == -2 && !g.comp.planted) drawBomb(g.comp.dropped, false, false);
        for (const Game::Fire& f : g.fires) {
            // Flames: glowing columns that flicker (cosmetic hash of time), dying down at the end.
            float age = float(g.simTime + tickAcc - f.start);
            float life = std::clamp(std::min(age / 0.3f, float(kFireLife - age) / 1.0f), 0.0f, 1.0f);
            int tick = int((g.simTime + tickAcc) * 14.0);
            for (uint32_t pi = 0; pi < 26; ++pi) {
                uint32_t hsh = (pi + 1) * 2654435761u, flick = (pi * 977u + uint32_t(tick)) * 2246822519u;
                float a = float(hsh % 6283) / 1000.0f, rr = std::sqrt(float((hsh >> 8) % 1000) / 1000.0f);
                Vec3 c = f.pos + Vec3{std::cos(a) * rr * kFireRadius, std::sin(a) * rr * kFireRadius, 0};
                float hgt = (10.0f + float(flick % 26)) * life, wdt = 5.0f + float((hsh >> 4) % 5);
                uint32_t col = (flick >> 8) % 3 == 0 ? 0xffd25a : (flick >> 8) % 3 == 1 ? 0xff8a2a : 0xe8461c;
                dynamicBoxes.push_back(makeEmissive(c - Vec3{wdt, wdt, 0}, c + Vec3{wdt, wdt, hgt}, col));
            }
        }
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
            g.vm.setPrimed(g.weapon == &g.guns[kWGrenade] ? g.nadeHold : 0);  // wind-up pose while you hold a grenade
            g.vm.update({paused ? 0.0f : fdt, frameYawDelta, framePitchDelta, length2d(g.player.velocity),
                         g.player.onGround, float(reloadProgress), ws.def->reloadTime});
        }

        const uint64_t tScene = SDL_GetPerformanceCounter();
        renderer.beginFrame(pixW, pixH);
        renderer.drawBoxes(viewProj, eye, dynamicBoxes);
        {  // the sky behind it (the camera roll is a degree or so: ignored here)
            const float ty = std::tan(vfov * 0.5f), tx = ty * aspect;
            const Vec3 f = anglesToForward(camPitch, camYaw), r = yawToRight(camYaw), u = cross(r, f);
            renderer.drawSky(f, r * tx, u * ty);
        }
        modelDraws.clear();
        g.fx.appendTracers(modelDraws);
        for (const ModelDraw& md : modelDraws) renderer.drawModel(viewProj, md.model, md.boxes);
        for (const ModelDraw& md : deadDraws) renderer.drawModel(viewProj, md.model, md.boxes);
        if (replayView) {  // back to the live game
            std::swap(g.dummies, replayDummies);
            std::swap(g.smokes, replaySmokes);
            std::swap(g.nades, noNades);
            std::swap(g.fires, noFires);
            std::swap(g.fx, g.replayFx);
            g.lastDummyRenderPos = keepRenderPos;
            g.team = keepTeam;
            g.spec = keepSpec;
            g.simTime = keepTime;
        }

        // First-person weapon: own FOV and fresh depth so it never clips into walls.
        if (cfg.show_viewmodel && g.zoom == 0 && !spectating && !replayView) {
            float vmVfov = 2.0f * std::atan(std::tan(cfg.viewmodel_fov * 0.5f * kDegToRad) * 0.75f);
            Mat4 vmViewProj = perspective(vmVfov, aspect, 0.5f, 256.0f) * roll * viewFromAngles(eye, camPitch, camYaw);
            modelDraws.clear();
            g.vm.build(eye, camPitch, camYaw, cfg.viewmodel_offset_x, cfg.viewmodel_offset_y, cfg.viewmodel_offset_z,
                       cfg.viewmodel_bob, modelDraws);
            renderer.clearDepth();
            for (const ModelDraw& md : modelDraws) renderer.drawModel(vmViewProj, md.model, md.boxes, &md.paint);
        }

        const uint64_t tDraw = SDL_GetPerformanceCounter();
        // HUD: rebuild at most ~60 Hz unless something changed (keeps uploads tiny at 1000+ FPS).
        double nowSec = double(frameStart) / freq;
        bool markerExpired = hitMarkerShownUntil > 0 && g.simTime >= g.hitMarkerUntil;
        bool rebuild = g.hudDirty || markerExpired || nowSec - lastHudBuild > 1.0 / 60.0;
        if (rebuild) {
            buildHud(hud, g, cfg, stats, pixW, pixH);
            lastHudBuild = nowSec;
            g.hudDirty = false;
            hitMarkerShownUntil = g.simTime < g.hitMarkerUntil ? g.hitMarkerUntil : 0;
        }
        renderer.drawHud(hud, rebuild);
        // The inventory / case stage: the weapon turning in front of its backdrop, over the HUD.
        if (g_menu.screen == kMenuInventory || g_menu.screen == kMenuCase) {
            const Stage st = showcaseStage(cfg, pixW, pixH);
            if (st.show && pixW > 0 && pixH > 0) {
                const float sv = std::tan(20.0f * kDegToRad), sh = sv * aspect, dist = 60.0f;
                // The stage's middle (a little above, clear of the name) in camera space.
                const float px = st.x + st.w * 0.5f, py = st.y + st.h * 0.44f;
                const Vec3 centre{(px / float(pixW) * 2.0f - 1.0f) * sh * dist, (1.0f - py / float(pixH) * 2.0f) * sv * dist, -dist};
                // Long guns fill most of the stage; the MAC-10, knives and pistols are shown smaller, like CS.
                const float room = std::min(st.w / float(pixW) * 2.0f * sh, st.h / float(pixH) * 2.0f * sv * 1.6f) * dist;
                const int sw = st.weapon;
                const float size = room * (sw == kWRifle || sw == kWSniper || sw == kWNova || sw == kWXm1014 || sw == kWM4A1S || sw == kWGalil ||
                                                   sw == kWSsg08 ? 0.85f
                                           : sw == kWUmp45 ? 0.62f : sw == kWMac10 ? 0.5f : sw == kWKnife ? 0.55f : 0.42f);
                const Equipped& it = st.item;
                const int knife = st.weapon == kWKnife && it.skin >= 0 ? allSkins()[size_t(it.skin)].knife : kKnifeDefault;
                modelDraws.clear();
                ViewModel::buildShowcase(viewWeaponFor(st.weapon), knife, paintFor(it), eye, camPitch, camYaw,
                                         float(std::fmod(g_uiTime * 25.0, 360.0)), centre, size, modelDraws);
                const Mat4 stageProj = perspective(40.0f * kDegToRad, aspect, 1.0f, 512.0f) * viewFromAngles(eye, camPitch, camYaw);
                renderer.clearDepth();
                for (const ModelDraw& md : modelDraws) renderer.drawModel(stageProj, md.model, md.boxes, &md.paint);
            }
        }

        const uint64_t tHud = SDL_GetPerformanceCounter();
        if (bench && !opt.benchRaw) glFinish();  // so GPU time shows up as GPU, not inside the next frame
        const uint64_t tGpu = SDL_GetPerformanceCounter();
        if (!opt.screenshotPath.empty() && frame == opt.screenshotFrame) {
            bool ok = renderer.screenshot(opt.screenshotPath);
            std::fprintf(stderr, "screenshot %s: %s (shots %d, hits %d)\n", opt.screenshotPath.c_str(),
                         ok ? "ok" : SDL_GetError(), g.shots, g.hits);
            running = false;
        }

        SDL_GL_SwapWindow(window);
        if (cfg.low_latency) frameFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        ++frame;
        if (bench && frame > 30) {  // skip warm-up frames
            const uint64_t tSwap = SDL_GetPerformanceCounter();
            auto ms = [&](uint64_t a, uint64_t b) { return double(b - a) * 1000.0 / freq; };
            BenchStats& b = benchStats;
            b.wait += ms(frameStart, tFence);
            b.sim += ms(tFence, tSim);
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
                char report[900];
                std::snprintf(report, sizeof(report),
                              "GPU: %s\nresolution %dx%d  msaa %d  map %d  mode %d  bots %zu  static boxes %zu\n"
                              "frames %zu  avg %.0f fps (%.3f ms)  1%% low %.0f fps (%.3f ms)  dynamic boxes/frame %.0f\n"
                              "ms per frame:  latency wait %.3f  input+sim %.3f  scene %.3f  draw calls %.3f  hud %.3f  gpu %.3f  swap %.3f\n"
                              "low latency %d  vsync %d  fps cap %d\n"
                              "audio: mixer thread busy %.2f%% of one core, up to %d voices at once\n",
                              reinterpret_cast<const char*>(glGetString(GL_RENDERER)), pixW, pixH, std::clamp(cfg.msaa, 0, 8),
                              g.mapId, g.mode, g.dummies.size(), g.world.solids.size(), n, 1000.0 * double(n) / total,
                              total / double(n), 1000.0 / double(sorted[n * 99 / 100]), double(sorted[n * 99 / 100]),
                              double(b.dynBoxes) / double(n), b.wait / double(n), b.sim / double(n), b.scene / double(n),
                              b.draw / double(n), b.hud / double(n), b.gpu / double(n), b.swap / double(n), cfg.low_latency,
                              cfg.vsync, cfg.fps_max, g.audio ? audio.mixLoad() * 100.0 : 0.0, g.audio ? audio.peakVoices() : 0);
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
