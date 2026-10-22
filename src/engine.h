// The drum & bass synth: drum voices, Reese bass, chord stab, FX sounds,
// a 16-step sequencer and the master effects chain. Runs on the audio thread.
#pragma once
#include <memory>

#include "common.h"

namespace dnb {

class Engine {
public:
    Engine(Shared& shared, float sampleRate);
    ~Engine();
    // Render `frames` stereo frames, interleaved L/R.
    void process(float* out, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dnb
