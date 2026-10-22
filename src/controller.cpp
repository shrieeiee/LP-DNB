#include "controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "log.h"
#include "styles.h"

namespace dnb {
namespace {

// Launchpad mk1 LEDs have 4 levels of red and 4 of green; mixes give amber,
// orange, yellow and lime. Colours are floats 0..3 and get rounded at the end.
const Color OFF{0.f, 0.f};
const Color RED{3.f, 0.f};
const Color GREEN{0.f, 3.f};
const Color AMBER{3.f, 3.f};
const Color ORANGE{3.f, 1.5f};
const Color LIME{1.5f, 3.f};
const Color YELLOW{2.f, 3.f};
const Color DIM_RED{1.f, 0.f};
const Color DIM_GREEN{0.f, 1.f};
const Color DIM_AMBER{1.f, 1.f};

Color scale(Color c, float k) { return {c.r * k, c.g * k}; }
Color lerp(Color a, Color b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k}; }
void add(Color& d, Color c, float k = 1.f) {
    d.r = std::min(3.f, d.r + c.r * k);
    d.g = std::min(3.f, d.g + c.g * k);
}
void maxb(Color& d, Color c) {
    d.r = std::max(d.r, c.r);
    d.g = std::max(d.g, c.g);
}
uint8_t quant(Color c) {
    int r = std::clamp(static_cast<int>(c.r + 0.5f), 0, 3);
    int g = std::clamp(static_cast<int>(c.g + 0.5f), 0, 3);
    return static_cast<uint8_t>(r | (g << 4));
}
Color ledToColor(uint8_t v) { return {static_cast<float>(v & 3), static_cast<float>((v >> 4) & 3)}; }
float frac(double v) { return static_cast<float>(v - std::floor(v)); }
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float angDiff(float a, float b) {
    float d = std::fmod(std::fabs(a - b), kTwoPi);
    return d > kPi ? kTwoPi - d : d;
}
int trackHitSlot(int t) {
    if (t == T_BASS) return kBassSlot;
    if (t == T_CHORD) return kChordSlot;
    return t;
}
const int kStutDiv[4] = {4, 8, 16, 32};

}  // namespace

// ------------------------------------------------------- shared helpers --

Color slotColor(const Style& s, int slot) {
    if (slot == T_BASS || slot == kBassSlot) return GREEN;
    if (slot == T_CHORD || slot == kChordSlot) return YELLOW;
    if (slot < 0 || slot > kExtraSlot) return AMBER;
    const DrumDef& d = s.drums[static_cast<size_t>(slot)];
    switch (d.type) {
        case DrumType::Kick: return RED;
        case DrumType::Snare: return d.gain < 0.5f ? ORANGE : AMBER;
        case DrumType::Clap: return AMBER;
        case DrumType::Metal: return d.p[4] > 30.f ? GREEN : (d.p[4] > 5.f ? LIME : YELLOW);
        case DrumType::Rim: return YELLOW;
        case DrumType::Tom: return ORANGE;
        case DrumType::Shaker: return LIME;
        case DrumType::Cowbell: return YELLOW;
    }
    return AMBER;
}

Color fxColor(int fx) {
    switch (fx) {
        case FX_STUT4: case FX_STUT8: case FX_STUT16: case FX_STUT32: case FX_REVERSE: case FX_TAPESTOP:
            return RED;
        case FX_HALFTIME: case FX_PUMP: case FX_GATE: return ORANGE;
        case FX_LPF: case FX_HPF: case FX_CRUSH: case FX_FLANGE: case FX_LOFI: return GREEN;
        case FX_DELAY: case FX_REVERB: return LIME;
        default: return AMBER;  // sound effects: siren, riser, horn, laser
    }
}

std::string trackName(const Style& s, int t) {
    if (t == T_BASS) return s.bass.name;
    if (t == T_CHORD) return s.chord.name;
    if (t == T_LEAD) return "Lead (recorded)";
    if (t >= 0 && t < kDrumTracks) return s.drums[static_cast<size_t>(t)].name;
    return "?";
}

// ------------------------------------------------------------ controller --

Controller::Controller(Shared& sh) : sh_(sh) {
    for (int s = 0; s < kHitSlots; ++s) {
        seenHits_[s] = sh_.hits[s].load();
        hitTime_[s] = -10.0;
    }
    for (int f = 0; f < FX_COUNT; ++f) fxRelease_[f] = -10.0;
    for (int& p : padFx_) p = -1;
    for (int& b : bassPadSemi_) b = -1000;
}

double Controller::beatSec() const { return 60.0 / std::max(40.f, sh_.bpm.load()); }
int Controller::currentStyle() const { return sh_.pattern.load() / kMaxSlots; }

void Controller::chooseStyle(int style) {
    if (style < 0 || style >= styleCount()) return;
    sh_.send(Cmd::QueuePattern, style * kMaxSlots);
    const Style& s = styles()[static_cast<size_t>(style)];
    logf("Style: %s (%s, %.0f BPM)%s", s.name.c_str(), s.keyName.c_str(), s.bpm,
         sh_.playing.load() ? " - starts on the next bar" : "");
}

void Controller::choosePattern(int slot) {
    const int style = currentStyle();
    if (slot < 0 || slot >= sh_.slotCount[style].load()) return;
    const int p = style * kMaxSlots + slot;
    sh_.send(Cmd::QueuePattern, p);
    logf("Pattern %d: %s%s", slot + 1, sh_.patternName[p].c_str(), sh_.playing.load() ? " (next bar)" : "");
}

// ------------------------------------------------------------------ input --

void Controller::onMidi(const std::vector<unsigned char>& m, double now) {
    if (m.size() < 3) return;
    const unsigned char st = m[0] & 0xF0;
    if (st == 0x90 || st == 0x80) {
        bool down = st == 0x90 && m[2] > 0;
        int x = m[1] & 15, y = m[1] >> 4;
        if (y > 7) return;
        if (x == 8) onScene(y, down);
        else if (x < 8) onGrid(x, y, down, now);
    } else if (st == 0xB0 && m[1] >= 104 && m[1] <= 111) {
        onTop(m[1] - 104, m[2] > 0, now);
    }
}

void Controller::onGrid(int x, int y, bool down, double now) {
    const int p = sh_.pattern.load();
    const Style& st = styleOf(p);

    // Holding View turns the top half into a picker: styles on rows 1-2,
    // this style's patterns (built-in and your own) on rows 3-4.
    if (viewHeld_ && y < 4) {
        if (down) {
            if (y < 2) chooseStyle(y * 8 + x);
            else choosePattern(pickerPage_ * 16 + (y - 2) * 8 + x);
            comboUsed_ = true;
        }
        return;
    }

    if (y == 4) {  // drum pads (+ chord pad on the right)
        if (x == kChordSlot) {
            if (down && !chordPadHeld_) {
                chordPadHeld_ = true;
                sh_.send(Cmd::ChordOn, lastBassIdx_);
                spawnRipple(7.f, 4.f, YELLOW, now, 7.f);
            } else if (!down && chordPadHeld_) {
                chordPadHeld_ = false;
                sh_.send(Cmd::ChordOff);
            }
            return;
        }
        if (down) {
            sh_.send(Cmd::Trigger, x);
            const DrumType t = st.drums[static_cast<size_t>(x)].type;
            if (t != DrumType::Kick && t != DrumType::Snare && t != DrumType::Clap)
                spawnRipple(static_cast<float>(x), 4.f, slotColor(st, x), now, 7.f);
        }
        return;
    }

    if (y == 5) {  // note pads: the style's scale, one octave
        if (down) {
            const int semi = degreeSemis(st, x);
            if (bassPadSemi_[x] != -1000) sh_.send(Cmd::BassOff, bassPadSemi_[x]);
            bassPadSemi_[x] = semi;
            lastBassIdx_ = x;
            sh_.send(Cmd::BassOn, semi);
            spawnRipple(static_cast<float>(x), 5.f, GREEN, now, 6.f);
            if (view_ == EDIT && heldStep_ >= 0 && (selTrack_ == T_BASS || selTrack_ == T_CHORD)) {
                sh_.note[p][selTrack_][heldStep_].store(static_cast<int8_t>(x));
                if (!stepSounds(sh_.on[p][selTrack_][heldStep_].load())) sh_.on[p][selTrack_][heldStep_].store(STEP_ON);
            }
        } else if (bassPadSemi_[x] != -1000) {
            sh_.send(Cmd::BassOff, bassPadSemi_[x]);
            bassPadSemi_[x] = -1000;
        }
        return;
    }

    if (y >= 6) {  // effect pads, mapped by the style
        const int pad = (y - 6) * 8 + x;
        if (down && padFx_[pad] < 0) {
            const int f = st.fx[static_cast<size_t>(pad)];
            if (f < 0 || f >= FX_COUNT || fxHeld_[f]) return;
            padFx_[pad] = f;
            fxHeld_[f] = true;
            fxPress_[f] = now;
            sh_.send(Cmd::FxOn, f);
            spawnRipple(static_cast<float>(x), static_cast<float>(y), fxColor(f), now, 11.f);
            logf("FX: %s", fxName(f));
        } else if (!down && padFx_[pad] >= 0) {
            const int f = padFx_[pad];
            padFx_[pad] = -1;
            fxHeld_[f] = false;
            fxRelease_[f] = now;
            if (f == FX_RISER) riserHeldFor_ = now - fxPress_[f];
            sh_.send(Cmd::FxOff, f);
        }
        return;
    }

    // rows 0..3
    if (view_ == EDIT && y < 2) {
        const int bars = sh_.patternLength(p) / kSteps;
        if (editBar_ >= bars) editBar_ = bars - 1;
        const int s = editBar_ * kSteps + y * 8 + x;
        if (down) {
            const bool on = stepSounds(sh_.on[p][selTrack_][s].load());
            sh_.on[p][selTrack_][s].store(on ? STEP_OFF : STEP_ON);
            if (selTrack_ >= T_BASS) {
                if (!on) sh_.note[p][selTrack_][s].store(static_cast<int8_t>(lastBassIdx_));
                heldStep_ = s;
            }
        } else if (heldStep_ == s) {
            heldStep_ = -1;
        }
        return;
    }
    if (down) spawnRipple(static_cast<float>(x), static_cast<float>(y), AMBER, now, 6.f);
}

void Controller::onScene(int row, bool down) {
    if (!down) return;
    const int p = sh_.pattern.load();
    const Style& st = styleOf(p);
    if (view_ == EDIT) {
        if (viewHeld_) {
            for (int s = 0; s < kMaxSteps; ++s) {
                sh_.on[p][row][s].store(STEP_OFF);
                for (int l = 0; l < kLocks; ++l) sh_.lock[p][row][s][l].store(0);
            }
            comboUsed_ = true;
            logf("Cleared %s", trackName(st, row).c_str());
        } else {
            selTrack_ = row;
            heldStep_ = -1;
            logf("Editing: %s", trackName(st, row).c_str());
        }
    } else {
        bool m = !sh_.mute[row].load();
        sh_.mute[row].store(m);
        logf("%s %s", trackName(st, row).c_str(), m ? "muted" : "unmuted");
    }
}

void Controller::nudgeBpm(float d) {
    float b = std::clamp(std::round(sh_.bpm.load() + d), 60.f, 200.f);
    sh_.bpm.store(b);
    logf("Tempo: %.0f BPM", b);
}

void Controller::onTop(int i, bool down, double now) {
    switch (i) {
        case 0:
            if (down) {
                bool playing = sh_.playing.load();
                sh_.send(playing ? Cmd::Stop : Cmd::Play);
                logf("%s", playing ? "Stop" : "Play");
            }
            break;
        case 1:
            if (down) {
                viewHeld_ = true;
                comboUsed_ = false;
                pickerPage_ = (sh_.pattern.load() % kMaxSlots) / 16;  // open on the current pattern's page
            } else {
                viewHeld_ = false;
                if (!comboUsed_) {
                    view_ = view_ == PERFORM ? EDIT : PERFORM;
                    heldStep_ = -1;
                    logf("View: %s", view_ == EDIT ? "EDIT (steps on top two rows)" : "PERFORM");
                }
            }
            break;
        case 2:
        case 3:
            if (viewHeld_) {  // while picking: page through the patterns (16 at a time)
                if (down) {
                    const int pages = (sh_.slotCount[currentStyle()].load() + 15) / 16;
                    pickerPage_ = std::clamp(pickerPage_ + (i == 2 ? -1 : 1), 0, std::max(0, pages - 1));
                    comboUsed_ = true;
                }
                break;
            }
            // EDIT view of a long pattern: these pick which bar is on the top rows.
            if (view_ == EDIT && sh_.patternLength(sh_.pattern.load()) > kSteps) {
                if (down) {
                    const int bars = sh_.patternLength(sh_.pattern.load()) / kSteps;
                    editBar_ = std::clamp(editBar_ + (i == 2 ? -1 : 1), 0, bars - 1);
                    ++editBarPresses_;
                    heldStep_ = -1;
                    logf("Editing bar %d of %d", editBar_ + 1, bars);
                }
                break;
            }
            if (down) {
                bpmHeld_ = i == 2 ? -1 : 1;
                nudgeBpm(static_cast<float>(bpmHeld_));
                bpmNextRepeat_ = now + 0.4;
            } else {
                bpmHeld_ = 0;
            }
            break;
        default:
            if (!down) break;
            {
                // The four buttons show the "page" of four patterns the current one is on.
                const int src = sh_.pattern.load();
                const int slot = ((src % kMaxSlots) / 4) * 4 + (i - 4);
                if (viewHeld_) {
                    if (slot >= sh_.slotCount[src / kMaxSlots].load()) break;
                    const int dst = (src / kMaxSlots) * kMaxSlots + slot;
                    if (dst != src) {
                        copyPattern(sh_, src, dst);
                        logf("Copied \"%s\" over \"%s\"", sh_.patternName[src].c_str(), sh_.patternName[dst].c_str());
                    }
                    comboUsed_ = true;
                } else {
                    choosePattern(slot);
                }
            }
            break;
    }
}

void Controller::releaseAll(double now) {
    for (int pad = 0; pad < 16; ++pad)
        if (padFx_[pad] >= 0) onGrid(pad % 8, 6 + pad / 8, false, now);
    for (int x = 0; x < 8; ++x)
        if (bassPadSemi_[x] != -1000) onGrid(x, 5, false, now);
    if (chordPadHeld_) onGrid(kChordSlot, 4, false, now);
    heldStep_ = -1;
    bpmHeld_ = 0;
    viewHeld_ = false;
}

void Controller::update(double now) {
    if (bpmHeld_ != 0 && now >= bpmNextRepeat_) {
        nudgeBpm(static_cast<float>(bpmHeld_));
        bpmNextRepeat_ = now + 0.06;
    }
}

// ------------------------------------------------------------ light show --

void Controller::spawnRipple(float x, float y, Color c, double now, float speed) {
    if (ripples_.size() > 24) ripples_.erase(ripples_.begin());
    ripples_.push_back({x, y, c, now, speed});
}

void Controller::drawFx(int fx, double t, double now, float dt, Color cv[8][4]) {
    const double beat = beatSec();
    const float level = sh_.level.load();

    switch (fx) {
        case FX_STUT4:
        case FX_STUT8:
        case FX_STUT16:
        case FX_STUT32: {
            const int div = kStutDiv[fx - FX_STUT4];
            const double L = beat * 4.0 / div;
            const int n = static_cast<int>(t / L);
            const float p = frac(t / L);
            const Color c = n % 3 == 0 ? AMBER : (n % 3 == 1 ? RED : GREEN);
            const float flash = (1.f - p) * (1.f - p) * (1.f - p);
            const int sweep = static_cast<int>(p * 8.f);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float k = 0.f;
                    if (fx == FX_STUT4) k = x == sweep ? 1.f : 0.6f * flash;          // flash + sweeping bar
                    else if (fx == FX_STUT8) k = ((x < 4) == (n % 2 == 0)) ? 0.4f + 0.6f * flash : 0.f;  // halves
                    else if (fx == FX_STUT16) k = ((x + y + n) & 1) ? 0.3f + 0.7f * flash : 0.f;  // checker
                    else k = hash32(static_cast<uint32_t>(n * 131 + x * 7 + y * 13)) % 3 == 0 ? 1.f : 0.f;  // glitch
                    cv[x][y] = scale(c, k);
                }
            break;
        }
        case FX_TAPESTOP: {
            const float rate = sh_.tapeRate.load();
            tapeScroll_ += dt * (0.3f + rate * 8.f);
            const Color c{3.f * (0.25f + 0.75f * rate), 3.f * rate * rate};
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float v = 0.5f + 0.5f * std::sin((y - tapeScroll_) * 1.8f + x * 0.4f);
                    cv[x][y] = scale(c, v);
                }
            break;
        }
        case FX_HALFTIME: {
            const float p = frac(t / (2.0 * beat));
            const float breath = 0.5f - 0.5f * std::cos(kTwoPi * p);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float d = std::hypot(x - 3.5f, y - 1.5f);
                    float k = std::clamp(1.f + 3.f * breath - d, 0.f, 1.f) * (0.3f + 0.7f * breath);
                    cv[x][y] = scale(ORANGE, k);
                }
            break;
        }
        case FX_REVERSE: {
            const float head = 7.5f - frac(t / beat) * 9.f;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float dx = x - head;
                    float k = dx >= 0.f ? std::max(0.f, 1.f - dx / 3.f) : std::max(0.f, 1.f + dx * 2.f);
                    cv[x][y] = scale((y & 1) ? LIME : GREEN, k);
                }
            break;
        }
        case FX_DELAY: {
            const double E = beat * 0.75;
            const int n = static_cast<int>(t / E);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) cv[x][y] = scale(cv[x][y], 0.5f);
            for (int j = 0; j < 4; ++j) {
                if (n - j < 0) break;
                float a = static_cast<float>(frac(t / E) * E + j * E);
                float cx = ((n - j) & 1) ? 5.5f : 1.5f;  // ping-pong
                float radius = a * 6.f;
                float amp = std::pow(0.6f, static_cast<float>(j));
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 4; ++y) {
                        float d = std::hypot(x - cx, y - 1.5f);
                        add(cv[x][y], (n - j) & 1 ? LIME : GREEN, std::max(0.f, 1.f - std::fabs(d - radius)) * amp);
                    }
            }
            break;
        }
        case FX_LPF: {
            const float edge = sh_.lpAmt.load() * 4.f;  // ceiling closing in from the top
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float yc = y + 0.5f;
                    if (yc < edge - 0.5f) cv[x][y] = OFF;
                    else if (yc < edge + 0.5f) cv[x][y] = RED;
                    else cv[x][y] = scale(GREEN, 0.4f + 0.6f * std::min(1.f, level * 1.5f));
                }
            break;
        }
        case FX_HPF: {
            const float edge = 4.f - sh_.hpAmt.load() * 4.f;  // floor rising from the bottom
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float yc = y + 0.5f;
                    if (yc > edge + 0.5f) cv[x][y] = OFF;
                    else if (yc > edge - 0.5f) cv[x][y] = AMBER;
                    else cv[x][y] = scale(LIME, 0.4f + 0.6f * std::min(1.f, level * 1.5f));
                }
            break;
        }
        case FX_CRUSH: {
            const int seed = static_cast<int>(t / (beat / 4.0));
            if (seed != crushSeed_) {
                crushSeed_ = seed;
                static const Color pal[5] = {OFF, RED, GREEN, AMBER, ORANGE};
                for (int bx = 0; bx < 4; ++bx)
                    for (int by = 0; by < 2; ++by) {
                        Color c = pal[hash32(static_cast<uint32_t>(seed * 977 + bx * 31 + by * 7)) % 5];
                        for (int i = 0; i < 2; ++i)
                            for (int j = 0; j < 2; ++j) crushCells_[bx * 2 + i][by * 2 + j] = c;
                    }
            }
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) cv[x][y] = crushCells_[x][y];
            break;
        }
        case FX_FLANGE: {
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float v = std::sin(x * 0.9f + y * 0.9f - static_cast<float>(t) * 5.f);
                    cv[x][y] = {1.5f + 1.5f * v, 1.5f - 1.5f * v};
                }
            break;
        }
        case FX_SIREN: {
            const float ang = static_cast<float>(t) * kTwoPi * 1.6f;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float a = std::atan2(y - 1.5f, x - 3.5f);
                    float k1 = std::max(0.f, 1.f - angDiff(a, ang) / 0.9f);
                    float k2 = std::max(0.f, 1.f - angDiff(a, ang + kPi) / 0.9f);
                    cv[x][y] = {3.f * k1, 3.f * k2};
                }
            break;
        }
        case FX_RISER: {
            const float amt = sh_.riserAmt.load();
            const float fill = amt * 4.2f;
            const float strobe = 0.5f + 0.5f * std::sin(static_cast<float>(t) * (6.f + amt * 40.f));
            const Color c{3.f * std::min(1.f, amt * 1.5f), 3.f * (1.f - amt * 0.6f)};
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float f = std::clamp(fill - (3 - y), 0.f, 1.f);
                    float k = f < 1.f ? f * strobe : 1.f;
                    if (f <= 0.f && hash32(static_cast<uint32_t>(now * 30.0) * 64 + x * 4 + y) % 100 < amt * 25.f)
                        k = 0.4f;  // sparks above the fill line
                    cv[x][y] = scale(c, k);
                }
            break;
        }
        case FX_PUMP: {  // whole canvas ducks on every beat and swells back
            const float bp = sh_.beatPhase.load();
            const float k = std::min(1.f, bp / 0.45f);
            const float g = 0.1f + 0.9f * k * k;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float d = std::hypot(x - 3.5f, (y - 1.5f) * 1.6f) / 4.f;
                    cv[x][y] = scale(ORANGE, std::clamp(g * 1.3f - d * (1.f - g), 0.f, 1.f));
                }
            break;
        }
        case FX_GATE: {  // columns chop on and off with the 16th-note gate
            const float bp = sh_.beatPhase.load();
            const int sixteenth = static_cast<int>(bp * 4.f);
            const bool open = frac(bp * 4.f) < 0.5f;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    bool lit = open && ((x / 2) == sixteenth);
                    cv[x][y] = lit ? ORANGE : scale(cv[x][y], 0.15f);
                }
            break;
        }
        case FX_LOFI: {  // warm, dim, flickery, with dust specks
            const float flick = 0.75f + 0.25f * std::sin(static_cast<float>(now) * 23.f) * std::sin(static_cast<float>(now) * 7.f);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    Color c = scale(Color{2.f, 1.2f}, flick * (0.5f + 0.5f * std::sin(x * 0.7f + y + static_cast<float>(t) * 0.8f)));
                    if (hash32(static_cast<uint32_t>(now * 12.0) * 64 + x * 4 + y) % 23 == 0) c = DIM_AMBER;
                    cv[x][y] = c;
                }
            break;
        }
        case FX_LASER: {  // beams fire across a row every 8th note
            const double E = beat * 0.5;
            const int n = static_cast<int>(t / E);
            const float head = frac(t / E) * 11.f - 1.f;
            const int row = static_cast<int>(hash32(static_cast<uint32_t>(n) * 7919u) % 4);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    cv[x][y] = scale(cv[x][y], 0.1f);
                    if (y == row) {
                        float dx = head - x;
                        if (dx >= 0.f && dx < 3.f) cv[x][y] = scale(n & 1 ? RED : GREEN, 1.f - dx / 3.f);
                    }
                }
            break;
        }
        case FX_HORN: {
            const Color c = (static_cast<int>(t * 6.0) & 1) ? AMBER : RED;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) {
                    float d = std::max(std::fabs(x - 3.5f), std::fabs(y - 1.5f));
                    float wv = frac(d * 0.45f - t * 2.5);
                    cv[x][y] = scale(c, wv > 0.55f ? 1.f : 0.1f);
                }
            break;
        }
        default:
            break;
    }
}

void Controller::buildCanvas(double now, float dt, Color cv[8][4]) {
    const bool playing = sh_.playing.load();

    int active = -1;
    double latest = -1.0;
    for (int f = 0; f < FX_COUNT; ++f)
        if (fxHeld_[f] && f != FX_REVERB && fxPress_[f] > latest) {
            latest = fxPress_[f];
            active = f;
        }

    // 1) base layer: level meter per track while playing, a calm wave while stopped
    Color idleColor = ledToColor(styleOf(sh_.pattern.load()).led);
    if (idleColor.r + idleColor.g < 1.f) idleColor = GREEN;
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 4; ++y) {
            Color c = OFF;
            if (playing) {
                static const Color lv[4] = {GREEN, LIME, AMBER, RED};
                const int l = 3 - y;
                float f = std::clamp(env_[x] * 4.2f - l, 0.f, 1.f);
                c = scale(lv[l], f);
            } else {
                // Idle: a slow, dim wave in the style's colour. Kept slow and low so the
                // Launchpad's 4 brightness steps change gently instead of strobing.
                const float t = static_cast<float>(now);
                const float v = 0.5f + 0.5f * std::sin((x + y * 1.3f) * 0.55f - t * 0.9f);
                c = scale(idleColor, 0.12f + 0.42f * v);
            }
            cv[x][y] = c;
        }
    if (!playing) {
        // Now and then one pad twinkles and fades out over a second.
        const double period = 0.9;
        const uint32_t n = static_cast<uint32_t>(now / period);
        const float age = static_cast<float>(now - n * period);
        const uint32_t h = hash32(n * 2654435761u);
        const int tx = static_cast<int>(h % 8), ty = static_cast<int>((h >> 8) % 4);
        maxb(cv[tx][ty], scale(AMBER, std::max(0.f, 1.f - age / 1.0f)));
    }

    // 2) the most recently pressed effect takes over the canvas
    if (active >= 0) drawFx(active, now - fxPress_[active], now, dt, cv);

    // riser release = "the drop": full-canvas flash
    if (!fxHeld_[FX_RISER] && riserHeldFor_ > 0.8 && now - fxRelease_[FX_RISER] < 0.45) {
        float k = 1.f - static_cast<float>((now - fxRelease_[FX_RISER]) / 0.45);
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 4; ++y) maxb(cv[x][y], scale(AMBER, k));
    }

    // 3) reverb glow: sparkles that bloom and diffuse, tail fades after release
    {
        Color next[8][4];
        const float decay = std::exp(-dt * 1.3f);
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 4; ++y) {
                Color sum{0, 0};
                int n = 0;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        int xx = x + dx, yy = y + dy;
                        if ((dx || dy) && xx >= 0 && xx < 8 && yy >= 0 && yy < 4) {
                            sum.r += glow_[xx][yy].r;
                            sum.g += glow_[xx][yy].g;
                            ++n;
                        }
                    }
                next[x][y] = {(glow_[x][y].r * 0.6f + sum.r / n * 0.4f) * decay,
                              (glow_[x][y].g * 0.6f + sum.g / n * 0.4f) * decay};
            }
        if (fxHeld_[FX_REVERB]) {
            for (int i = 0; i < 2; ++i) {
                int x = static_cast<int>((rng_.next() * 0.5f + 0.5f) * 7.99f);
                int y = static_cast<int>((rng_.next() * 0.5f + 0.5f) * 3.99f);
                add(next[x][y], rng_.next() > 0.f ? AMBER : GREEN);
            }
        }
        bool any = fxHeld_[FX_REVERB];
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 4; ++y) {
                glow_[x][y] = next[x][y];
                if (glow_[x][y].r > 0.05f || glow_[x][y].g > 0.05f) any = true;
            }
        if (any) {
            if (fxHeld_[FX_REVERB] && active < 0)
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 4; ++y) cv[x][y] = scale(cv[x][y], 0.35f);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 4; ++y) add(cv[x][y], glow_[x][y]);
        }
    }

    // 4) ripples from pads and hits
    const float life = 0.7f;
    ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(),
                                  [&](const Ripple& r) { return now - r.t0 > life; }),
                   ripples_.end());
    for (const Ripple& r : ripples_) {
        float a = static_cast<float>(now - r.t0);
        float radius = a * r.speed;
        float fade = 1.f - a / life;
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 4; ++y) {
                float d = std::hypot(x - r.x, y - r.y);
                float k = std::max(0.f, 1.f - std::fabs(d - radius) * 1.1f) * fade;
                if (k > 0.f) add(cv[x][y], r.c, k);
            }
    }
}

void Controller::render(double now, Frame& out) {
    const float dt = lastFrame_ > 0.0 ? static_cast<float>(std::min(0.1, now - lastFrame_)) : 0.033f;
    lastFrame_ = now;

    const int p = sh_.pattern.load();
    const int styleIdx = p / kMaxSlots;
    const Style& st = styleOf(p);
    if (styleIdx != lastStyle_) {
        if (lastStyle_ >= 0) styleChangedAt_ = now;
        lastStyle_ = styleIdx;
    }

    // Pick up hits from the audio thread; the ripple depends on the kind of sound.
    for (int s = 0; s < kHitSlots; ++s) {
        uint32_t h = sh_.hits[s].load(std::memory_order_relaxed);
        if (h == seenHits_[s]) continue;
        seenHits_[s] = h;
        hitTime_[s] = now;
        if (s == kBassSlot) { env_[T_BASS] = 1.f; continue; }
        if (s == kLeadSlot) {  // lead synth: a ripple dropping from the top, placed by pitch
            const int n = sh_.leadNote.load();
            const float x = ((n % 12) * 7.f) / 11.f;
            const Color lc = (n / 12) % 2 ? LIME : GREEN;
            spawnRipple(x, -0.6f, lc, now, 7.f);
            continue;
        }
        if (s == kChordSlot) {
            env_[T_CHORD] = 1.f;
            spawnRipple(-1.f, 1.5f, YELLOW, now, 8.f);  // closes in from the sides
            spawnRipple(8.f, 1.5f, YELLOW, now, 8.f);
            continue;
        }
        if (s < kDrumTracks) env_[s] = 1.f;
        const DrumDef& d = st.drums[static_cast<size_t>(s)];
        switch (d.type) {
            case DrumType::Kick: spawnRipple(3.5f, 4.2f, RED, now, 10.f); break;        // rises from the bottom
            case DrumType::Snare:
            case DrumType::Clap: spawnRipple(3.5f, -1.2f, AMBER, now, 10.f); break;     // falls from the top
            case DrumType::Metal:
                if (d.p[4] < 2.5f) spawnRipple(3.5f, 1.5f, AMBER, now, 7.f);             // crash bursts from the middle
                break;
            case DrumType::Tom: spawnRipple(s < 4 ? 0.f : 7.f, 3.f, ORANGE, now, 9.f); break;  // toms/congas from a corner
            case DrumType::Cowbell: spawnRipple(7.f, 0.f, YELLOW, now, 9.f); break;
            default: break;
        }
    }
    if (sh_.bassPlaying.load() > -1000) env_[T_BASS] = std::max(env_[T_BASS], 0.65f);
    for (float& e : env_) e *= std::exp(-dt * 7.f);

    Color grid[8][8];
    Color cv[8][4];
    buildCanvas(now, dt, cv);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 4; ++y) grid[y][x] = cv[x][y];

    const bool playing = sh_.playing.load();
    const int curStep = sh_.step.load();

    // EDIT view: 16 steps of the selected track on the top two rows.
    const int bars = sh_.patternLength(p) / kSteps;
    if (editBar_ >= bars) editBar_ = bars - 1;
    if (view_ == EDIT) {
        const Color tc = slotColor(st, selTrack_);
        for (int s = editBar_ * kSteps; s < (editBar_ + 1) * kSteps; ++s) {
            const int x = s % 8, y = (s % kSteps) / 8;
            const uint8_t v = sh_.on[p][selTrack_][s].load();
            const bool on = stepSounds(v);
            Color c = (s % 4 == 0) ? DIM_RED : OFF;
            if (stepKind(v) == STEP_TIE) c = scale(tc, 0.34f);
            if (on) {
                if (selTrack_ >= T_BASS) c = Color{sh_.note[p][selTrack_][s].load() * 3.f / 9.f, 3.f};  // redder = higher
                else c = stepKind(v) == STEP_SOFT ? scale(tc, 0.5f) : tc;
            }
            if (playing && s == curStep) c = on ? AMBER : RED;
            if (s == heldStep_) c = frac(now * 6.0) < 0.5f ? AMBER : OFF;
            grid[y][x] = c;
        }
    }

    // Style picker (while View is held): one pad per style, current one blinks.
    if (viewHeld_) {
        for (int i = 0; i < 16; ++i) {
            const int x = i % 8, y = i / 8;
            Color c = OFF;
            if (i < styleCount()) {
                c = scale(ledToColor(styles()[static_cast<size_t>(i)].led), 0.5f);
                if (i == styleIdx) c = frac(now * 3.0) < 0.5f ? ledToColor(styles()[static_cast<size_t>(i)].led) : DIM_AMBER;
            }
            grid[y][x] = c;
        }
        const int count = sh_.slotCount[styleIdx].load(), q = sh_.queued.load();
        for (int kk = 0; kk < 16; ++kk) {
            const int x = kk % 8, y = 2 + kk / 8, k = pickerPage_ * 16 + kk;
            Color c = OFF;
            if (k < count) {
                c = k < kFactorySlots ? DIM_GREEN : DIM_AMBER;  // built-in / your own
                const int pk = styleIdx * kMaxSlots + k;
                if (pk == p) c = GREEN;
                if (pk == q) c = frac(now * 4.0) < 0.5f ? AMBER : OFF;
            }
            grid[y][x] = c;
        }
    }
    // Style just changed: a quick wipe across the top half in the new style's colour.
    if (now - styleChangedAt_ < 0.5 && !viewHeld_) {
        const float head = static_cast<float>((now - styleChangedAt_) / 0.5) * 10.f - 1.f;
        const Color sc = ledToColor(st.led);
        for (int x = 0; x < 8; ++x) {
            float k = std::max(0.f, 1.f - std::fabs(x - head) / 1.5f);
            for (int y = 0; y < 4; ++y) maxb(grid[y][x], scale(sc, k));
        }
    }

    // Row 4: drum pads (+ chord) flash on every hit, live or sequenced.
    for (int x = 0; x < 8; ++x) {
        const Color sc = slotColor(st, x);
        float k = std::clamp(1.f - static_cast<float>((now - hitTime_[x]) / 0.18), 0.f, 1.f);
        if (x == kChordSlot && chordPadHeld_) k = 1.f;
        grid[4][x] = lerp(scale(sc, 0.34f), sc, k);
    }

    // Row 5: note pads in the style's scale.
    const int bassSemi = sh_.bassPlaying.load();
    for (int x = 0; x < 8; ++x) {
        Color c = (x == 0 || x == 7) ? DIM_AMBER : DIM_GREEN;
        if (view_ == EDIT && x == lastBassIdx_) c = {1.f, 2.f};
        if (bassSemi == degreeSemis(st, x)) c = GREEN;
        if (bassPadSemi_[x] != -1000) c = AMBER;
        grid[5][x] = c;
    }

    // Rows 6-7: effect pads (style's layout), animated while held.
    const double beat = beatSec();
    for (int pad = 0; pad < 16; ++pad) {
        const int x = pad % 8, y = 6 + pad / 8;
        const int f = padFx_[pad] >= 0 ? padFx_[pad] : st.fx[static_cast<size_t>(pad)];
        const Color base = fxColor(f);
        Color c = scale(base, 0.34f);
        if (f >= 0 && f < FX_COUNT && fxHeld_[f]) {
            const double t = now - fxPress_[f];
            if (f <= FX_STUT32) {
                const double L = beat * 4.0 / kStutDiv[f];
                c = frac(t / L) < 0.5f ? AMBER : DIM_RED;  // blinks at the repeat rate
            } else if (f == FX_TAPESTOP) {
                c = {3.f, 3.f * sh_.tapeRate.load()};
            } else if (f == FX_RISER) {
                c = scale(AMBER, 0.34f + 0.66f * sh_.riserAmt.load());
            } else if (f == FX_GATE || f == FX_PUMP) {
                c = scale(base, 0.3f + 0.7f * (1.f - sh_.beatPhase.load()));
            } else {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(t) * kTwoPi * 4.f);
                c = scale(base, 0.5f + 0.5f * pulse);
            }
        }
        grid[y][x] = c;
    }

    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) out[y * 8 + x] = quant(grid[y][x]);

    // Scene buttons (right column).
    for (int t = 0; t < kPadTracks; ++t) {
        Color c;
        const bool muted = sh_.mute[t].load();
        if (view_ == EDIT) {
            c = t == selTrack_ ? AMBER : (muted ? DIM_RED : DIM_GREEN);
        } else if (muted) {
            c = RED;
        } else {
            float k = std::clamp(1.f - static_cast<float>((now - hitTime_[trackHitSlot(t)]) / 0.12), 0.f, 1.f);
            c = lerp(DIM_GREEN, GREEN, k);
        }
        out[64 + t] = quant(c);
    }

    // Top buttons.
    const int queued = sh_.queued.load();
    out[72] = quant(playing ? (curStep % 4 == 0 ? GREEN : DIM_GREEN) : Color{2.f, 0.f});
    out[73] = quant(viewHeld_ ? ledToColor(st.led) : (view_ == EDIT ? AMBER : DIM_GREEN));
    if (viewHeld_) {  // BPM-/+ page the picker: lit when there's a page that way
        const int pages = (sh_.slotCount[styleIdx].load() + 15) / 16;
        out[74] = quant(pickerPage_ > 0 ? GREEN : OFF);
        out[75] = quant(pickerPage_ < pages - 1 ? GREEN : OFF);
    } else if (view_ == EDIT && bars > 1) {  // bar paging: lit when there's a bar that way
        out[74] = quant(editBar_ > 0 ? GREEN : DIM_RED);
        out[75] = quant(editBar_ < bars - 1 ? GREEN : DIM_RED);
    } else {
        out[74] = quant(bpmHeld_ < 0 ? AMBER : DIM_AMBER);
        out[75] = quant(bpmHeld_ > 0 ? AMBER : DIM_AMBER);
    }
    const int page = ((p % kMaxSlots) / 4) * 4;
    const int count = sh_.slotCount[styleIdx].load();
    for (int i = 0; i < 4; ++i) {
        const int slot = page + i;
        Color c = slot < count ? (slot < kFactorySlots ? DIM_RED : DIM_AMBER) : OFF;
        if (styleIdx * kMaxSlots + slot == p) c = GREEN;
        if (styleIdx * kMaxSlots + slot == queued) c = frac(now * 4.0) < 0.5f ? AMBER : OFF;
        if (queued >= 0 && queued / kMaxSlots != styleIdx && i == 0)
            c = frac(now * 4.0) < 0.5f ? ledToColor(styleOf(queued).led) : OFF;  // style change pending
        out[76 + i] = quant(c);
    }
}

}  // namespace dnb
