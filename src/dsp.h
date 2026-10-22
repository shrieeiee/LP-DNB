// Small DSP building blocks: oscillators, filters, reverb.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dnb {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// xorshift white noise in [-1, 1)
struct Rng {
    uint32_t s = 0x9E3779B9u;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<int32_t>(s) * (1.0f / 2147483648.0f);
    }
};

inline float polyblep(float t, float dt) {
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

// Band-limited sawtooth.
struct Saw {
    float ph = 0.f;
    float tick(float freq, float sr) {
        float dt = freq / sr;
        ph += dt;
        if (ph >= 1.f) ph -= 1.f;
        return 2.f * ph - 1.f - polyblep(ph, dt);
    }
};

// Zero-delay-feedback state variable filter (Cytomic / Simper).
struct SVF {
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set(float freq, float q, float sr) {
        freq = clampf(freq, 10.f, sr * 0.45f);
        float g = std::tan(kPi * freq / sr);
        k = 1.f / q;
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

struct OnePoleLP {
    float z = 0, a = 1;
    void setHz(float f, float sr) { a = 1.f - std::exp(-kTwoPi * f / sr); }
    float tick(float x) { return z += a * (x - z); }
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

// Compact stereo Freeverb-style reverb (4 combs + 2 allpasses per side).
class Reverb {
public:
    void init(float sr) {
        static const int combLen[4] = {1116, 1188, 1277, 1356};
        static const int apLen[2] = {556, 441};
        const float scale = sr / 44100.f;
        const int spread = 23;
        for (int i = 0; i < 4; ++i) {
            l_[i].buf.assign(static_cast<size_t>(combLen[i] * scale), 0.f);
            r_[i].buf.assign(static_cast<size_t>((combLen[i] + spread) * scale), 0.f);
        }
        for (int i = 0; i < 2; ++i) {
            al_[i].buf.assign(static_cast<size_t>(apLen[i] * scale), 0.f);
            ar_[i].buf.assign(static_cast<size_t>((apLen[i] + spread) * scale), 0.f);
        }
    }
    void tick(float in, float feedback, float damp, float& outL, float& outR) {
        float x = in * 0.03f;
        float L = 0, R = 0;
        for (int i = 0; i < 4; ++i) {
            L += l_[i].tick(x, feedback, damp);
            R += r_[i].tick(x, feedback, damp);
        }
        for (int i = 0; i < 2; ++i) {
            L = al_[i].tick(L);
            R = ar_[i].tick(R);
        }
        outL = L;
        outR = R;
    }

private:
    struct Comb {
        std::vector<float> buf;
        size_t i = 0;
        float store = 0;
        float tick(float x, float fb, float damp) {
            float y = buf[i];
            store = y * (1.f - damp) + store * damp;
            buf[i] = x + store * fb;
            if (++i >= buf.size()) i = 0;
            return y;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        size_t i = 0;
        float tick(float x) {
            float b = buf[i];
            float y = -x + b;
            buf[i] = x + b * 0.5f;
            if (++i >= buf.size()) i = 0;
            return y;
        }
    };
    Comb l_[4], r_[4];
    Allpass al_[2], ar_[2];
};

}  // namespace dnb
