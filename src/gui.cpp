#include "gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "log.h"
#include "patterns.h"

namespace dnb {
namespace {

// ------------------------------------------------------------ palette --

const ImU32 kBg = IM_COL32(18, 18, 22, 255);
const ImU32 kPanel = IM_COL32(28, 28, 34, 255);
const ImU32 kPadOff = IM_COL32(44, 44, 52, 255);
const ImU32 kPadEdge = IM_COL32(70, 70, 80, 255);
const ImU32 kTextDim = IM_COL32(150, 150, 160, 255);
const ImVec4 kAccent = ImVec4(1.00f, 0.62f, 0.10f, 1.f);  // amber
const ImVec4 kGreen = ImVec4(0.30f, 0.85f, 0.35f, 1.f);
const ImVec4 kRed = ImVec4(0.92f, 0.25f, 0.22f, 1.f);

const ImU32 kTrackCol[kTracks] = {
    IM_COL32(235, 70, 60, 255),   // kick
    IM_COL32(255, 175, 40, 255),  // snare
    IM_COL32(240, 125, 50, 255),  // ghost
    IM_COL32(90, 210, 90, 255),   // hat
    IM_COL32(170, 225, 70, 255),  // open hat
    IM_COL32(235, 215, 70, 255),  // ride
    IM_COL32(60, 200, 170, 255),  // bass
    IM_COL32(200, 140, 240, 255), // stab
};

const char* kDrumLabel[8] = {"Kick", "Snare", "Ghost", "Hat", "Open\nhat", "Ride", "Crash", "Stab"};
const char* kBassLabel[8] = {"F", "G", "Ab", "Bb", "C", "Db", "Eb", "F"};
const char* kFxLabel[FX_COUNT] = {"Stut\n1/4", "Stut\n1/8", "Stut\n1/16", "Stut\n1/32", "Tape\nstop", "Half\ntime",
                                  "Rev", "Delay", "Low\npass", "High\npass", "Crush", "Flange", "Verb", "Siren",
                                  "Riser", "Horn"};
const char* kFxHelp[FX_COUNT] = {
    "Loops the last quarter note", "Loops the last eighth note", "Loops the last 16th note",
    "Loops the last 32nd note (glitchy roll)", "Winds the audio down to a stop", "Sequencer plays at half speed",
    "Plays the last beat backwards", "Dotted-eighth ping-pong echoes", "Resonant low-pass sweep on everything",
    "Resonant high-pass sweep on everything", "Sample-rate and bit reduction", "Jet-plane flanger sweep",
    "Big reverb wash with a long tail", "Dub siren into the delay", "Noise riser: hold it to build, release to drop",
    "Air horn"};
const char* kFxTitle[FX_COUNT] = {"Stutter\n1/4", "Stutter\n1/8", "Stutter\n1/16", "Stutter\n1/32",
                                  "Tape\nstop", "Half-\ntime", "Reverse", "Delay\nthrow",
                                  "Low-pass\nsweep", "High-pass\nsweep", "Bitcrush", "Flanger",
                                  "Reverb\nwash", "Dub\nsiren", "Noise\nriser", "Air\nhorn"};
const char* kTopLabel[8] = {"Play", "View", "BPM-", "BPM+", "1", "2", "3", "4"};
const char* kSideLabel[8] = {"Kick", "Snr", "Ghst", "Hat", "OHat", "Ride", "Bass", "Stab"};

const char* noteName(int semi) {
    static const char* n[12] = {"F", "F#", "G", "Ab", "A", "Bb", "B", "C", "Db", "D", "Eb", "E"};
    return n[((semi % 12) + 12) % 12];
}

// Launchpad LED value (red 0..3 | green 0..3 << 4) to a screen colour that
// looks like the real thing: red + green LEDs mix into amber / yellow.
ImU32 ledColor(uint8_t v) {
    const int r = v & 3, g = (v >> 4) & 3;
    if (!r && !g) return kPadOff;
    int R = r ? 80 + 58 * r : 20 * g;
    int G = g ? static_cast<int>((70 + 60 * g) * (r ? 0.85f : 1.f)) : 15 * r;
    return IM_COL32(std::min(R, 255), std::min(G, 255), 30, 255);
}
bool ledLit(uint8_t v) { return (v & 0x33) != 0; }

ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | (static_cast<ImU32>(a) << 24); }
ImU32 scaleCol(ImU32 c, float k) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x * k, v.y * k, v.z * k, v.w));
}

void centeredText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 col, const char* text) {
    // Split on '\n' and centre each line.
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
    ImVec4 base = active ? col : ImVec4(0.20f, 0.20f, 0.24f, 1.f);
    ImVec4 hov = active ? ImVec4(col.x * 1.1f, col.y * 1.1f, col.z * 1.1f, 1.f) : ImVec4(0.27f, 0.27f, 0.32f, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(0.05f, 0.05f, 0.05f, 1.f) : ImVec4(0.92f, 0.92f, 0.94f, 1.f));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void statusDot(ImU32 col, float s) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5 * s, p.y + h * 0.5f), 5 * s, col);
    ImGui::Dummy(ImVec2(14 * s, h));
    ImGui::SameLine();
}

// ------------------------------------------------------------ sections --

void drawTopBar(App& app, GuiState& gs, double now) {
    Shared& sh = app.shared();
    const float s = gs.scale;
    const bool playing = sh.playing.load();
    const ImVec2 big(110 * s, 40 * s);

    if (coloredButton(playing ? "Stop" : "Play", playing ? kRed : kGreen, big, true)) {
        app.virtualPress(0, -1, true);
        app.virtualPress(0, -1, false);
    }
    ImGui::SameLine(0, 18 * s);

    // Tempo
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "TEMPO");
    float bpm = sh.bpm.load();
    ImGui::SetNextItemWidth(170 * s);
    if (ImGui::SliderFloat("##bpm", &bpm, 100.f, 200.f, "%.0f BPM")) sh.bpm.store(std::round(bpm));
    ImGui::EndGroup();
    ImGui::SameLine(0, 18 * s);

    // Patterns
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "PATTERN");
    const int cur = sh.pattern.load(), queued = sh.queued.load();
    for (int i = 0; i < kPatterns; ++i) {
        if (i) ImGui::SameLine(0, 4 * s);
        char label[64];
        std::snprintf(label, sizeof label, "%d  %s##pat%d", i + 1, kPresets[i].name, i);
        bool blink = i == queued && std::fmod(now * 4.0, 1.0) < 0.5;
        if (coloredButton(label, i == cur ? kGreen : kAccent, ImVec2(0, ImGui::GetFrameHeight()), i == cur || blink)) {
            app.virtualPress(4 + i, -1, true);
            app.virtualPress(4 + i, -1, false);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switches at the end of the bar while playing");
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 18 * s);

    // Volume + meter
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "VOLUME");
    float vol = sh.volume.load();
    ImGui::SetNextItemWidth(140 * s);
    if (ImGui::SliderFloat("##vol", &vol, 0.f, 1.f, "%.2f")) sh.volume.store(vol);
    ImGui::EndGroup();
    ImGui::SameLine(0, 10 * s);

    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "LEVEL");
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 120 * s, h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(35, 35, 42, 255), 3 * s);
        const float lv = std::min(1.f, sh.level.load());
        const int segs = 16;
        for (int i = 0; i < segs; ++i) {
            float x0 = p.x + 3 * s + i * (w - 6 * s) / segs;
            float x1 = x0 + (w - 6 * s) / segs - 2 * s;
            bool on = lv * segs > i;
            ImU32 c = i < 10 ? IM_COL32(80, 210, 90, 255) : (i < 14 ? IM_COL32(240, 190, 50, 255) : IM_COL32(235, 70, 60, 255));
            dl->AddRectFilled(ImVec2(x0, p.y + 4 * s), ImVec2(x1, p.y + h - 4 * s), on ? c : scaleCol(c, 0.18f), 1.5f * s);
        }
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 18 * s);

    // Step position
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "STEP");
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight(), d = 9 * s, gap = 3 * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int step = sh.step.load();
        float x = p.x;
        for (int i = 0; i < kSteps; ++i) {
            if (i && i % 4 == 0) x += 4 * s;
            ImU32 c = playing && i == step ? IM_COL32(255, 170, 40, 255)
                                           : (i % 4 == 0 ? IM_COL32(90, 90, 100, 255) : IM_COL32(55, 55, 64, 255));
            dl->AddRectFilled(ImVec2(x, p.y + (h - d) * 0.5f), ImVec2(x + d, p.y + (h + d) * 0.5f), c, 2 * s);
            x += d + gap;
        }
        ImGui::Dummy(ImVec2(x - p.x, h));
    }
    ImGui::EndGroup();
}

void drawLaunchpad(App& app, GuiState& gs, float width, double now) {
    const float s = gs.scale;
    MidiHub& midi = app.midi();
    Controller& ctl = app.controller();

    // Connection status
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
    const float labelSize = std::max(10.f * s, std::min(pad * 0.24f, 15.f * s));
    const Frame& fr = app.frame();

    dl->AddRectFilled(ImVec2(o.x - 10 * s, o.y - 10 * s), ImVec2(o.x + pitch * 9 + topGap + 10 * s, o.y + pitch * 9 + topGap + 10 * s),
                      IM_COL32(12, 12, 14, 255), 10 * s);

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
        dl->AddCircle(c, r, hov ? IM_COL32(200, 200, 210, 255) : kPadEdge, 0, 1.2f * s);
        centeredText(dl, small, labelSize * 0.9f, ImVec2(c.x - r, c.y + r + 1 * s), ImVec2(c.x + r, c.y + r + labelSize + 3 * s),
                     kTextDim, kTopLabel[x]);
    }

    // Grid + right column
    for (int y = 0; y < 8; ++y) {
        const float py = o.y + (y + 1) * pitch + topGap;
        for (int x = 0; x < 8; ++x) {
            ImVec2 a(o.x + x * pitch, py), b(a.x + pad, py + pad);
            bool hov = button(x, y, a, b);
            uint8_t v = fr[y * 8 + x];
            bool lit = ledLit(v);
            if (lit) dl->AddRectFilled(ImVec2(a.x - 3 * s, a.y - 3 * s), ImVec2(b.x + 3 * s, b.y + 3 * s), withAlpha(ledColor(v), 60), 6 * s);
            dl->AddRectFilled(a, b, ledColor(v), 4 * s);
            dl->AddRect(a, b, hov ? IM_COL32(220, 220, 230, 255) : (lit ? withAlpha(ledColor(v), 255) : kPadEdge), 4 * s, 0, 1.2f * s);
            const char* label = nullptr;
            if (y == 4) label = kDrumLabel[x];
            else if (y == 5) label = kBassLabel[x];
            else if (y >= 6) label = kFxLabel[(y - 6) * 8 + x];
            else if (ctl.editView() && y < 2) {
                static char num[4];
                std::snprintf(num, sizeof num, "%d", y * 8 + x + 1);
                label = num;
            }
            if (label)
                centeredText(dl, small, labelSize, a, b, lit ? IM_COL32(15, 15, 15, 230) : IM_COL32(175, 175, 185, 255), label);
        }
        // Right column round button
        ImVec2 c(o.x + 8 * pitch + pad * 0.5f + topGap, py + pad * 0.5f);
        float r = pad * 0.36f;
        bool hov = button(8, y, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
        uint8_t v = fr[64 + y];
        if (ledLit(v)) dl->AddCircleFilled(c, r + 4 * s, withAlpha(ledColor(v), 70));
        dl->AddCircleFilled(c, r, ledColor(v));
        dl->AddCircle(c, r, hov ? IM_COL32(200, 200, 210, 255) : kPadEdge, 0, 1.2f * s);
        centeredText(dl, small, labelSize * 0.8f, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                     ledLit(v) ? IM_COL32(15, 15, 15, 230) : kTextDim, kSideLabel[y]);
    }

    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + pitch * 9 + topGap + 18 * s));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "%s view.", ctl.editView() ? "EDIT" : "PERFORM");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1), ctl.editView()
                                                          ? "Top two rows are the 16 steps of the selected track."
                                                          : "Right column mutes tracks. Hold the bottom rows for effects.");
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1),
                       "Keys: Space play  Z-, drums  1-8 bass  Q-I / A-K effects (hold)");
}

void drawSequencer(App& app, GuiState& gs) {
    Shared& sh = app.shared();
    Controller& ctl = app.controller();
    const float s = gs.scale;
    const int p = sh.pattern.load();
    const bool playing = sh.playing.load();
    const int step = sh.step.load();

    ImGui::Text("Pattern %d: %s", p + 1, kPresets[p].name);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1), "  click steps to toggle, right-click a bass step for its note");

    const float muteW = 26 * s, nameW = 96 * s, groupGap = 8 * s, cellGap = 3 * s;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cell = std::max(14 * s, (avail - muteW - nameW - 16 * s - groupGap * 3 - cellGap * 15) / 16.f);
    const float rowH = std::min(cell, 34 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int t = 0; t < kTracks; ++t) {
        ImGui::PushID(t);
        const bool muted = sh.mute[t].load();
        if (coloredButton("M", kRed, ImVec2(muteW, rowH), muted)) sh.mute[t].store(!muted);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(muted ? "Unmute" : "Mute");
        ImGui::SameLine(0, 6 * s);

        const bool sel = ctl.selectedTrack() == t;
        {
            ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("name", ImVec2(nameW, rowH))) ctl.selectTrack(t);
            const bool hov = ImGui::IsItemHovered();
            ImVec2 b(a.x + nameW, a.y + rowH);
            if (sel || hov) dl->AddRectFilled(a, b, sel ? IM_COL32(80, 60, 30, 255) : IM_COL32(45, 45, 54, 255), 4 * s);
            ImU32 tc = muted ? IM_COL32(110, 110, 120, 255) : kTrackCol[t];
            const float th = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(a.x + 8 * s, a.y + (rowH - th) * 0.5f), tc, trackName(t));
            if (hov) ImGui::SetTooltip("Select this track for the Launchpad's EDIT view");
        }
        ImGui::SameLine(0, 10 * s);

        for (int st = 0; st < kSteps; ++st) {
            if (st) ImGui::SameLine(0, st % 4 == 0 ? groupGap + cellGap : cellGap);
            ImGui::PushID(st);
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("c", ImVec2(cell, rowH));
            const bool hov = ImGui::IsItemHovered();
            const bool on = sh.on[p][t][st].load() != 0;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (!on && t == T_BASS) sh.bassNote[p][st].store(static_cast<int8_t>(kBassScale[ctl.lastBassIndex()]));
                sh.on[p][t][st].store(on ? 0 : 1);
            }
            if (t == T_BASS && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                gs.bassPopupStep = st;
                ImGui::OpenPopup("bassnote");
            }
            if (t == T_BASS && gs.bassPopupStep == st && ImGui::BeginPopup("bassnote")) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "Step %d note", st + 1);
                for (int k = 0; k < 8; ++k) {
                    char lbl[16];
                    std::snprintf(lbl, sizeof lbl, "%s%s", kBassLabel[k], k == 7 ? " (high)" : "");
                    if (ImGui::Selectable(lbl, on && sh.bassNote[p][st].load() == kBassScale[k])) {
                        sh.bassNote[p][st].store(static_cast<int8_t>(kBassScale[k]));
                        sh.on[p][T_BASS][st].store(1);
                    }
                }
                ImGui::Separator();
                if (ImGui::Selectable("Off", !on)) sh.on[p][T_BASS][st].store(0);
                ImGui::EndPopup();
            }
            ImVec2 b(a.x + cell, a.y + rowH);
            const bool head = playing && st == step;
            ImU32 col = on ? (muted ? scaleCol(kTrackCol[t], 0.45f) : kTrackCol[t])
                           : (st % 4 == 0 ? IM_COL32(52, 52, 62, 255) : IM_COL32(40, 40, 48, 255));
            if (head) col = on ? IM_COL32(255, 245, 220, 255) : IM_COL32(85, 80, 70, 255);
            dl->AddRectFilled(a, b, col, 3 * s);
            if (hov) dl->AddRect(a, b, IM_COL32(220, 220, 230, 200), 3 * s, 0, 1.2f * s);
            if (t == T_BASS && on) {
                const char* nn = noteName(sh.bassNote[p][st].load());
                ImVec2 ts = ImGui::CalcTextSize(nn);
                if (ts.x < cell)
                    dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(10, 10, 10, 255), nn);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset pattern")) loadPreset(sh, p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this pattern back to how it started");
    ImGui::SameLine();
    if (ImGui::Button("Clear pattern"))
        for (int t = 0; t < kTracks; ++t)
            for (int st = 0; st < kSteps; ++st) sh.on[p][t][st].store(0);
    ImGui::SameLine(0, 20 * s);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "Copy to:");
    for (int q = 0; q < kPatterns; ++q) {
        if (q == p) continue;
        ImGui::SameLine();
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "%d##copy", q + 1);
        if (ImGui::Button(lbl)) {
            for (int t = 0; t < kTracks; ++t)
                for (int st = 0; st < kSteps; ++st) sh.on[q][t][st].store(sh.on[p][t][st].load());
            for (int st = 0; st < kSteps; ++st) sh.bassNote[q][st].store(sh.bassNote[p][st].load());
            logf("Copied pattern %d -> %d", p + 1, q + 1);
        }
    }
}

void drawEffects(App& app, GuiState& gs) {
    const float s = gs.scale;
    Controller& ctl = app.controller();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1), "Hold a button (or its key) to play the effect. Let go to stop.");
    ImGui::Spacing();
    const char* keys = "QWERTYUIASDFGHJK";
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = 6 * s;
    const float w = (avail - gap * 7) / 8.f;
    const float h = std::min(w * 0.8f, 70 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int f = 0; f < FX_COUNT; ++f) {
        if (f % 8) ImGui::SameLine(0, gap);
        ImGui::PushID(f);
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("fx", ImVec2(w, h));
        if (ImGui::IsItemActivated()) app.virtualPress(f % 8, 6 + f / 8, true);
        if (ImGui::IsItemDeactivated()) app.virtualPress(f % 8, 6 + f / 8, false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", fxName(f), kFxHelp[f]);
        const bool held = ctl.fxHeld(f);
        const bool hov = ImGui::IsItemHovered();
        ImVec2 b(a.x + w, a.y + h);
        ImU32 base = f < 8 ? IM_COL32(235, 90, 60, 255) : (f < 13 ? IM_COL32(90, 200, 120, 255) : IM_COL32(240, 180, 50, 255));
        dl->AddRectFilled(a, b, held ? base : scaleCol(base, 0.25f), 5 * s);
        if (hov) dl->AddRect(a, b, IM_COL32(230, 230, 240, 220), 5 * s, 0, 1.5f * s);
        centeredText(dl, ImGui::GetFont(), ImGui::GetFontSize(), a, ImVec2(b.x, b.y - 8 * s),
                     held ? IM_COL32(10, 10, 10, 255) : IM_COL32(235, 235, 240, 255), kFxTitle[f]);
        char k[2] = {keys[f], 0};
        ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
        dl->AddText(small, 12 * s, ImVec2(a.x + 6 * s, b.y - 16 * s), held ? IM_COL32(20, 20, 20, 200) : IM_COL32(150, 150, 160, 200), k);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
                       "Tip: hold Noise Riser for a few seconds and let go on the 1 for a drop. Effects stack.");
}

void drawDevices(App& app, GuiState& gs, double now) {
    const float s = gs.scale;
    MidiHub& midi = app.midi();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
                       "Devices are checked every second. Plug things in or out at any time.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan now")) midi.forceRescan();
    ImGui::Spacing();

    // --- Launchpad
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

    // --- Other MIDI inputs
    ImGui::Spacing();
    ImGui::SeparatorText("Other MIDI inputs");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
                       "Keyboards and pads play the bass. Channel 10 (drum pads) plays the drums.");
    auto kbs = midi.keyboards();
    if (kbs.empty()) ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1), "None plugged in.");
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
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1), "%s",
                           !k.enabled ? "(off)" : (k.open ? "" : "(in use by another app?)"));
        ImGui::PopID();
    }

    // --- Audio
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
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1),
                       "If the chosen device is unplugged, sound moves to the default and comes back when it returns.");
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
    st.WindowRounding = 0.f;
    st.ChildRounding = 8.f;
    st.FrameRounding = 5.f;
    st.PopupRounding = 6.f;
    st.GrabRounding = 4.f;
    st.TabRounding = 5.f;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(8, 7);
    st.WindowPadding = ImVec2(14, 12);
    st.GrabMinSize = 12.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kBg);
    c[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(kPanel);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.6f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.21f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.23f, 0.28f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.34f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.24f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.27f, 0.32f, 1.f);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.75f, 0.3f, 1.f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Header] = ImVec4(0.30f, 0.24f, 0.14f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.30f, 0.16f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.34f, 0.16f, 1.f);
    c[ImGuiCol_Tab] = ImVec4(0.17f, 0.17f, 0.21f, 1.f);
    c[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.30f, 0.16f, 1.f);
    c[ImGuiCol_TabSelected] = ImVec4(0.45f, 0.33f, 0.12f, 1.f);
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.36f, 1.f);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.f);
    st.ScaleAllSizes(scale);
}

void drawGui(App& app, GuiState& gs, double now) {
    const float s = gs.scale;
    handleKeys(app, gs);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("LP-DNB", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    drawTopBar(app, gs, now);
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = std::clamp(avail * 0.42f, 360 * s, 620 * s);

    ImGui::BeginChild("launchpad", ImVec2(leftW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    drawLaunchpad(app, gs, leftW - 2 * ImGui::GetStyle().WindowPadding.x - 10 * s, now);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    const float logH = ImGui::GetTextLineHeightWithSpacing() * 5 + 20 * s;
    ImGui::BeginChild("right", ImVec2(0, ImGui::GetContentRegionAvail().y - logH - ImGui::GetStyle().ItemSpacing.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Sequencer", nullptr, gs.forceTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawSequencer(app, gs);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Effects", nullptr, gs.forceTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawEffects(app, gs);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Devices", nullptr, gs.forceTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawDevices(app, gs, now);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    gs.forceTab = -1;
    ImGui::EndChild();

    ImGui::BeginChild("log", ImVec2(0, logH), ImGuiChildFlags_AlwaysUseWindowPadding);
    for (const auto& l : Log::get().tail(5)) ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.66f, 1), "%s", l.c_str());
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::End();
}

}  // namespace dnb
