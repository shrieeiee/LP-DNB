// Shared definitions between the audio engine (audio thread) and the
// Launchpad controller (main thread). Everything crossing threads is either
// an atomic or goes through the lock-free command queue.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dnb {

constexpr int kTracks = 8;
constexpr int kSteps = 16;
constexpr int kPatterns = 4;

// Sequencer tracks (also the order of the right-hand scene buttons).
enum Track : int { T_KICK = 0, T_SNARE, T_GHOST, T_HAT, T_OHAT, T_RIDE, T_BASS, T_STAB };

// One-shot sounds on the drum pad row.
enum Sound : int { S_KICK = 0, S_SNARE, S_GHOST, S_HAT, S_OHAT, S_RIDE, S_CRASH, S_STAB, S_COUNT };
constexpr int kBassHitSlot = S_COUNT;  // extra hit counter used for bass notes

// Effect pads, bottom two rows of the grid (row 6 then row 7).
enum Fx : int {
    FX_STUT4 = 0, FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
    FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN,
    FX_COUNT
};

// Bass pads: F natural minor, one octave (semitones above F1).
constexpr int kBassScale[8] = {0, 2, 3, 5, 7, 8, 10, 12};

inline const char* trackName(int t) {
    static const char* n[kTracks] = {"Kick", "Snare", "Ghost snare", "Closed hat",
                                     "Open hat", "Ride", "Reese bass", "Chord stab"};
    return (t >= 0 && t < kTracks) ? n[t] : "?";
}

inline const char* fxName(int f) {
    static const char* n[FX_COUNT] = {"Stutter 1/4", "Stutter 1/8", "Stutter 1/16", "Stutter 1/32",
                                      "Tape stop",   "Half-time",   "Reverse",      "Delay throw",
                                      "Low-pass sweep", "High-pass sweep", "Bitcrush", "Flanger",
                                      "Reverb wash", "Dub siren",   "Noise riser",  "Air horn"};
    return (f >= 0 && f < FX_COUNT) ? n[f] : "?";
}

struct Cmd {
    enum Type : uint8_t { Trigger, BassOn, BassOff, FxOn, FxOff, Play, Stop, QueuePattern };
    Type type = Trigger;
    int a = 0;
};

// Single-producer / single-consumer lock-free ring buffer.
template <typename T, size_t N>
class SpscQueue {
public:
    bool push(const T& v) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t n = (h + 1) % N;
        if (n == tail_.load(std::memory_order_acquire)) return false;
        buf_[h] = v;
        head_.store(n, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        v = buf_[t];
        tail_.store((t + 1) % N, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> buf_{};
    std::atomic<size_t> head_{0}, tail_{0};
};

struct Shared {
    // Pattern data: edited by the controller, read by the sequencer.
    std::atomic<uint8_t> on[kPatterns][kTracks][kSteps];
    std::atomic<int8_t> bassNote[kPatterns][kSteps];
    std::atomic<bool> mute[kTracks];
    std::atomic<float> bpm{174.f};

    // Telemetry published by the audio thread for the light show.
    std::atomic<bool> playing{false};
    std::atomic<int> step{0};
    std::atomic<int> pattern{0};
    std::atomic<int> queued{-1};
    std::atomic<uint32_t> hits[S_COUNT + 1];
    std::atomic<int> bassPlaying{-1};  // semitone currently sounding, -1 if silent
    std::atomic<float> level{0.f};
    std::atomic<float> lpAmt{0.f}, hpAmt{0.f}, riserAmt{0.f}, tapeRate{1.f};

    SpscQueue<Cmd, 1024> cmds;

    Shared() {
        for (int p = 0; p < kPatterns; ++p) {
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) on[p][t][s].store(0);
            for (int s = 0; s < kSteps; ++s) bassNote[p][s].store(0);
        }
        for (int t = 0; t < kTracks; ++t) mute[t].store(false);
        for (auto& h : hits) h.store(0);
    }

    void send(Cmd::Type type, int a = 0) { cmds.push(Cmd{type, a}); }
};

}  // namespace dnb
