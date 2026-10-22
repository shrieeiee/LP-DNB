#include "controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "log.h"

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

const Color kSoundColor[S_COUNT + 1] = {RED, AMBER, ORANGE, GREEN, LIME, YELLOW, AMBER, YELLOW, GREEN};

const Color kFxColor[FX_COUNT] = {AMBER, AMBER, AMBER, AMBER, RED,   ORANGE, LIME,   GREEN,
                                  RED,   AMBER, ORANGE, LIME, AMBER, RED,    AMBER,  ORANGE};

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
int bassIdx(int semi) {
    for (int i = 0; i < 8; ++i)
        if (kBassScale[i] == semi) return i;
    return 0;
}
int trackHitSlot(int t) {
    if (t == T_BASS) return kBassHitSlot;
    if (t == T_STAB) return S_STAB;
    return t;
}
const int kStutDiv[4] = {4, 8, 16, 32};

}  // namespace

Controller::Controller(Shared& sh) : sh_(sh) {
    for (int s = 0; s <= S_COUNT; ++s) {
        seenHits_[s] = sh_.hits[s].load();
        hitTime_[s] = -10.0;
    }
    for (int f = 0; f < FX_COUNT; ++f) fxRelease_[f] = -10.0;
}

double Controller::beatSec() const { return 60.0 / std::max(60.f, sh_.bpm.load()); }

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

    if (y == 4) {  // drum pads
        if (down) {
            sh_.send(Cmd::Trigger, x);
            if (x == S_GHOST || x == S_HAT || x == S_OHAT || x == S_RIDE)
                spawnRipple(static_cast<float>(x), 4.f, kSoundColor[x], now, 7.f);
        }
        return;
    }

    if (y == 5) {  // bass pads
        const int semi = kBassScale[x];
        if (down) {
            bassPadHeld_[x] = true;
            lastBassIdx_ = x;
            sh_.send(Cmd::BassOn, semi);
            spawnRipple(static_cast<float>(x), 5.f, GREEN, now, 6.f);
            if (view_ == EDIT && heldStep_ >= 0) {
                sh_.bassNote[p][heldStep_].store(static_cast<int8_t>(semi));
                sh_.on[p][T_BASS][heldStep_].store(1);
            }
        } else if (bassPadHeld_[x]) {
            bassPadHeld_[x] = false;
            sh_.send(Cmd::BassOff, semi);
        }
        return;
    }

    if (y >= 6) {  // effect pads
        const int f = (y - 6) * 8 + x;
        if (down && !fxHeld_[f]) {
            fxHeld_[f] = true;
            fxPress_[f] = now;
            sh_.send(Cmd::FxOn, f);
            spawnRipple(static_cast<float>(x), static_cast<float>(y), kFxColor[f], now, 11.f);
            logf("FX: %s", fxName(f));
        } else if (!down && fxHeld_[f]) {
            fxHeld_[f] = false;
            fxRelease_[f] = now;
            if (f == FX_RISER) riserHeldFor_ = now - fxPress_[f];
            sh_.send(Cmd::FxOff, f);
        }
        return;
    }

    // rows 0..3
    if (view_ == EDIT && y < 2) {
        const int s = y * 8 + x;
        if (down) {
            bool on = sh_.on[p][selTrack_][s].load() != 0;
            sh_.on[p][selTrack_][s].store(on ? 0 : 1);
            if (selTrack_ == T_BASS) {
                if (!on) sh_.bassNote[p][s].store(static_cast<int8_t>(kBassScale[lastBassIdx_]));
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
    if (view_ == EDIT) {
        if (viewHeld_) {
            const int p = sh_.pattern.load();
            for (int s = 0; s < kSteps; ++s) sh_.on[p][row][s].store(0);
            comboUsed_ = true;
            logf("Cleared %s in pattern %d", trackName(row), p + 1);
        } else {
            selTrack_ = row;
            heldStep_ = -1;
            logf("Editing: %s", trackName(row));
        }
    } else {
        bool m = !sh_.mute[row].load();
        sh_.mute[row].store(m);
        logf("%s %s", trackName(row), m ? "muted" : "unmuted");
    }
}

void Controller::nudgeBpm(float d) {
    float b = std::clamp(std::round(sh_.bpm.load() + d), 100.f, 200.f);
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
                const int target = i - 4;
                if (viewHeld_) {
                    const int src = sh_.pattern.load();
                    for (int t = 0; t < kTracks; ++t)
                        for (int s = 0; s < kSteps; ++s) sh_.on[target][t][s].store(sh_.on[src][t][s].load());
                    for (int s = 0; s < kSteps; ++s) sh_.bassNote[target][s].store(sh_.bassNote[src][s].load());
                    comboUsed_ = true;
                    logf("Copied pattern %d -> %d", src + 1, target + 1);
                } else {
                    sh_.send(Cmd::QueuePattern, target);
                    logf("Pattern %d %s", target + 1, sh_.playing.load() ? "queued for next bar" : "selected");
                }
            }
            break;
    }
}

void Controller::releaseAll(double now) {
    for (int f = 0; f < FX_COUNT; ++f)
        if (fxHeld_[f]) onGrid(f % 8, 6 + f / 8, false, now);
    for (int x = 0; x < 8; ++x)
        if (bassPadHeld_[x]) onGrid(x, 5, false, now);
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

    // 1) base layer: level meter per track while playing, plasma while stopped
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 4; ++y) {
            Color c = OFF;
            if (playing) {
                static const Color lv[4] = {GREEN, LIME, AMBER, RED};
                const int l = 3 - y;
                float f = std::clamp(env_[x] * 4.2f - l, 0.f, 1.f);
                c = scale(lv[l], f);
            } else {
                const float t = static_cast<float>(now);
                float v = std::sin(x * 0.55f + t * 1.1f) + std::sin(y * 0.8f - t * 0.9f) +
                          std::sin((x + y) * 0.45f + t * 0.6f) +
                          std::sin(std::hypot(x - 3.5f, y - 1.5f) * 0.9f - t * 1.4f);
                c = {1.f + std::sin(v * 1.4f), 1.f + std::sin(v * 1.4f + 2.1f)};
            }
            cv[x][y] = c;
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

    // Pick up drum/bass hits from the audio thread.
    for (int s = 0; s <= S_COUNT; ++s) {
        uint32_t h = sh_.hits[s].load(std::memory_order_relaxed);
        if (h == seenHits_[s]) continue;
        seenHits_[s] = h;
        hitTime_[s] = now;
        if (s == kBassHitSlot) env_[T_BASS] = 1.f;
        else if (s == S_STAB) env_[T_STAB] = 1.f;
        else if (s <= S_RIDE) env_[s] = 1.f;
        switch (s) {
            case S_KICK: spawnRipple(3.5f, 4.2f, RED, now, 10.f); break;           // rises from the bottom
            case S_SNARE: spawnRipple(3.5f, -1.2f, AMBER, now, 10.f); break;       // falls from the top
            case S_CRASH: spawnRipple(3.5f, 1.5f, AMBER, now, 7.f); break;         // bursts from the middle
            case S_STAB:
                spawnRipple(-1.f, 1.5f, YELLOW, now, 8.f);                         // closes in from the sides
                spawnRipple(8.f, 1.5f, YELLOW, now, 8.f);
                break;
            default: break;
        }
    }
    if (sh_.bassPlaying.load() >= 0) env_[T_BASS] = std::max(env_[T_BASS], 0.65f);
    for (float& e : env_) e *= std::exp(-dt * 7.f);

    Color grid[8][8];
    Color cv[8][4];
    buildCanvas(now, dt, cv);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 4; ++y) grid[y][x] = cv[x][y];

    const int p = sh_.pattern.load();
    const bool playing = sh_.playing.load();
    const int curStep = sh_.step.load();

    // EDIT view: 16 steps of the selected track on the top two rows.
    if (view_ == EDIT) {
        for (int s = 0; s < kSteps; ++s) {
            const int x = s % 8, y = s / 8;
            const bool on = sh_.on[p][selTrack_][s].load() != 0;
            Color c = (s % 4 == 0) ? DIM_RED : OFF;
            if (on) {
                c = selTrack_ == T_BASS
                        ? Color{bassIdx(sh_.bassNote[p][s].load()) * 3.f / 7.f, 3.f}  // greener = lower
                        : GREEN;
            }
            if (playing && s == curStep) c = on ? AMBER : RED;
            if (s == heldStep_) c = frac(now * 6.0) < 0.5f ? AMBER : OFF;
            grid[y][x] = c;
        }
    }

    // Row 4: drum pads flash on every hit, live or sequenced.
    for (int x = 0; x < 8; ++x) {
        float k = std::clamp(1.f - static_cast<float>((now - hitTime_[x]) / 0.18), 0.f, 1.f);
        grid[4][x] = lerp(scale(kSoundColor[x], 0.34f), kSoundColor[x], k);
    }

    // Row 5: bass pads.
    const int bassSemi = sh_.bassPlaying.load();
    for (int x = 0; x < 8; ++x) {
        Color c = (x == 0 || x == 7) ? DIM_AMBER : DIM_GREEN;
        if (view_ == EDIT && x == lastBassIdx_) c = {1.f, 2.f};
        if (bassSemi == kBassScale[x]) c = GREEN;
        if (bassPadHeld_[x]) c = AMBER;
        grid[5][x] = c;
    }

    // Rows 6-7: effect pads, animated while held.
    const double beat = beatSec();
    for (int f = 0; f < FX_COUNT; ++f) {
        const int x = f % 8, y = 6 + f / 8;
        Color c = f < 8 ? DIM_RED : (f < 13 ? DIM_GREEN : DIM_AMBER);
        if (fxHeld_[f]) {
            const double t = now - fxPress_[f];
            if (f <= FX_STUT32) {
                const double L = beat * 4.0 / kStutDiv[f];
                c = frac(t / L) < 0.5f ? AMBER : DIM_RED;  // blinks at the repeat rate
            } else if (f == FX_TAPESTOP) {
                c = {3.f, 3.f * sh_.tapeRate.load()};
            } else if (f == FX_RISER) {
                c = scale(AMBER, 0.34f + 0.66f * sh_.riserAmt.load());
            } else {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(t) * kTwoPi * 4.f);
                c = scale(kFxColor[f], 0.5f + 0.5f * pulse);
            }
        }
        grid[y][x] = c;
    }

    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) out[y * 8 + x] = quant(grid[y][x]);

    // Scene buttons (right column).
    for (int t = 0; t < kTracks; ++t) {
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
    out[73] = quant(view_ == EDIT ? AMBER : DIM_GREEN);
    out[74] = quant(bpmHeld_ < 0 ? AMBER : DIM_AMBER);
    out[75] = quant(bpmHeld_ > 0 ? AMBER : DIM_AMBER);
    for (int i = 0; i < 4; ++i) {
        Color c = DIM_RED;
        if (i == p) c = GREEN;
        if (i == queued) c = frac(now * 4.0) < 0.5f ? AMBER : OFF;
        out[76 + i] = quant(c);
    }
}

}  // namespace dnb
