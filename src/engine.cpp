#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "dsp.h"
#include "sample.h"
#include "styles.h"

namespace dnb {
namespace {

inline float midiHz(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f); }
inline float semiRatio(float s) { return std::pow(2.f, s / 12.f); }

// Where each kind of drum sits in the stereo field (before the mixer's pan).
float defaultPan(const DrumDef& d, int slot) {
    switch (d.type) {
        case DrumType::Kick:
        case DrumType::Snare:
        case DrumType::Clap: return 0.f;
        case DrumType::Metal:
            if (d.p[4] < 2.5f) return -0.25f;   // crash / ride
            if (d.p[4] < 5.f) return -0.3f;     // ride
            return d.p[4] > 30.f ? 0.22f : 0.3f; // closed / open hat
        case DrumType::Rim: return 0.18f;
        case DrumType::Tom: return slot % 2 ? -0.35f : 0.35f;
        case DrumType::Shaker: return -0.35f;
        case DrumType::Cowbell: return -0.2f;
    }
    return 0.f;
}

bool isHatLike(const DrumDef& d) {
    return (d.type == DrumType::Metal && d.p[4] > 5.f) || d.type == DrumType::Shaker;
}

// ---------------------------------------------------------------- drums --
// One voice per drum slot: either synthesised or a sample.
struct DrumVoice {
    DrumDef d;
    const Sample* smp = nullptr;
    bool active = false;
    float t = 0.f, vel = 0.f, ck = 1.f, ckMul = 1.f;
    float ph[7] = {};
    SVF f1, f2, f3;
    OnePoleLP toneLp, rattle;
    float tone = 0.f, gl = 1.f, gr = 1.f;
    double spos = 0.0;
    float sstep = 1.f, sdecay = 0.f;
    Rng nr{0x1234567u};
    bool isKick = false;

    void trig(const DrumDef& def, const Sample* s, float v, float pitch, float decMul, float toneIn, float pan, float sr) {
        d = def;
        smp = s;
        t = 0.f;
        vel = v * def.gain;
        ck = 1.f;
        ckMul = 1.f;
        tone = clampf(toneIn, -1.f, 1.f);
        toneLp.setHz(2200.f, sr);
        panGains(pan, gl, gr);
        isKick = def.type == DrumType::Kick;
        for (float& p : ph) p = 0.f;
        active = true;
        if (smp) {
            spos = 0.0;
            sstep = pitch;
            sdecay = decMul < 0.999f ? (1.f / decMul - 1.f) * 10.f : 0.f;
            return;
        }
        // Scale the synth parameters: pitch moves the tones, decay stretches the envelopes.
        float* p = d.p;
        const float dm = 1.f / std::max(0.1f, decMul);
        switch (d.type) {
            case DrumType::Kick: p[0] *= pitch; p[1] *= pitch; p[3] *= dm; break;
            case DrumType::Snare: p[0] *= pitch; p[3] *= std::sqrt(pitch); p[5] *= std::sqrt(pitch); p[1] *= dm; p[2] *= dm; break;
            case DrumType::Clap: p[0] *= pitch; p[1] *= dm; break;
            case DrumType::Metal: p[0] *= pitch; p[1] *= pitch; p[3] *= pitch; p[4] *= dm; break;
            case DrumType::Rim: p[0] *= pitch; p[1] *= dm; break;
            case DrumType::Tom: p[0] *= pitch; p[2] *= dm; break;
            case DrumType::Shaker: p[0] *= pitch; p[2] *= dm; break;
            case DrumType::Cowbell: p[0] *= pitch; p[1] *= pitch; p[2] *= dm; break;
        }
        f1.reset(); f2.reset(); f3.reset();
        switch (d.type) {
            case DrumType::Kick: f1.set(5000.f, 0.7f, sr); break;
            case DrumType::Snare: f1.set(p[3], 0.7f, sr); f2.set(p[5], 0.7f, sr); rattle.setHz(60.f, sr); break;
            case DrumType::Clap: f1.set(p[0], p[3], sr); f2.set(p[0] * 1.07f, p[3], sr); break;
            case DrumType::Metal: f1.set(p[1], p[2], sr); f2.set(p[3], 0.7f, sr); break;
            case DrumType::Rim: f1.set(p[0] * 1.3f, 2.f, sr); break;
            case DrumType::Shaker: f1.set(p[0], 0.7f, sr); f2.set(p[0] * 1.6f, 1.2f, sr); break;
            case DrumType::Cowbell: f1.set(2000.f * pitch, 1.4f, sr); break;
            default: break;
        }
    }
    void choke(float sr) {
        if (active) ckMul = std::exp(-150.f / sr);
    }

    // Adds this voice into L/R; returns the mono level (for the sidechain key).
    float tick(float sr, Rng& rng, float& L, float& R) {
        if (!active) return 0.f;
        float out = 0.f, outR = 0.f, life = 2.f;
        bool stereo = false;
        if (smp) {
            const size_t n = smp->L.size();
            if (spos >= static_cast<double>(n - 1)) { active = false; return 0.f; }
            const size_t i0 = static_cast<size_t>(spos);
            const float fr = static_cast<float>(spos - static_cast<double>(i0));
            out = smp->L[i0] + (smp->L[i0 + 1] - smp->L[i0]) * fr;
            outR = smp->R[i0] + (smp->R[i0 + 1] - smp->R[i0]) * fr;
            spos += sstep;
            stereo = true;
            if (sdecay > 0.f) {
                const float e = std::exp(-t * sdecay);
                out *= e;
                outR *= e;
                life = 9.2f / sdecay;
            } else {
                life = 1e9f;
            }
            out *= 1.1f;
            outR *= 1.1f;
        } else {
            const float* p = d.p;
            switch (d.type) {
                case DrumType::Kick: {
                    float f = p[1] + (p[0] - p[1]) * std::exp(-t * p[2]);
                    advPhase(ph[0], f / sr);
                    advPhase(ph[1], 3100.f / sr);
                    float body = std::sin(kTwoPi * ph[0]) * std::exp(-t * p[3]);
                    // beater click: a short high blip plus a puff of filtered noise
                    float click = (std::sin(kTwoPi * ph[1]) * 0.6f + f1.lp(rng.next())) * std::exp(-t * 420.f) * p[4];
                    float drive = 1.f + p[5] * 3.f;
                    out = std::tanh(body * drive + click) / std::tanh(drive) * 0.95f;
                    life = 9.2f / p[3];
                    break;
                }
                case DrumType::Snare: {
                    float bend = 1.f + 0.5f * std::exp(-t * 60.f);
                    advPhase(ph[0], p[0] * bend / sr);
                    advPhase(ph[1], p[0] * 1.74f * bend / sr);
                    float toneS = (std::sin(kTwoPi * ph[0]) * 0.6f + std::sin(kTwoPi * ph[1]) * 0.4f) * std::exp(-t * p[1]);
                    float n = rng.next();
                    // snare wires: noise with a slowly wobbling, random loudness
                    float wob = 0.75f + 0.5f * std::fabs(rattle.tick(nr.next() * 3.f));
                    float noise = f2.lp(f1.hp(n)) * std::exp(-t * p[2]) * wob + n * std::exp(-t * 120.f) * 0.4f;
                    out = std::tanh((toneS * p[4] * 1.6f + noise * (1.f - p[4]) * 1.6f) * 1.5f);
                    life = 9.2f / std::min(p[1], p[2]);
                    break;
                }
                case DrumType::Clap: {
                    const float sp = p[2] * 0.001f;
                    float env = 0.f;
                    for (int i = 0; i < 3; ++i) {
                        float ti = t - sp * i;
                        if (ti >= 0.f) env += std::exp(-ti * 320.f);
                    }
                    float tt = t - sp * 3.f;
                    if (tt >= 0.f) env += std::exp(-tt * p[1]) * 0.8f;
                    out = f1.bp(rng.next()) * env * 2.6f;   // two different noises: a wide clap
                    outR = f2.bp(nr.next()) * env * 2.6f;
                    stereo = true;
                    life = sp * 3.f + 9.2f / p[1];
                    break;
                }
                case DrumType::Metal: {
                    static const float freqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
                    float sq = 0.f;
                    for (int i = 0; i < 6; ++i) {
                        const float dt = freqs[i] * p[0] / sr;
                        advPhase(ph[i], dt);
                        sq += blepSquare(ph[i], dt);   // band-limited: no fizzy aliasing
                    }
                    float x = sq * (1.f / 6.f) * (1.f - p[5]) + rng.next() * p[5];
                    out = f2.hp(f1.bp(x)) * std::exp(-t * p[4]) * 1.6f;
                    life = 9.2f / p[4];
                    break;
                }
                case DrumType::Rim: {
                    advPhase(ph[0], p[0] / sr);
                    advPhase(ph[1], p[0] * 0.36f / sr);
                    out = (std::sin(kTwoPi * ph[0]) * 0.7f + std::sin(kTwoPi * ph[1]) * 0.5f) * std::exp(-t * p[1]) +
                          f1.bp(rng.next()) * std::exp(-t * 400.f) * 0.5f;
                    life = 9.2f / p[1];
                    break;
                }
                case DrumType::Tom: {
                    float f = p[0] * (1.f + p[1] * std::exp(-t * 18.f));
                    advPhase(ph[0], f / sr);
                    out = std::sin(kTwoPi * ph[0]) * std::exp(-t * p[2]) + rng.next() * p[3] * std::exp(-t * 40.f);
                    out = std::tanh(out * 1.3f);
                    life = 9.2f / p[2];
                    break;
                }
                case DrumType::Shaker: {
                    const float a = p[1] * 0.001f;
                    float env = t < a ? t / a : std::exp(-(t - a) * p[2]);
                    float n = rng.next();
                    out = (f1.hp(n) * 0.7f + f2.bp(n) * 0.5f) * env * 1.3f;
                    life = a + 9.2f / p[2];
                    break;
                }
                case DrumType::Cowbell: {
                    const float d0 = p[0] / sr, d1 = p[1] / sr;
                    advPhase(ph[0], d0);
                    advPhase(ph[1], d1);
                    float x = blepSquare(ph[0], d0) + blepSquare(ph[1], d1);
                    float env = std::exp(-t * p[2]) * 0.75f + std::exp(-t * p[2] * 6.f) * 0.25f;
                    out = f1.bp(x * 0.5f) * env * 1.8f;
                    life = 9.2f / p[2];
                    break;
                }
            }
        }
        if (!stereo) outR = out;
        // tone: darker (blend toward a low-pass) or brighter (lift the highs)
        if (tone != 0.f) {
            const float m = 0.5f * (out + outR);
            const float lp = toneLp.tick(m);
            const float adj = tone < 0.f ? -tone * (lp - m) : tone * (m - lp) * 0.9f;
            out += adj;
            outR += adj;
        }
        t += 1.f / sr;
        ck *= ckMul;
        if (t > life || ck < 1e-4f) active = false;
        const float g = vel * ck;
        L += out * g * gl;
        R += outR * g * gr;
        return std::fabs(0.5f * (out + outR) * g);
    }
};

// ----------------------------------------------------------------- bass --

struct MonoVoice {
    MonoDef d;
    Saw s1, s2;
    float sqPh = 0.f, sinPh = 0.f, subPh = 0.f, lfoPh = 0.f;
    float freq = 55.f, target = 55.f, glide = 0.f;
    float amp = 0.f, held = 0.f, fenv = 0.f, dropEnv = 0.f, accentK = 1.f, cutMul = 1.f;
    bool gate = false;
    int semi = -1000;
    SVF fl, fr;
    Ladder ll, lr;
    AdaaTanh satL, satR;
    float vel = 1.f;

    void noteOn(const MonoDef& def, int root, int semis, bool accent, bool slide, float cutoffMul = 1.f) {
        const bool legato = slide && gate && amp > 0.01f;
        d = def;
        semi = semis;
        cutMul = cutoffMul;
        target = midiHz(static_cast<float>(root + semis + 12 * d.oscOctave));
        if (legato) {
            glide = d.glide;
            if (accent) fenv = std::max(fenv, 0.7f);
        } else {
            glide = 0.f;
            freq = target;
            fenv = 1.f;
            dropEnv = 1.f;
            held = 1.f;
            lfoPh = 0.f;
        }
        accentK = accent ? d.accent : 1.f;
        gate = true;
    }
    void noteOff() { gate = false; }

    void tick(float sr, float beatHz, float pm, float& L, float& R) {
        if (!gate && amp < 1e-5f) { amp = 0.f; return; }
        if (glide > 0.f) freq += (target - freq) * (1.f - std::exp(-1.f / (glide * sr)));
        else freq = target;
        float pitch = freq * pm;
        if (d.drop > 0.f) {
            pitch *= std::pow(2.f, d.drop * dropEnv / 12.f);
            dropEnv *= std::exp(-d.dropRate / sr);
        }
        if (gate) {
            if (d.decay > 0.f) held *= std::exp(-d.decay / sr);
            amp += (held - amp) * 0.012f;
        } else {
            amp *= std::exp(-d.release / sr);
        }
        fenv *= std::exp(-d.envDecay / sr);
        advPhase(lfoPh, d.lfoRate * beatHz / sr);

        // filter updated every sample: smooth sweeps
        const float lfo = d.lfoDepth * (0.5f - 0.5f * std::cos(kTwoPi * lfoPh));
        const float cut = (d.cutoff + d.envAmt * fenv * fenv * accentK + lfo) * cutMul;
        if (d.filter == 1) {
            const float k = clampf((d.res - 0.7f) / 1.6f, 0.f, 3.9f);
            ll.set(cut, k, sr);
            lr.set(cut, k, sr);
        } else {
            fl.set(cut, d.res, sr);
            fr.set(cut, d.res, sr);
        }
        float xl = 0.f, xr = 0.f;
        if (d.saw > 0.f) {
            const float dt = d.detune * 0.01f;
            const float a = s1.tick(pitch * (1.f - dt), sr), b = s2.tick(pitch * (1.f + dt), sr);
            const float w = clampf(d.width, 0.f, 1.f);
            xl += (a * (0.5f + 0.5f * w) + b * (0.5f - 0.5f * w)) * d.saw;
            xr += (b * (0.5f + 0.5f * w) + a * (0.5f - 0.5f * w)) * d.saw;
        }
        if (d.square > 0.f) {
            advPhase(sqPh, pitch / sr);
            const float sq = blepSquare(sqPh, pitch / sr) * d.square * 0.7f;
            xl += sq;
            xr += sq;
        }
        float tl, tr;
        if (d.filter == 1) { tl = ll.tick(xl * 0.7f); tr = lr.tick(xr * 0.7f); }
        else { tl = fl.lp(xl); tr = fr.lp(xr); }
        advPhase(sinPh, pitch / sr);
        const float sn = std::sin(kTwoPi * sinPh) * d.sine;
        tl += sn;
        tr += sn;
        advPhase(subPh, pitch * 0.5f / sr);
        const float sub = std::sin(kTwoPi * subPh) * d.sub * 0.6f;
        const float drv = std::max(0.5f, d.drive) * (accentK > 1.f ? 1.2f : 1.f);
        const float norm = 0.5f / std::tanh(std::max(0.5f, d.drive));
        const float g = amp * d.gain * vel;
        L += (satL.tick(tl * drv) * norm + sub) * g;   // anti-aliased drive
        R += (satR.tick(tr * drv) * norm + sub) * g;
    }
};

// --------------------------------------------------------------- chords --

struct ChordVoice {
    ChordDef d;
    int n = 0, uni = 1;
    float freqs[5] = {};
    float ph[5][2] = {};
    Saw saws[5][7];
    float t = 10.f, env = 0.f, fenv = 0.f, vel = 1.f, cutMul = 1.f;
    bool gate = false, active = false;
    SVF fl, fr;

    void noteOn(const ChordDef& def, const float* f, int count, float v, float cutoffMul = 1.f) {
        d = def;
        n = std::max(1, std::min(5, count));
        uni = std::max(1, std::min(7, d.unison));
        for (int i = 0; i < n; ++i) freqs[i] = f[i];
        t = 0.f;
        fenv = 1.f;
        gate = true;
        active = true;
        vel = v;
        cutMul = cutoffMul;
        for (auto& p : ph) p[0] = p[1] = 0.f;
    }
    void noteOff() { gate = false; }

    void tick(float sr, Rng& rng, float pm, float& L, float& R) {
        if (!active) return;
        if (gate) {
            if (t < d.attack) env = t / d.attack;
            else if (d.decay > 0.f) env = d.sustain + (1.f - d.sustain) * std::exp(-(t - d.attack) * d.decay);
            else env = 1.f;
            if (d.decay > 0.f && d.sustain <= 0.f && t > d.attack && env < 1e-4f) { active = false; return; }
        } else {
            env *= std::exp(-d.release / sr);
            if (env < 1e-4f) { active = false; return; }
        }
        fenv *= std::exp(-d.envDecay / sr);
        const float cut = (d.cutoff + d.envAmt * fenv) * cutMul;
        fl.set(cut, d.res, sr);
        fr.set(cut, d.res, sr);

        float xl = 0.f, xr = 0.f;
        const float det = d.detune * 0.01f, spread = clampf(d.spread, 0.f, 1.f);
        for (int i = 0; i < n; ++i) {
            const float f = freqs[i] * pm;
            if (d.osc == ChordOsc::Saw) {
                for (int u = 0; u < uni; ++u) {
                    const float pos = uni == 1 ? 0.f : (2.f * u / (uni - 1) - 1.f);  // -1..1
                    const float y = saws[i][u].tick(f * (1.f + det * pos), sr);
                    const float pr = 0.5f + 0.5f * pos * spread;
                    xl += y * (1.f - pr) * 2.f / uni;
                    xr += y * pr * 2.f / uni;
                }
                continue;
            }
            float y = 0.f;
            switch (d.osc) {
                case ChordOsc::Square:
                    advPhase(ph[i][0], f / sr);
                    y = blepSquare(ph[i][0], f / sr) * 1.4f;
                    break;
                case ChordOsc::EPiano: {
                    advPhase(ph[i][0], f / sr);
                    advPhase(ph[i][1], f / sr);
                    const float idx = 1.4f * std::exp(-t * 7.f) + 0.25f;
                    y = std::sin(kTwoPi * ph[i][0] + idx * std::sin(kTwoPi * ph[i][1])) * 1.6f;
                    break;
                }
                case ChordOsc::Organ: {
                    advPhase(ph[i][0], f / sr);
                    const float a = kTwoPi * ph[i][0];
                    y = (std::sin(a) + 0.5f * std::sin(2.f * a) + 0.35f * std::sin(3.f * a) + 0.2f * std::sin(4.f * a)) * 1.1f;
                    break;
                }
                case ChordOsc::Bell: {
                    advPhase(ph[i][0], f / sr);
                    advPhase(ph[i][1], f * 3.5f / sr);
                    const float idx = 2.5f * std::exp(-t * 3.f);
                    y = std::sin(kTwoPi * ph[i][0] + idx * std::sin(kTwoPi * ph[i][1])) * 1.4f;
                    break;
                }
                case ChordOsc::Marimba:
                    advPhase(ph[i][0], f / sr);
                    advPhase(ph[i][1], f * 3.93f / sr);
                    y = (std::sin(kTwoPi * ph[i][0]) + 0.5f * std::sin(kTwoPi * ph[i][1]) * std::exp(-t * 30.f)) * 1.5f;
                    break;
                default: break;
            }
            // spread the chord's notes across the stereo field
            const float pos = n == 1 ? 0.f : (2.f * i / (n - 1) - 1.f);
            const float pr = 0.5f + 0.5f * pos * spread;
            xl += y * (1.f - pr) * 2.f;
            xr += y * pr * 2.f;
        }
        if (d.osc == ChordOsc::Organ && t < 0.01f) {
            const float c = rng.next() * 0.3f * (1.f - t * 100.f);
            xl += c;
            xr += c;
        }
        const float sc = 0.55f / std::sqrt(static_cast<float>(n));
        const float g = env * d.gain * vel;
        L += fl.lp(xl * sc) * g;
        R += fr.lp(xr * sc) * g;
        t += 1.f / sr;
    }
};

// ---------------------------------------------------------- FX sounds --

struct SirenVoice {
    bool held = false;
    float amp = 0.f, lfo = 0.f;
    Saw osc;
    SVF filt;
    float tick(float sr) {
        amp += ((held ? 1.f : 0.f) - amp) * (held ? 0.004f : 0.0004f);
        if (amp < 1e-4f) return 0.f;
        advPhase(lfo, 3.2f / sr);
        float tri = lfo < 0.5f ? lfo * 2.f : 2.f - lfo * 2.f;
        filt.set(3000.f, 0.8f, sr);
        return filt.lp(osc.tick(470.f * std::pow(2.f, tri), sr)) * amp * 0.5f;
    }
};

struct RiserVoice {
    bool held = false;
    float t = 0.f, amp = 0.f;
    Saw saw;
    SVF bp;
    float progress() const { return std::min(1.f, t / 6.f); }
    float tick(float sr, Rng& rng) {
        if (held) t += 1.f / sr;
        float target = held ? 0.2f + 0.8f * progress() : 0.f;
        amp += (target - amp) * (held ? 0.002f : 0.0008f);
        if (amp < 1e-4f) return 0.f;
        float tt = std::min(t, 8.f);
        bp.set(std::min(250.f * std::pow(2.f, tt * 0.85f), 14000.f), 3.f, sr);
        return (bp.bp(rng.next()) * 1.6f + saw.tick(110.f * std::pow(2.f, tt * 0.5f), sr) * 0.12f) * amp;
    }
};

struct HornVoice {
    bool held = false;
    float t = 0.f, amp = 0.f, vib = 0.f;
    Saw saws[4];
    SVF filt;
    float tick(float sr) {
        amp += ((held ? 1.f : 0.f) - amp) * (held ? 0.006f : 0.002f);
        if (amp < 1e-4f) return 0.f;
        t += 1.f / sr;
        advPhase(vib, 6.f / sr);
        float f = 466.16f * (1.f - 0.08f * std::exp(-t * 18.f)) * (1.f + 0.004f * std::sin(kTwoPi * vib));
        float x = saws[0].tick(f, sr) + saws[1].tick(f * 1.004f, sr) + saws[2].tick(f * 0.996f, sr) +
                  saws[3].tick(f * 0.501f, sr) * 0.8f;
        filt.set(2600.f, 1.2f, sr);
        return std::tanh(filt.lp(x * 0.3f) * 2.f) * amp * 0.45f;
    }
};

// "Pew" zaps repeating every 8th note while held.
struct LaserVoice {
    bool held = false;
    float t = 1.f, ph = 0.f;
    double next = 0.0;
    float tick(float sr, double eighthSamples, double now) {
        if (held && now >= next) {
            t = 0.f;
            next = now + eighthSamples;
        }
        if (t > 0.6f) return 0.f;
        float f = 150.f + 2600.f * std::exp(-t * 22.f);
        advPhase(ph, f / sr);
        float y = blepSquare(ph, f / sr) * std::exp(-t * 7.f) * 0.22f;
        t += 1.f / sr;
        return y;
    }
};

// ------------------------------------------------------------ lead synth --
// The second MIDI instrument: up to 10 voices (poly patches) or one legato
// voice (mono patches), with sustain pedal, pitch bend and mod-wheel vibrato.
struct LeadSynth {
    static constexpr int N = 10;
    ChordVoice poly[N];
    int polyNote[N];
    bool polyHeld[N] = {}, polySus[N] = {};
    uint64_t polyAge[N] = {};
    MonoVoice mono;
    int monoStack[16];
    int monoCount = 0;
    int patch = 0;
    bool sustain = false, monoSus = false;
    float bend = 0.f, mod = 0.f, vibPh = 0.f;
    uint64_t counter = 0;

    LeadSynth() { for (int& n : polyNote) n = -1; }

    const LeadPatch& p() const { return leadPatches()[static_cast<size_t>(patch) % leadPatches().size()]; }
    bool isMono() const { auto k = p().kind; return k == LeadPatch::MONO || k == LeadPatch::STYLE_BASS; }

    void noteOn(const Style& st, int note, int velocity) {
        const LeadPatch& lp = p();
        const float v = 0.35f + 0.65f * std::min(127, std::max(1, velocity)) / 127.f;
        if (isMono()) {
            const MonoDef& d = lp.kind == LeadPatch::STYLE_BASS ? st.bass : lp.mono;
            for (int i = 0; i < monoCount; ++i)
                if (monoStack[i] == note) { for (int k = i; k < monoCount - 1; ++k) monoStack[k] = monoStack[k + 1]; --monoCount; break; }
            if (monoCount < 16) monoStack[monoCount++] = note;
            mono.noteOn(d, lp.transpose - 12 * d.oscOctave, note, velocity > 110, monoCount > 1);
            mono.vel = v;
            monoSus = false;
            return;
        }
        ChordDef d = lp.kind == LeadPatch::STYLE_CHORDS ? st.chord : lp.poly;
        const float f = midiHz(static_cast<float>(note + lp.transpose));
        int slot = -1;
        for (int i = 0; i < N; ++i) if (polyNote[i] == note && (polyHeld[i] || polySus[i])) { slot = i; break; }
        if (slot < 0) for (int i = 0; i < N; ++i) if (!poly[i].active) { slot = i; break; }
        if (slot < 0) {  // steal the oldest released voice, else the oldest one
            uint64_t best = UINT64_MAX;
            for (int i = 0; i < N; ++i) if (!polyHeld[i] && polyAge[i] < best) { best = polyAge[i]; slot = i; }
            if (slot < 0) for (int i = 0; i < N; ++i) if (polyAge[i] < best) { best = polyAge[i]; slot = i; }
        }
        poly[slot].noteOn(d, &f, 1, v);
        polyNote[slot] = note;
        polyHeld[slot] = true;
        polySus[slot] = false;
        polyAge[slot] = ++counter;
    }

    void noteOff(int note) {
        if (isMono()) {
            for (int i = 0; i < monoCount; ++i)
                if (monoStack[i] == note) { for (int k = i; k < monoCount - 1; ++k) monoStack[k] = monoStack[k + 1]; --monoCount; break; }
            if (monoCount == 0) {
                if (sustain) monoSus = true;
                else mono.noteOff();
            } else {
                mono.noteOn(mono.d, p().transpose - 12 * mono.d.oscOctave, monoStack[monoCount - 1], false, true);
            }
            return;
        }
        for (int i = 0; i < N; ++i) {
            if (polyNote[i] != note || !polyHeld[i]) continue;
            polyHeld[i] = false;
            if (sustain) polySus[i] = true;
            else poly[i].noteOff();
        }
    }

    void setSustain(bool on) {
        sustain = on;
        if (on) return;
        for (int i = 0; i < N; ++i) if (polySus[i]) { polySus[i] = false; poly[i].noteOff(); }
        if (monoSus && monoCount == 0) mono.noteOff();
        monoSus = false;
    }

    void allOff() {
        for (int i = 0; i < N; ++i) { poly[i].noteOff(); polyHeld[i] = polySus[i] = false; }
        mono.noteOff();
        monoCount = 0;
        monoSus = false;
    }

    bool idle() const {
        if (mono.gate || mono.amp > 1e-5f) return false;
        for (const auto& v : poly) if (v.active) return false;
        return true;
    }

    void tick(float sr, float beatHz, Rng& rng, float& L, float& R) {
        advPhase(vibPh, 5.5f / sr);
        const float semis = bend + mod * 0.35f * std::sin(kTwoPi * vibPh);
        const float pm = semis == 0.f ? 1.f : std::pow(2.f, semis / 12.f);
        float l = 0.f, r = 0.f;
        mono.tick(sr, beatHz, pm, l, r);
        for (auto& v : poly) v.tick(sr, rng, pm, l, r);
        L += l * p().gain;
        R += r * p().gain;
    }
};

}  // namespace

// ================================================================ Impl ==

struct Engine::Impl {
    Shared& sh;
    float sr;
    Rng rng{0x2545F491u}, hrng{0x9E3779B9u};
    uint64_t sampleCount = 0;

    DrumVoice drums[kDrumSlots];
    const Sample* samples[kMaxStyles][kDrumSlots] = {};
    MonoVoice bass;
    ChordVoice chords[3];
    int curChord = 0;
    uint32_t chordGen[3] = {};  // bumps each time a chord voice is (re)used
    uint32_t bassGen = 0;       // bumps on every bass note
    uint32_t leadGen[128] = {}; // bumps on every lead note, per MIDI note
    LeadSynth lead;
    LeadSynth synths[kSynths];  // lp-tenori's layer instruments
    uint32_t synthGen[kSynths][128] = {};
    SirenVoice siren;
    RiserVoice riser;
    HornVoice horn;
    LaserVoice laser;

    // sidechain: follows the kick and ducks the music
    Follower scFollow;
    float scRelease = -1.f;

    // ---- sequencer
    bool playing = false;
    int step = -1;
    double phase = 0.0, stepLen = 1.0;
    int pattern = 0, queued = -1;
    bool halftime = false;
    double beatPos = 0.0, beatsSincePlay = 0.0;
    int barCount = 0;
    bool fillBar = false, crashNext = false;
    uint64_t bassOffAt = 0, chordOffAt = 0, leadOffAt = 0;
    bool seqBassActive = false, seqChordActive = false;
    int seqLeadNote = -1;
    int liveBass[16];
    int liveCount = 0;
    int liveChord = 0;
    int songPos = 0, songRep = 0;
    // lead recording
    int recNote = -1, recStep = -1;
    bool recSkipNext = false;

    struct Hit { uint64_t at; int slot; float vel, pitch, decay, tone; };
    Hit hits[96];
    int hitCount = 0;

    // Releases for Cmd::Note: each one only lets go if no newer note took the voice.
    struct Release { uint64_t at; int slot; int note; uint32_t gen; };
    Release releases[256];
    int releaseCount = 0;

    // Commands stamped for a moment after this block: held back until their block.
    Cmd later[512];
    int laterCount = 0;

    // ---- buffer FX (stereo)
    bool fxHeld[FX_COUNT] = {};
    int bufStack[8];
    int bufCount = 0;
    enum BufMode { BUF_NONE, BUF_LOOP, BUF_TAPE } bufMode = BUF_NONE;
    bool bufTarget = false;
    float bufXf = 0.f, xfStep = 0.f;
    std::vector<float> ringL, ringR, loopL, loopR;
    size_t w = 0;
    int loopLen = 1, loopPos = 0;
    bool loopReverse = false;
    double tapeR = 0.0;
    float tapeRate = 1.f;

    float lpAmt = 0.f, hpAmt = 0.f;
    SVF lpfL, lpfR, hpfL, hpfR;

    Smooth crushMix, flMix, dSend, rSend, pumpMix, gateMix, lofiMix, dTime;
    OnePoleLP gateSmooth, lofiL1, lofiL2, lofiR1, lofiR2, crackleLp;
    int crushCount = 0;
    float crushL = 0.f, crushR = 0.f;
    std::vector<float> wowL, wowR;
    size_t wowW = 0;
    float wowPh = 0.f, crackle = 0.f;

    std::vector<float> flL, flR;
    size_t flW = 0;
    float flLfo = 0.f;

    std::vector<float> dlL, dlR;
    size_t dW = 0;
    OnePoleLP dfL, dfR;

    FdnReverb reverb;
    Chorus chorus;
    Biquad lowShelfL, lowShelfR, highShelfL, highShelfR;
    BusCompressor glue;
    Limiter limiter;
    float vol = 1.0f, level = 0.f, levelL = 0.f, levelR = 0.f;
    float cpu = 0.f;
    double blockNow = 0.0;
    int curFrame = 0;

    Impl(Shared& s, float rate) : sh(s), sr(rate) {
        ringL.assign(static_cast<size_t>(sr * 4), 0.f);
        ringR.assign(ringL.size(), 0.f);
        loopL.assign(static_cast<size_t>(sr * 2), 0.f);
        loopR.assign(loopL.size(), 0.f);
        flL.assign(4096, 0.f);
        flR.assign(4096, 0.f);
        wowL.assign(2048, 0.f);
        wowR.assign(2048, 0.f);
        dlL.assign(static_cast<size_t>(sr * 3), 0.f);
        dlR.assign(static_cast<size_t>(sr * 3), 0.f);
        dfL.setHz(3500.f, sr);
        dfR.setHz(3500.f, sr);
        lofiL1.setHz(3200.f, sr); lofiL2.setHz(4200.f, sr);
        lofiR1.setHz(3200.f, sr); lofiR2.setHz(4200.f, sr);
        crackleLp.setHz(2500.f, sr);
        gateSmooth.setHz(300.f, sr);
        reverb.init(sr);
        chorus.init(sr);
        lowShelfL.shelf(false, 90.f, 1.0f, sr); lowShelfR = lowShelfL;
        highShelfL.shelf(true, 9000.f, 1.5f, sr); highShelfR = highShelfL;
        glue.init(sr);
        limiter.init(sr);
        crushMix.setTime(8.f, sr);
        flMix.setTime(30.f, sr);
        dSend.setTime(5.f, sr);
        rSend.setTime(40.f, sr);
        pumpMix.setTime(30.f, sr);
        gateMix.setTime(10.f, sr);
        lofiMix.setTime(60.f, sr);
        dTime.setTime(120.f, sr);
        xfStep = 1.f / (0.004f * sr);  // 4 ms crossfade for buffer FX
        pattern = sh.pattern.load();
    }

    int styleIdx() const { return pattern / kMaxSlots; }
    const Style& style() const { return styleOf(pattern); }
    ChannelMix& mix(int ch) { return sh.mix[styleIdx() % kMaxStyles][ch]; }
    double samplesPerBeat() const { return sr * 60.0 / std::max(40.f, sh.bpm.load(std::memory_order_relaxed)); }
    double samplesPerStep() const { return samplesPerBeat() / 4.0 * (halftime ? 2.0 : 1.0); }
    double lengthOfStep(int s) const {
        const double swing = style().swing;
        return samplesPerStep() * 2.0 * ((s & 1) ? (1.0 - swing) : swing);
    }
    void hit(int slot) { sh.hits[slot].fetch_add(1, std::memory_order_relaxed); }

    bool anySolo() {
        for (int c = 0; c < kChannels; ++c)
            if (mix(c).solo.load(std::memory_order_relaxed)) return true;
        return false;
    }
    // Is this channel audible (mixer solo), and is this track un-muted?
    bool audible(int ch) { return !anySolo() || mix(ch).solo.load(std::memory_order_relaxed); }

    // -------------------------------------------------------- triggers --
    void triggerDrum(int slot, float vel, float pitchSemis = 0.f, float decayLock = 0.f, float toneLock = 0.f) {
        if (slot < 0 || slot > kExtraSlot) return;
        const Style& st = style();
        const DrumDef& def = st.drums[static_cast<size_t>(slot)];
        ChannelMix& m = mix(slot);
        const float pitch = semiRatio(pitchSemis + m.tune.load(std::memory_order_relaxed));
        const float decMul = std::pow(4.f, clampf(decayLock + m.decay.load(std::memory_order_relaxed), -1.f, 1.f));
        const float tone = clampf(toneLock + m.tone.load(std::memory_order_relaxed), -1.f, 1.f);
        const float pan = clampf(defaultPan(def, slot) * sh.width.load(std::memory_order_relaxed) + m.pan.load(std::memory_order_relaxed), -1.f, 1.f);
        drums[slot].trig(def, samples[styleIdx() % kMaxStyles][slot], vel, pitch, decMul, tone, pan, sr);
        if (def.chokes >= 0 && def.chokes <= kExtraSlot && def.chokes != slot) drums[def.chokes].choke(sr);
        hit(slot);
    }
    void queueDrum(uint64_t at, int slot, float vel, float pitch, float decay, float tone) {
        if (hitCount < 96) hits[hitCount++] = {at, slot, vel, pitch, decay, tone};
    }

    void chordOn(int degree, float vel, float semis = 0.f, float cutMul = 1.f) {
        const Style& st = style();
        float f[5];
        const int n = std::clamp(st.chord.notes, 1, 5);
        for (int i = 0; i < n; ++i)
            f[i] = midiHz(static_cast<float>(st.root + st.chord.octave + degreeSemis(st, degree + 2 * i)) + semis);
        chords[curChord].noteOff();
        curChord = (curChord + 1) % 3;
        ++chordGen[curChord];
        chords[curChord].noteOn(st.chord, f, n, vel, cutMul * std::pow(2.f, mix(kChordSlot).tone.load() * 2.f));
        hit(kChordSlot);
    }
    void chordOffAll() { for (auto& c : chords) c.noteOff(); }

    void bassOn(int semis, bool accent, bool slide, float cutMul = 1.f) {
        const Style& st = style();
        const int tune = static_cast<int>(std::lround(mix(kBassSlot).tune.load()));
        bass.noteOn(st.bass, st.root + tune, semis, accent, slide, cutMul * std::pow(2.f, mix(kBassSlot).tone.load() * 2.f));
        ++bassGen;
        hit(kBassSlot);
    }
    void liveBassOn(int semis) {
        if (liveCount < 16) liveBass[liveCount++] = semis;
        seqBassActive = false;
        bassOn(semis, false, liveCount > 1);
    }
    void liveBassOff(int semis) {
        for (int i = 0; i < liveCount; ++i)
            if (liveBass[i] == semis) {
                for (int j = i; j < liveCount - 1; ++j) liveBass[j] = liveBass[j + 1];
                --liveCount;
                break;
            }
        if (liveCount > 0) bassOn(liveBass[liveCount - 1], false, true);
        else bass.noteOff();
    }

    // ------------------------------------------------- scheduled notes --
    // Cmd::Note: one note on any sound, released after `gate` seconds. Used by
    // lp-tenori, which runs its own sequencer and schedules notes slightly ahead.
    void addRelease(double gateSec, int slot, int note, uint32_t gen) {
        const uint64_t at = sampleCount + static_cast<uint64_t>(std::max(0.005, gateSec) * sr);
        if (releaseCount < 256) {
            releases[releaseCount++] = {at, slot, note, gen};
            return;
        }
        // Full (hundreds of overlapping notes): let the oldest one go now to make room.
        int oldest = 0;
        for (int i = 1; i < releaseCount; ++i) if (releases[i].at < releases[oldest].at) oldest = i;
        const Release r = releases[oldest];
        releases[oldest] = {at, slot, note, gen};
        release(r);
    }
    void release(const Release& r) {
        if (r.slot == kChordSlot) {
            if (r.note >= 0 && r.note < 3 && chordGen[r.note] == r.gen && liveChord == 0) chords[r.note].noteOff();
        } else if (r.slot == kBassSlot) {
            if (bassGen == r.gen && liveCount == 0 && !seqBassActive) bass.noteOff();
        } else if (r.slot == kLeadSlot) {
            if (r.note >= 0 && r.note < 128 && leadGen[r.note] == r.gen) lead.noteOff(r.note);
        } else if (r.slot >= kSynthSlot0 && r.slot < kSynthSlot0 + kSynths) {
            const int k = r.slot - kSynthSlot0;
            if (r.note >= 0 && r.note < 128 && synthGen[k][r.note] == r.gen) synths[k].noteOff(r.note);
        }
    }
    void playNote(int slot, int pitch, float vel, float gateSec) {
        vel = clampf(vel, 0.f, 1.25f);
        if (vel <= 0.f) return;
        const double gate = gateSec > 0.f ? gateSec : samplesPerStep() / sr;
        if (slot >= 0 && slot <= kExtraSlot) {
            if (!audible(slot)) return;
            triggerDrum(slot, vel * mix(slot).volume.load(std::memory_order_relaxed), static_cast<float>(pitch));
        } else if (slot == kChordSlot) {
            if (!audible(kChordSlot)) return;
            chordOn(pitch, vel * mix(kChordSlot).volume.load(std::memory_order_relaxed));
            addRelease(gate, kChordSlot, curChord, chordGen[curChord]);
        } else if (slot == kBassSlot) {
            if (!audible(kBassSlot) || liveCount > 0) return;  // a bass pad held live wins
            seqBassActive = false;
            bassOn(pitch, vel > 1.f, false);
            bass.vel = std::min(1.f, vel) * mix(kBassSlot).volume.load(std::memory_order_relaxed);
            addRelease(gate, kBassSlot, pitch, bassGen);
        } else if (slot == kLeadSlot) {
            if (!audible(kLeadSlot) || pitch < 0 || pitch > 127) return;
            lead.noteOn(style(), pitch, std::max(1, std::min(127, static_cast<int>(std::lround(vel * 100.f)))));
            ++leadGen[pitch];
            sh.leadNote.store(pitch, std::memory_order_relaxed);
            hit(kLeadSlot);
            addRelease(gate, kLeadSlot, pitch, leadGen[pitch]);
        } else if (slot >= kSynthSlot0 && slot < kSynthSlot0 + kSynths) {
            if (!audible(kLeadSlot) || pitch < 0 || pitch > 127) return;
            const int k = slot - kSynthSlot0;
            synths[k].noteOn(style(), pitch, std::max(1, std::min(127, static_cast<int>(std::lround(vel * 100.f)))));
            ++synthGen[k][pitch];
            hit(kLeadSlot);
            addRelease(gate, slot, pitch, synthGen[k][pitch]);
        }
    }

    // ------------------------------------------------------- sequencer --
    int noteLength(int p, int t, int s) const {
        const int len = sh.patternLength(p);
        int n = 1;
        while (n < len && stepKind(sh.on[p][t][(s + n) % len].load(std::memory_order_relaxed)) == STEP_TIE) ++n;
        return n;
    }
    float lockOf(int p, int t, int s, int l) const { return sh.lock[p][t][s][l].load(std::memory_order_relaxed) / 100.f; }

    void fireStep(int s) {
        const int p = pattern;
        const Style& st = style();
        const double len = lengthOfStep(s);
        const int bs = s % kSteps;  // position within the bar
        const float hz = sh.humanize.load(std::memory_order_relaxed);
        const float hs = sh.hatSwing.load(std::memory_order_relaxed);

        // after a fill: a crash on the downbeat (if this style has one)
        if (bs == 0 && crashNext) {
            crashNext = false;
            const DrumDef& ex = st.drums[kExtraSlot];
            if (ex.type == DrumType::Metal && ex.p[4] < 2.5f && audible(kExtraSlot)) queueDrum(sampleCount, kExtraSlot, 0.9f, 0, 0, 0);
        }

        for (int t = 0; t < kDrumTracks; ++t) {
            uint8_t v = sh.on[p][t][s].load(std::memory_order_relaxed);
            // Automatic fill on the last beat of every Nth bar: snare rolls, kick drops out.
            if (fillBar && bs >= 12) {
                const DrumType ty = st.drums[static_cast<size_t>(t)].type;
                if (ty == DrumType::Kick && bs > 12) v = STEP_OFF;
                if (t == 1) v = bs == 15 ? STEP_ROLL3 : STEP_ROLL2;
            }
            if (!stepSounds(v) || sh.mute[t].load(std::memory_order_relaxed) || !audible(t)) continue;
            const int k = stepKind(v);
            float vel = k == STEP_ACCENT ? 1.3f : (k == STEP_SOFT ? 0.4f : (k >= STEP_ROLL2 && k <= STEP_ROLL3 ? 0.85f : 1.f));
            if (fillBar && bs >= 12 && t == 1) vel = 0.55f + 0.15f * (bs - 12);
            vel *= 1.f + hz * 0.22f * hrng.next();                  // humanize: loudness
            vel *= mix(t).volume.load(std::memory_order_relaxed);
            const float pitch = sh.lock[p][t][s][LOCK_PITCH].load(std::memory_order_relaxed);
            const float dec = lockOf(p, t, s, LOCK_DECAY), tone = lockOf(p, t, s, LOCK_FILTER);
            double delay = hz * hrng.uni() * 0.007 * sr;              // humanize: timing (0-7 ms late)
            if ((bs & 1) && isHatLike(st.drums[static_cast<size_t>(t)])) delay += hs * len * 0.35;  // hat swing
            queueDrum(sampleCount + static_cast<uint64_t>(delay), t, vel, pitch, dec, tone);
            const int extra = k == STEP_ROLL2 ? 1 : (k == STEP_ROLL3 ? 2 : 0);
            for (int r = 1; r <= extra; ++r)
                queueDrum(sampleCount + static_cast<uint64_t>(delay + len * r / (extra + 1)), t, vel * (0.75f + 0.1f * r), pitch, dec, tone);
        }
        if (fillBar && bs == 15) crashNext = true;

        // Bass
        {
            const uint8_t v = sh.on[p][T_BASS][s].load(std::memory_order_relaxed);
            if (stepSounds(v) && !sh.mute[T_BASS].load(std::memory_order_relaxed) && audible(kBassSlot) && liveCount == 0) {
                const int n = noteLength(p, T_BASS, s);
                const int plen = sh.patternLength(p);
                const uint8_t after = sh.on[p][T_BASS][(s + n) % plen].load(std::memory_order_relaxed);
                const bool nextSlides = stepSounds(after) && (after & STEP_SLIDE);
                const int deg = sh.note[p][T_BASS][s].load(std::memory_order_relaxed);
                const int semis = degreeSemis(st, deg) + sh.lock[p][T_BASS][s][LOCK_PITCH].load(std::memory_order_relaxed);
                const float cutMul = std::pow(2.f, lockOf(p, T_BASS, s, LOCK_FILTER) * 2.f);
                bassOn(semis, stepKind(v) == STEP_ACCENT, (v & STEP_SLIDE) != 0, cutMul);
                bass.vel = mix(kBassSlot).volume.load(std::memory_order_relaxed);
                const double dm = std::pow(2.0, lockOf(p, T_BASS, s, LOCK_DECAY));
                bassOffAt = sampleCount + static_cast<uint64_t>(len * n * (nextSlides ? 1.1 : 0.85 * dm));
                seqBassActive = true;
            }
        }
        // Chords
        {
            const uint8_t v = sh.on[p][T_CHORD][s].load(std::memory_order_relaxed);
            if (stepSounds(v) && !sh.mute[T_CHORD].load(std::memory_order_relaxed) && audible(kChordSlot)) {
                const int n = noteLength(p, T_CHORD, s);
                const float semis = sh.lock[p][T_CHORD][s][LOCK_PITCH].load(std::memory_order_relaxed) + mix(kChordSlot).tune.load();
                chordOn(sh.note[p][T_CHORD][s].load(std::memory_order_relaxed),
                        (stepKind(v) == STEP_ACCENT ? 1.25f : 1.f) * mix(kChordSlot).volume.load(), semis,
                        std::pow(2.f, lockOf(p, T_CHORD, s, LOCK_FILTER) * 2.f));
                chordOffAt = sampleCount + static_cast<uint64_t>(len * n * 0.9 * std::pow(2.0, lockOf(p, T_CHORD, s, LOCK_DECAY)));
                seqChordActive = true;
            }
        }
        // Lead track (what you recorded). Silent while recording so you only hear what you play.
        {
            const uint8_t v = sh.on[p][T_LEAD][s].load(std::memory_order_relaxed);
            const bool rec = sh.recordLead.load(std::memory_order_relaxed);
            if (recSkipNext) {
                recSkipNext = false;
            } else if (stepSounds(v) && !rec && !sh.mute[T_LEAD].load(std::memory_order_relaxed) && audible(kLeadSlot)) {
                if (seqLeadNote >= 0) lead.noteOff(seqLeadNote);
                const int n = sh.note[p][T_LEAD][s].load(std::memory_order_relaxed) +
                              sh.lock[p][T_LEAD][s][LOCK_PITCH].load(std::memory_order_relaxed);
                lead.noteOn(style(), n, 100);
                seqLeadNote = n;
                leadOffAt = sampleCount + static_cast<uint64_t>(len * noteLength(p, T_LEAD, s) * 0.9);
                hit(kLeadSlot);
                sh.leadNote.store(n, std::memory_order_relaxed);
            }
        }
    }

    void switchPattern(int p) {
        const int oldStyle = pattern / kMaxSlots;
        pattern = p;
        queued = -1;
        sh.pattern.store(pattern);
        sh.queued.store(-1);
        if (p / kMaxSlots != oldStyle) sh.bpm.store(style().bpm);
    }

    // At the end of a pattern: queued pattern, else the next song entry.
    void patternEnded() {
        if (queued >= 0) { switchPattern(queued); return; }
        if (!sh.songMode.load(std::memory_order_relaxed)) return;
        const int n = sh.songLen.load(std::memory_order_relaxed);
        if (n <= 0) return;
        if (++songRep >= std::max(1, sh.songRepeats[songPos % kMaxSong].load())) {
            songRep = 0;
            songPos = (songPos + 1) % n;
            const int slot = sh.songSlot[songPos].load();
            const int st = styleIdx();
            if (slot >= 0 && slot < sh.slotCount[st].load()) switchPattern(st * kMaxSlots + slot);
            sh.songPos.store(songPos);
        }
    }

    void advanceStep() {
        const int len = sh.patternLength(pattern);
        int next = step + 1;
        if (next >= len) {
            next = 0;
            if (step >= 0) patternEnded();
        }
        step = next;
        if (step % kSteps == 0) {
            if (beatsSincePlay > 0.01) ++barCount;
            const int every = sh.fillEvery.load(std::memory_order_relaxed);
            fillBar = every > 0 && (barCount % every) == every - 1;
            sh.bar.store(barCount, std::memory_order_relaxed);
        }
        if (step % 4 == 0) beatPos = std::floor(beatPos + 0.5);
        // a key held while recording the lead: extend its note through this step
        if (recNote >= 0 && sh.recordLead.load(std::memory_order_relaxed) && step != recStep) {
            sh.on[pattern][T_LEAD][step].store(STEP_TIE);
            recStep = step;
        }
        stepLen = lengthOfStep(step);
        sh.step.store(step, std::memory_order_relaxed);
        fireStep(step);
    }

    void sequencerTick() {
        beatPos += 1.0 / samplesPerBeat();
        // Scheduled-note releases run whether or not the groovebox sequencer is playing.
        for (int i = 0; i < releaseCount;) {
            if (sampleCount >= releases[i].at) {
                const Release r = releases[i];
                releases[i] = releases[--releaseCount];
                release(r);
            } else {
                ++i;
            }
        }
        if (!playing) return;
        beatsSincePlay += 1.0 / samplesPerBeat();
        phase += 1.0;
        if (phase >= stepLen) {
            phase -= stepLen;
            if (phase >= stepLen) phase = 0.0;
            advanceStep();
        }
        for (int i = 0; i < hitCount;) {
            if (sampleCount >= hits[i].at) {
                const Hit h = hits[i];
                hits[i] = hits[--hitCount];
                triggerDrum(h.slot, h.vel, h.pitch, h.decay, h.tone);
            } else {
                ++i;
            }
        }
        if (seqBassActive && sampleCount >= bassOffAt) {
            seqBassActive = false;
            if (liveCount == 0) bass.noteOff();
        }
        if (seqChordActive && sampleCount >= chordOffAt) {
            seqChordActive = false;
            if (liveChord == 0) chordOffAll();
        }
        if (seqLeadNote >= 0 && sampleCount >= leadOffAt) {
            lead.noteOff(seqLeadNote);
            seqLeadNote = -1;
        }
    }

    // Lead keyboard notes go into the pattern while recording (quantised to steps).
    void recordLeadOn(int note) {
        if (!playing || !sh.recordLead.load(std::memory_order_relaxed)) return;
        const int len = sh.patternLength(pattern);
        const bool late = phase > stepLen * 0.5;  // closer to the next step
        const int target = late ? (step + 1) % len : std::max(0, step);
        sh.on[pattern][T_LEAD][target].store(STEP_ON);
        sh.note[pattern][T_LEAD][target].store(static_cast<int8_t>(std::max(0, std::min(127, note))));
        for (int l = 0; l < kLocks; ++l) sh.lock[pattern][T_LEAD][target][l].store(0);
        recNote = note;
        recStep = target;
        if (late) recSkipNext = true;  // it's already sounding: don't retrigger on that step
    }
    void recordLeadOff(int note) {
        if (note == recNote) recNote = -1;
    }

    // ---------------------------------------------------- buffer FX --
    void engageBufFx(int fx) {
        const double spb = samplesPerBeat();
        const size_t n = ringL.size();
        auto grab = [&](int len) {
            len = std::max(64, std::min(len, static_cast<int>(loopL.size())));
            for (int k = 0; k < len; ++k) {
                const size_t src = (w + n - static_cast<size_t>(len) + static_cast<size_t>(k)) % n;
                loopL[static_cast<size_t>(k)] = ringL[src];
                loopR[static_cast<size_t>(k)] = ringR[src];
            }
            loopLen = len;
            loopPos = 0;
        };
        switch (fx) {
            case FX_STUT4: grab(static_cast<int>(spb)); loopReverse = false; bufMode = BUF_LOOP; break;
            case FX_STUT8: grab(static_cast<int>(spb / 2)); loopReverse = false; bufMode = BUF_LOOP; break;
            case FX_STUT16: grab(static_cast<int>(spb / 4)); loopReverse = false; bufMode = BUF_LOOP; break;
            case FX_STUT32: grab(static_cast<int>(spb / 8)); loopReverse = false; bufMode = BUF_LOOP; break;
            case FX_REVERSE: grab(static_cast<int>(spb)); loopReverse = true; bufMode = BUF_LOOP; break;
            case FX_TAPESTOP: tapeR = static_cast<double>(w); tapeRate = 1.f; bufMode = BUF_TAPE; break;
            default: return;
        }
        bufTarget = true;
    }
    static bool isBufFx(int f) {
        return f == FX_STUT4 || f == FX_STUT8 || f == FX_STUT16 || f == FX_STUT32 || f == FX_TAPESTOP || f == FX_REVERSE;
    }
    void fxOn(int f) {
        if (f < 0 || f >= FX_COUNT || fxHeld[f]) return;
        fxHeld[f] = true;
        if (isBufFx(f)) {
            if (bufCount < 8) bufStack[bufCount++] = f;
            engageBufFx(f);
        }
        switch (f) {
            case FX_HALFTIME: halftime = true; break;
            case FX_SIREN: siren.held = true; break;
            case FX_RISER: riser.held = true; riser.t = 0.f; break;
            case FX_HORN: horn.held = true; horn.t = 0.f; break;
            case FX_LASER: laser.held = true; laser.next = 0.0; break;
            default: break;
        }
    }
    void fxOff(int f) {
        if (f < 0 || f >= FX_COUNT || !fxHeld[f]) return;
        fxHeld[f] = false;
        if (isBufFx(f)) {
            for (int i = 0; i < bufCount; ++i)
                if (bufStack[i] == f) {
                    for (int j = i; j < bufCount - 1; ++j) bufStack[j] = bufStack[j + 1];
                    --bufCount;
                    break;
                }
            if (bufCount > 0) engageBufFx(bufStack[bufCount - 1]);
            else bufTarget = false;
        }
        switch (f) {
            case FX_HALFTIME: halftime = false; break;
            case FX_SIREN: siren.held = false; break;
            case FX_RISER: riser.held = false; break;
            case FX_HORN: horn.held = false; break;
            case FX_LASER: laser.held = false; break;
            default: break;
        }
    }
    void bufferFx(float& l, float& r) {
        if (bufMode == BUF_NONE) return;
        float bl = l, br = r;
        if (bufMode == BUF_LOOP) {
            const int idx = loopReverse ? loopLen - 1 - loopPos : loopPos;
            const float win = std::min(1.f, std::min(static_cast<float>(loopPos), static_cast<float>(loopLen - 1 - loopPos)) / 96.f);
            bl = loopL[static_cast<size_t>(idx)] * win;
            br = loopR[static_cast<size_t>(idx)] * win;
            if (++loopPos >= loopLen) loopPos = 0;
        } else if (bufMode == BUF_TAPE) {
            tapeRate = std::max(0.f, tapeRate - static_cast<float>(1.0 / (samplesPerBeat() * 0.9)));
            const float g = std::min(1.f, tapeRate * 3.f);
            bl = readFrac(ringL, tapeR) * g;
            br = readFrac(ringR, tapeR) * g;
            tapeR += tapeRate;
            if (tapeR >= static_cast<double>(ringL.size())) tapeR -= static_cast<double>(ringL.size());
        }
        bufXf += bufTarget ? xfStep : -xfStep;
        if (bufXf >= 1.f) bufXf = 1.f;
        if (bufXf <= 0.f) {
            bufXf = 0.f;
            if (!bufTarget) { bufMode = BUF_NONE; tapeRate = 1.f; }
        }
        l += (bl - l) * bufXf;
        r += (br - r) * bufXf;
    }

    // ------------------------------------------------------ commands --
    void apply(const Cmd& c) {
        switch (c.type) {
            case Cmd::Trigger:
                if (c.a == kChordSlot) {  // one-shot chord (e.g. from a drum pad): one beat long
                    chordOn(0, 1.f);
                    chordOffAt = sampleCount + static_cast<uint64_t>(samplesPerBeat());
                    seqChordActive = true;
                } else {
                    triggerDrum(c.a, mix(c.a).volume.load());
                }
                break;
            case Cmd::BassOn: liveBassOn(c.a); break;
            case Cmd::BassOff: liveBassOff(c.a); break;
            case Cmd::ChordOn:
                ++liveChord;
                seqChordActive = false;
                chordOn(c.a, 1.f);
                break;
            case Cmd::ChordOff:
                liveChord = std::max(0, liveChord - 1);
                if (liveChord == 0) chordOffAll();
                break;
            case Cmd::LeadOn:
                lead.noteOn(style(), c.a, c.b);
                sh.leadNote.store(c.a, std::memory_order_relaxed);
                hit(kLeadSlot);
                recordLeadOn(c.a);
                break;
            case Cmd::LeadOff: lead.noteOff(c.a); recordLeadOff(c.a); break;
            case Cmd::JumpPattern:
                pattern = c.a;
                if (queued >= 0) { queued = -1; sh.queued.store(-1); }
                sh.pattern.store(c.a);
                if (step >= sh.patternLength(pattern)) step = -1;
                break;
            case Cmd::LeadPatch:
                lead.allOff();
                lead.patch = std::max(0, std::min(c.a, static_cast<int>(leadPatches().size()) - 1));
                break;
            case Cmd::LeadSustain: lead.setSustain(c.a != 0); break;
            case Cmd::LeadBend: lead.bend = c.f; break;
            case Cmd::LeadMod: lead.mod = c.f; break;
            case Cmd::LeadAllOff:
                lead.allOff();
                lead.setSustain(false);
                for (int i = 0; i < releaseCount;)  // scheduled lead releases have nothing left to do
                    if (releases[i].slot == kLeadSlot) releases[i] = releases[--releaseCount];
                    else ++i;
                break;
            case Cmd::Note: playNote(c.a, c.b, c.f, c.g); break;
            case Cmd::SynthPatch:
                if (c.a >= 0 && c.a < kSynths) {
                    LeadSynth& sy = synths[c.a];
                    const int p = std::max(0, std::min(c.b, static_cast<int>(leadPatches().size()) - 1));
                    if (p != sy.patch) { sy.allOff(); sy.patch = p; }
                }
                break;
            case Cmd::FxOn: fxOn(c.a); break;
            case Cmd::FxOff: fxOff(c.a); break;
            case Cmd::SetSample:
                if (c.a >= 0 && c.a < kMaxStyles && c.b >= 0 && c.b < kDrumSlots)
                    samples[c.a][c.b] = static_cast<const Sample*>(c.ptr);
                break;
            case Cmd::Play:
                playing = true;
                step = -1;
                phase = 0.0;
                stepLen = 0.0;  // the first step fires on the very next sample
                barCount = 0;
                beatsSincePlay = 0.0;
                fillBar = crashNext = false;
                songPos = 0;
                songRep = 0;
                if (sh.songMode.load() && sh.songLen.load() > 0) {
                    const int slot = sh.songSlot[0].load(), st = styleIdx();
                    if (slot >= 0 && slot < sh.slotCount[st].load()) switchPattern(st * kMaxSlots + slot);
                    sh.songPos.store(0);
                }
                sh.clockBeats.store(0.0);
                sh.clockTime.store(blockNow + curFrame / static_cast<double>(sr));
                sh.playing.store(true);
                break;
            case Cmd::Stop:
                playing = false;
                if (liveCount == 0) bass.noteOff();
                if (liveChord == 0) chordOffAll();
                if (seqLeadNote >= 0) { lead.noteOff(seqLeadNote); seqLeadNote = -1; }
                seqBassActive = seqChordActive = false;
                hitCount = 0;
                recNote = -1;
                sh.playing.store(false);
                break;
            case Cmd::QueuePattern:
                if (c.a < 0 || c.a / kMaxSlots >= styleCount() || c.a % kMaxSlots >= sh.slotCount[c.a / kMaxSlots].load()) break;
                if (playing) {
                    queued = c.a;
                    sh.queued.store(c.a);
                } else {
                    switchPattern(c.a);
                }
                break;
        }
    }

    // --------------------------------------------------------- render --
    void process(float* out, int frames, double now) {
        disableDenormals();
        const auto t0 = std::chrono::steady_clock::now();
        blockNow = now;
        curFrame = 0;
        const double blockSec = frames / static_cast<double>(sr);

        // Commands land at the sample matching when they were sent, one block
        // later: constant latency instead of block-sized jitter. Ones stamped for
        // a moment after this block's start wait for their own block.
        Cmd cmds[256];
        int off[256];
        int nc = 0;
        auto take = [&](const Cmd& c) {
            const double o = (c.t + blockSec - now) * sr;
            if (c.t > now && o >= frames && laterCount < 512) return false;  // not this block yet
            off[nc] = std::max(0, std::min(frames - 1, static_cast<int>(o)));
            cmds[nc++] = c;
            return true;
        };
        {
            int keep = 0;
            for (int i = 0; i < laterCount; ++i) {
                if (nc >= 256 || !take(later[i])) later[keep++] = later[i];
            }
            laterCount = keep;
        }
        Cmd c;
        while (nc < 256 && sh.cmds.pop(c)) {
            if (!take(c)) later[laterCount++] = c;
        }
        for (int i = 1; i < nc; ++i)  // stable insertion sort by offset
            for (int j = i; j > 0 && off[j] < off[j - 1]; --j) {
                std::swap(off[j], off[j - 1]);
                std::swap(cmds[j], cmds[j - 1]);
            }
        int ci = 0;

        const Style& stAtStart = style();
        if (scRelease != stAtStart.duckRelease) {
            scRelease = stAtStart.duckRelease;
            scFollow.set(1.f, scRelease * 1000.f, sr);
        }
        reverb.setSize(std::max(sh.reverbSize.load(), fxHeld[FX_REVERB] ? 0.92f : 0.f));
        const float volTarget = clampf(sh.volume.load(std::memory_order_relaxed), 0.f, 1.f);
        const float lv = clampf(sh.leadVolume.load(std::memory_order_relaxed), 0.f, 1.f);
        const float fb = clampf(sh.delayFeedback.load(), 0.f, 0.9f);
        const float chorusAmt = clampf(sh.chorus.load(), 0.f, 1.f);
        const float width = clampf(sh.width.load(), 0.f, 1.5f);
        const float glueAmt = clampf(sh.glue.load(), 0.f, 1.f);
        glue.ratio = 1.5f + glueAmt * 2.5f;
        glue.threshDb = -9.f - glueAmt * 9.f;
        glue.makeupDb = glueAmt * 4.f;
        const float lpUpBase = 1.f / static_cast<float>(samplesPerBeat() * 2.0);
        const float lpDownBase = 1.f / static_cast<float>(samplesPerBeat() * 0.25);

        for (int i = 0; i < frames; ++i) {
            curFrame = i;
            while (ci < nc && off[ci] <= i) apply(cmds[ci++]);
            sequencerTick();

            const Style& st = style();
            const double spb = samplesPerBeat();
            const float beatHz = static_cast<float>(sr / spb);

            // --- drums (stereo); the kick feeds the sidechain key
            float dL = 0.f, dR = 0.f, key = 0.f, revL = 0.f, revR = 0.f, dlyS = 0.f;
            for (int s = 0; s < kDrumSlots; ++s) {
                float l = 0.f, r = 0.f;
                const float m = drums[s].tick(sr, rng, l, r);
                if (drums[s].isKick) key += m;
                dL += l;
                dR += r;
                ChannelMix& cm = mix(s);
                const float rs = drums[s].d.verb + cm.reverb.load(std::memory_order_relaxed);
                revL += l * rs;
                revR += r * rs;
                dlyS += 0.5f * (l + r) * cm.delay.load(std::memory_order_relaxed);
            }
            // --- bass + chords (the music bus, ducked by the sidechain)
            float bL = 0.f, bR = 0.f, cL = 0.f, cR = 0.f;
            bass.tick(sr, beatHz, 1.f, bL, bR);
            for (auto& ch : chords) ch.tick(sr, rng, 1.f, cL, cR);
            {
                float gl, gr;
                panGains(mix(kBassSlot).pan.load(), gl, gr);
                bL *= gl; bR *= gr;
                panGains(mix(kChordSlot).pan.load(), gl, gr);
                const float cv = mix(kChordSlot).volume.load();
                cL *= gl * cv; cR *= gr * cv;
            }
            const float env = scFollow.tick(key);
            const float depth = st.duck * clampf(sh.sidechain.load(std::memory_order_relaxed), 0.f, 1.5f);
            const float duck = 1.f - std::min(0.95f, depth * std::min(1.f, env * 2.6f));
            const float mL = (bL * 0.9f + cL) * duck, mR = (bR * 0.9f + cR) * duck;
            const ChordDef& cd = st.chord;
            const ChannelMix& cmx = mix(kChordSlot);
            const ChannelMix& bmx = mix(kBassSlot);

            // --- lead
            float lL = 0.f, lR = 0.f;
            lead.tick(sr, beatHz, rng, lL, lR);
            const LeadPatch& lp = lead.p();
            {
                ChannelMix& lm = mix(kLeadSlot);
                float gl, gr;
                panGains(lm.pan.load(), gl, gr);
                const float g = lv * lv * 1.2f * lm.volume.load() * (1.f - 0.25f * (1.f - duck));
                lL *= g * gl;
                lR *= g * gr;
            }
            const float leadChorus = lp.kind == LeadPatch::POLY ? lp.poly.chorus : 0.f;

            // --- layer synths (lp-tenori): through the Lead channel, each with its
            // own sound's chorus / reverb / delay amounts.
            float yL = 0.f, yR = 0.f, ycL = 0.f, ycR = 0.f, yvL = 0.f, yvR = 0.f, yd = 0.f;
            for (auto& sy : synths) {
                if (sy.idle()) continue;
                float a = 0.f, b = 0.f;
                sy.tick(sr, beatHz, rng, a, b);
                const LeadPatch& p = sy.p();
                const float ch = p.kind == LeadPatch::POLY ? p.poly.chorus : 0.f;
                yL += a; yR += b;
                ycL += a * ch; ycR += b * ch;
                yvL += a * p.verb; yvR += b * p.verb;
                yd += 0.5f * (a + b) * p.delay;
            }
            {
                ChannelMix& lm = mix(kLeadSlot);
                float gl, gr;
                panGains(lm.pan.load(), gl, gr);
                const float g = 1.2f * lm.volume.load() * (1.f - 0.25f * (1.f - duck));
                yL *= g * gl; yR *= g * gr;
                ycL *= g * gl; ycR *= g * gr;
                yvL *= g * gl; yvR *= g * gr;
                yd *= g;
            }

            // --- FX sounds (mono, centred)
            const float sirenOut = siren.tick(sr), hornOut = horn.tick(sr);
            const float laserOut = laser.tick(sr, spb / 2.0, static_cast<double>(sampleCount));
            const float fxs = (sirenOut + riser.tick(sr, rng) + hornOut + laserOut) * 0.6f;

            // --- chorus send (chords / pads / keys)
            float chL, chR;
            const float chSend = cd.chorus * chorusAmt;
            chorus.tick(cL * duck * chSend + (lL * leadChorus + ycL) * chorusAmt, cR * duck * chSend + (lR * leadChorus + ycR) * chorusAmt, chL, chR);

            const float trim = st.trim;
            float xL = (dL * 0.85f + mL) * trim + lL + yL + fxs + chL * 0.8f;
            float xR = (dR * 0.85f + mR) * trim + lR + yR + fxs + chR * 0.8f;
            const float lRev = mix(kLeadSlot).reverb.load(), lDly = mix(kLeadSlot).delay.load();
            const float auxRevL = revL * 0.6f * trim + cL * (cd.verb + cmx.reverb.load()) + bL * bmx.reverb.load() + lL * (lp.verb + lRev) + yvL + yL * lRev;
            const float auxRevR = revR * 0.6f * trim + cR * (cd.verb + cmx.reverb.load()) + bR * bmx.reverb.load() + lR * (lp.verb + lRev) + yvR + yR * lRev;
            const float auxDly = sirenOut * 0.6f + hornOut * 0.25f + laserOut * 0.5f + 0.5f * (cL + cR) * (cd.delay + cmx.delay.load()) +
                                 0.5f * (bL + bR) * bmx.delay.load() + 0.5f * (lL + lR) * (lp.delay + lDly) + yd + 0.5f * (yL + yR) * lDly + dlyS;

            // --- buffer effects (stutter / reverse / tape stop)
            ringL[w] = xL;
            ringR[w] = xR;
            bufferFx(xL, xR);
            if (++w >= ringL.size()) w = 0;

            // --- master filters
            lpAmt = fxHeld[FX_LPF] ? std::min(1.f, lpAmt + lpUpBase) : std::max(0.f, lpAmt - lpDownBase);
            hpAmt = fxHeld[FX_HPF] ? std::min(1.f, hpAmt + lpUpBase) : std::max(0.f, hpAmt - lpDownBase);
            if (lpAmt > 0.f) {
                lpfL.set(20000.f * std::pow(2.f, -lpAmt * 6.f), 0.8f + lpAmt * 1.8f, sr);
                lpfR.a1 = lpfL.a1; lpfR.a2 = lpfL.a2; lpfR.a3 = lpfL.a3; lpfR.k = lpfL.k;
                xL = lpfL.lp(xL);
                xR = lpfR.lp(xR);
            } else { lpfL.reset(); lpfR.reset(); }
            if (hpAmt > 0.f) {
                hpfL.set(20.f * std::pow(2.f, hpAmt * 7.f), 0.8f + hpAmt * 1.2f, sr);
                hpfR.a1 = hpfL.a1; hpfR.a2 = hpfL.a2; hpfR.a3 = hpfL.a3; hpfR.k = hpfL.k;
                xL = hpfL.hp(xL);
                xR = hpfR.hp(xR);
            } else { hpfL.reset(); hpfR.reset(); }

            // --- bitcrush
            const float cm = crushMix.tick(fxHeld[FX_CRUSH] ? 1.f : 0.f);
            if (cm > 0.001f) {
                if (++crushCount >= 6) {
                    crushCount = 0;
                    crushL = std::round(xL * 10.f) / 10.f;
                    crushR = std::round(xR * 10.f) / 10.f;
                }
                xL += (crushL - xL) * cm;
                xR += (crushR - xR) * cm;
            }

            // --- lo-fi vinyl: darker, warbly, crackly
            const float lm = lofiMix.tick(fxHeld[FX_LOFI] ? 1.f : 0.f);
            wowL[wowW] = xL;
            wowR[wowW] = xR;
            if (lm > 0.001f) {
                advPhase(wowPh, 0.55f / sr);
                const double wd = sr * (0.004 + 0.0012 * std::sin(kTwoPi * wowPh));
                const float wl = lofiL2.tick(lofiL1.tick(readFrac(wowL, static_cast<double>(wowW) - wd)));
                const float wr = lofiR2.tick(lofiR1.tick(readFrac(wowR, static_cast<double>(wowW) - wd)));
                if (rng.next() > 0.9994f) crackle = rng.next();
                crackle *= 0.9f;
                const float cr = crackleLp.tick(crackle) * 0.5f;
                xL += (std::tanh(wl * 1.3f) * 0.85f + cr - xL) * lm;
                xR += (std::tanh(wr * 1.3f) * 0.85f + cr - xR) * lm;
            }
            if (++wowW >= wowL.size()) wowW = 0;

            // --- flanger (stereo: the two sides sweep a quarter-cycle apart)
            const float fm = flMix.tick(fxHeld[FX_FLANGE] ? 1.f : 0.f);
            advPhase(flLfo, 0.35f / sr);
            const double fdl = sr * (0.0012 + 0.0045 * (0.5 + 0.5 * std::sin(kTwoPi * flLfo)));
            const double fdr = sr * (0.0012 + 0.0045 * (0.5 + 0.5 * std::cos(kTwoPi * flLfo)));
            const float fl = readFrac(flL, static_cast<double>(flW) - fdl), fr = readFrac(flR, static_cast<double>(flW) - fdr);
            flL[flW] = xL + fl * 0.65f * fm;
            flR[flW] = xR + fr * 0.65f * fm;
            if (++flW >= flL.size()) flW = 0;
            xL = (xL + fl * fm) / (1.f + 0.6f * fm);
            xR = (xR + fr * fm) / (1.f + 0.6f * fm);

            // --- sidechain pump and trance gate, locked to the beat
            const float bp = static_cast<float>(beatPos - std::floor(beatPos));
            const float pm = pumpMix.tick(fxHeld[FX_PUMP] ? 1.f : 0.f);
            if (pm > 0.001f) {
                const float k = std::min(1.f, bp / 0.45f);
                const float pump = 1.f - pm * (1.f - (0.12f + 0.88f * k * k * (3.f - 2.f * k)));
                xL *= pump;
                xR *= pump;
            }
            const float gm = gateMix.tick(fxHeld[FX_GATE] ? 1.f : 0.f);
            {
                float sub = bp * 4.f;
                sub -= std::floor(sub);
                const float g = gateSmooth.tick(sub < 0.5f ? 1.f : 0.f);
                if (gm > 0.001f) {
                    const float gg = 1.f - gm * (1.f - g);
                    xL *= gg;
                    xR *= gg;
                }
            }

            // --- ping-pong delay (time and feedback from the settings)
            const float ds = dSend.tick(fxHeld[FX_DELAY] ? 1.f : 0.f);
            const double dsamp = dTime.tick(static_cast<float>(spb * delayDivBeats(sh.delayDiv.load(std::memory_order_relaxed))));
            const double rd = static_cast<double>(dW) - std::min(dsamp, static_cast<double>(dlL.size() - 4));
            const float dl = readFrac(dlL, rd), dr = readFrac(dlR, rd);
            dlL[dW] = 0.5f * (xL + xR) * ds + auxDly + dfR.tick(dr) * fb;
            dlR[dW] = dfL.tick(dl) * fb;
            if (++dW >= dlL.size()) dW = 0;

            // --- reverb (FDN; holding Reverb wash pushes a big send and a long tail)
            const float rs = rSend.tick(fxHeld[FX_REVERB] ? 1.f : 0.f);
            float rl, rr;
            reverb.tick(xL * rs + auxRevL, xR * rs + auxRevR, rl, rr);

            float L = xL + dl * 0.6f + rl * 0.75f;
            float R = xR + dr * 0.6f + rr * 0.75f;

            // --- stereo width (mid/side)
            {
                const float mid = 0.5f * (L + R), side = 0.5f * (L - R) * width;
                L = mid + side;
                R = mid - side;
            }
            // --- master: gentle EQ, glue compression, volume, look-ahead limiter
            L = highShelfL.tick(lowShelfL.tick(L));
            R = highShelfR.tick(lowShelfR.tick(R));
            L *= 0.7f;
            R *= 0.7f;
            glue.process(L, R);
            vol += (volTarget * volTarget - vol) * 0.001f;  // squared = more natural taper
            L *= vol;
            R *= vol;
            limiter.process(L, R);
            out[2 * i] = L;
            out[2 * i + 1] = R;

            levelL = std::fabs(L) > levelL ? std::fabs(L) : levelL * 0.9997f;
            levelR = std::fabs(R) > levelR ? std::fabs(R) : levelR * 0.9997f;
            ++sampleCount;
        }
        while (ci < nc) apply(cmds[ci++]);

        if (sh.recordAudio.load(std::memory_order_relaxed)) sh.recFifo.write(out, static_cast<size_t>(frames) * 2);

        level = std::max(levelL, levelR);
        sh.level.store(level, std::memory_order_relaxed);
        sh.levelL.store(levelL, std::memory_order_relaxed);
        sh.levelR.store(levelR, std::memory_order_relaxed);
        sh.gainReduction.store(glue.grDb + 20.f * -std::log10(std::max(0.01f, 1.f - limiter.reduction())), std::memory_order_relaxed);
        sh.lpAmt.store(lpAmt, std::memory_order_relaxed);
        sh.hpAmt.store(hpAmt, std::memory_order_relaxed);
        sh.riserAmt.store(riser.held ? riser.progress() : 0.f, std::memory_order_relaxed);
        sh.tapeRate.store(bufMode == BUF_TAPE ? tapeRate : 1.f, std::memory_order_relaxed);
        sh.bassPlaying.store(bass.gate ? bass.semi : -1000, std::memory_order_relaxed);
        sh.beatPhase.store(static_cast<float>(beatPos - std::floor(beatPos)), std::memory_order_relaxed);
        sh.clockBeats.store(beatsSincePlay, std::memory_order_relaxed);
        sh.clockTime.store(now + blockSec, std::memory_order_relaxed);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cpu += (static_cast<float>(took / blockSec) - cpu) * 0.1f;
        sh.cpuLoad.store(cpu, std::memory_order_relaxed);
    }
};

Engine::Engine(Shared& shared, float sampleRate) : impl_(std::make_unique<Impl>(shared, sampleRate)) {}
Engine::~Engine() = default;
void Engine::process(float* out, int frames) { impl_->process(out, frames, monotonicSec()); }
void Engine::process(float* out, int frames, double now) { impl_->process(out, frames, now); }

}  // namespace dnb
