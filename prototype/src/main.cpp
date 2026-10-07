// Feel Lab: single-player movement + shooting prototype.
// Fixed 128 Hz simulation, uncapped rendering with interpolation, raw mouse input.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "audio.h"
#include "combat.h"
#include "config.h"
#include "fx.h"
#include "gl.h"
#include "movement.h"
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
    WeaponState rifle, knife;
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
    double hitMarkerUntil = 0;
    bool hitMarkerHead = false;
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
};

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
    in.jumpPressed = g.jumpLatch;
    g.jumpLatch = false;

    // Weapon switching / reload.
    if (g.switchTo) {
        WeaponState* target = g.switchTo == 1 ? &g.rifle : &g.knife;
        if (target != g.weapon) {
            g.weapon->reloadEndTime = -1;
            g.weapon = target;
            g.weapon->nextFireTime = std::max(g.weapon->nextFireTime, g.simTime + 0.25);  // draw time
            g.vm.onDraw(g.weapon == &g.rifle ? ViewWeapon::Rifle : ViewWeapon::Knife);
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
        g.hudDirty = true;
    }
    g.reloadLatch = false;
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

    bool autofire = opt.autofireStart >= 0 && g.simTime >= opt.autofireStart && g.simTime < opt.autofireEnd;
    bool wantFire = g.fireHeld || g.fireLatch || autofire;
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

        // Cosmetics: shot sound, weapon kick, tracer, impacts.
        sound(g, Sfx::RifleShot, 0.9f, 0.0f, 0.97f + float(ws.shotCounter % 7) * 0.01f);
        g.vm.onShot(ws.shotCounter * 2654435761u);
        g.fx.tracer(g.vm.muzzleWorld(g.lastRenderEye, float(g.viewPitch), float(g.viewYaw)), r.end);
        Vec3 shotDir = normalize(r.end - r.start);
        if (r.dummyIndex >= 0) {
            sound(g, r.group == kHead ? Sfx::HitHead : Sfx::HitBody, r.group == kHead ? 0.9f : 0.75f);
            g.fx.blood(r.end, shotDir);
        } else if (r.hitWorld) {
            g.fx.impact(r.end, r.normal, 0x5c6168);
        }

        if (r.dummyIndex >= 0) {
            g.hits++;
            if (r.group == kHead) g.headshots++;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s %d%s  %.0fM", hitGroupName(r.group), int(r.damage + 0.5f),
                          r.kill ? "  KILL" : "", r.distance * 0.0254f);
            pushHitLog(g, buf, r.group == kHead ? 0xff6060 : 0xffffff);
            g.hitMarkerUntil = g.simTime + 0.12;
            g.hitMarkerHead = r.group == kHead;
        } else if (r.hitWorld) {
            // Decal color follows the spray index (yellow first shot -> red late spray).
            float t = float(r.sprayIndex) / float(std::max(1, wd.patternLen - 1));
            uint32_t col = lerpColor(0xffe650, 0xe02828, t);
            Vec3 c = r.end + r.normal * 0.6f, h{1.4f, 1.4f, 1.4f};
            g.pendingDecals.push_back(makeBox(c - h, c + h, col, false));
        }
        if (ws.ammo == 0) {
            ws.reloadEndTime = g.simTime + wd.reloadTime;
            g.reloadStage = 0;
        }
        g.hudDirty = true;
    }
    if (!fired && !(wantFire && ws.ammo > 0 && ws.reloadEndTime < 0)) decayRecoil(ws, kTickDt);

    // Movement.
    g.prevPlayer = g.player;
    playerMove(g.player, in, float(g.viewYaw), wd.maxSpeed, g.world, g.moveParams);

    for (Dummy& d : g.dummies) updateDummy(d, kTickDt);

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

void buildHud(HudBatch& hud, const Game& g, const Config& cfg, const FrameStats& st, int w, int h, bool paused,
              bool showHelp) {
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
    if (cfg.crosshair_outline) crossRects(1, 0x000000C0);
    crossRects(0, xc);

    // Hit marker.
    if (g.simTime < g.hitMarkerUntil) {
        uint32_t hc = g.hitMarkerHead ? 0xFF4040FF : 0xFFFFFFFF;
        float a = gap + 4, b = gap + 10;
        hud.line(cx - a, cy - a, cx - b, cy - b, 2, hc);
        hud.line(cx + a, cy - a, cx + b, cy - b, 2, hc);
        hud.line(cx - a, cy + a, cx - b, cy + b, 2, hc);
        hud.line(cx + a, cy + a, cx + b, cy + b, 2, hc);
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
                  g.drill ? "   AIM DRILL ON" : "");
    hud.text(x, y, buf, g.drill ? 0xFFD060FF : 0xD0D0D0FF);
    y += lh * 1.5f;
    if (showHelp) {
        const char* help[] = {
            "WASD MOVE   SPACE/WHEEL JUMP   CTRL CROUCH   SHIFT WALK",
            "MOUSE1 FIRE   R RELOAD   1 RIFLE   3 KNIFE (FASTER)",
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
        hud.rect(0, 0, float(w), float(h), 0x00000080);
        const char* l1 = "PAUSED";
        const char* l2 = "CLICK OR ESC TO RESUME   Q TO QUIT";
        hud.text(cx - hud.textWidth(l1, s * 3) / 2, cy - 30.0f * s, l1, 0xFFFFFFFF, s * 3);
        hud.text(cx - hud.textWidth(l2) / 2, cy, l2, 0xFFFFFFFF);
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
    SDL_Window* window = SDL_CreateWindow("Feel Lab", winW, winH, flags);
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

    const bool automated = !opt.screenshotPath.empty();
    Game g;
    resetGame(g, opt);
    Audio audio;
    if (!automated && SDL_InitSubSystem(SDL_INIT_AUDIO) && audio.init(std::clamp(cfg.volume, 0.0f, 1.0f)))
        g.audio = &audio;
    else if (!automated)
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    {
        std::vector<BoxInstance> statics;
        for (const Box& b : g.world.solids) statics.push_back(makeBox(b.mins, b.maxs, b.color, true));
        renderer.setStaticBoxes(statics);
    }

    bool paused = false, showHelp = true, running = true;
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
        if (automated) dt = 1.0 / 240.0;  // deterministic steps for screenshots/tests
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
                        double dyaw = -double(e.motion.xrel) * cfg.sensitivity * cfg.m_yaw;
                        double dpitch = double(e.motion.yrel) * cfg.sensitivity * cfg.m_pitch;
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
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button == SDL_BUTTON_LEFT) g.fireHeld = false;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (!paused) g.jumpLatch = true;
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    if (e.key.repeat) break;
                    SDL_Scancode sc = e.key.scancode;
                    if (sc == SDL_SCANCODE_ESCAPE) setPaused(!paused);
                    else if (paused && sc == SDL_SCANCODE_Q) running = false;
                    else if (sc == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
                        bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                        SDL_SetWindowFullscreen(window, !fs);
                    } else if (!paused) {
                        if (sc == SDL_SCANCODE_SPACE) g.jumpLatch = true;
                        else if (sc == SDL_SCANCODE_R) g.reloadLatch = true;
                        else if (sc == SDL_SCANCODE_1) g.switchTo = 1;
                        else if (sc == SDL_SCANCODE_3) g.switchTo = 3;
                        else if (sc == SDL_SCANCODE_C) renderer.clearDecals();
                        else if (sc == SDL_SCANCODE_F1) { showHelp = !showHelp; g.hudDirty = true; }
                        else if (sc == SDL_SCANCODE_F3) { setDrill(g, !g.drill); g.hudDirty = true; }
                    }
                    break;
                }
                default: break;
            }
        }

        if (!paused) {
            tickAcc += dt;
            while (tickAcc >= kTickDt) {
                simTick(g, opt);
                tickAcc -= kTickDt;
            }
        }
        float alpha = float(tickAcc / kTickDt);

        for (const BoxInstance& d : g.pendingDecals) renderer.addDecal(d);
        g.pendingDecals.clear();

        // Camera: interpolated position, latest mouse angles, plus partial recoil view punch.
        Vec3 origin = lerp(g.prevPlayer.origin, g.player.origin, alpha);
        float eyeZ = eyeHeight(g.prevPlayer) + (eyeHeight(g.player) - eyeHeight(g.prevPlayer)) * alpha;
        Vec3 eye = origin + Vec3{0, 0, eyeZ};
        float recoilIdx = g.recoilIndexPrev + (g.weapon->recoilIndex - g.recoilIndexPrev) * alpha;
        RecoilStep punch = g.weapon->def->canFire ? recoilAt(*g.weapon->def, recoilIdx) : RecoilStep{0, 0};
        float camPitch = float(g.viewPitch) - punch.up * cfg.view_recoil_tracking;
        float camYaw = float(g.viewYaw) - punch.right * cfg.view_recoil_tracking;

        float aspect = pixH > 0 ? float(pixW) / float(pixH) : 1.0f;
        float vfov = 2.0f * std::atan(std::tan(cfg.fov * 0.5f * kDegToRad) * 0.75f);
        Mat4 viewProj = perspective(vfov, aspect, 2.0f, 16384.0f) * viewFromAngles(eye, camPitch, camYaw);

        // Dummies at their interpolated positions; remember exactly what we drew for hit tests.
        dynamicBoxes.clear();
        for (size_t i = 0; i < g.dummies.size(); ++i) {
            const Dummy& d = g.dummies[i];
            Vec3 p = lerp(d.prevPos, d.pos, alpha);
            g.lastDummyRenderPos[i] = p;
            // Dead dummies collapse to the floor (cosmetic; they are no longer hittable).
            float squash = 1.0f;
            if (!d.alive()) {
                float t = 1.0f - d.respawnLeft;
                if (t > 0.6f) continue;
                squash = std::max(0.06f, 1.0f - t / 0.22f);
            }
            for (const Hitbox& hb : dummyHitboxes()) {
                uint32_t base = hb.group == kHead ? 0xe0b48a : hb.group == kChest ? 0x9c3c3c
                              : hb.group == kStomach ? 0x86363a : 0x3e4450;
                uint32_t col = lerpColor(base, 0xffffff, std::min(1.0f, d.flash[hb.group] / 0.15f));
                Vec3 mn = hb.mins, mx = hb.maxs;
                mn.z *= squash;
                mx.z *= squash;
                dynamicBoxes.push_back(makeBox(p + mn, p + mx, col, false));
            }
        }
        g.lastRenderEye = eye;

        // Cosmetic updates at frame rate.
        float fdt = float(dt);
        g.fx.update(paused ? 0.0f : fdt);
        g.fx.appendParticles(dynamicBoxes);
        {
            const WeaponState& ws = *g.weapon;
            double reloadProgress =
                ws.reloadEndTime >= 0 ? g.simTime + tickAcc - (ws.reloadEndTime - ws.def->reloadTime) : -1.0;
            g.vm.update({paused ? 0.0f : fdt, frameYawDelta, framePitchDelta, length2d(g.player.velocity),
                         g.player.onGround, float(reloadProgress), ws.def->reloadTime});
        }

        renderer.beginFrame(pixW, pixH);
        renderer.drawBoxes(viewProj, eye, dynamicBoxes);
        modelDraws.clear();
        g.fx.appendTracers(modelDraws);
        for (const ModelDraw& md : modelDraws) renderer.drawModel(viewProj, md.model, md.boxes);

        // First-person weapon: own FOV and fresh depth so it never clips into walls.
        if (cfg.show_viewmodel) {
            float vmVfov = 2.0f * std::atan(std::tan(cfg.viewmodel_fov * 0.5f * kDegToRad) * 0.75f);
            Mat4 vmViewProj = perspective(vmVfov, aspect, 0.5f, 256.0f) * viewFromAngles(eye, camPitch, camYaw);
            modelDraws.clear();
            g.vm.build(eye, camPitch, camYaw, cfg.viewmodel_offset_x, cfg.viewmodel_offset_y, cfg.viewmodel_offset_z,
                       cfg.viewmodel_bob, modelDraws);
            renderer.clearDepth();
            for (const ModelDraw& md : modelDraws) renderer.drawModel(vmViewProj, md.model, md.boxes);
        }

        // HUD: rebuild at most ~60 Hz unless something changed (keeps uploads tiny at 1000+ FPS).
        double nowSec = double(frameStart) / freq;
        bool markerExpired = hitMarkerShownUntil > 0 && g.simTime >= g.hitMarkerUntil;
        bool rebuild = g.hudDirty || markerExpired || nowSec - lastHudBuild > 1.0 / 60.0;
        if (rebuild) {
            buildHud(hud, g, cfg, stats, pixW, pixH, paused, showHelp);
            lastHudBuild = nowSec;
            g.hudDirty = false;
            hitMarkerShownUntil = g.simTime < g.hitMarkerUntil ? g.hitMarkerUntil : 0;
        }
        renderer.drawHud(hud, rebuild);

        if (automated && frame == opt.screenshotFrame) {
            bool ok = renderer.screenshot(opt.screenshotPath);
            std::fprintf(stderr, "screenshot %s: %s (shots %d, hits %d)\n", opt.screenshotPath.c_str(),
                         ok ? "ok" : SDL_GetError(), g.shots, g.hits);
            running = false;
        }

        SDL_GL_SwapWindow(window);
        ++frame;

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
