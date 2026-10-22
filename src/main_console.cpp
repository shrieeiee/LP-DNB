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
#include "engine.h"
#include "launchpad.h"
#include "styles.h"

using namespace dnb;

namespace {

std::atomic<bool> gRunning{true};
void onSignal(int) { gRunning = false; }

void printLayout() {
    std::printf(
        "\n"
        "  TOP ROW (round):  [Play/Stop] [View] [BPM-] [BPM+] [Pat 1] [Pat 2] [Pat 3] [Pat 4]\n"
        "  Hold [View] and tap a pad in the top half to change style:\n");
    for (int i = 0; i < styleCount(); ++i) {
        const Style& s = styles()[static_cast<size_t>(i)];
        std::printf("     row %d pad %d  %-12s %3.0f BPM  %s\n", i / 8 + 1, i % 8 + 1, s.name.c_str(), s.bpm, s.blurb.c_str());
    }
    std::printf(
        "\n"
        "  Rows 1-4  PERFORM: light show      EDIT: rows 1-2 = 16 steps of the selected track\n"
        "  Row 5     the style's drum kit (6 tracks + 1 extra) and its chord sound on the right\n"
        "  Row 6     notes in the style's key (bass)\n"
        "  Rows 7-8  hold for effects (each style has its own set)\n"
        "  Right column: mute tracks (PERFORM) / choose track to edit (EDIT)\n"
        "  Ctrl+C to quit.\n\n");
}

// ----------------------------------------------------------------- demo --
// Offline render through every style: checks levels and writes a WAV.

void writeWav(const std::string& path, const std::vector<float>& lr, int sr) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(lr.size() * 2);
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr * 4)); u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : lr) {
        int v = static_cast<int>(std::max(-1.f, std::min(1.f, s)) * 32767.f);
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

// all = every pattern of every style; otherwise the first two of each.
int runDemo(const std::string& path, bool all) {
    const int sr = 48000;
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    Engine engine(*sh, static_cast<float>(sr));

    std::vector<int> segs;
    for (int s = 0; s < styleCount(); ++s)
        for (int k = 0; k < (all ? kSlots : 2); ++k) segs.push_back(s * kSlots + k);
    const int barsPerSeg = 2;

    struct Stat { double peak = 0, sum = 0; size_t n = 0; bool bad = false; };
    std::vector<Stat> stats(segs.size());

    sh->send(Cmd::QueuePattern, segs[0]);
    sh->send(Cmd::Play);
    std::vector<float> audio;
    std::vector<float> buf(64 * 2);
    size_t seg = 0;
    int bar = -1, lastStep = -1, fxOn = -1;
    bool queuedNext = false;
    int badTotal = 0;
    while (seg < segs.size()) {
        engine.process(buf.data(), 64);
        const int step = sh->step.load();
        if (step != lastStep) {
            if (step == 0) {
                const int now = sh->pattern.load();
                if (seg + 1 < segs.size() && now == segs[seg + 1]) {  // next pattern has just started
                    ++seg;
                    bar = 0;
                    queuedNext = false;
                } else if (++bar >= barsPerSeg && seg + 1 == segs.size()) {
                    ++seg;  // last segment done
                }
            }
            if (seg < segs.size()) {
                const Style& st = styleOf(segs[seg]);
                // Last bar of a segment: queue the next pattern; hold an effect in the middle.
                if (bar == barsPerSeg - 1) {
                    if (!queuedNext && seg + 1 < segs.size()) {
                        sh->send(Cmd::QueuePattern, segs[seg + 1]);
                        queuedNext = true;
                    }
                    if (step == 8 && fxOn < 0) {
                        fxOn = st.fx[(seg * 5 + 3) % 16];
                        sh->send(Cmd::FxOn, fxOn);
                    }
                    if (step == 13 && fxOn >= 0) {
                        sh->send(Cmd::FxOff, fxOn);
                        fxOn = -1;
                    }
                }
            }
            lastStep = step;
        }
        if (seg >= segs.size()) break;
        if (bar >= 0) {
            Stat& s = stats[seg];
            for (float v : buf) {
                if (!std::isfinite(v)) { s.bad = true; v = 0.f; }
                s.peak = std::max(s.peak, static_cast<double>(std::fabs(v)));
                s.sum += v * v;
                ++s.n;
            }
            audio.insert(audio.end(), buf.begin(), buf.end());
        }
    }

    std::printf("%-12s %-18s %6s %6s\n", "Style", "Pattern", "Peak", "RMS");
    for (size_t i = 0; i < segs.size(); ++i) {
        const Style& st = styleOf(segs[i]);
        const Stat& s = stats[i];
        std::printf("%-12s %-18s %6.3f %6.3f%s\n", st.name.c_str(), st.patterns[static_cast<size_t>(segs[i] % kSlots)].name.c_str(),
                    s.peak, s.n ? std::sqrt(s.sum / s.n) : 0.0, s.bad ? "  ** NON-FINITE **" : "");
        badTotal += s.bad ? 1 : 0;
    }
    if (!path.empty()) {
        writeWav(path, audio, sr);
        std::printf("Wrote %s (%.1f s)\n", path.c_str(), audio.size() / 2.0 / sr);
    }
    return badTotal ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string audioName;
    int startStyle = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--list") {
            MidiHub::listPorts();
            return 0;
        } else if (a == "--style") {
            startStyle = std::max(0, std::atoi(next()) - 1);
        } else if (a == "--audio") {
            audioName = next();
        } else if (a == "--demo") {
            return runDemo(next(), false);
        } else if (a == "--test") {
            return runDemo(i + 1 < argc ? next() : "", true);
        } else {
            std::printf("Usage: lp-dnb [--list] [--style N] [--audio \"device name\"] [--demo out.wav] [--test [out.wav]]\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }

    App app;
    if (startStyle > 0 && startStyle < styleCount()) app.controller().chooseStyle(startStyle);
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
