// Shared definitions between the audio engine (audio thread) and the
// controller/GUI (main thread). Everything crossing threads is either an
// atomic or goes through the lock-free command queue.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dnb {

// Seconds since program start (steady clock). Used for all UI timing.
inline double monotonicSec() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

constexpr int kTracks = 8;          // 6 drum tracks + bass + chords
constexpr int kSteps = 16;
constexpr int kSlots = 4;           // patterns per style
constexpr int kMaxStyles = 16;
constexpr int kMaxPatterns = kMaxStyles * kSlots;  // pattern index = style * kSlots + slot

// Sequencer tracks (also the order of the right-hand scene buttons).
constexpr int T_BASS = 6;
constexpr int T_CHORD = 7;
constexpr int kDrumTracks = 6;

// Sound slots on the drum pad row: 0-5 = the drum tracks, 6 = extra one-shot
// (crash, cowbell...), 7 = chord. Slot 8 is the bass (used for hit counters).
constexpr int kExtraSlot = 6;
constexpr int kChordSlot = 7;
constexpr int kBassSlot = 8;
constexpr int kHitSlots = 9;

// Step codes stored in Shared::on (low nibble) plus flags.
enum StepCode : uint8_t {
    STEP_OFF = 0,
    STEP_ON = 1,
    STEP_ACCENT = 2,
    STEP_ROLL2 = 3,  // two hits in the step (32nds)
    STEP_ROLL3 = 4,  // three hits in the step (triplet roll)
    STEP_SOFT = 5,   // ghost note
    STEP_TIE = 6,    // bass/chord: keep the previous note held
};
constexpr uint8_t STEP_SLIDE = 0x10;  // bass: glide into this note (acid / 808 slide)
inline int stepKind(uint8_t v) { return v & 0x0F; }
inline bool stepSounds(uint8_t v) { int k = stepKind(v); return k != STEP_OFF && k != STEP_TIE; }

// All effects. Each style puts 16 of them on its two effect rows.
enum Fx : int {
    FX_STUT4 = 0, FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
    FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN,
    FX_PUMP, FX_GATE, FX_LOFI, FX_LASER,
    FX_COUNT
};

inline const char* fxName(int f) {
    static const char* n[FX_COUNT] = {"Stutter 1/4", "Stutter 1/8", "Stutter 1/16", "Stutter 1/32",
                                      "Tape stop",   "Half-time",   "Reverse",      "Delay throw",
                                      "Low-pass sweep", "High-pass sweep", "Bitcrush", "Flanger",
                                      "Reverb wash", "Dub siren",   "Noise riser",  "Air horn",
                                      "Sidechain pump", "Trance gate", "Lo-fi vinyl", "Laser zap"};
    return (f >= 0 && f < FX_COUNT) ? n[f] : "?";
}

struct Cmd {
    enum Type : uint8_t { Trigger, BassOn, BassOff, ChordOn, ChordOff, FxOn, FxOff, Play, Stop, QueuePattern };
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
    // Pattern data: edited by the controller/GUI, read by the sequencer.
    // on = StepCode (+ flags); note = scale degree for the bass and chord tracks.
    std::atomic<uint8_t> on[kMaxPatterns][kTracks][kSteps];
    std::atomic<int8_t> note[kMaxPatterns][kTracks][kSteps];
    std::atomic<bool> mute[kTracks];
    std::atomic<float> bpm{174.f};
    std::atomic<float> volume{1.0f};  // master output 0..1

    // Telemetry published by the audio thread for the light show / GUI.
    std::atomic<bool> playing{false};
    std::atomic<int> step{0};
    std::atomic<int> pattern{0};   // style * kSlots + slot
    std::atomic<int> queued{-1};
    std::atomic<uint32_t> hits[kHitSlots];
    std::atomic<int> bassPlaying{-1000};  // semitone currently sounding, -1000 if silent
    std::atomic<float> level{0.f};
    std::atomic<float> lpAmt{0.f}, hpAmt{0.f}, riserAmt{0.f}, tapeRate{1.f};
    std::atomic<float> beatPhase{0.f};  // 0..1 within the current beat

    SpscQueue<Cmd, 1024> cmds;

    Shared() {
        for (int p = 0; p < kMaxPatterns; ++p)
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kSteps; ++s) {
                    on[p][t][s].store(0);
                    note[p][t][s].store(0);
                }
        for (int t = 0; t < kTracks; ++t) mute[t].store(false);
        for (auto& h : hits) h.store(0);
    }

    void send(Cmd::Type type, int a = 0) { cmds.push(Cmd{type, a}); }
};

}  // namespace dnb
