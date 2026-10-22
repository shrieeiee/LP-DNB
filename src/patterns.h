// Four starter patterns. 'x' = hit, '.' = rest. Bass rows use digits/letters
// as indexes into the F-minor bass scale (0-7), '.' = rest.
#pragma once
#include <cstring>

#include "common.h"

namespace dnb {

struct Preset {
    const char* name;
    const char* rows[kTracks];  // kick snare ghost hat ohat ride bass stab
};

inline const Preset kPresets[kPatterns] = {
    {"Two-step",
     {"x.........x.....",
      "....x.......x...",
      ".......x.x....x.",
      "x.x.x.x.x.x.x.x.",
      "..............x.",
      "................",
      "0..0..2...0..3..",
      "................"}},
    {"Amen roller",
     {"x.x.......xx....",
      "....x..x.x...x..",
      "......x.......x.",
      "................",
      "................",
      "x.x.x.x.x.x.x.x.",
      "0.......5.......",
      "x..............."}},
    {"Jungle",
     {"x.....x...x.....",
      "....x..x.x..x..x",
      ".x.........x....",
      "xxxxxx.xxxxxxx.x",
      "......x.......x.",
      "................",
      "0...0...6...4...",
      "................"}},
    {"Liquid",
     {"x.........x.....",
      "....x.......x...",
      ".........x....x.",
      "..x...x...x...x.",
      "................",
      "x.x.x.x.x.x.x.x.",
      "0.....2.....5...",
      "x.....x.......x."}},
};

inline void loadPresets(Shared& sh) {
    for (int p = 0; p < kPatterns; ++p) {
        for (int t = 0; t < kTracks; ++t) {
            const char* row = kPresets[p].rows[t];
            for (int s = 0; s < kSteps && row[s]; ++s) {
                char c = row[s];
                if (t == T_BASS) {
                    bool on = c >= '0' && c <= '7';
                    sh.on[p][t][s].store(on ? 1 : 0);
                    sh.bassNote[p][s].store(static_cast<int8_t>(on ? kBassScale[c - '0'] : 0));
                } else {
                    sh.on[p][t][s].store(c == 'x' ? 1 : 0);
                }
            }
        }
    }
}

}  // namespace dnb
