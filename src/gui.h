// The window contents (Dear ImGui). Platform code creates the window and
// calls drawGui() once per frame.
#pragma once
#include <string>
#include <vector>

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
    int seqBar = 0;              // which bar of a long pattern the sequencer shows
    bool follow = true;          // ...or follow the playhead
    int syncedPresses = 0;       // Launchpad bar-button presses already followed
    std::vector<std::string> dropped;  // files dragged onto the window (UTF-8), handled next frame
    float dropX = -1.f, dropY = -1.f;  // where they were dropped (window coordinates)
    float rowY0[8] = {}, rowY1[8] = {};  // mixer drum rows on screen last frame (for drops)
    char pathBuf[512] = {};      // typed sample path (where there's no file dialog)
};

void setupGuiStyle(float scale);
void drawGui(App& app, GuiState& gs, double now);

}  // namespace dnb
