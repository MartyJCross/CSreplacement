#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"  // Ogg Vorbis decoding (public domain), compiled in crisp_third_party

namespace {

constexpr int kRate = 48000;
constexpr int kMaxVoices = 48;

struct Rng {
    uint32_t s = 0x12345678u;
    float noise() {  // -1..1
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(s) / 2147483648.0f - 1.0f;
    }
    float jitter(float amount) { return 1.0f + noise() * amount; }  // 1 +- amount
};

float lpCoef(float hz) { return 1.0f - std::exp(-2.0f * kPi * hz / kRate); }

std::vector<float> buffer(float seconds) { return std::vector<float>(size_t(seconds * kRate), 0.0f); }

// Band-limited noise burst with exponential decay, added into `b` at time t0.
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

// Decaying sine with a pitch glide from f0 to f1 (thumps, booms, rings).
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

// Struck metal: a cluster of inharmonic partials (ratios of a struck bar) plus a short click.
void addMetal(std::vector<float>& b, float t0, float amp, float baseHz, float tau, Rng& rng) {
    const float ratios[4] = {1.0f, 2.76f, 5.40f, 8.93f}, gains[4] = {1.0f, 0.6f, 0.35f, 0.18f};
    for (int k = 0; k < 4; ++k)
        addTone(b, t0, amp * gains[k] * 0.5f, baseHz * ratios[k], baseHz * ratios[k], tau / (1.0f + float(k) * 0.7f));
    addNoise(b, t0, amp * 0.7f, 2000, 14000, 0.0018f, rng, 0.0001f);
}

// Sparse crunchy grains (sand / grit under a boot).
void addGrit(std::vector<float>& b, float t0, float len, float amp, Rng& rng) {
    for (int k = 0; k < 7; ++k) {
        float t = t0 + (rng.noise() * 0.5f + 0.5f) * len;
        addNoise(b, t, amp * (0.5f + 0.5f * std::fabs(rng.noise())), 1800, 7500, 0.0022f, rng, 0.0002f);
    }
}

// Soft clip for weight: rounds off peaks and adds harmonics the ear reads as "punch".
void saturate(std::vector<float>& b, float drive) {
    float m = 0;
    for (float v : b) m = std::max(m, std::fabs(v));
    if (m <= 0) return;
    for (float& v : b) v = std::tanh(v / m * drive);
}

void lowpass(std::vector<float>& b, float hz) {
    float a = lpCoef(hz), y = 0;
    for (float& v : b) { y += a * (v - y); v = y; }
}

// Cuts sub-bass rumble below `hz` (the "bass turned up too high" part).
void highpass(std::vector<float>& b, float hz) {
    float a = lpCoef(hz), y = 0;
    for (float& v : b) { y += a * (v - y); v -= y; }
}

// Outdoor slapback: a handful of darker, decaying reflections off the walls (cheap "space").
void addReflections(std::vector<float>& b, float firstMs, float spreadMs, int taps, float gain, float decay,
                    float darkHz, Rng& rng) {
    std::vector<float> dry = b, wet(b.size(), 0.0f);
    lowpass(dry, darkHz);
    float g = gain;
    for (int k = 0; k < taps; ++k) {
        float ms = firstMs + spreadMs * (float(k) + 0.5f * (rng.noise() * 0.5f + 0.5f));
        size_t d = size_t(ms * 0.001f * kRate);
        for (size_t i = d; i < b.size(); ++i) wet[i] += dry[i - d] * g;
        g *= decay;
    }
    for (size_t i = 0; i < b.size(); ++i) b[i] += wet[i];
}

void normalize(std::vector<float>& b, float peak) {
    float m = 0;
    for (float v : b) m = std::max(m, std::fabs(v));
    if (m > 0)
        for (float& v : b) v = std::tanh(v / m * 1.4f) / std::tanh(1.4f) * peak;
}

// Fade the last few ms so nothing ends with a click.
void fadeTail(std::vector<float>& b) {
    size_t n = std::min(b.size(), size_t(0.01f * kRate));
    for (size_t i = 0; i < n; ++i) b[b.size() - 1 - i] *= float(i) / float(n);
}

// ---- The sounds. Each call jitters a few parameters, so every variant comes out a bit different. ----

std::vector<float> rifleShot(Rng& r) {
    auto b = buffer(0.75f);
    addNoise(b, 0, 0.95f * r.jitter(0.1f), 1800, 11000, 0.0045f, r, 0.0002f);         // crack
    addNoise(b, 0, 1.0f, 120, 1800 * r.jitter(0.08f), 0.035f * r.jitter(0.1f), r, 0.0003f);  // blast body
    addNoise(b, 0.001f, 0.9f, 400, 1600, 0.02f, r);                                    // mid bark
    addTone(b, 0, 0.4f, 112 * r.jitter(0.05f), 52, 0.05f * r.jitter(0.1f));            // boom
    addTone(b, 0, 0.1f, 70, 42, 0.06f);                                                // chest punch
    addTone(b, 0, 0.22f, 190 * r.jitter(0.05f), 95, 0.022f);                           // thump (low-mid, short)
    addMetal(b, 0.028f * r.jitter(0.1f), 0.10f, 1900 * r.jitter(0.06f), 0.03f, r);    // bolt carrier
    highpass(b, 70);
    saturate(b, 2.6f);  // denser: louder at the same peak
    addReflections(b, 55, 45, 5, 0.24f, 0.6f, 4000, r);
    fadeTail(b);
    normalize(b, 0.95f);
    return b;
}

std::vector<float> rifleShotFar(Rng& r) {  // distant: no crack, dark, mostly echo
    auto b = buffer(1.1f);
    addNoise(b, 0, 1.0f, 90, 900, 0.05f * r.jitter(0.1f), r, 0.002f);
    addTone(b, 0, 0.8f, 80 * r.jitter(0.05f), 40, 0.09f);
    highpass(b, 60);
    saturate(b, 1.3f);
    addReflections(b, 90, 90, 6, 0.4f, 0.68f, 1400, r);
    lowpass(b, 2000);
    fadeTail(b);
    normalize(b, 0.85f);
    return b;
}

std::vector<float> pistolShot(Rng& r) {  // snappier and lighter than the rifle
    auto b = buffer(0.55f);
    addNoise(b, 0, 0.9f * r.jitter(0.1f), 2000, 10000, 0.003f, r, 0.0002f);
    addNoise(b, 0, 1.0f, 250, 2600 * r.jitter(0.08f), 0.022f * r.jitter(0.1f), r, 0.0003f);
    addTone(b, 0, 0.18f, 230 * r.jitter(0.05f), 120, 0.016f);  // thump
    addTone(b, 0, 0.22f, 150 * r.jitter(0.05f), 70, 0.03f);
    addMetal(b, 0.018f, 0.12f, 2600 * r.jitter(0.06f), 0.025f, r);  // slide
    highpass(b, 90);
    saturate(b, 2.2f);  // denser: louder at the same peak
    addReflections(b, 50, 40, 4, 0.18f, 0.6f, 5500, r);
    fadeTail(b);
    normalize(b, 0.9f);
    return b;
}

// The suppressed pistol, like a spy film's: a "thwump" of gas out of the can for the body (150-1500 Hz),
// the bright "pfft" spit and a quick falling "zip" on top, the slide cycling with a little steel ring, a
// little room.
std::vector<float> suppressedShot(Rng& r) {
    auto b = buffer(0.32f);
    addNoise(b, 0, 1.0f, 160, 1300 * r.jitter(0.08f), 0.024f * r.jitter(0.12f), r, 0.0006f);  // the thwump
    addTone(b, 0, 0.16f, 150 * r.jitter(0.06f), 95, 0.025f);                                   // body
    addTone(b, 0, 0.22f, 260 * r.jitter(0.05f), 150, 0.012f);                                  // punch
    addNoise(b, 0.0003f, 0.42f, 1500, 5500, 0.005f, r, 0.0002f);                               // pfft
    addTone(b, 0, 0.2f, 3400 * r.jitter(0.05f), 1500, 0.006f);                                 // zip
    addNoise(b, 0.012f * r.jitter(0.1f), 0.2f, 300, 1600, 0.006f, r, 0.0002f);                 // slide back
    addMetal(b, 0.013f, 0.1f, 2400 * r.jitter(0.06f), 0.012f, r);
    addNoise(b, 0.05f * r.jitter(0.1f), 0.14f, 300, 1400, 0.005f, r, 0.0002f);                 // and home
    highpass(b, 100);
    saturate(b, 1.8f);
    lowpass(b, 7500);
    addReflections(b, 35, 30, 4, 0.14f, 0.55f, 2500, r);
    fadeTail(b);
    normalize(b, 0.8f);
    return b;
}

// The suppressed guns, the way a suppressor really changes a shot. A gunshot's energy is mostly the muzzle blast,
// below 500 Hz, and that blast is the one part a suppressor takes away. What's left: the high "fizzle" of gas
// leaving the can, the bullet's crack if it's supersonic (the M4A1-S's 5.56; the USP's .45 is subsonic, so none),
// and the action cycling, which suddenly stands out. So: only the top of the AK recording (its blast cut below
// `cut`), a short breathy noise fizzle, the crack for the rifle, the action as two dull noise clacks, a little room.
// Nothing tonal (v0.16's metal ring and "zip" sounded like tapping glass) and no body (v0.19 was "way too thumpy").
struct Silencer {
    float cut, tail, report, fizz, fizzLo, fizzHi, fizzTail, crack, clack, room;
};
constexpr Silencer kUspS = {700, 30, 1.0f, 0.5f, 1400, 5000, 0.012f, 0.0f, 0.4f, 0.12f};
constexpr Silencer kM4A1S = {600, 40, 1.0f, 0.45f, 1200, 5500, 0.016f, 0.6f, 0.35f, 0.16f};

std::vector<float> suppressedFrom(const std::vector<float>& shot, const Silencer& t, bool rifle, Rng& r) {
    float peak = 0;
    for (float v : shot) peak = std::max(peak, std::fabs(v));
    auto b = buffer(rifle ? 0.3f : 0.25f);
    if (peak > 0) {  // what's left of the report: the AK's top end, short
        size_t onset = 0;
        while (onset < shot.size() && std::fabs(shot[onset]) < peak * 0.15f) ++onset;
        onset = onset > 24 ? onset - 24 : 0;
        const float tail = t.tail * 0.001f;
        for (size_t i = 0; i < b.size() && onset + i < shot.size(); ++i)
            b[i] = shot[onset + i] / peak * std::exp(-float(i) / kRate / tail);
        for (int k = 0; k < 3; ++k) highpass(b, t.cut);  // the blast gone (three poles)
        lowpass(b, 7000);
        normalize(b, t.report);
    }
    addNoise(b, 0.0002f, t.fizz, t.fizzLo * r.jitter(0.08f), t.fizzHi, t.fizzTail * r.jitter(0.12f), r, 0.0004f);  // fizzle
    if (t.crack > 0) addNoise(b, 0, t.crack, 2000, 12000, 0.0012f, r, 0.00005f);  // the bullet's crack
    addNoise(b, (rifle ? 0.02f : 0.013f) * r.jitter(0.1f), t.clack, 700, 3500, 0.0035f, r, 0.0002f);  // the action back...
    addNoise(b, (rifle ? 0.06f : 0.045f) * r.jitter(0.1f), t.clack * 0.7f, 600, 3000, 0.0035f, r, 0.0002f);  // ...and home
    highpass(b, 350);  // nothing low left anywhere
    saturate(b, 1.4f);
    addReflections(b, rifle ? 40.0f : 30.0f, rifle ? 35.0f : 28.0f, 4, t.room, 0.5f, 3500, r);
    fadeTail(b);
    normalize(b, rifle ? 0.85f : 0.8f);
    return b;
}

// The M4A1-S: the same spy-film "thwip" on top, but a rifle underneath: more gas, a deeper thump, the bolt
// carrier slamming, a longer tail. (Only a stand-in: the game builds it with suppressedFrom.)
std::vector<float> suppressedRifle(Rng& r) {
    auto b = buffer(0.45f);
    addNoise(b, 0, 1.0f, 170, 1500 * r.jitter(0.08f), 0.034f * r.jitter(0.12f), r, 0.0005f);  // the thwump
    addTone(b, 0, 0.3f, 150 * r.jitter(0.06f), 80, 0.04f);                                     // oomph
    addTone(b, 0, 0.25f, 230 * r.jitter(0.05f), 130, 0.014f);                                  // punch
    addNoise(b, 0.0003f, 0.38f, 1400, 5000, 0.006f, r, 0.0002f);                               // pfft
    addTone(b, 0, 0.16f, 3000 * r.jitter(0.05f), 1300, 0.007f);                                // zip
    addMetal(b, 0.02f * r.jitter(0.1f), 0.1f, 1900 * r.jitter(0.06f), 0.02f, r);                 // bolt carrier
    highpass(b, 80);
    saturate(b, 2.0f);
    lowpass(b, 7000);
    addReflections(b, 45, 40, 5, 0.2f, 0.6f, 2500, r);
    fadeTail(b);
    normalize(b, 0.9f);
    return b;
}

// A 12 gauge: a huge low blast and a long rolling echo (the recordings replace it).
std::vector<float> shotgunShot(Rng& r) {
    auto b = buffer(1.2f);
    addNoise(b, 0, 0.7f, 1800, 9000, 0.005f, r, 0.0002f);
    addNoise(b, 0, 1.0f, 70, 1400 * r.jitter(0.08f), 0.07f * r.jitter(0.1f), r, 0.0004f);
    addTone(b, 0, 0.45f, 85 * r.jitter(0.05f), 38, 0.09f);
    highpass(b, 50);
    saturate(b, 2.0f);
    addReflections(b, 70, 80, 6, 0.3f, 0.65f, 3000, r);
    fadeTail(b);
    normalize(b, 0.98f);
    return b;
}

// A sniper scoping in: a short, soft mechanical "chk" and a breath of air as the lens slides (not a click).
std::vector<float> zoomSound(Rng& r) {
    auto b = buffer(0.09f);
    addNoise(b, 0, 0.6f, 400, 2600 * r.jitter(0.1f), 0.006f, r, 0.0004f);
    addTone(b, 0, 0.25f, 520 * r.jitter(0.05f), 380, 0.008f);
    addNoise(b, 0.012f, 0.3f, 900, 5000, 0.012f, r, 0.004f);
    lowpass(b, 6000);
    fadeTail(b);
    normalize(b, 0.45f);
    return b;
}

std::vector<float> sniperShot(Rng& r) {  // big: heavy boom, long rolling echo
    auto b = buffer(1.5f);
    addNoise(b, 0, 0.8f, 2200, 12000, 0.005f, r, 0.0002f);
    addNoise(b, 0, 1.0f, 90, 1600 * r.jitter(0.08f), 0.07f * r.jitter(0.1f), r, 0.0004f);
    addNoise(b, 0.001f, 0.8f, 300, 1400, 0.03f, r);
    addTone(b, 0, 0.4f, 95 * r.jitter(0.05f), 40, 0.08f);
    addTone(b, 0, 0.12f, 55, 32, 0.12f);
    highpass(b, 60);
    saturate(b, 1.8f);
    addReflections(b, 80, 85, 6, 0.3f, 0.66f, 3500, r);
    fadeTail(b);
    normalize(b, 0.98f);
    return b;
}

std::vector<float> footstep(Rng& r) {  // boot on sand/stone: heel, roll, toe scuff, grit
    auto b = buffer(0.2f);
    float toe = 0.045f * r.jitter(0.25f);
    addNoise(b, 0, 0.9f, 140 * r.jitter(0.15f), 2200 * r.jitter(0.15f), 0.011f * r.jitter(0.2f), r, 0.0008f);
    addTone(b, 0, 0.3f, 85 * r.jitter(0.1f), 55, 0.012f);
    addNoise(b, toe, 0.5f * r.jitter(0.25f), 500, 4500 * r.jitter(0.15f), 0.014f, r, 0.001f);
    addGrit(b, 0.002f, toe + 0.03f, 0.18f, r);
    lowpass(b, 6500);
    fadeTail(b);
    normalize(b, 0.7f);
    return b;
}

std::vector<float> landing(Rng& r) {  // thud, grit and a little gear rattle
    auto b = buffer(0.3f);
    addTone(b, 0, 0.3f, 80 * r.jitter(0.08f), 48, 0.04f);
    addNoise(b, 0, 0.9f, 120, 1200, 0.04f, r, 0.001f);
    addGrit(b, 0.004f, 0.05f, 0.3f, r);
    addMetal(b, 0.02f * r.jitter(0.2f), 0.06f, 1500 * r.jitter(0.1f), 0.02f, r);
    addMetal(b, 0.05f * r.jitter(0.2f), 0.04f, 1900 * r.jitter(0.1f), 0.02f, r);
    fadeTail(b);
    normalize(b, 0.8f);
    return b;
}

std::vector<float> hitBody(Rng& r) {  // dull, meaty thwack
    auto b = buffer(0.15f);
    addNoise(b, 0, 1.0f, 300, 3000 * r.jitter(0.1f), 0.012f * r.jitter(0.15f), r, 0.0003f);
    addTone(b, 0, 0.3f, 165 * r.jitter(0.08f), 90, 0.022f);
    addNoise(b, 0.005f, 0.45f, 700, 2000, 0.03f, r);
    saturate(b, 1.8f);
    fadeTail(b);
    normalize(b, 0.62f);
    return b;
}

std::vector<float> hitHead(Rng& r) {  // helmet "dink": bright ring over a thwack
    auto b = buffer(0.6f);
    addMetal(b, 0, 0.9f, 3050 * r.jitter(0.04f), 0.12f, r);
    addTone(b, 0, 0.25f, 1450 * r.jitter(0.04f), 1450, 0.07f);
    addNoise(b, 0, 0.5f, 300, 2500, 0.01f, r, 0.0003f);
    fadeTail(b);
    normalize(b, 0.6f);
    return b;
}

std::vector<float> dryFire(Rng& r) {
    auto b = buffer(0.08f);
    addMetal(b, 0, 0.8f, 2300 * r.jitter(0.06f), 0.012f, r);
    fadeTail(b);
    normalize(b, 0.5f);
    return b;
}

std::vector<float> magOut(Rng& r) {
    auto b = buffer(0.28f);
    addMetal(b, 0, 0.7f, 1500 * r.jitter(0.06f), 0.02f, r);
    addMetal(b, 0.035f, 0.4f, 2100 * r.jitter(0.06f), 0.015f, r);
    addNoise(b, 0.02f, 0.25f, 600, 5000, 0.05f, r, 0.004f);  // mag sliding out
    fadeTail(b);
    normalize(b, 0.55f);
    return b;
}

std::vector<float> magIn(Rng& r) {
    auto b = buffer(0.28f);
    addNoise(b, 0, 0.2f, 600, 4000, 0.03f, r, 0.004f);
    addMetal(b, 0.04f, 1.0f, 1200 * r.jitter(0.06f), 0.03f, r);  // seat
    addTone(b, 0.04f, 0.4f, 170, 110, 0.03f);
    fadeTail(b);
    normalize(b, 0.65f);
    return b;
}

std::vector<float> bolt(Rng& r) {
    auto b = buffer(0.32f);
    addMetal(b, 0, 0.6f, 1700 * r.jitter(0.06f), 0.025f, r);    // back
    addNoise(b, 0.01f, 0.2f, 1500, 7000, 0.04f, r);
    addMetal(b, 0.13f, 1.0f, 1350 * r.jitter(0.06f), 0.035f, r); // forward
    addTone(b, 0.13f, 0.3f, 210, 150, 0.03f);
    fadeTail(b);
    normalize(b, 0.65f);
    return b;
}

std::vector<float> drawSound(Rng& r) {
    auto b = buffer(0.22f);
    addNoise(b, 0, 0.3f, 500, 4500, 0.05f, r, 0.01f);  // cloth / holster
    addMetal(b, 0.09f, 0.6f, 1800 * r.jitter(0.08f), 0.02f, r);
    fadeTail(b);
    normalize(b, 0.45f);
    return b;
}

std::vector<float> hitMarker(Rng& r) {  // short, dry, bright tick: reads instantly over gunfire
    auto b = buffer(0.06f);
    addTone(b, 0, 0.8f, 3200 * r.jitter(0.03f), 2900, 0.007f);
    addTone(b, 0, 0.4f, 1600 * r.jitter(0.03f), 1500, 0.006f);
    addNoise(b, 0, 0.5f, 3000, 12000, 0.0015f, r, 0.0001f);
    fadeTail(b);
    normalize(b, 0.55f);
    return b;
}

std::vector<float> footstepWood(Rng& r) {  // hollow knock of a boot on planks
    auto b = buffer(0.2f);
    addTone(b, 0, 0.7f, 210 * r.jitter(0.1f), 170, 0.03f);
    addTone(b, 0, 0.35f, 410 * r.jitter(0.1f), 380, 0.02f);
    addNoise(b, 0, 0.6f, 300, 3000, 0.012f, r, 0.0006f);
    addNoise(b, 0.05f * r.jitter(0.2f), 0.35f, 600, 4000, 0.01f, r, 0.0008f);  // toe
    fadeTail(b);
    normalize(b, 0.7f);
    return b;
}

std::vector<float> footstepMetal(Rng& r) {  // boot on sheet metal: a short clang
    auto b = buffer(0.3f);
    addMetal(b, 0, 0.6f, 620 * r.jitter(0.08f), 0.07f, r);
    addNoise(b, 0, 0.6f, 200, 2500, 0.012f, r, 0.0006f);
    addMetal(b, 0.05f * r.jitter(0.2f), 0.3f, 780 * r.jitter(0.08f), 0.05f, r);
    fadeTail(b);
    normalize(b, 0.7f);
    return b;
}

std::vector<float> flashBang(Rng& r) {  // sharp, bright pop
    auto b = buffer(0.6f);
    addNoise(b, 0, 1.0f, 1500, 14000, 0.012f, r, 0.0002f);
    addNoise(b, 0, 0.7f, 200, 2500, 0.04f, r, 0.0004f);
    addTone(b, 0, 0.3f, 180 * r.jitter(0.05f), 90, 0.04f);
    highpass(b, 120);
    addReflections(b, 50, 50, 4, 0.25f, 0.6f, 5000, r);
    fadeTail(b);
    normalize(b, 0.95f);
    return b;
}

std::vector<float> flashRing(Rng& r) {  // tinnitus: a high whine with a slow beat, fading over 3 s
    auto b = buffer(3.0f);
    float f = 3600.0f * r.jitter(0.03f);
    for (size_t i = 0; i < b.size(); ++i) {
        float t = float(i) / kRate;
        float env = std::min(1.0f, t / 0.05f) * std::exp(-t / 1.1f);
        b[i] = (std::sin(2.0f * kPi * f * t) + 0.6f * std::sin(2.0f * kPi * (f + 7.0f) * t)) * env;
    }
    fadeTail(b);
    normalize(b, 0.35f);
    return b;
}

std::vector<float> explosion(Rng& r) {  // HE: crack, big boom, rumble and debris
    auto b = buffer(1.6f);
    addNoise(b, 0, 0.9f, 1200, 10000, 0.01f, r, 0.0002f);
    addNoise(b, 0, 1.0f, 60, 1500, 0.12f * r.jitter(0.1f), r, 0.001f);
    addTone(b, 0, 0.8f, 75 * r.jitter(0.05f), 32, 0.18f);
    addGrit(b, 0.05f, 0.4f, 0.25f, r);
    highpass(b, 40);
    saturate(b, 2.0f);
    addReflections(b, 80, 90, 6, 0.35f, 0.66f, 2500, r);
    fadeTail(b);
    normalize(b, 0.98f);
    return b;
}

std::vector<float> fireCrackle(Rng& r) {  // molotov fire: a short burst of crackles over a roar
    auto b = buffer(0.45f);
    addNoise(b, 0, 0.35f, 120, 1200, 0.3f, r, 0.05f);
    for (int k = 0; k < 9; ++k)
        addNoise(b, (r.noise() * 0.5f + 0.5f) * 0.4f, 0.6f * std::fabs(r.noise()), 1500, 9000, 0.004f, r, 0.0002f);
    fadeTail(b);
    normalize(b, 0.6f);
    return b;
}

std::vector<float> bombBeep(Rng&) {  // the C4 chirp
    auto b = buffer(0.12f);
    addTone(b, 0, 0.8f, 2650, 2650, 0.04f);
    addTone(b, 0, 0.3f, 5300, 5300, 0.02f);
    fadeTail(b);
    normalize(b, 0.5f);
    return b;
}

std::vector<float> defuseKit(Rng& r) {  // kit clicks onto the bomb
    auto b = buffer(0.35f);
    addMetal(b, 0, 0.7f, 1600, 0.02f, r);
    addMetal(b, 0.12f, 0.5f, 2100, 0.02f, r);
    addNoise(b, 0.05f, 0.25f, 800, 5000, 0.05f, r, 0.005f);
    fadeTail(b);
    normalize(b, 0.55f);
    return b;
}

std::vector<float> impactStone(Rng& r) {  // a chip off a wall: sharp crack and a little grit
    auto b = buffer(0.16f);
    addNoise(b, 0, 0.9f, 1200 * r.jitter(0.1f), 9000, 0.004f * r.jitter(0.2f), r, 0.0001f);
    addNoise(b, 0, 0.5f, 300, 2000, 0.012f, r, 0.0002f);
    addGrit(b, 0.004f, 0.06f, 0.12f, r);
    fadeTail(b);
    normalize(b, 0.6f);
    return b;
}

std::vector<float> impactWood(Rng& r) {  // a dull, hollow thunk
    auto b = buffer(0.16f);
    addTone(b, 0, 0.8f, 330 * r.jitter(0.1f), 240, 0.025f);
    addNoise(b, 0, 0.6f, 400, 3500, 0.008f, r, 0.0002f);
    lowpass(b, 5000);
    fadeTail(b);
    normalize(b, 0.6f);
    return b;
}

std::vector<float> impactMetal(Rng& r) {  // a ricochet-ish ping
    auto b = buffer(0.3f);
    addNoise(b, 0, 0.6f, 2000, 10000, 0.002f, r, 0.0001f);
    addMetal(b, 0, 0.5f, 2300 * r.jitter(0.08f), 0.07f, r);
    fadeTail(b);
    normalize(b, 0.55f);
    return b;
}

std::vector<float> helmetHit(Rng& r) {  // the helmet "tink": bright, short, unmistakable
    auto b = buffer(0.25f);
    addMetal(b, 0, 0.8f, 3100 * r.jitter(0.04f), 0.05f, r);
    addNoise(b, 0, 0.4f, 3000, 11000, 0.0015f, r, 0.0001f);
    fadeTail(b);
    normalize(b, 0.6f);
    return b;
}

std::vector<float> whiz(Rng& r) {  // a bullet going past: a fast, breathy zip that falls in pitch
    auto b = buffer(0.14f);
    addNoise(b, 0.01f, 0.7f, 1500, 6000 * r.jitter(0.1f), 0.03f, r, 0.02f);
    addTone(b, 0.01f, 0.25f, 2600 * r.jitter(0.08f), 900, 0.04f);
    fadeTail(b);
    normalize(b, 0.45f);
    return b;
}

std::vector<float> uiClick(Rng& r) {  // fallback menu click
    auto b = buffer(0.03f);
    addTone(b, 0, 0.6f, 2200 * r.jitter(0.05f), 1800, 0.004f);
    fadeTail(b);
    normalize(b, 0.4f);
    return b;
}

// Every sound's file name (dumps, and recordings in assets/sounds: <name>_<n>.wav or .ogg).
const char* const kSfxNames[] = {"rifle_shot", "dry_fire", "mag_out", "mag_in", "bolt", "draw", "footstep",
                                 "land", "hit_body", "hit_head", "sniper_shot", "pistol_shot", "rifle_shot_far",
                                 "hit_marker", "footstep_wood", "footstep_metal", "flash_bang", "flash_ring",
                                 "explosion", "fire", "bomb_beep", "defuse", "impact_stone", "impact_wood",
                                 "impact_metal", "helmet_hit", "whiz", "ui_click", "suppressed_shot", "zoom",
                                 "suppressed_rifle", "shotgun_shot", "galil_shot", "mac10_shot", "ump_shot",
                                 "ssg_shot", "xm_shot", "deagle_shot", "berettas_shot"};
static_assert(sizeof(kSfxNames) / sizeof(kSfxNames[0]) == size_t(Sfx::Count), "name every sound");

// A recording -> 48 kHz mono float, or empty if it can't be read. WAV through SDL, Ogg through stb_vorbis.
std::vector<float> loadRecording(const std::string& path) {
    std::vector<float> out;
    SDL_AudioSpec src{};
    Uint8* data = nullptr;
    int len = 0;
    short* pcm = nullptr;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".ogg") == 0) {
        int channels = 0, rate = 0;
        const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &pcm);
        if (frames <= 0 || !pcm) return out;
        src = SDL_AudioSpec{SDL_AUDIO_S16, channels, rate};
        data = reinterpret_cast<Uint8*>(pcm);
        len = frames * channels * int(sizeof(short));
    } else {
        Uint32 wavLen = 0;
        if (!SDL_LoadWAV(path.c_str(), &src, &data, &wavLen)) return out;
        len = int(wavLen);
    }
    const SDL_AudioSpec dst{SDL_AUDIO_F32, 1, kRate};
    Uint8* conv = nullptr;
    int convLen = 0;
    if (SDL_ConvertAudioSamples(&src, data, len, &dst, &conv, &convLen) && conv) {
        const float* f = reinterpret_cast<const float*>(conv);
        out.assign(f, f + convLen / int(sizeof(float)));
        SDL_free(conv);
    }
    if (pcm) std::free(pcm);
    else SDL_free(data);
    return out;
}

struct SoundBank {
    std::vector<std::vector<float>> clips;
    std::vector<int> first, count;
};

SoundBank synthesize() {
    using Maker = std::vector<float> (*)(Rng&);
    struct Entry { Sfx id; Maker make; int variants; };
    const Entry entries[] = {
        {Sfx::RifleShot, rifleShot, 4},  {Sfx::DryFire, dryFire, 2}, {Sfx::MagOut, magOut, 2},
        {Sfx::MagIn, magIn, 2},          {Sfx::Bolt, bolt, 2},       {Sfx::Draw, drawSound, 2},
        {Sfx::Footstep, footstep, 6},    {Sfx::Land, landing, 3},    {Sfx::HitBody, hitBody, 4},
        {Sfx::HitHead, hitHead, 3},      {Sfx::SniperShot, sniperShot, 3}, {Sfx::PistolShot, pistolShot, 4},
        {Sfx::RifleShotFar, rifleShotFar, 3},    {Sfx::HitMarker, hitMarker, 3},
        {Sfx::FootstepWood, footstepWood, 4},    {Sfx::FootstepMetal, footstepMetal, 4},
        {Sfx::FlashBang, flashBang, 2},          {Sfx::FlashRing, flashRing, 1},
        {Sfx::Explosion, explosion, 3},          {Sfx::Fire, fireCrackle, 4},
        {Sfx::BombBeep, bombBeep, 1},            {Sfx::Defuse, defuseKit, 1},
        {Sfx::ImpactStone, impactStone, 4},      {Sfx::ImpactWood, impactWood, 3},
        {Sfx::ImpactMetal, impactMetal, 3},      {Sfx::HelmetHit, helmetHit, 2},
        {Sfx::Whiz, whiz, 3},                    {Sfx::UiClick, uiClick, 1},
        {Sfx::SuppressedShot, suppressedShot, 4},  {Sfx::Zoom, zoomSound, 2},
        {Sfx::SuppressedRifle, suppressedRifle, 4}, {Sfx::ShotgunShot, shotgunShot, 3},
        // (the guns' own sounds: replaced by their recordings, or by a shared sound pitched: kGunFallbacks)
        {Sfx::GalilShot, rifleShot, 1},  {Sfx::Mac10Shot, rifleShot, 1},  {Sfx::UmpShot, rifleShot, 1},
        {Sfx::SsgShot, sniperShot, 1},   {Sfx::XmShot, shotgunShot, 1},   {Sfx::DeagleShot, pistolShot, 1},
        {Sfx::BerettasShot, pistolShot, 1},
    };
    static_assert(sizeof(entries) / sizeof(entries[0]) == size_t(Sfx::Count), "every sound needs an entry");
    SoundBank bank;
    bank.first.assign(size_t(Sfx::Count), 0);
    bank.count.assign(size_t(Sfx::Count), 0);
    Rng rng;
    for (const Entry& e : entries) {
        bank.first[size_t(e.id)] = int(bank.clips.size());
        bank.count[size_t(e.id)] = e.variants;
        for (int v = 0; v < e.variants; ++v) bank.clips.push_back(e.make(rng));
    }
    return bank;
}

// Every sound: synthesized, then recordings in assetDir swapped in: <name>_1.wav / .ogg, _2, ... Each is
// levelled to the synthesized sound's peak, so the mix stays balanced however loud the file is. Without a
// recording of its own, the suppressed pistol is made from the pistol's recordings.
// Where a sound is, as ears hear it (see Audio::play3D). `d` = source - listener, `occ` 0..1 how blocked.
struct Spatial { float pan, delayL, delayR, splitHz, hiL, hiR, gain; };
Spatial spatialize(const Vec3& d, float listenerYawDeg, float occ) {
    const float flat = std::sqrt(d.x * d.x + d.y * d.y);
    const float y = listenerYawDeg * kDegToRad;
    const Vec3 right{std::sin(y), -std::cos(y), 0}, fwd{std::cos(y), std::sin(y), 0};
    const float side = flat > 1e-3f ? dot(d, right) / flat : 0.0f;  // -1 left .. +1 right
    const float front = flat > 1e-3f ? dot(d, fwd) / flat : 1.0f;   // -1 behind .. +1 ahead
    const float elev = std::atan2(d.z, std::max(flat, 1.0f));        // radians, + = above
    Spatial sp{side * 0.7f, 0, 0, 2200.0f, 1.0f, 1.0f, 1.0f};
    // Between the ears: the far one hears it up to 0.65 ms later and duller (the head in the way).
    const float itd = 0.00065f * kRate * std::fabs(side);
    (side > 0 ? sp.delayL : sp.delayR) = itd;
    sp.hiL = 1.0f - 0.55f * std::max(0.0f, side);
    sp.hiR = 1.0f - 0.55f * std::max(0.0f, -side);
    // Behind you: duller and a touch quieter (the ear flaps face forwards).
    const float behind = std::max(0.0f, -front);
    sp.hiL *= 1.0f - 0.35f * behind;
    sp.hiR *= 1.0f - 0.35f * behind;
    sp.gain *= 1.0f - 0.15f * behind;
    // Above or below (only when it's really on another level): below is darker, above brighter.
    if (std::fabs(d.z) > 40.0f) {
        const float e = std::clamp(elev / 0.8f, -1.0f, 1.0f);
        const float k = e < 0 ? 1.0f + 0.45f * e : 1.0f + 0.3f * e;
        sp.hiL *= k;
        sp.hiR *= k;
        if (e < 0) sp.gain *= 1.0f + 0.1f * e;
    }
    // Through walls: muffled and quieter, like CS.
    occ = std::clamp(occ, 0.0f, 1.0f);
    if (occ > 0) {
        sp.hiL *= 1.0f - 0.75f * occ;
        sp.hiR *= 1.0f - 0.75f * occ;
        sp.splitHz = 2200.0f - 1300.0f * occ;
        sp.gain *= 1.0f - 0.3f * occ;
    }
    return sp;
}

// A gun without its own recording sounds like a shared one at another pitch (lower = heavier).
struct GunFallback { Sfx sfx, from; float pitch; };
constexpr GunFallback kGunFallbacks[] = {
    {Sfx::GalilShot, Sfx::RifleShot, 1.04f}, {Sfx::Mac10Shot, Sfx::RifleShot, 1.22f},
    {Sfx::UmpShot, Sfx::RifleShot, 0.9f},    {Sfx::SsgShot, Sfx::SniperShot, 1.18f},
    {Sfx::XmShot, Sfx::ShotgunShot, 1.06f},  {Sfx::DeagleShot, Sfx::PistolShot, 0.82f},
    {Sfx::BerettasShot, Sfx::PistolShot, 1.1f},
};

std::vector<float> repitched(const std::vector<float>& x, float pitch) {
    std::vector<float> out(x.empty() ? 0 : size_t(float(x.size() - 1) / pitch) + 1);
    for (size_t i = 0; i < out.size(); ++i) {
        const float at = float(i) * pitch;
        const size_t k = std::min(size_t(at), x.size() - 1);
        const float f = at - float(k);
        out[i] = k + 1 < x.size() ? x[k] * (1 - f) + x[k + 1] * f : x[k];
    }
    return out;
}

SoundBank loadBank(const std::string& assetDir, int& loaded) {
    SoundBank bank = synthesize();
    loaded = 0;
    std::vector<std::vector<float>> rifleClips;
    bool ownSuppressed = false, ownSuppressedRifle = false;
    std::vector<char> own(size_t(Sfx::Count), 0);
    auto replace = [&](size_t s, std::vector<std::vector<float>>& clips) {
        float synthPeak = 0;
        for (int v = 0; v < bank.count[s]; ++v)
            for (float x : bank.clips[size_t(bank.first[s] + v)]) synthPeak = std::max(synthPeak, std::fabs(x));
        bank.first[s] = int(bank.clips.size());
        bank.count[s] = int(clips.size());
        for (std::vector<float>& c : clips) {
            float peak = 0;
            for (float x : c) peak = std::max(peak, std::fabs(x));
            if (peak > 0)
                for (float& x : c) x *= synthPeak / peak;
            bank.clips.push_back(std::move(c));
        }
        ++loaded;
    };
    for (size_t s = 0; !assetDir.empty() && s < size_t(Sfx::Count); ++s) {
        std::vector<std::vector<float>> clips;
        for (int k = 1; k <= 12; ++k) {
            const std::string base = assetDir + "/" + kSfxNames[s] + "_" + std::to_string(k);
            std::vector<float> c = loadRecording(base + ".wav");
            if (c.empty()) c = loadRecording(base + ".ogg");
            if (c.empty()) break;
            clips.push_back(std::move(c));
        }
        if (clips.empty()) continue;
        if (s == size_t(Sfx::RifleShot)) rifleClips = clips;
        if (s == size_t(Sfx::SuppressedShot)) ownSuppressed = true;
        if (s == size_t(Sfx::SuppressedRifle)) ownSuppressedRifle = true;
        own[s] = 1;
        replace(s, clips);
    }
    for (const GunFallback& f : kGunFallbacks) {
        if (own[size_t(f.sfx)]) continue;
        const size_t from = size_t(f.from);
        bank.first[size_t(f.sfx)] = int(bank.clips.size());
        bank.count[size_t(f.sfx)] = bank.count[from];
        for (int v = 0; v < bank.count[from]; ++v)
            bank.clips.push_back(repitched(bank.clips[size_t(bank.first[from] + v)], f.pitch));
    }
    // The suppressed guns are built on the AK-47's recordings (the synthesized rifle if there are none), unless
    // they have recordings of their own.
    if (rifleClips.empty())
        for (int v = 0; v < bank.count[size_t(Sfx::RifleShot)]; ++v)
            rifleClips.push_back(bank.clips[size_t(bank.first[size_t(Sfx::RifleShot)] + v)]);
    for (int rifle = 0; rifle < 2 && !rifleClips.empty(); ++rifle) {
        if (rifle ? ownSuppressedRifle : ownSuppressed) continue;
        Rng rng;
        std::vector<std::vector<float>> clips;
        for (int v = 0; v < 4; ++v)
            clips.push_back(suppressedFrom(rifleClips[size_t(v + rifle) % rifleClips.size()], rifle ? kM4A1S : kUspS, rifle != 0, rng));
        replace(size_t(rifle ? Sfx::SuppressedRifle : Sfx::SuppressedShot), clips);
    }
    return bank;
}

}  // namespace

bool Audio::dumpWavs(const std::string& dir, const std::string& assetDir) {
    const char* const* names = kSfxNames;
    int loaded = 0;
    SoundBank bank = loadBank(assetDir, loaded);
    for (size_t s = 0; s < size_t(Sfx::Count); ++s)
        for (int v = 0; v < bank.count[s]; ++v) {
            const std::vector<float>& clip = bank.clips[size_t(bank.first[s] + v)];
            std::ofstream f(dir + "/" + names[s] + "_" + std::to_string(v + 1) + ".wav", std::ios::binary);
            if (!f) return false;
            auto u32 = [&](uint32_t x) { f.write(reinterpret_cast<const char*>(&x), 4); };
            auto u16 = [&](uint16_t x) { f.write(reinterpret_cast<const char*>(&x), 2); };
            uint32_t bytes = uint32_t(clip.size() * 2);
            f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
            u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
            f.write("data", 4); u32(bytes);
            for (float x : clip) u16(uint16_t(int16_t(std::clamp(x, -1.0f, 1.0f) * 32767.0f)));
        }
    return true;
}

// Dev aid: a footstep from ahead, left, right, behind, above, below and behind a wall, rendered the way play3D
// would, as stereo WAVs (spatial_<where>.wav) so the cues can be measured.
bool Audio::dumpSpatial(const std::string& dir, const std::string& assetDir) {
    int loaded = 0;
    SoundBank bank = loadBank(assetDir, loaded);
    const std::vector<float>& snd = bank.clips[size_t(bank.first[size_t(Sfx::Footstep)])];
    struct Case { const char* name; Vec3 d; float occ; };
    const Case cases[] = {{"ahead", {300, 0, 0}, 0},  {"left", {0, 300, 0}, 0},    {"right", {0, -300, 0}, 0},
                          {"behind", {-300, 0, 0}, 0}, {"above", {150, 0, 260}, 0}, {"below", {150, 0, -260}, 0},
                          {"wall", {300, 0, 0}, 1}};
    for (const Case& c : cases) {
        const Spatial sp = spatialize(c.d, 0.0f, c.occ);
        const float a = (std::clamp(sp.pan, -1.0f, 1.0f) + 1.0f) * kPi * 0.25f;
        Voice v{0, 0.0, 1.0f, std::cos(a) * sp.gain * 1.4142f, std::sin(a) * sp.gain * 1.4142f};
        v.delayL = sp.delayL;
        v.delayR = sp.delayR;
        v.split = lpCoef(sp.splitHz);
        v.hiL = sp.hiL;
        v.hiR = sp.hiR;
        std::vector<float> out((snd.size() + 64) * 2, 0.0f);
        mixVoice(v, snd, out.data(), int(out.size() / 2));
        std::ofstream f(dir + "/spatial_" + c.name + ".wav", std::ios::binary);
        if (!f) return false;
        auto u32 = [&](uint32_t x) { f.write(reinterpret_cast<const char*>(&x), 4); };
        auto u16 = [&](uint16_t x) { f.write(reinterpret_cast<const char*>(&x), 2); };
        const uint32_t bytes = uint32_t(out.size() * 2);
        f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
        u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4); u16(4); u16(16);
        f.write("data", 4); u32(bytes);
        for (float x : out) u16(uint16_t(int16_t(std::clamp(x, -1.0f, 1.0f) * 32767.0f)));
    }
    return true;
}

bool Audio::init(float masterVolume, const std::string& assetDir) {
    master_ = masterVolume;
    SoundBank bank = loadBank(assetDir, loaded_);
    sounds_ = std::move(bank.clips);
    first_ = std::move(bank.first);
    count_ = std::move(bank.count);
    last_.assign(first_.size(), -1);
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
    if (musicLoader_.joinable()) musicLoader_.join();
}

void Audio::loadMusic(const std::string& path) {
    if (musicLoader_.joinable() || musicReady_) return;
    musicLoader_ = std::thread([this, path] {
        int channels = 0, rate = 0;
        short* pcm = nullptr;
        const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &pcm);
        if (frames <= 0 || !pcm || channels < 1 || rate <= 0) {
            if (pcm) std::free(pcm);
            return;  // no music file: the menu is just quiet
        }
        // To stereo at 48 kHz (linear interpolation), a few ms faded at the ends so the loop doesn't click.
        const double step = double(rate) / kRate;
        const size_t out = size_t(double(frames) / step);
        std::vector<float> m(out * 2);
        for (size_t i = 0; i < out; ++i) {
            const double src = double(i) * step;
            const size_t i0 = std::min(size_t(src), size_t(frames - 1)), i1 = std::min(i0 + 1, size_t(frames - 1));
            const float f = float(src - double(i0));
            for (int c = 0; c < 2; ++c) {
                const int ch = std::min(c, channels - 1);
                const float a = pcm[i0 * size_t(channels) + size_t(ch)], b = pcm[i1 * size_t(channels) + size_t(ch)];
                m[i * 2 + size_t(c)] = (a + (b - a) * f) / 32768.0f;
            }
        }
        std::free(pcm);
        const size_t fade = std::min(out / 2, size_t(kRate / 200));
        for (size_t i = 0; i < fade; ++i) {
            const float g = float(i) / float(fade);
            for (int c = 0; c < 2; ++c) {
                m[i * 2 + size_t(c)] *= g;
                m[(out - 1 - i) * 2 + size_t(c)] *= g;
            }
        }
        std::fprintf(stderr, "music: %.0f s loaded\n", double(out) / kRate);
        music_ = std::move(m);
        musicReady_ = true;  // (publishes music_ to the audio thread)
    });
}

float Audio::rand01() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

void Audio::play(Sfx s, float gain, float pan, float pitch) { start(s, gain, pan, pitch, 0, 0, 2000, 1, 1); }

void Audio::start(Sfx s, float gain, float pan, float pitch, float delayL, float delayR, float splitHz, float hiL, float hiR) {
    if (!stream_ || gain <= 0.001f) return;
    // Pick a variant (not the one we just played) and nudge pitch +-2.5% and volume +-1 dB.
    size_t id = size_t(s);
    int n = count_[id], v = int(rand01() * float(n)) % n;
    if (n > 1 && v == last_[id]) v = (v + 1) % n;
    last_[id] = v;
    pitch *= 0.975f + 0.05f * rand01();
    gain *= 0.89f + 0.22f * rand01();
    pan = std::clamp(pan, -1.0f, 1.0f);
    float a = (pan + 1.0f) * kPi * 0.25f;  // equal-power pan
    Voice voice{first_[id] + v, 0.0, pitch, std::cos(a) * gain * 1.4142f, std::sin(a) * gain * 1.4142f};
    voice.delayL = delayL;
    voice.delayR = delayR;
    voice.split = lpCoef(splitHz);
    voice.hiL = hiL;
    voice.hiR = hiR;
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() < kMaxVoices) pending_.push_back(voice);
}

void Audio::play3D(Sfx s, const Vec3& pos, const Vec3& listener, float listenerYawDeg, float maxDist, float gain,
                   float pitch) {
    const float dist = length(pos - listener);
    if (dist >= maxDist) return;
    float falloff = 1.0f - dist / maxDist;
    falloff *= falloff;
    const float occ = occlusion_ ? occlusion_(occlusionCtx_, listener, pos) : 0.0f;
    const Spatial sp = spatialize(pos - listener, listenerYawDeg, occ);
    start(s, gain * falloff * sp.gain, sp.pan, pitch, sp.delayL, sp.delayR, sp.splitHz, sp.hiL, sp.hiR);
}

void SDLCALL Audio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<Audio*>(user);
    int frames = additional / int(sizeof(float) * 2);
    while (frames > 0) {
        int n = std::min(frames, int(self->mixBuf_.size() / 2));
        const uint64_t t0 = SDL_GetPerformanceCounter();
        self->mix(self->mixBuf_.data(), n);
        self->mixTicks_ += SDL_GetPerformanceCounter() - t0;
        self->mixedFrames_ += uint64_t(n);
        SDL_PutAudioStreamData(stream, self->mixBuf_.data(), n * int(sizeof(float) * 2));
        frames -= n;
    }
}

void Audio::mixVoice(Voice& v, const std::vector<float>& snd, float* out, int frames) {
    const double last = double(snd.size() - 1);
    auto at = [&](double p) {
        if (p < 0 || p >= last) return 0.0f;
        const size_t i0 = size_t(p);
        const float f = float(p - double(i0));
        return snd[i0] + (snd[i0 + 1] - snd[i0]) * f;
    };
    const bool plain = v.delayL == 0 && v.delayR == 0 && v.hiL == 1 && v.hiR == 1;
    for (int i = 0; i < frames && v.pos < last + double(std::max(v.delayL, v.delayR)); ++i) {
        if (plain) {
            const float smp = at(v.pos);
            out[i * 2] += smp * v.gl;
            out[i * 2 + 1] += smp * v.gr;
        } else {  // each ear: its own delay, and its highs kept by `hi` above the split
            const float l = at(v.pos - double(v.delayL)), r = at(v.pos - double(v.delayR));
            v.lpL += v.split * (l - v.lpL);
            v.lpR += v.split * (r - v.lpR);
            out[i * 2] += (v.lpL + (l - v.lpL) * v.hiL) * v.gl;
            out[i * 2 + 1] += (v.lpR + (r - v.lpR) * v.hiR) * v.gr;
        }
        v.pos += v.rate;
    }
}

void Audio::mix(float* out, int frames) {
    {  // only the hand-over is locked: a sound played on the game thread never waits for a whole mix
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Voice& v : pending_) {
            if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());  // steal the oldest
            voices_.push_back(v);
        }
        pending_.clear();
    }
    std::fill(out, out + frames * 2, 0.0f);
    // The music, fading towards its target level (about a second from silent to full).
    if (musicReady_) {
        const float target = musicTarget_, rampStep = 1.0f / kRate;
        const size_t len = music_.size() / 2;
        for (int i = 0; i < frames && len > 0 && (musicGain_ > 0 || target > 0); ++i) {
            musicGain_ = musicGain_ < target ? std::min(target, musicGain_ + rampStep) : std::max(target, musicGain_ - rampStep);
            out[i * 2] += music_[musicPos_ * 2] * musicGain_;
            out[i * 2 + 1] += music_[musicPos_ * 2 + 1] * musicGain_;
            if (++musicPos_ >= len) musicPos_ = 0;
        }
    }
    if (int(voices_.size()) > peakVoices_) peakVoices_ = int(voices_.size());
    for (Voice& v : voices_) mixVoice(v, sounds_[size_t(v.sound)], out, frames);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) {
                                     return v.pos >= double(sounds_[size_t(v.sound)].size() - 1) + double(std::max(v.delayL, v.delayR));
                                 }),
                  voices_.end());
    for (int i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * master_);  // soft limiter
}
