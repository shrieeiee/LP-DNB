// lp-tenori's window contents (Dear ImGui). main_win32.cpp creates the window
// and calls drawTenoriGui() once per frame when built as lp-tenori.
#pragma once

namespace dnb {

class App;
class Tenori;
struct GuiState;

void drawTenoriGui(App& app, Tenori& t, GuiState& gs, double now);

}  // namespace dnb
