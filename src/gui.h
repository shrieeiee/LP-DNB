// The window contents (Dear ImGui). Platform code creates the window and
// calls drawGui() once per frame.
#pragma once

struct ImFont;

namespace dnb {

class App;

struct GuiState {
    float scale = 1.f;          // DPI scale
    ImFont* smallFont = nullptr;  // pad labels; falls back to the default font
    bool keyHeld[64] = {};
    int bassPopupStep = -1;
    int forceTab = -1;  // select a tab programmatically (0..2), then resets
};

void setupGuiStyle(float scale);
void drawGui(App& app, GuiState& gs, double now);

}  // namespace dnb
