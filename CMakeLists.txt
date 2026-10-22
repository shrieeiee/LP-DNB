cmake_minimum_required(VERSION 3.16)
project(lp_dnb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Dependencies are downloaded at configure time. We only take their sources
# (SOURCE_SUBDIR points nowhere so their own build systems are not run).
include(FetchContent)
FetchContent_Declare(rtmidi
  GIT_REPOSITORY https://github.com/thestk/rtmidi.git
  GIT_TAG 6.0.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none_)
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.22
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none_)
FetchContent_MakeAvailable(rtmidi miniaudio)

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
