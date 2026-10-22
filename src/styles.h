// Musical styles: each has its own drum kit, bass synth, chord sound, key,
// swing, effect layout and four patterns. The Launchpad and GUI remap
// themselves to whichever style is playing.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common.h"

namespace dnb {

// ------------------------------------------------------------------ drums --

enum class DrumType : uint8_t { Kick, Snare, Clap, Metal, Rim, Tom, Shaker, Cowbell };

// Parameter meaning depends on type (see engine.cpp DrumVoice):
//  Kick:    start Hz, end Hz, pitch decay /s, amp decay /s, click 0..1, drive 0..1
//  Snare:   tone Hz, tone decay /s, noise decay /s, noise high-pass Hz, tone mix 0..1, noise low-pass Hz
//  Clap:    band Hz, tail decay /s, burst spacing ms, filter Q
//  Metal:   pitch ratio, band Hz, band Q, high-pass Hz, decay /s, noise mix 0..1   (hats, ride, crash, tamb)
//  Rim:     tone Hz, decay /s
//  Tom:     tone Hz, pitch drop amount, decay /s, noise 0..1                      (toms, congas)
//  Shaker:  high-pass Hz, attack ms, decay /s
//  Cowbell: tone 1 Hz, tone 2 Hz, decay /s
struct DrumDef {
    std::string name;   // full name (GUI)
    std::string label;  // short pad label
    DrumType type = DrumType::Kick;
    float p[6] = {};
    float gain = 1.f;
    float verb = 0.f;   // reverb send
    int chokes = -1;    // slot this sound cuts off (closed hat -> open hat)
};

// ------------------------------------------------------------------- bass --

// Monophonic bass synth. Two (detunable) saws, a square, a sine and a sub,
// through a resonant low-pass with envelope and optional tempo-synced LFO.
struct MonoDef {
    std::string name = "Bass";
    float saw = 1.f, square = 0.f, sine = 0.f, sub = 0.5f;
    float detune = 0.f;        // percent per saw
    int oscOctave = 1;         // oscillators sit this many octaves above the root; sub one below them
    float cutoff = 300.f, envAmt = 2000.f, envDecay = 4.f, res = 1.f;
    float decay = 0.f;         // amp decay /s while held (0 = sustain)
    float release = 8.f;       // amp release /s
    float glide = 0.04f;       // seconds, used on slides / legato
    float drop = 0.f, dropRate = 25.f;  // 808-style pitch drop in semitones at note start
    float lfoDepth = 0.f, lfoRate = 0.f;  // filter LFO in Hz depth, cycles per beat
    float drive = 2.f, gain = 1.f, accent = 1.6f;
};

// ----------------------------------------------------------------- chords --

enum class ChordOsc : uint8_t { Saw, Square, EPiano, Organ, Bell, Marimba };

// Chords are built from the style's scale by stacking thirds on a degree, so
// every chord fits the key (minor 7ths, major 9ths... come out naturally).
struct ChordDef {
    std::string name = "Chords";
    ChordOsc osc = ChordOsc::Saw;
    int notes = 3;             // 1..5 stacked thirds (3 = triad, 4 = 7th, 5 = 9th)
    int octave = 24;           // semitones above the bass root
    float attack = 0.003f, decay = 4.f, sustain = 0.f, release = 6.f;
    float cutoff = 3000.f, envAmt = 0.f, envDecay = 8.f, res = 0.9f;
    float detune = 0.3f;       // percent
    float gain = 0.8f, verb = 0.25f, delay = 0.1f;
};

// --------------------------------------------------------------- patterns --
//
// Drum rows:  . rest   x hit   X accent   o soft (ghost)   r 2-hit roll   t 3-hit roll
// Bass rows:  . rest   - hold previous note   0-9 scale degree
//             A-J = degree 0-9 accented, a-j = degree 0-9 sliding in from the previous note
// Chord rows: . rest   - hold   0-9 chord on that scale degree (A-J accent)
struct PatternDef {
    std::string name;
    std::array<std::string, kTracks> rows;
};

struct Style {
    std::string name, blurb;
    std::string padLabel;      // short name for the Launchpad style picker
    float bpm = 120.f;
    float swing = 0.5f;        // 0.5 = straight, 0.66 = full triplet shuffle
    int root = 29;             // MIDI note of the bass root (F1 = 29)
    std::array<int, 7> scale{0, 2, 3, 5, 7, 8, 10};
    std::string keyName;
    float duck = 0.5f;         // how much the kick ducks bass + chords
    float trim = 1.f;          // overall level, so styles sit at similar loudness
    std::array<DrumDef, 7> drums;  // 0-5 = sequenced drum tracks, 6 = extra pad
    MonoDef bass;
    ChordDef chord;
    std::array<int, 16> fx{};  // effect on each of the 16 effect pads
    uint8_t led = 0x33;        // Launchpad colour of this style in the picker
    std::array<PatternDef, kFactorySlots> patterns;  // built-in patterns
};

const std::vector<Style>& styles();  // built once, never changes
inline int styleCount() { return static_cast<int>(styles().size()); }
inline const Style& styleOf(int pattern) { return styles()[static_cast<size_t>(pattern / kMaxSlots) % styles().size()]; }

// Scale degree (can be > 6 or negative) to semitones above the root.
inline int degreeSemis(const Style& s, int deg) {
    int oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    int d = deg - oct * 7;
    return s.scale[static_cast<size_t>(d)] + 12 * oct;
}

// Note name of a semitone offset from the style root.
const char* noteNameFor(const Style& s, int semis);
// Roman numeral for the chord built on a degree ("i", "IV", "ii°"...).
std::string chordNumeral(const Style& s, int degree, int notes);

void loadPreset(Shared& sh, int pattern);  // built-in slot only (slot < kFactorySlots)
void loadAllPresets(Shared& sh);           // every style's built-in patterns + names

// Pattern rows as text, in the same format as the built-in patterns above.
void parseRow(const std::string& row, bool melodic, uint8_t codes[kSteps], int8_t notes[kSteps]);
std::string encodeRow(const Shared& sh, int pattern, int track);
void setRow(Shared& sh, int pattern, int track, const std::string& row);
bool factoryUnchanged(const Shared& sh, int pattern);  // built-in pattern still as shipped
void copyPattern(Shared& sh, int from, int to);
void clearPattern(Shared& sh, int pattern);

// ------------------------------------------------------------ lead synth --
// Sounds for the second MIDI instrument. Poly patches use the chord synth one
// voice per note; mono patches use the bass synth with legato glide.
struct LeadPatch {
    std::string name, group;
    enum Kind { POLY, MONO, STYLE_BASS, STYLE_CHORDS } kind = POLY;
    ChordDef poly;
    MonoDef mono;
    int transpose = 0;   // semitones added to the played note
    float gain = 1.f, verb = 0.2f, delay = 0.1f;
};
const std::vector<LeadPatch>& leadPatches();

}  // namespace dnb
