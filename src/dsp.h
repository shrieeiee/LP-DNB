// DSP building blocks: oscillators, filters, saturation, reverb, chorus,
// dynamics. Everything here is allocation-free once constructed.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define DNB_HAVE_SSE 1
#endif

namespace dnb {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Flush denormals to zero on this thread. Decaying reverb/delay tails reach
// tiny "denormal" numbers that are very slow on x86 CPUs and cause crackles.
inline void disableDenormals() {
#if defined(DNB_HAVE_SSE)
    _mm_setcsr(_mm_getcsr() | 0x8040);  // FTZ | DAZ
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));  // FZ
#endif
}

// xorshift white noise in [-1, 1)
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<int32_t>(s) * (1.0f / 2147483648.0f);
    }
    float uni() { return 0.5f + 0.5f * next(); }  // [0, 1)
};

inline float polyblep(float t, float dt) {
    if (dt <= 0.f) return 0.f;
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

inline void advPhase(float& ph, float inc) {
    ph += inc;
    if (ph >= 1.f) ph -= std::floor(ph);
}

// Band-limited square (two polyBLEP edges).
inline float blepSquare(float ph, float dt) {
    float p2 = ph + 0.5f;
    if (p2 >= 1.f) p2 -= 1.f;
    return (ph < 0.5f ? 1.f : -1.f) + polyblep(ph, dt) - polyblep(p2, dt);
}

// Band-limited sawtooth.
struct Saw {
    float ph = 0.f;
    float tick(float freq, float sr) {
        float dt = freq / sr;
        ph += dt;
        if (ph >= 1.f) ph -= std::floor(ph);
        return 2.f * ph - 1.f - polyblep(ph, dt);
    }
};

// tanh with first-order anti-derivative anti-aliasing: much less harsh
// aliasing when driving hard, for the cost of a log/cosh.
struct AdaaTanh {
    float x1 = 0.f, F1 = 0.f;
    static float F(float x) {  // log(cosh(x)), safe for large |x|
        const float a = std::fabs(x);
        return a > 12.f ? a - 0.693147f : std::log(std::cosh(x));
    }
    float tick(float x) {
        const float Fx = F(x);
        const float d = x - x1;
        const float y = std::fabs(d) < 1e-4f ? std::tanh(0.5f * (x + x1)) : (Fx - F1) / d;
        x1 = x;
        F1 = Fx;
        return y;
    }
};

// Zero-delay-feedback state variable filter (Cytomic / Simper). 12 dB/oct.
struct SVF {
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set(float freq, float q, float sr) {
        freq = clampf(freq, 10.f, sr * 0.45f);
        float g = std::tan(kPi * freq / sr);
        k = 1.f / std::max(0.05f, q);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void tick(float v0, float& lp, float& bp, float& hp) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }
    float lp(float x) { float l, b, h; tick(x, l, b, h); return l; }
    float bp(float x) { float l, b, h; tick(x, l, b, h); return b; }
    float hp(float x) { float l, b, h; tick(x, l, b, h); return h; }
    void reset() { ic1 = ic2 = 0; }
};

// 24 dB/oct ladder (Zavalishin's zero-delay-feedback form) with a saturating
// input stage: the squelchy resonance of a 303 / Moog.
struct Ladder {
    float s[4] = {}, G = 0.f, g = 0.f, k = 0.f;
    void set(float freq, float res, float sr) {
        freq = clampf(freq, 10.f, sr * 0.42f);
        g = std::tan(kPi * freq / sr);
        G = g / (1.f + g);
        k = clampf(res, 0.f, 3.95f);
    }
    float tick(float x) {
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float inv = 1.f / (1.f + g);
        const float S = G3 * s[0] * inv + G2 * s[1] * inv + G * s[2] * inv + s[3] * inv;
        const float y4 = (G4 * x + S) / (1.f + k * G4);
        float u = std::tanh(x - k * y4);
        for (int i = 0; i < 4; ++i) {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = y + v;
            u = y;
        }
        return u * (1.f + 0.45f * k);  // keep level up as resonance eats the bass
    }
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.f; }
};

struct OnePoleLP {
    float z = 0, a = 1;
    void setHz(float f, float sr) { a = 1.f - std::exp(-kTwoPi * f / sr); }
    float tick(float x) { return z += a * (x - z); }
};

// RBJ biquad, used for the master shelving EQ.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void shelf(bool high, float f, float gainDb, float sr) {
        const float A = std::pow(10.f, gainDb / 40.f), w = kTwoPi * f / sr;
        const float cs = std::cos(w), sn = std::sin(w), alpha = sn / 2.f * std::sqrt(2.f);
        const float sq = 2.f * std::sqrt(A) * alpha;
        float B0, B1, B2, A0, A1, A2;
        if (high) {
            B0 = A * ((A + 1) + (A - 1) * cs + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sq);
            A0 = (A + 1) - (A - 1) * cs + sq; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - sq;
        } else {
            B0 = A * ((A + 1) - (A - 1) * cs + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sq);
            A0 = (A + 1) + (A - 1) * cs + sq; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - sq;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    float tick(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Exponential parameter smoother.
struct Smooth {
    float v = 0, coef = 0.01f;
    void setTime(float ms, float sr) { coef = 1.f - std::exp(-1.f / (ms * 0.001f * sr)); }
    float tick(float target) { return v += coef * (target - v); }
};

// Linear-interpolated read from a circular buffer.
inline float readFrac(const std::vector<float>& buf, double pos) {
    const size_t n = buf.size();
    while (pos < 0) pos += static_cast<double>(n);
    size_t i0 = static_cast<size_t>(pos) % n;
    size_t i1 = (i0 + 1) % n;
    float f = static_cast<float>(pos - std::floor(pos));
    return buf[i0] + (buf[i1] - buf[i0]) * f;
}

// Constant-power pan: -1 = left, 0 = centre, 1 = right.
inline void panGains(float pan, float& l, float& r) {
    const float a = (clampf(pan, -1.f, 1.f) + 1.f) * 0.25f * kPi;
    l = std::cos(a) * 1.41421356f;
    r = std::sin(a) * 1.41421356f;
}

// ------------------------------------------------------------ reverb ----
// 8-line feedback delay network with Hadamard mixing, damping, gentle
// modulation and pre-delay: a smooth, lush stereo hall/plate.
class FdnReverb {
public:
    void init(float sr) {
        sr_ = sr;
        static const float ms[8] = {29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.1f, 67.3f};
        for (int i = 0; i < 8; ++i) {
            base_[i] = ms[i] * 0.001f * sr;
            line_[i].assign(static_cast<size_t>(base_[i] * 2.2f) + 64, 0.f);
            w_[i] = 0;
        }
        pre_.assign(static_cast<size_t>(0.05f * sr) + 4, 0.f);
        setSize(0.6f);
    }
    // size 0..1: room to long hall. Recomputes delay lengths and decay.
    void setSize(float size) {
        size = clampf(size, 0.f, 1.f);
        if (std::fabs(size - size_) < 1e-3f) return;
        size_ = size;
        const float scale = 0.55f + 1.4f * size;
        const float rt60 = 0.6f + 5.5f * size * size;  // seconds
        for (int i = 0; i < 8; ++i) {
            len_[i] = base_[i] * scale;
            gain_[i] = std::pow(10.f, -3.f * len_[i] / (rt60 * sr_));
        }
        damp_.setHz(5200.f + 2000.f * (1.f - size), sr_);
        for (auto& d : dampers_) d = damp_;
    }
    void tick(float inL, float inR, float& outL, float& outR) {
        // pre-delay ~20 ms
        pre_[pw_] = 0.5f * (inL + inR);
        const size_t pd = static_cast<size_t>(0.02f * sr_);
        const float x = pre_[(pw_ + pre_.size() - pd) % pre_.size()];
        if (++pw_ >= pre_.size()) pw_ = 0;
        const float side = 0.5f * (inL - inR);

        advPhase(mod_, 0.37f / sr_);
        const float m = std::sin(kTwoPi * mod_) * 9.f;
        float y[8];
        for (int i = 0; i < 8; ++i) {
            const float len = len_[i] + (i == 1 ? m : (i == 6 ? -m : 0.f));
            y[i] = readFrac(line_[i], static_cast<double>(w_[i]) - len);
        }
        // Hadamard 8 (fast Walsh-Hadamard transform), normalised.
        float h[8];
        for (int i = 0; i < 8; ++i) h[i] = y[i];
        for (int span = 1; span < 8; span <<= 1)
            for (int i = 0; i < 8; i += span * 2)
                for (int j = i; j < i + span; ++j) {
                    const float a = h[j], b = h[j + span];
                    h[j] = a + b;
                    h[j + span] = a - b;
                }
        const float norm = 0.35355339f;  // 1/sqrt(8)
        for (int i = 0; i < 8; ++i) {
            const float in = (i < 4 ? x + side : x - side) * 0.35f;
            float v = h[i] * norm * gain_[i] + in;
            v = dampers_[i].tick(v);
            line_[i][static_cast<size_t>(w_[i])] = v;
            if (++w_[i] >= static_cast<int>(line_[i].size())) w_[i] = 0;
        }
        outL = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f) * 0.5f;
        outR = (y[1] - y[3] + y[5] - y[7] + y[0] * 0.5f) * 0.5f;
    }

private:
    float sr_ = 48000.f, size_ = -1.f, mod_ = 0.f;
    float base_[8] = {}, len_[8] = {}, gain_[8] = {};
    std::vector<float> line_[8];
    int w_[8] = {};
    std::vector<float> pre_;
    size_t pw_ = 0;
    OnePoleLP damp_, dampers_[8];
};

// ------------------------------------------------------------ chorus ----
// Stereo chorus: two modulated delay taps per side, wet only.
class Chorus {
public:
    void init(float sr) {
        sr_ = sr;
        bufL_.assign(static_cast<size_t>(0.05f * sr), 0.f);
        bufR_.assign(bufL_.size(), 0.f);
    }
    void tick(float inL, float inR, float& outL, float& outR) {
        bufL_[w_] = inL;
        bufR_[w_] = inR;
        advPhase(p1_, 0.31f / sr_);
        advPhase(p2_, 0.43f / sr_);
        const double base = 0.012 * sr_, depth = 0.0035 * sr_;
        auto tap = [&](const std::vector<float>& b, float ph) {
            return readFrac(b, static_cast<double>(w_) - (base + depth * std::sin(kTwoPi * ph)));
        };
        outL = 0.5f * (tap(bufL_, p1_) + tap(bufR_, p2_ + 0.5f));
        outR = 0.5f * (tap(bufR_, p1_ + 0.25f) + tap(bufL_, p2_ + 0.75f));
        if (++w_ >= bufL_.size()) w_ = 0;
    }

private:
    float sr_ = 48000.f, p1_ = 0.f, p2_ = 0.3f;
    std::vector<float> bufL_, bufR_;
    size_t w_ = 0;
};

// ---------------------------------------------------------- dynamics ----
// Envelope follower with separate attack / release times.
struct Follower {
    float env = 0.f, att = 0.f, rel = 0.f;
    void set(float attackMs, float releaseMs, float sr) {
        att = 1.f - std::exp(-1.f / (std::max(0.05f, attackMs) * 0.001f * sr));
        rel = 1.f - std::exp(-1.f / (std::max(1.f, releaseMs) * 0.001f * sr));
    }
    float tick(float x) {
        const float a = std::fabs(x);
        env += (a > env ? att : rel) * (a - env);
        return env;
    }
};

// Stereo-linked feed-forward compressor ("glue" on the master bus).
struct BusCompressor {
    Follower f;
    float threshDb = -14.f, ratio = 2.f, makeupDb = 0.f, grDb = 0.f;
    void init(float sr) { f.set(10.f, 140.f, sr); }
    void process(float& l, float& r) {
        const float lvl = f.tick(std::max(std::fabs(l), std::fabs(r)));
        const float db = 20.f * std::log10(lvl + 1e-9f);
        const float over = db - threshDb;
        // soft knee (6 dB)
        float gr = 0.f;
        if (over > 3.f) gr = over * (1.f - 1.f / ratio);
        else if (over > -3.f) gr = (over + 3.f) * (over + 3.f) / 12.f * (1.f - 1.f / ratio);
        grDb = gr;
        const float g = std::pow(10.f, (makeupDb - gr) / 20.f);
        l *= g;
        r *= g;
    }
};

// Look-ahead brick-wall limiter: catches peaks before they arrive, so the
// output never goes over the ceiling and nothing audibly clips.
class Limiter {
public:
    void init(float sr, float lookaheadMs = 1.5f, float ceiling = 0.966f) {
        n_ = std::max(8, static_cast<int>(lookaheadMs * 0.001f * sr));
        dl_.assign(static_cast<size_t>(n_), 0.f);
        dr_.assign(static_cast<size_t>(n_), 0.f);
        need_.assign(static_cast<size_t>(n_), 1.f);
        ceiling_ = ceiling;
        att_ = 1.f - std::exp(-1.f / (n_ * 0.4f));
        rel_ = 1.f - std::exp(-1.f / (0.08f * sr));
    }
    void process(float& l, float& r) {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float need = peak > ceiling_ ? ceiling_ / peak : 1.f;
        need_[static_cast<size_t>(i_)] = need;
        dl_[static_cast<size_t>(i_)] = l;
        dr_[static_cast<size_t>(i_)] = r;
        if (++i_ >= n_) i_ = 0;
        float target = 1.f;
        for (float v : need_) target = std::min(target, v);  // small window: a plain scan is fine
        g_ += (target < g_ ? att_ : rel_) * (target - g_);
        const float gg = g_;
        l = dl_[static_cast<size_t>(i_)] * gg;
        r = dr_[static_cast<size_t>(i_)] * gg;
        // Final safety: anything left over the ceiling is gently clipped.
        if (std::fabs(l) > ceiling_) l = std::copysign(ceiling_, l);
        if (std::fabs(r) > ceiling_) r = std::copysign(ceiling_, r);
        reduction_ = 1.f - gg;
    }
    float reduction() const { return reduction_; }

private:
    int n_ = 72, i_ = 0;
    std::vector<float> dl_, dr_, need_;
    float ceiling_ = 0.966f, g_ = 1.f, att_ = 0.1f, rel_ = 0.001f, reduction_ = 0.f;
};

}  // namespace dnb
