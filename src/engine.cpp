#include "engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp.h"

namespace dnb {
namespace {

constexpr float kF1 = 43.6535f;  // F1 in Hz, the root of everything

inline float semiToHz(float semi) { return kF1 * std::pow(2.f, semi / 12.f); }

// ---------------------------------------------------------------- drums --

struct KickVoice {
    float t = 10.f, ph = 0.f, vel = 0.f;
    void trig(float v) { t = 0.f; ph = 0.f; vel = v; }
    float tick(float sr, Rng& rng) {
        if (t > 0.9f) return 0.f;
        float f = 46.f + 190.f * std::exp(-t * 32.f);
        ph += f / sr;
        if (ph >= 1.f) ph -= 1.f;
        float body = std::sin(kTwoPi * ph) * std::exp(-t * 5.5f);
        float click = rng.next() * std::exp(-t * 900.f) * 0.35f;
        t += 1.f / sr;
        return std::tanh(body * 1.7f + click) * vel;
    }
};

struct SnareVoice {
    float t = 10.f, ph1 = 0.f, ph2 = 0.f, vel = 0.f, decay = 1.f;
    SVF hp;
    void init(float sr) { hp.set(1400.f, 0.7f, sr); }
    void trig(float v, float decayMul) { t = 0.f; vel = v; decay = decayMul; }
    float tick(float sr, Rng& rng) {
        if (t > 0.6f) return 0.f;
        float bend = 1.f + 0.5f * std::exp(-t * 60.f);
        ph1 += 190.f * bend / sr;
        ph2 += 330.f * bend / sr;
        if (ph1 >= 1.f) ph1 -= 1.f;
        if (ph2 >= 1.f) ph2 -= 1.f;
        float tone = (std::sin(kTwoPi * ph1) * 0.6f + std::sin(kTwoPi * ph2) * 0.4f) *
                     std::exp(-t * 28.f * decay);
        float n = rng.next();
        float noise = hp.hp(n) * std::exp(-t * 15.f * decay) + n * std::exp(-t * 120.f) * 0.4f;
        t += 1.f / sr;
        return std::tanh((tone * 0.8f + noise) * 1.5f) * vel;
    }
};

// 808-style metallic voice: six detuned squares + noise, band/high-passed.
struct MetalVoice {
    float env = 0.f, coef = 0.f, normalCoef = 0.f, chokeCoef = 0.f, vel = 0.f;
    float ph[6] = {};
    float ratio = 1.f, noiseMix = 0.5f;
    SVF bp, hp;
    void init(float sr, float ratioIn, float bpHz, float bpQ, float hpHz, float decayPerSec,
              float noise) {
        ratio = ratioIn;
        noiseMix = noise;
        bp.set(bpHz, bpQ, sr);
        hp.set(hpHz, 0.7f, sr);
        normalCoef = std::exp(-decayPerSec / sr);
        chokeCoef = std::exp(-150.f / sr);
    }
    void trig(float v) { env = 1.f; coef = normalCoef; vel = v; }
    void choke() { coef = chokeCoef; }
    float tick(float sr, Rng& rng) {
        if (env < 1e-4f) return 0.f;
        static const float freqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
        float sq = 0.f;
        for (int i = 0; i < 6; ++i) {
            ph[i] += freqs[i] * ratio / sr;
            if (ph[i] >= 1.f) ph[i] -= 1.f;
            sq += ph[i] < 0.5f ? 1.f : -1.f;
        }
        float x = sq * (1.f / 6.f) * (1.f - noiseMix) + rng.next() * noiseMix;
        x = hp.hp(bp.bp(x));
        env *= coef;
        return x * env * vel * 1.6f;
    }
};

// ------------------------------------------------------------ musical --

// Mono Reese: two detuned saws, resonant low-pass with envelope, sine sub.
struct BassVoice {
    Saw s1, s2;
    float subPh = 0.f, freq = 55.f, target = 55.f, amp = 0.f, fenv = 0.f;
    bool gate = false;
    int semi = -1;
    SVF filt;
    int coefCounter = 0;
    void noteOn(int semitone, bool legato) {
        semi = semitone;
        target = semiToHz(static_cast<float>(semitone));
        if (!legato || amp < 0.01f) {
            freq = target;
            fenv = 1.f;
        } else {
            fenv = std::max(fenv, 0.55f);
        }
        gate = true;
    }
    void noteOff() { gate = false; }
    float tick(float sr) {
        if (!gate && amp < 1e-4f) {
            semi = -1;
            return 0.f;
        }
        freq += (target - freq) * (1.f - std::exp(-1.f / (0.035f * sr)));
        float aTarget = gate ? 1.f : 0.f;
        amp += (aTarget - amp) * (gate ? 0.01f : 0.0015f);
        fenv *= std::exp(-4.f / sr);
        if ((coefCounter++ & 7) == 0) filt.set(260.f + 2400.f * fenv * fenv, 1.6f, sr);
        float f2 = freq * 2.f;
        float x = s1.tick(f2 * 0.9952f, sr) + s2.tick(f2 * 1.0048f, sr);
        float lp = filt.lp(x * 0.5f);
        subPh += freq / sr;
        if (subPh >= 1.f) subPh -= 1.f;
        float sub = std::sin(kTwoPi * subPh);
        return (std::tanh(lp * 2.2f) * 0.5f + sub * 0.6f) * amp;
    }
};

// Fm9 chord stab: 5 notes x 2 saws through a plucky low-pass.
struct StabVoice {
    Saw saws[10];
    float t = 10.f;
    SVF filt;
    int coefCounter = 0;
    float tick(float sr) {
        if (t > 1.5f) return 0.f;
        static const float semis[5] = {24.f, 27.f, 31.f, 34.f, 38.f};  // F3 Ab3 C4 Eb4 G4
        float x = 0.f;
        for (int i = 0; i < 5; ++i) {
            float f = semiToHz(semis[i]);
            x += saws[i * 2].tick(f * 0.997f, sr) + saws[i * 2 + 1].tick(f * 1.003f, sr);
        }
        if ((coefCounter++ & 7) == 0) filt.set(400.f + 3200.f * std::exp(-t * 9.f), 1.1f, sr);
        float y = filt.lp(x * 0.1f) * std::exp(-t * 3.5f);
        t += 1.f / sr;
        return y;
    }
};

// --------------------------------------------------------- FX sounds --

struct SirenVoice {
    bool held = false;
    float amp = 0.f, lfo = 0.f;
    Saw osc;
    SVF filt;
    float tick(float sr) {
        amp += ((held ? 1.f : 0.f) - amp) * (held ? 0.004f : 0.0004f);
        if (amp < 1e-4f) return 0.f;
        lfo += 3.2f / sr;
        if (lfo >= 1.f) lfo -= 1.f;
        float tri = lfo < 0.5f ? lfo * 2.f : 2.f - lfo * 2.f;
        float f = 470.f * std::pow(2.f, tri);
        filt.set(3000.f, 0.8f, sr);
        return filt.lp(osc.tick(f, sr)) * amp * 0.5f;
    }
};

struct RiserVoice {
    bool held = false;
    float t = 0.f, amp = 0.f;
    Saw saw;
    SVF bp;
    int coefCounter = 0;
    float progress() const { return std::min(1.f, t / 6.f); }
    float tick(float sr, Rng& rng) {
        if (held) t += 1.f / sr;
        float target = held ? 0.2f + 0.8f * progress() : 0.f;
        amp += (target - amp) * (held ? 0.002f : 0.0008f);
        if (amp < 1e-4f) return 0.f;
        float tt = std::min(t, 8.f);
        if ((coefCounter++ & 15) == 0) bp.set(std::min(250.f * std::pow(2.f, tt * 0.85f), 14000.f), 3.f, sr);
        float x = bp.bp(rng.next()) * 1.6f + saw.tick(110.f * std::pow(2.f, tt * 0.5f), sr) * 0.12f;
        return x * amp;
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
        vib += 6.f / sr;
        if (vib >= 1.f) vib -= 1.f;
        float f = 466.16f * (1.f - 0.08f * std::exp(-t * 18.f)) *
                  (1.f + 0.004f * std::sin(kTwoPi * vib));
        float x = saws[0].tick(f, sr) + saws[1].tick(f * 1.004f, sr) + saws[2].tick(f * 0.996f, sr) +
                  saws[3].tick(f * 0.501f, sr) * 0.8f;
        filt.set(2600.f, 1.2f, sr);
        return std::tanh(filt.lp(x * 0.3f) * 2.f) * amp * 0.45f;
    }
};

}  // namespace

// ================================================================ Impl ==

struct Engine::Impl {
    Shared& sh;
    float sr;
    Rng rng;
    uint64_t sampleCount = 0;

    // voices
    KickVoice kick;
    SnareVoice snare, ghost;
    MetalVoice hat, ohat, ride, crash;
    BassVoice bass;
    StabVoice stab;
    SirenVoice siren;
    RiserVoice riser;
    HornVoice horn;
    float duck = 0.f;

    // sequencer
    bool playing = false;
    int step = 15;
    double phase = 0.0;
    int pattern = 0, queued = -1;
    bool halftime = false;
    uint64_t bassOffAt = 0;
    bool seqBassActive = false;
    int liveBass[16];
    int liveCount = 0;

    // FX state
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

    Smooth crushMix, flMix, dSend, rSend;
    int crushCount = 0;
    float crushHeld = 0.f;

    std::vector<float> flBuf;
    size_t flW = 0;
    float flLfo = 0.f;

    std::vector<float> dlL, dlR;
    size_t dW = 0;
    OnePoleLP dfL, dfR;

    Reverb reverb;
    float level = 0.f;

    Impl(Shared& s, float rate) : sh(s), sr(rate) {
        snare.init(sr);
        ghost.init(sr);
        hat.init(sr, 1.0f, 10000.f, 1.0f, 7000.f, 60.f, 0.5f);
        ohat.init(sr, 1.0f, 10000.f, 1.0f, 6500.f, 9.f, 0.5f);
        ride.init(sr, 1.35f, 5200.f, 1.6f, 3000.f, 3.2f, 0.25f);
        crash.init(sr, 1.1f, 7000.f, 0.5f, 4000.f, 1.6f, 0.7f);
        ring.assign(static_cast<size_t>(sr * 4), 0.f);
        loopBuf.assign(static_cast<size_t>(sr * 2), 0.f);
        flBuf.assign(4096, 0.f);
        dlL.assign(static_cast<size_t>(sr * 3), 0.f);
        dlR.assign(static_cast<size_t>(sr * 3), 0.f);
        dfL.setHz(3500.f, sr);
        dfR.setHz(3500.f, sr);
        reverb.init(sr);
        crushMix.setTime(8.f, sr);
        flMix.setTime(30.f, sr);
        dSend.setTime(5.f, sr);
        rSend.setTime(40.f, sr);
        xfStep = 1.f / (0.004f * sr);  // 4 ms crossfade for buffer FX
    }

    double samplesPerBeat() const { return sr * 60.0 / std::max(60.f, sh.bpm.load(std::memory_order_relaxed)); }
    double samplesPerStep() const { return samplesPerBeat() / 4.0 * (halftime ? 2.0 : 1.0); }

    void hit(int slot) { sh.hits[slot].fetch_add(1, std::memory_order_relaxed); }

    void triggerSound(int s, float vel) {
        switch (s) {
            case S_KICK: kick.trig(vel); duck = 1.f; break;
            case S_SNARE: snare.trig(vel, 1.f); break;
            case S_GHOST: ghost.trig(vel * 0.32f, 1.9f); break;
            case S_HAT: hat.trig(vel * 0.55f); ohat.choke(); break;
            case S_OHAT: ohat.trig(vel * 0.5f); break;
            case S_RIDE: ride.trig(vel * 0.45f); break;
            case S_CRASH: crash.trig(vel * 0.55f); break;
            case S_STAB: stab.t = 0.f; break;
            default: return;
        }
        hit(s);
    }

    // ---------------------------------------------------------- bass --
    void bassOn(int semi) {
        bass.noteOn(semi, bass.gate);
        hit(kBassHitSlot);
    }
    void liveBassOn(int semi) {
        if (liveCount < 16) liveBass[liveCount++] = semi;
        seqBassActive = false;
        bassOn(semi);
    }
    void liveBassOff(int semi) {
        for (int i = 0; i < liveCount; ++i) {
            if (liveBass[i] == semi) {
                for (int j = i; j < liveCount - 1; ++j) liveBass[j] = liveBass[j + 1];
                --liveCount;
                break;
            }
        }
        if (liveCount > 0) bass.noteOn(liveBass[liveCount - 1], true);
        else bass.noteOff();
    }

    // ----------------------------------------------------- sequencer --
    void fireStep(int s) {
        const int p = pattern;
        for (int t = T_KICK; t <= T_RIDE; ++t) {
            if (sh.on[p][t][s].load(std::memory_order_relaxed) && !sh.mute[t].load(std::memory_order_relaxed))
                triggerSound(t, 1.f);
        }
        if (sh.on[p][T_STAB][s].load(std::memory_order_relaxed) && !sh.mute[T_STAB].load(std::memory_order_relaxed))
            triggerSound(S_STAB, 1.f);

        if (sh.on[p][T_BASS][s].load(std::memory_order_relaxed) && !sh.mute[T_BASS].load(std::memory_order_relaxed) &&
            liveCount == 0) {
            // Note lasts until the next bass step (max 8 steps).
            int len = 1;
            while (len < 8 && !sh.on[p][T_BASS][(s + len) % kSteps].load(std::memory_order_relaxed)) ++len;
            bassOn(sh.bassNote[p][s].load(std::memory_order_relaxed));
            bassOffAt = sampleCount + static_cast<uint64_t>(len * samplesPerStep() * 0.92);
            seqBassActive = true;
        }
    }

    void sequencerTick() {
        if (!playing) return;
        const double sps = samplesPerStep();
        phase += 1.0;
        if (phase >= sps) {
            phase -= sps;
            if (phase >= sps) phase = 0.0;
            step = (step + 1) % kSteps;
            if (step == 0 && queued >= 0) {
                pattern = queued;
                queued = -1;
                sh.pattern.store(pattern);
                sh.queued.store(-1);
            }
            sh.step.store(step, std::memory_order_relaxed);
            fireStep(step);
        }
        if (seqBassActive && sampleCount >= bassOffAt) {
            seqBassActive = false;
            if (liveCount == 0) bass.noteOff();
        }
    }

    // ---------------------------------------------------- buffer FX --
    void engageBufFx(int fx) {
        const double spb = samplesPerBeat();
        const size_t n = ring.size();
        auto grab = [&](int len) {
            len = std::max(64, std::min(len, static_cast<int>(loopBuf.size())));
            for (int k = 0; k < len; ++k) loopBuf[k] = ring[(w + n - len + k) % n];
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
            bf = loopBuf[idx] * win;
            if (++loopPos >= loopLen) loopPos = 0;
        } else if (bufMode == BUF_TAPE) {
            tapeRate = std::max(0.f, tapeRate - static_cast<float>(1.0 / (samplesPerBeat() * 0.9)));
            bf = readFrac(ring, tapeR) * std::min(1.f, tapeRate * 3.f);
            tapeR += tapeRate;
            if (tapeR >= ring.size()) tapeR -= ring.size();
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
                case Cmd::Trigger: triggerSound(c.a, 1.f); break;
                case Cmd::BassOn: liveBassOn(c.a); break;
                case Cmd::BassOff: liveBassOff(c.a); break;
                case Cmd::FxOn: fxOn(c.a); break;
                case Cmd::FxOff: fxOff(c.a); break;
                case Cmd::Play:
                    playing = true;
                    step = kSteps - 1;
                    phase = samplesPerStep();
                    sh.playing.store(true);
                    break;
                case Cmd::Stop:
                    playing = false;
                    if (liveCount == 0) bass.noteOff();
                    seqBassActive = false;
                    sh.playing.store(false);
                    break;
                case Cmd::QueuePattern:
                    if (c.a < 0 || c.a >= kPatterns) break;
                    if (playing) {
                        queued = c.a;
                        sh.queued.store(c.a);
                    } else {
                        pattern = c.a;
                        queued = -1;
                        sh.pattern.store(c.a);
                        sh.queued.store(-1);
                    }
                    break;
            }
        }
    }

    // --------------------------------------------------------- render --
    void process(float* out, int frames) {
        drain();
        const double spb = samplesPerBeat();
        const float duckDecay = std::exp(-9.f / sr);
        const float lpUp = static_cast<float>(1.0 / (spb * 2.0));
        const float lpDown = static_cast<float>(1.0 / (spb * 0.25));
        const double delaySamples = spb * 0.75;  // dotted eighth

        for (int i = 0; i < frames; ++i) {
            sequencerTick();

            // --- voices
            float sn = snare.tick(sr, rng);
            float drums = kick.tick(sr, rng) + sn + ghost.tick(sr, rng) + hat.tick(sr, rng) +
                          ohat.tick(sr, rng) + ride.tick(sr, rng) + crash.tick(sr, rng);
            duck *= duckDecay;
            float st = stab.tick(sr);
            float music = (bass.tick(sr) * 0.9f + st * 0.8f) * (1.f - 0.55f * duck);
            float sirenOut = siren.tick(sr);
            float hornOut = horn.tick(sr);
            float fxs = sirenOut + riser.tick(sr, rng) + hornOut;

            float x = drums * 0.85f + music + fxs * 0.6f;
            float auxRev = st * 0.35f + sn * 0.08f;
            float auxDelay = sirenOut * 0.6f + hornOut * 0.25f + st * 0.12f;

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

            // --- flanger
            float fm = flMix.tick(fxHeld[FX_FLANGE] ? 1.f : 0.f);
            flLfo += 0.35f / sr;
            if (flLfo >= 1.f) flLfo -= 1.f;
            double flDelay = sr * (0.0012 + 0.0045 * (0.5 + 0.5 * std::sin(kTwoPi * flLfo)));
            float fl = readFrac(flBuf, static_cast<double>(flW) - flDelay);
            flBuf[flW] = y + fl * 0.65f * fm;
            if (++flW >= flBuf.size()) flW = 0;
            y = (y + fl * fm) / (1.f + 0.6f * fm);

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

            float L = std::tanh((y + dl * 0.6f + rl * 0.9f) * 0.68f);
            float R = std::tanh((y + dr * 0.6f + rr * 0.9f) * 0.68f);
            out[2 * i] = L;
            out[2 * i + 1] = R;

            float pk = std::max(std::fabs(L), std::fabs(R));
            level = pk > level ? pk : level * 0.9997f;
            ++sampleCount;
        }

        sh.level.store(level, std::memory_order_relaxed);
        sh.lpAmt.store(lpAmt, std::memory_order_relaxed);
        sh.hpAmt.store(hpAmt, std::memory_order_relaxed);
        sh.riserAmt.store(riser.held ? riser.progress() : 0.f, std::memory_order_relaxed);
        sh.tapeRate.store(bufMode == BUF_TAPE ? tapeRate : 1.f, std::memory_order_relaxed);
        sh.bassPlaying.store(bass.gate ? bass.semi : -1, std::memory_order_relaxed);
    }
};

Engine::Engine(Shared& shared, float sampleRate) : impl_(std::make_unique<Impl>(shared, sampleRate)) {}
Engine::~Engine() = default;
void Engine::process(float* out, int frames) { impl_->process(out, frames); }

}  // namespace dnb
