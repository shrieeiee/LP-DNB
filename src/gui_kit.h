// Pieces of lp-dnb's window that lp-tenori's window uses too (defined in gui.cpp).
#pragma once
#include <atomic>
#include <cstdint>

#include "common.h"
#include "controller.h"  // Color
#include "imgui.h"

namespace dnb {

class App;
struct GuiState;

namespace gui {

extern const char* kFxShort[FX_COUNT];   // two-line pad labels
extern const char* kFxTitle[FX_COUNT];   // two-line button titles
extern const char* kFxHelp[FX_COUNT];    // one-line descriptions

ImU32 guiColor(Color c);                 // screen colour for an LED colour
ImU32 ledColor(uint8_t v);               // screen colour for a Launchpad LED value
bool ledLit(uint8_t v);
ImU32 withAlpha(ImU32 c, int a);
void centeredText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 col, const char* text);
bool coloredButton(const char* label, ImVec4 col, ImVec2 size, bool active);
void statusDot(ImU32 col, float s);
float stableAvailX();                    // width as if the scrollbar were showing (no layout shake)
int pillRow(const char* id, const char* const* labels, int n, int sel, ImVec4 col, float s);
bool resetSlider(const char* id, std::atomic<float>& a, float lo, float hi, float def, const char* fmt, float width);
void meterBar(ImDrawList* dl, ImVec2 p, float w, float h, float v, ImU32 col);

// Whole pages.
void drawMixer(App& app, GuiState& gs);
void drawDevices(App& app, GuiState& gs, double now);
void drawAbout(App& app, GuiState& gs);  // version, credits and licences

}  // namespace gui
}  // namespace dnb
