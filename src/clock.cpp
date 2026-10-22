#include "clock.h"

#include <chrono>
#include <cmath>

#include "RtMidi.h"
#include "common.h"
#include "log.h"

namespace dnb {

MidiClockOut::MidiClockOut(Shared& sh) : sh_(sh) { th_ = std::thread([this] { run(); }); }

MidiClockOut::~MidiClockOut() {
    quit_ = true;
    if (th_.joinable()) th_.join();
    open("");
}

bool MidiClockOut::open(const std::string& port) {
    std::lock_guard<std::mutex> lk(mu_);
    if (out_) {
        if (open_.load()) {
            const unsigned char stop = 0xFC;
            try { out_->sendMessage(&stop, 1); } catch (...) {}
        }
        try { out_->closePort(); } catch (...) {}
        out_.reset();
    }
    open_ = false;
    port_ = port;
    if (port.empty()) return true;
    try {
        out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "lp-dnb clock");
        for (unsigned i = 0; i < out_->getPortCount(); ++i)
            if (out_->getPortName(i) == port) {
                out_->openPort(i, "lp-dnb clock");
                break;
            }
    } catch (const RtMidiError&) {
    }
    if (!out_ || !out_->isPortOpen()) {
        out_.reset();
        logf("Couldn't open \"%s\" for MIDI clock", port.c_str());
        return false;
    }
    open_ = true;
    logf("Sending MIDI clock to %s", port.c_str());
    return true;
}

void MidiClockOut::run() {
    bool wasPlaying = false;
    long long sent = 0;
    while (!quit_.load()) {
        // Idle: check rarely. Sending: every millisecond (sleeping less than that on
        // Windows becomes a busy loop that eats a whole CPU core).
        std::this_thread::sleep_for(std::chrono::milliseconds(open_.load() ? 1 : 50));
        std::lock_guard<std::mutex> lk(mu_);
        if (!out_ || !open_.load()) { wasPlaying = false; continue; }
        auto send1 = [&](unsigned char b) {
            try { out_->sendMessage(&b, 1); } catch (...) {}
        };
        const bool playing = sh_.playing.load();
        if (playing && !wasPlaying) { send1(0xFA); sent = 0; }  // Start
        if (!playing && wasPlaying) send1(0xFC);                   // Stop
        wasPlaying = playing;
        if (!playing) continue;
        const double now = monotonicSec();
        const double beats = sh_.clockBeats.load() + (now - sh_.clockTime.load()) * sh_.bpm.load() / 60.0;
        if (beats < 0.0) continue;
        const long long due = static_cast<long long>(std::floor(beats * 24.0)) + 1;
        if (due - sent > 48) sent = due - 1;  // we stalled: don't machine-gun ticks
        while (sent < due) { send1(0xF8); ++sent; }
    }
}

}  // namespace dnb
