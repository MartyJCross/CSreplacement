#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace {

constexpr int kRate = 48000;
constexpr int kMaxVoices = 48;

struct Rng {
    uint32_t s = 0x12345678u;
    float noise() {  // -1..1
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(s) / 2147483648.0f - 1.0f;
    }
};

float lpCoef(float hz) { return 1.0f - std::exp(-2.0f * kPi * hz / kRate); }

std::vector<float> buffer(float seconds) { return std::vector<float>(size_t(seconds * kRate), 0.0f); }

// Filtered noise burst with exponential decay, added into `b` at time t0.
void addNoise(std::vector<float>& b, float t0, float amp, float lowHz, float highHz, float tau, Rng& rng,
              float attack = 0.0005f) {
    float lpHi = 0, lpLo = 0, aHi = lpCoef(highHz), aLo = lpCoef(lowHz);
    size_t start = size_t(t0 * kRate);
    for (size_t i = start; i < b.size(); ++i) {
        float t = float(i - start) / kRate;
        float env = std::min(1.0f, t / attack) * std::exp(-t / tau);
        if (env < 1e-4f && t > attack) break;
        float n = rng.noise();
        lpHi += aHi * (n - lpHi);    // remove content above highHz
        lpLo += aLo * (lpHi - lpLo); // ...and below lowHz
        b[i] += (lpHi - lpLo) * amp * env;
    }
}

// Decaying sine with optional pitch glide (thumps, rings).
void addTone(std::vector<float>& b, float t0, float amp, float f0, float f1, float tau) {
    size_t start = size_t(t0 * kRate);
    float phase = 0;
    for (size_t i = start; i < b.size(); ++i) {
        float t = float(i - start) / kRate;
        float env = std::exp(-t / tau);
        if (env < 1e-4f) break;
        float f = f1 + (f0 - f1) * std::exp(-t / (tau * 0.6f));
        phase += 2.0f * kPi * f / kRate;
        b[i] += std::sin(phase) * amp * env;
    }
}

void addClick(std::vector<float>& b, float t0, float amp, float tone, Rng& rng) {
    addNoise(b, t0, amp, 1500, 12000, 0.0025f, rng, 0.0001f);
    addTone(b, t0, amp * 0.5f, tone, tone, 0.006f);
}

void normalize(std::vector<float>& b, float peak) {
    float m = 0;
    for (float v : b) m = std::max(m, std::fabs(v));
    if (m > 0)
        for (float& v : b) v = std::tanh(v / m * 1.4f) / std::tanh(1.4f) * peak;
}

std::vector<std::vector<float>> synthesize() {
    Rng rng;
    std::vector<std::vector<float>> s(static_cast<size_t>(Sfx::Count));

    auto& shot = s[size_t(Sfx::RifleShot)] = buffer(0.55f);
    addNoise(shot, 0, 1.0f, 2500, 16000, 0.006f, rng, 0.0002f);  // supersonic crack
    addNoise(shot, 0, 0.9f, 150, 2500, 0.045f, rng, 0.0004f);    // muzzle blast body
    addNoise(shot, 0.004f, 0.35f, 60, 700, 0.17f, rng, 0.01f);   // room tail
    addTone(shot, 0, 0.9f, 130, 42, 0.05f);                      // low thump
    addClick(shot, 0.035f, 0.08f, 3200, rng);                    // action cycling
    normalize(shot, 0.95f);

    auto& dry = s[size_t(Sfx::DryFire)] = buffer(0.06f);
    addClick(dry, 0, 0.8f, 2600, rng);
    normalize(dry, 0.5f);

    auto& magOut = s[size_t(Sfx::MagOut)] = buffer(0.25f);
    addClick(magOut, 0, 0.7f, 1800, rng);
    addClick(magOut, 0.03f, 0.4f, 2400, rng);
    addNoise(magOut, 0.02f, 0.25f, 800, 6000, 0.05f, rng);
    normalize(magOut, 0.55f);

    auto& magIn = s[size_t(Sfx::MagIn)] = buffer(0.25f);
    addClick(magIn, 0, 0.6f, 1500, rng);
    addClick(magIn, 0.045f, 1.0f, 1200, rng);
    addTone(magIn, 0.045f, 0.4f, 180, 120, 0.03f);
    normalize(magIn, 0.65f);

    auto& bolt = s[size_t(Sfx::Bolt)] = buffer(0.3f);
    addClick(bolt, 0, 0.6f, 2200, rng);
    addNoise(bolt, 0.01f, 0.2f, 1500, 7000, 0.04f, rng);
    addClick(bolt, 0.13f, 1.0f, 1700, rng);
    addTone(bolt, 0.13f, 0.3f, 220, 150, 0.03f);
    normalize(bolt, 0.65f);

    auto& draw = s[size_t(Sfx::Draw)] = buffer(0.2f);
    addNoise(draw, 0, 0.3f, 600, 5000, 0.05f, rng, 0.01f);
    addClick(draw, 0.09f, 0.7f, 2000, rng);
    normalize(draw, 0.45f);

    auto& step = s[size_t(Sfx::Footstep)] = buffer(0.12f);
    addNoise(step, 0, 0.8f, 80, 900, 0.018f, rng, 0.001f);
    addTone(step, 0, 0.7f, 110, 60, 0.02f);
    addNoise(step, 0.008f, 0.25f, 1500, 6000, 0.01f, rng);  // grit
    normalize(step, 0.7f);

    auto& land = s[size_t(Sfx::Land)] = buffer(0.25f);
    addNoise(land, 0, 0.9f, 60, 600, 0.04f, rng, 0.001f);
    addTone(land, 0, 1.0f, 90, 45, 0.05f);
    addNoise(land, 0.01f, 0.25f, 1200, 5000, 0.02f, rng);
    normalize(land, 0.8f);

    auto& hitBody = s[size_t(Sfx::HitBody)] = buffer(0.12f);
    addNoise(hitBody, 0, 0.9f, 200, 1800, 0.014f, rng, 0.0003f);
    addTone(hitBody, 0, 0.6f, 190, 110, 0.025f);
    normalize(hitBody, 0.6f);

    // Headshot "dink": inharmonic metallic partials + a sharp transient.
    auto& hitHead = s[size_t(Sfx::HitHead)] = buffer(0.9f);
    addClick(hitHead, 0, 0.6f, 5000, rng);
    addTone(hitHead, 0, 0.55f, 2450, 2450, 0.16f);
    addTone(hitHead, 0, 0.35f, 3910, 3910, 0.11f);
    addTone(hitHead, 0, 0.22f, 5340, 5340, 0.07f);
    addTone(hitHead, 0, 0.15f, 1280, 1280, 0.09f);
    normalize(hitHead, 0.6f);

    return s;
}

}  // namespace

bool Audio::dumpWavs(const std::string& dir) {
    const char* names[] = {"rifle_shot", "dry_fire", "mag_out", "mag_in", "bolt", "draw",
                           "footstep", "land", "hit_body", "hit_head"};
    static_assert(sizeof(names) / sizeof(names[0]) == size_t(Sfx::Count), "name every sound");
    std::vector<std::vector<float>> all = synthesize();
    for (size_t i = 0; i < all.size(); ++i) {
        std::ofstream f(dir + "/" + names[i] + ".wav", std::ios::binary);
        if (!f) return false;
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        uint32_t bytes = uint32_t(all[i].size() * 2);
        f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
        u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
        f.write("data", 4); u32(bytes);
        for (float v : all[i]) u16(uint16_t(int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    }
    return true;
}

bool Audio::init(float masterVolume) {
    master_ = masterVolume;
    sounds_ = synthesize();
    voices_.reserve(kMaxVoices);
    pending_.reserve(kMaxVoices);
    mixBuf_.resize(4096 * 2);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::callback, this);
    if (!stream_) return false;
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Audio::shutdown() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

void Audio::play(Sfx s, float gain, float pan, float pitch) {
    if (!stream_ || gain <= 0.001f) return;
    pan = std::clamp(pan, -1.0f, 1.0f);
    float a = (pan + 1.0f) * kPi * 0.25f;  // equal-power pan
    Voice v{int(s), 0.0, pitch, std::cos(a) * gain * 1.4142f, std::sin(a) * gain * 1.4142f};
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() < kMaxVoices) pending_.push_back(v);
}

void Audio::play3D(Sfx s, const Vec3& pos, const Vec3& listener, float listenerYawDeg, float maxDist, float gain,
                   float pitch) {
    Vec3 d = pos - listener;
    float dist = length(d);
    if (dist >= maxDist) return;
    float falloff = 1.0f - dist / maxDist;
    falloff *= falloff;
    Vec3 dir = dist > 1e-3f ? d * (1.0f / dist) : Vec3{};
    float y = listenerYawDeg * kDegToRad;
    Vec3 right{std::sin(y), -std::cos(y), 0}, fwd{std::cos(y), std::sin(y), 0};
    float pan = dot(dir, right);
    float behind = dot(dir, fwd) < 0 ? 0.75f : 1.0f;  // crude front/back cue
    play(s, gain * falloff * behind, pan, pitch);
}

void SDLCALL Audio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<Audio*>(user);
    int frames = additional / int(sizeof(float) * 2);
    while (frames > 0) {
        int n = std::min(frames, int(self->mixBuf_.size() / 2));
        self->mix(self->mixBuf_.data(), n);
        SDL_PutAudioStreamData(stream, self->mixBuf_.data(), n * int(sizeof(float) * 2));
        frames -= n;
    }
}

void Audio::mix(float* out, int frames) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Voice& v : pending_) {
            if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());  // steal the oldest
            voices_.push_back(v);
        }
        pending_.clear();
    }
    std::fill(out, out + frames * 2, 0.0f);
    for (Voice& v : voices_) {
        const std::vector<float>& snd = sounds_[size_t(v.sound)];
        const double last = double(snd.size() - 1);
        for (int i = 0; i < frames && v.pos < last; ++i) {
            size_t i0 = size_t(v.pos);
            float f = float(v.pos - double(i0));
            float smp = snd[i0] + (snd[i0 + 1] - snd[i0]) * f;
            out[i * 2] += smp * v.gl;
            out[i * 2 + 1] += smp * v.gr;
            v.pos += v.rate;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.pos >= double(sounds_[size_t(v.sound)].size() - 1); }),
                  voices_.end());
    for (int i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * master_);  // soft limiter
}
