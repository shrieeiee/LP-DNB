// Shared definitions between the audio engine (audio thread) and the
// controller/GUI (main thread). Everything crossing threads is either an
// atomic or goes through the lock-free command queue.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dnb {

// Seconds since program start (steady clock). Used for all UI timing.
inline double monotonicSec() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

constexpr int kTracks = 9;          // 6 drum tracks + bass + chords + lead
constexpr int kPadTracks = 8;       // tracks on the Launchpad's right-hand column (not the lead)
constexpr int kSteps = 16;          // steps per bar (one Launchpad page / GUI page)
constexpr int kMaxSteps = 64;       // longest pattern: 4 bars
constexpr int kFactorySlots = 8;    // built-in patterns per style (slots 0-7)
constexpr int kMaxSlots = 24;       // 8 built-in + up to 16 of your own per style
constexpr int kMaxStyles = 16;
constexpr int kMaxPatterns = kMaxStyles * kMaxSlots;  // pattern index = style * kMaxSlots + slot
constexpr int kMaxSong = 64;        // entries in a song chain

// Sequencer tracks (also the order of the right-hand scene buttons).
constexpr int T_BASS = 6;
constexpr int T_CHORD = 7;
constexpr int T_LEAD = 8;           // what you recorded from the lead keyboard
constexpr int kDrumTracks = 6;

// Sound slots / mixer channels: 0-5 = the drum tracks, 6 = extra one-shot
// (crash, cowbell...), 7 = chords, 8 = bass, 9 = lead.
constexpr int kExtraSlot = 6;
constexpr int kChordSlot = 7;
constexpr int kBassSlot = 8;
constexpr int kLeadSlot = 9;
constexpr int kHitSlots = 10;
constexpr int kChannels = 10;
constexpr int kDrumSlots = 7;       // drum sounds that can be synth or sample

inline int trackChannel(int t) {
    if (t == T_BASS) return kBassSlot;
    if (t == T_CHORD) return kChordSlot;
    if (t == T_LEAD) return kLeadSlot;
    return t;
}

// Step codes stored in Shared::on (low nibble) plus flags.
enum StepCode : uint8_t {
    STEP_OFF = 0,
    STEP_ON = 1,
    STEP_ACCENT = 2,
    STEP_ROLL2 = 3,  // two hits in the step (32nds)
    STEP_ROLL3 = 4,  // three hits in the step (triplet roll)
    STEP_SOFT = 5,   // ghost note
    STEP_TIE = 6,    // bass/chord/lead: keep the previous note held
};
constexpr uint8_t STEP_SLIDE = 0x10;  // bass: glide into this note (acid / 808 slide)
inline int stepKind(uint8_t v) { return v & 0x0F; }
inline bool stepSounds(uint8_t v) { int k = stepKind(v); return k != STEP_OFF && k != STEP_TIE; }

// Per-step parameter locks (0 = not locked).
enum Lock : int { LOCK_PITCH = 0, LOCK_DECAY, LOCK_FILTER, kLocks };
// pitch: semitones (-24..24). decay / filter: -100..100 (% shorter/longer, darker/brighter).

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

// Delay time choices for the delay throw / sends.
enum DelayDiv : int { DLY_QUARTER = 0, DLY_DOTTED8, DLY_EIGHTH, DLY_TRIPLET8, DLY_SIXTEENTH, DLY_COUNT };
inline const char* delayDivName(int d) {
    static const char* n[DLY_COUNT] = {"1/4", "Dotted 1/8", "1/8", "1/8 triplet", "1/16"};
    return (d >= 0 && d < DLY_COUNT) ? n[d] : "?";
}
inline double delayDivBeats(int d) {
    static const double b[DLY_COUNT] = {1.0, 0.75, 0.5, 1.0 / 3.0, 0.25};
    return (d >= 0 && d < DLY_COUNT) ? b[d] : 0.75;
}

struct Cmd {
    enum Type : uint8_t {
        Trigger, BassOn, BassOff, ChordOn, ChordOff, FxOn, FxOff, Play, Stop, QueuePattern,
        LeadOn,       // a = MIDI note, b = velocity
        LeadOff,      // a = MIDI note
        LeadPatch,    // a = patch index
        LeadSustain,  // a = pedal down (1) / up (0)
        LeadBend,     // f = semitones
        LeadMod,      // f = 0..1 (vibrato)
        LeadAllOff,
        JumpPattern,  // a = pattern; switch now, keep tempo (after patterns are reordered)
        SetSample,    // a = style, b = drum slot, ptr = const Sample* (or null = synth)
    };
    Type type = Trigger;
    int a = 0;
    int b = 0;
    float f = 0.f;
    double t = 0.0;            // when it was sent (monotonicSec), for sample-accurate timing
    const void* ptr = nullptr;
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
    size_t size() const {
        size_t h = head_.load(std::memory_order_acquire), t = tail_.load(std::memory_order_acquire);
        return (h + N - t) % N;
    }

private:
    std::array<T, N> buf_{};
    std::atomic<size_t> head_{0}, tail_{0};
};

// Lock-free float FIFO used to hand recorded audio to the file writer.
class AudioFifo {
public:
    explicit AudioFifo(size_t n) : buf_(new float[n]), n_(n) {}
    ~AudioFifo() { delete[] buf_; }
    size_t write(const float* d, size_t count) {
        size_t h = head_.load(std::memory_order_relaxed), t = tail_.load(std::memory_order_acquire);
        size_t free = (t + n_ - h - 1) % n_;
        if (count > free) count = free;
        for (size_t i = 0; i < count; ++i) buf_[(h + i) % n_] = d[i];
        head_.store((h + count) % n_, std::memory_order_release);
        return count;
    }
    size_t read(float* d, size_t count) {
        size_t t = tail_.load(std::memory_order_relaxed), h = head_.load(std::memory_order_acquire);
        size_t avail = (h + n_ - t) % n_;
        if (count > avail) count = avail;
        for (size_t i = 0; i < count; ++i) d[i] = buf_[(t + i) % n_];
        tail_.store((t + count) % n_, std::memory_order_release);
        return count;
    }

private:
    float* buf_;
    size_t n_;
    std::atomic<size_t> head_{0}, tail_{0};
};

// Mixer and sound controls for one channel of one style.
struct ChannelMix {
    std::atomic<float> volume{1.f};   // 0..1.5
    std::atomic<float> pan{0.f};      // -1..1 (added to the sound's own position)
    std::atomic<float> reverb{0.f};   // extra send, 0..1
    std::atomic<float> delay{0.f};    // extra send, 0..1
    std::atomic<float> tune{0.f};     // semitones, -12..12
    std::atomic<float> decay{0.f};    // -1..1 (shorter / longer)
    std::atomic<float> tone{0.f};     // -1..1 (darker / brighter)
    std::atomic<bool> solo{false};
};

struct Shared {
    // Pattern data: edited by the controller/GUI, read by the sequencer.
    // on = StepCode (+ flags); note = scale degree (bass, chords) or MIDI note (lead).
    std::atomic<uint8_t> on[kMaxPatterns][kTracks][kMaxSteps];
    std::atomic<int8_t> note[kMaxPatterns][kTracks][kMaxSteps];
    std::atomic<int8_t> lock[kMaxPatterns][kTracks][kMaxSteps][kLocks];
    std::atomic<uint8_t> length[kMaxPatterns];  // 16, 32, 48 or 64 steps
    std::atomic<bool> mute[kTracks];
    std::atomic<float> bpm{174.f};
    std::atomic<float> volume{1.0f};  // master output 0..1

    // Mixer per style per channel.
    ChannelMix mix[kMaxStyles][kChannels];

    // Feel and effects settings.
    std::atomic<float> humanize{0.25f};   // 0..1: tiny timing / loudness variations
    std::atomic<float> hatSwing{0.f};     // 0..1: extra swing on hats and shakers only
    std::atomic<int> delayDiv{DLY_DOTTED8};
    std::atomic<float> delayFeedback{0.55f};
    std::atomic<float> reverbSize{0.6f};
    std::atomic<float> chorus{0.35f};     // how much chords / pads / keys get chorused
    std::atomic<float> width{1.f};        // stereo width 0..1.5
    std::atomic<float> glue{0.5f};        // master bus compression 0..1
    std::atomic<float> sidechain{1.f};    // scales each style's kick ducking 0..1.5
    std::atomic<int> fillEvery{8};        // automatic drum fill every N bars (0 = off)

    // Song mode: a chain of this style's patterns, each played `repeats` times.
    std::atomic<bool> songMode{false};
    std::atomic<int> songLen{0};
    std::atomic<int> songSlot[kMaxSong];
    std::atomic<int> songRepeats[kMaxSong];
    std::atomic<int> songPos{0};          // telemetry: entry playing

    // Recording.
    std::atomic<bool> recordAudio{false}; // engine copies its output into recFifo
    std::atomic<bool> recordLead{false};  // lead keyboard notes are written into the pattern
    AudioFifo recFifo{48000 * 2 * 4};     // 4 s of stereo

    // Telemetry published by the audio thread for the light show / GUI.
    std::atomic<bool> playing{false};
    std::atomic<int> step{0};             // 0 .. length-1
    std::atomic<int> pattern{0};          // style * kMaxSlots + slot
    std::atomic<int> queued{-1};
    std::atomic<uint32_t> hits[kHitSlots];
    std::atomic<int> bassPlaying{-1000};  // semitone currently sounding, -1000 if silent
    std::atomic<float> level{0.f};
    std::atomic<float> levelL{0.f}, levelR{0.f}, gainReduction{0.f};
    std::atomic<float> lpAmt{0.f}, hpAmt{0.f}, riserAmt{0.f}, tapeRate{1.f};
    std::atomic<float> beatPhase{0.f};    // 0..1 within the current beat
    std::atomic<int> leadNote{60};        // last lead note played (for the lights)
    std::atomic<float> leadVolume{0.8f};
    std::atomic<float> cpuLoad{0.f};      // audio callback time / buffer time
    std::atomic<int> bar{0};              // bars since play started
    // For the MIDI clock: beats since Play at the start of the last block, and when that was.
    std::atomic<double> clockBeats{0.0};
    std::atomic<double> clockTime{0.0};

    // Pattern bank. slotCount is read by the audio thread; names are main-thread only.
    std::atomic<int> slotCount[kMaxStyles];
    std::string patternName[kMaxPatterns];

    SpscQueue<Cmd, 1024> cmds;

    Shared() {
        for (int p = 0; p < kMaxPatterns; ++p) {
            length[p].store(kSteps);
            for (int t = 0; t < kTracks; ++t)
                for (int s = 0; s < kMaxSteps; ++s) {
                    on[p][t][s].store(0);
                    note[p][t][s].store(0);
                    for (int l = 0; l < kLocks; ++l) lock[p][t][s][l].store(0);
                }
        }
        for (int t = 0; t < kTracks; ++t) mute[t].store(false);
        for (auto& h : hits) h.store(0);
        for (auto& c : slotCount) c.store(kFactorySlots);
        for (int i = 0; i < kMaxSong; ++i) { songSlot[i].store(0); songRepeats[i].store(1); }
    }

    void send(Cmd::Type type, int a = 0, int b = 0, float f = 0.f, const void* ptr = nullptr) {
        Cmd c;
        c.type = type; c.a = a; c.b = b; c.f = f; c.ptr = ptr;
        c.t = monotonicSec();
        cmds.push(c);
    }
    int patternLength(int p) const {
        int l = length[p].load(std::memory_order_relaxed);
        return l < kSteps ? kSteps : (l > kMaxSteps ? kMaxSteps : l);
    }
};

}  // namespace dnb
