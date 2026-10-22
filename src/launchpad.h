// Novation Launchpad Mini (mk1 / mk2, the micro-USB ones) MIDI I/O.
//
// Protocol notes (Launchpad S/Mini programmer's reference):
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
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class RtMidiIn;
class RtMidiOut;

namespace dnb {

// One LED: low nibble = red 0..3, high nibble = green 0..3.
using Frame = std::array<uint8_t, 80>;

class Launchpad {
public:
    Launchpad();
    ~Launchpad();

    static void listPorts();
    // portIn/portOut < 0 means "auto-detect a port named Launchpad".
    bool open(int portIn, int portOut, std::string& err);
    void close();

    // Blocks until a message arrives or `timeoutMs` elapses. Returns all queued messages.
    std::vector<std::vector<unsigned char>> waitMessages(int timeoutMs);
    // Stream a whole frame using double-buffered rapid update.
    void pushFrame(const Frame& f);
    void clear();

private:
    static void onMessage(double, std::vector<unsigned char>* msg, void* user);
    void send(unsigned char a, unsigned char b, unsigned char c);

    std::unique_ptr<RtMidiIn> in_;
    std::unique_ptr<RtMidiOut> out_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::vector<unsigned char>> queue_;
    int updateBuf_ = 0;
    Frame shown_{};
    bool haveShown_ = false;
};

}  // namespace dnb
