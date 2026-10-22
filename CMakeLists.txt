cmake_minimum_required(VERSION 3.16)
project(lp_dnb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# RtMidi (MIDI) and miniaudio (audio) are bundled in third_party/, so no
# downloads or Git are needed to build.
set(rtmidi_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi)
set(miniaudio_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

set(CORE_SOURCES
  src/app.cpp
  src/styles.cpp
  src/storage.cpp
  src/engine.cpp
  src/controller.cpp
  src/launchpad.cpp
  ${rtmidi_SOURCE_DIR}/RtMidi.cpp)

if(WIN32)
  # Windows: the GUI app (Dear ImGui on Direct3D 11).
  add_executable(lp-dnb WIN32
    ${CORE_SOURCES}
    src/gui.cpp
    src/fonts_data.cpp
    src/main_win32.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(lp-dnb PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(lp-dnb PRIVATE d3d11 d3dcompiler dxgi dwmapi)
  # A windowed app: no console window (MSVC needs the entry point spelled out with WinMain).
  if(MSVC)
    target_link_options(lp-dnb PRIVATE /SUBSYSTEM:WINDOWS)
  else()
    target_link_options(lp-dnb PRIVATE -mwindows)
  endif()
else()
  # macOS / Linux: console version (same synth and light show, no window).
  add_executable(lp-dnb ${CORE_SOURCES} src/main_console.cpp)
endif()
target_include_directories(lp-dnb PRIVATE src ${rtmidi_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
# Keep MIDI port names stable when devices are plugged in/out (no index suffix).
target_compile_definitions(lp-dnb PRIVATE RTMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES)

if(WIN32)
  target_compile_definitions(lp-dnb PRIVATE __WINDOWS_MM__ NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(lp-dnb PRIVATE winmm)
elseif(APPLE)
  target_compile_definitions(lp-dnb PRIVATE __MACOSX_CORE__)
  target_link_libraries(lp-dnb PRIVATE "-framework CoreMIDI" "-framework CoreAudio"
                        "-framework CoreFoundation" "-framework AudioToolbox")
else()
  find_package(ALSA)
  find_package(Threads REQUIRED)
  if(ALSA_FOUND)
    target_compile_definitions(lp-dnb PRIVATE __LINUX_ALSA__)
    target_link_libraries(lp-dnb PRIVATE ALSA::ALSA)
  else()
    message(WARNING "ALSA dev headers not found (install libasound2-dev): building without MIDI support")
  endif()
  target_link_libraries(lp-dnb PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(MSVC)
  target_compile_options(lp-dnb PRIVATE /W3 /O2 /fp:fast)
else()
  target_compile_options(lp-dnb PRIVATE -Wall -Wextra -O2)
endif()

# Make "Start" in Visual Studio run lp-dnb instead of ALL_BUILD.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT lp-dnb)
