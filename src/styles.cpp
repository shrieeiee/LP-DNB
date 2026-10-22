#include "styles.h"

#include <algorithm>

#include <cstdio>

#include "log.h"

namespace dnb {
namespace {

// ----------------------------------------------------------- drum helpers --

DrumDef mk(const char* name, const char* label, DrumType t, std::array<float, 6> p, float gain, float verb = 0.f,
           int chokes = -1) {
    DrumDef d;
    d.name = name;
    d.label = label;
    d.type = t;
    for (int i = 0; i < 6; ++i) d.p[i] = p[static_cast<size_t>(i)];
    d.gain = gain;
    d.verb = verb;
    d.chokes = chokes;
    return d;
}
DrumDef kick(const char* n, const char* l, float f0, float f1, float pdec, float adec, float click, float drive, float g = 1.f) {
    return mk(n, l, DrumType::Kick, {f0, f1, pdec, adec, click, drive}, g);
}
DrumDef snare(const char* n, const char* l, float tone, float tdec, float ndec, float hp, float mix, float bright,
              float g = 1.f, float verb = 0.08f) {
    return mk(n, l, DrumType::Snare, {tone, tdec, ndec, hp, mix, bright}, g, verb);
}
DrumDef clap(const char* n, const char* l, float hz, float tail, float spread, float q, float g = 1.f, float verb = 0.12f) {
    return mk(n, l, DrumType::Clap, {hz, tail, spread, q, 0, 0}, g, verb);
}
DrumDef metal(const char* n, const char* l, float ratio, float bp, float q, float hp, float dec, float noise, float g,
              int chokes = -1, float verb = 0.f) {
    return mk(n, l, DrumType::Metal, {ratio, bp, q, hp, dec, noise}, g, verb, chokes);
}
DrumDef closedHat(const char* n = "Closed hat", float dec = 60.f, float g = 0.55f, int chokes = 3) {
    return metal(n, "Hat", 1.0f, 10000.f, 1.0f, 7000.f, dec, 0.5f, g, chokes);
}
DrumDef openHat(const char* n = "Open hat", float dec = 9.f, float g = 0.5f) {
    return metal(n, "Open\nhat", 1.0f, 10000.f, 1.0f, 6500.f, dec, 0.5f, g);
}
DrumDef ride(float g = 0.45f) { return metal("Ride", "Ride", 1.35f, 5200.f, 1.6f, 3000.f, 3.2f, 0.25f, g); }
DrumDef crash(float g = 0.55f) { return metal("Crash", "Crash", 1.1f, 7000.f, 0.5f, 4000.f, 1.6f, 0.7f, g, -1, 0.2f); }
DrumDef tamb(float g = 0.4f) { return metal("Tambourine", "Tamb", 1.6f, 9000.f, 1.2f, 6000.f, 16.f, 0.75f, g); }
DrumDef rim(const char* n = "Rim", float hz = 1700.f, float g = 0.6f) { return mk(n, "Rim", DrumType::Rim, {hz, 45.f, 0, 0, 0, 0}, g, 0.05f); }
DrumDef tom(const char* n, const char* l, float hz, float drop, float dec, float noise, float g = 0.8f, float verb = 0.1f) {
    return mk(n, l, DrumType::Tom, {hz, drop, dec, noise, 0, 0}, g, verb);
}
DrumDef shaker(float g = 0.5f, float dec = 28.f) { return mk("Shaker", "Shaker", DrumType::Shaker, {6000.f, 9.f, dec, 0, 0, 0}, g); }
DrumDef cowbell(float g = 0.5f) { return mk("Cowbell", "Cow\nbell", DrumType::Cowbell, {540.f, 800.f, 9.f, 0, 0, 0}, g, 0.05f); }

PatternDef pat(const char* name, std::array<const char*, kTracks> rows) {
    PatternDef p;
    p.name = name;
    for (size_t i = 0; i < rows.size(); ++i) p.rows[i] = rows[i] ? rows[i] : "";
    return p;
}

const std::array<int, 7> MINOR{0, 2, 3, 5, 7, 8, 10};
const std::array<int, 7> DORIAN{0, 2, 3, 5, 7, 9, 10};
const std::array<int, 7> PHRYGIAN{0, 1, 3, 5, 7, 8, 10};

// ---------------------------------------------------------------- styles --

void enrich(const Style& s, PatternDef& p);

std::vector<Style> build() {
    std::vector<Style> v;

    {   // ------------------------------------------------ Drum & Bass
        Style s;
        s.name = "Drum & Bass";
        s.duckRelease = 0.18f;
        s.padLabel = "DnB";
        s.trim = 0.93f;
        s.blurb = "174 BPM rollers: Reese bass, two-step breaks, rave stabs";
        s.bpm = 174; s.swing = 0.5f; s.root = 29; s.scale = MINOR; s.keyName = "F minor"; s.duck = 0.55f;
        s.drums = {kick("Kick", "Kick", 236, 46, 32, 5.5f, 0.35f, 0.24f),
                   snare("Snare", "Snare", 190, 28, 15, 1400, 0.45f, 12000),
                   snare("Ghost snare", "Ghost", 200, 50, 30, 1600, 0.4f, 9000, 0.32f, 0.f),
                   closedHat(), openHat(), ride(), crash()};
        s.bass.name = "Reese bass";
        s.bass.filter = 1; s.bass.width = 0.6f;
        s.bass.saw = 1; s.bass.sub = 0.6f; s.bass.detune = 0.48f; s.bass.cutoff = 260; s.bass.envAmt = 2400;
        s.bass.envDecay = 4; s.bass.res = 1.6f; s.bass.release = 9; s.bass.drive = 2.2f;
        s.chord.name = "Rave stab";
        s.chord.unison = 3; s.chord.spread = 0.7f; s.chord.chorus = 0.2f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 5; s.chord.decay = 3.5f;
        s.chord.cutoff = 400; s.chord.envAmt = 3200; s.chord.envDecay = 9; s.chord.res = 1.1f; s.chord.verb = 0.35f;
        s.chord.delay = 0.12f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN};
        s.led = 0x03;
        s.patterns = {
            pat("Two-step", {"x.........x.....", "....x.......x...", ".......x.x....x.", "x.x.x.x.x.x.x.x.",
                             "..............x.", "................", "0--0--2---0--3--", "................"}),
            pat("Amen roller", {"x.x.......xx....", "....x..x.x...x..", "......x.......x.", "................",
                                "................", "x.x.x.x.x.x.x.x.", "0-------5-------", "0..............."}),
            pat("Jungle", {"x.....x...x.....", "....x..x.x..x..x", ".x.........x....", "xxxxxx.xxxxxxx.x",
                           "......x.......x.", "................", "0---0---6---4---", "................"}),
            pat("Liquid", {"x.........x.....", "....x.......x...", ".........x....x.", "..x...x...x...x.",
                           "................", "x.x.x.x.x.x.x.x.", "0-----2-----5---", "0-----5-------3-"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ House
        Style s;
        s.name = "House";
        s.duckRelease = 0.14f;
        s.padLabel = "House";
        s.trim = 0.9f;
        s.blurb = "124 BPM: 909 kit, offbeat hats, deep sub and organ chords";
        s.bpm = 124; s.swing = 0.54f; s.root = 33; s.scale = DORIAN; s.keyName = "A dorian"; s.duck = 0.6f;
        s.drums = {kick("909 kick", "Kick", 300, 52, 28, 4.5f, 0.5f, 0.4f),
                   clap("Clap", "Clap", 1200, 18, 9, 1.4f),
                   closedHat("Closed hat", 70.f, 0.45f),
                   openHat("Open hat", 8.f, 0.5f),
                   shaker(0.45f),
                   rim("Rim", 1700.f, 0.5f),
                   crash()};
        s.bass.name = "Deep sub";
        s.bass.saw = 0.15f; s.bass.sine = 1.f; s.bass.sub = 0.f; s.bass.oscOctave = 0; s.bass.cutoff = 500;
        s.bass.envAmt = 700; s.bass.envDecay = 6; s.bass.decay = 2.5f; s.bass.release = 12; s.bass.drive = 1.6f;
        s.bass.gain = 1.2f;
        s.chord.name = "Organ stab";
        s.chord.chorus = 0.5f; s.chord.spread = 0.4f; s.chord.osc = ChordOsc::Organ; s.chord.notes = 4; s.chord.decay = 6.f;
        s.chord.release = 10; s.chord.cutoff = 4000; s.chord.gain = 0.7f; s.chord.verb = 0.2f; s.chord.delay = 0.1f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_GATE, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
                FX_LPF, FX_HPF, FX_PUMP, FX_FLANGE, FX_REVERB, FX_LOFI, FX_RISER, FX_CRUSH};
        s.led = 0x33;
        s.patterns = {
            pat("Classic", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.",
                            "xoxoxoxoxoxoxoxo", "...x.......x.x..", "..0-..0-..0-.2-.", "0..0..0...3..3.."}),
            pat("Deep", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "................",
                         "o.o.o.o.o.o.o.o.", "..........x.....", "0-....0-.0-...3-", "0.....0.......3."}),
            pat("Jackin'", {"x...x...x...x...", "....x.......x.x.", "XoxoXoxoXoxoXoxo", "..x...x...x...x.",
                            "................", "x..x..x...x..x..", "0.07..0.0.07..0.", "...0.....0..3..."}),
            pat("Piano house", {"x...x...x...x...", "....x.......x...", "................", "..x...x...x...x.",
                                "xoxoxoxoxoxoxoxo", "................", "0.0.3.3.4.4.2.2.", "0...3...4...2..."}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Techno
        Style s;
        s.name = "Techno";
        s.duckRelease = 0.12f;
        s.padLabel = "Techno";
        s.trim = 0.74f;
        s.blurb = "132 BPM: rumbling kick, acid 303 line, dub chords";
        s.bpm = 132; s.swing = 0.5f; s.root = 28; s.scale = PHRYGIAN; s.keyName = "E phrygian"; s.duck = 0.5f;
        s.drums = {kick("Rumble kick", "Kick", 260, 44, 30, 3.0f, 0.4f, 1.0f, 0.9f),
                   clap("Clap", "Clap", 1100, 14, 10, 1.2f, 0.9f, 0.25f),
                   closedHat("Closed hat", 80.f, 0.45f),
                   openHat("Open hat", 7.f, 0.45f),
                   ride(0.35f),
                   tom("Perc", "Perc", 260, 0.6f, 12, 0.15f, 0.6f, 0.25f),
                   crash()};
        s.bass.name = "Acid 303";
        s.bass.filter = 1; s.bass.width = 0.f;
        s.bass.saw = 1; s.bass.sub = 0.f; s.bass.cutoff = 180; s.bass.envAmt = 2600; s.bass.envDecay = 7;
        s.bass.res = 7.f; s.bass.release = 30; s.bass.glide = 0.06f; s.bass.drive = 1.8f; s.bass.accent = 1.9f;
        s.bass.gain = 0.8f;
        s.chord.name = "Dub chord";
        s.chord.unison = 2; s.chord.spread = 0.8f; s.chord.chorus = 0.3f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.decay = 7;
        s.chord.cutoff = 600; s.chord.envAmt = 1500; s.chord.envDecay = 10; s.chord.res = 1.5f;
        s.chord.verb = 0.45f; s.chord.delay = 0.5f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_GATE, FX_RISER, FX_PUMP};
        s.led = 0x13;
        s.patterns = {
            pat("Peak time", {"x...x...x...x...", "....x.......x...", "xxXxxxXxxxXxxxXx", "..x...x...x...x.",
                              "................", "...........x..x.", "..0...0...0...0.", "................"}),
            pat("Hypnotic", {"x...x...x...x...", "................", ".o.o.o.o.o.o.o.o", "..x...x...x...x.",
                             "o.o.o.o.o.o.o.o.", "......x.......x.", "................", "...0..0....0..1."}),
            pat("Industrial", {"x...x...x..xx...", "....X.......X...", "xxxxxxxxxxxxxxxx", "................",
                               "................", "..x..x..x..x..x.", "0.0.0.0.0.0.0.0.", "................"}),
            pat("Acid", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.",
                         "................", "................", "00A0.70a0H0.3d.0", "................"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Hip-hop
        Style s;
        s.name = "Hip-hop";
        s.padLabel = "Hip-\nhop";
        s.blurb = "90 BPM boom bap: dusty drums, swing, Rhodes chords";
        s.bpm = 90; s.swing = 0.58f; s.root = 26; s.scale = DORIAN; s.keyName = "D dorian"; s.duck = 0.2f;
        s.drums = {kick("Dusty kick", "Kick", 200, 55, 25, 6, 0.25f, 0.5f),
                   snare("Snare", "Snare", 185, 24, 16, 800, 0.5f, 7000, 1.0f, 0.1f),
                   closedHat("Closed hat", 55.f, 0.45f),
                   openHat("Open hat", 8.f, 0.4f),
                   rim("Rim", 1600.f, 0.5f),
                   shaker(0.4f),
                   crash(0.45f)};
        s.bass.name = "Round bass";
        s.bass.saw = 0.2f; s.bass.sine = 0.9f; s.bass.sub = 0.5f; s.bass.cutoff = 500; s.bass.envAmt = 500;
        s.bass.decay = 2.5f; s.bass.release = 10; s.bass.drive = 1.5f; s.bass.gain = 1.1f;
        s.chord.name = "Rhodes";
        s.chord.chorus = 0.6f; s.chord.spread = 0.5f; s.chord.osc = ChordOsc::EPiano; s.chord.notes = 4; s.chord.decay = 1.2f;
        s.chord.release = 4; s.chord.gain = 0.8f; s.chord.verb = 0.25f; s.chord.delay = 0.05f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_LOFI,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN};
        s.led = 0x32;
        s.patterns = {
            pat("Boom bap", {"x......x..x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................",
                             "..............x.", "................", "0------.3-..2-..", "0-------5-------"}),
            pat("Off-kilter", {"x......x.x....x.", "....x.......x...", "x.oxx.x.x.oxx.x.", "................",
                               "...o......o.....", "................", "0-...0..3-...5..", "0-------3-----4-"}),
            pat("Laid back", {"x..x......x..x..", "....x.......x...", "XoxoXoxoXoxoXoxo", "..............x.",
                              "................", "o.o.o.o.o.o.o.o.", "0..0..0...3..4..", "0-------3-------"}),
            pat("Jazzy", {"x.......x.x.....", "....x.......x..o", "x.x.x.x.x.x.x.x.", "................",
                          "......o.......o.", "................", "0---2---3---4---", "0-------3---4---"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Trap
        Style s;
        s.name = "Trap";
        s.padLabel = "Trap";
        s.blurb = "140 BPM half-time: 808 slides, hi-hat rolls, bells";
        s.bpm = 140; s.swing = 0.5f; s.root = 31; s.scale = MINOR; s.keyName = "G minor"; s.duck = 0.1f;
        s.drums = {kick("Kick", "Kick", 250, 55, 35, 9, 0.5f, 0.5f, 0.9f),
                   clap("Clap", "Clap", 1300, 16, 8, 1.5f, 1.0f, 0.15f),
                   closedHat("Hi-hat", 90.f, 0.45f),
                   openHat("Open hat", 10.f, 0.4f),
                   rim("Snap", 2200.f, 0.45f),
                   tom("Perc", "Perc", 420, 0.3f, 16, 0.05f, 0.5f, 0.2f),
                   crash(0.45f)};
        s.bass.name = "808";
        s.bass.saw = 0; s.bass.sine = 1; s.bass.sub = 0; s.bass.oscOctave = 0; s.bass.cutoff = 8000; s.bass.envAmt = 0;
        s.bass.decay = 0.9f; s.bass.release = 4; s.bass.glide = 0.09f; s.bass.drop = 7; s.bass.dropRate = 28;
        s.bass.drive = 2.8f; s.bass.gain = 1.1f;
        s.chord.name = "Bells";
        s.chord.spread = 0.7f; s.chord.chorus = 0.2f; s.chord.osc = ChordOsc::Bell; s.chord.notes = 3; s.chord.octave = 36;
        s.chord.decay = 2.5f; s.chord.release = 3; s.chord.gain = 0.55f; s.chord.verb = 0.35f; s.chord.delay = 0.25f;
        s.fx = {FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_GATE,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_LASER, FX_RISER, FX_HORN};
        s.led = 0x30;
        s.patterns = {
            pat("Classic", {"x......x..x.....", "........x.......", "x.x.x.xrx.x.x.tt", "................",
                            "...........x....", "................", "0------0..d-----", "0...0...5...4..."}),
            pat("Rolling hats", {"x.....x...x...x.", "........x.......", "xxxxrxxxtxxxrrxx", "......x.........",
                                 "................", "...x.......x....", "0-----0---4-----", "................"}),
            pat("Drill", {"x.........x.....", "........x.....x.", "x..x..x...x..x..", "................",
                          "......x.........", "................", "0-----e---3--b--", "0.......5......."}),
            pat("Bounce", {"x..x..x...x..x..", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x.......x.....",
                           "................", "..x.....x.......", "0..0..0...3..4..", "................"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Dubstep
        Style s;
        s.name = "Dubstep";
        s.duckRelease = 0.2f;
        s.padLabel = "Dub-\nstep";
        s.trim = 0.77f;
        s.blurb = "140 BPM half-time: wobble bass, huge snare, dark pads";
        s.bpm = 140; s.swing = 0.5f; s.root = 29; s.scale = PHRYGIAN; s.keyName = "F phrygian"; s.duck = 0.5f;
        s.drums = {kick("Heavy kick", "Kick", 280, 48, 30, 4.5f, 0.55f, 0.7f),
                   snare("Big snare", "Snare", 175, 18, 9, 500, 0.5f, 11000, 1.1f, 0.3f),
                   closedHat("Closed hat", 60.f, 0.45f),
                   openHat("Open hat", 8.f, 0.4f),
                   clap("Clap", "Clap", 1200, 16, 9, 1.4f, 0.7f, 0.3f),
                   tom("Tom", "Tom", 150, 0.9f, 8, 0.15f, 0.7f, 0.2f),
                   crash()};
        s.bass.name = "Wobble";
        s.bass.filter = 1; s.bass.width = 0.5f;
        s.bass.saw = 0.8f; s.bass.square = 0.6f; s.bass.sub = 0.7f; s.bass.detune = 0.3f; s.bass.cutoff = 160;
        s.bass.envAmt = 0; s.bass.lfoDepth = 2200; s.bass.lfoRate = 2.f; s.bass.res = 3.f; s.bass.drive = 3.f;
        s.bass.release = 10;
        s.chord.name = "Dark pad";
        s.chord.unison = 5; s.chord.spread = 1.f; s.chord.chorus = 0.5f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.attack = 0.25f;
        s.chord.decay = 0; s.chord.sustain = 1; s.chord.release = 1.5f; s.chord.cutoff = 900; s.chord.detune = 0.6f;
        s.chord.gain = 0.55f; s.chord.verb = 0.5f; s.chord.delay = 0.1f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_LASER};
        s.led = 0x31;
        s.patterns = {
            pat("Half-time", {"x......x..x.....", "........x.......", "x.x.x.x.x.x.x.x.", "......x.......x.",
                              "........x.......", "..............xx", "0-------..0-3---", "0---------------"}),
            pat("Wobble", {"x.....x...x.....", "........x.......", "..x...x...x...x.", "................",
                           "........x.......", "................", "0---0---5---4---", "................"}),
            pat("Riddim", {"x..x......x.....", "........x.......", "x.x.x.x.x.x.x.x.", "................",
                           "................", "....x.......x...", "0-.0-.0-....0-..", "................"}),
            pat("Dub", {"x.........x.....", "........x.......", "..x...x...x...x.", "..............x.",
                        "................", "....x...........", "0-------3-------", "0-------3-------"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ UK Garage
        Style s;
        s.name = "UK Garage";
        s.duckRelease = 0.14f;
        s.padLabel = "UK\nGarage";
        s.trim = 1.07f;
        s.blurb = "132 BPM: shuffled 2-step, bumpy organ bass, M1 stabs";
        s.bpm = 132; s.swing = 0.64f; s.root = 31; s.scale = MINOR; s.keyName = "G minor"; s.duck = 0.45f;
        s.drums = {kick("Kick", "Kick", 230, 52, 30, 6, 0.4f, 0.35f),
                   snare("Snare", "Snare", 200, 26, 16, 1200, 0.4f, 12000, 0.9f, 0.1f),
                   closedHat("Closed hat", 70.f, 0.45f),
                   openHat("Open hat", 9.f, 0.45f),
                   rim("Rim", 1750.f, 0.55f),
                   shaker(0.4f),
                   crash()};
        s.bass.name = "Organ bass";
        s.bass.saw = 0; s.bass.square = 0.8f; s.bass.sine = 0.5f; s.bass.sub = 0.3f; s.bass.cutoff = 900;
        s.bass.envAmt = 800; s.bass.envDecay = 8; s.bass.decay = 4; s.bass.release = 14; s.bass.drive = 1.8f;
        s.chord.name = "M1 organ";
        s.chord.chorus = 0.4f; s.chord.spread = 0.4f; s.chord.osc = ChordOsc::Organ; s.chord.notes = 4; s.chord.decay = 9;
        s.chord.release = 12; s.chord.gain = 0.7f; s.chord.verb = 0.2f; s.chord.delay = 0.12f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_PUMP,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN};
        s.led = 0x23;
        s.patterns = {
            pat("2-step", {"x......x..x.....", "....x.......x...", "xoxoxoxoxoxoxoxo", "................",
                           "...x.x.....x...x", "................", "0-.0..3-..3-.2..", "..0.....0..3...."}),
            pat("Speed garage", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.",
                                 "................", "x.x.x.x.x.x.x.x.", "0.00.0..0.00.3.0", "................"}),
            pat("Bumpy", {"x.....x..x......", "....x.......x...", "x.xx.xx.x.xx.xx.", "..............x.",
                          "..x....x..x.....", "oxoxoxoxoxoxoxox", "0..0..3..0..5..4", "0.....3.....4..."}),
            pat("Vocal", {"x......x.x......", "....x.......x...", "..x...x...x...x.", "................",
                          "x..x.......x....", "................", "0---..3---..2-..", "0..0..3...3..2.."}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Reggaeton
        Style s;
        s.name = "Reggaeton";
        s.padLabel = "Reggae\n-ton";
        s.trim = 1.08f;
        s.blurb = "95 BPM dembow: timbales, sub bass, plucky chords";
        s.bpm = 95; s.swing = 0.5f; s.root = 33; s.scale = MINOR; s.keyName = "A minor"; s.duck = 0.3f;
        s.drums = {kick("Kick", "Kick", 220, 52, 28, 5.5f, 0.35f, 0.4f),
                   snare("Snare", "Snare", 220, 30, 20, 1500, 0.45f, 10000, 0.8f, 0.08f),
                   closedHat("Closed hat", 65.f, 0.4f),
                   openHat("Open hat", 9.f, 0.4f),
                   tom("Timbale", "Timb", 380, 0.25f, 10, 0.2f, 0.6f, 0.15f),
                   shaker(0.4f),
                   crash()};
        s.bass.name = "Sub bass";
        s.bass.saw = 0.1f; s.bass.sine = 1; s.bass.sub = 0; s.bass.oscOctave = 0; s.bass.cutoff = 600; s.bass.envAmt = 400;
        s.bass.decay = 2.f; s.bass.release = 10; s.bass.drive = 1.8f; s.bass.gain = 1.2f;
        s.chord.name = "Pluck";
        s.chord.unison = 3; s.chord.spread = 0.7f; s.chord.chorus = 0.2f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.decay = 6;
        s.chord.cutoff = 500; s.chord.envAmt = 3500; s.chord.envDecay = 14; s.chord.res = 1.3f;
        s.chord.gain = 0.7f; s.chord.verb = 0.25f; s.chord.delay = 0.15f;
        s.fx = {FX_STUT8, FX_STUT16, FX_STUT32, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_GATE,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_REVERB, FX_LASER, FX_RISER, FX_HORN};
        s.led = 0x03;
        s.patterns = {
            pat("Dembow", {"x...x...x...x...", "...x..x....x..x.", "x.x.x.x.x.x.x.x.", "................",
                           "..............xx", "oxoxoxoxoxoxoxox", "0..0..0.5..5..4.", "0.......5......."}),
            pat("Perreo", {"x...x...x...x...", "...x..x....x..x.", "................", "..x...x...x...x.",
                           "...x.......x....", "x.x.x.x.x.x.x.x.", "0...0...3...4...", "0..0..0.5..5..5."}),
            pat("Dancehall", {"x......x..x.....", "...x..x.....x...", "x.x.x.x.x.x.x.x.", "................",
                              "..x.....x..x....", "................", "0..0..0...4..3..", "..0...0...0...0."}),
            pat("Moombah", {"x...x...x...x...", "...x..x....x..x.", "..x...x...x...x.", "................",
                            "xx..x.x...x.x...", "................", "0-..0-..3-..2-..", "0...........4..."}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Disco / Funk
        Style s;
        s.name = "Disco";
        s.padLabel = "Disco";
        s.trim = 1.1f;
        s.blurb = "118 BPM: four-to-the-floor, octave bass, strings, cowbell";
        s.bpm = 118; s.swing = 0.52f; s.root = 28; s.scale = DORIAN; s.keyName = "E dorian"; s.duck = 0.25f;
        s.drums = {kick("Kick", "Kick", 180, 60, 25, 7, 0.3f, 0.2f),
                   snare("Snare", "Snare", 200, 26, 16, 1000, 0.5f, 9000, 0.85f, 0.12f),
                   closedHat("Closed hat", 50.f, 0.45f),
                   openHat("Open hat", 8.f, 0.45f),
                   tamb(),
                   cowbell(),
                   clap("Clap", "Clap", 1200, 18, 9, 1.4f, 0.8f, 0.15f)};
        s.bass.name = "Slap bass";
        s.bass.width = 0.2f;
        s.bass.saw = 0.9f; s.bass.square = 0.3f; s.bass.sub = 0.3f; s.bass.cutoff = 400; s.bass.envAmt = 3500;
        s.bass.envDecay = 12; s.bass.res = 2.5f; s.bass.decay = 3.5f; s.bass.release = 14; s.bass.drive = 1.8f;
        s.chord.name = "Strings";
        s.chord.unison = 4; s.chord.spread = 1.f; s.chord.chorus = 0.6f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 4; s.chord.attack = 0.12f;
        s.chord.decay = 0.5f; s.chord.sustain = 0.8f; s.chord.release = 2.5f; s.chord.cutoff = 2500;
        s.chord.detune = 0.5f; s.chord.gain = 0.5f; s.chord.verb = 0.45f; s.chord.delay = 0.05f;
        s.fx = {FX_STUT8, FX_STUT16, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_GATE, FX_PUMP,
                FX_LPF, FX_HPF, FX_FLANGE, FX_REVERB, FX_LOFI, FX_LASER, FX_RISER, FX_CRUSH};
        s.led = 0x33;
        s.patterns = {
            pat("Four on the floor", {"x...x...x...x...", "....x.......x...", "x...x...x...x...", "..x...x...x...x.",
                                      "xoxoxoxoxoxoxoxo", "................", "0.7.0.7.2.9.2.9.", "0-------2-------"}),
            pat("Funk", {"x.....x.x.x.....", "....x..o.o..x..o", "XoxoXoxoXoxoXoxo", "................",
                         "................", "................", "0..0.7.3..0.5.4.", "0-------3-------"}),
            pat("Nu-disco", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.",
                             "................", "................", "0.0.H.0.3.3.H.3.", "0-------5---4---"}),
            pat("Cowbell groove", {"x...x...x...x...", "................", "x.x.x.x.x.x.x.x.", "................",
                                   "..x...x...x...x.", "x..x..x...x.x...", "0..0..7.0..0..3.", "0-------3-------"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Synthwave
        Style s;
        s.name = "Synthwave";
        s.duckRelease = 0.2f;
        s.padLabel = "Synth\nwave";
        s.blurb = "100 BPM: gated snare, pulsing arp bass, wide pads";
        s.bpm = 100; s.swing = 0.5f; s.root = 33; s.scale = MINOR; s.keyName = "A minor"; s.duck = 0.4f;
        s.drums = {kick("Kick", "Kick", 240, 50, 30, 5, 0.45f, 0.45f),
                   snare("Gated snare", "Snare", 170, 18, 9, 700, 0.4f, 9000, 1.0f, 0.55f),
                   closedHat("Closed hat", 60.f, 0.4f),
                   openHat("Open hat", 8.f, 0.4f),
                   tom("80s tom", "Tom", 140, 1.0f, 7, 0.2f, 0.8f, 0.35f),
                   clap("Clap", "Clap", 1200, 16, 9, 1.4f, 0.6f, 0.4f),
                   crash()};
        s.bass.name = "Arp bass";
        s.bass.filter = 1; s.bass.width = 0.4f;
        s.bass.saw = 1; s.bass.detune = 0.3f; s.bass.sub = 0.5f; s.bass.cutoff = 300; s.bass.envAmt = 3000;
        s.bass.envDecay = 9; s.bass.res = 2; s.bass.decay = 5; s.bass.release = 14; s.bass.drive = 1.6f;
        s.chord.name = "Pad";
        s.chord.unison = 5; s.chord.spread = 1.f; s.chord.chorus = 0.6f; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.attack = 0.4f;
        s.chord.decay = 0; s.chord.sustain = 1; s.chord.release = 2.5f; s.chord.cutoff = 1600; s.chord.detune = 0.7f;
        s.chord.gain = 0.55f; s.chord.verb = 0.6f; s.chord.delay = 0.2f;
        s.fx = {FX_STUT8, FX_STUT16, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_GATE, FX_PUMP,
                FX_LPF, FX_HPF, FX_FLANGE, FX_REVERB, FX_LOFI, FX_LASER, FX_RISER, FX_CRUSH};
        s.led = 0x13;
        s.patterns = {
            pat("Drive", {"x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "................",
                          "................", "....x.......x...", "0000000055554444", "0-------5---4---"}),
            pat("Night drive", {"x.........x.....", "........x.......", "x.x.x.x.x.x.x.x.", "..............x.",
                                "..........x.x.x.", "................", "0.0.0.0.5.5.4.4.", "0-------5---4---"}),
            pat("Outrun", {"x...x...x...x...", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x...x...x...x.",
                           "................", "................", "0707070757574747", "0-------3-------"}),
            pat("Ballad", {"x.......x.x.....", "....x.......x...", "x...x...x...x...", "................",
                           "..............xx", "................", "0-------5---3---", "0-------5---3---"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Lo-fi
        Style s;
        s.name = "Lo-fi";
        s.padLabel = "Lo-fi";
        s.trim = 1.15f;
        s.blurb = "78 BPM: lazy swing, soft drums, jazzy electric piano";
        s.bpm = 78; s.swing = 0.62f; s.root = 26; s.scale = DORIAN; s.keyName = "D dorian"; s.duck = 0.2f;
        s.drums = {kick("Soft kick", "Kick", 160, 55, 20, 7, 0.1f, 0.2f),
                   snare("Snare", "Snare", 210, 30, 22, 1200, 0.5f, 5000, 0.8f, 0.12f),
                   closedHat("Closed hat", 50.f, 0.35f),
                   openHat("Open hat", 9.f, 0.3f),
                   rim("Rim", 1500.f, 0.45f),
                   shaker(0.35f),
                   clap("Clap", "Clap", 1000, 20, 10, 1.2f, 0.6f, 0.2f)};
        s.bass.name = "Warm sub";
        s.bass.saw = 0.1f; s.bass.sine = 1; s.bass.sub = 0.4f; s.bass.cutoff = 600; s.bass.envAmt = 300;
        s.bass.decay = 1.5f; s.bass.release = 8; s.bass.drive = 1.3f; s.bass.gain = 1.1f;
        s.chord.name = "Electric piano";
        s.chord.chorus = 0.7f; s.chord.spread = 0.5f; s.chord.osc = ChordOsc::EPiano; s.chord.notes = 5; s.chord.decay = 1.0f;
        s.chord.release = 3; s.chord.gain = 0.7f; s.chord.verb = 0.3f; s.chord.delay = 0.08f;
        s.fx = {FX_LOFI, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_REVERB, FX_STUT4, FX_STUT8,
                FX_LPF, FX_HPF, FX_CRUSH, FX_FLANGE, FX_GATE, FX_PUMP, FX_RISER, FX_STUT16};
        s.led = 0x32;
        s.patterns = {
            pat("Chill", {"x......x..x.....", "....x.......x...", "x.o.x.o.x.o.x.o.", "................",
                          "................", "................", "0-------3-------", "0-------3-------"}),
            pat("Study", {"x.........x.....", "....x.......x...", "x.ox.o.xx.ox.o.x", "..............x.",
                          "......o......o..", "................", "0-..5-..3-..4-..", "0---5---3---4---"}),
            pat("Rainy", {"x.......x.......", "....x.......x...", "................", "................",
                          "..x.....x..x....", "xoxoxoxoxoxoxoxo", "0-------6-------", "0-------6-------"}),
            pat("Jazz hop", {"x......x.x......", "....x.......x...", "x..x..x.x..x..x.", "................",
                             "................", "................", "0---2---3---4---", "0-------3---4---"}),
        };
        v.push_back(s);
    }

    {   // ------------------------------------------------ Afro house
        Style s;
        s.name = "Afro House";
        s.padLabel = "Afro\nHouse";
        s.trim = 1.05f;
        s.blurb = "122 BPM: congas, shakers, clave, marimba";
        s.bpm = 122; s.swing = 0.55f; s.root = 31; s.scale = DORIAN; s.keyName = "G dorian"; s.duck = 0.4f;
        s.drums = {kick("Kick", "Kick", 220, 54, 28, 5.5f, 0.35f, 0.3f),
                   clap("Clap", "Clap", 1200, 18, 9, 1.4f, 0.8f, 0.15f),
                   shaker(0.45f, 24.f),
                   tom("Conga high", "Conga\nhi", 330, 0.15f, 14, 0.05f, 0.6f, 0.08f),
                   tom("Conga low", "Conga\nlo", 220, 0.15f, 11, 0.05f, 0.65f, 0.08f),
                   rim("Clave", 2400.f, 0.45f),
                   cowbell(0.45f)};
        s.drums[5].label = "Clave";
        s.bass.name = "Round sub";
        s.bass.saw = 0; s.bass.square = 0.2f; s.bass.sine = 0.9f; s.bass.sub = 0.4f; s.bass.cutoff = 700;
        s.bass.envAmt = 500; s.bass.decay = 3.5f; s.bass.release = 12; s.bass.drive = 1.6f; s.bass.gain = 1.1f;
        s.chord.name = "Marimba";
        s.chord.spread = 0.6f; s.chord.osc = ChordOsc::Marimba; s.chord.notes = 3; s.chord.octave = 36;
        s.chord.decay = 7; s.chord.release = 8; s.chord.gain = 0.7f; s.chord.verb = 0.2f; s.chord.delay = 0.15f;
        s.fx = {FX_STUT4, FX_STUT8, FX_STUT16, FX_TAPESTOP, FX_HALFTIME, FX_REVERSE, FX_DELAY, FX_GATE,
                FX_LPF, FX_HPF, FX_PUMP, FX_FLANGE, FX_REVERB, FX_SIREN, FX_RISER, FX_HORN};
        s.led = 0x30;
        s.patterns = {
            pat("Afro house", {"x...x...x...x...", "....x.......x...", "xoxoxoxoxoxoxoxo", "..x..x.x..x..x..",
                               "...x......x.....", "x..x..x...x.x...", "0...0..0..3...4.", "0..3..5.4..3..0."}),
            pat("Afrobeats", {"x..x...x..x.....", "....x.......x...", "x.xxx.xxx.xxx.xx", "......x.......x.",
                              "...x.......x....", "..x..x....x..x..", "0..0...3..5..4..", "0...0...3...4..."}),
            pat("Tribal", {"x...x...x...x...", "................", "oxoxoxoxoxoxoxox", "x.xx.x.xx.xx.x.x",
                           "..x..x..x...x..x", "................", "0-..0-..0-..3-..", "................"}),
            pat("Log drum", {"x.......x.......", "....x.......x...", "oooxoooxoooxooox", "................",
                             "................", "x..x..x...x.x...", "..0..0.3..0..5..", "0-------3-------"}),
        };
        v.push_back(s);
    }

    // More built-in patterns (slots 5-8), based on well-known grooves for each style.
    struct Extra { const char* style; const char* name; std::array<const char*, kTracks> rows; };
    static const Extra extras[] = {
        {"Drum & Bass", "Amen (last bar)", {"..xx......x.....", "....x........x..", ".o.....o.o......", "................", "..........x.....", "x.x.x.x.x.x.x.x.", "0---0---3---5---", "................"}},
        {"Drum & Bass", "Think break", {"x.....x...x.....", "....x.......x...", "..o....o.o....o.", "xxxxxxxxxxxxxxxx", "......x.......x.", "................", "0-----0---6-----", "0..............."}},
        {"Drum & Bass", "Halftime", {"x......x..x.....", "........x.......", "............o..o", "x.x.x.x.x.x.x.x.", "..............x.", "................", "0-------3---5---", "0-------5-------"}},
        {"Drum & Bass", "Neuro stepper", {"x.........x.x...", "....x.......x...", "..o..o.....o..o.", "..x...x...x...x.", "......x.......x.", "................", "0.0-0.0-3.3-5.5.", "................"}},
        {"House", "Chicago jack", {"x...x...x...x...", "....x.......x...", "XoxoXoxoXoxoXoxo", "..x...x...x...x.", "................", "x..x..x..x..x..x", "0..0..7.0..0..5.", "0..0..0...0..0.."}},
        {"House", "Shuffle", {"x...x..ox...x..o", "....x.......x...", "..x...x...x...x.", "................", "oo.ooo.ooo.ooo.o", "................", "..0..0.3..0..0.5", "0.....3.....4..."}},
        {"House", "Tech house", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.", "................", "...x..x....x..x.", "..0.0..0..0.0..0", "................"}},
        {"House", "Garage house", {"x...x...x...x...", "....x..x....x...", "xoxoxoxoxoxoxoxo", "..x.......x.....", "................", "..x..x....x..x..", "0-.07-.0..0-.5..", "...0.....0...3.."}},
        {"Techno", "Hard techno", {"x...x...x...x...", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", "x.x.x.x.x.x.x.x.", "................", "0.00.0.00.0.00.0", "................"}},
        {"Techno", "Detroit", {"x...x...x...x...", "....x.......x...", ".o.o.o.o.o.o.o.o", "..x...x...x...x.", "................", "x..x..x...x..x..", "0..0..4...0..3..", "0.....3.....0..."}},
        {"Techno", "Minimal roller", {"x...x...x...x..o", "................", ".o.o.o.o.o.o.o.o", "..x...x...x...x.", "................", "..x....x..x....x", "..0..0....0..0..", "...0.......0...."}},
        {"Techno", "Electro", {"x.....x...x.....", "....x.......x...", "x.xxx.xxx.xxx.xx", "................", "................", "...x......x..x..", "0..0..0.3..3..3.", "................"}},
        {"Hip-hop", "Funky Drummer", {"x.x.......x..x..", "....X..o.o.oX..o", "xxxxxxx.xxxxx.xx", ".......x.....x..", "................", "................", "0.0.......0..3..", "0-------3-------"}},
        {"Hip-hop", "Impeach", {"x......xx.x.....", "....x.......x...", "x.x.x.x.x.x.xxx.", "................", "..............o.", "................", "0------0..3-....", "0-------4-------"}},
        {"Hip-hop", "Head nod", {"x..x..x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "................", "oxoxoxoxoxoxoxox", "0..0..0...3.....", "0-------5-------"}},
        {"Hip-hop", "Bongo break", {"x.....x...x.....", "....x.......x...", "x...x...x...x...", "................", "..x.x..x..x.x..x", "oooooooooooooooo", "0.....3...4.....", "0-------3-------"}},
        {"Trap", "Triplet flow", {"x......x..x.x...", "........x.......", "x..x..x..x..x.x.", "................", "...............x", "................", "0------.0.e-----", "0...0...5...4..."}},
        {"Trap", "Atlanta bounce", {"x.x.....x.x..x..", "........x.......", "xxxtxxxxxxrxxxtx", "......x.........", "................", "....x.......x...", "0.0-----0.0--5--", "................"}},
        {"Trap", "Grime hats drill", {"x.........x.x...", "........x....x..", "x..x..x.x..x..x.", "................", "...x............", "................", "0----c---3--e---", "0.......5......."}},
        {"Trap", "Rage", {"x.......x.......", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", "................", "................", "0.0.0.0.5.5.4.4.", "0.......5......."}},
        {"Dubstep", "Deep dub", {"x.........x.....", "........x.......", "..x...x...x...x.", "................", "................", "......x.....x...", "0-------0---3---", "0---------------"}},
        {"Dubstep", "Brostep", {"x.x.......x.x...", "........x.......", "xxxxxxxxxxxxxxxx", "................", "........x.......", "................", "0-0-0-0-5-5-3-3-", "................"}},
        {"Dubstep", "Tearout", {"x..x..x...x..x..", "........x.....x.", "x.x.x.x.x.x.x.x.", "................", "........x.......", "..............xx", "0.0.0.......0.0.", "................"}},
        {"Dubstep", "Ghost hats", {"x......x..x.....", "........x.......", ".o.o.o.ox.o.o.o.", "......x.........", "................", "...x.......x....", "0-------3-------", "0-------1-------"}},
        {"UK Garage", "Classic 2-step", {"x.........x.....", "....x.......x...", "..x...x...x...x.", "................", ".......x......x.", "oxoxoxoxoxoxoxox", "0-.0-.3-..3-.2..", "..0..0....0..3.."}},
        {"UK Garage", "Future garage", {"x......x..x.....", "....x.......x..x", ".o..o.o..o..o.o.", "......x.......x.", "................", "................", "0-------5-------", "0-------5---3---"}},
        {"UK Garage", "4x4 garage", {"x...x...x...x...", "....x.......x...", "oxoxoxoxoxoxoxox", "..x...x...x...x.", "x..x..x....x..x.", "................", "0.0..0.0.3..3.0.", "...0.......0...."}},
        {"UK Garage", "Grime", {"x.......x.x.....", "........x.......", "x..x..x.x..x..x.", "................", "......x.......x.", "................", "0.......5...3...", "0.......1......."}},
        {"Reggaeton", "Dembow timbal", {"x...x...x...x...", "...x..x....x..x.", "x.x.x.x.x.x.x.x.", "................", "....x.x.....x.xx", "................", "0..0..0.5..5..4.", "0.......5......."}},
        {"Reggaeton", "Trapeton", {"x......x..x.....", "...x..x....x..x.", "x.x.x.xrx.x.x.tt", "................", "................", "................", "0------.0.3-----", "0.......5......."}},
        {"Reggaeton", "One drop", {"........x.......", "........x.......", "..x...x...x...x.", "................", "x.....x.....x...", "................", "0..0....3..3....", "..0...0...0...0."}},
        {"Reggaeton", "Cumbia", {"x...x...x...x...", "....x.......x...", "................", "................", "..x...x...x...x.", "x.xxx.xxx.xxx.xx", "0...4...0...4...", "0..0..0.4..4..4."}},
        {"Disco", "Philly", {"x...x...x...x...", "....x.......x...", "XoxoXoxoXoxoXoxo", "..x...x...x...x.", "....x.......x...", "................", "0.7.0.7.5.5.4.4.", "0-------3-------"}},
        {"Disco", "Boogie", {"x..x..x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "oxoxoxoxoxoxoxox", "................", "0..07..0.5..4.3.", "..0...0...0...0."}},
        {"Disco", "Italo", {"x...x...x...x...", "....x.......x...", ".x.x.x.x.x.x.x.x", "................", "................", "................", "0707070707070707", "0-------5---4---"}},
        {"Disco", "Salsoul", {"x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "..............x.", "xxxxxxxxxxxxxxxx", "x..x..x...x.x...", "0..0..7.0..0..3.", "0-------3-------"}},
        {"Synthwave", "Darksynth", {"x...x...x...x...", "....x.......x...", "xxxxxxxxxxxxxxxx", "................", "................", "................", "0000000033335555", "0-------3-------"}},
        {"Synthwave", "Retro halftime", {"x.........x.....", "........x.......", "x.x.x.x.x.x.x.x.", "................", "............xxx.", "................", "0.0.0.0.5.5.6.6.", "0-------5---6---"}},
        {"Synthwave", "Power ballad", {"x.......x.x.....", "....x.......x...", "x...x...x...x...", "................", "..............t.", "................", "0-------5---6---", "0-------5---6---"}},
        {"Synthwave", "Chillwave", {"x.....x...x.....", "....x.......x...", ".x.x.x.x.x.x.x.x", "................", "................", "....x.......x...", "0..0..0.3..3..3.", "0-------3-------"}},
        {"Lo-fi", "Dilla swing", {"x.....x..x...x..", "....x.......x...", "x.ox.o.xx.ox.o.x", "................", "...o......o.....", "................", "0-..0-.-3-..5-..", "0-------3-----4-"}},
        {"Lo-fi", "Bossa", {"x..x....x..x....", "................", "................", "................", "x..x..x...x..x..", "oxoxoxoxoxoxoxox", "0...4...0...4...", "0-------3-------"}},
        {"Lo-fi", "Brushes", {"x.........x.....", "....o.......o...", "................", "................", "................", "xoxoxoxoxoxoxoxo", "0---2---3---4---", "0---3---4---6---"}},
        {"Lo-fi", "Dusty boom bap", {"x......xx.x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "..............o.", "................", "0------0..3-....", "0-------5-------"}},
        {"Afro House", "Amapiano", {"x.......x.......", "....x.......x...", "xoxoxoxoxoxoxoxo", "................", "................", "x..x..x...x.x...", "0..0.0..3..0.0.5", "0-------3-------"}},
        {"Afro House", "Afrobeat", {"x..x......x..x..", "....x.......x...", "xxxxxxxxxxxxxxxx", "..x..x.x..x..x.x", "x.....x.....x...", "x..x..x...x.x...", "0..0..3...5..4..", "0..3..5.4..3..0."}},
        {"Afro House", "Gqom", {"x..x..x...x..x..", "....x.......x...", "................", "...x...x...x...x", "................", "................", "0-..0-.......0-.", "0-------3-------"}},
        {"Afro House", "Afro tech", {"x...x...x...x...", "....x.......x...", "oxoxoxoxoxoxoxox", "..x...x..x..x...", "x.....x.....x...", "................", "..0.0..0..0.0..0", "................"}},
    };
    for (auto& s : v) {
        int k = 4;
        for (const Extra& e : extras)
            if (s.name == e.style && k < kFactorySlots) s.patterns[static_cast<size_t>(k++)] = pat(e.name, e.rows);
        for (; k < kFactorySlots; ++k) s.patterns[static_cast<size_t>(k)] = pat("Empty", {"", "", "", "", "", "", "", "", ""});
    }
    // Sanity-check the pattern text so a typo can't break the sequencer.
    for (auto& s : v)
        for (auto& p : s.patterns)
            for (size_t t = 0; t < static_cast<size_t>(T_LEAD); ++t)
                if (p.rows[t].size() != kSteps) {
                    logf("Pattern \"%s / %s\" row %zu has %zu steps (fixed to 16)", s.name.c_str(), p.name.c_str(), t,
                         p.rows[t].size());
                    p.rows[t].resize(kSteps, '.');
                }
    for (auto& s : v)
        for (auto& p : s.patterns) enrich(s, p);
    return v;
}

// Gives a one-bar groove a second bar with the little end-of-phrase moves a
// drummer / producer would make: ghost-note pickups, an open hat into the
// next bar, hat dynamics and the bass jumping up an octave on its last note.
void enrich(const Style& s, PatternDef& p) {
    bool anyHit = false;
    for (size_t t = 0; t < static_cast<size_t>(T_LEAD); ++t) {
        if (p.rows[t].size() != static_cast<size_t>(kSteps)) return;  // hand-made longer pattern: leave it
        if (p.rows[t].find_first_not_of('.') != std::string::npos) anyHit = true;
    }
    if (!anyHit) return;
    auto hits = [](const std::string& r) { int n = 0; for (char c : r) n += c != '.' ? 1 : 0; return n; };
    auto hasDynamics = [](const std::string& r) { return r.find_first_of("oXrt") != std::string::npos; };

    std::array<std::string, kTracks> bar2;
    for (size_t t = 0; t < static_cast<size_t>(T_LEAD); ++t) bar2[t] = p.rows[t];

    int closed = -1, open = -1, snareT = -1;
    for (int t = 0; t < kDrumTracks; ++t) {
        const DrumDef& d = s.drums[static_cast<size_t>(t)];
        const std::string& r = p.rows[static_cast<size_t>(t)];
        if (d.type == DrumType::Metal && d.p[4] > 30.f && closed < 0) closed = t;
        else if (d.type == DrumType::Metal && d.p[4] > 5.f && d.p[4] <= 30.f && open < 0) open = t;
        else if ((d.type == DrumType::Snare || d.type == DrumType::Clap) && snareT < 0 &&
                 (r[4] != '.' || r[8] != '.' || r[12] != '.'))
            snareT = t;
    }
    // Hats: 16th-note hats with no dynamics get a strong/weak pattern.
    if (closed >= 0) {
        std::string& r = p.rows[static_cast<size_t>(closed)];
        if (hits(r) >= 12 && !hasDynamics(r)) {
            for (int i = 1; i < kSteps; i += 2) if (r[static_cast<size_t>(i)] == 'x') r[static_cast<size_t>(i)] = 'o';
            bar2[static_cast<size_t>(closed)] = r;
        }
    }
    // Snare: ghost-note pickup at the end of bar 2 (tightened with a decay lock).
    if (snareT >= 0) {
        std::string& r = bar2[static_cast<size_t>(snareT)];
        if (r[15] == '.' && r[14] != 'x' && r[14] != 'X') {
            r[15] = 'o';
            p.locks += std::to_string(snareT) + ":31:0:-40:0 ";
            if (r[13] == '.' && r[12] != '.') r[13] = 'o';
        }
    }
    // Open hat into the next bar, cutting the closed hats under it.
    if (open >= 0 && closed >= 0) {
        std::string& o = bar2[static_cast<size_t>(open)];
        std::string& c = bar2[static_cast<size_t>(closed)];
        if (hits(o) <= 4 && o[14] == '.' && o[15] == '.' && c[14] != '.') {
            o[14] = 'x';
            c[14] = '.';
            c[15] = '.';
        }
    }
    // Bass: the last note of bar 2 jumps up an octave.
    {
        std::string& r = bar2[static_cast<size_t>(T_BASS)];
        for (int i = kSteps - 1; i >= 8; --i) {
            const char ch = r[static_cast<size_t>(i)];
            if (ch == '.' || ch == '-') continue;
            if (i + 1 < kSteps && r[static_cast<size_t>(i + 1)] == '-') break;  // a long held note: leave it
            int deg = -1; char base = '0';
            if (ch >= '0' && ch <= '9') { deg = ch - '0'; base = '0'; }
            else if (ch >= 'A' && ch <= 'J') { deg = ch - 'A'; base = 'A'; }
            else if (ch >= 'a' && ch <= 'j') { deg = ch - 'a'; base = 'a'; }
            if (deg >= 0 && deg <= 2) r[static_cast<size_t>(i)] = static_cast<char>(base + deg + 7);
            break;
        }
    }
    for (size_t t = 0; t < static_cast<size_t>(T_LEAD); ++t) p.rows[t] += bar2[t];
}

}  // namespace

const std::vector<Style>& styles() {
    static const std::vector<Style> s = build();
    return s;
}

const char* noteNameFor(const Style& s, int semis) {
    static const char* n[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return n[(((s.root + semis) % 12) + 12) % 12];
}

std::string chordNumeral(const Style& s, int degree, int notes) {
    static const char* up[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* lo[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const int d = ((degree % 7) + 7) % 7;
    const int third = degreeSemis(s, degree + 2) - degreeSemis(s, degree);
    const int fifth = degreeSemis(s, degree + 4) - degreeSemis(s, degree);
    std::string r = third == 4 ? up[d] : lo[d];
    if (fifth == 6) r += "\xC2\xB0";  // degree sign (diminished)
    if (notes >= 4) r += notes >= 5 ? "9" : "7";
    return r;
}

// Drum, bass and chord rows are one character per step. The lead row is
// space-separated tokens, because it stores real MIDI notes: "60 - - . 63 ...".
int parseRow(const std::string& row, int track, uint8_t codes[kMaxSteps], int8_t notes[kMaxSteps]) {
    for (int s = 0; s < kMaxSteps; ++s) { codes[s] = STEP_OFF; notes[s] = 0; }
    if (track == T_LEAD) {
        int s = 0;
        size_t i = 0;
        while (i < row.size() && s < kMaxSteps) {
            while (i < row.size() && row[i] == ' ') ++i;
            if (i >= row.size()) break;
            size_t j = row.find(' ', i);
            if (j == std::string::npos) j = row.size();
            const std::string tok = row.substr(i, j - i);
            if (tok == "-") codes[s] = STEP_TIE;
            else if (tok != ".") {
                try {
                    int n = std::stoi(tok);
                    codes[s] = STEP_ON;
                    notes[s] = static_cast<int8_t>(std::max(0, std::min(127, n)));
                } catch (...) {
                }
            }
            ++s;
            i = j;
        }
        return s;
    }
    const bool melodic = track >= T_BASS;
    const int n = std::min(kMaxSteps, static_cast<int>(row.size()));
    for (int s = 0; s < n; ++s) {
        const char c = row[static_cast<size_t>(s)];
        uint8_t code = STEP_OFF;
        int8_t note = 0;
        if (melodic) {
            if (c >= '0' && c <= '9') { code = STEP_ON; note = static_cast<int8_t>(c - '0'); }
            else if (c >= 'A' && c <= 'J') { code = STEP_ACCENT; note = static_cast<int8_t>(c - 'A'); }
            else if (c >= 'a' && c <= 'j') { code = STEP_ON | STEP_SLIDE; note = static_cast<int8_t>(c - 'a'); }
            else if (c == '-') { code = STEP_TIE; }
        } else {
            switch (c) {
                case 'x': code = STEP_ON; break;
                case 'X': code = STEP_ACCENT; break;
                case 'o': code = STEP_SOFT; break;
                case 'r': code = STEP_ROLL2; break;
                case 't': code = STEP_ROLL3; break;
                default: break;
            }
        }
        codes[s] = code;
        notes[s] = note;
    }
    return n;
}

std::string encodeRow(const Shared& sh, int p, int t) {
    const int len = sh.patternLength(p);
    if (t == T_LEAD) {
        std::string out;
        for (int s = 0; s < len; ++s) {
            const uint8_t v = sh.on[p][t][s].load();
            if (s) out += ' ';
            if (stepKind(v) == STEP_TIE) out += '-';
            else if (stepSounds(v)) out += std::to_string(static_cast<int>(sh.note[p][t][s].load()));
            else out += '.';
        }
        return out;
    }
    std::string out(static_cast<size_t>(len), '.');
    const bool melodic = t >= T_BASS;
    for (int s = 0; s < len; ++s) {
        const uint8_t v = sh.on[p][t][s].load();
        const int k = stepKind(v);
        const int n = std::clamp<int>(sh.note[p][t][s].load(), 0, 9);
        char c = '.';
        if (melodic) {
            if (k == STEP_TIE) c = '-';
            else if (stepSounds(v)) c = static_cast<char>((v & STEP_SLIDE) ? 'a' + n : (k == STEP_ACCENT ? 'A' + n : '0' + n));
        } else {
            switch (k) {
                case STEP_ON: c = 'x'; break;
                case STEP_ACCENT: c = 'X'; break;
                case STEP_SOFT: c = 'o'; break;
                case STEP_ROLL2: c = 'r'; break;
                case STEP_ROLL3: c = 't'; break;
                default: break;
            }
        }
        out[static_cast<size_t>(s)] = c;
    }
    return out;
}

int setRow(Shared& sh, int p, int t, const std::string& row) {
    uint8_t codes[kMaxSteps];
    int8_t notes[kMaxSteps];
    const int n = parseRow(row, t, codes, notes);
    for (int s = 0; s < kMaxSteps; ++s) {
        sh.on[p][t][s].store(codes[s]);
        sh.note[p][t][s].store(notes[s]);
    }
    return n;
}

// "track:step:pitch:decay:filter" tokens.
static void parseLocks(const std::string& text, int8_t out[kTracks][kMaxSteps][kLocks]) {
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s)
            for (int l = 0; l < kLocks; ++l) out[t][s][l] = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        int v[5] = {-1, -1, 0, 0, 0};
        if (j > i && std::sscanf(text.substr(i, j - i).c_str(), "%d:%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 &&
            v[0] >= 0 && v[0] < kTracks && v[1] >= 0 && v[1] < kMaxSteps) {
            out[v[0]][v[1]][LOCK_PITCH] = static_cast<int8_t>(std::clamp(v[2], -24, 24));
            out[v[0]][v[1]][LOCK_DECAY] = static_cast<int8_t>(std::clamp(v[3], -100, 100));
            out[v[0]][v[1]][LOCK_FILTER] = static_cast<int8_t>(std::clamp(v[4], -100, 100));
        }
        i = j;
    }
}

void applyLocks(Shared& sh, int p, const std::string& text) {
    static int8_t buf[kTracks][kMaxSteps][kLocks];
    parseLocks(text, buf);
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s)
            for (int l = 0; l < kLocks; ++l) sh.lock[p][t][s][l].store(buf[t][s][l]);
}

std::string encodeLocks(const Shared& sh, int p) {
    std::string out;
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) {
            const int a = sh.lock[p][t][s][0].load(), b = sh.lock[p][t][s][1].load(), c = sh.lock[p][t][s][2].load();
            if (!a && !b && !c) continue;
            out += std::to_string(t) + ":" + std::to_string(s) + ":" + std::to_string(a) + ":" + std::to_string(b) + ":" +
                   std::to_string(c) + " ";
        }
    if (!out.empty()) out.pop_back();
    return out;
}

int presetLength(const PatternDef& pd) {
    int len = kSteps;
    uint8_t codes[kMaxSteps];
    int8_t notes[kMaxSteps];
    for (int t = 0; t < kTracks; ++t) len = std::max(len, parseRow(pd.rows[static_cast<size_t>(t)], t, codes, notes));
    return std::min(kMaxSteps, (len + kSteps - 1) / kSteps * kSteps);
}

bool factoryUnchanged(const Shared& sh, int p) {
    const int slot = p % kMaxSlots;
    if (slot >= kFactorySlots) return false;
    const PatternDef& pd = styleOf(p).patterns[static_cast<size_t>(slot)];
    if (sh.patternLength(p) != presetLength(pd)) return false;
    {
        static int8_t want[kTracks][kMaxSteps][kLocks];
        parseLocks(pd.locks, want);
        for (int t = 0; t < kTracks; ++t)
            for (int s = 0; s < kMaxSteps; ++s)
                for (int l = 0; l < kLocks; ++l)
                    if (sh.lock[p][t][s][l].load() != want[t][s][l]) return false;
    }
    for (int t = 0; t < kTracks; ++t) {
        uint8_t codes[kMaxSteps];
        int8_t notes[kMaxSteps];
        parseRow(pd.rows[static_cast<size_t>(t)], t, codes, notes);
        for (int s = 0; s < kMaxSteps; ++s) {
            if (sh.on[p][t][s].load() != codes[s]) return false;
            if (s < sh.patternLength(p) && stepSounds(codes[s]) && t >= T_BASS && sh.note[p][t][s].load() != notes[s]) return false;
        }
    }
    return true;
}

void copyPattern(Shared& sh, int from, int to) {
    sh.length[to].store(sh.length[from].load());
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) {
            sh.on[to][t][s].store(sh.on[from][t][s].load());
            sh.note[to][t][s].store(sh.note[from][t][s].load());
            for (int l = 0; l < kLocks; ++l) sh.lock[to][t][s][l].store(sh.lock[from][t][s][l].load());
        }
}

void clearPattern(Shared& sh, int p) {
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) {
            sh.on[p][t][s].store(STEP_OFF);
            sh.note[p][t][s].store(0);
            for (int l = 0; l < kLocks; ++l) sh.lock[p][t][s][l].store(0);
        }
}

void setPatternLength(Shared& sh, int p, int len) {
    len = std::max(kSteps, std::min(kMaxSteps, (len / kSteps) * kSteps));
    const int old = sh.patternLength(p);
    // Growing: repeat the existing bars into the new ones so it keeps playing the same.
    for (int s = old; s < len; ++s)
        for (int t = 0; t < kTracks; ++t) {
            sh.on[p][t][s].store(sh.on[p][t][s % old].load());
            sh.note[p][t][s].store(sh.note[p][t][s % old].load());
            for (int l = 0; l < kLocks; ++l) sh.lock[p][t][s][l].store(sh.lock[p][t][s % old][l].load());
        }
    sh.length[p].store(static_cast<uint8_t>(len));
}

void loadPreset(Shared& sh, int pattern) {
    const int slot = pattern % kMaxSlots;
    if (slot >= kFactorySlots) return;
    clearPattern(sh, pattern);
    const PatternDef& pd = styleOf(pattern).patterns[static_cast<size_t>(slot)];
    int len = kSteps;
    for (int t = 0; t < kTracks; ++t) len = std::max(len, setRow(sh, pattern, t, pd.rows[static_cast<size_t>(t)]));
    sh.length[pattern].store(static_cast<uint8_t>(std::min(kMaxSteps, (len + kSteps - 1) / kSteps * kSteps)));
    applyLocks(sh, pattern, pd.locks);
    sh.patternName[pattern] = pd.name;
}

void loadAllPresets(Shared& sh) {
    for (int st = 0; st < styleCount() && st < kMaxStyles; ++st) {
        for (int k = 0; k < kFactorySlots; ++k) loadPreset(sh, st * kMaxSlots + k);
        sh.slotCount[st].store(kFactorySlots);
    }
}

// ------------------------------------------------------------ lead patches --

namespace {
std::vector<LeadPatch> buildLeads() {
    std::vector<LeadPatch> v;
    auto poly = [&](const char* name, const char* group, ChordOsc osc, float attack, float decay, float sustain,
                    float release, float cutoff, float envAmt, float envDecay, float res, float detune, float gain,
                    float verb, float delay) {
        LeadPatch p;
        p.name = name;
        p.group = group;
        p.kind = LeadPatch::POLY;
        p.poly.osc = osc;
        p.poly.notes = 1;
        p.poly.attack = attack; p.poly.decay = decay; p.poly.sustain = sustain; p.poly.release = release;
        p.poly.cutoff = cutoff; p.poly.envAmt = envAmt; p.poly.envDecay = envDecay; p.poly.res = res;
        p.poly.detune = detune; p.poly.gain = 1.f;
        p.poly.unison = osc == ChordOsc::Saw ? 3 : 1;
        p.poly.spread = 0.6f;
        p.poly.chorus = (osc == ChordOsc::EPiano || osc == ChordOsc::Organ) ? 0.6f : 0.2f;
        p.gain = gain; p.verb = verb; p.delay = delay;
        v.push_back(p);
    };
    //    name               group     osc               att    dec   sus   rel   cut    env    envD  res   det   gain  verb  delay
    poly("Supersaw lead",   "Leads",  ChordOsc::Saw,     0.005f, 1.5f, 0.75f, 5.f, 3200,  3500,  5,    1.2f, 0.8f, 0.55f, 0.25f, 0.2f);
    v.back().poly.unison = 7; v.back().poly.spread = 1.f; v.back().poly.detune = 1.1f;
    poly("Square lead",     "Leads",  ChordOsc::Square,  0.005f, 2.f,  0.7f,  6.f, 2400,  2500,  6,    1.4f, 0.f,  0.45f, 0.2f,  0.2f);
    poly("Brass",           "Leads",  ChordOsc::Saw,     0.04f,  1.2f, 0.8f,  5.f, 900,   2600,  4,    1.0f, 0.4f, 0.55f, 0.2f,  0.05f);
    poly("Pluck",           "Leads",  ChordOsc::Saw,     0.002f, 6.f,  0.f,   8.f, 450,   4200,  14,   1.3f, 0.3f, 0.6f,  0.25f, 0.25f);
    poly("Electric piano",  "Keys",   ChordOsc::EPiano,  0.002f, 1.0f, 0.f,   4.f, 6000,  0,     8,    0.9f, 0.f,  0.7f,  0.25f, 0.05f);
    poly("Organ",           "Keys",   ChordOsc::Organ,   0.003f, 0.f,  1.f,   12.f, 5000, 0,     8,    0.9f, 0.f,  0.45f, 0.2f,  0.05f);
    poly("Bells",           "Keys",   ChordOsc::Bell,    0.002f, 2.f,  0.f,   3.f, 8000,  0,     8,    0.9f, 0.f,  0.55f, 0.35f, 0.2f);
    poly("Marimba",         "Keys",   ChordOsc::Marimba, 0.001f, 7.f,  0.f,   8.f, 8000,  0,     8,    0.9f, 0.f,  0.7f,  0.2f,  0.1f);
    poly("Strings",         "Pads",   ChordOsc::Saw,     0.15f,  0.5f, 0.85f, 2.5f, 2600, 0,     8,    0.9f, 0.5f, 0.45f, 0.45f, 0.05f);
    poly("Warm pad",        "Pads",   ChordOsc::Saw,     0.5f,   0.f,  1.f,   1.5f, 1400, 0,     8,    0.9f, 0.7f, 0.45f, 0.55f, 0.15f);

    auto mono = [&](const char* name, MonoDef m, int transpose, float gain, float verb, float delay) {
        LeadPatch p;
        p.name = name;
        p.group = "Mono";
        p.kind = LeadPatch::MONO;
        p.mono = m;
        p.transpose = transpose;
        p.gain = gain; p.verb = verb; p.delay = delay;
        v.push_back(p);
    };
    {
        MonoDef m; m.saw = 1; m.sub = 0; m.cutoff = 220; m.envAmt = 3000; m.envDecay = 6; m.res = 6.f; m.release = 25;
        m.glide = 0.06f; m.drive = 1.8f; m.oscOctave = 0;
        m.filter = 1; m.width = 0.f;
        mono("Acid lead", m, 0, 0.8f, 0.15f, 0.25f);
    }
    {
        MonoDef m; m.saw = 1; m.square = 0.5f; m.detune = 0.6f; m.sub = 0.f; m.cutoff = 1800; m.envAmt = 2500; m.envDecay = 4;
        m.res = 1.5f; m.release = 8; m.glide = 0.08f; m.drive = 1.5f; m.oscOctave = 0;
        mono("Mono synth lead", m, 0, 0.6f, 0.2f, 0.2f);
    }
    {
        MonoDef m; m.saw = 1; m.sub = 0.6f; m.detune = 0.48f; m.cutoff = 260; m.envAmt = 2400; m.envDecay = 4; m.res = 1.6f;
        m.release = 9; m.drive = 2.2f; m.oscOctave = 0;
        m.filter = 1; m.width = 0.6f;
        mono("Reese bass", m, 0, 0.9f, 0.05f, 0.f);
    }
    {
        MonoDef m; m.saw = 0; m.sine = 1; m.sub = 0; m.oscOctave = 0; m.cutoff = 8000; m.envAmt = 0; m.decay = 0.9f;
        m.release = 4; m.glide = 0.09f; m.drop = 7; m.dropRate = 28; m.drive = 2.8f;
        mono("808 bass", m, 0, 1.0f, 0.f, 0.f);
    }
    {
        MonoDef m; m.saw = 0.8f; m.square = 0.6f; m.sub = 0.7f; m.detune = 0.3f; m.cutoff = 160; m.envAmt = 0;
        m.lfoDepth = 2200; m.lfoRate = 2.f; m.res = 3.f; m.drive = 3.f; m.release = 10; m.oscOctave = 0;
        m.filter = 1; m.width = 0.5f;
        mono("Wobble bass", m, 0, 0.8f, 0.05f, 0.f);
    }
    {
        LeadPatch p; p.name = "Style bass"; p.group = "This style"; p.kind = LeadPatch::STYLE_BASS; p.gain = 0.9f;
        p.verb = 0.05f; p.delay = 0.f; v.push_back(p);
        LeadPatch c; c.name = "Style chord sound"; c.group = "This style"; c.kind = LeadPatch::STYLE_CHORDS; c.gain = 0.8f;
        v.push_back(c);
    }
    return v;
}
}  // namespace

const std::vector<LeadPatch>& leadPatches() {
    static const std::vector<LeadPatch> v = buildLeads();
    return v;
}

}  // namespace dnb
