// Maps Launchpad buttons to the synth (remapped per style) and draws the light show.
#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "dsp.h"
#include "launchpad.h"

namespace dnb {

struct Color {
    float r = 0.f, g = 0.f;
};

struct Style;

class Controller {
public:
    explicit Controller(Shared& sh);
    void onMidi(const std::vector<unsigned char>& m, double now);
    void update(double now);  // auto-repeat for tempo buttons
    void render(double now, Frame& out);

    // Lets go of every held pad (used when a device is unplugged mid-press).
    void releaseAll(double now);

    bool editView() const { return view_ == EDIT; }
    bool stylePickerShown() const { return viewHeld_; }
    int pickerPage() const { return pickerPage_; }
    int editBar() const { return editBar_; }
    int editBarPresses() const { return editBarPresses_; }  // bumps when BPM-/+ change the bar
    void setEditBar(int b) { editBar_ = b < 0 ? 0 : (b > 3 ? 3 : b); }
    int selectedTrack() const { return selTrack_; }
    void selectTrack(int t) { selTrack_ = t; heldStep_ = -1; }
    bool fxHeld(int fx) const { return fx >= 0 && fx < FX_COUNT && fxHeld_[fx]; }
    int lastBassIndex() const { return lastBassIdx_; }

    int currentStyle() const;
    void chooseStyle(int style);  // queues the style's first pattern
    void choosePattern(int slot);  // within the current style

private:
    enum View { PERFORM, EDIT };

    void onGrid(int x, int y, bool down, double now);
    void onScene(int row, bool down);
    void onTop(int i, bool down, double now);
    void nudgeBpm(float d);

    void buildCanvas(double now, float dt, Color canvas[8][4]);
    void drawFx(int fx, double t, double now, float dt, Color canvas[8][4]);
    void spawnRipple(float x, float y, Color c, double now, float speed = 9.f);
    double beatSec() const;

    Shared& sh_;
    View view_ = PERFORM;
    int selTrack_ = 0;
    int lastBassIdx_ = 0;  // scale degree of the last bass pad played
    int heldStep_ = -1;
    bool viewHeld_ = false, comboUsed_ = false;
    int bassPadSemi_[8];   // semitone sent for each held bass pad (-1000 = not held)
    bool chordPadHeld_ = false;

    bool fxHeld_[FX_COUNT] = {};
    int padFx_[16];        // effect id held on each effect pad (-1 = none)
    double fxPress_[FX_COUNT] = {};
    double fxRelease_[FX_COUNT] = {};
    double riserHeldFor_ = 0.0;

    int bpmHeld_ = 0;  // -1 / +1 while a tempo button is held
    double bpmNextRepeat_ = 0.0;

    // light-show state
    uint32_t seenHits_[kHitSlots] = {};
    double hitTime_[kHitSlots] = {};
    float env_[kTracks] = {};
    struct Ripple {
        float x, y;
        Color c;
        double t0;
        float speed;
    };
    std::vector<Ripple> ripples_;
    Color glow_[8][4] = {};
    float tapeScroll_ = 0.f;
    int crushSeed_ = -1;
    Color crushCells_[8][4] = {};
    double lastFrame_ = 0.0;
    int lastStyle_ = -1;
    int pickerPage_ = 0;  // which 16 patterns the View picker shows
    int editBar_ = 0;     // EDIT view: which bar of a long pattern is on the top rows
    int editBarPresses_ = 0;
    double styleChangedAt_ = -10.0;
    Rng rng_;
};

// Shared helpers for the GUI so it matches the Launchpad.
Color slotColor(const Style& s, int slot);  // drum pad / track colour (LED scale 0..3)
Color fxColor(int fx);
std::string trackName(const Style& s, int track);

}  // namespace dnb
