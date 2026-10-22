#include "gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "log.h"
#include "styles.h"

namespace dnb {
namespace {

// ------------------------------------------------------------ palette --

// Soft dark violet base with candy accents. Text stays high-contrast.
const ImU32 kBg = IM_COL32(26, 23, 36, 255);
const ImU32 kPanel = IM_COL32(37, 33, 51, 255);
const ImU32 kWell = IM_COL32(29, 26, 41, 255);       // recessed areas (Launchpad body, meters)
const ImU32 kCell = IM_COL32(52, 47, 70, 255);       // empty step
const ImU32 kCellBeat = IM_COL32(64, 58, 86, 255);   // empty step on the beat
const ImU32 kPadOff = IM_COL32(60, 55, 80, 255);
const ImU32 kPadEdge = IM_COL32(84, 77, 108, 255);
const ImU32 kTextDim = IM_COL32(172, 165, 196, 255);
const ImVec4 kAccent = ImVec4(1.00f, 0.66f, 0.40f, 1.f);  // peach
const ImVec4 kGreen = ImVec4(0.42f, 0.88f, 0.62f, 1.f);   // mint
const ImVec4 kRed = ImVec4(1.00f, 0.43f, 0.52f, 1.f);     // rose
const ImVec4 kLilac = ImVec4(0.72f, 0.62f, 1.00f, 1.f);
const ImVec4 kLabel = ImVec4(0.70f, 0.66f, 0.80f, 1.f);
const ImVec4 kHint = ImVec4(0.58f, 0.55f, 0.68f, 1.f);

const char* kFxShort[FX_COUNT] = {"Stut\n1/4", "Stut\n1/8", "Stut\n1/16", "Stut\n1/32", "Tape\nstop", "Half\ntime",
                                  "Rev", "Delay", "Low\npass", "High\npass", "Crush", "Flange", "Verb", "Siren",
                                  "Riser", "Horn", "Pump", "Gate", "Lo-fi", "Laser"};
const char* kFxTitle[FX_COUNT] = {"Stutter\n1/4", "Stutter\n1/8", "Stutter\n1/16", "Stutter\n1/32",
                                  "Tape\nstop", "Half-\ntime", "Reverse", "Delay\nthrow",
                                  "Low-pass\nsweep", "High-pass\nsweep", "Bitcrush", "Flanger",
                                  "Reverb\nwash", "Dub\nsiren", "Noise\nriser", "Air\nhorn",
                                  "Sidechain\npump", "Trance\ngate", "Lo-fi\nvinyl", "Laser\nzap"};
const char* kFxHelp[FX_COUNT] = {
    "Loops the last quarter note", "Loops the last eighth note", "Loops the last 16th note",
    "Loops the last 32nd note (glitchy roll)", "Winds the audio down to a stop", "Sequencer plays at half speed",
    "Plays the last beat backwards", "Dotted-eighth ping-pong echoes", "Resonant low-pass sweep on everything",
    "Resonant high-pass sweep on everything", "Sample-rate and bit reduction", "Jet-plane flanger sweep",
    "Big reverb wash with a long tail", "Dub siren into the delay", "Noise riser: hold it to build, release to drop",
    "Air horn", "Ducks the whole mix on every beat, like heavy sidechain compression",
    "Chops the mix into 16th notes", "Dark, warbly, crackly old vinyl", "Pew-pew zaps every 8th note into the delay"};
const char* kTopLabel[8] = {"Play", "View", "BPM-", "BPM+", "1", "2", "3", "4"};

// GUI colour for an LED colour (so tracks match their Launchpad pads).
ImU32 guiColor(Color c) {
    const int r = static_cast<int>(c.r + 0.5f), g = static_cast<int>(c.g * 2.f + 0.5f);  // g in half steps
    if (r >= 3 && g == 0) return IM_COL32(255, 112, 128, 255);   // red -> strawberry
    if (r >= 3 && g >= 5) return IM_COL32(255, 196, 92, 255);    // amber -> honey
    if (r >= 3) return IM_COL32(255, 150, 100, 255);             // orange -> peach
    if (r == 0) return IM_COL32(110, 225, 150, 255);             // green -> mint
    if (r == 2) return IM_COL32(250, 226, 112, 255);             // yellow -> lemon
    return IM_COL32(190, 235, 120, 255);                         // lime -> apple
}
ImU32 trackColor(const Style& s, int t) {
    if (t == T_BASS) return IM_COL32(100, 215, 215, 255);   // aqua
    if (t == T_CHORD) return IM_COL32(190, 160, 255, 255);  // lilac
    return guiColor(slotColor(s, t));
}

// Launchpad LED value (red 0..3 | green 0..3 << 4) to a screen colour that
// looks like the real thing: red + green LEDs mix into amber / yellow.
ImU32 ledColor(uint8_t v) {
    const int r = v & 3, g = (v >> 4) & 3;
    if (!r && !g) return kPadOff;
    int R = r ? 80 + 58 * r : 20 * g;
    int G = g ? static_cast<int>((70 + 60 * g) * (r ? 0.85f : 1.f)) : 15 * r;
    auto soft = [](int c) { return std::min(255, static_cast<int>(c * 0.86f + 255 * 0.14f)); };
    return IM_COL32(soft(std::min(R, 255)), soft(std::min(G, 255)), soft(40), 255);
}
bool ledLit(uint8_t v) { return (v & 0x33) != 0; }

ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24); }
ImU32 scaleCol(ImU32 c, float k) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x * k, v.y * k, v.z * k, v.w));
}

std::string firstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

void centeredText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 col, const char* text) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { lines.push_back(cur); cur.clear(); }
        else cur += *p;
    }
    lines.push_back(cur);
    const float lh = size * 1.05f;
    float y = (a.y + b.y) * 0.5f - lh * lines.size() * 0.5f;
    for (auto& l : lines) {
        ImVec2 ts = font->CalcTextSizeA(size, 1e9f, 0.f, l.c_str());
        dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, y), col, l.c_str());
        y += lh;
    }
}

bool coloredButton(const char* label, ImVec4 col, ImVec2 size, bool active) {
    ImVec4 base = active ? col : ImVec4(0.25f, 0.23f, 0.34f, 1.f);
    ImVec4 hov = active ? ImVec4(std::min(1.f, col.x * 1.08f), std::min(1.f, col.y * 1.08f), std::min(1.f, col.z * 1.08f), 1.f)
                        : ImVec4(0.32f, 0.29f, 0.44f, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(0.13f, 0.10f, 0.18f, 1.f) : ImVec4(0.94f, 0.92f, 0.98f, 1.f));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void statusDot(ImU32 col, float s) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 7 * s, p.y + h * 0.5f), 7 * s, withAlpha(col, 60));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 7 * s, p.y + h * 0.5f), 4.5f * s, col);
    ImGui::Dummy(ImVec2(18 * s, h));
    ImGui::SameLine();
}

// Width available in a scrolling panel, *as if the scrollbar were always showing*.
// Laying out against this keeps the layout identical whether or not the scrollbar
// is visible. Otherwise taller rows can bring in the scrollbar, which narrows the
// rows, which removes the scrollbar... and the whole panel shakes every frame.
float stableAvailX() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float full = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - st.WindowPadding.x - st.ScrollbarSize;
    return std::max(50.f, full - ImGui::GetCursorScreenPos().x);
}

ImVec4 styleAccent(const Style& st) { return ImGui::ColorConvertU32ToFloat4(ledColor(st.led)); }

// ------------------------------------------------------------ sections --

void drawTopBar(App& app, GuiState& gs, double now) {
    Shared& sh = app.shared();
    Controller& ctl = app.controller();
    const float s = gs.scale;
    const bool playing = sh.playing.load();
    const int pat = sh.pattern.load(), styleIdx = pat / kMaxSlots, queued = sh.queued.load();
    const Style& st = styleOf(pat);

    {   // Big pill Play / Stop button with a drawn icon.
        const ImVec2 sz(132 * s, 52 * s);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 6 * s));  // centre it against the labelled controls
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
        const float fs = big->FontSize;
        const ImVec2 ts = big->CalcTextSizeA(fs, 1e9f, 0.f, txt);
        dl->AddText(big, fs, ImVec2(a.x + 50 * s + (sz.x - 50 * s - ts.x) * 0.45f, cy - ts.y * 0.5f), ink, txt);
        if (pressed) {
            app.virtualPress(0, -1, true);
            app.virtualPress(0, -1, false);
        }
        if (hov) ImGui::SetTooltip("Space bar works too");
        ImGui::EndGroup();
    }
    ImGui::SameLine(0, 16 * s);

    // Style
    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "STYLE");
    ImGui::SetNextItemWidth(170 * s);
    const bool stylePending = queued >= 0 && queued / kMaxSlots != styleIdx;
    std::string preview = st.name;
    if (stylePending) preview += "  > " + styleOf(queued).name;
    if (ImGui::BeginCombo("##style", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < styleCount(); ++i) {
            const Style& o = styles()[static_cast<size_t>(i)];
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "%s##st%d", o.name.c_str(), i);
            ImGui::PushStyleColor(ImGuiCol_Text, styleAccent(o));
            if (ImGui::Selectable(lbl, i == styleIdx)) ctl.chooseStyle(i);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s, %.0f BPM", o.blurb.c_str(), o.keyName.c_str(), o.bpm);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Or hold View on the Launchpad and tap a pad in the top half");
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Tempo
    ImGui::BeginGroup();
    float bpm = sh.bpm.load();
    ImGui::TextColored(kLabel, "TEMPO");
    ImGui::SameLine();
    ImGui::Text("%.0f BPM", bpm);
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::SliderFloat("##bpm", &bpm, 60.f, 200.f, "")) sh.bpm.store(std::round(bpm));
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Patterns of this style (built-in + your own)
    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "PATTERN");
    {
        const int count = sh.slotCount[styleIdx].load();
        std::string pv = std::to_string(pat % kMaxSlots + 1) + "  " + sh.patternName[pat];
        if (queued >= 0 && queued / kMaxSlots == styleIdx && queued != pat) pv += "  > " + sh.patternName[queued];
        ImGui::SetNextItemWidth(200 * s);
        if (ImGui::BeginCombo("##pattern", pv.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int k = 0; k < count; ++k) {
                if (k == kFactorySlots) ImGui::SeparatorText("Your patterns");
                const int gi = styleIdx * kMaxSlots + k;
                char lbl[128];
                std::snprintf(lbl, sizeof lbl, "%d  %s##p%d", k + 1, sh.patternName[gi].c_str(), k);
                if (ImGui::Selectable(lbl, gi == pat)) ctl.choosePattern(k);
            }
            ImGui::Separator();
            if (count < kMaxSlots && ImGui::Selectable("+ New pattern...")) gs.openNew = true;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switches at the end of the bar while playing");
        ImGui::SameLine(0, 4 * s);
        if (ImGui::Button("+##newpat") && count < kMaxSlots) gs.openNew = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New pattern in %s", st.name.c_str());
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 12 * s);

    // Volume + meter
    ImGui::BeginGroup();
    float vol = sh.volume.load();
    ImGui::TextColored(kLabel, "VOLUME");
    ImGui::SameLine();
    ImGui::Text("%d%%", static_cast<int>(std::round(vol * 100.f)));
    ImGui::SetNextItemWidth(130 * s);
    if (ImGui::SliderFloat("##vol", &vol, 0.f, 1.f, "")) sh.volume.store(vol);
    ImGui::EndGroup();
    ImGui::SameLine(0, 8 * s);

    ImGui::BeginGroup();
    ImGui::TextColored(kLabel, "LEVEL");
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 90 * s, h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kWell, h * 0.5f);
        const float lv = std::min(1.f, sh.level.load());
        const int segs = 12;
        for (int i = 0; i < segs; ++i) {
            float x0 = p.x + 10 * s + i * (w - 20 * s) / segs;
            float x1 = x0 + (w - 20 * s) / segs - 3 * s;
            bool on = lv * segs > i;
            ImU32 c = i < 8 ? IM_COL32(110, 225, 150, 255) : (i < 11 ? IM_COL32(255, 196, 92, 255) : IM_COL32(255, 112, 128, 255));
            dl->AddRectFilled(ImVec2(x0, p.y + 9 * s), ImVec2(x1, p.y + h - 9 * s), on ? c : withAlpha(c, 45), 3 * s);
        }
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::EndGroup();
}

void drawLaunchpad(App& app, GuiState& gs, float width, double now) {
    const float s = gs.scale;
    MidiHub& midi = app.midi();
    Controller& ctl = app.controller();
    const Style& st = styleOf(app.shared().pattern.load());

    if (midi.launchpadConnected()) {
        bool act = now - midi.launchpadLastActivity() < 0.12;
        statusDot(act ? IM_COL32(255, 200, 60, 255) : IM_COL32(80, 220, 90, 255), s);
        ImGui::Text("Launchpad connected");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1), "(%s)", midi.launchpadInName().c_str());
    } else {
        statusDot(std::fmod(now, 1.4) < 0.7 ? IM_COL32(120, 120, 130, 255) : IM_COL32(60, 60, 68, 255), s);
        ImGui::Text("No Launchpad - plug it in, it will connect automatically");
    }
    ImGui::Spacing();

    const float gap = 0.14f;
    const float pitch = std::min(width / 9.3f, (ImGui::GetContentRegionAvail().y - 70 * s) / 9.4f);
    const float pad = pitch / (1.f + gap);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float topGap = pitch * 0.18f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    const float labelSize = std::max(11.5f * s, std::min(pad * 0.27f, 16.f * s));
    const Frame& fr = app.frame();
    const bool picker = ctl.stylePickerShown();

    dl->AddRectFilled(ImVec2(o.x - 14 * s, o.y - 14 * s), ImVec2(o.x + pitch * 9 + topGap + 14 * s, o.y + pitch * 9 + topGap + 14 * s),
                      kWell, 26 * s);

    auto button = [&](int x, int y, ImVec2 a, ImVec2 b) {
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(y * 16 + x + 32);
        ImGui::InvisibleButton("pad", ImVec2(b.x - a.x, b.y - a.y));
        if (ImGui::IsItemActivated()) app.virtualPress(x, y, true);
        if (ImGui::IsItemDeactivated()) app.virtualPress(x, y, false);
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        return hov;
    };

    // Top row: round buttons
    for (int x = 0; x < 8; ++x) {
        ImVec2 c(o.x + x * pitch + pad * 0.5f, o.y + pad * 0.5f);
        float r = pad * 0.36f;
        bool hov = button(x, -1, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        uint8_t v = fr[72 + x];
        if (ledLit(v)) dl->AddCircleFilled(c, r + 4 * s, withAlpha(ledColor(v), 70));
        dl->AddCircleFilled(c, r, ledColor(v));
        dl->AddCircle(c, r, hov ? IM_COL32(240, 235, 255, 255) : kPadEdge, 0, (hov ? 2.f : 1.2f) * s);
        const char* lbl = x == 1 && picker ? "Style" : kTopLabel[x];
        centeredText(dl, small, labelSize * 0.9f, ImVec2(c.x - r, c.y + r + 1 * s), ImVec2(c.x + r, c.y + r + labelSize + 3 * s),
                     kTextDim, lbl);
        if (hov && x == 1) ImGui::SetTooltip("Tap: switch PERFORM / EDIT view\nHold: pick a style on the top half");
    }

    // Grid + right column
    for (int y = 0; y < 8; ++y) {
        const float py = o.y + (y + 1) * pitch + topGap;
        for (int x = 0; x < 8; ++x) {
            ImVec2 a(o.x + x * pitch, py), b(a.x + pad, py + pad);
            bool hov = button(x, y, a, b);
            uint8_t v = fr[y * 8 + x];
            bool lit = ledLit(v);
            const float pr = pad * 0.27f;  // soft, squircle-ish pads
            if (lit) dl->AddRectFilled(ImVec2(a.x - 4 * s, a.y - 4 * s), ImVec2(b.x + 4 * s, b.y + 4 * s), withAlpha(ledColor(v), 55), pr + 4 * s);
            dl->AddRectFilled(a, b, ledColor(v), pr);
            if (lit) dl->AddRectFilled(ImVec2(a.x + pad * 0.14f, a.y + pad * 0.08f), ImVec2(b.x - pad * 0.14f, a.y + pad * 0.2f),
                                       IM_COL32(255, 255, 255, 45), pad * 0.06f);  // little shine
            dl->AddRect(a, b, hov ? IM_COL32(240, 235, 255, 255) : (lit ? withAlpha(ledColor(v), 255) : kPadEdge), pr, 0, (hov ? 2.f : 1.2f) * s);
            std::string label;
            if (picker && y < 2) {
                const int i = y * 8 + x;
                if (i < styleCount()) label = styles()[static_cast<size_t>(i)].padLabel;
            } else if (picker && y < 4) {
                const int k = (y - 2) * 8 + x, styleIdx = app.shared().pattern.load() / kMaxSlots;
                if (k < app.shared().slotCount[styleIdx].load()) {
                    // Short two-line version of the pattern name.
                    std::string n = app.shared().patternName[styleIdx * kMaxSlots + k];
                    const size_t sp = n.find(' ');
                    if (sp != std::string::npos && sp <= 8) n = n.substr(0, sp) + "\n" + n.substr(sp + 1);
                    std::string a = n.substr(0, n.find('\n')), b = n.find('\n') == std::string::npos ? "" : n.substr(n.find('\n') + 1);
                    if (a.size() > 7) a = a.substr(0, 7);
                    if (b.size() > 7) b = b.substr(0, 6) + ".";
                    label = b.empty() ? a : a + "\n" + b;
                }
            } else if (y == 4) {
                label = x == kChordSlot ? std::string("Chord") : st.drums[static_cast<size_t>(x)].label;
            } else if (y == 5) {
                label = noteNameFor(st, degreeSemis(st, x));
            } else if (y >= 6) {
                const int f = st.fx[static_cast<size_t>((y - 6) * 8 + x)];
                if (f >= 0 && f < FX_COUNT) label = kFxShort[f];
            } else if (ctl.editView() && y < 2) {
                label = std::to_string(y * 8 + x + 1);
            }
            if (!label.empty())
                centeredText(dl, small, labelSize, a, b, lit ? IM_COL32(40, 30, 50, 235) : IM_COL32(196, 190, 220, 255), label.c_str());
            if (hov && y == 4) ImGui::SetTooltip("%s", x == kChordSlot ? (st.chord.name + " (on the last note pad played)").c_str()
                                                                       : st.drums[static_cast<size_t>(x)].name.c_str());
            if (hov && y >= 6) {
                const int f = st.fx[static_cast<size_t>((y - 6) * 8 + x)];
                if (f >= 0 && f < FX_COUNT) ImGui::SetTooltip("%s\n%s", fxName(f), kFxHelp[f]);
            }
        }
        // Right column round button
        ImVec2 c(o.x + 8 * pitch + pad * 0.5f + topGap, py + pad * 0.5f);
        float r = pad * 0.36f;
        bool hov = button(8, y, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        uint8_t v = fr[64 + y];
        if (ledLit(v)) dl->AddCircleFilled(c, r + 4 * s, withAlpha(ledColor(v), 70));
        dl->AddCircleFilled(c, r, ledColor(v));
        dl->AddCircle(c, r, hov ? IM_COL32(240, 235, 255, 255) : kPadEdge, 0, (hov ? 2.f : 1.2f) * s);
        std::string side = y < kDrumTracks ? firstLine(st.drums[static_cast<size_t>(y)].label) : (y == T_BASS ? "Bass" : "Chord");
        if (side.size() > 5) side = side.substr(0, 5);
        centeredText(dl, small, labelSize * 0.82f, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                     ledLit(v) ? IM_COL32(40, 30, 50, 235) : kTextDim, side.c_str());
        if (hov) ImGui::SetTooltip("%s: %s", trackName(st, y).c_str(), ctl.editView() ? "edit this track" : "mute / unmute");
    }

    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + pitch * 9 + topGap + 18 * s));
    if (picker) {
        ImGui::TextColored(kAccent, "Rows 1-2: pick a style.  Rows 3-4: pick a pattern (yours are amber).");
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(kLabel, "%s view:", ctl.editView() ? "EDIT" : "PERFORM");
        ImGui::SameLine();
        ImGui::TextColored(kHint, ctl.editView() ? "top two rows are the 16 steps of the selected track."
                                                 : "hold View to change style. Right column mutes.");
        ImGui::PopTextWrapPos();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextColored(kHint, "Keys: Space play, Z-, drums, 1-8 notes, Q-I / A-K effects (hold)");
    ImGui::PopTextWrapPos();
}

void drawStepPopup(App& app, GuiState& gs, int p, const Style& st) {
    Shared& sh = app.shared();
    if (!ImGui::BeginPopup("stepedit")) return;
    const int t = gs.popupTrack, s = gs.popupStep;
    if (t < 0 || s < 0) { ImGui::EndPopup(); return; }
    const uint8_t v = sh.on[p][t][s].load();
    const int kind = stepKind(v);
    ImGui::TextColored(kLabel, "%s, step %d", trackName(st, t).c_str(), s + 1);
    ImGui::Separator();
    if (t < kDrumTracks) {
        struct Opt { const char* name; uint8_t code; };
        static const Opt opts[] = {{"Hit", STEP_ON}, {"Accent (loud)", STEP_ACCENT}, {"Ghost (soft)", STEP_SOFT},
                                   {"Roll x2", STEP_ROLL2}, {"Roll x3", STEP_ROLL3}, {"Off", STEP_OFF}};
        for (const Opt& o : opts)
            if (ImGui::Selectable(o.name, kind == o.code)) sh.on[p][t][s].store(o.code);
    } else {
        const int deg = sh.note[p][t][s].load();
        const int maxDeg = t == T_BASS ? 9 : 6;
        for (int d = 0; d <= maxDeg; ++d) {
            std::string lbl = t == T_BASS ? std::string(noteNameFor(st, degreeSemis(st, d))) + (d >= 7 ? " (high)" : "")
                                          : chordNumeral(st, d, st.chord.notes) + "  (" + noteNameFor(st, degreeSemis(st, d)) + ")";
            lbl += "##d" + std::to_string(d);
            if (ImGui::Selectable(lbl.c_str(), stepSounds(v) && deg == d)) {
                sh.note[p][t][s].store(static_cast<int8_t>(d));
                uint8_t keep = stepSounds(v) ? v : static_cast<uint8_t>(STEP_ON);
                sh.on[p][t][s].store(keep);
            }
        }
        ImGui::Separator();
        bool accent = kind == STEP_ACCENT;
        if (stepSounds(v) && ImGui::Checkbox("Accent", &accent))
            sh.on[p][t][s].store(static_cast<uint8_t>((accent ? STEP_ACCENT : STEP_ON) | (v & STEP_SLIDE)));
        if (t == T_BASS && stepSounds(v)) {
            bool slide = (v & STEP_SLIDE) != 0;
            if (ImGui::Checkbox("Slide in from previous note", &slide))
                sh.on[p][t][s].store(static_cast<uint8_t>((v & 0x0F) | (slide ? STEP_SLIDE : 0)));
        }
        if (ImGui::Selectable("Hold previous note", kind == STEP_TIE)) sh.on[p][t][s].store(STEP_TIE);
        if (ImGui::Selectable("Off", kind == STEP_OFF)) sh.on[p][t][s].store(STEP_OFF);
    }
    ImGui::EndPopup();
}

void drawSequencer(App& app, GuiState& gs) {
    Shared& sh = app.shared();
    Controller& ctl = app.controller();
    const float s = gs.scale;
    const int p = sh.pattern.load();
    const Style& st = styleOf(p);
    const bool playing = sh.playing.load();
    const int step = sh.step.load();

    ImGui::TextColored(styleAccent(st), "%s", st.name.c_str());
    ImGui::SameLine();
    ImGui::Text("- %s", sh.patternName[p].c_str());
    ImGui::SameLine();
    ImGui::TextColored(kHint, "   %s, %.0f BPM, swing %d%%", st.keyName.c_str(), st.bpm,
                       static_cast<int>(std::round((st.swing - 0.5f) / 0.16f * 100.f)));
    ImGui::TextColored(kHint, "%s", st.blurb.c_str());
    // Pattern chips: every pattern of this style; yours are amber.
    {
        const int styleIdx = p / kMaxSlots, count = sh.slotCount[styleIdx].load(), queued = sh.queued.load();
        const float maxX = ImGui::GetCursorScreenPos().x + stableAvailX();
        for (int k = 0; k < count; ++k) {
            const int gi = styleIdx * kMaxSlots + k;
            char lbl[128];
            std::snprintf(lbl, sizeof lbl, "%s##chip%d", sh.patternName[gi].c_str(), k);
            const float w = ImGui::CalcTextSize(sh.patternName[gi].c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (k > 0 && ImGui::GetItemRectMax().x + 4 * s + w <= maxX) ImGui::SameLine(0, 4 * s);
            const bool blink = gi == queued && std::fmod(ImGui::GetTime() * 4.0, 1.0) < 0.5;
            ImVec4 col = gi == p ? kGreen : (k < kFactorySlots ? ImVec4(0.55f, 0.75f, 0.55f, 1) : kAccent);
            if (coloredButton(lbl, col, ImVec2(0, 0), gi == p || blink)) app.controller().choosePattern(k);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s pattern %d%s", k < kFactorySlots ? "Built-in" : "Your", k + 1,
                                  gi == p ? " (playing)" : " - click to switch on the next bar");
        }
        if (count < kMaxSlots) {
            if (ImGui::GetItemRectMax().x + 8 * s + 70 * s <= maxX) ImGui::SameLine(0, 8 * s);
            if (ImGui::Button("+ New")) gs.openNew = true;
        }
    }
    ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.47f, 1), "Click a step to toggle it. Right-click for accents, rolls, notes, slides and holds.");

    const float muteW = 36 * s, nameW = 128 * s, groupGap = 10 * s, cellGap = 4 * s;
    const float avail = stableAvailX();
    const float cell = std::max(14 * s, (avail - muteW - nameW - 16 * s - groupGap * 3 - cellGap * 15) / 16.f);
    const float rowH = std::max(26 * s, std::min(cell, 38 * s));
    const float cr = std::min(cell, rowH) * 0.34f;  // step corner radius
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();

    for (int t = 0; t < kTracks; ++t) {
        ImGui::PushID(t);
        const bool muted = sh.mute[t].load();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (coloredButton("M", kRed, ImVec2(muteW, rowH), muted)) sh.mute[t].store(!muted);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(muted ? "Unmute" : "Mute");
        ImGui::SameLine(0, 6 * s);

        const ImU32 tcol = trackColor(st, t);
        const bool sel = ctl.selectedTrack() == t;
        {
            ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("name", ImVec2(nameW, rowH))) ctl.selectTrack(t);
            const bool hov = ImGui::IsItemHovered();
            ImVec2 b(a.x + nameW, a.y + rowH);
            if (sel || hov) dl->AddRectFilled(a, b, sel ? IM_COL32(86, 70, 128, 255) : IM_COL32(58, 53, 80, 255), rowH * 0.5f);
            const float th = ImGui::GetTextLineHeight();
            const std::string nm = trackName(st, t);
            dl->PushClipRect(a, b, true);
            dl->AddText(ImVec2(a.x + 8 * s, a.y + (rowH - th) * 0.5f), muted ? IM_COL32(110, 110, 120, 255) : tcol, nm.c_str());
            dl->PopClipRect();
            if (hov) ImGui::SetTooltip("%s\nClick to select it for the Launchpad's EDIT view", nm.c_str());
        }
        ImGui::SameLine(0, 10 * s);

        for (int stp = 0; stp < kSteps; ++stp) {
            if (stp) ImGui::SameLine(0, stp % 4 == 0 ? groupGap + cellGap : cellGap);
            ImGui::PushID(stp);
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("c", ImVec2(cell, rowH));
            const bool hov = ImGui::IsItemHovered();
            const uint8_t v = sh.on[p][t][stp].load();
            const int kind = stepKind(v);
            const bool on = stepSounds(v);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (on) {
                    sh.on[p][t][stp].store(STEP_OFF);
                } else {
                    if (t >= T_BASS) sh.note[p][t][stp].store(static_cast<int8_t>(ctl.lastBassIndex()));
                    sh.on[p][t][stp].store(STEP_ON);
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                gs.popupTrack = t;
                gs.popupStep = stp;
                ImGui::OpenPopup("stepedit");
            }
            if (gs.popupTrack == t && gs.popupStep == stp) drawStepPopup(app, gs, p, st);

            ImVec2 b(a.x + cell, a.y + rowH);
            const bool head = playing && stp == step;
            ImU32 base = muted ? scaleCol(tcol, 0.45f) : tcol;
            ImU32 col = stp % 4 == 0 ? kCellBeat : kCell;
            if (on) col = kind == STEP_SOFT ? scaleCol(base, 0.5f) : base;
            if (head) col = on ? IM_COL32(255, 248, 235, 255) : IM_COL32(96, 88, 124, 255);
            if (kind == STEP_TIE) {
                // A hold: a bar joining it to the previous note.
                dl->AddRectFilled(a, b, stp % 4 == 0 ? kCellBeat : kCell, cr);
                dl->AddRectFilled(ImVec2(a.x - cellGap - 1, a.y + rowH * 0.32f), ImVec2(b.x, b.y - rowH * 0.32f),
                                  head ? IM_COL32(255, 248, 235, 255) : scaleCol(base, 0.7f), rowH * 0.18f);
            } else {
                dl->AddRectFilled(a, b, col, cr);
            }
            if (on && kind == STEP_ACCENT) dl->AddCircleFilled(ImVec2(b.x - cell * 0.2f, a.y + rowH * 0.2f), 3 * s, IM_COL32(255, 255, 255, 230));
            if (on && (kind == STEP_ROLL2 || kind == STEP_ROLL3)) {
                const int n = kind == STEP_ROLL2 ? 2 : 3;
                for (int k = 0; k < n; ++k) {
                    float cx = a.x + cell * (k + 1) / (n + 1);
                    dl->AddCircleFilled(ImVec2(cx, b.y - 5 * s), 2.2f * s, IM_COL32(20, 20, 20, 220));
                }
            }
            if (on && (v & STEP_SLIDE))
                dl->AddTriangleFilled(ImVec2(a.x, b.y), ImVec2(a.x + 7 * s, b.y), ImVec2(a.x, b.y - 7 * s), IM_COL32(20, 20, 20, 230));
            if (hov) dl->AddRect(a, b, IM_COL32(240, 235, 255, 220), cr, 0, 2.f * s);
            if (t >= T_BASS && on) {
                const int deg = sh.note[p][t][stp].load();
                std::string txt = t == T_BASS ? std::string(noteNameFor(st, degreeSemis(st, deg))) + (deg >= 7 ? "'" : "")
                                              : chordNumeral(st, deg, 3);
                const float fs = std::min(ImGui::GetFontSize(), 14.f * s);
                ImVec2 ts = small->CalcTextSizeA(fs, 1e9f, 0.f, txt.c_str());
                if (ts.x < cell - 2 * s)
                    dl->AddText(small, fs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(10, 10, 10, 255), txt.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    const bool builtin = p % kMaxSlots < kFactorySlots;
    const bool full = sh.slotCount[p / kMaxSlots].load() >= kMaxSlots;
    if (full) ImGui::BeginDisabled();
    if (ImGui::Button("Duplicate")) {
        app.newPattern(sh.patternName[p] + " copy", true);
    }
    if (full) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(full ? "This style already has 16 patterns" : "Make a copy of this pattern as one of your own");
    ImGui::SameLine();
    if (builtin) {
        const bool same = factoryUnchanged(sh, p);
        if (same) ImGui::BeginDisabled();
        if (ImGui::Button("Reset")) loadPreset(sh, p);
        if (same) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(same ? "This built-in pattern is as it shipped" : "Put this built-in pattern back to how it shipped");
    } else {
        if (ImGui::Button("Rename")) {
            gs.target = p;
            std::snprintf(gs.nameBuf, sizeof gs.nameBuf, "%s", sh.patternName[p].c_str());
            gs.openRename = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            gs.target = p;
            gs.openDelete = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) clearPattern(sh, p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn every step off");
    ImGui::SameLine(0, 16 * s);
    ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.47f, 1), "Saved automatically");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your patterns, edits and settings are saved to\n%s", app.dataPath().c_str());
}

void drawPatternModals(App& app, GuiState& gs) {
    Shared& sh = app.shared();
    const float s = gs.scale;
    if (gs.openNew) {
        gs.openNew = false;
        const int style = sh.pattern.load() / kMaxSlots;
        std::snprintf(gs.nameBuf, sizeof gs.nameBuf, "My pattern %d", sh.slotCount[style].load() - kFactorySlots + 1);
        ImGui::OpenPopup("New pattern");
    }
    if (gs.openRename) { gs.openRename = false; ImGui::OpenPopup("Rename pattern"); }
    if (gs.openDelete) { gs.openDelete = false; ImGui::OpenPopup("Delete pattern"); }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int p = sh.pattern.load();
        ImGui::Text("New pattern in %s", styleOf(p).name.c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(300 * s);
        const bool enter = ImGui::InputText("Name", gs.nameBuf, sizeof gs.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::RadioButton(("Start from a copy of \"" + sh.patternName[p] + "\"").c_str(), gs.newCopy)) gs.newCopy = true;
        if (ImGui::RadioButton("Start from scratch (just a kick)", !gs.newCopy)) gs.newCopy = false;
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120 * s, 0)) || enter) {
            app.newPattern(gs.nameBuf, gs.newCopy);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rename pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(300 * s);
        const bool enter = ImGui::InputText("Name", gs.nameBuf, sizeof gs.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Rename", ImVec2(120 * s, 0)) || enter) {
            app.renamePattern(gs.target, gs.nameBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (gs.target >= 0) ImGui::Text("Delete \"%s\"? This can't be undone.", sh.patternName[gs.target].c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.12f, 1));
        if (ImGui::Button("Delete", ImVec2(120 * s, 0))) {
            app.deletePattern(gs.target);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Keep it", ImVec2(120 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawEffects(App& app, GuiState& gs) {
    const float s = gs.scale;
    Controller& ctl = app.controller();
    const Style& st = styleOf(app.shared().pattern.load());
    ImGui::TextColored(kHint, "Hold a button (or its key) to play the effect. Let go to stop. Each style has its own set.");
    ImGui::Spacing();
    const char* keys = "QWERTYUIASDFGHJK";
    const float avail = stableAvailX();
    const float gap = 6 * s;
    const float w = (avail - gap * 7) / 8.f;
    const float h = std::min(w * 0.85f, 86 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    for (int pad = 0; pad < 16; ++pad) {
        const int f = st.fx[static_cast<size_t>(pad)];
        if (pad % 8) ImGui::SameLine(0, gap);
        ImGui::PushID(pad);
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("fx", ImVec2(w, h));
        if (ImGui::IsItemActivated()) app.virtualPress(pad % 8, 6 + pad / 8, true);
        if (ImGui::IsItemDeactivated()) app.virtualPress(pad % 8, 6 + pad / 8, false);
        if (ImGui::IsItemHovered() && f >= 0 && f < FX_COUNT) ImGui::SetTooltip("%s\n%s", fxName(f), kFxHelp[f]);
        const bool held = ctl.fxHeld(f);
        const bool hov = ImGui::IsItemHovered();
        ImVec2 b(a.x + w, a.y + h);
        const ImU32 base = guiColor(fxColor(f));
        dl->AddRectFilled(a, b, held ? base : withAlpha(base, 70), 20 * s);
        if (hov) dl->AddRect(a, b, IM_COL32(240, 235, 255, 230), 20 * s, 0, 2.f * s);
        centeredText(dl, ImGui::GetFont(), ImGui::GetFontSize(), a, ImVec2(b.x, b.y - 8 * s),
                     held ? IM_COL32(40, 30, 50, 255) : IM_COL32(245, 240, 255, 255), f >= 0 && f < FX_COUNT ? kFxTitle[f] : "");
        char k[2] = {keys[pad], 0};
        dl->AddText(small, 13 * s, ImVec2(a.x + 14 * s, b.y - 20 * s), held ? IM_COL32(40, 30, 50, 200) : IM_COL32(190, 180, 215, 200), k);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextColored(kHint, "Tip: hold Noise Riser for a few seconds and let go on the 1 for a drop. Effects stack.");
}

void drawStyles(App& app, GuiState& gs) {
    const float s = gs.scale;
    Controller& ctl = app.controller();
    const int cur = app.shared().pattern.load() / kMaxSlots;
    ImGui::TextColored(kHint, "Each style has its own sounds, key, swing, effects and four patterns. Switching happens on the next bar.");
    ImGui::Spacing();
    const float avail = stableAvailX();
    const int cols = avail > 900 * s ? 4 : 3;
    const float gap = 8 * s;
    const float w = (avail - gap * (cols - 1)) / cols, h = 92 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < styleCount(); ++i) {
        const Style& o = styles()[static_cast<size_t>(i)];
        if (i % cols) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("st", ImVec2(w, h))) ctl.chooseStyle(i);
        const bool hov = ImGui::IsItemHovered();
        ImVec2 b(a.x + w, a.y + h);
        const ImU32 acc = ledColor(o.led);
        dl->AddRectFilled(a, b, i == cur ? withAlpha(acc, 70) : (hov ? IM_COL32(60, 55, 82, 255) : IM_COL32(48, 44, 66, 255)), 20 * s);
        dl->AddCircleFilled(ImVec2(b.x - 20 * s, a.y + 20 * s), 7 * s, acc);  // colour dot
        if (i == cur) dl->AddRect(a, b, acc, 20 * s, 0, 2.f * s);
        dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2(a.x + 18 * s, a.y + 10 * s), IM_COL32(245, 240, 255, 255), o.name.c_str());
        char sub[96];
        std::snprintf(sub, sizeof sub, "%.0f BPM  %s", o.bpm, o.keyName.c_str());
        dl->AddText(ImVec2(a.x + 18 * s, a.y + 10 * s + ImGui::GetTextLineHeight()), IM_COL32(185, 178, 210, 255), sub);
        ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
        dl->AddText(small, 13.5f * s, ImVec2(a.x + 18 * s, a.y + 16 * s + ImGui::GetTextLineHeight() * 2), IM_COL32(160, 152, 188, 255),
                    o.blurb.c_str(), nullptr, w - 32 * s);
        dl->PopClipRect();
        ImGui::PopID();
    }
}

const char* kRouteNames[3] = {"Lead synth", "Bass", "Drums"};

bool routeCombo(MidiHub& midi, const std::string& name, int route, float width) {
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::BeginCombo("##route", kRouteNames[route % 3])) {
        for (int r = 0; r < 3; ++r)
            if (ImGui::Selectable(kRouteNames[r], r == route)) {
                midi.setKeyboardRoute(name, r);
                changed = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("What this keyboard plays.\nLead synth: the sound chosen in the Instrument tab\nBass: the style's bass in its key\nDrums: the style's kit\nChannel 10 always plays drums.");
    return changed;
}

// Piano keyboard: click (and drag) to play the lead synth. Lower on a key = louder.
void drawPiano(App& app, GuiState& gs, int lo, int hi, float height) {
    const float s = gs.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto isBlack = [](int n) { int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; };
    int whites = 0;
    for (int n = lo; n <= hi; ++n) whites += isBlack(n) ? 0 : 1;
    const float W = stableAvailX();
    const float ww = W / whites, bw = ww * 0.62f, bh = height * 0.6f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("piano", ImVec2(W, height));
    const bool active = ImGui::IsItemActive();
    const ImVec2 m = ImGui::GetIO().MousePos;

    // Key positions
    std::vector<std::pair<int, ImVec4>> rects;  // note, (x0,y0,x1,y1)
    int wi = 0;
    for (int n = lo; n <= hi; ++n) {
        if (isBlack(n)) continue;
        rects.push_back({n, ImVec4(o.x + wi * ww, o.y, o.x + (wi + 1) * ww - 1.5f * s, o.y + height)});
        ++wi;
    }
    std::vector<std::pair<int, ImVec4>> blacks;
    wi = 0;
    for (int n = lo; n <= hi; ++n) {
        if (!isBlack(n)) { ++wi; continue; }
        const float cx = o.x + wi * ww;
        blacks.push_back({n, ImVec4(cx - bw * 0.5f, o.y, cx + bw * 0.5f, o.y + bh)});
    }
    auto inside = [&](const ImVec4& r) { return m.x >= r.x && m.x < r.z && m.y >= r.y && m.y < r.w; };
    int under = -1;
    for (auto& b : blacks) if (inside(b.second)) under = b.first;
    if (under < 0) for (auto& w : rects) if (inside(w.second)) under = w.first;

    // Mouse playing, with glissando when dragging across keys.
    if (active && under != gs.pianoHeld) {
        if (gs.pianoHeld >= 0) app.leadKey(gs.pianoHeld, false);
        gs.pianoHeld = under;
        if (under >= 0) {
            const float rel = (m.y - o.y) / height;
            app.leadKey(under, true, std::max(30, std::min(127, static_cast<int>(40 + rel * 95))));
        }
    }
    if (!active && gs.pianoHeld >= 0) {
        app.leadKey(gs.pianoHeld, false);
        gs.pianoHeld = -1;
    }

    const Style& st = styleOf(app.shared().pattern.load());
    auto inKey = [&](int n) {
        const int pc = (((n - st.root) % 12) + 12) % 12;
        for (int v : st.scale) if (v == pc) return true;
        return false;
    };
    const ImU32 lit = IM_COL32(110, 225, 150, 255);
    for (auto& w : rects) {
        const ImVec4& r = w.second;
        const bool held = app.leadKeyHeld(w.first);
        dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), held ? lit : IM_COL32(240, 236, 250, 255), 10 * s, ImDrawFlags_RoundCornersBottom);
        if (inKey(w.first)) dl->AddCircleFilled(ImVec2((r.x + r.z) * 0.5f, r.w - 9 * s), 3 * s, IM_COL32(255, 160, 40, 220));
        if (w.first % 12 == 0) {
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "C%d", w.first / 12 - 1);
            dl->AddText(ImVec2(r.x + 4 * s, r.w - 24 * s - ImGui::GetTextLineHeight() * 0.3f), IM_COL32(90, 90, 100, 255), lbl);
        }
    }
    for (auto& b : blacks) {
        const ImVec4& r = b.second;
        const bool held = app.leadKeyHeld(b.first);
        dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), held ? lit : IM_COL32(48, 42, 66, 255), 7 * s, ImDrawFlags_RoundCornersBottom);
        if (inKey(b.first)) dl->AddCircleFilled(ImVec2((r.x + r.z) * 0.5f, r.w - 8 * s), 2.5f * s, IM_COL32(255, 160, 40, 220));
    }
}

void drawInstrument(App& app, GuiState& gs) {
    const float s = gs.scale;
    Shared& sh = app.shared();
    MidiHub& midi = app.midi();
    const Style& st = styleOf(sh.pattern.load());

    // Which keyboards play the lead
    auto kbs = midi.keyboards();
    int leads = 0;
    for (const auto& k : kbs) leads += (k.enabled && k.route == ROUTE_LEAD) ? 1 : 0;
    if (kbs.empty()) {
        ImGui::TextColored(kHint, "Plug in a second MIDI keyboard to play over the beat, or click the keys below.");
    } else {
        for (const auto& k : kbs) {
            ImGui::PushID(k.name.c_str());
            const double now = monotonicSec();
            statusDot(!k.enabled ? IM_COL32(70, 70, 78, 255)
                                 : (now - k.lastActivity < 0.12 ? IM_COL32(255, 200, 60, 255) : IM_COL32(80, 220, 90, 255)), s);
            ImGui::Text("%s", k.name.c_str());
            ImGui::SameLine(0, 12 * s);
            ImGui::TextColored(kHint, "plays");
            ImGui::SameLine();
            routeCombo(midi, k.name, k.route, 140 * s);
            ImGui::PopID();
        }
        if (leads == 0) ImGui::TextColored(kAccent, "None of your keyboards are set to play the lead synth.");
    }
    // Sound
    ImGui::SeparatorText("Sound");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * s, 7 * s));
    const auto& pats = leadPatches();
    std::string group;
    const float indent = 100 * s, bw = 168 * s, maxX = ImGui::GetCursorScreenPos().x + stableAvailX();
    for (size_t i = 0; i < pats.size(); ++i) {
        const LeadPatch& lp = pats[i];
        if (lp.group != group) {
            group = lp.group;
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kLabel, "%s", group.c_str());
            ImGui::SameLine(indent);
        } else if (ImGui::GetItemRectMax().x + 6 * s + bw <= maxX) {
            ImGui::SameLine(0, 6 * s);
        } else {
            ImGui::Dummy(ImVec2(0, 0));  // new row, indented under the group
            ImGui::SameLine(indent);
        }
        std::string label = lp.name;
        if (lp.kind == LeadPatch::STYLE_BASS) label = st.bass.name + " (style)";
        if (lp.kind == LeadPatch::STYLE_CHORDS) label = st.chord.name + " (style)";
        label += "##lp" + std::to_string(i);
        if (coloredButton(label.c_str(), kAccent, ImVec2(bw, 0), app.leadPatch() == static_cast<int>(i)))
            app.setLeadPatch(static_cast<int>(i));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", lp.kind == LeadPatch::MONO || lp.kind == LeadPatch::STYLE_BASS
                                        ? "Mono: one note at a time, glides between held notes"
                                        : "Poly: play chords");
    }
    ImGui::PopStyleVar(2);

    // Controls
    ImGui::SeparatorText("Controls");
    float lv = sh.leadVolume.load();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Volume %d%%", static_cast<int>(std::round(lv * 100.f)));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::SliderFloat("##leadvol", &lv, 0.f, 1.f, "")) sh.leadVolume.store(lv);
    ImGui::SameLine(0, 24 * s);
    ImGui::TextUnformatted("Octave");
    ImGui::SameLine();
    if (ImGui::Button("-##oct")) app.setLeadOctave(app.leadOctave() - 1);
    ImGui::SameLine();
    ImGui::Text("%+d", app.leadOctave());
    ImGui::SameLine();
    if (ImGui::Button("+##oct")) app.setLeadOctave(app.leadOctave() + 1);
    ImGui::SameLine(0, 24 * s);
    bool snap = app.leadSnap();
    if (ImGui::Checkbox("Snap to key", &snap)) app.setLeadSnap(snap);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Wrong notes move to the nearest note in %s, so anything you play fits.\nThe orange dots on the keyboard show the notes in the key.", st.keyName.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kHint, "(%s)", st.keyName.c_str());
    ImGui::SameLine(0, 24 * s);
    ImGui::TextColored(kHint, "(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Velocity, sustain pedal, pitch bend (+/-2) and mod wheel (vibrato) all work.");

    const float ph = std::max(120 * s, std::min(160 * s, ImGui::GetContentRegionAvail().y - 4 * s));
    drawPiano(app, gs, 48, 84, ph);
}

void drawDevices(App& app, GuiState& gs, double now) {
    const float s = gs.scale;
    MidiHub& midi = app.midi();
    ImGui::TextColored(kHint, "Devices are checked every second. Plug things in or out at any time.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan now")) midi.forceRescan();
    ImGui::Spacing();

    ImGui::SeparatorText("Launchpad");
    if (midi.launchpadConnected()) {
        statusDot(IM_COL32(80, 220, 90, 255), s);
        ImGui::Text("Connected: %s", midi.launchpadInName().c_str());
    } else {
        statusDot(IM_COL32(120, 120, 130, 255), s);
        ImGui::Text("Not connected");
    }
    auto portCombo = [&](const char* label, const std::string& current, const std::vector<std::string>& names,
                         bool isInput) {
        ImGui::SetNextItemWidth(320 * s);
        const char* preview = current.empty() ? "Auto-detect (any \"Launchpad\")" : current.c_str();
        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::Selectable("Auto-detect (any \"Launchpad\")", current.empty()))
                midi.setLaunchpadPorts(isInput ? "" : midi.wantedIn(), isInput ? midi.wantedOut() : "");
            for (const auto& n : names)
                if (ImGui::Selectable(n.c_str(), n == current))
                    midi.setLaunchpadPorts(isInput ? n : midi.wantedIn(), isInput ? midi.wantedOut() : n);
            ImGui::EndCombo();
        }
    };
    portCombo("Input port", midi.wantedIn(), midi.inputs(), true);
    portCombo("Output port", midi.wantedOut(), midi.outputs(), false);

    ImGui::Spacing();
    ImGui::SeparatorText("Other MIDI inputs");
    ImGui::TextColored(kHint, "Choose what each one plays: the lead synth (Instrument tab), the style's bass, or its drums.");
    auto kbs = midi.keyboards();
    if (kbs.empty()) ImGui::TextColored(kLabel, "None plugged in.");
    for (const auto& k : kbs) {
        ImGui::PushID(k.name.c_str());
        bool en = k.enabled;
        if (ImGui::Checkbox("##en", &en)) midi.setKeyboardEnabled(k.name, en);
        ImGui::SameLine();
        ImU32 dot = !k.enabled ? IM_COL32(70, 70, 78, 255)
                               : (!k.open ? IM_COL32(230, 80, 60, 255)
                                          : (now - k.lastActivity < 0.12 ? IM_COL32(255, 200, 60, 255) : IM_COL32(80, 220, 90, 255)));
        statusDot(dot, s);
        ImGui::Text("%s", k.name.c_str());
        ImGui::SameLine(0, 12 * s);
        routeCombo(midi, k.name, k.route, 140 * s);
        ImGui::SameLine();
        ImGui::TextColored(kHint, "%s", !k.enabled ? "(off)" : (k.open ? "" : "(in use by another app?)"));
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Audio output");
    statusDot(app.audioRunning() ? IM_COL32(80, 220, 90, 255) : IM_COL32(230, 80, 60, 255), s);
    ImGui::Text("%s", app.audioStatus().c_str());
    ImGui::SetNextItemWidth(320 * s);
    const std::string& pref = app.preferredAudio();
    if (ImGui::BeginCombo("Device", pref.empty() ? "System default" : pref.c_str())) {
        if (ImGui::Selectable("System default", pref.empty())) app.selectAudio("");
        for (const auto& n : app.audioDevices())
            if (ImGui::Selectable(n.c_str(), n == pref)) app.selectAudio(n);
        ImGui::EndCombo();
    }
    ImGui::TextColored(kHint, "If the chosen device is unplugged, sound moves to the default and comes back when it returns.");
}

void handleKeys(App& app, GuiState& gs) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    struct Map { ImGuiKey key; int x, y; };
    static const Map maps[] = {
        {ImGuiKey_Z, 0, 4}, {ImGuiKey_X, 1, 4}, {ImGuiKey_C, 2, 4}, {ImGuiKey_V, 3, 4},
        {ImGuiKey_B, 4, 4}, {ImGuiKey_N, 5, 4}, {ImGuiKey_M, 6, 4}, {ImGuiKey_Comma, 7, 4},
        {ImGuiKey_1, 0, 5}, {ImGuiKey_2, 1, 5}, {ImGuiKey_3, 2, 5}, {ImGuiKey_4, 3, 5},
        {ImGuiKey_5, 4, 5}, {ImGuiKey_6, 5, 5}, {ImGuiKey_7, 6, 5}, {ImGuiKey_8, 7, 5},
        {ImGuiKey_Q, 0, 6}, {ImGuiKey_W, 1, 6}, {ImGuiKey_E, 2, 6}, {ImGuiKey_R, 3, 6},
        {ImGuiKey_T, 4, 6}, {ImGuiKey_Y, 5, 6}, {ImGuiKey_U, 6, 6}, {ImGuiKey_I, 7, 6},
        {ImGuiKey_A, 0, 7}, {ImGuiKey_S, 1, 7}, {ImGuiKey_D, 2, 7}, {ImGuiKey_F, 3, 7},
        {ImGuiKey_G, 4, 7}, {ImGuiKey_H, 5, 7}, {ImGuiKey_J, 6, 7}, {ImGuiKey_K, 7, 7},
        {ImGuiKey_Space, 0, -1},
    };
    static_assert(sizeof(maps) / sizeof(maps[0]) <= 64, "keyHeld too small");
    int i = 0;
    for (const Map& m : maps) {
        bool down = ImGui::IsKeyDown(m.key);
        if (down != gs.keyHeld[i]) {
            gs.keyHeld[i] = down;
            app.virtualPress(m.x, m.y, down);
        }
        ++i;
    }
}

}  // namespace

void setupGuiStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    // Rounded and roomy: pill buttons/sliders, round checkboxes, soft panels.
    st.WindowRounding = 20.f;  // dialogs (the main window turns this off)
    st.ChildRounding = 22.f;
    st.FrameRounding = 18.f;
    st.PopupRounding = 18.f;
    st.GrabRounding = 18.f;
    st.TabRounding = 14.f;
    st.ScrollbarRounding = 12.f;
    st.FramePadding = ImVec2(16, 9);
    st.ItemSpacing = ImVec2(10, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.WindowPadding = ImVec2(18, 16);
    st.GrabMinSize = 22.f;
    st.ScrollbarSize = 16.f;
    st.FrameBorderSize = 0.f;
    st.ChildBorderSize = 0.f;
    st.PopupBorderSize = 0.f;
    st.WindowBorderSize = 0.f;
    st.SeparatorTextBorderSize = 2.f;
    st.SeparatorTextPadding = ImVec2(14, 4);
    ImVec4* c = st.Colors;
    auto u = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    c[ImGuiCol_WindowBg] = u(kBg);
    c[ImGuiCol_ChildBg] = u(kPanel);
    c[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.16f, 0.25f, 0.99f);
    c[ImGuiCol_TitleBg] = ImVec4(0.30f, 0.26f, 0.44f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.36f, 0.30f, 0.54f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.30f, 0.26f, 0.44f, 1.f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.08f, 0.06f, 0.12f, 0.6f);
    c[ImGuiCol_Border] = ImVec4(0.40f, 0.36f, 0.52f, 0.5f);
    c[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.23f, 0.34f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.32f, 0.29f, 0.44f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.38f, 0.34f, 0.52f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.25f, 0.23f, 0.34f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.29f, 0.44f, 1.f);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.78f, 0.58f, 1.f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Header] = ImVec4(0.36f, 0.30f, 0.54f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.42f, 0.36f, 0.62f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.48f, 0.40f, 0.70f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(0.34f, 0.31f, 0.46f, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.31f, 0.46f, 1.f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.93f, 0.99f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.52f, 0.64f, 1.f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.72f, 0.62f, 1.00f, 0.4f);
    st.ScaleAllSizes(scale);
}

void drawGui(App& app, GuiState& gs, double now) {
    const float s = gs.scale;
    handleKeys(app, gs);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("LP-DNB", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    drawTopBar(app, gs, now);
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = std::clamp(avail * 0.40f, 360 * s, 600 * s);

    ImGui::BeginChild("launchpad", ImVec2(leftW, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawLaunchpad(app, gs, leftW - 2 * ImGui::GetStyle().WindowPadding.x - 10 * s, now);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    const float logH = ImGui::GetTextLineHeightWithSpacing() * 3 + ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::BeginChild("right", ImVec2(0, ImGui::GetContentRegionAvail().y - logH - ImGui::GetStyle().ItemSpacing.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    // Pill-shaped tabs.
    if (gs.forceTab >= 0) gs.tab = gs.forceTab;
    gs.forceTab = -1;
    {
        const char* names[5] = {"Sequencer", "Effects", "Instrument", "Styles", "Devices"};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * s, 10 * s));
        for (int i = 0; i < 5; ++i) {
            if (i) ImGui::SameLine(0, 8 * s);
            if (coloredButton(names[i], kLilac, ImVec2(0, 0), gs.tab == i)) gs.tab = i;
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 4 * s));
    }
    switch (gs.tab) {
        case 0: drawSequencer(app, gs); break;
        case 1: drawEffects(app, gs); break;
        case 2: drawInstrument(app, gs); break;
        case 3: drawStyles(app, gs); break;
        default: drawDevices(app, gs, now); break;
    }
    ImGui::EndChild();

    ImGui::BeginChild("log", ImVec2(0, logH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    for (const auto& l : Log::get().tail(3)) ImGui::TextColored(kHint, "%s", l.c_str());
    ImGui::EndChild();
    ImGui::EndGroup();

    drawPatternModals(app, gs);
    ImGui::End();
}

}  // namespace dnb
