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

add_executable(lp-dnb
  src/main.cpp
  src/engine.cpp
  src/controller.cpp
  src/launchpad.cpp
  ${rtmidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(lp-dnb PRIVATE src ${rtmidi_SOURCE_DIR} ${miniaudio_SOURCE_DIR})

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
    target_compile_definitions(lp-dnb PRIVATE __RTMIDI_DUMMY__)
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
