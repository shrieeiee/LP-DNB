#include "launchpad.h"

#include <algorithm>
#include <cstdio>

#include "RtMidi.h"
#include "common.h"
#include "log.h"

namespace dnb {

namespace {

// RtMidi reports problems through this instead of throwing or printing.
// `user` points at an atomic<bool> that gets set on real errors (or is null).
void onRtMidiError(RtMidiError::Type type, const std::string& text, void* user) {
    if (type == RtMidiError::WARNING || type == RtMidiError::DEBUG_WARNING) return;
    if (user) static_cast<std::atomic<bool>*>(user)->store(true);
    static std::string last;
    if (text != last) {
        last = text;
        logf("MIDI: %s", text.c_str());
    }
}

int indexOf(const std::vector<std::string>& v, const std::string& s) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) return static_cast<int>(i);
    return -1;
}

// Port names, with duplicates made unique ("Name", "Name (2)") so that two
// identical devices can be told apart. Names stay stable when other devices
// come and go (RtMidi's own index suffix is turned off in the build).
std::vector<std::string> portNames(RtMidi& m) {
    std::vector<std::string> out;
    for (unsigned i = 0, n = m.getPortCount(); i < n; ++i) {
        std::string name = m.getPortName(i);
        int dup = 1;
        for (const auto& o : out)
            if (o == name || o.rfind(name + " (", 0) == 0) ++dup;
        if (dup > 1) name += " (" + std::to_string(dup) + ")";
        out.push_back(name);
    }
    return out;
}

int findContaining(const std::vector<std::string>& v, const char* needle) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].find(needle) != std::string::npos) return static_cast<int>(i);
    return -1;
}

}  // namespace

struct MidiHub::KbPort {
    MidiHub* hub = nullptr;
    std::atomic<int> route{ROUTE_LEAD};
    std::unique_ptr<RtMidiIn> in;
    std::atomic<double> lastActivity{-10.0};
    std::atomic<bool> error{false};
};

MidiHub::MidiHub() {
    try {
        scanIn_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "lp-dnb scan");
        scanOut_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "lp-dnb scan");
        scanIn_->setErrorCallback(&onRtMidiError, nullptr);
        scanOut_->setErrorCallback(&onRtMidiError, nullptr);
    } catch (const RtMidiError& e) {
        logf("MIDI unavailable: %s", e.getMessage().c_str());
    }
}

MidiHub::~MidiHub() {
    closeLaunchpad(nullptr);
    for (auto& kv : kbs_) {
        if (kv.second->in) {
            kv.second->in->cancelCallback();
            kv.second->in->closePort();
        }
    }
}

void MidiHub::listPorts() {
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

bool MidiHub::isLaunchpadName(const std::string& n) const {
    if (!wantIn_.empty()) return n == wantIn_;
    return n.find("Launchpad") != std::string::npos;
}

void MidiHub::setLaunchpadPorts(const std::string& in, const std::string& out) {
    if (in == wantIn_ && out == wantOut_) return;
    wantIn_ = in;
    wantOut_ = out;
    closeLaunchpad("switching ports");
    failedOnce_.clear();
    forceRescan();
}

// ------------------------------------------------------------- scanning --

bool MidiHub::poll(double now) {
    const bool wasConnected = launchpadConnected();
    if (now >= nextScan_) {
        nextScan_ = now + 1.0;
        scan(now);
    }
    return wasConnected && !launchpadConnected();
}

void MidiHub::scan(double now) {
    (void)now;
    if (!scanIn_ || !scanOut_) return;

    const std::vector<std::string> ins = portNames(*scanIn_), outs = portNames(*scanOut_);
    const bool changed = ins != inNames_ || outs != outNames_;
    inNames_ = ins;
    outNames_ = outs;

    // --- Launchpad: drop it if unplugged or erroring, (re)connect when present.
    if (launchpadConnected()) {
        if (indexOf(ins, lpInName_) < 0 || indexOf(outs, lpOutName_) < 0) closeLaunchpad("unplugged");
        else if (lpError_.load()) closeLaunchpad("stopped responding");
    }
    if (!launchpadConnected()) {
        int i = wantIn_.empty() ? findContaining(ins, "Launchpad") : indexOf(ins, wantIn_);
        int o = wantOut_.empty() ? findContaining(outs, "Launchpad") : indexOf(outs, wantOut_);
        if (i >= 0 && o >= 0) openLaunchpad(ins[i], outs[o]);
    }

    // --- Other inputs: open new ones, close vanished/disabled ones.
    for (auto it = kbs_.begin(); it != kbs_.end();) {
        const bool present = indexOf(ins, it->first) >= 0;
        const bool wanted = present && !kbDisabled_.count(it->first) && !isLaunchpadName(it->first) &&
                            !it->second->error.load();
        if (!wanted) {
            if (it->second->in) {
                it->second->in->cancelCallback();
                it->second->in->closePort();
            }
            if (!present) logf("MIDI input unplugged: %s", it->first.c_str());
            it = kbs_.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t i = 0; i < ins.size(); ++i) {
        const std::string& name = ins[i];
        if (isLaunchpadName(name) || kbDisabled_.count(name) || kbs_.count(name)) continue;
        if (failedOnce_.count(name) && !changed) continue;  // don't hammer a port another app holds
        auto kb = std::make_unique<KbPort>();
        kb->hub = this;
        try {
            kb->in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "lp-dnb keys", 512);
            kb->in->setErrorCallback(&onRtMidiError, &kb->error);
            kb->in->openPort(static_cast<unsigned>(i), "lp-dnb keys");
        } catch (const RtMidiError&) {
        }
        if (!kb->in || !kb->in->isPortOpen() || kb->error.load()) {
            if (!failedOnce_.count(name)) logf("Couldn't open MIDI input \"%s\" (in use by another app?)", name.c_str());
            failedOnce_.insert(name);
            continue;
        }
        failedOnce_.erase(name);
        kb->in->ignoreTypes(true, true, true);
        kb->route = keyboardRoute(name);
        kb->in->setCallback(&MidiHub::onKeyboardMsg, kb.get());
        static const char* what[3] = {"the lead synth", "the bass", "the drums"};
        logf("MIDI input connected: %s (plays %s; change it in Devices)", name.c_str(), what[keyboardRoute(name) % 3]);
        kbs_[name] = std::move(kb);
    }
}

void MidiHub::openLaunchpad(const std::string& inName, const std::string& outName) {
    const std::string key = inName + "|" + outName;
    try {
        lpError_ = false;
        lpIn_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "lp-dnb", 1024);
        lpOut_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "lp-dnb");
        lpIn_->setErrorCallback(&onRtMidiError, &lpError_);
        lpOut_->setErrorCallback(&onRtMidiError, &lpError_);
        // Port indexes can shift when devices come and go, so look them up fresh.
        const int i = indexOf(portNames(*lpIn_), inName);
        const int o = indexOf(portNames(*lpOut_), outName);
        if (i >= 0) lpIn_->openPort(static_cast<unsigned>(i));
        if (o >= 0) lpOut_->openPort(static_cast<unsigned>(o));
        if (i < 0 || o < 0 || !lpIn_->isPortOpen() || !lpOut_->isPortOpen() || lpError_.load()) throw 0;
    } catch (...) {
        lpIn_.reset();
        lpOut_.reset();
        lpError_ = false;
        if (!failedOnce_.count(key))
            logf("Found \"%s\" but couldn't open it. Close Ableton / Novation Components and it will connect.",
                 inName.c_str());
        failedOnce_.insert(key);
        return;
    }
    failedOnce_.erase(key);
    lpIn_->ignoreTypes(true, true, true);
    lpIn_->setCallback(&MidiHub::onLaunchpadMsg, this);
    lpInName_ = inName;
    lpOutName_ = outName;

    send(0xB0, 0x00, 0x00);  // reset
    send(0xB0, 0x00, 0x01);  // X-Y layout
    updateBuf_ = 0;
    send(0xB0, 0x00, 0x20 | (0 << 2) | 1);  // update buffer 0, display buffer 1
    haveShown_ = false;
    logf("Launchpad connected: %s", inName.c_str());
}

void MidiHub::closeLaunchpad(const char* why) {
    if (!lpIn_ && !lpOut_) return;
    const bool gone = why && std::string(why) == "unplugged";
    if (lpOut_ && !lpError_.load() && !gone) send(0xB0, 0x00, 0x00);  // LEDs off, single buffering again
    if (lpIn_) {
        lpIn_->cancelCallback();
        lpIn_->closePort();
    }
    if (lpOut_) lpOut_->closePort();
    lpIn_.reset();
    lpOut_.reset();
    if (why) logf("Launchpad disconnected (%s). Plug it back in and it will reconnect.", why);
    lpInName_.clear();
    lpOutName_.clear();
    lpError_ = false;
}

// ------------------------------------------------------------- messages --

void MidiHub::onLaunchpadMsg(double, std::vector<unsigned char>* msg, void* user) {
    auto* self = static_cast<MidiHub*>(user);
    self->lpActivity_.store(monotonicSec());
    std::lock_guard<std::mutex> lk(self->mu_);
    self->queue_.push_back({MidiMsg::LAUNCHPAD, ROUTE_LEAD, *msg});
}

void MidiHub::onKeyboardMsg(double, std::vector<unsigned char>* msg, void* user) {
    auto* kb = static_cast<KbPort*>(user);
    kb->lastActivity.store(monotonicSec());
    std::lock_guard<std::mutex> lk(kb->hub->mu_);
    kb->hub->queue_.push_back({MidiMsg::KEYBOARD, kb->route.load(), *msg});
}

std::vector<MidiMsg> MidiHub::takeMessages() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<MidiMsg> out;
    out.swap(queue_);
    return out;
}

std::vector<MidiHub::KeyboardInfo> MidiHub::keyboards() const {
    std::vector<KeyboardInfo> out;
    for (const std::string& n : inNames_) {
        if (isLaunchpadName(n)) continue;
        auto it = kbs_.find(n);
        out.push_back({n, kbDisabled_.count(n) == 0, it != kbs_.end(),
                       it != kbs_.end() ? it->second->lastActivity.load() : -10.0, keyboardRoute(n)});
    }
    return out;
}

int MidiHub::keyboardRoute(const std::string& name) const {
    auto it = kbRoute_.find(name);
    return it == kbRoute_.end() ? ROUTE_LEAD : it->second;
}

void MidiHub::setKeyboardRoute(const std::string& name, int route) {
    kbRoute_[name] = route;
    auto it = kbs_.find(name);
    if (it != kbs_.end()) it->second->route = route;
}

void MidiHub::restore(const std::map<std::string, int>& routes, const std::set<std::string>& disabled) {
    kbRoute_ = routes;
    kbDisabled_ = disabled;
    for (auto& kv : kbs_) kv.second->route = keyboardRoute(kv.first);
    forceRescan();
}

void MidiHub::setKeyboardEnabled(const std::string& name, bool on) {
    if (on) {
        kbDisabled_.erase(name);
        failedOnce_.erase(name);
    } else {
        kbDisabled_.insert(name);
    }
    forceRescan();
}

// ------------------------------------------------------------------ LEDs --

void MidiHub::send(unsigned char a, unsigned char b, unsigned char c) {
    if (!lpOut_) return;
    unsigned char m[3] = {a, b, c};
    lpOut_->sendMessage(m, 3);
}

void MidiHub::pushFrame(const Frame& f) {
    if (!launchpadConnected()) return;
    if (haveShown_ && f == shown_) return;
    // Flags = 0 while double buffering: write only the hidden (update) buffer.
    auto vel = [](uint8_t v) -> unsigned char {
        unsigned r = v & 3, g = (v >> 4) & 3;
        return static_cast<unsigned char>(g * 16 + r);
    };
    for (int i = 0; i < 80; i += 2) send(0x92, vel(f[i]), vel(f[i + 1]));
    // Show what we just wrote; start updating the other buffer. This CC also
    // resets the rapid-update pointer for the next frame.
    const int shownBuf = updateBuf_;
    updateBuf_ ^= 1;
    send(0xB0, 0x00, static_cast<unsigned char>(0x20 | (updateBuf_ << 2) | shownBuf));
    shown_ = f;
    haveShown_ = true;
}

}  // namespace dnb
