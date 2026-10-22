#include "launchpad.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>

#include "RtMidi.h"

namespace dnb {

namespace {
int findPort(RtMidi& m, const char* needle) {
    unsigned n = m.getPortCount();
    for (unsigned i = 0; i < n; ++i) {
        std::string name = m.getPortName(i);
        if (name.find(needle) != std::string::npos) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

Launchpad::Launchpad() = default;
Launchpad::~Launchpad() { close(); }

void Launchpad::listPorts() {
    try {
        RtMidiIn in;
        RtMidiOut out;
        std::printf("MIDI inputs:\n");
        for (unsigned i = 0; i < in.getPortCount(); ++i) std::printf("  [%u] %s\n", i, in.getPortName(i).c_str());
        std::printf("MIDI outputs:\n");
        for (unsigned i = 0; i < out.getPortCount(); ++i) std::printf("  [%u] %s\n", i, out.getPortName(i).c_str());
    } catch (const RtMidiError& e) {
        std::printf("MIDI error: %s\n", e.getMessage().c_str());
    }
}

bool Launchpad::open(int portIn, int portOut, std::string& err) {
    try {
        in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "lp-dnb", 1024);
        out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "lp-dnb");
        if (portIn < 0) portIn = findPort(*in_, "Launchpad");
        if (portOut < 0) portOut = findPort(*out_, "Launchpad");
        if (portIn < 0 || portOut < 0) {
            err = "No MIDI port with \"Launchpad\" in its name. Plug it in, or run with --list and pick ports with --in/--out.";
            in_.reset();
            out_.reset();
            return false;
        }
        in_->openPort(static_cast<unsigned>(portIn));
        out_->openPort(static_cast<unsigned>(portOut));
        in_->ignoreTypes(true, true, true);  // drop sysex, timing, active sensing
        in_->setCallback(&Launchpad::onMessage, this);
        std::printf("Launchpad in : %s\nLaunchpad out: %s\n", in_->getPortName(portIn).c_str(),
                    out_->getPortName(portOut).c_str());
    } catch (const RtMidiError& e) {
        err = e.getMessage();
        in_.reset();
        out_.reset();
        return false;
    }
    send(0xB0, 0x00, 0x00);  // reset
    send(0xB0, 0x00, 0x01);  // X-Y layout
    updateBuf_ = 0;
    send(0xB0, 0x00, 0x20 | (0 << 2) | 1);  // update buffer 0, display buffer 1
    haveShown_ = false;
    return true;
}

void Launchpad::close() {
    if (out_) {
        try {
            send(0xB0, 0x00, 0x00);  // reset: LEDs off, single buffering again
        } catch (...) {
        }
    }
    if (in_) {
        try {
            in_->cancelCallback();
            in_->closePort();
        } catch (...) {
        }
    }
    in_.reset();
    out_.reset();
}

void Launchpad::onMessage(double, std::vector<unsigned char>* msg, void* user) {
    auto* self = static_cast<Launchpad*>(user);
    {
        std::lock_guard<std::mutex> lk(self->mu_);
        self->queue_.push_back(*msg);
    }
    self->cv_.notify_one();
}

std::vector<std::vector<unsigned char>> Launchpad::waitMessages(int timeoutMs) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return !queue_.empty(); });
    std::vector<std::vector<unsigned char>> out(queue_.begin(), queue_.end());
    queue_.clear();
    return out;
}

void Launchpad::send(unsigned char a, unsigned char b, unsigned char c) {
    if (!out_) return;
    unsigned char m[3] = {a, b, c};
    out_->sendMessage(m, 3);
}

void Launchpad::pushFrame(const Frame& f) {
    if (!out_) return;
    if (haveShown_ && f == shown_) return;
    // Flags = 0 while double buffering: write only the hidden (update) buffer.
    auto vel = [](uint8_t v) -> unsigned char {
        unsigned r = v & 3, g = (v >> 4) & 3;
        return static_cast<unsigned char>(g * 16 + r);
    };
    for (int i = 0; i < 80; i += 2) send(0x92, vel(f[i]), vel(f[i + 1]));
    // Show what we just wrote; start updating the other buffer. This CC also
    // resets the rapid-update pointer for the next frame.
    int shownBuf = updateBuf_;
    updateBuf_ ^= 1;
    send(0xB0, 0x00, static_cast<unsigned char>(0x20 | (updateBuf_ << 2) | shownBuf));
    shown_ = f;
    haveShown_ = true;
}

void Launchpad::clear() {
    Frame z{};
    haveShown_ = false;
    pushFrame(z);
}

}  // namespace dnb
