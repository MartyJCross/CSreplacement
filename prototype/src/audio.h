// Tiny audio mixer on SDL3 with procedurally synthesized sounds (no asset files).
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
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
    // Positional: stereo pan + distance falloff relative to the listener (eye position + yaw).
    void play3D(Sfx s, const Vec3& pos, const Vec3& listener, float listenerYawDeg, float maxDist, float gain = 1.0f,
                float pitch = 1.0f);

    // Dev aid: writes every synthesized sound (each variant) as a 16-bit WAV into `dir`.
    static bool dumpWavs(const std::string& dir);
    void setVolume(float v) { master_ = v; }

private:
    struct Voice {
        int sound;
        double pos;
        float rate, gl, gr;
    };
    static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);
    float rand01();  // main-thread only (cosmetic variation)

    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::vector<float>> sounds_;  // every variant of every sound, mono, 48 kHz
    std::vector<int> first_, count_, last_;   // per Sfx: first variant index, how many, last played
    std::vector<Voice> voices_;               // only touched on the audio thread
    std::vector<Voice> pending_;              // guarded by mutex_
    std::vector<float> mixBuf_;
    std::mutex mutex_;
    std::atomic<float> master_{0.6f};
    int loaded_ = 0;  // sounds replaced by recordings from assets/
    uint32_t rng_ = 0x51ED270Bu;
};
