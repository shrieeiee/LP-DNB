// lp-dnb Windows GUI: Win32 window + Direct3D 11 + Dear ImGui.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <d3d11.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>

#include "app.h"
#include "fonts_data.h"
#include "common.h"
#include "gui.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "log.h"
#include "storage.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gRtv = nullptr;
UINT gResizeW = 0, gResizeH = 0;
bool gOccluded = false;
std::vector<std::string>* gDropped = nullptr;  // files dragged onto the window
float* gDropXY = nullptr;

void createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        gDevice->CreateRenderTargetView(back, nullptr, &gRtv);
        back->Release();
    }
}

void cleanupRenderTarget() {
    if (gRtv) { gRtv->Release(); gRtv = nullptr; }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // no GPU driver: fall back to software rendering
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (hr != S_OK) return false;
    createRenderTarget();
    return true;
}

void cleanupDevice() {
    cleanupRenderTarget();
    if (gSwapChain) { gSwapChain->Release(); gSwapChain = nullptr; }
    if (gContext) { gContext->Release(); gContext = nullptr; }
    if (gDevice) { gDevice->Release(); gDevice = nullptr; }
}

// Freeze watchdog: if the window stops updating for a few seconds, write down
// what it was doing to lp-dnb-freeze-log.txt next to the save file, so a hang
// that only happens after a long time can be tracked down.
std::atomic<double> gHeartbeat{0.0};
std::atomic<bool> gWatchQuit{false};

void watchdog() {
    bool reported = false;
    while (!gWatchQuit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const double stalled = dnb::monotonicSec() - gHeartbeat.load();
        if (stalled < 4.0) { reported = false; continue; }
        if (reported) continue;
        reported = true;
        try {
            const auto path = std::filesystem::u8path(dnb::dataFilePath()).parent_path() / "lp-dnb-freeze-log.txt";
            std::filesystem::create_directories(path.parent_path());
            if (FILE* f = _wfopen(path.wstring().c_str(), L"a")) {
                std::fprintf(f, "%s  window stopped updating while: %s (running %.0f s)\n", dnb::timeStamp().c_str(),
                             dnb::uiPhase().load(), dnb::monotonicSec());
                std::fclose(f);
            }
        } catch (...) {
        }
    }
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            gResizeW = LOWORD(lParam);
            gResizeH = HIWORD(lParam);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 1000;
            mmi->ptMinTrackSize.y = 680;
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            POINT pt{};
            DragQueryPoint(drop, &pt);
            if (gDropXY) { gDropXY[0] = static_cast<float>(pt.x); gDropXY[1] = static_cast<float>(pt.y); }
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n && gDropped; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring w(len, L'\0');
                DragQueryFileW(drop, i, &w[0], len + 1);
                const int u = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string s8(static_cast<size_t>(u > 0 ? u - 1 : 0), '\0');
                if (u > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s8[0], u, nullptr, nullptr);
                gDropped->push_back(s8);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // Alt doesn't open a menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


}  // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // This is a windowed app. If it was ever built or started with its own
    // console window, close that window (but leave a terminal we were run from alone).
    if (HWND con = GetConsoleWindow()) {
        DWORD procs[2];
        if (GetConsoleProcessList(procs, 2) <= 1) {
            ShowWindow(con, SW_HIDE);
            FreeConsole();
        }
    }
    dnb::Log::get().setEcho(false);  // no console to print to
    timeBeginPeriod(1);              // 1 ms sleeps: smooth frame pacing and MIDI clock
    gHeartbeat = dnb::monotonicSec();
    std::thread watch(watchdog);
    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, hInst, nullptr, LoadCursor(nullptr, IDC_ARROW),
                   nullptr, nullptr, L"LP-DNB", nullptr};
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(static_cast<int>(1440 * dpi), static_cast<int>(work.right - work.left));
    const int h = std::min(static_cast<int>(900 * dpi), static_cast<int>(work.bottom - work.top));
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"LP-DNB  \u2013  groovebox for Launchpad Mini", WS_OVERLAPPEDWINDOW,
                              work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                              w, h, nullptr, nullptr, wc.hInstance, nullptr);

    if (!createDevice(hwnd)) {
        cleanupDevice();
        MessageBoxW(nullptr, L"Couldn't start Direct3D 11.", L"LP-DNB", MB_ICONERROR);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout, nothing to save

    dnb::GuiState gs;
    gs.scale = dpi;
    gDropped = &gs.dropped;
    static float dropXY[2] = {-1.f, -1.f};
    gDropXY = dropXY;
    DragAcceptFiles(hwnd, TRUE);
    dnb::setupGuiStyle(dpi);

    // Built-in rounded font (Nunito): normal text, pad labels, and the big Play button.
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // the data lives in the program, ImGui mustn't free it
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dnb::kFontBody), static_cast<int>(dnb::kFontBodySize), 19.f * dpi, &cfg);
    gs.smallFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dnb::kFontBold), static_cast<int>(dnb::kFontBoldSize), 14.5f * dpi, &cfg);
    gs.bigFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dnb::kFontBold), static_cast<int>(dnb::kFontBoldSize), 24.f * dpi, &cfg);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDevice, gContext);

    dnb::App app;

    bool done = false;
    double frameStart = dnb::monotonicSec();
    while (!done) {
        gHeartbeat = dnb::monotonicSec();
        dnb::uiPhase() = "handling window messages";
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // MIDI, hot-plug and Launchpad lights keep running even when minimised.
        const double now = dnb::monotonicSec();
        app.tick(now);

        if ((gOccluded && gSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || IsIconic(hwnd)) {
            Sleep(5);
            continue;
        }
        dnb::uiPhase() = "drawing";
        gOccluded = false;

        if (gResizeW != 0 && gResizeH != 0) {
            cleanupRenderTarget();
            gSwapChain->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
            gResizeW = gResizeH = 0;
            createRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        gs.dropX = dropXY[0];
        gs.dropY = dropXY[1];
        dnb::drawGui(app, gs, now);
        ImGui::Render();

        const float clear[4] = {0.07f, 0.07f, 0.086f, 1.f};
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        gContext->ClearRenderTargetView(gRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // No vsync wait: with the screen asleep there's no vsync, and waiting for it can
        // hang. Pace to about 60 frames a second ourselves instead.
        dnb::uiPhase() = "presenting the frame";
        HRESULT hr = gSwapChain->Present(0, 0);
        gOccluded = hr == DXGI_STATUS_OCCLUDED;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            // Graphics driver restarted (it happens after sleep): rebuild the device.
            ImGui_ImplDX11_Shutdown();
            cleanupDevice();
            if (!createDevice(hwnd)) { Sleep(500); continue; }
            ImGui_ImplDX11_Init(gDevice, gContext);
        }
        dnb::uiPhase() = "idle";
        const double target = frameStart + 1.0 / 60.0;
        double t = dnb::monotonicSec();
        if (t < target) Sleep(static_cast<DWORD>((target - t) * 1000.0));
        frameStart = std::max(target, dnb::monotonicSec() - 0.05);
    }

    gWatchQuit = true;
    watch.join();
    timeEndPeriod(1);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
