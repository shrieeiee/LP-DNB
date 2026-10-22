// lp-tenori: a Tenori-on style sequencer for the Launchpad Mini, playing the
// LP-DNB sound engine. Eight layers, each with its own sound (the style's drum
// kit, bass, chords or the lead synth), its own way of playing (Score, Bounce,
// Path or Push), octave, loop length and volume. Notes send ripples of light
// across the grid, Tenori-on style.
//
// Runs its own clock and sends the engine Cmd::Note commands up to 60 ms ahead,
// each stamped with the exact moment it should sound. The engine holds them back
// until then, so timing is sample-accurate however irregularly the main loop
// runs. Everything happens on the main thread (the engine's command queue has a
// single producer).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "common.h"
#include "launchpad.h"

namespace dnb {

struct Style;

constexpr int kLayers = 8;
constexpr int kTSteps = 16;    // longest Score loop (two pages of eight)
constexpr int kPathMax = 16;   // points in a Path

enum TSound : int { SND_DRUMS = 0, SND_BASS, SND_CHORDS, SND_LEAD, SND_COUNT };
enum TMode : int { MODE_SCORE = 0, MODE_BOUNCE, MODE_PATH, MODE_PUSH, MODE_COUNT };

const char* soundName(int s);
const char* modeName(int m);

struct Layer {
    // --- settings (saved)
    int sound = SND_LEAD;
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

    void setPlaying(bool on, double now);
    bool playing() const { return playingFlag_; }

    // Settings.
    void setStyle(int style);
    int style() const;
    void setLeadPatch(int patch);
    int leadPatch() const { return leadPatch_; }

    // For the demo and tests: direct access to the layers.
    Layer& layer(int i) { return layers_[static_cast<size_t>(i % kLayers)]; }
    void selectLayer(int i) { cur_ = i % kLayers; }

    // One line describing what's selected, for the console.
    std::string status() const;

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

    // sequencing (hold mu_)
    void fireStep(uint64_t g, double t, double dur);
    void play(int layer, int x, int y, int idx, double t, float vel, float gate);
    Event voice(const Layer& L, int idx, float vel, float gate) const;
    double stepDur(uint64_t g) const;
    double baseStep() const;
    void preview(int layer, int x, int y, int idx, double now);
    void send(const Event& e, double t);
    void noteLights(int layer, int x, int y, double t);
    int visiblePage(const Layer& L, double now) const;
    int64_t stepAt(double now) const;  // global step sounding at `now` (-1 = none yet)
    void clearLayer(int i);
    void undo();

    Shared& sh_;

    std::array<Layer, kLayers> layers_;
    std::array<Layer, kLayers> undo_;
    bool haveUndo_ = false;
    int cur_ = 0;
    int leadPatch_ = 0;
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

    // Push: pads held on Push layers. rate = steps between hits.
    struct Held { int layer, x, y; double skipUntil; };
    std::vector<Held> held_;

    // buttons
    bool shift_ = false, fxBtn_ = false, shiftUsed_ = false;
    int padFx_[16];
    int bpmHeld_ = 0;
    double bpmNext_ = 0.0;

    // lights
    struct Ripple { int layer; float x, y; double t; };
    std::vector<Ripple> ripples_;
    double flash_[kLayers][kTSteps][8] = {};  // when each note last sounded [layer][step or column][row]
    double layerHit_[kLayers] = {};

    // saving
    std::string lastSaved_;
    double nextSave_ = 0.0;
    float pendingBpm_ = 0.f;  // saved tempo, applied once the saved style is in
    int pendingStyle_ = -1;
};

}  // namespace dnb
