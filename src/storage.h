// Saving and loading between sessions: your own patterns, edits to the
// built-in ones, and settings. Stored as a readable text file:
//   Windows: %APPDATA%\LP-DNB\lp-dnb-data.txt
//   macOS:   ~/Library/Application Support/LP-DNB/lp-dnb-data.txt
//   Linux:   ~/.config/lp-dnb/lp-dnb-data.txt
#pragma once
#include <map>
#include <set>
#include <string>

#include "common.h"

namespace dnb {

struct Settings {
    std::string styleName, patternName;  // what was playing
    float bpm = 0.f;                     // 0 = use the style's tempo
    float volume = 1.f;
    std::string audioDevice;             // "" = system default
    std::string leadPatch = "Supersaw lead";
    float leadVolume = 0.8f;
    int leadOctave = 0;
    bool leadSnap = false;
    std::map<std::string, int> routes;   // MIDI input name -> what it plays
    std::set<std::string> disabledInputs;
    int audioBuffer = 256;               // frames per audio period
    bool audioExclusive = false;         // Windows: WASAPI exclusive mode
    std::string clockOut, clockIn;       // MIDI clock output / input port ("" = off)
    // Sampled drums: style name -> slot -> WAV path ("" = synth).
    std::map<std::string, std::map<int, std::string>> samples;
};

std::string dataFilePath();
// Where recordings and MIDI exports go (Music/LP-DNB; created if needed).
std::string outputDir();
// "2026-10-22 14-03-11"
std::string timeStamp();
// Replaces characters that aren't allowed in file names.
std::string safeFileName(const std::string& s);
std::string serialize(const Shared& sh, const Settings& st);
// Loads patterns into `sh` (after the built-ins are loaded) and fills `st`.
// Returns false if there was no file; `message` says what happened.
bool loadData(Shared& sh, Settings& st, std::string& message);
bool writeData(const std::string& text, std::string& error);

}  // namespace dnb
