// The window contents (Dear ImGui). Platform code creates the window and
// calls drawGui() once per frame.
#pragma once

struct ImFont;

namespace dnb {

class App;

struct GuiState {
    float scale = 1.f;          // DPI scale
    ImFont* smallFont = nullptr;  // pad labels; falls back to the default font
    ImFont* bigFont = nullptr;    // Play button
    int tab = 0;                  // which page is showing on the right
    bool keyHeld[64] = {};
    int popupTrack = -1, popupStep = -1;  // step being edited in the right-click menu
    int forceTab = -1;  // select a tab programmatically (0..4), then resets
    // pattern management popups
    bool openNew = false, openRename = false, openDelete = false;
    bool newCopy = true;
    int target = -1;
    char nameBuf[64] = {};
    int pianoHeld = -1;  // on-screen keyboard note held with the mouse
};

void setupGuiStyle(float scale);
void drawGui(App& app, GuiState& gs, double now);

}  // namespace dnb
