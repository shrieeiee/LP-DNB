// lp-dnb console front-end (macOS / Linux, or anywhere without the GUI).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "common.h"
#include "controller.h"
#include "engine.h"
#include "launchpad.h"
#include "patterns.h"

using namespace dnb;

namespace {

std::atomic<bool> gRunning{true};
void onSignal(int) { gRunning = false; }

void printLayout() {
    std::printf(
        "\n"
        "  TOP ROW (round):  [Play/Stop] [View] [BPM-] [BPM+] [Pat 1] [Pat 2] [Pat 3] [Pat 4]\n"
        "\n"
        "  Rows 1-4  PERFORM: light show (tap it for ripples)\n"
        "            EDIT:    rows 1-2 = 16 steps of the selected track\n"
        "  Row 5     drums:  Kick  Snare Ghost Hat   OpenHat Ride  Crash Stab\n"
        "  Row 6     bass:   F minor scale, F1 .. F2 (Reese + sub)\n"
        "  Row 7     hold:   Stut1/4 Stut1/8 Stut1/16 Stut1/32 TapeStop HalfTime Reverse Delay\n"
        "  Row 8     hold:   LowPass HighPass Crush Flanger Reverb Siren Riser AirHorn\n"
        "\n"
        "  RIGHT COLUMN (round), tracks Kick Snare Ghost Hat OpenHat Ride Bass Stab:\n"
        "     PERFORM: mute/unmute    EDIT: choose track to edit\n"
        "\n"
        "  Bass steps: hold a step and press a bass pad to set its note.\n"
        "  Hold [View] + pattern button: copy current pattern there.\n"
        "  Hold [View] + right-column button (EDIT): clear that track.\n"
        "  Ctrl+C to quit.\n\n");
}

// ---------------------------------------------------------------- demo --
// Offline render: plays the patterns with scripted effects into a WAV, while
// driving the controller with fake pad presses. Handy for checking the
// build without hardware.

void writeWav(const std::string& path, const std::vector<float>& lr, int sr) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(lr.size() * 2);
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : lr) {
        int v = static_cast<int>(std::max(-1.f, std::min(1.f, s)) * 32767.f);
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

void printFrame(const Frame& fr) {
    // r = red, g = green, a = amber/mix; upper case = bright
    auto ch = [](uint8_t v) {
        int r = v & 3, g = (v >> 4) & 3;
        if (!r && !g) return '.';
        char c = r && g ? 'a' : (r ? 'r' : 'g');
        return std::max(r, g) >= 2 ? static_cast<char>(c - 32) : c;
    };
    std::printf("   ");
    for (int i = 0; i < 8; ++i) std::printf("%c", ch(fr[72 + i]));
    std::printf("\n");
    for (int y = 0; y < 8; ++y) {
        std::printf("   ");
        for (int x = 0; x < 8; ++x) std::printf("%c", ch(fr[y * 8 + x]));
        std::printf(" %c\n", ch(fr[64 + y]));
    }
}

int runDemo(const std::string& path) {
    const int sr = 48000;
    auto sh = std::make_unique<Shared>();
    loadPresets(*sh);
    Engine engine(*sh, static_cast<float>(sr));
    Controller ctl(*sh);

    const double beat = 60.0 / sh->bpm.load();
    const double bar = beat * 4;
    const double total = bar * 24;

    struct Ev {
        double bar;
        std::vector<unsigned char> msg;
    };
    auto pad = [](int x, int y, bool down) {
        return std::vector<unsigned char>{0x90, static_cast<unsigned char>(y * 16 + x),
                                          static_cast<unsigned char>(down ? 127 : 0)};
    };
    auto top = [](int i, bool down) {
        return std::vector<unsigned char>{0xB0, static_cast<unsigned char>(104 + i),
                                          static_cast<unsigned char>(down ? 127 : 0)};
    };
    std::vector<Ev> evs = {
        {0.0, top(0, true)}, {0.01, top(0, false)},             // play pattern 1
        {2.0, pad(0, 6, true)}, {2.25, pad(0, 6, false)},        // stutter 1/4
        {2.5, pad(2, 6, true)}, {2.75, pad(2, 6, false)},        // stutter 1/16
        {3.0, pad(0, 7, true)}, {4.0, pad(0, 7, false)},         // low-pass sweep
        {3.5, top(5, true)}, {3.51, top(5, false)},              // queue pattern 2
        {5.0, pad(7, 6, true)}, {5.5, pad(7, 6, false)},         // delay throw
        {6.0, pad(13 % 8, 7, true)}, {7.0, pad(13 % 8, 7, false)},  // siren
        {7.5, top(6, true)}, {7.51, top(6, false)},              // queue pattern 3
        {8.0, pad(0, 5, true)}, {8.5, pad(0, 5, false)},         // live bass
        {9.0, pad(2, 7, true)}, {10.0, pad(2, 7, false)},        // bitcrush
        {10.0, pad(3, 7, true)}, {11.0, pad(3, 7, false)},       // flanger
        {11.0, pad(5, 6, true)}, {12.0, pad(5, 6, false)},       // half-time
        {11.5, top(7, true)}, {11.51, top(7, false)},            // queue pattern 4
        {12.0, pad(4, 7, true)}, {13.0, pad(4, 7, false)},       // reverb wash
        {14.0, pad(6, 6, true)}, {14.5, pad(6, 6, false)},       // reverse
        {15.0, pad(1, 7, true)}, {16.0, pad(1, 7, false)},       // high-pass
        {16.0, pad(6, 7, true)}, {20.0, pad(6, 7, false)},       // riser for 4 bars -> drop
        {20.0, pad(6, 4, true)}, {20.05, pad(6, 4, false)},      // crash on the drop
        {21.0, pad(7, 7, true)}, {21.5, pad(7, 7, false)},       // air horn
        {22.0, pad(4, 6, true)}, {23.0, pad(4, 6, false)},       // tape stop
    };

    std::vector<float> audio;
    audio.reserve(static_cast<size_t>(total * sr * 2) + 1024);
    const int block = 256;
    std::vector<float> buf(block * 2);
    size_t ev = 0;
    double t = 0.0, nextFrame = 0.0;
    int framesShown = 0;
    Frame frame{};
    double peak = 0.0, sumSq = 0.0;
    bool bad = false;
    while (t < total) {
        while (ev < evs.size() && evs[ev].bar * bar <= t) ctl.onMidi(evs[ev++].msg, t);
        engine.process(buf.data(), block);
        for (float s : buf) {
            if (!std::isfinite(s)) bad = true;
            peak = std::max(peak, static_cast<double>(std::fabs(s)));
            sumSq += s * s;
        }
        audio.insert(audio.end(), buf.begin(), buf.end());
        t += static_cast<double>(block) / sr;
        if (t >= nextFrame) {
            ctl.render(t, frame);
            nextFrame += 1.0 / 30.0;
            // show a few frames mid-effect
            static const double showAt[] = {1.0, 3.6, 6.4, 9.5, 17.5, 20.05};
            if (framesShown < 6 && t >= showAt[framesShown] * bar) {
                std::printf("bar %.2f:\n", t / bar);
                printFrame(frame);
                ++framesShown;
            }
        }
    }
    writeWav(path, audio, sr);
    std::printf("Wrote %s: %.1f s, peak %.3f, RMS %.3f%s\n", path.c_str(), total, peak,
                std::sqrt(sumSq / audio.size()), bad ? "  ** NON-FINITE SAMPLES **" : "");
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    float bpm = 174.f;
    std::string audioName;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--list") {
            MidiHub::listPorts();
            return 0;
        } else if (a == "--bpm") {
            bpm = static_cast<float>(std::atof(next()));
        } else if (a == "--audio") {
            audioName = next();
        } else if (a == "--demo") {
            return runDemo(next());
        } else {
            std::printf("Usage: lp-dnb [--list] [--bpm 174] [--audio \"device name\"] [--demo out.wav]\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }

    App app;
    app.shared().bpm.store(std::max(100.f, std::min(200.f, bpm)));
    if (!audioName.empty()) app.selectAudio(audioName);

    std::signal(SIGINT, onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, onSignal);
#endif
    printLayout();
    std::printf("Waiting for a Launchpad (plug it in any time). Ctrl+C to quit.\n");

    while (gRunning) {
        app.tick(monotonicSec());
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    std::printf("\nBye.\n");
    return 0;
}
