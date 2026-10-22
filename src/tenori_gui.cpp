// lp-tenori's window: an on-screen Launchpad, the eight layers with an
// instrument picker, a big editor for the selected layer, effects, the mixer
// and devices. Everything edits through Tenori, exactly like the Launchpad does.
#include "tenori_gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "gui.h"
#include "gui_kit.h"
#include "imgui.h"
#include "log.h"
#include "styles.h"
#include "tenori.h"

namespace dnb {

using namespace gui;

namespace {

const ImU32 kWell = IM_COL32(29, 26, 41, 255);
const ImU32 kCell = IM_COL32(52, 47, 70, 255);
const ImU32 kCellBeat = IM_COL32(64, 58, 86, 255);
const ImU32 kPadEdge = IM_COL32(84, 77, 108, 255);
const ImU32 kTextDim = IM_COL32(172, 165, 196, 255);
const ImU32 kInk = IM_COL32(40, 30, 50, 235);
const ImVec4 kGreen(0.42f, 0.88f, 0.62f, 1.f);
const ImVec4 kRed(1.00f, 0.43f, 0.52f, 1.f);
const ImVec4 kLilac(0.72f, 0.62f, 1.00f, 1.f);
const ImVec4 kAccent(1.00f, 0.66f, 0.40f, 1.f);
const ImVec4 kLabel(0.70f, 0.66f, 0.80f, 1.f);
const ImVec4 kHint(0.58f, 0.55f, 0.68f, 1.f);

// Layer colours on screen: close to the Launchpad's, but all eight different.
const ImU32 kLayerGui[kLayers] = {IM_COL32(110, 225, 150, 255), IM_COL32(255, 112, 128, 255), IM_COL32(255, 196, 92, 255),
                                  IM_COL32(255, 150, 100, 255), IM_COL32(190, 235, 120, 255), IM_COL32(255, 172, 64, 255),
                                  IM_COL32(250, 226, 112, 255), IM_COL32(255, 150, 210, 255)};
ImVec4 layerVec(int i) { return ImGui::ColorConvertU32ToFloat4(kLayerGui[i & 7]); }
ImU32 shade(ImU32 c, float k) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x * k, v.y * k, v.z * k, v.w));
}
ImU32 mixCol(ImU32 a, ImU32 b, float k) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * k, x.y + (y.y - x.y) * k, x.z + (y.z - x.z) * k, 1.f));
}
ImU32 soundGui(int s) {
    switch (s) {
        case SND_DRUMS: return IM_COL32(255, 112, 128, 255);
        case SND_BASS: return IM_COL32(100, 215, 215, 255);
        case SND_CHORDS: return IM_COL32(190, 160, 255, 255);
        default: return IM_COL32(110, 225, 150, 255);
    }
}
ImU32 modeGui(int m) {
    switch (m) {
        case MODE_SCORE: return IM_COL32(110, 225, 150, 255);
        case MODE_BOUNCE: return IM_COL32(255, 196, 92, 255);
        case MODE_PATH: return IM_COL32(255, 150, 100, 255);
        default: return IM_COL32(255, 112, 128, 255);
    }
}
ImU32 groupGui(const std::string& g) {
    if (g == "Leads") return IM_COL32(110, 225, 150, 255);
    if (g == "Keys") return IM_COL32(250, 226, 112, 255);
    if (g == "Pads") return IM_COL32(190, 160, 255, 255);
    if (g == "Mono") return IM_COL32(255, 150, 100, 255);
    return IM_COL32(100, 215, 215, 255);
}

const char* kTopNames[8] = {"Play", "Page", "BPM-", "BPM+", "Mode", "Sound", "Fx", "Shift"};

struct Ui {
    int tab = 0;
    bool shiftLatched = false, fxLatched = false;  // on-screen Shift / Fx: click to hold, click again to let go
    int paint = -1;                // Score: painting notes on (1) or off (0) while dragging
    int pushX = -1, pushY = -1, pushLayer = -1;    // Push square held with the mouse
    bool fxHeld[16] = {};          // Effects page buttons held
    int fxSent[16];                // ...and which effect each one started
    int pickerFor = -1;            // layer the instrument popup is for
    Ui() { for (int& f : fxSent) f = -1; }
};
Ui& ui() {
    static Ui u;
    return u;
}

// A grid of equal cells with one invisible button over it, so a press can be
// dragged across cells (painting notes, sliding a Push finger).
struct Grid {
    ImVec2 o;
    float cell = 0.f, gap = 0.f;
    int cols = 8, rows = 8;
    ImVec2 a(int c, int r) const { return ImVec2(o.x + c * cell, o.y + r * cell); }
    ImVec2 b(int c, int r) const { return ImVec2(o.x + (c + 1) * cell - gap, o.y + (r + 1) * cell - gap); }
    bool at(ImVec2 p, int& c, int& r) const {
        c = static_cast<int>(std::floor((p.x - o.x) / cell));
        r = static_cast<int>(std::floor((p.y - o.y) / cell));
        return c >= 0 && c < cols && r >= 0 && r < rows;
    }
};

std::string shortName(const std::string& n, size_t width = 6) {
    // Two short lines, for a pad label.
    const size_t sp = n.find(' ');
    std::string a = n.substr(0, sp), b = sp == std::string::npos ? "" : n.substr(sp + 1);
    if (a.size() > width) a = a.substr(0, width);
    if (b.size() > width) b = b.substr(0, width - 1) + ".";
    return b.empty() ? a : a + "\n" + b;
}

// ---------------------------------------------------------------- top bar --

void drawTopBar(App& app, Tenori& t, GuiState& gs, double now) {
    Shared& sh = app.shared();
    const float s = gs.scale;
    const bool playing = t.playing();
    const Style& st = styles()[static_cast<size_t>(t.style())];

    {   // Play / Stop pill with the beat ticking underneath.
        const ImVec2 sz(132 * s, 52 * s);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 6 * s));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("playstop", sz);
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 c = playing ? kRed : kGreen;
        if (hov) c = ImVec4(std::min(1.f, c.x * 1.08f), std::min(1.f, c.y * 1.08f), std::min(1.f, c.z * 1.08f), 1.f);
        const float press = act ? 2 * s : 0.f;
        dl->AddRectFilled(ImVec2(a.x, a.y + 3 * s), ImVec2(a.x + sz.x, a.y + sz.y + 3 * s), withAlpha(ImGui::GetColorU32(c), 90), sz.y * 0.5f);
        dl->AddRectFilled(ImVec2(a.x, a.y + press), ImVec2(a.x + sz.x, a.y + sz.y + press), ImGui::GetColorU32(c), sz.y * 0.5f);
        const ImU32 ink = IM_COL32(33, 26, 46, 255);
        const float cy = a.y + sz.y * 0.5f + press, ix = a.x + 30 * s, r = 9 * s;
        if (playing) dl->AddRectFilled(ImVec2(ix - r, cy - r), ImVec2(ix + r, cy + r), ink, 4 * s);
        else dl->AddTriangleFilled(ImVec2(ix - r * 0.8f, cy - r * 1.1f), ImVec2(ix - r * 0.8f, cy + r * 1.1f), ImVec2(ix + r * 1.1f, cy), ink);
        ImFont* big = gs.bigFont ? gs.bigFont : ImGui::GetFont();
        const char* txt = playing ? "Stop" : "Play";
        const ImVec2 ts = big->CalcTextSizeA(big->FontSize, 1e9f, 0.f, txt);
        dl->AddText(big, big->FontSize, ImVec2(a.x + 50 * s + (sz.x - 50 * s - ts.x) * 0.45f, cy - ts.y * 0.5f), ink, txt);
        if (pressed) t.setPlaying(!playing, now);
        if (hov) ImGui::SetTooltip("Space bar works too");
        ImGui::EndGroup();
    }
    ImGui::SameLine(0, 8 * s);
    {   // Record what you hear to a WAV file.
        const bool rec = app.recording();
        const ImVec2 sz(rec ? 112 * s : 52 * s, 52 * s);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 6 * s));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("rec", sz)) {
            if (rec) app.stopRecording();
            else app.startRecording();
        }
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), rec ? IM_COL32(255, 110, 130, 255) : (hov ? IM_COL32(70, 62, 96, 255) : IM_COL32(58, 52, 80, 255)), sz.y * 0.5f);
        const bool blink = rec && std::fmod(now, 1.0) < 0.6;
        dl->AddCircleFilled(ImVec2(a.x + 26 * s, a.y + sz.y * 0.5f), 9 * s, rec ? (blink ? IM_COL32(60, 20, 30, 255) : IM_COL32(120, 40, 55, 255)) : IM_COL32(255, 100, 120, 255));
        if (rec) {
            char tx[32];
            const int secs = static_cast<int>(app.recordingSeconds());
            std::snprintf(tx, sizeof tx, "%d:%02d", secs / 60, secs % 60);
            ImFont* big = gs.bigFont ? gs.bigFont : ImGui::GetFont();
            const ImVec2 ts = big->CalcTextSizeA(big->FontSize, 1e9f, 0.f, tx);
            dl->AddText(big, big->FontSize, ImVec2(a.x + 44 * s, a.y + (sz.y - ts.y) * 0.5f), IM_COL32(40, 20, 30, 255), tx);
        }
        if (hov) ImGui::SetTooltip(rec ? "Stop recording" : "Record what you hear to a WAV file\n(saved in %s)", app.outputFolder().c_str());
        ImGui::EndGroup();
    }
    ImGui::SameLine(0, 16 * s);

    // Style
    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "STYLE");
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::BeginCombo("##style", st.name.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < styleCount(); ++i) {
            const Style& o = styles()[static_cast<size_t>(i)];
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "%s##st%d", o.name.c_str(), i);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ledColor(o.led)));
            if (ImGui::Selectable(lbl, i == t.style())) t.setStyle(i);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s, %.0f BPM", o.blurb.c_str(), o.keyName.c_str(), o.bpm);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The style sets the key, the drum kit, bass and chord sounds, swing and effects");
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Tempo
    ImGui::BeginGroup();
    float bpm = sh.bpm.load();
    ImGui::TextColored(kLabel, "TEMPO");
    ImGui::SameLine();
    ImGui::Text("%.0f BPM", bpm);
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::SliderFloat("##bpm", &bpm, 40.f, 250.f, "")) sh.bpm.store(std::round(bpm));
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Swing and the beat
    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "FEEL");
    bool sw = t.swing();
    if (ImGui::Checkbox("Swing", &sw)) t.setSwing(sw);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use the style's swing (%s is %.0f%%)", st.name.c_str(), st.swing * 100.f);
    ImGui::SameLine(0, 10 * s);
    {
        const int64_t g = t.stepAt(now);
        const int beat = g >= 0 ? static_cast<int>((g / 4) % 4) : -1;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        for (int i = 0; i < 4; ++i)
            dl->AddCircleFilled(ImVec2(p.x + 8 * s + i * 16 * s, p.y + h * 0.5f), 5 * s,
                                i == beat ? (i == 0 ? IM_COL32(255, 196, 92, 255) : IM_COL32(110, 225, 150, 255)) : IM_COL32(64, 58, 86, 255));
        ImGui::Dummy(ImVec2(66 * s, h));
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Volume + meter
    ImGui::BeginGroup();
    float vol = sh.volume.load();
    ImGui::TextColored(kLabel, "VOLUME");
    ImGui::SameLine();
    ImGui::Text("%d%%", static_cast<int>(std::round(vol * 100.f)));
    ImGui::SetNextItemWidth(120 * s);
    if (ImGui::SliderFloat("##vol", &vol, 0.f, 1.f, "")) sh.volume.store(vol);
    ImGui::EndGroup();
    ImGui::SameLine(0, 8 * s);
    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "LEVEL");
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 90 * s, h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kWell, h * 0.5f);
        auto db = [](float v) { return v <= 1e-4f ? 0.f : std::clamp(1.f + 20.f * std::log10(v) / 36.f, 0.f, 1.f); };
        const float lv[2] = {db(sh.levelL.load()), db(sh.levelR.load())};
        const int segs = 12;
        const float rowH = (h - 16 * s) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < segs; ++i) {
                const float x0 = p.x + 10 * s + i * (w - 20 * s) / segs, x1 = x0 + (w - 20 * s) / segs - 3 * s;
                const float y0 = p.y + 7 * s + ch * (rowH + 2 * s);
                const ImU32 c = i < 8 ? IM_COL32(110, 225, 150, 255) : (i < 11 ? IM_COL32(255, 196, 92, 255) : IM_COL32(255, 112, 128, 255));
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + rowH), lv[ch] * segs > i ? c : withAlpha(c, 45), 2 * s);
            }
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::EndGroup();
}

// ------------------------------------------------------ on-screen Launchpad --

std::string padLabel(const Tenori& t, int x, int y) {
    if (t.shiftHeld()) {
        switch (y) {
            case 0: case 1: {
                const int i = y * 8 + x;
                return i < styleCount() ? styles()[static_cast<size_t>(i)].padLabel : "";
            }
            case 2: {
                static const char* n[8] = {"Drums", "Bass", "Chord", "Synth", "Score", "Bnce", "Path", "Push"};
                return n[x];
            }
            case 3: {
                static const char* n[8] = {"-2", "-1", "Oct\n0", "+1", "+2", "Show\nall", "Swing", "Follow"};
                return n[x];
            }
            case 4: case 5: {
                const size_t i = static_cast<size_t>((y - 4) * 8 + x);
                if (i >= synthPatches().size()) return "";
                return shortName(leadPatches()[static_cast<size_t>(synthPatches()[i])].name);
            }
            case 6: return x == 0 ? "Vol\n1" : std::to_string(x + 1);
            default: return x == 0 ? "Len\n2" : std::to_string((x + 1) * 2);
        }
    }
    if (t.fxHeld() && y >= 6) {
        const int f = styles()[static_cast<size_t>(t.style())].fx[static_cast<size_t>((y - 6) * 8 + x)];
        return f >= 0 && f < FX_COUNT ? kFxShort[f] : "";
    }
    return "";
}

void drawLaunchpad(App& app, Tenori& t, GuiState& gs, float width, double now) {
    const float s = gs.scale;
    MidiHub& midi = app.midi();
    Ui& u = ui();
    // The real Shift / Fx buttons were let go: drop the on-screen latch too.
    if (!t.shiftHeld()) u.shiftLatched = false;
    if (!t.fxHeld()) u.fxLatched = false;

    if (midi.launchpadConnected()) {
        const bool act = now - midi.launchpadLastActivity() < 0.12;
        statusDot(act ? IM_COL32(255, 200, 60, 255) : IM_COL32(80, 220, 90, 255), s);
        ImGui::Text("Launchpad connected");
    } else {
        statusDot(std::fmod(now, 1.4) < 0.7 ? IM_COL32(120, 120, 130, 255) : IM_COL32(60, 60, 68, 255), s);
        ImGui::Text("No Launchpad - plug it in any time");
    }
    ImGui::Spacing();

    const float pitch = std::min(width / 9.3f, (ImGui::GetContentRegionAvail().y - 90 * s) / 9.4f);
    const float pad = pitch / 1.14f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float topGap = pitch * 0.18f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    const float labelSize = std::max(11.5f * s, std::min(pad * 0.27f, 16.f * s));
    const Frame& fr = app.frame();

    dl->AddRectFilled(ImVec2(o.x - 14 * s, o.y - 14 * s), ImVec2(o.x + pitch * 9 + topGap + 14 * s, o.y + pitch * 9 + topGap + 14 * s),
                      kWell, 26 * s);

    auto button = [&](int x, int y, ImVec2 a, ImVec2 b) {
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(y * 16 + x + 32);
        ImGui::InvisibleButton("pad", ImVec2(b.x - a.x, b.y - a.y));
        const bool latch = y < 0 && (x == 6 || x == 7);  // Fx and Shift latch for the mouse
        if (latch) {
            if (ImGui::IsItemActivated()) {
                bool& l = x == 7 ? u.shiftLatched : u.fxLatched;
                l = !l;
                app.virtualPress(x, y, l);
            }
        } else {
            if (ImGui::IsItemActivated()) app.virtualPress(x, y, true);
            if (ImGui::IsItemDeactivated()) app.virtualPress(x, y, false);
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        return hov;
    };
    auto roundButton = [&](ImVec2 c, float r, uint8_t v, bool hov) {
        if (ledLit(v)) dl->AddCircleFilled(c, r + 4 * s, withAlpha(ledColor(v), 70));
        dl->AddCircleFilled(c, r, ledColor(v));
        dl->AddCircle(c, r, hov ? IM_COL32(240, 235, 255, 255) : kPadEdge, 0, (hov ? 2.f : 1.2f) * s);
    };

    static const char* kTopHelp[8] = {
        "Play / stop\nShift: clear every layer", "Score: steps 1-8 / 9-16\nShift: page follows the playhead",
        "Tempo down (hold to keep going)\nShift: by 5", "Tempo up (hold to keep going)\nShift: by 5",
        "Next mode for this layer\nShift: clear this layer", "Next sound for this layer\nShift: undo the clear",
        "Hold: the bottom two rows play the style's effects\n(click here to latch it with the mouse)",
        "Hold: settings page\n(click here to latch it with the mouse)"};
    for (int x = 0; x < 8; ++x) {
        const ImVec2 c(o.x + x * pitch + pad * 0.5f, o.y + pad * 0.5f);
        const float r = pad * 0.36f;
        const bool hov = button(x, -1, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        roundButton(c, r, fr[static_cast<size_t>(72 + x)], hov);
        centeredText(dl, small, labelSize * 0.9f, ImVec2(c.x - r - 6 * s, c.y + r + 1 * s), ImVec2(c.x + r + 6 * s, c.y + r + labelSize + 3 * s),
                     kTextDim, kTopNames[x]);
        if (hov) ImGui::SetTooltip("%s", kTopHelp[x]);
    }

    for (int y = 0; y < 8; ++y) {
        const float py = o.y + (y + 1) * pitch + topGap;
        for (int x = 0; x < 8; ++x) {
            const ImVec2 a(o.x + x * pitch, py), b(a.x + pad, py + pad);
            const bool hov = button(x, y, a, b);
            const uint8_t v = fr[static_cast<size_t>(y * 8 + x)];
            const bool lit = ledLit(v);
            const float pr = pad * 0.27f;
            if (lit) dl->AddRectFilled(ImVec2(a.x - 4 * s, a.y - 4 * s), ImVec2(b.x + 4 * s, b.y + 4 * s), withAlpha(ledColor(v), 55), pr + 4 * s);
            dl->AddRectFilled(a, b, ledColor(v), pr);
            if (lit) dl->AddRectFilled(ImVec2(a.x + pad * 0.14f, a.y + pad * 0.08f), ImVec2(b.x - pad * 0.14f, a.y + pad * 0.2f),
                                       IM_COL32(255, 255, 255, 45), pad * 0.06f);
            dl->AddRect(a, b, hov ? IM_COL32(240, 235, 255, 255) : (lit ? withAlpha(ledColor(v), 255) : kPadEdge), pr, 0, (hov ? 2.f : 1.2f) * s);
            const std::string label = padLabel(t, x, y);
            if (!label.empty())
                centeredText(dl, small, labelSize, a, b, lit ? kInk : IM_COL32(196, 190, 220, 255), label.c_str());
            if (hov && t.shiftHeld() && (y == 4 || y == 5)) {
                const size_t i = static_cast<size_t>((y - 4) * 8 + x);
                if (i < synthPatches().size()) ImGui::SetTooltip("%s", leadPatches()[static_cast<size_t>(synthPatches()[i])].name.c_str());
            }
        }
        // Right column: the layers.
        const ImVec2 c(o.x + 8 * pitch + pad * 0.5f + topGap, py + pad * 0.5f);
        const float r = pad * 0.36f;
        const bool hov = button(8, y, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        const uint8_t v = fr[static_cast<size_t>(64 + y)];
        roundButton(c, r, v, hov);
        const std::string num = std::to_string(y + 1);
        centeredText(dl, small, labelSize, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ledLit(v) ? kInk : kTextDim, num.c_str());
        if (hov) ImGui::SetTooltip("Layer %d: %s, %s\n%s", y + 1, t.instrumentName(y).c_str(), modeName(t.layer(y).mode),
                                   t.shiftHeld() ? "Click to mute / unmute" : "Click to show and edit it");
    }

    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + pitch * 9 + topGap + 22 * s));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    if (t.shiftHeld()) {
        ImGui::TextColored(kAccent, "SETTINGS: rows 1-2 style, 3 sound and mode, 4 octave and switches, 5-6 synth sound, "
                                    "7 volume, 8 loop length. Right column mutes.");
    } else {
        const Layer& L = t.layer(t.current());
        ImGui::TextColored(layerVec(t.current()), "Showing layer %d (%s, %s).", t.current() + 1, t.instrumentName(t.current()).c_str(),
                           modeName(L.mode));
        ImGui::SameLine();
        ImGui::TextColored(kHint, "Right column picks the layer.");
    }
    ImGui::TextColored(kHint, "Keys: Space play, 1-8 layer. Click Fx or Shift above to hold them.");
    ImGui::PopTextWrapPos();
}

// ------------------------------------------------------- instrument picker --

void auditionSound(Tenori& t, int li, double now) {
    t.audition(li, t.layer(li).sound == SND_DRUMS ? 0 : 4, now);
}

// Every instrument a layer can play, as big buttons. Picking one plays a note.
void drawInstrumentPicker(Tenori& t, int li, GuiState& gs, double now, float width) {
    const float s = gs.scale;
    Layer& L = t.layer(li);
    const Style& st = styles()[static_cast<size_t>(t.style())];
    const float gap = 6 * s;
    const int perRow = std::max(2, static_cast<int>((width + gap) / (150 * s + gap)));
    const float w = (width - gap * (perRow - 1)) / perRow, h = 46 * s;
    int n = 0;
    auto tile = [&](const std::string& title, const std::string& sub, ImU32 col, bool sel) {
        if (n % perRow) ImGui::SameLine(0, gap);
        ++n;
        ImGui::PushID(title.c_str());
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("inst", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(a.x + w, a.y + h);
        dl->AddRectFilled(a, b, sel ? col : (hov ? IM_COL32(70, 62, 96, 255) : IM_COL32(52, 47, 70, 255)), 12 * s);
        if (!sel) dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 6 * s, b.y), col, 12 * s, ImDrawFlags_RoundCornersLeft);
        const ImU32 ink = sel ? kInk : IM_COL32(240, 236, 250, 255);
        ImFont* f = ImGui::GetFont();
        dl->AddText(f, ImGui::GetFontSize(), ImVec2(a.x + 14 * s, a.y + 5 * s), ink, title.c_str());
        ImFont* sm = gs.smallFont ? gs.smallFont : f;
        dl->AddText(sm, sm->FontSize, ImVec2(a.x + 14 * s, a.y + h - sm->FontSize - 5 * s),
                    sel ? withAlpha(kInk, 180) : withAlpha(kTextDim, 220), sub.c_str());
        return pressed;
    };

    ImGui::TextColored(kLabel, "FROM %s", st.name.c_str());
    if (tile("Drum kit", st.drums[0].name + ", " + st.drums[1].name + "...", soundGui(SND_DRUMS), L.sound == SND_DRUMS)) {
        t.setSound(li, SND_DRUMS);
        auditionSound(t, li, now);
    }
    if (tile("Bass", st.bass.name, soundGui(SND_BASS), L.sound == SND_BASS)) {
        t.setSound(li, SND_BASS);
        auditionSound(t, li, now);
    }
    if (tile("Chords", st.chord.name, soundGui(SND_CHORDS), L.sound == SND_CHORDS)) {
        t.setSound(li, SND_CHORDS);
        auditionSound(t, li, now);
    }
    std::string group;
    for (int p : synthPatches()) {
        const LeadPatch& lp = leadPatches()[static_cast<size_t>(p)];
        if (lp.group != group) {
            group = lp.group;
            n = 0;
            ImGui::Dummy(ImVec2(0, 2 * s));
            ImGui::TextColored(kLabel, "%s", group == "This style" ? "SYNTH: THIS STYLE'S" : ("SYNTH: " + group).c_str());
        }
        std::string sub = lp.kind == LeadPatch::MONO ? "mono, glides" : (lp.kind == LeadPatch::STYLE_CHORDS ? st.chord.name : "polyphonic");
        if (tile(lp.name, sub, groupGui(lp.group), L.sound == SND_SYNTH && L.patch == p)) {
            t.setPatch(li, p);
            auditionSound(t, li, now);
        }
    }
}

// ------------------------------------------------------------ layer cards --

// The eight layers as cards: click one to show it. They flash as they play.
void drawLayerCards(Tenori& t, GuiState& gs, double now) {
    const float s = gs.scale;
    const float gap = 6 * s, full = stableAvailX();
    const float w = (full - gap * 7) / 8.f, h = 60 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* sm = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    for (int i = 0; i < kLayers; ++i) {
        const Layer& L = t.layer(i);
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("card", ImVec2(w, h))) t.selectLayer(i);
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) t.layer(i).muted = !L.muted;
        if (hov) ImGui::SetTooltip("Layer %d: %s, %s%s\nClick to show it (or press %d). Right-click to mute.", i + 1,
                                   t.instrumentName(i).c_str(), modeName(L.mode), L.muted ? " (muted)" : "", i + 1);
        ImGui::PopID();
        const ImVec2 b(a.x + w, a.y + h);
        const bool sel = i == t.current();
        const ImU32 col = L.muted ? IM_COL32(90, 84, 110, 255) : kLayerGui[i];
        const double hit = now - t.layerHit(i);
        const float glow = !L.muted && hit >= 0.0 && hit < 0.25 ? static_cast<float>(1.0 - hit / 0.25) : 0.f;
        dl->AddRectFilled(a, b, sel ? IM_COL32(64, 58, 88, 255) : (hov ? IM_COL32(52, 47, 70, 255) : IM_COL32(40, 36, 55, 255)), 12 * s);
        if (glow > 0.f) dl->AddRectFilled(a, b, withAlpha(col, static_cast<int>(70 * glow)), 12 * s);
        dl->AddRect(a, b, sel ? col : withAlpha(col, 90), 12 * s, 0, (sel ? 2.5f : 1.2f) * s);
        // Number badge.
        const float r = 13 * s;
        const ImVec2 c(a.x + 8 * s + r, a.y + 8 * s + r);
        dl->AddCircleFilled(c, r, L.empty() ? shade(col, 0.55f) : col);
        const std::string num = std::to_string(i + 1);
        centeredText(dl, sm, sm->FontSize, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), kInk, num.c_str());
        // Mode, then the instrument (clipped to the card).
        dl->AddText(sm, sm->FontSize, ImVec2(c.x + r + 6 * s, c.y - sm->FontSize * 0.5f),
                    L.muted ? IM_COL32(150, 144, 170, 255) : modeGui(L.mode), L.muted ? "Muted" : modeName(L.mode));
        const std::string inst = t.instrumentName(i);
        dl->PushClipRect(ImVec2(a.x + 6 * s, a.y), ImVec2(b.x - 6 * s, b.y), true);
        dl->AddText(sm, sm->FontSize, ImVec2(a.x + 9 * s, b.y - sm->FontSize - 7 * s), IM_COL32(225, 220, 240, 255), inst.c_str());
        dl->PopClipRect();
    }
}

// Instrument, mode, octave, volume and mute for the selected layer.
void drawLayerControls(Tenori& t, GuiState& gs, double now) {
    const float s = gs.scale;
    const int i = t.current();
    Layer& L = t.layer(i);
    const bool narrow = stableAvailX() < 930 * s;

    if (!narrow) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kLabel, "INSTRUMENT");
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(shade(soundGui(L.sound), 0.45f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(shade(soundGui(L.sound), 0.6f)));
    const std::string inst = t.instrumentName(i) + "##inst";
    if (ImGui::Button(inst.c_str(), ImVec2((narrow ? 150 : 200) * s, 0))) ImGui::OpenPopup("picker");
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose what layer %d plays", i + 1);
    if (ImGui::BeginPopup("picker")) {
        ImGui::TextColored(layerVec(i), "Instrument for layer %d", i + 1);
        ImGui::TextColored(kHint, "Picking one plays a note so you can hear it.");
        ImGui::Separator();
        drawInstrumentPicker(t, i, gs, now, 660 * s);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2((narrow ? 7 : 11) * s, ImGui::GetStyle().FramePadding.y));
    for (int m = 0; m < MODE_COUNT; ++m) {
        if (m) ImGui::SameLine(0, 3 * s);
        ImGui::PushID(m);
        if (coloredButton(modeName(m), ImGui::ColorConvertU32ToFloat4(modeGui(m)), ImVec2(0, 0), L.mode == m)) t.setMode(i, m);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s", modeName(m), modeHelp(m));
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    ImGui::SameLine(0, 14 * s);
    ImGui::BeginDisabled(L.octave <= -2);
    if (ImGui::ArrowButton("octdn", ImGuiDir_Left)) --L.octave;
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4 * s);
    ImGui::Text("Oct %+d", L.octave);
    ImGui::SameLine(0, 4 * s);
    ImGui::BeginDisabled(L.octave >= 2);
    if (ImGui::ArrowButton("octup", ImGuiDir_Right)) ++L.octave;
    ImGui::EndDisabled();

    ImGui::SameLine(0, 14 * s);
    ImGui::Text("Vol %d", L.volume + 1);
    ImGui::SameLine(0, 6 * s);
    ImGui::SetNextItemWidth((narrow ? 60 : 90) * s);
    int vol = L.volume + 1;
    if (ImGui::SliderInt("##vol", &vol, 1, 8, "")) L.volume = vol - 1;

    ImGui::SameLine(0, 8 * s);
    if (coloredButton(L.muted ? "Muted" : "Mute", kRed, ImVec2(0, 0), L.muted)) L.muted = !L.muted;
}

// ----------------------------------------------------------------- editors --

// Cell size that lets `rows` rows (plus a header and a line of text) fit in the
// height left in the panel, so the editor doesn't need scrolling.
float fitHeight(GuiState& gs, int rows) {
    const float s = gs.scale;
    const float left = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2 - 20 * s;
    return std::max(18 * s, left / rows);
}

// Row labels on the left (click to hear the note). idx = 7 - row.
float rowLabels(Tenori& t, int li, GuiState& gs, ImVec2 o, float cell, float labelW, double now) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = gs.scale;
    for (int y = 0; y < 8; ++y) {
        const int idx = 7 - y;
        const ImVec2 a(o.x, o.y + y * cell), b(o.x + labelW - 8 * s, o.y + (y + 1) * cell - std::max(2.f, cell * 0.1f));
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(1000 + y);
        if (ImGui::InvisibleButton("row", ImVec2(b.x - a.x, b.y - a.y))) t.audition(li, idx, now);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) dl->AddRectFilled(a, b, IM_COL32(58, 52, 80, 255), 6 * s);
        std::string name = t.rowName(li, idx);
        const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        dl->AddText(ImVec2(b.x - ts.x - 6 * s, (a.y + b.y - ts.y) * 0.5f), hov ? IM_COL32(245, 240, 255, 255) : kTextDim, name.c_str());
        if (hov) ImGui::SetTooltip("Click to hear it");
    }
    return labelW;
}

void drawScore(Tenori& t, int li, GuiState& gs, double now) {
    const float s = gs.scale;
    Layer& L = t.layer(li);
    Ui& u = ui();
    const float labelW = 110 * s;
    const float avail = stableAvailX();
    const float cell = std::clamp(std::min((avail - labelW) / 16.f, fitHeight(gs, 8)), 18 * s, 46 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float headH = ImGui::GetTextLineHeight() + 6 * s;
    const int ph = t.scoreStep(li, now);
    Grid g{ImVec2(top.x + labelW, top.y + headH), cell, std::max(2.f, cell * 0.1f), 16, 8};

    for (int c = 0; c < 16; ++c) {
        const std::string n = std::to_string(c + 1);
        const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
        const ImU32 col = c == ph ? IM_COL32(255, 196, 92, 255) : (c < L.length ? kTextDim : IM_COL32(100, 94, 122, 255));
        dl->AddText(ImVec2(g.o.x + c * cell + (cell - g.gap - ts.x) * 0.5f, top.y), col, n.c_str());
    }
    rowLabels(t, li, gs, ImVec2(top.x, g.o.y), cell, labelW, now);

    ImGui::SetCursorScreenPos(g.o);
    ImGui::InvisibleButton("score", ImVec2(cell * 16, cell * 8));
    int hc = -1, hr = -1;
    const bool over = g.at(ImGui::GetMousePos(), hc, hr);
    if (ImGui::IsItemActivated() && over) {
        const bool on = L.score[hr] & (1u << hc);
        u.paint = on ? 0 : 1;
        t.toggleScore(li, hc, hr, now);
    } else if (ImGui::IsItemActive() && over && u.paint >= 0) {
        const bool on = L.score[hr] & (1u << hc);
        if (on != (u.paint == 1)) t.toggleScore(li, hc, hr, now);
    }
    if (!ImGui::IsItemActive()) u.paint = -1;

    const ImU32 lc = kLayerGui[li];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) {
            const ImVec2 a = g.a(c, r), b = g.b(c, r);
            const bool on = L.score[r] & (1u << c), inLoop = c < L.length;
            ImU32 col = on ? lc : ((c / 4) % 2 ? kCellBeat : kCell);
            if (!inLoop) col = shade(col, 0.5f);
            dl->AddRectFilled(a, b, col, cell * 0.22f);
            if (c == ph) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, on ? 70 : 28), cell * 0.22f);
            const double age = now - t.lastHit(li, c, r);
            if (on && age >= 0.0 && age < 0.3) {
                const float k = static_cast<float>(1.0 - age / 0.3);
                dl->AddRect(ImVec2(a.x - 3 * s * k, a.y - 3 * s * k), ImVec2(b.x + 3 * s * k, b.y + 3 * s * k),
                            withAlpha(IM_COL32(255, 255, 255, 255), static_cast<int>(200 * k)), cell * 0.25f, 0, 2 * s);
            }
            if (over && c == hc && r == hr && ImGui::IsItemHovered()) dl->AddRect(a, b, IM_COL32(240, 235, 255, 200), cell * 0.22f, 0, 1.5f * s);
        }
    // Loop end marker.
    if (L.length < 16) {
        const float x = g.o.x + L.length * cell - g.gap * 0.5f;
        dl->AddLine(ImVec2(x, g.o.y - 4 * s), ImVec2(x, g.o.y + 8 * cell + 2 * s), IM_COL32(255, 196, 92, 255), 2 * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(top.x, g.o.y + 8 * cell + 8 * s));
}

void drawBounce(Tenori& t, int li, GuiState& gs, double now) {
    const float s = gs.scale;
    Layer& L = t.layer(li);
    const float labelW = 110 * s;
    const float cell = std::clamp(std::min((stableAvailX() - labelW) / 8.f, fitHeight(gs, 8)), 24 * s, 56 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float headH = ImGui::GetTextLineHeight() + 6 * s;
    Grid g{ImVec2(top.x + labelW, top.y + headH), cell, std::max(2.f, cell * 0.1f), 8, 8};
    // Columns are notes; rows say how often a ball dropped from there lands.
    ImFont* sm = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    for (int c = 0; c < 8; ++c) {
        const std::string n = t.rowName(li, c);
        const ImVec2 ts = sm->CalcTextSizeA(sm->FontSize, 1e9f, 0.f, n.c_str());
        dl->AddText(sm, sm->FontSize, ImVec2(g.o.x + c * cell + (cell - g.gap - ts.x) * 0.5f, top.y + 3 * s), kTextDim, n.c_str());
    }
    for (int r = 0; r < 8; ++r) {
        char tx[32];
        if (r == 7) std::snprintf(tx, sizeof tx, "every step");
        else std::snprintf(tx, sizeof tx, "every %d", 2 * (7 - r));
        const ImVec2 ts = ImGui::CalcTextSize(tx);
        dl->AddText(ImVec2(g.o.x - ts.x - 14 * s, g.o.y + r * cell + (cell - g.gap - ts.y) * 0.5f), IM_COL32(130, 124, 152, 255), tx);
    }
    ImGui::SetCursorScreenPos(g.o);
    if (ImGui::InvisibleButton("bounce", ImVec2(cell * 8, cell * 8))) {}
    int hc, hr;
    const bool over = g.at(ImGui::GetMousePos(), hc, hr);
    if (ImGui::IsItemActivated() && over) t.tapBounce(li, hc, hr, now);
    const bool hovGrid = ImGui::IsItemHovered();
    const ImU32 lc = kLayerGui[li];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            const ImVec2 a = g.a(c, r), b = g.b(c, r);
            ImU32 col = r == 7 ? kCellBeat : kCell;
            if (L.ball[c] >= 0 && r >= L.ball[c]) col = mixCol(col, lc, 0.18f);  // the ball's lane
            dl->AddRectFilled(a, b, col, cell * 0.22f);
            if (r == L.ball[c]) dl->AddRect(a, b, withAlpha(lc, 200), cell * 0.22f, 0, 2 * s);
            if (hovGrid && over && c == hc && r == hr) dl->AddRect(a, b, IM_COL32(240, 235, 255, 200), cell * 0.22f, 0, 1.5f * s);
        }
    for (int c = 0; c < 8; ++c) {
        if (L.ball[c] < 0) continue;
        const double age = now - t.lastHit(li, c, 7);
        if (age >= 0.0 && age < 0.25) {
            const float k = static_cast<float>(1.0 - age / 0.25);
            dl->AddRectFilled(g.a(c, 7), g.b(c, 7), withAlpha(IM_COL32(255, 255, 255, 255), static_cast<int>(150 * k)), cell * 0.22f);
        }
        const int y = std::clamp(t.playing() ? L.ballY[c] : L.ball[c], 0, 7);
        const ImVec2 a = g.a(c, y), b = g.b(c, y);
        dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.x - a.x) * 0.36f, lc);
    }
    ImGui::SetCursorScreenPos(ImVec2(top.x, g.o.y + 8 * cell + 8 * s));
}

void drawPath(Tenori& t, int li, GuiState& gs, double now) {
    const float s = gs.scale;
    Layer& L = t.layer(li);
    const float labelW = 110 * s;
    const float cell = std::clamp(std::min((stableAvailX() - labelW) / 8.f, fitHeight(gs, 8)), 24 * s, 56 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    Grid g{ImVec2(top.x + labelW, top.y + 4 * s), cell, std::max(2.f, cell * 0.1f), 8, 8};
    rowLabels(t, li, gs, ImVec2(top.x, g.o.y), cell, labelW, now);
    ImGui::SetCursorScreenPos(g.o);
    ImGui::InvisibleButton("path", ImVec2(cell * 8, cell * 8));
    int hc, hr;
    const bool over = g.at(ImGui::GetMousePos(), hc, hr);
    if (ImGui::IsItemActivated() && over) t.tapPath(li, hc, hr, now);
    const bool hovGrid = ImGui::IsItemHovered();
    const ImU32 lc = kLayerGui[li];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            dl->AddRectFilled(g.a(c, r), g.b(c, r), kCell, cell * 0.22f);
            if (hovGrid && over && c == hc && r == hr) dl->AddRect(g.a(c, r), g.b(c, r), IM_COL32(240, 235, 255, 200), cell * 0.22f, 0, 1.5f * s);
        }
    auto centre = [&](std::pair<int, int> p) {
        const ImVec2 a = g.a(p.first, p.second), b = g.b(p.first, p.second);
        return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    };
    const int n = static_cast<int>(L.path.size());
    for (int i = 0; i + 1 < n || (n > 2 && i < n); ++i)
        dl->AddLine(centre(L.path[static_cast<size_t>(i)]), centre(L.path[static_cast<size_t>((i + 1) % n)]), withAlpha(lc, 110), 3 * s);
    for (int i = 0; i < n; ++i) {
        const auto p = L.path[static_cast<size_t>(i)];
        const ImVec2 c = centre(p);
        const double age = now - t.lastHit(li, p.first, p.second);
        const float k = age >= 0.0 && age < 0.3 ? static_cast<float>(1.0 - age / 0.3) : 0.f;
        if (k > 0.f) dl->AddCircleFilled(c, cell * (0.36f + 0.14f * k), withAlpha(IM_COL32(255, 255, 255, 255), static_cast<int>(120 * k)));
        dl->AddCircleFilled(c, cell * 0.34f, lc);
        const std::string num = std::to_string(i + 1);
        centeredText(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(c.x - cell, c.y - cell), ImVec2(c.x + cell, c.y + cell), kInk, num.c_str());
    }
    if (t.playing() && n > 1 && L.pathFrom < n && L.pathAt < n && L.legTo > L.legFrom) {
        const ImVec2 a = centre(L.path[static_cast<size_t>(L.pathFrom)]), b = centre(L.path[static_cast<size_t>(L.pathAt)]);
        const float k = static_cast<float>(std::clamp((now - L.legFrom) / (L.legTo - L.legFrom), 0.0, 1.0));
        const ImVec2 p(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k);
        dl->AddCircleFilled(p, cell * 0.16f, IM_COL32(255, 255, 255, 235));
        dl->AddCircle(p, cell * 0.24f, withAlpha(lc, 200), 0, 2 * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(top.x, g.o.y + 8 * cell + 8 * s));
    ImGui::TextColored(kHint, n ? "%d of %d points. The light visits them in number order." : "No points yet: click squares to place some.",
                       n, kPathMax);
}

void drawPush(Tenori& t, int li, GuiState& gs, double now) {
    const float s = gs.scale;
    Ui& u = ui();
    const float labelW = 110 * s;
    const float cell = std::clamp(std::min((stableAvailX() - labelW) / 8.f, fitHeight(gs, 8)), 24 * s, 56 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float headH = ImGui::GetTextLineHeight() + 6 * s;
    Grid g{ImVec2(top.x + labelW, top.y + headH), cell, std::max(2.f, cell * 0.1f), 8, 8};
    ImFont* sm = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    for (int c = 0; c < 8; ++c) {
        const char* n = pushRateName(c);
        const ImVec2 ts = sm->CalcTextSizeA(sm->FontSize, 1e9f, 0.f, n);
        dl->AddText(sm, sm->FontSize, ImVec2(g.o.x + c * cell + (cell - g.gap - ts.x) * 0.5f, top.y + 3 * s), kTextDim, n);
    }
    rowLabels(t, li, gs, ImVec2(top.x, g.o.y), cell, labelW, now);
    ImGui::SetCursorScreenPos(g.o);
    ImGui::InvisibleButton("push", ImVec2(cell * 8, cell * 8));
    int hc, hr;
    const bool over = g.at(ImGui::GetMousePos(), hc, hr);
    // Hold with the mouse; slide to another square to move your finger.
    if (ImGui::IsItemActive() && over && (hc != u.pushX || hr != u.pushY || u.pushLayer != li)) {
        if (u.pushX >= 0) t.pushUp(u.pushLayer, u.pushX, u.pushY);
        u.pushX = hc;
        u.pushY = hr;
        u.pushLayer = li;
        t.pushDown(li, hc, hr, now);
    }
    if (!ImGui::IsItemActive() && u.pushX >= 0) {
        t.pushUp(u.pushLayer, u.pushX, u.pushY);
        u.pushX = u.pushY = u.pushLayer = -1;
    }
    const bool hovGrid = ImGui::IsItemHovered();
    const ImU32 lc = kLayerGui[li];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            const ImVec2 a = g.a(c, r), b = g.b(c, r);
            const bool held = t.pushHeld(li, c, r);
            dl->AddRectFilled(a, b, held ? shade(lc, 0.75f) : kCell, cell * 0.22f);
            const double age = now - t.lastHit(li, c, r);
            if (age >= 0.0 && age < 0.15) {
                const float k = static_cast<float>(1.0 - age / 0.15);
                dl->AddRectFilled(a, b, withAlpha(lc, static_cast<int>(255 * k)), cell * 0.22f);
            }
            if (hovGrid && over && c == hc && r == hr) dl->AddRect(a, b, IM_COL32(240, 235, 255, 200), cell * 0.22f, 0, 1.5f * s);
        }
    ImGui::SetCursorScreenPos(ImVec2(top.x, g.o.y + 8 * cell + 8 * s));
    ImGui::TextColored(kHint, "Hold a square to repeat it (it plays straight away, then on the beat). Several squares can be held on the Launchpad.");
}

void drawEditor(Tenori& t, GuiState& gs, double now) {
    const float s = gs.scale;
    const int li = t.current();
    Layer& L = t.layer(li);
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::PushFont(gs.bigFont);
    ImGui::TextColored(layerVec(li), "Layer %d", li + 1);
    ImGui::PopFont();
    ImGui::SameLine(0, 12 * s);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  -  %s", t.instrumentName(li).c_str(), modeName(L.mode));
    if (L.mode == MODE_SCORE) {
        ImGui::SameLine(0, 18 * s);
        ImGui::TextColored(kLabel, "LOOP");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * s);
        ImGui::SliderInt("##len", &L.length, 1, kTSteps, "%d steps");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layers with different lengths drift against each other");
    }
    ImGui::SameLine(0, 18 * s);
    ImGui::BeginDisabled(L.empty());
    if (ImGui::Button("Clear")) t.clearLayer(li);
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4 * s);
    ImGui::BeginDisabled(!t.canUndo());
    if (ImGui::Button("Undo")) t.undo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Brings back what the last Clear removed (again to redo)");
    ImGui::TextColored(kHint, "%s", modeHelp(L.mode));
    ImGui::Spacing();
    switch (L.mode) {
        case MODE_SCORE: drawScore(t, li, gs, now); break;
        case MODE_BOUNCE: drawBounce(t, li, gs, now); break;
        case MODE_PATH: drawPath(t, li, gs, now); break;
        default: drawPush(t, li, gs, now); break;
    }
}

// ----------------------------------------------------------------- pages --

void drawLayersPage(Tenori& t, GuiState& gs, double now) {
    drawLayerCards(t, gs, now);
    ImGui::Dummy(ImVec2(0, 4 * gs.scale));
    drawLayerControls(t, gs, now);
    drawEditor(t, gs, now);
}

void drawInstrumentsPage(App& app, Tenori& t, GuiState& gs, double now) {
    const float s = gs.scale;
    ImGui::TextColored(kHint, "Pick the instrument for each layer. Every layer has its own synth, so they can all be different.");
    ImGui::Spacing();
    for (int i = 0; i < kLayers; ++i) {
        if (i) ImGui::SameLine(0, 4 * s);
        ImGui::PushID(i);
        char lbl[48];
        std::snprintf(lbl, sizeof lbl, "Layer %d", i + 1);
        if (coloredButton(lbl, layerVec(i), ImVec2(0, 0), t.current() == i)) t.selectLayer(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.instrumentName(i).c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    drawInstrumentPicker(t, t.current(), gs, now, stableAvailX());

    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SeparatorText("MIDI keyboard");
    ImGui::TextColored(kHint, "Other MIDI keyboards play their own synth over the top (same sounds, set here).");
    ImGui::SetNextItemWidth(260 * s);
    const auto& lps = leadPatches();
    const int cur = std::clamp(app.leadPatch(), 0, static_cast<int>(lps.size()) - 1);
    if (ImGui::BeginCombo("Keyboard sound", lps[static_cast<size_t>(cur)].name.c_str())) {
        for (size_t i = 0; i < lps.size(); ++i)
            if (ImGui::Selectable(lps[i].name.c_str(), static_cast<int>(i) == cur)) app.setLeadPatch(static_cast<int>(i));
        ImGui::EndCombo();
    }
}

void drawEffectsPage(App& app, Tenori& t, GuiState& gs) {
    const float s = gs.scale;
    Shared& sh = app.shared();
    Ui& u = ui();
    const Style& st = styles()[static_cast<size_t>(t.style())];
    ImGui::TextColored(kHint, "Hold a button to play the effect, let go to stop. Each style has its own set (on the Launchpad: hold Fx).");
    ImGui::Spacing();
    const float avail = stableAvailX(), gap = 6 * s;
    const float w = (avail - gap * 7) / 8.f, h = std::min(w * 0.85f, 86 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int pad = 0; pad < 16; ++pad) {
        const int f = st.fx[static_cast<size_t>(pad)];
        if (pad % 8) ImGui::SameLine(0, gap);
        ImGui::PushID(pad);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("fx", ImVec2(w, h));
        if (ImGui::IsItemActivated() && f >= 0 && f < FX_COUNT) {
            sh.send(Cmd::FxOn, f);
            u.fxHeld[pad] = true;
            u.fxSent[pad] = f;
        }
        if (ImGui::IsItemDeactivated() && u.fxHeld[pad]) {
            sh.send(Cmd::FxOff, u.fxSent[pad]);
            u.fxHeld[pad] = false;
        }
        if (ImGui::IsItemHovered() && f >= 0 && f < FX_COUNT) ImGui::SetTooltip("%s\n%s", fxName(f), kFxHelp[f]);
        const bool held = u.fxHeld[pad], hov = ImGui::IsItemHovered();
        const ImVec2 b(a.x + w, a.y + h);
        const ImU32 base = guiColor(fxColor(f));
        dl->AddRectFilled(a, b, held ? base : withAlpha(base, 70), 20 * s);
        if (hov) dl->AddRect(a, b, IM_COL32(240, 235, 255, 230), 20 * s, 0, 2.f * s);
        centeredText(dl, ImGui::GetFont(), ImGui::GetFontSize(), a, b, held ? kInk : IM_COL32(245, 240, 255, 255),
                     f >= 0 && f < FX_COUNT ? kFxTitle[f] : "");
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Effect settings");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Delay time");
    ImGui::SameLine();
    {
        const char* divs[DLY_COUNT];
        for (int i = 0; i < DLY_COUNT; ++i) divs[i] = delayDivName(i);
        const int c = pillRow("dly", divs, DLY_COUNT, sh.delayDiv.load(), kLilac, s);
        if (c >= 0) sh.delayDiv.store(c);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Echoes");
    ImGui::SameLine();
    resetSlider("##fb", sh.delayFeedback, 0.f, 0.9f, 0.55f, "%.2f", 140 * s);
    ImGui::SameLine(0, 18 * s);
    ImGui::TextUnformatted("Reverb size");
    ImGui::SameLine();
    resetSlider("##rs", sh.reverbSize, 0.f, 1.f, 0.6f, "%.2f", 140 * s);
    ImGui::SameLine(0, 18 * s);
    ImGui::TextUnformatted("Chorus");
    ImGui::SameLine();
    resetSlider("##ch", sh.chorus, 0.f, 1.f, 0.35f, "%.2f", 140 * s);
    ImGui::TextColored(kHint, "The layer synths go through the Mixer's Lead channel. Right-click a slider to reset it.");
}

void handleKeys(Tenori& t, double now) {
    if (ImGui::GetIO().WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) t.setPlaying(!t.playing(), now);
    for (int i = 0; i < kLayers; ++i)
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) t.selectLayer(i);
}

}  // namespace

void drawTenoriGui(App& app, Tenori& t, GuiState& gs, double now) {
    const float s = gs.scale;
    Ui& u = ui();
    handleKeys(t, now);

    // Files dragged onto the window: a folder = a drum kit, WAVs = the Mixer row
    // under the mouse (else the drums in order from the first).
    if (!gs.dropped.empty()) {
        const int style = t.style();
        int slot = 0;
        if (u.tab == 2)
            for (int k = 0; k < kDrumSlots; ++k)
                if (gs.dropY >= gs.rowY0[k] && gs.dropY < gs.rowY1[k]) slot = k;
        for (const std::string& f : gs.dropped) {
            std::error_code ec;
            if (std::filesystem::is_directory(std::filesystem::u8path(f), ec)) app.loadKitFolder(style, f);
            else if (slot < kDrumSlots && app.setDrumSample(style, slot, f)) ++slot;
        }
        gs.dropped.clear();
        u.tab = 2;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("LP-TENORI", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    drawTopBar(app, t, gs, now);
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = std::clamp(avail * 0.36f, 340 * s, 560 * s);
    ImGui::BeginChild("launchpad", ImVec2(leftW, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawLaunchpad(app, t, gs, leftW - 2 * ImGui::GetStyle().WindowPadding.x - 10 * s, now);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    const float logH = ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::BeginChild("right", ImVec2(0, ImGui::GetContentRegionAvail().y - logH - ImGui::GetStyle().ItemSpacing.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const char* names[6] = {"Layers", "Instruments", "Mixer", "Effects", "Devices", "About"};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18 * s, 10 * s));
        for (int i = 0; i < 6; ++i) {
            if (i) ImGui::SameLine(0, 6 * s);
            if (coloredButton(names[i], kLilac, ImVec2(0, 0), u.tab == i)) u.tab = i;
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 4 * s));
    }
    switch (u.tab) {
        case 0: drawLayersPage(t, gs, now); break;
        case 1: drawInstrumentsPage(app, t, gs, now); break;
        case 2: drawMixer(app, gs); break;
        case 3: drawEffectsPage(app, t, gs); break;
        case 4: drawDevices(app, gs, now); break;
        default: drawAbout(app, gs); break;
    }
    ImGui::EndChild();

    ImGui::BeginChild("log", ImVec2(0, logH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    for (const auto& l : Log::get().tail(2)) ImGui::TextColored(kHint, "%s", l.c_str());
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::End();
}

}  // namespace dnb
