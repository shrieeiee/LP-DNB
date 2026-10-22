// Maps Launchpad buttons to the synth and draws the light show.
#pragma once
#include <vector>

#include "common.h"
#include "dsp.h"
#include "launchpad.h"

namespace dnb {

struct Color {
    float r = 0.f, g = 0.f;
};

class Controller {
public:
    explicit Controller(Shared& sh);
    void onMidi(const std::vector<unsigned char>& m, double now);
    void update(double now);  // auto-repeat for tempo buttons
    void render(double now, Frame& out);

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
    int selTrack_ = T_KICK;
    int lastBassIdx_ = 0;
    int heldStep_ = -1;
    bool viewHeld_ = false, comboUsed_ = false;
    bool bassPadHeld_[8] = {};

    bool fxHeld_[FX_COUNT] = {};
    double fxPress_[FX_COUNT] = {};
    double fxRelease_[FX_COUNT] = {};
    double riserHeldFor_ = 0.0;

    int bpmHeld_ = 0;  // -1 / +1 while a tempo button is held
    double bpmNextRepeat_ = 0.0;

    // light-show state
    uint32_t seenHits_[S_COUNT + 1] = {};
    double hitTime_[S_COUNT + 1] = {};
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
    Rng rng_;
};

}  // namespace dnb
