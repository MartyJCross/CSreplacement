// Tiny audio mixer on SDL3 with procedurally synthesized sounds (no asset files).
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "vecmath.h"

enum class Sfx {
    RifleShot, DryFire, MagOut, MagIn, Bolt, Draw, Footstep, Land, HitBody, HitHead, SniperShot,
    PistolShot, RifleShotFar,  // far = a rifle heard from a distance (muffled, more echo)
    HitMarker,                 // crisp tick when your bullet connects
    FootstepWood, FootstepMetal,  // steps on crates/doors and on the container/car
    FlashBang, FlashRing, Explosion, Fire,  // grenades: flash pop, your ears ringing, HE, molotov crackle
    BombBeep, Defuse,                       // the planted bomb's beep; the defuse kit clicking on
    ImpactStone, ImpactWood, ImpactMetal,   // your bullet hitting the world, by material
    HelmetHit,                              // a headshot stopped by a helmet: a bright "tink"
    Whiz,                                   // a bot's bullet passing close to your head
    UiClick,                                // menu clicks
    SuppressedShot,                         // the starting pistol's silencer: a muted "thwip" and the slide
    Zoom,                                   // a sniper scoping in: a soft mechanical "chk"
    SuppressedRifle,                        // the M4A1-S: the movie "thwip" with a rifle's punch
    ShotgunShot,                            // the Nova
    // Every other gun its own recording (without one: a shared sound, pitched; see loadBank).
    GalilShot, Mac10Shot, UmpShot, SsgShot, XmShot, DeagleShot, BerettasShot,
    RoundStart, BombPlanted, RoundWin, RoundLose,  // the match's cues: "go", the alarm, the stings
    LowAmmo, LastRound,                     // your magazine: nearly empty (a tick), the last round (a ping)
    Count
};

class Audio {
public:
    // Synthesizes every sound, then swaps in recordings found in `assetDir` (see audio.cpp).
    bool init(float masterVolume, const std::string& assetDir = "");
    int loadedFromAssets() const { return loaded_; }
    void shutdown();

    // pan: -1 left .. +1 right. pitch: playback-rate multiplier. Every play picks one of the sound's
    // variants (never the same one twice in a row) with a touch of random pitch and volume.
    void play(Sfx s, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f);
    // Positional, relative to the listener (eye position + yaw): distance falloff, and where it is, the way ears
    // hear it: the far ear a fraction of a millisecond later and duller (head shadow), from behind duller still,
    // from below darker and from above brighter, and muffled through walls (the occlusion test, if set).
    void play3D(Sfx s, const Vec3& pos, const Vec3& listener, float listenerYawDeg, float maxDist, float gain = 1.0f,
                float pitch = 1.0f);
    // How blocked the way from the listener to a sound is, 0 (clear) .. 1 (behind walls). Called on the main
    // thread from play3D.
    using OcclusionFn = float (*)(const void* ctx, const Vec3& listener, const Vec3& source);
    void setOcclusion(OcclusionFn fn, const void* ctx) { occlusion_ = fn; occlusionCtx_ = ctx; }

    // Dev aid: writes every synthesized sound (each variant) as a 16-bit WAV into `dir`; with `assetDir`,
    // what the game really plays (recordings swapped in).
    static bool dumpWavs(const std::string& dir, const std::string& assetDir = "");
    // Dev aid: a footstep from each direction (and through a wall), rendered like play3D, as stereo WAVs.
    static bool dumpSpatial(const std::string& dir, const std::string& assetDir = "");
    void setVolume(float v) { master_ = v; }
    // How busy the mixer thread is: the share of real time it spends mixing (0.01 = 1% of one core), and the most
    // voices it has mixed at once. For --bench.
    double mixLoad() const {
        const double audio = double(mixedFrames_.load()) / 48000.0;
        return audio > 0 ? double(mixTicks_.load()) / double(SDL_GetPerformanceFrequency()) / audio : 0.0;
    }
    int peakVoices() const { return peakVoices_.load(); }

    // Music: an Ogg/WAV decoded on a background thread (startup isn't held up), then looped. setMusic() sets
    // the level it fades to over about a second (0 = silent; it keeps its place while silent).
    void loadMusic(const std::string& path);
    void setMusic(float gain) { musicTarget_ = gain; }

private:
    struct Voice {
        int sound;
        double pos;
        float rate, gl, gr;
        // 3D: each ear reads the sound `delay` samples late (the far ear: up to ~0.65 ms) and keeps `hi` of what's
        // above the split frequency (`split`: the one-pole filter's coefficient). 2D sounds: no delay, hi = 1.
        float delayL = 0, delayR = 0, split = 0, hiL = 1, hiR = 1;
        float lpL = 0, lpR = 0;
    };
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);
    static void mixVoice(Voice& v, const std::vector<float>& snd, float* out, int frames);
    float rand01();  // main-thread only (cosmetic variation)

    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::vector<float>> sounds_;  // every variant of every sound, mono, 48 kHz
    std::vector<int> first_, count_, last_;   // per Sfx: first variant index, how many, last played
    std::vector<Voice> voices_;               // only touched on the audio thread
    std::vector<Voice> pending_;              // guarded by mutex_
    std::vector<float> mixBuf_;
    std::mutex mutex_;
    std::atomic<float> master_{0.6f};
    std::atomic<uint64_t> mixTicks_{0}, mixedFrames_{0};
    std::atomic<int> peakVoices_{0};
    int loaded_ = 0;  // sounds replaced by recordings from assets/
    std::vector<float> music_;           // stereo, interleaved, 48 kHz (written by the loader thread)
    std::atomic<bool> musicReady_{false};
    std::atomic<float> musicTarget_{0.0f};
    float musicGain_ = 0;                // audio thread only
    size_t musicPos_ = 0;                // frames, audio thread only
    std::thread musicLoader_;
    OcclusionFn occlusion_ = nullptr;
    const void* occlusionCtx_ = nullptr;
    void start(Sfx s, float gain, float pan, float pitch, float delayL, float delayR, float splitHz, float hiL, float hiR);
    uint32_t rng_ = 0x51ED270Bu;
};
