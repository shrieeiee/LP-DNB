// MIDI device handling: the Launchpad Mini (mk1 / mk2, the micro-USB ones)
// plus any other MIDI inputs (keyboards, pads), with hot-plug detection.
//
// Launchpad protocol notes (Launchpad S/Mini programmer's reference):
//  * Grid pad (x,y) is note 16*y + x, y=0 at the top. Column 8 is the round
//    "scene" buttons on the right. Pads send note-on, velocity 127/0.
//  * The 8 round buttons along the top are CC 104..111.
//  * LED colour is a velocity: 16*green + red (each 0..3) + flags.
//  * "Rapid update" = note-on on MIDI channel 3; each message sets two LEDs,
//    walking grid (row-major), then scene buttons, then top buttons: 80 LEDs.
//  * Double buffering via CC 0: 0x20 | (update<<2) | display, which stops
//    tearing while we stream whole frames.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

namespace dnb {

// One LED: low nibble = red 0..3, high nibble = green 0..3.
using Frame = std::array<uint8_t, 80>;

// What another MIDI input plays.
enum KbRoute : int { ROUTE_LEAD = 0, ROUTE_BASS = 1, ROUTE_DRUMS = 2 };

struct MidiMsg {
    enum Source { LAUNCHPAD, KEYBOARD } source = LAUNCHPAD;
    int route = ROUTE_LEAD;  // for keyboards
    std::vector<unsigned char> bytes;
};

class MidiHub {
public:
    MidiHub();
    ~MidiHub();

    static void listPorts();

    // Call regularly (every frame is fine); rescans ports about once a second,
    // connecting new devices and dropping unplugged ones. Returns true if the
    // Launchpad was just disconnected (so held pads can be released).
    bool poll(double now);
    void forceRescan() { nextScan_ = 0.0; }

    std::vector<MidiMsg> takeMessages();
    void pushFrame(const Frame& f);

    // --- Launchpad selection. Empty name = auto-detect anything called "Launchpad".
    void setLaunchpadPorts(const std::string& in, const std::string& out);
    const std::string& wantedIn() const { return wantIn_; }
    const std::string& wantedOut() const { return wantOut_; }
    bool launchpadConnected() const { return lpIn_ != nullptr && lpOut_ != nullptr; }
    const std::string& launchpadInName() const { return lpInName_; }
    const std::string& launchpadOutName() const { return lpOutName_; }

    // --- Port lists from the latest scan.
    const std::vector<std::string>& inputs() const { return inNames_; }
    const std::vector<std::string>& outputs() const { return outNames_; }

    // --- Other MIDI inputs (keyboards etc.). New ones are enabled automatically.
    struct KeyboardInfo {
        std::string name;
        bool enabled;
        bool open;
        double lastActivity;
        int route;
    };
    std::vector<KeyboardInfo> keyboards() const;
    void setKeyboardEnabled(const std::string& name, bool on);
    void setKeyboardRoute(const std::string& name, int route);
    int keyboardRoute(const std::string& name) const;
    // For saving / restoring between sessions.
    const std::map<std::string, int>& routes() const { return kbRoute_; }
    const std::set<std::string>& disabledInputs() const { return kbDisabled_; }
    void restore(const std::map<std::string, int>& routes, const std::set<std::string>& disabled);
    double launchpadLastActivity() const { return lpActivity_.load(); }

private:
    struct KbPort;
    static void onLaunchpadMsg(double, std::vector<unsigned char>* msg, void* user);
    static void onKeyboardMsg(double, std::vector<unsigned char>* msg, void* user);
    void scan(double now);
    void openLaunchpad(const std::string& in, const std::string& out);
    void closeLaunchpad(const char* why);
    void send(unsigned char a, unsigned char b, unsigned char c);
    bool isLaunchpadName(const std::string& n) const;

    std::unique_ptr<RtMidiIn> scanIn_;
    std::unique_ptr<RtMidiOut> scanOut_;
    std::vector<std::string> inNames_, outNames_;
    double nextScan_ = 0.0;

    std::string wantIn_, wantOut_;
    std::unique_ptr<RtMidiIn> lpIn_;
    std::unique_ptr<RtMidiOut> lpOut_;
    std::string lpInName_, lpOutName_;
    std::atomic<bool> lpError_{false};
    std::atomic<double> lpActivity_{-10.0};
    std::set<std::string> failedOnce_;
    int updateBuf_ = 0;
    Frame shown_{};
    bool haveShown_ = false;

    std::map<std::string, std::unique_ptr<KbPort>> kbs_;
    std::set<std::string> kbDisabled_;
    std::map<std::string, int> kbRoute_;

    mutable std::mutex mu_;
    std::vector<MidiMsg> queue_;
};

}  // namespace dnb
