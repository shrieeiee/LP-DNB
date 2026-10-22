#include "engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp.h"
#include "styles.h"

namespace dnb {
namespace {

inline float midiHz(float note) { return 440.f * std::pow(2.f, (note - 69.f) / 12.f); }

inline float sqWave(float ph, float dt) {
    float p2 = ph + 0.5f;
    if (p2 >= 1.f) p2 -= 1.f;
    return (ph < 0.5f ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(p2, dt);
}

inline void adv(float& ph, float inc) {
    ph += inc;
    if (ph >= 1.f) ph -= std::floor(ph);
}

// ---------------------------------------------------------------- drums --

struct DrumVoice {
    DrumDef d;
    bool active = false;
    float t = 0.f, vel = 0.f, ck = 1.f, ckMul = 1.f;
    float ph[6] = {};
    SVF f1, f2;

    void trig(const DrumDef& def, float v, float sr) {
        d = def;
        t = 0.f;
        vel = v * def.gain;
        ck = 1.f;
        ckMul = 1.f;
        for (float& p : ph) p = 0.f;
        active = true;
        f1.reset();
        f2.reset();
        switch (d.type) {
            case DrumType::Snare: f1.set(d.p[3], 0.7f, sr); f2.set(d.p[5], 0.7f, sr); break;
            case DrumType::Clap: f1.set(d.p[0], d.p[3], sr); break;
            case DrumType::Metal: f1.set(d.p[1], d.p[2], sr); f2.set(d.p[3], 0.7f, sr); break;
            case DrumType::Rim: f1.set(d.p[0] * 1.3f, 2.f, sr); break;
            case DrumType::Shaker: f1.set(d.p[0], 0.7f, sr); f2.set(d.p[0] * 1.6f, 1.2f, sr); break;
            case DrumType::Cowbell: f1.set(2000.f, 1.4f, sr); break;
            default: break;
        }
    }
    void choke(float sr) {
        if (active) ckMul = std::exp(-150.f / sr);
    }

    float tick(float sr, Rng& rng) {
        if (!active) return 0.f;
        const float* p = d.p;
        float out = 0.f, life = 2.f;
        switch (d.type) {
            case DrumType::Kick: {
                float f = p[1] + (p[0] - p[1]) * std::exp(-t * p[2]);
                adv(ph[0], f / sr);
                float body = std::sin(kTwoPi * ph[0]) * std::exp(-t * p[3]);
                float click = rng.next() * std::exp(-t * 900.f) * p[4];
                float drive = 1.f + p[5] * 3.f;
                out = std::tanh(body * drive + click) / std::tanh(drive) * 0.95f;
                life = 9.2f / p[3];
                break;
            }
            case DrumType::Snare: {
                float bend = 1.f + 0.5f * std::exp(-t * 60.f);
                adv(ph[0], p[0] * bend / sr);
                adv(ph[1], p[0] * 1.74f * bend / sr);
                float tone = (std::sin(kTwoPi * ph[0]) * 0.6f + std::sin(kTwoPi * ph[1]) * 0.4f) * std::exp(-t * p[1]);
                float n = rng.next();
                float noise = f2.lp(f1.hp(n)) * std::exp(-t * p[2]) + n * std::exp(-t * 120.f) * 0.4f;
                out = std::tanh((tone * p[4] * 1.6f + noise * (1.f - p[4]) * 1.6f) * 1.5f);
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
                out = f1.bp(rng.next()) * env * 2.6f;
                life = sp * 3.f + 9.2f / p[1];
                break;
            }
            case DrumType::Metal: {
                static const float freqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
                float sq = 0.f;
                for (int i = 0; i < 6; ++i) {
                    adv(ph[i], freqs[i] * p[0] / sr);
                    sq += ph[i] < 0.5f ? 1.f : -1.f;
                }
                float x = sq * (1.f / 6.f) * (1.f - p[5]) + rng.next() * p[5];
                out = f2.hp(f1.bp(x)) * std::exp(-t * p[4]) * 1.6f;
                life = 9.2f / p[4];
                break;
            }
            case DrumType::Rim: {
                adv(ph[0], p[0] / sr);
                adv(ph[1], p[0] * 0.36f / sr);
                out = (std::sin(kTwoPi * ph[0]) * 0.7f + std::sin(kTwoPi * ph[1]) * 0.5f) * std::exp(-t * p[1]) +
                      f1.bp(rng.next()) * std::exp(-t * 400.f) * 0.5f;
                life = 9.2f / p[1];
                break;
            }
            case DrumType::Tom: {
                float f = p[0] * (1.f + p[1] * std::exp(-t * 18.f));
                adv(ph[0], f / sr);
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
                adv(ph[0], p[0] / sr);
                adv(ph[1], p[1] / sr);
                float x = (ph[0] < 0.5f ? 1.f : -1.f) + (ph[1] < 0.5f ? 1.f : -1.f);
                float env = std::exp(-t * p[2]) * 0.75f + std::exp(-t * p[2] * 6.f) * 0.25f;
                out = f1.bp(x * 0.5f) * env * 1.8f;
                life = 9.2f / p[2];
                break;
            }
        }
        t += 1.f / sr;
        ck *= ckMul;
        if (t > life || ck < 1e-4f) active = false;
        return out * vel * ck;
    }
};

// ----------------------------------------------------------------- bass --

struct MonoVoice {
    MonoDef d;
    Saw s1, s2;
    float sqPh = 0.f, sinPh = 0.f, subPh = 0.f, lfoPh = 0.f;
    float freq = 55.f, target = 55.f, glide = 0.f;
    float amp = 0.f, held = 0.f, fenv = 0.f, dropEnv = 0.f, accentK = 1.f;
    bool gate = false;
    int semi = -1000;
    SVF filt;
    int counter = 0;

    void noteOn(const MonoDef& def, int root, int semis, bool accent, bool slide) {
        const bool legato = slide && gate && amp > 0.01f;
        d = def;
        semi = semis;
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
    bool sounding() const { return gate || amp > 1e-4f; }

    float tick(float sr, float beatHz) {
        if (!gate && amp < 1e-5f) {
            amp = 0.f;
            return 0.f;
        }
        if (glide > 0.f) freq += (target - freq) * (1.f - std::exp(-1.f / (glide * sr)));
        else freq = target;
        float pitch = freq;
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
        adv(lfoPh, d.lfoRate * beatHz / sr);

        if ((counter++ & 7) == 0) {
            float lfo = d.lfoDepth * (0.5f - 0.5f * std::cos(kTwoPi * lfoPh));
            filt.set(d.cutoff + d.envAmt * fenv * fenv * accentK + lfo, d.res, sr);
        }
        float x = 0.f;
        if (d.saw > 0.f) {
            float dt = d.detune * 0.01f;
            x += (s1.tick(pitch * (1.f - dt), sr) + s2.tick(pitch * (1.f + dt), sr)) * 0.5f * d.saw;
        }
        if (d.square > 0.f) {
            adv(sqPh, pitch / sr);
            x += sqWave(sqPh, pitch / sr) * d.square * 0.7f;
        }
        float tone = filt.lp(x);
        adv(sinPh, pitch / sr);
        tone += std::sin(kTwoPi * sinPh) * d.sine;
        adv(subPh, pitch * 0.5f / sr);
        float drv = std::max(0.5f, d.drive);
        float y = std::tanh(tone * drv * (accentK > 1.f ? 1.2f : 1.f)) / std::tanh(drv) * 0.5f +
                  std::sin(kTwoPi * subPh) * d.sub * 0.6f;
        return y * amp * d.gain;
    }
};

// --------------------------------------------------------------- chords --

struct ChordVoice {
    ChordDef d;
    int n = 0;
    float freqs[5] = {};
    float ph[5][2] = {};
    Saw saws[5][2];
    float t = 10.f, env = 0.f, relFrom = 0.f, fenv = 0.f, vel = 1.f;
    bool gate = false, active = false;
    SVF filt;
    int counter = 0;

    void noteOn(const ChordDef& def, const float* f, int count, float v) {
        d = def;
        n = count;
        for (int i = 0; i < n; ++i) freqs[i] = f[i];
        t = 0.f;
        fenv = 1.f;
        gate = true;
        active = true;
        vel = v;
        for (auto& p : ph) p[0] = p[1] = 0.f;
    }
    void noteOff() {
        if (gate) {
            gate = false;
            relFrom = env;
        }
    }
    float tick(float sr, Rng& rng) {
        if (!active) return 0.f;
        // envelope
        if (gate) {
            if (t < d.attack) env = t / d.attack;
            else if (d.decay > 0.f) env = d.sustain + (1.f - d.sustain) * std::exp(-(t - d.attack) * d.decay);
            else env = 1.f;
        } else {
            env *= std::exp(-d.release / sr);
            if (env < 1e-4f) {
                active = false;
                return 0.f;
            }
        }
        if (gate && d.decay > 0.f && d.sustain <= 0.f && t > d.attack && env < 1e-4f) {
            active = false;
            return 0.f;
        }
        fenv *= std::exp(-d.envDecay / sr);
        if ((counter++ & 7) == 0) filt.set(d.cutoff + d.envAmt * fenv, d.res, sr);

        float x = 0.f;
        const float det = d.detune * 0.01f;
        for (int i = 0; i < n; ++i) {
            const float f = freqs[i];
            switch (d.osc) {
                case ChordOsc::Saw:
                    x += saws[i][0].tick(f * (1.f - det), sr) + saws[i][1].tick(f * (1.f + det), sr);
                    break;
                case ChordOsc::Square:
                    adv(ph[i][0], f / sr);
                    x += sqWave(ph[i][0], f / sr) * 1.4f;
                    break;
                case ChordOsc::EPiano: {
                    adv(ph[i][0], f / sr);
                    adv(ph[i][1], f / sr);
                    float idx = 1.4f * std::exp(-t * 7.f) + 0.25f;
                    x += std::sin(kTwoPi * ph[i][0] + idx * std::sin(kTwoPi * ph[i][1])) * 1.6f;
                    break;
                }
                case ChordOsc::Organ: {
                    adv(ph[i][0], f / sr);
                    float a = kTwoPi * ph[i][0];
                    x += (std::sin(a) + 0.5f * std::sin(2.f * a) + 0.35f * std::sin(3.f * a) + 0.2f * std::sin(4.f * a)) * 1.1f;
                    break;
                }
                case ChordOsc::Bell: {
                    adv(ph[i][0], f / sr);
                    adv(ph[i][1], f * 3.5f / sr);
                    float idx = 2.5f * std::exp(-t * 3.f);
                    x += std::sin(kTwoPi * ph[i][0] + idx * std::sin(kTwoPi * ph[i][1])) * 1.4f;
                    break;
                }
                case ChordOsc::Marimba: {
                    adv(ph[i][0], f / sr);
                    adv(ph[i][1], f * 3.93f / sr);
                    x += (std::sin(kTwoPi * ph[i][0]) + 0.5f * std::sin(kTwoPi * ph[i][1]) * std::exp(-t * 30.f)) * 1.5f;
                    break;
                }
            }
        }
        if (d.osc == ChordOsc::Organ && t < 0.01f) x += rng.next() * 0.3f * (1.f - t * 100.f);  // key click
        float y = filt.lp(x * (0.55f / std::sqrt(static_cast<float>(std::max(1, n)))));
        t += 1.f / sr;
        return y * env * d.gain * vel;
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
        adv(lfo, 3.2f / sr);
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
    int counter = 0;
    float progress() const { return std::min(1.f, t / 6.f); }
    float tick(float sr, Rng& rng) {
        if (held) t += 1.f / sr;
        float target = held ? 0.2f + 0.8f * progress() : 0.f;
        amp += (target - amp) * (held ? 0.002f : 0.0008f);
        if (amp < 1e-4f) return 0.f;
        float tt = std::min(t, 8.f);
        if ((counter++ & 15) == 0) bp.set(std::min(250.f * std::pow(2.f, tt * 0.85f), 14000.f), 3.f, sr);
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
        adv(vib, 6.f / sr);
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
        adv(ph, f / sr);
        float y = (ph < 0.5f ? 1.f : -1.f) * std::exp(-t * 7.f) * 0.22f;
        t += 1.f / sr;
        return y;
    }
};

}  // namespace

// ================================================================ Impl ==

struct Engine::Impl {
    Shared& sh;
    float sr;
    Rng rng;
    uint64_t sampleCount = 0;

    DrumVoice drums[7];
    MonoVoice bass;
    ChordVoice chords[3];
    int curChord = 0;
    SirenVoice siren;
    RiserVoice riser;
    HornVoice horn;
    LaserVoice laser;
    float duck = 0.f, duckAmt = 0.5f;
    float drumVerb = 0.f;  // per-sample reverb send from drums

    // sequencer
    bool playing = false;
    int step = 15;
    double phase = 0.0, stepLen = 1.0;
    int pattern = 0, queued = -1;
    bool halftime = false;
    double beatPos = 0.0;
    uint64_t bassOffAt = 0, chordOffAt = 0;
    bool seqBassActive = false, seqChordActive = false;
    int liveBass[16];
    int liveCount = 0;
    int liveChord = 0;
    struct Roll { uint64_t at; int slot; float vel; };
    Roll rolls[32];
    int rollCount = 0;

    // buffer FX
    bool fxHeld[FX_COUNT] = {};
    int bufStack[8];
    int bufCount = 0;
    enum BufMode { BUF_NONE, BUF_LOOP, BUF_TAPE } bufMode = BUF_NONE;
    bool bufTarget = false;
    float bufXf = 0.f, xfStep = 0.f;
    std::vector<float> ring, loopBuf;
    size_t w = 0;
    int loopLen = 1, loopPos = 0;
    bool loopReverse = false;
    double tapeR = 0.0;
    float tapeRate = 1.f;

    float lpAmt = 0.f, hpAmt = 0.f;
    SVF lpf, hpf;
    int filtCounter = 0;

    Smooth crushMix, flMix, dSend, rSend, pumpMix, gateMix, lofiMix;
    OnePoleLP gateSmooth, lofiLp1, lofiLp2;
    int crushCount = 0;
    float crushHeld = 0.f;
    std::vector<float> wowBuf;
    size_t wowW = 0;
    float wowPh = 0.f, crackle = 0.f;
    OnePoleLP crackleLp;

    std::vector<float> flBuf;
    size_t flW = 0;
    float flLfo = 0.f;

    std::vector<float> dlL, dlR;
    size_t dW = 0;
    OnePoleLP dfL, dfR;

    Reverb reverb;
    float level = 0.f;
    float vol = 1.0f;

    Impl(Shared& s, float rate) : sh(s), sr(rate) {
        ring.assign(static_cast<size_t>(sr * 4), 0.f);
        loopBuf.assign(static_cast<size_t>(sr * 2), 0.f);
        flBuf.assign(4096, 0.f);
        wowBuf.assign(2048, 0.f);
        dlL.assign(static_cast<size_t>(sr * 3), 0.f);
        dlR.assign(static_cast<size_t>(sr * 3), 0.f);
        dfL.setHz(3500.f, sr);
        dfR.setHz(3500.f, sr);
        lofiLp1.setHz(3200.f, sr);
        lofiLp2.setHz(4200.f, sr);
        crackleLp.setHz(2500.f, sr);
        gateSmooth.setHz(300.f, sr);
        reverb.init(sr);
        crushMix.setTime(8.f, sr);
        flMix.setTime(30.f, sr);
        dSend.setTime(5.f, sr);
        rSend.setTime(40.f, sr);
        pumpMix.setTime(30.f, sr);
        gateMix.setTime(10.f, sr);
        lofiMix.setTime(60.f, sr);
        xfStep = 1.f / (0.004f * sr);  // 4 ms crossfade for buffer FX
        pattern = sh.pattern.load();
    }

    const Style& style() const { return styleOf(pattern); }
    double samplesPerBeat() const { return sr * 60.0 / std::max(40.f, sh.bpm.load(std::memory_order_relaxed)); }
    double samplesPerStep() const { return samplesPerBeat() / 4.0 * (halftime ? 2.0 : 1.0); }
    double lengthOfStep(int s) const {
        const double swing = style().swing;
        return samplesPerStep() * 2.0 * ((s & 1) ? (1.0 - swing) : swing);
    }

    void hit(int slot) { sh.hits[slot].fetch_add(1, std::memory_order_relaxed); }

    // -------------------------------------------------------- triggers --
    void triggerDrum(int slot, float vel) {
        if (slot < 0 || slot > kExtraSlot) return;
        const Style& st = style();
        const DrumDef& def = st.drums[static_cast<size_t>(slot)];
        drums[slot].trig(def, vel, sr);
        if (def.chokes >= 0 && def.chokes <= kExtraSlot && def.chokes != slot) drums[def.chokes].choke(sr);
        if (def.type == DrumType::Kick) {
            duck = 1.f;
            duckAmt = st.duck;
        }
        hit(slot);
    }

    void chordOn(int degree, float vel) {
        const Style& st = style();
        float f[5];
        const int n = std::clamp(st.chord.notes, 1, 5);
        for (int i = 0; i < n; ++i)
            f[i] = midiHz(static_cast<float>(st.root + st.chord.octave + degreeSemis(st, degree + 2 * i)));
        // Release the previous chord and start this one on the next of three
        // voices, so long pad tails overlap instead of being cut.
        chords[curChord].noteOff();
        curChord = (curChord + 1) % 3;
        chords[curChord].noteOn(st.chord, f, n, vel);
        hit(kChordSlot);
    }
    void chordOffAll() {
        for (auto& c : chords) c.noteOff();
    }

    void bassOn(int semis, bool accent, bool slide) {
        const Style& st = style();
        bass.noteOn(st.bass, st.root, semis, accent, slide);
        hit(kBassSlot);
    }
    void liveBassOn(int semis) {
        if (liveCount < 16) liveBass[liveCount++] = semis;
        seqBassActive = false;
        bassOn(semis, false, liveCount > 1);
    }
    void liveBassOff(int semis) {
        for (int i = 0; i < liveCount; ++i) {
            if (liveBass[i] == semis) {
                for (int j = i; j < liveCount - 1; ++j) liveBass[j] = liveBass[j + 1];
                --liveCount;
                break;
            }
        }
        if (liveCount > 0) bassOn(liveBass[liveCount - 1], false, true);
        else bass.noteOff();
    }

    // ----------------------------------------------------- sequencer --
    // Steps this note lasts: itself plus any ties after it.
    int noteLength(int p, int t, int s) const {
        int len = 1;
        while (len < kSteps && stepKind(sh.on[p][t][(s + len) % kSteps].load(std::memory_order_relaxed)) == STEP_TIE) ++len;
        return len;
    }

    void fireStep(int s) {
        const int p = pattern;
        const Style& st = style();
        const double len = lengthOfStep(s);
        for (int t = 0; t < kDrumTracks; ++t) {
            const uint8_t v = sh.on[p][t][s].load(std::memory_order_relaxed);
            if (!stepSounds(v) || sh.mute[t].load(std::memory_order_relaxed)) continue;
            const int k = stepKind(v);
            const float vel = k == STEP_ACCENT ? 1.3f : (k == STEP_SOFT ? 0.4f : (k >= STEP_ROLL2 && k <= STEP_ROLL3 ? 0.85f : 1.f));
            triggerDrum(t, vel);
            const int extra = k == STEP_ROLL2 ? 1 : (k == STEP_ROLL3 ? 2 : 0);
            for (int r = 1; r <= extra && rollCount < 32; ++r)
                rolls[rollCount++] = {sampleCount + static_cast<uint64_t>(len * r / (extra + 1)), t, vel * (0.75f + 0.1f * r)};
        }

        // Bass
        {
            const uint8_t v = sh.on[p][T_BASS][s].load(std::memory_order_relaxed);
            if (stepSounds(v) && !sh.mute[T_BASS].load(std::memory_order_relaxed) && liveCount == 0) {
                const int n = noteLength(p, T_BASS, s);
                const uint8_t after = sh.on[p][T_BASS][(s + n) % kSteps].load(std::memory_order_relaxed);
                const bool nextSlides = stepSounds(after) && (after & STEP_SLIDE);
                const int deg = sh.note[p][T_BASS][s].load(std::memory_order_relaxed);
                bassOn(degreeSemis(st, deg), stepKind(v) == STEP_ACCENT, (v & STEP_SLIDE) != 0);
                double hold = len * n * (nextSlides ? 1.1 : 0.85);
                bassOffAt = sampleCount + static_cast<uint64_t>(hold);
                seqBassActive = true;
            }
        }
        // Chords
        {
            const uint8_t v = sh.on[p][T_CHORD][s].load(std::memory_order_relaxed);
            if (stepSounds(v) && !sh.mute[T_CHORD].load(std::memory_order_relaxed)) {
                const int n = noteLength(p, T_CHORD, s);
                chordOn(sh.note[p][T_CHORD][s].load(std::memory_order_relaxed), stepKind(v) == STEP_ACCENT ? 1.25f : 1.f);
                chordOffAt = sampleCount + static_cast<uint64_t>(len * n * 0.9);
                seqChordActive = true;
            }
        }
    }

    void switchPattern(int p) {
        const int oldStyle = pattern / kSlots;
        pattern = p;
        queued = -1;
        sh.pattern.store(pattern);
        sh.queued.store(-1);
        if (p / kSlots != oldStyle) sh.bpm.store(style().bpm);
    }

    void sequencerTick() {
        beatPos += 1.0 / samplesPerBeat();
        if (!playing) return;
        phase += 1.0;
        if (phase >= stepLen) {
            phase -= stepLen;
            if (phase >= stepLen) phase = 0.0;
            step = (step + 1) % kSteps;
            if (step == 0 && queued >= 0) switchPattern(queued);
            if (step % 4 == 0) beatPos = std::floor(beatPos + 0.5);
            stepLen = lengthOfStep(step);
            sh.step.store(step, std::memory_order_relaxed);
            fireStep(step);
        }
        for (int i = 0; i < rollCount;) {
            if (sampleCount >= rolls[i].at) {
                if (!sh.mute[rolls[i].slot].load(std::memory_order_relaxed)) triggerDrum(rolls[i].slot, rolls[i].vel);
                rolls[i] = rolls[--rollCount];
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
    }

    // ---------------------------------------------------- buffer FX --
    void engageBufFx(int fx) {
        const double spb = samplesPerBeat();
        const size_t n = ring.size();
        auto grab = [&](int len) {
            len = std::max(64, std::min(len, static_cast<int>(loopBuf.size())));
            for (int k = 0; k < len; ++k) loopBuf[static_cast<size_t>(k)] = ring[(w + n - static_cast<size_t>(len) + static_cast<size_t>(k)) % n];
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
        return f == FX_STUT4 || f == FX_STUT8 || f == FX_STUT16 || f == FX_STUT32 || f == FX_TAPESTOP ||
               f == FX_REVERSE;
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
            for (int i = 0; i < bufCount; ++i) {
                if (bufStack[i] == f) {
                    for (int j = i; j < bufCount - 1; ++j) bufStack[j] = bufStack[j + 1];
                    --bufCount;
                    break;
                }
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

    float bufferFx(float x) {
        if (bufMode == BUF_NONE) return x;
        float bf = x;
        if (bufMode == BUF_LOOP) {
            int idx = loopReverse ? loopLen - 1 - loopPos : loopPos;
            const float edge = 96.f;
            float win = std::min(1.f, std::min(static_cast<float>(loopPos), static_cast<float>(loopLen - 1 - loopPos)) / edge);
            bf = loopBuf[static_cast<size_t>(idx)] * win;
            if (++loopPos >= loopLen) loopPos = 0;
        } else if (bufMode == BUF_TAPE) {
            tapeRate = std::max(0.f, tapeRate - static_cast<float>(1.0 / (samplesPerBeat() * 0.9)));
            bf = readFrac(ring, tapeR) * std::min(1.f, tapeRate * 3.f);
            tapeR += tapeRate;
            if (tapeR >= static_cast<double>(ring.size())) tapeR -= static_cast<double>(ring.size());
        }
        bufXf += bufTarget ? xfStep : -xfStep;
        if (bufXf >= 1.f) bufXf = 1.f;
        if (bufXf <= 0.f) {
            bufXf = 0.f;
            if (!bufTarget) {
                bufMode = BUF_NONE;
                tapeRate = 1.f;
            }
        }
        return x + (bf - x) * bufXf;
    }

    // ------------------------------------------------------ commands --
    void drain() {
        Cmd c;
        while (sh.cmds.pop(c)) {
            switch (c.type) {
                case Cmd::Trigger:
                    if (c.a == kChordSlot) {  // one-shot chord (e.g. from a drum pad): one beat long
                        chordOn(0, 1.f);
                        chordOffAt = sampleCount + static_cast<uint64_t>(samplesPerBeat());
                        seqChordActive = true;
                    } else {
                        triggerDrum(c.a, 1.f);
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
                case Cmd::FxOn: fxOn(c.a); break;
                case Cmd::FxOff: fxOff(c.a); break;
                case Cmd::Play:
                    playing = true;
                    step = kSteps - 1;
                    stepLen = lengthOfStep(step);
                    phase = stepLen;
                    sh.playing.store(true);
                    break;
                case Cmd::Stop:
                    playing = false;
                    if (liveCount == 0) bass.noteOff();
                    if (liveChord == 0) chordOffAll();
                    seqBassActive = seqChordActive = false;
                    rollCount = 0;
                    sh.playing.store(false);
                    break;
                case Cmd::QueuePattern:
                    if (c.a < 0 || c.a >= styleCount() * kSlots) break;
                    if (playing) {
                        queued = c.a;
                        sh.queued.store(c.a);
                    } else {
                        switchPattern(c.a);
                    }
                    break;
            }
        }
    }

    // --------------------------------------------------------- render --
    void process(float* out, int frames) {
        drain();
        const double spb = samplesPerBeat();
        const float beatHz = static_cast<float>(sr / spb);
        const float duckDecay = std::exp(-9.f / sr);
        const float lpUp = static_cast<float>(1.0 / (spb * 2.0));
        const float lpDown = static_cast<float>(1.0 / (spb * 0.25));
        const double delaySamples = spb * 0.75;  // dotted eighth
        const float volTarget = clampf(sh.volume.load(std::memory_order_relaxed), 0.f, 1.f);

        for (int i = 0; i < frames; ++i) {
            sequencerTick();

            // --- voices
            float drumsOut = 0.f, verbSend = 0.f;
            for (auto& d : drums) {
                float y = d.tick(sr, rng);
                drumsOut += y;
                verbSend += y * d.d.verb;
            }
            duck *= duckDecay;
            float ch = chords[0].tick(sr, rng) + chords[1].tick(sr, rng) + chords[2].tick(sr, rng);
            const ChordDef& cd = style().chord;
            float music = (bass.tick(sr, beatHz) * 0.9f + ch) * (1.f - duckAmt * duck);
            float sirenOut = siren.tick(sr);
            float hornOut = horn.tick(sr);
            float laserOut = laser.tick(sr, spb / 2.0, static_cast<double>(sampleCount));
            float fxs = sirenOut + riser.tick(sr, rng) + hornOut + laserOut;

            const float trim = style().trim;
            float x = (drumsOut * 0.85f + music) * trim + fxs * 0.6f;
            float auxRev = (ch * cd.verb + verbSend * 0.6f) * trim;
            float auxDelay = sirenOut * 0.6f + hornOut * 0.25f + laserOut * 0.5f + ch * cd.delay;

            // --- buffer effects (stutter / reverse / tape stop)
            ring[w] = x;
            float y = bufferFx(x);
            if (++w >= ring.size()) w = 0;

            // --- master filters
            lpAmt = fxHeld[FX_LPF] ? std::min(1.f, lpAmt + lpUp) : std::max(0.f, lpAmt - lpDown);
            hpAmt = fxHeld[FX_HPF] ? std::min(1.f, hpAmt + lpUp) : std::max(0.f, hpAmt - lpDown);
            if ((filtCounter++ & 15) == 0) {
                lpf.set(20000.f * std::pow(2.f, -lpAmt * 6.f), 0.8f + lpAmt * 1.8f, sr);
                hpf.set(20.f * std::pow(2.f, hpAmt * 7.f), 0.8f + hpAmt * 1.2f, sr);
            }
            if (lpAmt > 0.f) y = lpf.lp(y); else lpf.reset();
            if (hpAmt > 0.f) y = hpf.hp(y); else hpf.reset();

            // --- bitcrush
            float cm = crushMix.tick(fxHeld[FX_CRUSH] ? 1.f : 0.f);
            if (cm > 0.001f) {
                if (++crushCount >= 6) {
                    crushCount = 0;
                    crushHeld = std::round(y * 10.f) / 10.f;
                }
                y += (crushHeld - y) * cm;
            }

            // --- lo-fi vinyl: darker, warbly, crackly
            float lm = lofiMix.tick(fxHeld[FX_LOFI] ? 1.f : 0.f);
            wowBuf[wowW] = y;
            if (lm > 0.001f) {
                adv(wowPh, 0.55f / sr);
                double wd = sr * (0.004 + 0.0012 * std::sin(kTwoPi * wowPh));
                float wob = readFrac(wowBuf, static_cast<double>(wowW) - wd);
                float dark = lofiLp2.tick(lofiLp1.tick(wob));
                if (rng.next() > 0.9994f) crackle = rng.next();
                crackle *= 0.9f;
                float lo = std::tanh(dark * 1.3f) * 0.85f + crackleLp.tick(crackle) * 0.5f;
                y += (lo - y) * lm;
            }
            if (++wowW >= wowBuf.size()) wowW = 0;

            // --- flanger
            float fm = flMix.tick(fxHeld[FX_FLANGE] ? 1.f : 0.f);
            adv(flLfo, 0.35f / sr);
            double flDelay = sr * (0.0012 + 0.0045 * (0.5 + 0.5 * std::sin(kTwoPi * flLfo)));
            float fl = readFrac(flBuf, static_cast<double>(flW) - flDelay);
            flBuf[flW] = y + fl * 0.65f * fm;
            if (++flW >= flBuf.size()) flW = 0;
            y = (y + fl * fm) / (1.f + 0.6f * fm);

            // --- sidechain pump and trance gate, locked to the beat
            const float bp = static_cast<float>(beatPos - std::floor(beatPos));
            float pm = pumpMix.tick(fxHeld[FX_PUMP] ? 1.f : 0.f);
            if (pm > 0.001f) {
                float k = std::min(1.f, bp / 0.45f);
                float pump = 0.12f + 0.88f * k * k * (3.f - 2.f * k);
                y *= 1.f - pm * (1.f - pump);
            }
            float gm = gateMix.tick(fxHeld[FX_GATE] ? 1.f : 0.f);
            {
                float sub = bp * 4.f;
                sub -= std::floor(sub);
                float g = gateSmooth.tick(sub < 0.5f ? 1.f : 0.f);
                if (gm > 0.001f) y *= 1.f - gm * (1.f - g);
            }

            // --- ping-pong delay (dotted 1/8)
            float ds = dSend.tick(fxHeld[FX_DELAY] ? 1.f : 0.f);
            float din = y * ds + auxDelay;
            double rd = static_cast<double>(dW) - delaySamples;
            float dl = readFrac(dlL, rd);
            float dr = readFrac(dlR, rd);
            dlL[dW] = din + dfR.tick(dr) * 0.55f;
            dlR[dW] = dfL.tick(dl) * 0.55f;
            if (++dW >= dlL.size()) dW = 0;

            // --- reverb (holding the pad also stretches the tail)
            float rs = rSend.tick(fxHeld[FX_REVERB] ? 1.f : 0.f);
            float rl, rr;
            reverb.tick(y * rs + auxRev, 0.84f + 0.12f * rs, 0.3f, rl, rr);

            vol += (volTarget * volTarget - vol) * 0.001f;  // squared = more natural taper
            float L = std::tanh((y + dl * 0.6f + rl * 0.9f) * 0.68f);
            float R = std::tanh((y + dr * 0.6f + rr * 0.9f) * 0.68f);
            out[2 * i] = L * vol;
            out[2 * i + 1] = R * vol;

            float pk = std::max(std::fabs(L), std::fabs(R));
            level = pk > level ? pk : level * 0.9997f;
            ++sampleCount;
        }

        sh.level.store(level, std::memory_order_relaxed);
        sh.lpAmt.store(lpAmt, std::memory_order_relaxed);
        sh.hpAmt.store(hpAmt, std::memory_order_relaxed);
        sh.riserAmt.store(riser.held ? riser.progress() : 0.f, std::memory_order_relaxed);
        sh.tapeRate.store(bufMode == BUF_TAPE ? tapeRate : 1.f, std::memory_order_relaxed);
        sh.bassPlaying.store(bass.gate ? bass.semi : -1000, std::memory_order_relaxed);
        sh.beatPhase.store(static_cast<float>(beatPos - std::floor(beatPos)), std::memory_order_relaxed);
    }
};

Engine::Engine(Shared& shared, float sampleRate) : impl_(std::make_unique<Impl>(shared, sampleRate)) {}
Engine::~Engine() = default;
void Engine::process(float* out, int frames) { impl_->process(out, frames); }

}  // namespace dnb
