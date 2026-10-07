// Tiny audio mixer on SDL3 with procedurally synthesized sounds (no asset files).
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "vecmath.h"

enum class Sfx { RifleShot, DryFire, MagOut, MagIn, Bolt, Draw, Footstep, Land, HitBody, HitHead, Count };

class Audio {
public:
    bool init(float masterVolume);
    void shutdown();

    // pan: -1 left .. +1 right. pitch: playback-rate multiplier.
    void play(Sfx s, float gain = 1.0f, float pan = 0.0f, float pitch = 1.0f);
    // Positional: stereo pan + distance falloff relative to the listener (eye position + yaw).
    void play3D(Sfx s, const Vec3& pos, const Vec3& listener, float listenerYawDeg, float maxDist, float gain = 1.0f,
                float pitch = 1.0f);

    // Dev aid: writes every synthesized sound as a 16-bit WAV into `dir` (no audio device needed).
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

    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::vector<float>> sounds_;  // mono, 48 kHz
    std::vector<Voice> voices_;               // only touched on the audio thread
    std::vector<Voice> pending_;              // guarded by mutex_
    std::vector<float> mixBuf_;
    std::mutex mutex_;
    std::atomic<float> master_{0.6f};
};
