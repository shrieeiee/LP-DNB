// lp-tenori: Tenori-on style sequencer on the Launchpad Mini. See tenori.h.
#include "tenori.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "controller.h"  // Color, fxColor
#include "storage.h"
#include "styles.h"

namespace dnb {

namespace fs = std::filesystem;

namespace {

constexpr double kLookahead = 0.06;  // how far ahead notes are queued (seconds)

// Layer colours (LED scale 0..3: red, green). The Launchpad Mini only has a
// handful of distinct hues, so layer 8 shares layer 2's red.
const Color kLayerCol[kLayers] = {{0.f, 3.f}, {3.f, 0.f}, {3.f, 3.f}, {3.f, 1.f},
                                  {1.f, 3.f}, {3.f, 2.f}, {2.f, 3.f}, {3.f, 0.f}};
const Color kOff{0.f, 0.f};

Color sc(Color c, float k) { return {c.r * k, c.g * k}; }
void maxb(Color& d, Color c) {
    d.r = std::max(d.r, c.r);
    d.g = std::max(d.g, c.g);
}
uint8_t quantize(Color c) {
    const int r = std::clamp(static_cast<int>(c.r + 0.5f), 0, 3);
    const int g = std::clamp(static_cast<int>(c.g + 0.5f), 0, 3);
    return static_cast<uint8_t>(r | (g << 4));
}
Color ledColor(uint8_t v) { return {static_cast<float>(v & 3), static_cast<float>((v >> 4) & 3)}; }

Color soundCol(int s) {
    switch (s) {
        case SND_DRUMS: return {3.f, 0.f};
        case SND_BASS: return {3.f, 3.f};
        case SND_CHORDS: return {0.f, 3.f};
        default: return {1.5f, 3.f};
    }
}
Color modeCol(int m) {
    switch (m) {
        case MODE_SCORE: return {0.f, 3.f};
        case MODE_BOUNCE: return {3.f, 3.f};
        case MODE_PATH: return {3.f, 1.f};
        default: return {3.f, 0.f};
    }
}

// Push: steps between repeats for each column (whole bar ... 1/64 note).
const double kPushRate[8] = {16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25};

// How long notes ring, in steps, per sound.
float gateSteps(int sound) {
    switch (sound) {
        case SND_BASS: return 0.85f;
        case SND_CHORDS: return 1.75f;
        case SND_LEAD: return 0.9f;
        default: return 1.f;
    }
}

float velFor(int vol) { return 0.2f + 0.8f * static_cast<float>(std::clamp(vol, 0, 7)) / 7.f; }

int chebyshev(std::pair<int, int> a, std::pair<int, int> b) {
    return std::max(std::abs(a.first - b.first), std::abs(a.second - b.second));
}

// Lead sounds on Shift rows 5-6: every patch except "Style bass" (that's what a
// Bass layer is for), so the style's chord sound still fits on 16 pads.
const std::vector<int>& patchPads() {
    static const std::vector<int> pads = [] {
        std::vector<int> v;
        const auto& lp = leadPatches();
        for (int i = 0; i < static_cast<int>(lp.size()) && v.size() < 16; ++i)
            if (lp[static_cast<size_t>(i)].kind != LeadPatch::STYLE_BASS) v.push_back(i);
        return v;
    }();
    return pads;
}

int findPatch(const std::string& name) {
    const auto& lp = leadPatches();
    for (size_t i = 0; i < lp.size(); ++i)
        if (lp[i].name == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

const char* soundName(int s) {
    static const char* n[SND_COUNT] = {"Drums", "Bass", "Chords", "Lead"};
    return s >= 0 && s < SND_COUNT ? n[s] : "?";
}
const char* modeName(int m) {
    static const char* n[MODE_COUNT] = {"Score", "Bounce", "Path", "Push"};
    return m >= 0 && m < MODE_COUNT ? n[m] : "?";
}

// ------------------------------------------------------------------ Layer --

Layer::Layer() {
    for (auto& b : ball) b = -1;
    resetMotion();
}

bool Layer::empty() const {
    for (uint16_t r : score) if (r) return false;
    for (int8_t b : ball) if (b >= 0) return false;
    return path.empty();
}

void Layer::clear() {
    for (auto& r : score) r = 0;
    for (auto& b : ball) b = -1;
    path.clear();
    resetMotion();
}

void Layer::resetMotion() {
    for (int x = 0; x < 8; ++x) {
        ballY[x] = ball[x];
        ballDir[x] = 1;
    }
    pathFrom = pathAt = pathWait = 0;
    legFrom = legTo = 0.0;
}

// ----------------------------------------------------------------- Tenori --

Tenori::Tenori(Shared& sh) : sh_(sh) {
    for (int& f : padFx_) f = -1;
    stepT_.fill(-1e9);
    const int sounds[kLayers] = {SND_LEAD, SND_DRUMS, SND_BASS, SND_CHORDS, SND_LEAD, SND_DRUMS, SND_LEAD, SND_LEAD};
    const int modes[kLayers] = {MODE_SCORE, MODE_SCORE, MODE_SCORE, MODE_SCORE, MODE_BOUNCE, MODE_BOUNCE, MODE_PATH, MODE_PUSH};
    for (int i = 0; i < kLayers; ++i) {
        layers_[static_cast<size_t>(i)].sound = sounds[i];
        layers_[static_cast<size_t>(i)].mode = modes[i];
    }
    layers_[7].octave = 1;
    const int pluck = findPatch("Pluck");
    leadPatch_ = pluck >= 0 ? pluck : 0;
}

Tenori::~Tenori() = default;

int Tenori::style() const {
    const int s = sh_.pattern.load() / kMaxSlots;
    return s >= 0 && s < styleCount() ? s : 0;
}

void Tenori::setStyle(int s) {
    if (s < 0 || s >= styleCount()) return;
    // The groovebox sequencer isn't running, so this switches straight away
    // (and sets the style's tempo).
    sh_.send(Cmd::QueuePattern, s * kMaxSlots);
}

void Tenori::setLeadPatch(int p) {
    if (p < 0 || p >= static_cast<int>(leadPatches().size())) return;
    leadPatch_ = p;
    sh_.send(Cmd::LeadPatch, p);
}

void Tenori::setPlaying(bool on, double now) {
    if (on == playingFlag_) return;
    if (on) {
        g_ = 0;
        firedUpTo_ = 0;
        nextT_ = now + 0.01;
        stepT_.fill(-1e9);
        for (auto& L : layers_) L.resetMotion();
    }
    playingFlag_ = on;
}

void Tenori::onClock(bool start) { setPlaying(start, monotonicSec()); }

// ------------------------------------------------------------- sequencing --

double Tenori::baseStep() const { return 60.0 / std::max(40.f, sh_.bpm.load()) / 4.0; }

double Tenori::stepDur(uint64_t g) const {
    const double b = baseStep();
    if (!swing_) return b;
    const double s = styles()[static_cast<size_t>(style())].swing;
    return b * 2.0 * ((g & 1) ? (1.0 - s) : s);
}

int64_t Tenori::stepAt(double now) const {
    if (!playingFlag_ || firedUpTo_ == 0) return -1;
    for (uint64_t k = 0; k < 63 && k < firedUpTo_; ++k) {
        const uint64_t g = firedUpTo_ - 1 - k;
        if (stepT_[g % 64] <= now) return static_cast<int64_t>(g);
    }
    return -1;
}

void Tenori::schedule(double until) {
    if (!playingFlag_) return;
    // After a long stall (the computer slept, a dialog blocked) skip ahead
    // rather than firing a burst of late notes.
    if (until - nextT_ > 0.5) nextT_ = until - kLookahead;
    while (nextT_ < until) {
        const double d = stepDur(g_);
        stepT_[g_ % 64] = nextT_;
        fireStep(g_, nextT_, d);
        firedUpTo_ = g_ + 1;
        nextT_ += d;
        ++g_;
    }
}

Tenori::Event Tenori::voice(const Layer& L, int idx, float vel, float gate) const {
    const Style& st = styles()[static_cast<size_t>(style())];
    const int deg = idx + 7 * L.octave;
    switch (L.sound) {
        case SND_DRUMS:
            if (idx < kDrumSlots) return {idx, std::clamp(12 * L.octave, -24, 24), vel, gate};
            return {kChordSlot, 7 * L.octave, vel, std::max(gate, static_cast<float>(baseStep() * 2.0))};
        case SND_BASS: return {kBassSlot, degreeSemis(st, deg), vel, gate};
        case SND_CHORDS: return {kChordSlot, deg, vel, gate};
        default: return {kLeadSlot, std::clamp(st.root + 36 + degreeSemis(st, deg), 0, 127), vel, gate};
    }
}

void Tenori::send(const Event& e, double t) {
    Cmd c;
    c.type = Cmd::Note;
    c.a = e.slot;
    c.b = e.pitch;
    c.f = e.vel;
    c.g = e.gate;
    c.t = t;
    sh_.cmds.push(c);
}

void Tenori::noteLights(int layer, int x, int y, double t) {
    flash_[layer][x & 15][y & 7] = t;
    layerHit_[layer] = t;
    if (ripples_.size() >= 48) ripples_.erase(ripples_.begin());
    ripples_.push_back({layer, static_cast<float>(x % 8), static_cast<float>(y), t});
}

void Tenori::play(int li, int x, int y, int idx, double t, float vel, float gate) {
    const Layer& L = layers_[static_cast<size_t>(li)];
    if (L.muted) return;
    send(voice(L, idx, vel, gate), t);
    noteLights(li, x, y, t);
}

void Tenori::fireStep(uint64_t g, double t, double dur) {
    const double base = baseStep();
    for (int li = 0; li < kLayers; ++li) {
        Layer& L = layers_[static_cast<size_t>(li)];
        const float vel = velFor(L.volume);
        const float gate = static_cast<float>(gateSteps(L.sound) * base);
        switch (L.mode) {
            case MODE_SCORE: {
                const int s = static_cast<int>(g % static_cast<uint64_t>(std::clamp(L.length, 1, kTSteps)));
                for (int y = 0; y < 8; ++y)
                    if (L.score[y] & (1u << s)) play(li, s, y, 7 - y, t, vel, gate);
                break;
            }
            case MODE_BOUNCE:
                // Each ball falls from the row you tapped to the bottom and back up,
                // sounding as it lands: higher balls repeat less often.
                for (int x = 0; x < 8; ++x) {
                    const int top = L.ball[x];
                    if (top < 0) continue;
                    if (top >= 7) {  // resting on the floor: every step
                        L.ballY[x] = 7;
                        play(li, x, 7, x, t, vel, gate);
                        continue;
                    }
                    L.ballY[x] += L.ballDir[x];
                    if (L.ballY[x] >= 7) {
                        L.ballY[x] = 7;
                        L.ballDir[x] = -1;
                        play(li, x, 7, x, t, vel, gate);
                    } else if (L.ballY[x] <= top) {
                        L.ballY[x] = top;
                        L.ballDir[x] = 1;
                    }
                }
                break;
            case MODE_PATH: {
                // A cursor travels from point to point in the order you placed them,
                // taking as many steps as the distance between them.
                const int n = static_cast<int>(L.path.size());
                if (n == 0) break;
                if (L.pathAt >= n) { L.pathAt = 0; L.pathWait = 0; }
                if (L.pathWait <= 0) {
                    const auto p = L.path[static_cast<size_t>(L.pathAt)];
                    const int next = (L.pathAt + 1) % n;
                    const int leg = n == 1 ? 4 : std::max(1, chebyshev(p, L.path[static_cast<size_t>(next)]));
                    play(li, p.first, p.second, 7 - p.second, t, vel,
                         static_cast<float>(std::min(2.0, leg * base * 0.9)));
                    L.pathFrom = L.pathAt;
                    L.pathAt = next;
                    L.pathWait = leg;
                    L.legFrom = t;
                    L.legTo = t + leg * base;
                }
                --L.pathWait;
                break;
            }
            case MODE_PUSH:
                // Held pads repeat: the row is the note, the column how fast.
                for (const Held& h : held_) {
                    if (h.layer != li) continue;
                    const double r = kPushRate[h.x & 7];
                    if (r >= 1.0) {
                        if (g % static_cast<uint64_t>(r) == 0 && t >= h.skipUntil)
                            play(li, h.x, h.y, 7 - h.y, t, vel, static_cast<float>(r * base * 0.8));
                    } else {
                        const int k = static_cast<int>(std::lround(1.0 / r));
                        for (int j = 0; j < k; ++j) {
                            const double tt = t + j * dur / k;
                            if (tt >= h.skipUntil) play(li, h.x, h.y, 7 - h.y, tt, vel, static_cast<float>(dur / k * 0.8));
                        }
                    }
                }
                break;
            default: break;
        }
    }
}

void Tenori::preview(int li, int x, int y, int idx, double now) {
    const Layer& L = layers_[static_cast<size_t>(li)];
    play(li, x, y, idx, now, velFor(L.volume), static_cast<float>(gateSteps(L.sound) * baseStep() * 2.0));
}

void Tenori::clearLayer(int i) {
    undo_ = layers_;
    haveUndo_ = true;
    layers_[static_cast<size_t>(i)].clear();
    held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return h.layer == i; }), held_.end());
}

void Tenori::undo() {
    if (!haveUndo_) return;
    std::swap(layers_, undo_);  // pressing it again redoes
    for (auto& L : layers_) L.resetMotion();
}

// ------------------------------------------------------------------ input --

void Tenori::onLaunchpad(const std::vector<unsigned char>& m, double now) {
    if (m.size() < 3) return;
    const unsigned char st = m[0] & 0xF0;
    if (st == 0x90 || st == 0x80) {
        const bool down = st == 0x90 && m[2] > 0;
        const int x = m[1] & 15, y = m[1] >> 4;
        if (y > 7) return;
        if (x == 8) onScene(y, down, now);
        else if (x < 8) onGrid(x, y, down, now);
    } else if (st == 0xB0 && m[1] >= 104 && m[1] <= 111) {
        onTop(m[1] - 104, m[2] > 0, now);
    }
}

int Tenori::visiblePage(const Layer& L, double now) const {
    if (L.length <= 8) return 0;
    if (follow_ && playingFlag_) {
        const int64_t g = stepAt(now);
        if (g >= 0) return static_cast<int>((g % L.length) / 8);
    }
    return page_;
}

void Tenori::onGrid(int x, int y, bool down, double now) {
    if (shift_) {
        if (down) onShiftGrid(x, y);
        shiftUsed_ = true;
        return;
    }
    // Effects on the bottom two rows while Fx is held (and until each pad is let go).
    const int fi = (y - 6) * 8 + x;
    if (y >= 6 && (fxBtn_ || padFx_[fi] >= 0)) {
        if (down && fxBtn_ && padFx_[fi] < 0) {
            padFx_[fi] = styles()[static_cast<size_t>(style())].fx[static_cast<size_t>(fi)];
            sh_.send(Cmd::FxOn, padFx_[fi]);
        } else if (!down && padFx_[fi] >= 0) {
            sh_.send(Cmd::FxOff, padFx_[fi]);
            padFx_[fi] = -1;
        }
        return;
    }

    Layer& L = layers_[static_cast<size_t>(cur_)];
    switch (L.mode) {
        case MODE_SCORE: {
            if (!down) return;
            const int s = visiblePage(L, now) * 8 + x;
            L.score[y] ^= static_cast<uint16_t>(1u << s);
            if ((L.score[y] & (1u << s)) && !playingFlag_) preview(cur_, s, y, 7 - y, now);
            break;
        }
        case MODE_BOUNCE:
            if (!down) return;
            if (L.ball[x] == y) {
                L.ball[x] = -1;
            } else {
                L.ball[x] = static_cast<int8_t>(y);
                L.ballY[x] = y;
                L.ballDir[x] = 1;
                if (!playingFlag_) preview(cur_, x, 7, x, now);
            }
            break;
        case MODE_PATH: {
            if (!down) return;
            auto it = std::find(L.path.begin(), L.path.end(), std::make_pair(x, y));
            if (it != L.path.end()) {
                const int i = static_cast<int>(it - L.path.begin());
                L.path.erase(it);
                const int n = static_cast<int>(L.path.size());
                if (i < L.pathAt) --L.pathAt;
                if (i < L.pathFrom) --L.pathFrom;
                if (L.pathAt >= n) L.pathAt = 0;
                if (L.pathFrom >= n) L.pathFrom = 0;
            } else if (static_cast<int>(L.path.size()) < kPathMax) {
                L.path.emplace_back(x, y);
                if (!playingFlag_) preview(cur_, x, y, 7 - y, now);
            }
            break;
        }
        case MODE_PUSH:
            if (down) {
                const double interval = kPushRate[x] * baseStep();
                // Sound straight away, then repeat on the grid (skipping a grid hit
                // that would land right on top of this one).
                held_.push_back({cur_, x, y, playingFlag_ ? now + interval * 0.5 : 0.0});
                play(cur_, x, y, 7 - y, now, std::min(1.25f, velFor(L.volume) * 1.1f), static_cast<float>(std::max(interval, baseStep()) * 0.8));
            } else {
                held_.erase(std::remove_if(held_.begin(), held_.end(),
                                           [&](const Held& h) { return h.layer == cur_ && h.x == x && h.y == y; }),
                            held_.end());
            }
            break;
        default: break;
    }
}

void Tenori::onShiftGrid(int x, int y) {
    Layer& L = layers_[static_cast<size_t>(cur_)];
    switch (y) {
        case 0: case 1: setStyle(y * 8 + x); break;
        case 2:
            if (x < 4) {
                L.sound = x;
            } else if (L.mode != x - 4) {
                L.mode = x - 4;
                L.resetMotion();
                held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return h.layer == cur_; }), held_.end());
            }
            break;
        case 3:
            if (x <= 4) L.octave = x - 2;
            else if (x == 5) ghosts_ = !ghosts_;
            else if (x == 6) swing_ = !swing_;
            else follow_ = !follow_;
            break;
        case 4: case 5: {
            const size_t i = static_cast<size_t>((y - 4) * 8 + x);
            if (i < patchPads().size()) setLeadPatch(patchPads()[i]);
            break;
        }
        case 6: L.volume = x; break;
        case 7: {
            int len = (x + 1) * 2;
            if (L.length == len) len -= 1;  // tap again for the odd length
            L.length = len;
            break;
        }
        default: break;
    }
}

void Tenori::onScene(int row, bool down, double) {
    if (!down) return;
    if (shift_) {
        layers_[static_cast<size_t>(row)].muted = !layers_[static_cast<size_t>(row)].muted;
        shiftUsed_ = true;
        return;
    }
    cur_ = row;
}

void Tenori::nudgeBpm(float d) {
    sh_.bpm.store(std::clamp(std::round(sh_.bpm.load() + d), 40.f, 250.f));
}

void Tenori::onTop(int i, bool down, double now) {
    Layer& L = layers_[static_cast<size_t>(cur_)];
    switch (i) {
        case 0:  // Play (Shift: clear every layer)
            if (!down) break;
            if (shift_) {
                undo_ = layers_;
                haveUndo_ = true;
                for (auto& l : layers_) l.clear();
                held_.clear();
                shiftUsed_ = true;
            } else {
                setPlaying(!playingFlag_, now);
            }
            break;
        case 1:  // Page (Shift: follow the playhead)
            if (!down) break;
            if (shift_) {
                follow_ = true;
                shiftUsed_ = true;
            } else {
                page_ = visiblePage(L, now) ^ 1;
                follow_ = false;
            }
            break;
        case 2: case 3:  // tempo -/+ (Shift: by 5), hold to repeat
            if (down) {
                bpmHeld_ = i == 2 ? -1 : 1;
                nudgeBpm(static_cast<float>(bpmHeld_ * (shift_ ? 5 : 1)));
                bpmNext_ = now + 0.4;
                if (shift_) shiftUsed_ = true;
            } else {
                bpmHeld_ = 0;
            }
            break;
        case 4:  // Mode (Shift: clear this layer)
            if (!down) break;
            if (shift_) {
                clearLayer(cur_);
                shiftUsed_ = true;
            } else {
                L.mode = (L.mode + 1) % MODE_COUNT;
                L.resetMotion();
                held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return h.layer == cur_; }), held_.end());
            }
            break;
        case 5:  // Sound (Shift: undo the last clear)
            if (!down) break;
            if (shift_) {
                undo();
                shiftUsed_ = true;
            } else {
                L.sound = (L.sound + 1) % SND_COUNT;
            }
            break;
        case 6: fxBtn_ = down; break;
        case 7:
            shift_ = down;
            if (down) shiftUsed_ = false;
            break;
        default: break;
    }
}

void Tenori::releaseAll(double) {
    for (int& f : padFx_)
        if (f >= 0) { sh_.send(Cmd::FxOff, f); f = -1; }
    held_.clear();
    shift_ = fxBtn_ = false;
    bpmHeld_ = 0;
}

void Tenori::update(double now) {
    if (pendingBpm_ > 0.f && style() == pendingStyle_) {
        sh_.bpm.store(pendingBpm_);
        pendingBpm_ = 0.f;
    }
    schedule(now + kLookahead);
    if (bpmHeld_ != 0 && now >= bpmNext_) {
        nudgeBpm(static_cast<float>(bpmHeld_ * (shift_ ? 5 : 1)));
        bpmNext_ = now + 0.08;
    }
    if (now >= nextSave_) {
        nextSave_ = now + 2.0;
        saveIfChanged();
    }
}

// ----------------------------------------------------------------- lights --

void Tenori::render(double now, Frame& out) {
    Color grid[8][8] = {};
    const Style& st = styles()[static_cast<size_t>(style())];
    const Layer& L = layers_[static_cast<size_t>(cur_)];
    const Color lc = kLayerCol[cur_];
    const bool blink = std::fmod(now, 0.5) < 0.25;
    auto flashAge = [&](int li, int x, int y) { return now - flash_[li][x & 15][y & 7]; };

    ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(), [&](const Ripple& r) { return now - r.t > 1.0; }),
                   ripples_.end());

    if (shift_) {
        // Settings page.
        for (int i = 0; i < 16; ++i) {
            if (i >= styleCount()) break;
            const Color c = ledColor(styles()[static_cast<size_t>(i)].led);
            grid[i / 8][i % 8] = i == style() ? (blink ? c : sc(c, 0.34f)) : sc(c, 0.34f);
        }
        for (int x = 0; x < 4; ++x) {
            grid[2][x] = sc(soundCol(x), L.sound == x ? 1.f : 0.34f);
            grid[2][x + 4] = sc(modeCol(x), L.mode == x ? 1.f : 0.34f);
        }
        for (int x = 0; x <= 4; ++x) grid[3][x] = x == L.octave + 2 ? Color{3.f, 3.f} : (x == 2 ? Color{0.f, 1.f} : Color{1.f, 1.f});
        grid[3][5] = ghosts_ ? Color{0.f, 3.f} : Color{1.f, 0.f};
        grid[3][6] = swing_ ? Color{0.f, 3.f} : Color{1.f, 0.f};
        grid[3][7] = follow_ ? Color{0.f, 3.f} : Color{1.f, 0.f};
        const auto& pads = patchPads();
        for (size_t i = 0; i < pads.size(); ++i) {
            const auto& p = leadPatches()[static_cast<size_t>(pads[i])];
            Color c = p.kind == LeadPatch::POLY ? Color{0.f, 1.f} : (p.kind == LeadPatch::MONO ? Color{1.f, 0.f} : Color{1.f, 1.f});
            if (pads[i] == leadPatch_) c = {3.f, 3.f};
            grid[4 + i / 8][i % 8] = c;
        }
        for (int x = 0; x < 8; ++x) {
            if (x <= L.volume) grid[6][x] = x == L.volume ? Color{3.f, 3.f} : Color{0.f, 2.f};
            const int n = (L.length + 1) / 2;
            if (x < n) grid[7][x] = x == n - 1 ? ((L.length & 1) ? Color{3.f, 1.f} : Color{3.f, 3.f}) : Color{0.f, 2.f};
        }
    } else {
        // Ripples spreading from every note of every layer.
        for (const Ripple& r : ripples_) {
            const double a = now - r.t;
            if (a < 0.0 || a > 0.8) continue;
            const float rad = static_cast<float>(a * 10.0), fade = static_cast<float>(1.0 - a / 0.8);
            // Kept to the lowest brightness so notes (middle) and hits (full) stand out.
            const float k0 = (r.layer == cur_ ? 0.4f : 0.33f) * fade;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const float d = std::hypot(static_cast<float>(x) - r.x, static_cast<float>(y) - r.y);
                    const float k = std::max(0.f, 1.f - std::fabs(d - rad)) * k0;
                    if (k > 0.f) maxb(grid[y][x], sc(kLayerCol[r.layer], k));
                }
        }
        const int64_t g = stepAt(now);
        // The other layers' Score notes, faintly (Shift + row 4, pad 6).
        if (ghosts_) {
            for (int li = 0; li < kLayers; ++li) {
                const Layer& o = layers_[static_cast<size_t>(li)];
                if (li == cur_ || o.mode != MODE_SCORE || o.muted) continue;
                const int pg = visiblePage(o, now);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        if (o.score[y] & (1u << (pg * 8 + x))) maxb(grid[y][x], sc(kLayerCol[li], 0.34f));
            }
        }
        switch (L.mode) {
            case MODE_SCORE: {
                const int pg = visiblePage(L, now);
                const int ph = g >= 0 ? static_cast<int>(g % L.length) : -1;
                for (int x = 0; x < 8; ++x) {
                    const int s = pg * 8 + x;
                    if (ph == s)
                        for (int y = 0; y < 8; ++y) maxb(grid[y][x], {0.8f, 0.8f});
                    for (int y = 0; y < 8; ++y) {
                        if (!(L.score[y] & (1u << s))) continue;
                        const double fa = flashAge(cur_, s, y);
                        grid[y][x] = fa >= 0.0 && fa < 0.15 ? lc : sc(lc, s < L.length ? 0.67f : 0.34f);
                    }
                }
                break;
            }
            case MODE_BOUNCE:
                for (int x = 0; x < 8; ++x) {
                    const int top = L.ball[x];
                    if (top < 0) continue;
                    if (top < 7) maxb(grid[top][x], sc(lc, 0.34f));
                    const double fa = flashAge(cur_, x, 7);
                    if (fa >= 0.0 && fa < 0.15) grid[7][x] = {3.f, 3.f};
                    grid[std::clamp(playingFlag_ ? L.ballY[x] : top, 0, 7)][x] = lc;
                }
                break;
            case MODE_PATH: {
                const int n = static_cast<int>(L.path.size());
                // the route, faintly
                for (int i = 0; i + 1 < n || (n > 2 && i < n); ++i) {
                    const auto a = L.path[static_cast<size_t>(i)], b = L.path[static_cast<size_t>((i + 1) % n)];
                    const int steps = chebyshev(a, b);
                    for (int k = 1; k < steps; ++k) {
                        const int x = a.first + (b.first - a.first) * k / steps;
                        const int y = a.second + (b.second - a.second) * k / steps;
                        maxb(grid[y][x], sc(lc, 0.2f));
                    }
                }
                for (int i = 0; i < n; ++i) {
                    const auto p = L.path[static_cast<size_t>(i)];
                    const double fa = flashAge(cur_, p.first, p.second);
                    grid[p.second][p.first] = fa >= 0.0 && fa < 0.15 ? Color{3.f, 3.f} : sc(lc, i == 0 ? 0.9f : 0.6f);
                }
                if (playingFlag_ && n > 1 && L.pathFrom < n && L.pathAt < n && L.legTo > L.legFrom) {
                    const auto a = L.path[static_cast<size_t>(L.pathFrom)], b = L.path[static_cast<size_t>(L.pathAt)];
                    const double k = std::clamp((now - L.legFrom) / (L.legTo - L.legFrom), 0.0, 1.0);
                    const int x = static_cast<int>(std::lround(a.first + (b.first - a.first) * k));
                    const int y = static_cast<int>(std::lround(a.second + (b.second - a.second) * k));
                    grid[y][x] = lc;
                }
                break;
            }
            case MODE_PUSH:
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 8; ++y) {
                        const double fa = flashAge(cur_, x, y);
                        if (fa >= 0.0 && fa < 0.12) grid[y][x] = lc;
                    }
                for (const Held& h : held_)
                    if (h.layer == cur_) maxb(grid[h.y][h.x], sc(lc, 0.67f));
                break;
            default: break;
        }
        // Effects on the bottom two rows while Fx is held.
        bool anyFx = fxBtn_;
        for (int f : padFx_) anyFx = anyFx || f >= 0;
        if (anyFx) {
            for (int i = 0; i < 16; ++i) {
                const Color c = fxColor(st.fx[static_cast<size_t>(i)]);
                grid[6 + i / 8][i % 8] = padFx_[i] >= 0 ? c : sc(c, 0.4f);
            }
        }
    }

    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) out[static_cast<size_t>(y * 8 + x)] = quantize(grid[y][x]);

    // Right column: the layers.
    for (int r = 0; r < kLayers; ++r) {
        const Layer& o = layers_[static_cast<size_t>(r)];
        Color c;
        if (shift_) {
            c = o.muted ? Color{3.f, 0.f} : (o.empty() ? Color{0.f, 1.f} : Color{0.f, 3.f});
        } else {
            float v = 0.f;
            if (r == cur_) v = o.muted ? (blink ? 0.67f : 0.f) : 1.f;
            else if (!o.muted && !o.empty()) v = 0.34f;
            const double ha = now - layerHit_[r];
            if (!o.muted && ha >= 0.0 && ha < 0.1) v = 1.f;
            c = sc(kLayerCol[r], v);
        }
        out[static_cast<size_t>(64 + r)] = quantize(c);
    }

    // Top row: Play, Page, BPM-, BPM+, Mode, Sound, Fx, Shift.
    const int64_t g = stepAt(now);
    out[72] = quantize(shift_ ? Color{3.f, 0.f}
                              : (playingFlag_ ? (g >= 0 && g % 4 == 0 ? Color{0.f, 3.f} : Color{0.f, 1.f}) : Color{2.f, 0.f}));
    {
        Color c = kOff;
        if (shift_) c = follow_ ? Color{0.f, 3.f} : Color{0.f, 1.f};
        else if (L.mode == MODE_SCORE && L.length > 8) {
            const int pg = visiblePage(L, now);
            c = follow_ ? (pg ? Color{3.f, 3.f} : Color{1.f, 1.f}) : (pg ? Color{0.f, 3.f} : Color{0.f, 1.f});
        }
        out[73] = quantize(c);
    }
    out[74] = quantize(bpmHeld_ < 0 ? Color{3.f, 3.f} : Color{1.f, 1.f});
    out[75] = quantize(bpmHeld_ > 0 ? Color{3.f, 3.f} : Color{1.f, 1.f});
    out[76] = quantize(shift_ ? Color{3.f, 0.f} : modeCol(L.mode));
    out[77] = quantize(shift_ ? (haveUndo_ ? Color{0.f, 3.f} : kOff) : soundCol(L.sound));
    out[78] = quantize(fxBtn_ ? Color{3.f, 3.f} : Color{1.f, 1.f});
    out[79] = quantize(shift_ ? Color{3.f, 3.f} : Color{1.f, 1.f});
}

// ----------------------------------------------------------------- status --

std::string Tenori::status() const {
    const Layer& L = layers_[static_cast<size_t>(cur_)];
    const Style& st = styles()[static_cast<size_t>(style())];
    char buf[256];
    std::string snd = soundName(L.sound);
    if (L.sound == SND_LEAD) snd += " (" + leadPatches()[static_cast<size_t>(leadPatch_)].name + ")";
    std::snprintf(buf, sizeof buf, "%s %3.0f BPM  %-12s | Layer %d: %s, %s, octave %+d%s%s%s", playingFlag_ ? "PLAY" : "stop",
                  static_cast<double>(sh_.bpm.load()), st.name.c_str(), cur_ + 1, snd.c_str(), modeName(L.mode), L.octave,
                  L.mode == MODE_SCORE ? (", " + std::to_string(L.length) + " steps").c_str() : "",
                  L.muted ? ", muted" : "", L.empty() ? ", empty" : "");
    return buf;
}

// ----------------------------------------------------------------- saving --

std::string Tenori::savePath() {
    if (const char* over = std::getenv("LPTENORI_DATA")) return over;
    return (fs::u8path(dataFilePath()).parent_path() / "lp-tenori.txt").u8string();
}

std::string Tenori::serialize() const {
    std::ostringstream o;
    o << "lp-tenori 1\n";
    o << "style " << styles()[static_cast<size_t>(style())].name << "\n";
    o << "bpm " << sh_.bpm.load() << "\n";
    o << "lead " << leadPatches()[static_cast<size_t>(leadPatch_)].name << "\n";
    o << "swing " << (swing_ ? 1 : 0) << "\nghosts " << (ghosts_ ? 1 : 0) << "\nfollow " << (follow_ ? 1 : 0) << "\n";
    o << "current " << cur_ << "\n";
    for (int i = 0; i < kLayers; ++i) {
        const Layer& L = layers_[static_cast<size_t>(i)];
        o << "layer " << i << " " << soundName(L.sound) << " " << modeName(L.mode) << " " << L.octave << " " << L.length
          << " " << L.volume << " " << (L.muted ? 1 : 0) << "\n";
        for (int y = 0; y < 8; ++y) {
            if (!L.score[y]) continue;
            o << "score " << i << " " << y << " ";
            for (int s = 0; s < kTSteps; ++s) o << ((L.score[y] & (1u << s)) ? 'x' : '.');
            o << "\n";
        }
        bool anyBall = false;
        for (int8_t b : L.ball) anyBall = anyBall || b >= 0;
        if (anyBall) {
            o << "bounce " << i;
            for (int8_t b : L.ball) o << " " << static_cast<int>(b);
            o << "\n";
        }
        if (!L.path.empty()) {
            o << "path " << i;
            for (const auto& p : L.path) o << " " << p.first << "," << p.second;
            o << "\n";
        }
    }
    return o.str();
}

bool Tenori::load(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("lp-tenori", 0) != 0) return false;
    auto rest = [](const std::string& l, size_t from) {
        size_t a = l.find_first_not_of(' ', from);
        return a == std::string::npos ? std::string() : l.substr(a);
    };
    auto layerAt = [&](int i) -> Layer* { return i >= 0 && i < kLayers ? &layers_[static_cast<size_t>(i)] : nullptr; };
    for (auto& L : layers_) L.clear();
    float bpm = 0.f;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "style") {
            const std::string name = rest(line, 5);
            for (int s = 0; s < styleCount(); ++s)
                if (styles()[static_cast<size_t>(s)].name == name) {
                    pendingStyle_ = s;
                    setStyle(s);
                }
        } else if (key == "bpm") {
            ls >> bpm;
        } else if (key == "lead") {
            const int p = findPatch(rest(line, 4));
            if (p >= 0) leadPatch_ = p;
        } else if (key == "swing") {
            int v = 1; ls >> v; swing_ = v != 0;
        } else if (key == "ghosts") {
            int v = 0; ls >> v; ghosts_ = v != 0;
        } else if (key == "follow") {
            int v = 1; ls >> v; follow_ = v != 0;
        } else if (key == "current") {
            int v = 0; ls >> v; cur_ = std::clamp(v, 0, kLayers - 1);
        } else if (key == "layer") {
            int i = -1, oct = 0, len = kTSteps, vol = 6, mute = 0;
            std::string snd, mode;
            ls >> i >> snd >> mode >> oct >> len >> vol >> mute;
            Layer* L = layerAt(i);
            if (!L) continue;
            for (int k = 0; k < SND_COUNT; ++k) if (snd == soundName(k)) L->sound = k;
            for (int k = 0; k < MODE_COUNT; ++k) if (mode == modeName(k)) L->mode = k;
            L->octave = std::clamp(oct, -2, 2);
            L->length = std::clamp(len, 1, kTSteps);
            L->volume = std::clamp(vol, 0, 7);
            L->muted = mute != 0;
        } else if (key == "score") {
            int i = -1, y = -1;
            std::string row;
            ls >> i >> y >> row;
            Layer* L = layerAt(i);
            if (!L || y < 0 || y > 7) continue;
            L->score[y] = 0;
            for (int s = 0; s < kTSteps && s < static_cast<int>(row.size()); ++s)
                if (row[static_cast<size_t>(s)] == 'x') L->score[y] |= static_cast<uint16_t>(1u << s);
        } else if (key == "bounce") {
            int i = -1;
            ls >> i;
            Layer* L = layerAt(i);
            if (!L) continue;
            for (int x = 0; x < 8; ++x) {
                int b = -1;
                ls >> b;
                L->ball[x] = static_cast<int8_t>(b >= 0 && b <= 7 ? b : -1);
            }
        } else if (key == "path") {
            int i = -1;
            ls >> i;
            Layer* L = layerAt(i);
            if (!L) continue;
            std::string p;
            while (ls >> p && static_cast<int>(L->path.size()) < kPathMax) {
                int x = -1, y = -1;
                if (std::sscanf(p.c_str(), "%d,%d", &x, &y) == 2 && x >= 0 && x < 8 && y >= 0 && y < 8) L->path.emplace_back(x, y);
            }
        }
    }
    for (auto& L : layers_) L.resetMotion();
    // Switching style sets that style's tempo once the engine gets to it, so the
    // saved tempo goes in after that (update()).
    if (bpm >= 40.f && bpm <= 250.f) {
        if (pendingStyle_ < 0 || pendingStyle_ == style()) sh_.bpm.store(bpm);
        else pendingBpm_ = bpm;
    }
    sh_.send(Cmd::LeadPatch, leadPatch_);
    return true;
}

void Tenori::loadFile() {
    std::ifstream f(fs::u8path(savePath()), std::ios::binary);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        load(ss.str());
    } else {
        sh_.send(Cmd::LeadPatch, leadPatch_);
    }
    lastSaved_ = serialize();
}

void Tenori::saveIfChanged() {
    const std::string text = serialize();
    if (text == lastSaved_) return;
    const fs::path path = fs::u8path(savePath());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.parent_path() / (path.filename().u8string() + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << text;
    }
    fs::rename(tmp, path, ec);
    if (ec) {  // Windows can refuse to rename over an existing file
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    lastSaved_ = text;
}

}  // namespace dnb
