#include "styles.h"

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
    for (size_t i = 0; i < rows.size(); ++i) p.rows[i] = rows[i];
    return p;
}

const std::array<int, 7> MINOR{0, 2, 3, 5, 7, 8, 10};
const std::array<int, 7> DORIAN{0, 2, 3, 5, 7, 9, 10};
const std::array<int, 7> PHRYGIAN{0, 1, 3, 5, 7, 8, 10};

// ---------------------------------------------------------------- styles --

std::vector<Style> build() {
    std::vector<Style> v;

    {   // ------------------------------------------------ Drum & Bass
        Style s;
        s.name = "Drum & Bass";
        s.padLabel = "DnB";
        s.trim = 0.93f;
        s.blurb = "174 BPM rollers: Reese bass, two-step breaks, rave stabs";
        s.bpm = 174; s.swing = 0.5f; s.root = 29; s.scale = MINOR; s.keyName = "F minor"; s.duck = 0.55f;
        s.drums = {kick("Kick", "Kick", 236, 46, 32, 5.5f, 0.35f, 0.24f),
                   snare("Snare", "Snare", 190, 28, 15, 1400, 0.45f, 12000),
                   snare("Ghost snare", "Ghost", 200, 50, 30, 1600, 0.4f, 9000, 0.32f, 0.f),
                   closedHat(), openHat(), ride(), crash()};
        s.bass.name = "Reese bass";
        s.bass.saw = 1; s.bass.sub = 0.6f; s.bass.detune = 0.48f; s.bass.cutoff = 260; s.bass.envAmt = 2400;
        s.bass.envDecay = 4; s.bass.res = 1.6f; s.bass.release = 9; s.bass.drive = 2.2f;
        s.chord.name = "Rave stab"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 5; s.chord.decay = 3.5f;
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
        s.chord.name = "Organ stab"; s.chord.osc = ChordOsc::Organ; s.chord.notes = 4; s.chord.decay = 6.f;
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
        s.bass.saw = 1; s.bass.sub = 0.f; s.bass.cutoff = 180; s.bass.envAmt = 2600; s.bass.envDecay = 7;
        s.bass.res = 7.f; s.bass.release = 30; s.bass.glide = 0.06f; s.bass.drive = 1.8f; s.bass.accent = 1.9f;
        s.bass.gain = 0.8f;
        s.chord.name = "Dub chord"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.decay = 7;
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
        s.chord.name = "Rhodes"; s.chord.osc = ChordOsc::EPiano; s.chord.notes = 4; s.chord.decay = 1.2f;
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
        s.chord.name = "Bells"; s.chord.osc = ChordOsc::Bell; s.chord.notes = 3; s.chord.octave = 36;
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
        s.bass.saw = 0.8f; s.bass.square = 0.6f; s.bass.sub = 0.7f; s.bass.detune = 0.3f; s.bass.cutoff = 160;
        s.bass.envAmt = 0; s.bass.lfoDepth = 2200; s.bass.lfoRate = 2.f; s.bass.res = 3.f; s.bass.drive = 3.f;
        s.bass.release = 10;
        s.chord.name = "Dark pad"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.attack = 0.25f;
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
        s.chord.name = "M1 organ"; s.chord.osc = ChordOsc::Organ; s.chord.notes = 4; s.chord.decay = 9;
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
        s.chord.name = "Pluck"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.decay = 6;
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
        s.bass.saw = 0.9f; s.bass.square = 0.3f; s.bass.sub = 0.3f; s.bass.cutoff = 400; s.bass.envAmt = 3500;
        s.bass.envDecay = 12; s.bass.res = 2.5f; s.bass.decay = 3.5f; s.bass.release = 14; s.bass.drive = 1.8f;
        s.chord.name = "Strings"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 4; s.chord.attack = 0.12f;
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
        s.bass.saw = 1; s.bass.detune = 0.3f; s.bass.sub = 0.5f; s.bass.cutoff = 300; s.bass.envAmt = 3000;
        s.bass.envDecay = 9; s.bass.res = 2; s.bass.decay = 5; s.bass.release = 14; s.bass.drive = 1.6f;
        s.chord.name = "Pad"; s.chord.osc = ChordOsc::Saw; s.chord.notes = 3; s.chord.attack = 0.4f;
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
        s.chord.name = "Electric piano"; s.chord.osc = ChordOsc::EPiano; s.chord.notes = 5; s.chord.decay = 1.0f;
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
        s.chord.name = "Marimba"; s.chord.osc = ChordOsc::Marimba; s.chord.notes = 3; s.chord.octave = 36;
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

    // Sanity-check the pattern text so a typo can't break the sequencer.
    for (auto& s : v)
        for (auto& p : s.patterns)
            for (size_t t = 0; t < p.rows.size(); ++t)
                if (p.rows[t].size() != kSteps) {
                    logf("Pattern \"%s / %s\" row %zu has %zu steps (fixed to 16)", s.name.c_str(), p.name.c_str(), t,
                         p.rows[t].size());
                    p.rows[t].resize(kSteps, '.');
                }
    return v;
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

void loadPreset(Shared& sh, int pattern) {
    const Style& st = styleOf(pattern);
    const PatternDef& pd = st.patterns[static_cast<size_t>(pattern % kSlots)];
    for (int t = 0; t < kTracks; ++t) {
        const std::string& row = pd.rows[static_cast<size_t>(t)];
        for (int s = 0; s < kSteps; ++s) {
            const char c = s < static_cast<int>(row.size()) ? row[static_cast<size_t>(s)] : '.';
            uint8_t code = STEP_OFF;
            int8_t note = 0;
            if (t >= T_BASS) {
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
            sh.on[pattern][t][s].store(code);
            sh.note[pattern][t][s].store(note);
        }
    }
}

void loadAllPresets(Shared& sh) {
    for (int p = 0; p < styleCount() * kSlots && p < kMaxPatterns; ++p) loadPreset(sh, p);
}

}  // namespace dnb
