// lp-tenori: Tenori-on style sequencer for the Launchpad Mini, using the LP-DNB
// sound engine. A console program on every platform.
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
#include "tenori.h"

#ifdef _WIN32  // after our headers, so its macros can't touch them
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace dnb;

namespace {

std::atomic<bool> gRunning{true};
void onSignal(int) { gRunning = false; }

void printLayout() {
    std::printf(
        "\n"
        "  LP-TENORI  -  a Tenori-on style sequencer on the LP-DNB sound engine\n"
        "\n"
        "  TOP ROW:  [Play] [Page] [BPM-] [BPM+] [Mode] [Sound] [Fx] [Shift]\n"
        "  RIGHT COLUMN: the 8 layers (tap to pick one; Shift + tap = mute)\n"
        "\n"
        "  Each layer plays one instrument (the style's Drums, Bass or Chords, or a Synth\n"
        "  of its own) in one mode:\n"
        "    Score   rows are notes (top = highest), columns are steps. Tap to toggle.\n"
        "            16 steps: [Page] flips between the two halves.\n"
        "    Bounce  tap a pad: a ball drops from there to the bottom row and back,\n"
        "            playing as it lands. Higher = slower. Columns are notes.\n"
        "    Path    tap points: a light travels between them in order, playing each\n"
        "            one. Further apart = longer gap. Tap a point again to remove it.\n"
        "    Push    hold pads to play them on repeat. Rows are notes, columns are\n"
        "            speed (left = once a bar ... right = 1/64 notes).\n"
        "  On Drums layers the rows (or Bounce columns) are the style's kit, bottom up,\n"
        "  with its chord stab at the top.\n"
        "\n"
        "  [Mode] / [Sound] step through the modes and sounds of this layer.\n"
        "  Hold [Fx]: the bottom two rows become the style's 16 effects (hold them).\n"
        "  Hold [Shift] for the settings page:\n"
        "    rows 1-2  style (sounds, key and tempo)\n"
        "    row 3     sound (4 pads) and mode (4 pads) of this layer\n"
        "    row 4     octave -2..+2 | show other layers | swing | page follows playhead\n"
        "    rows 5-6  this layer's synth (green = poly, red = mono, amber = this style's)\n"
        "    row 7     layer volume        row 8  Score loop length (tap twice = odd)\n"
        "    Shift + Mode = clear layer   Shift + Sound = undo   Shift + Play = clear all\n"
        "    Shift + BPM-/+ = tempo by 5  Shift + Page = page follows playhead\n"
        "\n"
        "  Other MIDI keyboards play LP-DNB's lead synth over the top. Your layers are saved to\n"
        "  %s\n"
        "  Ctrl+C to quit.\n\n",
        Tenori::savePath().c_str());
}

void writeWav(const std::string& path, const std::vector<float>& lr, int sr) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataBytes = static_cast<uint32_t>(lr.size() * 2);
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr * 4)); u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : lr) {
        const int v = static_cast<int>(std::max(-1.f, std::min(1.f, s)) * 32767.f);
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

// Offline render of a little scene that uses every mode: no hardware needed.
int runDemo(const std::string& path, int style, int bars) {
    const int sr = 48000, block = 256;
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    leadPatches();
    Engine engine(*sh, static_cast<float>(sr));
    Tenori t(*sh);
    t.setStyle(style);
    t.syncSynths();

    // Layer 1, lead Score: a rising and falling arpeggio.
    const int arp[16] = {0, 2, 4, 7, 4, 2, 0, 2, 1, 3, 5, 7, 5, 3, 1, 3};
    for (int s = 0; s < 16; ++s) t.layer(0).score[7 - arp[s]] |= static_cast<uint16_t>(1u << s);
    // Layer 2, drums Score: kick, second drum and third (usually a hat) from the bottom.
    Layer& d = t.layer(1);
    for (int s : {0, 10}) d.score[7] |= static_cast<uint16_t>(1u << s);
    for (int s : {4, 12}) d.score[6] |= static_cast<uint16_t>(1u << s);
    for (int s = 0; s < 16; s += 2) d.score[5] |= static_cast<uint16_t>(1u << s);
    // Layer 3, bass Score.
    for (auto [s, deg] : {std::pair{0, 0}, {3, 0}, {6, 4}, {8, 5}, {11, 3}, {14, 4}})
        t.layer(2).score[7 - deg] |= static_cast<uint16_t>(1u << s);
    // Layer 4, chords Score: two chords per bar.
    t.layer(3).score[7] |= 1u;
    t.layer(3).score[2] |= 1u << 8;
    // Layer 5, lead Bounce an octave up: three balls at different heights.
    t.layer(4).octave = 1;
    t.layer(4).ball[0] = 3;
    t.layer(4).ball[2] = 5;
    t.layer(4).ball[4] = 1;
    t.layer(4).volume = 4;
    // Layer 6, drums Bounce: a shaker-ish polyrhythm on the 5th sound.
    t.layer(5).ball[4] = 4;
    // Layer 7, lead Path.
    for (auto p : {std::pair{1, 1}, {6, 2}, {4, 5}, {2, 4}}) t.layer(6).path.push_back(p);
    t.layer(6).volume = 4;
    for (int i = 0; i < kLayers; ++i) t.layer(i).resetMotion();

    // Everything runs on a virtual clock that starts at the real one, so commands
    // the Tenori stamps with the real clock (lead sound, style) land straight away.
    const double t0 = monotonicSec();
    std::vector<float> buf(block * 2), audio;
    double vt = t0;
    // Let the style switch reach the engine first (it sets the style's tempo).
    for (int i = 0; i < 4; ++i) { engine.process(buf.data(), block, vt); vt += static_cast<double>(block) / sr; }
    t.setPlaying(true, vt);
    const double barSec = 60.0 / sh->bpm.load() * 4.0;
    const double end = vt + bars * barSec;
    bool pushed = false, released = false;
    const unsigned char pushPad = static_cast<unsigned char>(2 * 16 + 5);  // row 3, 1/8-step repeats
    uint32_t hits0[kHitSlots];
    for (int i = 0; i < kHitSlots; ++i) hits0[i] = sh->hits[i].load();
    double peak = 0.0, sum = 0.0;
    bool bad = false;
    while (vt < end) {
        // Hold a Push pad on layer 8 through the second half's first bar.
        const double bar = (vt - (end - bars * barSec)) / barSec;
        if (!pushed && bar >= bars / 2.0) {
            t.selectLayer(7);
            t.onLaunchpad({0x90, pushPad, 127}, vt);
            pushed = true;
        }
        if (pushed && !released && bar >= bars / 2.0 + 1.0) {
            t.onLaunchpad({0x90, pushPad, 0}, vt);
            released = true;
        }
        t.schedule(vt + 0.06);
        engine.process(buf.data(), block, vt);
        vt += static_cast<double>(block) / sr;
        for (float v : buf) {
            if (!std::isfinite(v)) { bad = true; v = 0.f; }
            peak = std::max(peak, static_cast<double>(std::fabs(v)));
            sum += static_cast<double>(v) * v;
        }
        audio.insert(audio.end(), buf.begin(), buf.end());
    }
    const Style& st = styles()[static_cast<size_t>(t.style())];
    std::printf("%s, %.0f BPM, %d bars: peak %.3f, RMS %.3f%s\n", st.name.c_str(), static_cast<double>(sh->bpm.load()), bars,
                peak, std::sqrt(sum / std::max<size_t>(1, audio.size())), bad ? "  ** NON-FINITE **" : "");
    std::printf("Notes played:");
    const char* names[kHitSlots] = {"drum1", "drum2", "drum3", "drum4", "drum5", "drum6", "extra", "chords", "bass", "lead"};
    for (int i = 0; i < kHitSlots; ++i) std::printf(" %s %u", names[i], sh->hits[i].load() - hits0[i]);
    std::printf("\n");
    if (!path.empty()) {
        writeWav(path, audio, sr);
        std::printf("Wrote %s (%.1f s)\n", path.c_str(), audio.size() / 2.0 / sr);
    }
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string audioName, demoPath;
    int startStyle = -1, bars = 8;
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--list") {
            MidiHub::listPorts();
            return 0;
        } else if (a == "--style") {
            startStyle = std::max(0, std::atoi(next()) - 1);
        } else if (a == "--audio") {
            audioName = next();
        } else if (a == "--demo") {
            demo = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') demoPath = next();
        } else if (a == "--bars") {
            bars = std::clamp(std::atoi(next()), 1, 64);
        } else {
            std::printf("Usage: lp-tenori [--list] [--style N] [--audio \"device name\"] [--demo [out.wav] [--style N] [--bars N]]\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }
    if (demo) return runDemo(demoPath, startStyle >= 0 ? startStyle : 9, bars);

#ifdef _WIN32
    timeBeginPeriod(1);  // 1 ms sleeps, so the main loop runs smoothly
#endif
    App app;
    app.setAutosave(false);  // leave the groovebox's own save file alone
    Tenori tenori(app.shared());
    tenori.loadFile();
    if (startStyle >= 0) tenori.setStyle(startStyle);
    if (!audioName.empty()) app.selectAudio(audioName);
    app.setSurface(&tenori);

    std::signal(SIGINT, onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, onSignal);
#endif
    printLayout();
    std::printf("Waiting for a Launchpad (plug it in any time).\n\n");

    std::string shown;
    double nextStatus = 0.0;
    bool wasConnected = false;
    while (gRunning) {
        const double now = monotonicSec();
        app.tick(now);
        if (now >= nextStatus) {
            nextStatus = now + 0.2;
            const bool conn = app.midi().launchpadConnected();
            if (conn != wasConnected) {
                std::printf("\r%-110s\r", "");
                std::printf(conn ? "Launchpad connected: %s\n" : "Launchpad disconnected%s\n",
                            conn ? app.midi().launchpadInName().c_str() : "");
                wasConnected = conn;
                shown.clear();
            }
            const std::string s = tenori.status();
            if (s != shown) {
                std::printf("\r%-110s", s.c_str());
                std::fflush(stdout);
                shown = s;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    tenori.saveIfChanged();
    app.setSurface(nullptr);
    std::printf("\nBye.\n");
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}
