#include "gui.h"
#include "gui_kit.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "log.h"
#include "notices_data.h"
#include "styles.h"

namespace dnb {
namespace gui {  // building blocks; gui_kit.h shares some with lp-tenori's window

// ------------------------------------------------------- file dialogs --
#ifdef _WIN32
std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
    return out;
}
std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}
constexpr bool kHaveDialogs = true;
std::string pickWavFile() {
    wchar_t buf[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"WAV files (*.wav)\0*.wav;*.wave\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = L"Choose a drum sample";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) ? toUtf8(buf) : std::string();
}
std::string pickFolder() {
    static bool com = false;
    if (!com) { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); com = true; }
    BROWSEINFOW bi{};
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = L"Choose a folder of drum samples (kick, snare, hat... in the file names)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return "";
    wchar_t buf[MAX_PATH * 4] = {};
    std::string out;
    if (SHGetPathFromIDListW(id, buf)) out = toUtf8(buf);
    CoTaskMemFree(id);
    return out;
}
void openInExplorer(const std::string& path) {
    ShellExecuteW(nullptr, L"open", fromUtf8(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#else
constexpr bool kHaveDialogs = false;
std::string pickWavFile() { return ""; }
std::string pickFolder() { return ""; }
void openInExplorer(const std::string&) {}
#endif

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
    if (t == T_LEAD) return IM_COL32(255, 160, 210, 255);   // bubblegum
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
    ImGui::SameLine(0, 8 * s);
    {   // Record the output to a WAV file.
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
            char t[32];
            const int secs = static_cast<int>(app.recordingSeconds());
            std::snprintf(t, sizeof t, "%d:%02d", secs / 60, secs % 60);
            ImFont* big = gs.bigFont ? gs.bigFont : ImGui::GetFont();
            const ImVec2 ts = big->CalcTextSizeA(big->FontSize, 1e9f, 0.f, t);
            dl->AddText(big, big->FontSize, ImVec2(a.x + 44 * s, a.y + (sz.y - ts.y) * 0.5f), IM_COL32(40, 20, 30, 255), t);
        }
        if (hov) ImGui::SetTooltip(rec ? "Stop recording" : "Record what you hear to a WAV file\n(saved in %s)", app.outputFolder().c_str());
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
        auto db = [](float v) { return v <= 1e-4f ? 0.f : std::clamp(1.f + 20.f * std::log10(v) / 36.f, 0.f, 1.f); };
        const float lv[2] = {db(sh.levelL.load()), db(sh.levelR.load())};
        const int segs = 12;
        const float rowH = (h - 16 * s) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < segs; ++i) {
                float x0 = p.x + 10 * s + i * (w - 20 * s) / segs;
                float x1 = x0 + (w - 20 * s) / segs - 3 * s;
                const float y0 = p.y + 7 * s + ch * (rowH + 2 * s);
                bool on = lv[ch] * segs > i;
                ImU32 c = i < 8 ? IM_COL32(110, 225, 150, 255) : (i < 11 ? IM_COL32(255, 196, 92, 255) : IM_COL32(255, 112, 128, 255));
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + rowH), on ? c : withAlpha(c, 45), 2 * s);
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
        const char* lbl = x == 1 && picker ? "Style" : (picker && x == 2 ? "Page-" : (picker && x == 3 ? "Page+" : kTopLabel[x]));
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
                const int k = ctl.pickerPage() * 16 + (y - 2) * 8 + x, styleIdx = app.shared().pattern.load() / kMaxSlots;
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
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(kAccent, "Rows 1-2: pick a style.  Rows 3-4: pick a pattern (yours are amber). Page-/Page+ for more.");
        ImGui::PopTextWrapPos();
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

// Right-click menu for one step: what kind of hit / which note, and its parameter locks.
void drawStepPopup(App& app, GuiState& gs, int p, const Style& st) {
    Shared& sh = app.shared();
    const float sc = gs.scale;
    if (!ImGui::BeginPopup("stepedit")) return;
    const int t = gs.popupTrack, s = gs.popupStep;
    if (t < 0 || s < 0) { ImGui::EndPopup(); return; }
    const uint8_t v = sh.on[p][t][s].load();
    const int kind = stepKind(v);
    ImGui::TextColored(kLabel, "%s, bar %d step %d", trackName(st, t).c_str(), s / kSteps + 1, s % kSteps + 1);
    ImGui::Separator();
    if (t < kDrumTracks) {
        struct Opt { const char* name; uint8_t code; };
        static const Opt opts[] = {{"Hit", STEP_ON}, {"Accent (loud)", STEP_ACCENT}, {"Ghost (soft)", STEP_SOFT},
                                   {"Roll x2", STEP_ROLL2}, {"Roll x3", STEP_ROLL3}, {"Off", STEP_OFF}};
        for (const Opt& o : opts)
            if (ImGui::Selectable(o.name, kind == o.code)) sh.on[p][t][s].store(o.code);
    } else if (t == T_LEAD) {
        int n = sh.note[p][t][s].load();
        static const char* names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
        ImGui::SetNextItemWidth(200 * sc);
        char fmt[32];
        std::snprintf(fmt, sizeof fmt, "%s%d", names[n % 12], n / 12 - 1);
        if (ImGui::SliderInt("Note", &n, 24, 108, fmt)) {
            sh.note[p][t][s].store(static_cast<int8_t>(n));
            if (!stepSounds(v)) sh.on[p][t][s].store(STEP_ON);
            app.leadKey(n, true, 100);  // let you hear it
            app.leadKey(n, false);
        }
        if (ImGui::Selectable("Hold previous note", kind == STEP_TIE)) sh.on[p][t][s].store(STEP_TIE);
        if (ImGui::Selectable("Off", kind == STEP_OFF)) sh.on[p][t][s].store(STEP_OFF);
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
    // Parameter locks: this one step can have its own pitch, length and tone.
    if (stepSounds(sh.on[p][t][s].load())) {
        ImGui::SeparatorText("Just this step");
        auto lockSlider = [&](const char* label, int l, int lo, int hi, const char* fmt, const char* tip) {
            int val = sh.lock[p][t][s][l].load();
            ImGui::SetNextItemWidth(200 * sc);
            if (ImGui::SliderInt(label, &val, lo, hi, fmt)) sh.lock[p][t][s][l].store(static_cast<int8_t>(val));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) sh.lock[p][t][s][l].store(0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nRight-click to reset", tip);
        };
        lockSlider("Pitch", LOCK_PITCH, -24, 24, "%+d semitones", "Retune this hit / note");
        if (t != T_LEAD) {
            lockSlider("Length", LOCK_DECAY, -100, 100, "%+d", "Shorter (-) or longer (+) than usual");
            lockSlider(t < kDrumTracks ? "Tone" : "Filter", LOCK_FILTER, -100, 100, "%+d", "Darker (-) or brighter (+)");
        }
        if (ImGui::SmallButton("Reset these"))
            for (int l = 0; l < kLocks; ++l) sh.lock[p][t][s][l].store(0);
    }
    ImGui::EndPopup();
}

bool hasLock(const Shared& sh, int p, int t, int s) {
    for (int l = 0; l < kLocks; ++l)
        if (sh.lock[p][t][s][l].load()) return true;
    return false;
}

// A row of pill buttons, one selected. Returns the clicked index or -1.
int pillRow(const char* id, const char* const* labels, int n, int sel, ImVec4 col, float s) {
    int clicked = -1;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, 6 * s));
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, 4 * s);
        ImGui::PushID(i);
        if (coloredButton(labels[i], col, ImVec2(0, 0), i == sel)) clicked = i;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return clicked;
}

void drawSequencer(App& app, GuiState& gs) {
    Shared& sh = app.shared();
    Controller& ctl = app.controller();
    const float s = gs.scale;
    const int p = sh.pattern.load();
    const Style& st = styleOf(p);
    const bool playing = sh.playing.load();
    const int step = sh.step.load();
    const int len = sh.patternLength(p), bars = len / kSteps;

    ImGui::TextColored(styleAccent(st), "%s", st.name.c_str());
    ImGui::SameLine();
    ImGui::Text("- %s", sh.patternName[p].c_str());
    ImGui::SameLine();
    ImGui::TextColored(kHint, "   %s, %.0f BPM, swing %d%%", st.keyName.c_str(), st.bpm,
                       static_cast<int>(std::round((st.swing - 0.5f) / 0.16f * 100.f)));
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
                                  gi == p ? " (playing)" : " - click to switch at the end of this pattern");
        }
        if (count < kMaxSlots) {
            if (ImGui::GetItemRectMax().x + 8 * s + 70 * s <= maxX) ImGui::SameLine(0, 8 * s);
            if (ImGui::Button("+ New")) gs.openNew = true;
        }
    }

    // Length and which bar is showing.
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kLabel, "LENGTH");
        ImGui::SameLine();
        static const char* lens[4] = {"1 bar", "2 bars", "3 bars", "4 bars"};
        const int c = pillRow("len", lens, 4, bars - 1, kLilac, s);
        if (c >= 0 && c + 1 != bars) setPatternLength(sh, p, (c + 1) * kSteps);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Making it longer repeats what's there, ready to vary");
        // The Launchpad's bar buttons (BPM-/+ in EDIT view) move this too.
        if (ctl.editBarPresses() != gs.syncedPresses) {
            gs.syncedPresses = ctl.editBarPresses();
            gs.seqBar = ctl.editBar();
            gs.follow = false;
        }
        if (bars > 1) {
            ImGui::SameLine(0, 18 * s);
            ImGui::TextColored(kLabel, "SHOWING");
            ImGui::SameLine();
            const int playingBar = playing ? step / kSteps : -1;
            if (gs.follow && playing) gs.seqBar = playingBar;
            gs.seqBar = std::clamp(gs.seqBar, 0, bars - 1);
            static const char* barNames[4] = {"1", "2", "3", "4"};
            const int b = pillRow("bar", barNames, bars, gs.seqBar, kAccent, s);
            if (b >= 0) { gs.seqBar = b; gs.follow = false; }
            ImGui::SameLine(0, 10 * s);
            ImGui::Checkbox("Follow", &gs.follow);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show whichever bar is playing");
        } else {
            gs.seqBar = 0;
        }
        ctl.setEditBar(gs.seqBar);  // the Launchpad's EDIT view shows the same bar
    }
    const int bar = gs.seqBar;

    const float muteW = 36 * s, nameW = 128 * s, groupGap = 10 * s, cellGap = 4 * s;
    const float avail = stableAvailX();
    const float cell = std::max(14 * s, (avail - muteW - nameW - 16 * s - groupGap * 3 - cellGap * 15) / 16.f);
    const float rowH = std::max(24 * s, std::min(cell, 34 * s));
    const float cr = std::min(cell, rowH) * 0.34f;  // step corner radius
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* small = gs.smallFont ? gs.smallFont : ImGui::GetFont();
    const bool recLead = sh.recordLead.load();

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
            if (ImGui::InvisibleButton("name", ImVec2(nameW, rowH)) && t < kPadTracks) ctl.selectTrack(t);
            const bool hov = ImGui::IsItemHovered();
            ImVec2 b(a.x + nameW, a.y + rowH);
            if (sel || hov) dl->AddRectFilled(a, b, sel ? IM_COL32(86, 70, 128, 255) : IM_COL32(58, 53, 80, 255), rowH * 0.5f);
            const float th = ImGui::GetTextLineHeight();
            const std::string nm = t == T_LEAD ? std::string("Lead") : trackName(st, t);
            dl->PushClipRect(a, b, true);
            dl->AddText(ImVec2(a.x + 8 * s, a.y + (rowH - th) * 0.5f), muted ? IM_COL32(110, 110, 120, 255) : tcol, nm.c_str());
            if (t == T_LEAD && recLead && std::fmod(ImGui::GetTime(), 0.8) < 0.5)
                dl->AddCircleFilled(ImVec2(b.x - 12 * s, a.y + rowH * 0.5f), 5 * s, IM_COL32(255, 90, 100, 255));
            dl->PopClipRect();
            if (hov) {
                if (t == T_LEAD) ImGui::SetTooltip("What you recorded from the lead keyboard.\nPress Rec lead below, then play.");
                else ImGui::SetTooltip("%s\nClick to select it for the Launchpad's EDIT view", nm.c_str());
            }
        }
        ImGui::SameLine(0, 10 * s);

        for (int c = 0; c < kSteps; ++c) {
            const int stp = bar * kSteps + c;
            if (c) ImGui::SameLine(0, c % 4 == 0 ? groupGap + cellGap : cellGap);
            ImGui::PushID(c);
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
                    if (t == T_LEAD) sh.note[p][t][stp].store(static_cast<int8_t>(sh.leadNote.load()));
                    else if (t >= T_BASS) sh.note[p][t][stp].store(static_cast<int8_t>(ctl.lastBassIndex()));
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
            ImU32 col = c % 4 == 0 ? kCellBeat : kCell;
            if (on) col = kind == STEP_SOFT ? scaleCol(base, 0.5f) : base;
            if (head) col = on ? IM_COL32(255, 248, 235, 255) : IM_COL32(96, 88, 124, 255);
            if (kind == STEP_TIE) {
                // A hold: a bar joining it to the previous note.
                dl->AddRectFilled(a, b, c % 4 == 0 ? kCellBeat : kCell, cr);
                dl->AddRectFilled(ImVec2(a.x - (c ? cellGap + 1 : 0), a.y + rowH * 0.32f), ImVec2(b.x, b.y - rowH * 0.32f),
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
            if (on && hasLock(sh, p, t, stp))  // a little notch: this step has its own settings
                dl->AddRectFilled(ImVec2(a.x + cell * 0.3f, a.y + 2 * s), ImVec2(b.x - cell * 0.3f, a.y + 5 * s), IM_COL32(30, 20, 40, 230), 2 * s);
            if (hov) dl->AddRect(a, b, IM_COL32(240, 235, 255, 220), cr, 0, 2.f * s);
            if (t >= T_BASS && on) {
                const int deg = sh.note[p][t][stp].load();
                std::string txt;
                if (t == T_BASS) txt = std::string(noteNameFor(st, degreeSemis(st, deg))) + (deg >= 7 ? "'" : "");
                else if (t == T_CHORD) txt = chordNumeral(st, deg, 3);
                else {
                    static const char* nn[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
                    txt = nn[((deg % 12) + 12) % 12];
                }
                const float fs = std::min(ImGui::GetFontSize(), 14.f * s);
                ImVec2 ts = small->CalcTextSizeA(fs, 1e9f, 0.f, txt.c_str());
                if (ts.x < cell - 2 * s)
                    dl->AddText(small, fs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(10, 10, 10, 255), txt.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.47f, 1),
                       "Click a step to toggle it. Right-click for accents, rolls, notes, slides, holds and per-step pitch / length / tone.");

    // Pattern buttons.
    const bool builtin = p % kMaxSlots < kFactorySlots;
    const bool full = sh.slotCount[p / kMaxSlots].load() >= kMaxSlots;
    if (full) ImGui::BeginDisabled();
    if (ImGui::Button("Duplicate")) app.newPattern(sh.patternName[p] + " copy", true);
    if (full) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(full ? "This style already has 16 of your patterns" : "Make a copy of this pattern as one of your own");
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
    if (ImGui::Button("Clear")) {
        clearPattern(sh, p);
        sh.length[p].store(static_cast<uint8_t>(len));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn every step off");
    ImGui::SameLine(0, 16 * s);
    {
        if (coloredButton(recLead ? "Recording lead##rl" : "Rec lead##rl", kRed, ImVec2(0, 0), recLead)) {
            sh.recordLead.store(!recLead);
            if (!recLead && !playing) sh.send(Cmd::Play);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Play the lead keyboard (or the on-screen keys) and the notes go\ninto this pattern's Lead row, snapped to the nearest step.");
        ImGui::SameLine();
        if (ImGui::Button("Clear lead"))
            for (int k = 0; k < kMaxSteps; ++k) {
                sh.on[p][T_LEAD][k].store(STEP_OFF);
                for (int l = 0; l < kLocks; ++l) sh.lock[p][T_LEAD][k][l].store(0);
            }
    }
    ImGui::SameLine(0, 16 * s);
    if (ImGui::Button("Export MIDI")) app.exportMidi();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves this pattern as a .mid file (drums, bass, chords, lead)\nfor your DAW, in %s", app.outputFolder().c_str());

    // Feel.
    ImGui::SeparatorText("Feel");
    {
        float hz = sh.humanize.load(), hs = sh.hatSwing.load();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Humanise");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130 * s);
        if (ImGui::SliderFloat("##hum", &hz, 0.f, 1.f, "")) sh.humanize.store(hz);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tiny random timing and loudness changes, like a real drummer");
        ImGui::SameLine(0, 20 * s);
        ImGui::TextUnformatted("Hat swing");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130 * s);
        if (ImGui::SliderFloat("##hsw", &hs, 0.f, 1.f, "")) sh.hatSwing.store(hs);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Swings only the hats and shakers, so the kick and snare stay tight");
        ImGui::SameLine(0, 20 * s);
        ImGui::TextUnformatted("Drum fill every");
        ImGui::SameLine();
        static const char* fills[4] = {"Off", "4 bars", "8 bars", "16 bars"};
        static const int fillN[4] = {0, 4, 8, 16};
        int fi = 0;
        for (int i = 0; i < 4; ++i) if (fillN[i] == sh.fillEvery.load()) fi = i;
        const int c = pillRow("fill", fills, 4, fi, kLilac, s);
        if (c >= 0) sh.fillEvery.store(fillN[c]);
    }

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

// A slider that resets on right-click.
bool resetSlider(const char* id, std::atomic<float>& a, float lo, float hi, float def, const char* fmt, float width) {
    float v = a.load();
    ImGui::SetNextItemWidth(width);
    bool ch = ImGui::SliderFloat(id, &v, lo, hi, fmt);
    if (ch) a.store(v);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { a.store(def); ch = true; }
    return ch;
}

void meterBar(ImDrawList* dl, ImVec2 p, float w, float h, float v, ImU32 col) {
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kWell, h * 0.5f);
    const float k = std::clamp(v, 0.f, 1.f);
    if (k > 0.002f) dl->AddRectFilled(p, ImVec2(p.x + std::max(h, w * k), p.y + h), col, h * 0.5f);
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

    Shared& sh = app.shared();
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delay feedback: how long the echoes keep going");
    ImGui::SameLine(0, 18 * s);
    ImGui::TextUnformatted("Reverb size");
    ImGui::SameLine();
    resetSlider("##rs", sh.reverbSize, 0.f, 1.f, 0.6f, "%.2f", 140 * s);
    ImGui::SameLine(0, 18 * s);
    ImGui::TextUnformatted("Chorus");
    ImGui::SameLine();
    resetSlider("##ch", sh.chorus, 0.f, 1.f, 0.35f, "%.2f", 140 * s);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stereo chorus on pads, keys and chords");
    ImGui::TextColored(kHint, "Per-sound reverb and delay sends are in the Mixer. Right-click a slider to reset it.");
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

// ---------------------------------------------------------------- mixer --

std::string channelName(const Style& st, int ch) {
    if (ch == kBassSlot) return st.bass.name;
    if (ch == kChordSlot) return st.chord.name;
    if (ch == kLeadSlot) return "Lead synth";
    return st.drums[static_cast<size_t>(ch)].name;
}
ImU32 channelColor(const Style& st, int ch) {
    if (ch == kBassSlot) return trackColor(st, T_BASS);
    if (ch == kChordSlot) return trackColor(st, T_CHORD);
    if (ch == kLeadSlot) return trackColor(st, T_LEAD);
    return guiColor(slotColor(st, ch));
}

void drawMixer(App& app, GuiState& gs) {
    const float s = gs.scale;
    Shared& sh = app.shared();
    const int styleIdx = sh.pattern.load() / kMaxSlots;
    const Style& st = styles()[static_cast<size_t>(styleIdx)];
    ImGui::TextColored(kHint, "Levels and sound shaping for %s (each style keeps its own). Right-click any slider to reset it.",
                       st.name.c_str());
    {
        if (ImGui::Button("Load kit folder...")) {
            const std::string f = kHaveDialogs ? pickFolder() : std::string(gs.pathBuf);
            if (!f.empty()) app.loadKitFolder(styleIdx, f);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pick a folder of WAVs: files with kick, snare, clap, hat, ride, tom...\nin their names are matched to this style's drums.");
        ImGui::SameLine();
        if (ImGui::Button("All drums back to synth"))
            for (int k = 0; k < kDrumSlots; ++k)
                if (!app.drumSample(styleIdx, k).empty()) app.setDrumSample(styleIdx, k, "");
        ImGui::SameLine(0, 12 * s);
        ImGui::TextColored(kHint, "Or drag WAV files / a folder onto a row.");
        if (!kHaveDialogs) {
            ImGui::SetNextItemWidth(380 * s);
            ImGui::InputTextWithHint("##path", "Path to a .wav file or kit folder", gs.pathBuf, sizeof gs.pathBuf);
        }
    }

    static const int order[kChannels] = {0, 1, 2, 3, 4, 5, kExtraSlot, kBassSlot, kChordSlot, kLeadSlot};
    for (int k = 0; k < 8; ++k) gs.rowY0[k] = gs.rowY1[k] = -1.f;
    const ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * s, 4 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 9 * s);  // slim handles so the values stay readable
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.025f));
    if (ImGui::BeginTable("mixer", 10, tf)) {
        const char* heads[10] = {"Sound", "Level", "Pan", "Reverb", "Delay", "Tune", "Length", "Tone", "Solo", "Source"};
        const float weights[10] = {1.5f, 1.3f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.45f, 1.3f};
        for (int i = 0; i < 10; ++i) ImGui::TableSetupColumn(heads[i], ImGuiTableColumnFlags_WidthStretch, weights[i]);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int i = 0; i < 10; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::TextColored(kLabel, "%s", heads[i]);
        }
        for (int ch : order) {
            ChannelMix& m = sh.mix[styleIdx][ch];
            const bool drum = ch < kDrumSlots;
            ImGui::TableNextRow();
            ImGui::PushID(ch);
            ImGui::TableSetColumnIndex(0);
            const float rowTop = ImGui::GetCursorScreenPos().y;
            ImGui::AlignTextToFramePadding();
            const std::string nm = channelName(st, ch);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(channelColor(st, ch)));
            ImGui::TextUnformatted(nm.c_str());
            ImGui::PopStyleColor();
            auto cellW = [] { return -FLT_MIN; };
            ImGui::TableSetColumnIndex(1);
            resetSlider("##vol", m.volume, 0.f, 1.5f, 1.f, "%.2f", cellW());
            ImGui::TableSetColumnIndex(2);
            resetSlider("##pan", m.pan, -1.f, 1.f, 0.f, "%+.2f", cellW());
            ImGui::TableSetColumnIndex(3);
            resetSlider("##rev", m.reverb, 0.f, 1.f, 0.f, "%.2f", cellW());
            ImGui::TableSetColumnIndex(4);
            resetSlider("##dly", m.delay, 0.f, 1.f, 0.f, "%.2f", cellW());
            ImGui::TableSetColumnIndex(5);
            if (ch != kLeadSlot) resetSlider("##tune", m.tune, -12.f, 12.f, 0.f, "%+.0f st", cellW());
            ImGui::TableSetColumnIndex(6);
            if (drum) resetSlider("##dec", m.decay, -1.f, 1.f, 0.f, "%+.2f", cellW());
            ImGui::TableSetColumnIndex(7);
            if (ch != kLeadSlot) resetSlider("##tone", m.tone, -1.f, 1.f, 0.f, "%+.2f", cellW());
            ImGui::TableSetColumnIndex(8);
            {
                const bool solo = m.solo.load();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4 * s));
                if (coloredButton("S", kAccent, ImVec2(-FLT_MIN, 0), solo)) m.solo.store(!solo);
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo: hear only the soloed sounds");
            }
            ImGui::TableSetColumnIndex(9);
            if (drum) {
                const std::string path = app.drumSample(styleIdx, ch);
                std::string label = path.empty() ? std::string("Synth") : std::filesystem::u8path(path).filename().u8string();
                if (label.size() > 16) label = label.substr(0, 15) + "~";
                label += "##src";
                if (coloredButton(label.c_str(), kGreen, ImVec2(-FLT_MIN, 0), !path.empty())) ImGui::OpenPopup("src");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.empty() ? "Synthesised. Click to use a WAV sample instead." : path.c_str());
                if (ImGui::BeginPopup("src")) {
                    if (ImGui::Selectable("Load a WAV file...")) {
                        const std::string f = kHaveDialogs ? pickWavFile() : std::string(gs.pathBuf);
                        if (!f.empty()) app.setDrumSample(styleIdx, ch, f);
                    }
                    if (ImGui::Selectable("Use the synth sound", path.empty())) app.setDrumSample(styleIdx, ch, "");
                    ImGui::EndPopup();
                }
            } else {
                ImGui::TextColored(kHint, "synth");
            }
            // Hovering this row: dropped files land here.
            if (drum) {
                gs.rowY0[ch] = rowTop;
                gs.rowY1[ch] = rowTop + ImGui::GetFrameHeightWithSpacing();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::SeparatorText("Master");
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Stereo width");
        ImGui::SameLine();
        resetSlider("##width", sh.width, 0.f, 1.5f, 1.f, "%.2f", 120 * s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = mono, 1 = normal, 1.5 = extra wide. Bass stays centred.");
        ImGui::SameLine(0, 18 * s);
        ImGui::TextUnformatted("Glue");
        ImGui::SameLine();
        resetSlider("##glue", sh.glue, 0.f, 1.f, 0.5f, "%.2f", 120 * s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bus compression that pulls the mix together. A limiter after it stops clipping.");
        ImGui::SameLine(0, 18 * s);
        ImGui::TextUnformatted("Kick ducking");
        ImGui::SameLine();
        resetSlider("##sc", sh.sidechain, 0.f, 1.5f, 1.f, "%.2f", 120 * s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much the bass and chords duck out of the way of the kick (sidechain)");
    }
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = 220 * s, h = 9 * s;
        ImGui::TextColored(kLabel, "OUT");
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        auto lvl = [](float v) { return v <= 1e-4f ? 0.f : std::clamp(1.f + 20.f * std::log10(v) / 48.f, 0.f, 1.f); };  // 48 dB scale
        meterBar(dl, ImVec2(p.x, p.y + 2 * s), w, h, lvl(sh.levelL.load()), IM_COL32(110, 225, 150, 255));
        meterBar(dl, ImVec2(p.x, p.y + 4 * s + h), w, h, lvl(sh.levelR.load()), IM_COL32(110, 225, 150, 255));
        ImGui::Dummy(ImVec2(w, h * 2 + 6 * s));
        ImGui::SameLine(0, 18 * s);
        const float gr = sh.gainReduction.load();
        ImGui::TextColored(kLabel, "SQUASH");
        ImGui::SameLine();
        p = ImGui::GetCursorScreenPos();
        meterBar(dl, ImVec2(p.x, p.y + 6 * s), 120 * s, h, gr / 12.f, IM_COL32(255, 196, 92, 255));
        ImGui::Dummy(ImVec2(120 * s, h * 2));
        ImGui::SameLine();
        ImGui::TextColored(kHint, "%.1f dB", gr);
        ImGui::SameLine(0, 18 * s);
        ImGui::TextColored(kHint, "CPU %.0f%%", sh.cpuLoad.load() * 100.f);
    }
}

// ----------------------------------------------------------------- song --

void drawSong(App& app, GuiState& gs) {
    const float s = gs.scale;
    Shared& sh = app.shared();
    const int p = sh.pattern.load(), styleIdx = p / kMaxSlots, count = sh.slotCount[styleIdx].load();
    bool on = sh.songMode.load();
    if (ImGui::Checkbox("Song mode", &on)) sh.songMode.store(on);
    ImGui::SameLine();
    ImGui::TextColored(kHint, "Plays this list of %s patterns in order, then loops. Press Play to start from the top.",
                       styleOf(p).name.c_str());
    ImGui::Spacing();
    int n = sh.songLen.load();
    const bool playing = sh.playing.load();
    const int pos = sh.songPos.load();
    int remove = -1, up = -1;
    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        const bool cur = on && playing && i == pos;
        ImGui::AlignTextToFramePadding();
        if (cur) ImGui::TextColored(kGreen, "> %2d", i + 1);
        else ImGui::TextColored(kLabel, "  %2d", i + 1);
        ImGui::SameLine();
        int slot = std::clamp(sh.songSlot[i].load(), 0, std::max(0, count - 1));
        ImGui::SetNextItemWidth(240 * s);
        if (ImGui::BeginCombo("##pat", sh.patternName[styleIdx * kMaxSlots + slot].c_str())) {
            for (int k = 0; k < count; ++k)
                if (ImGui::Selectable((sh.patternName[styleIdx * kMaxSlots + k] + "##" + std::to_string(k)).c_str(), k == slot))
                    sh.songSlot[i].store(k);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        int rep = sh.songRepeats[i].load();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        if (ImGui::Button("-")) rep = std::max(1, rep - 1);
        ImGui::SameLine();
        ImGui::Text("%d", rep);
        ImGui::SameLine();
        if (ImGui::Button("+")) rep = std::min(16, rep + 1);
        sh.songRepeats[i].store(rep);
        ImGui::SameLine(0, 14 * s);
        if (i > 0) {
            if (ImGui::Button("Up")) up = i;
        } else {
            ImGui::Dummy(ImVec2(ImGui::CalcTextSize("Up").x + ImGui::GetStyle().FramePadding.x * 2, ImGui::GetFrameHeight()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) remove = i;
        ImGui::PopID();
    }
    if (up > 0) {
        const int a = sh.songSlot[up].load(), b = sh.songRepeats[up].load();
        sh.songSlot[up].store(sh.songSlot[up - 1].load());
        sh.songRepeats[up].store(sh.songRepeats[up - 1].load());
        sh.songSlot[up - 1].store(a);
        sh.songRepeats[up - 1].store(b);
    }
    if (remove >= 0) {
        for (int i = remove; i < n - 1; ++i) {
            sh.songSlot[i].store(sh.songSlot[i + 1].load());
            sh.songRepeats[i].store(sh.songRepeats[i + 1].load());
        }
        sh.songLen.store(--n);
    }
    if (n == 0) ImGui::TextColored(kHint, "Empty. Add the patterns you want, in order.");
    ImGui::Spacing();
    if (n < kMaxSong && ImGui::Button("+ Add the playing pattern")) {
        sh.songSlot[n].store(p % kMaxSlots);
        sh.songRepeats[n].store(2);
        sh.songLen.store(n + 1);
    }
    ImGui::SameLine();
    if (n > 0 && ImGui::Button("Clear song")) sh.songLen.store(0);
    ImGui::Spacing();
    ImGui::TextColored(kHint, "Tip: with Record (top bar) running, song mode plays a whole track into a WAV file.");
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
    {
        static const int sizes[5] = {64, 128, 256, 512, 1024};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Buffer");
        ImGui::SameLine();
        char lbls[5][32];
        const char* ptrs[5];
        int sel = -1;
        for (int i = 0; i < 5; ++i) {
            std::snprintf(lbls[i], sizeof lbls[i], "%d (%.1f ms)", sizes[i], sizes[i] * 1000.0 / 48000.0);
            ptrs[i] = lbls[i];
            if (sizes[i] == app.audioBuffer()) sel = i;
        }
        const int c = pillRow("buf", ptrs, 5, sel, kLilac, s);
        if (c >= 0) app.setAudioBuffer(sizes[c]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smaller = pads respond faster. Go bigger if you hear crackles.");
#ifdef _WIN32
        bool ex = app.audioExclusive();
        if (ImGui::Checkbox("Exclusive mode (lowest latency; other apps go quiet)", &ex)) app.setAudioExclusive(ex);
#endif
        ImGui::TextColored(kHint, "Output latency about %.1f ms.  Audio engine CPU %.0f%%.", app.audioLatencyMs(),
                           app.shared().cpuLoad.load() * 100.f);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("MIDI clock");
    ImGui::TextColored(kHint, "Sync with drum machines, DAWs and arpeggiators: 24 ticks per beat, plus Start / Stop.");
    {
        ImGui::SetNextItemWidth(320 * s);
        const std::string& co = app.clockOutPort();
        if (ImGui::BeginCombo("Send clock to", co.empty() ? "Off" : co.c_str())) {
            if (ImGui::Selectable("Off", co.empty())) app.setClockOut("");
            for (const auto& n : midi.outputs())
                if (n != midi.launchpadOutName() && n.find("Launchpad") == std::string::npos &&
                    ImGui::Selectable(n.c_str(), n == co))
                    app.setClockOut(n);
            ImGui::EndCombo();
        }
        if (!co.empty()) {
            ImGui::SameLine();
            statusDot(app.clockOutOpen() ? IM_COL32(80, 220, 90, 255) : IM_COL32(230, 80, 60, 255), s);
            ImGui::TextColored(kHint, "%s", app.clockOutOpen() ? "sending" : "couldn't open");
        }
        ImGui::SetNextItemWidth(320 * s);
        const std::string& ci = midi.clockIn();
        if (ImGui::BeginCombo("Follow clock from", ci.empty() ? "Off (LP-DNB sets the tempo)" : ci.c_str())) {
            if (ImGui::Selectable("Off (LP-DNB sets the tempo)", ci.empty())) app.setClockIn("");
            for (const auto& k : midi.keyboards())
                if (ImGui::Selectable(k.name.c_str(), k.name == ci)) app.setClockIn(k.name);
            ImGui::EndCombo();
        }
        if (!ci.empty()) {
            ImGui::SameLine();
            const float b = app.clockInBpm();
            statusDot(b > 0.f ? IM_COL32(80, 220, 90, 255) : IM_COL32(120, 120, 130, 255), s);
            if (b > 0.f) ImGui::TextColored(kHint, "%.1f BPM", b);
            else ImGui::TextColored(kHint, "waiting for clock...");
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Recordings and exports");
    ImGui::TextColored(kHint, "%s", app.outputFolder().c_str());
#ifdef _WIN32
    ImGui::SameLine();
    if (ImGui::SmallButton("Open folder")) openInExplorer(app.outputFolder());
#endif
}


// ---------------------------------------------------------------- about --

void openUrl(const char* url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)url;
#endif
}

void linkText(const char* label, const char* url) {
    ImGui::TextColored(kLilac, "%s", label);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", url);
    }
    if (ImGui::IsItemClicked()) openUrl(url);
}

void drawAbout(App& app, GuiState& gs) {
    (void)app;
    const float s = gs.scale;
    ImFont* big = gs.bigFont ? gs.bigFont : ImGui::GetFont();
    ImGui::PushFont(big);
    ImGui::Text("LP-DNB %s", kVersion);
    ImGui::PopFont();
    ImGui::TextColored(kHint, "A groovebox for the original Novation Launchpad Mini: 12 styles, synth and sampled drums,");
    ImGui::TextColored(kHint, "a lead synth for a second keyboard, songs, recording and MIDI export.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Made by shrieeiee, written with Claude (Anthropic).");
    linkText("github.com/shrieeiee/LP-DNB", "https://github.com/shrieeiee/LP-DNB");
    ImGui::TextColored(kHint, "Not made by or affiliated with Novation. Launchpad is a trademark of Focusrite Audio Engineering Ltd.");

    ImGui::SeparatorText("Built with");
    struct Credit { const char* name; const char* who; const char* licence; const char* url; };
    static const Credit credits[] = {
        {"Dear ImGui 1.91.9b", "Omar Cornut and contributors", "MIT", "https://github.com/ocornut/imgui"},
        {"stb_truetype, stb_textedit, stb_rect_pack", "Sean Barrett (inside Dear ImGui)", "MIT / public domain", "https://github.com/nothings/stb"},
        {"RtMidi 6.0.0", "Gary P. Scavone", "MIT", "https://github.com/thestk/rtmidi"},
        {"miniaudio 0.11.22", "David Reid", "MIT-0 / public domain", "https://miniaud.io"},
        {"Nunito font", "Vernon Adams, Jacques Le Bailly, Manvel Shmavonyan, Alexei Vanyashin", "SIL Open Font License 1.1",
         "https://github.com/googlefonts/nunito"},
    };
    for (const Credit& c : credits) {
        ImGui::PushID(c.name);
        linkText(c.name, c.url);
        ImGui::SameLine();
        ImGui::TextColored(kHint, " %s  -  %s", c.who, c.licence);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextColored(kHint, "The built-in patterns follow well-known grooves; the articles they were checked against are listed in the README.");

    ImGui::SeparatorText("Licences");
    ImGui::TextColored(kHint, "The full licence texts (also in THIRD-PARTY-NOTICES.txt next to the program):");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kWell));
    ImGui::BeginChild("notices", ImVec2(0, std::max(160 * s, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(gs.smallFont ? gs.smallFont : ImGui::GetFont());
    ImGui::TextUnformatted(kThirdPartyNotices);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
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

}  // namespace gui
using namespace gui;

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

    // Files dragged onto the window: a folder = a drum kit, WAVs = the drum row under
    // the mouse in the Mixer (or the selected track, then the following ones).
    if (!gs.dropped.empty()) {
        const int style = app.shared().pattern.load() / kMaxSlots;
        int slot = app.controller().selectedTrack() < kDrumSlots ? app.controller().selectedTrack() : 0;
        if (gs.tab == 1)
            for (int k = 0; k < kDrumSlots; ++k)
                if (gs.dropY >= gs.rowY0[k] && gs.dropY < gs.rowY1[k]) slot = k;
        for (const std::string& f : gs.dropped) {
            std::error_code ec;
            if (std::filesystem::is_directory(std::filesystem::u8path(f), ec)) {
                app.loadKitFolder(style, f);
            } else if (slot < kDrumSlots) {
                if (app.setDrumSample(style, slot, f)) ++slot;
            }
        }
        gs.dropped.clear();
        gs.forceTab = 1;
    }

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
        const char* names[8] = {"Sequencer", "Mixer", "Effects", "Song", "Instrument", "Styles", "Devices", "About"};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18 * s, 10 * s));
        const float maxX = ImGui::GetCursorScreenPos().x + stableAvailX();
        for (int i = 0; i < 8; ++i) {
            const float w = ImGui::CalcTextSize(names[i]).x + 36 * s;
            if (i && ImGui::GetItemRectMax().x + 6 * s + w <= maxX) ImGui::SameLine(0, 6 * s);  // wraps when narrow
            if (coloredButton(names[i], kLilac, ImVec2(0, 0), gs.tab == i)) gs.tab = i;
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 4 * s));
    }
    switch (gs.tab) {
        case 0: drawSequencer(app, gs); break;
        case 1: drawMixer(app, gs); break;
        case 2: drawEffects(app, gs); break;
        case 3: drawSong(app, gs); break;
        case 4: drawInstrument(app, gs); break;
        case 5: drawStyles(app, gs); break;
        case 6: drawDevices(app, gs, now); break;
        default: drawAbout(app, gs); break;
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
