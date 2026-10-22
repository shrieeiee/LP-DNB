// MIDI clock output: sends Start / Stop and 24 clock ticks per beat to another
// MIDI device (a drum machine, a DAW, a synth's arpeggiator) so it follows
// LP-DNB's tempo. Runs on its own thread, timed from the audio engine's beat
// position so it stays locked to what you hear.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class RtMidiOut;

namespace dnb {

struct Shared;

class MidiClockOut {
public:
    explicit MidiClockOut(Shared& sh);
    ~MidiClockOut();
    // "" = off. Returns false if the port couldn't be opened.
    bool open(const std::string& port);
    const std::string& port() const { return port_; }
    bool isOpen() const { return open_.load(); }

private:
    void run();
    Shared& sh_;
    std::mutex mu_;
    std::unique_ptr<RtMidiOut> out_;
    std::string port_;
    std::atomic<bool> open_{false}, quit_{false};
    std::thread th_;
};

}  // namespace dnb
