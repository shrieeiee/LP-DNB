// lp-tenori: a Tenori-on style sequencer for the Launchpad Mini, playing the
// LP-DNB sound engine. Eight layers, each with its own instrument (the style's
// drum kit, bass or chords, or a synth of its own), its own way of playing
// (Score, Bounce, Path or Push), octave, loop length and volume. Notes send
// ripples of light across the grid, Tenori-on style.
//
// Runs its own clock and sends the engine Cmd::Note commands up to 60 ms ahead,
// each stamped with the exact moment it should sound. The engine holds them back
// until then, so timing is sample-accurate however irregularly the main loop
// runs. Everything happens on the main thread (the engine's command queue has a
// single producer): the Launchpad, the window and the sequencer.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "common.h"
#include "launchpad.h"

namespace dnb {

struct Style;

constexpr int kLayers = kSynths;  // one engine synth per layer
constexpr int kTSteps = 16;       // longest Score loop (two pages of eight)
constexpr int kPathMax = 16;      // points in a Path

// SND_SYNTH layers play their own synth (Layer::patch); the others play the style's sounds.
enum TSound : int { SND_DRUMS = 0, SND_BASS, SND_CHORDS, SND_SYNTH, SND_COUNT };
enum TMode : int { MODE_SCORE = 0, MODE_BOUNCE, MODE_PATH, MODE_PUSH, MODE_COUNT };

const char* soundName(int s);
const char* modeName(int m);
const char* modeHelp(int m);
// Synth sounds a layer can use (lead patch indices): all but "Style bass".
const std::vector<int>& synthPatches();
// Push: steps between repeats for each column, and a label for it.
double pushRate(int column);
const char* pushRateName(int column);

struct Layer {
    // --- settings (saved)
    int sound = SND_SYNTH;
    int patch = 0;             // SND_SYNTH: lead patch index
    int mode = MODE_SCORE;
    int octave = 0;            // -2..+2
    int length = kTSteps;      // Score loop length 1..16 steps
    int volume = 6;            // 0..7
    bool muted = false;
    uint16_t score[8] = {};    // Score: [row, 0 = top] one bit per step
    int8_t ball[8];            // Bounce: per column, the row the ball bounces up to (-1 = none)
    std::vector<std::pair<int, int>> path;  // Path: points (x, y) in the order they're visited

    // --- running state (not saved)
    int ballY[8];              // where each ball is
    int ballDir[8];            // +1 falling, -1 rising
    int pathFrom = 0;          // point the Path cursor left from
    int pathAt = 0;            // point it's heading for
    int pathWait = 0;          // steps until it gets there
    double legFrom = 0.0, legTo = 0.0;  // when the cursor left / arrives (for the lights)

    Layer();
    bool empty() const;
    void clear();
    void resetMotion();        // balls back to the top, Path back to its first point
};

class Tenori : public Surface {
public:
    explicit Tenori(Shared& sh);
    ~Tenori() override;

    // Queue every note due before `until` (monotonicSec, or a virtual clock offline).
    // update() calls it with now + 60 ms.
    void schedule(double until);

    // Surface (called from the App / main thread).
    void onLaunchpad(const std::vector<unsigned char>& m, double now) override;
    void render(double now, Frame& out) override;
    void update(double now) override;
    void releaseAll(double now) override;
    void onClock(bool start) override;

    // ---- Transport and global settings.
    void setPlaying(bool on, double now);
    bool playing() const { return playingFlag_; }
    void setStyle(int style);
    int style() const;
    bool swing() const { return swing_; }
    void setSwing(bool on) { swing_ = on; }
    bool ghosts() const { return ghosts_; }
    void setGhosts(bool on) { ghosts_ = on; }
    bool follow() const { return follow_; }
    void setFollow(bool on) { follow_ = on; }

    // ---- Layers. The Launchpad and the window both edit through these.
    Layer& layer(int i) { return layers_[static_cast<size_t>(i & 7)]; }
    const Layer& layer(int i) const { return layers_[static_cast<size_t>(i & 7)]; }
    int current() const { return cur_; }
    void selectLayer(int i) { cur_ = i & 7; }
    void setSound(int layer, int sound);
    void setPatch(int layer, int patch);    // also makes it a Synth layer
    void setMode(int layer, int mode);
    void toggleScore(int layer, int step, int row, double now);
    void tapBounce(int layer, int x, int y, double now);
    void tapPath(int layer, int x, int y, double now);
    void pushDown(int layer, int x, int y, double now);
    void pushUp(int layer, int x, int y);
    bool pushHeld(int layer, int x, int y) const;
    void clearLayer(int layer);
    void clearAll();
    void undo();                            // pressing it again redoes
    bool canUndo() const { return haveUndo_; }
    // Plays a layer's note right now (auditioning). idx = 0 lowest .. 7 highest.
    void audition(int layer, int idx, double now);

    // ---- For drawing.
    int64_t stepAt(double now) const;       // global step sounding at `now` (-1 = none)
    int scoreStep(int layer, double now) const;  // that layer's Score position (-1 = none)
    int visiblePage(int layer, double now) const;
    double lastHit(int layer, int x, int y) const { return flash_[layer & 7][x & 15][y & 7]; }
    double layerHit(int layer) const { return layerHit_[layer & 7]; }
    bool shiftHeld() const { return shift_; }
    bool fxHeld() const { return fxBtn_; }
    // What a layer plays ("Pluck", "Drums: Punchy kit"...) and each row's name.
    std::string instrumentName(int layer) const;
    std::string rowName(int layer, int idx) const;  // idx 0 = lowest

    // One line describing what's selected, for the console.
    std::string status() const;

    // Sends every layer's synth sound to the engine (after loading, or offline).
    void syncSynths();

    // Save file (next to the groovebox's): lp-tenori.txt.
    static std::string savePath();
    std::string serialize() const;
    bool load(const std::string& text);
    void loadFile();
    void saveIfChanged();

private:
    struct Event { int slot, pitch; float vel, gate; };

    // input
    void onGrid(int x, int y, bool down, double now);
    void onScene(int row, bool down, double now);
    void onTop(int i, bool down, double now);
    void onShiftGrid(int x, int y);
    void nudgeBpm(float d);

    // sequencing
    void fireStep(uint64_t g, double t, double dur);
    void play(int layer, int x, int y, int idx, double t, float vel, float gate);
    Event voice(int layer, int idx, float vel, float gate) const;
    double stepDur(uint64_t g) const;
    double baseStep() const;
    void preview(int layer, int x, int y, int idx, double now);
    void send(const Event& e, double t);
    void noteLights(int layer, int x, int y, double t);
    void dropHeld(int layer);

    Shared& sh_;

    std::array<Layer, kLayers> layers_;
    std::array<Layer, kLayers> undo_;
    bool haveUndo_ = false;
    int cur_ = 0;
    bool swing_ = true;
    bool ghosts_ = false;     // show the other layers' Score notes faintly
    bool follow_ = true;      // Score page follows the playhead
    int page_ = 0;

    // transport
    bool playingFlag_ = false;
    uint64_t g_ = 0;          // next step to fire
    double nextT_ = 0.0;      // when it fires
    std::array<double, 64> stepT_{};  // when recent steps fire, by g % 64
    uint64_t firedUpTo_ = 0;  // steps fired so far

    // Push: pads held on Push layers.
    struct Held { int layer, x, y; double skipUntil; };
    std::vector<Held> held_;

    // buttons
    bool shift_ = false, fxBtn_ = false;
    int padFx_[16];
    int bpmHeld_ = 0;
    double bpmNext_ = 0.0;

    // lights
    struct Ripple { int layer; float x, y; double t; };
    std::vector<Ripple> ripples_;
    double flash_[kLayers][kTSteps][8] = {};  // when each note last sounded [layer][step or column][row]
    double layerHit_[kLayers] = {};
    double rippleT_[kLayers][8][8] = {};      // when each pad last sent a ripple

    // saving
    std::string lastSaved_;
    double nextSave_ = 0.0;
    float pendingBpm_ = 0.f;  // saved tempo, applied once the saved style is in
    int pendingStyle_ = -1;
};

}  // namespace dnb
