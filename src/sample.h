// Sampled drums: WAV loading (8/16/24/32-bit PCM and 32-bit float, mono or
// stereo, any sample rate - resampled to the engine rate) and kit folders.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace dnb {

struct Style;

struct Sample {
    std::string name, path;     // file name, full path (UTF-8)
    std::vector<float> L, R;    // at the engine's sample rate
};

std::shared_ptr<Sample> loadWav(const std::string& path, float targetRate, std::string& error);

// For a kit folder: which file suits each of a style's 7 drum sounds
// ("" = nothing suitable). Matches on names like kick/bd, snare/sd, hat/hh...
std::vector<std::string> matchKitFolder(const std::string& folder, const Style& style);

}  // namespace dnb
