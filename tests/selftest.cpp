// Offline checks for the engine and file formats. Build target: lp-dnb-tests.
// Run it from any folder; it writes its scratch files to the current directory.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "engine.h"
#include "sample.h"
#include "storage.h"
#include "styles.h"
#include "tenori.h"

using namespace dnb;

static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                       \
            std::printf("\n");                              \
            ++failures;                                     \
        }                                                   \
    } while (0)

static void writeWav(const std::string& path, int fmt, int bits, int ch, int rate, const std::vector<float>& mono) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = static_cast<uint32_t>(mono.size() * ch * bits / 8);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(static_cast<uint16_t>(fmt)); u16(static_cast<uint16_t>(ch)); u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate * ch * bits / 8)); u16(static_cast<uint16_t>(ch * bits / 8)); u16(static_cast<uint16_t>(bits));
    f.write("data", 4); u32(bytes);
    for (float v : mono)
        for (int c = 0; c < ch; ++c) {
            const float x = c ? -v : v;
            if (fmt == 3) { f.write(reinterpret_cast<const char*>(&x), 4); continue; }
            if (bits == 8) { const uint8_t b = static_cast<uint8_t>(std::lround(x * 127.f) + 128); f.write(reinterpret_cast<const char*>(&b), 1); }
            if (bits == 16) { const int16_t b = static_cast<int16_t>(std::lround(x * 32767.f)); f.write(reinterpret_cast<const char*>(&b), 2); }
            if (bits == 24) { const int32_t b = static_cast<int32_t>(std::lround(x * 8388607.f)); f.write(reinterpret_cast<const char*>(&b), 3); }
            if (bits == 32) { const int32_t b = static_cast<int32_t>(std::lround(x * 2147483647.0)); f.write(reinterpret_cast<const char*>(&b), 4); }
        }
}

static void testWavFormats() {
    std::vector<float> sine(4410);
    for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * std::sin(6.2831853f * 441.f * i / 44100.f);
    struct F { int fmt, bits, ch; } fs[] = {{1, 8, 1}, {1, 16, 1}, {1, 16, 2}, {1, 24, 2}, {1, 32, 1}, {3, 32, 2}};
    for (const F& f : fs) {
        const std::string path = "selftest_" + std::to_string(f.bits) + "_" + std::to_string(f.ch) + "_" + std::to_string(f.fmt) + ".wav";
        writeWav(path, f.fmt, f.bits, f.ch, 44100, sine);
        std::string err;
        auto s = loadWav(path, 48000.f, err);
        CHECK(s != nullptr, "load %s: %s", path.c_str(), err.c_str());
        if (!s) continue;
        CHECK(std::abs(static_cast<int>(s->L.size()) - 4800) < 4, "%s resampled length %zu", path.c_str(), s->L.size());
        float peak = 0.f;
        for (size_t i = 100; i < s->L.size() - 300; ++i) peak = std::max(peak, std::fabs(s->L[i]));
        CHECK(std::fabs(peak - 0.5f) < 0.02f, "%s peak %.3f", path.c_str(), peak);
        if (f.ch == 2) CHECK(std::fabs(s->L[500] + s->R[500]) < 0.02f, "%s stereo channels", path.c_str());
        std::remove(path.c_str());
    }
    std::string err;
    CHECK(!loadWav("does-not-exist.wav", 48000.f, err), "missing file should fail");
}

// Renders `frames` of audio; returns the interleaved stereo output.
static std::vector<float> render(Engine& e, int frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2), buf(512);
    for (int i = 0; i < frames; i += 256) {
        const int n = std::min(256, frames - i);
        e.process(buf.data(), n);
        std::memcpy(out.data() + i * 2, buf.data(), sizeof(float) * n * 2);
    }
    return out;
}

static void testTimingAndSamples() {
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    Engine e(*sh, 48000.f);
    std::vector<float> buf(1024);
    for (int i = 0; i < 20; ++i) e.process(buf.data(), 256);  // settle
    // A command lands one block after it was sent (constant latency), not at the start
    // of whichever block happens to pick it up. Sent half a block ago into a 512-frame
    // block: expect sample 384, plus the limiter's 1.5 ms (72-sample) look-ahead.
    Cmd c;
    c.type = Cmd::Trigger;
    c.a = 0;
    c.t = monotonicSec() - 128.0 / 48000.0;  // sent half a block ago
    sh->cmds.push(c);
    std::vector<float> o(512 * 2);
    e.process(o.data(), 512);
    int first = -1;
    for (int i = 0; i < 512; ++i) if (std::fabs(o[2 * i]) > 1e-3f) { first = i; break; }
    CHECK(first >= 440 && first <= 470, "trigger landed at sample %d (expected about 456)", first);

    // Sampled drum: a 10 ms click replaces the kick.
    auto smp = std::make_shared<Sample>();
    smp->name = "click";
    smp->L.assign(480, 0.f);
    smp->R.assign(480, 0.f);
    smp->L[0] = smp->R[0] = 0.9f;
    sh->send(Cmd::SetSample, 0, 0, 0.f, smp.get());
    for (int i = 0; i < 40; ++i) e.process(buf.data(), 256);
    sh->send(Cmd::Trigger, 0);
    auto out = render(e, 9600);
    double late = 0.0;
    for (size_t i = 2 * 4800; i < out.size(); ++i) late += std::fabs(out[i]);
    CHECK(late < 2.0, "sampled click should be silent after 100 ms (energy %.3f)", late);
    sh->send(Cmd::SetSample, 0, 0, 0.f, nullptr);
    e.process(buf.data(), 256);
}

static void testLimiterAndStereo() {
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    sh->volume.store(1.f);
    Engine e(*sh, 48000.f);
    sh->send(Cmd::Play);
    sh->send(Cmd::FxOn, FX_HORN);
    auto out = render(e, 48000 * 4);
    float peak = 0.f;
    double side = 0, mid = 0;
    bool finite = true;
    for (size_t i = 0; i < out.size(); i += 2) {
        if (!std::isfinite(out[i]) || !std::isfinite(out[i + 1])) finite = false;
        peak = std::max({peak, std::fabs(out[i]), std::fabs(out[i + 1])});
        side += std::fabs(out[i] - out[i + 1]);
        mid += std::fabs(out[i] + out[i + 1]);
    }
    CHECK(finite, "non-finite output");
    CHECK(peak <= 0.975f, "limiter ceiling: peak %.3f", peak);
    CHECK(side > mid * 0.03, "output should be stereo (side/mid %.3f)", side / mid);
}

static void testLongPatternsAndSong() {
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    const int p = 0;
    CHECK(sh->patternLength(p) == 32, "built-in patterns are two bars (got %d)", sh->patternLength(p));
    sh->songMode.store(true);
    sh->songLen.store(2);
    sh->songSlot[0].store(0); sh->songRepeats[0].store(1);
    sh->songSlot[1].store(3); sh->songRepeats[1].store(1);
    Engine e(*sh, 48000.f);
    sh->send(Cmd::Play);
    // Two bars of pattern 0 at 174 BPM = 2.76 s, then the song moves on.
    render(e, static_cast<int>(48000 * 3.2));
    CHECK(sh->pattern.load() == 3, "song mode should be on entry 2 (pattern %d)", sh->pattern.load());
    render(e, static_cast<int>(48000 * 2.8));
    CHECK(sh->pattern.load() == 0, "song should loop back (pattern %d)", sh->pattern.load());
}

static void testSaveRoundTrip() {
    setEnv("LPDNB_DATA", "selftest-data.txt");
    std::remove("selftest-data.txt");
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    const int p = 1 * kMaxSlots + kFactorySlots;  // a new pattern in style 2
    copyPattern(*sh, 1 * kMaxSlots, p);
    sh->patternName[p] = "Round trip";
    sh->slotCount[1].store(kFactorySlots + 1);
    setPatternLength(*sh, p, 48);
    sh->lock[p][1][20][LOCK_PITCH].store(-5);
    sh->lock[p][T_BASS][3][LOCK_FILTER].store(60);
    sh->on[p][T_LEAD][5].store(STEP_ON);
    sh->note[p][T_LEAD][5].store(67);
    sh->on[p][T_LEAD][6].store(STEP_TIE);
    sh->mix[1][2].pan.store(-0.4f);
    sh->mix[1][kBassSlot].volume.store(1.3f);
    sh->humanize.store(0.7f);
    sh->songLen.store(1);
    sh->songSlot[0].store(5);
    sh->songRepeats[0].store(3);
    Settings st;
    st.styleName = "House";
    st.samples["House"][0] = "C:/kits/kick.wav";
    st.clockOut = "Some synth";
    std::string err;
    const std::string text = serialize(*sh, st);
    CHECK(writeData(text, err), "write: %s", err.c_str());

    auto sh2 = std::make_unique<Shared>();
    loadAllPresets(*sh2);
    Settings st2;
    std::string msg;
    CHECK(loadData(*sh2, st2, msg), "load: %s", msg.c_str());
    CHECK(serialize(*sh2, st2) == text, "save -> load -> save should be identical");
    CHECK(sh2->patternLength(p) == 48, "length %d", sh2->patternLength(p));
    CHECK(sh2->lock[p][1][20][LOCK_PITCH].load() == -5, "lock lost");
    CHECK(sh2->note[p][T_LEAD][5].load() == 67 && stepKind(sh2->on[p][T_LEAD][6].load()) == STEP_TIE, "lead row lost");
    CHECK(std::fabs(sh2->mix[1][2].pan.load() + 0.4f) < 1e-4f, "mixer lost");
    CHECK(st2.samples["House"][0] == "C:/kits/kick.wav" && st2.clockOut == "Some synth", "settings lost");
    CHECK(sh2->songRepeats[0].load() == 3 && sh2->songSlot[0].load() == 5, "song lost");
    // Factory patterns that weren't touched aren't written out.
    CHECK(text.find("name=Two-step") == std::string::npos, "unchanged built-ins shouldn't be saved");
    std::remove("selftest-data.txt");
}

static void testKitFolder() {
    namespace fs = std::filesystem;
    const fs::path dir = "selftest-kit";
    fs::remove_all(dir);
    fs::create_directories(dir / "hats");
    std::vector<float> click(100, 0.1f);
    for (const char* n : {"BD 01.wav", "Snare Tight.wav", "hats/HH closed.wav", "hats/OH 2.wav", "Ride bell.wav", "crash.wav", "notes.txt"})
        writeWav((dir / n).u8string(), 1, 16, 1, 48000, click);
    const Style& dnbStyle = styles()[0];  // kick, snare, ghost snare, closed hat, open hat, ride, crash
    const auto m = matchKitFolder(dir.u8string(), dnbStyle);
    auto name = [&](int i) { return m[static_cast<size_t>(i)].empty() ? std::string("-") : fs::u8path(m[static_cast<size_t>(i)]).filename().u8string(); };
    CHECK(name(0) == "BD 01.wav", "kick -> %s", name(0).c_str());
    CHECK(name(1) == "Snare Tight.wav", "snare -> %s", name(1).c_str());
    CHECK(name(3) == "HH closed.wav", "closed hat -> %s", name(3).c_str());
    CHECK(name(4) == "OH 2.wav", "open hat -> %s", name(4).c_str());
    CHECK(name(5) == "Ride bell.wav", "ride -> %s", name(5).c_str());
    CHECK(name(6) == "crash.wav", "crash -> %s", name(6).c_str());
    fs::remove_all(dir);
}

// --------------------------------------------------- scheduled notes --
// Renders on a virtual clock (block start = vt) so the timing is exact.
struct VClock {
    Engine& e;
    double vt;
    std::vector<float> out;
    void run(int frames, Tenori* t = nullptr) {
        std::vector<float> buf(512);
        for (int i = 0; i < frames; i += 256) {
            if (t) t->schedule(vt + 0.06);
            e.process(buf.data(), 256, vt);
            vt += 256.0 / 48000.0;
            out.insert(out.end(), buf.begin(), buf.end());
        }
    }
};

static Cmd noteCmd(int slot, int pitch, float vel, float gate, double t) {
    Cmd c;
    c.type = Cmd::Note;
    c.a = slot; c.b = pitch; c.f = vel; c.g = gate; c.t = t;
    return c;
}

static double rms(const std::vector<float>& v, size_t fromFrame, size_t toFrame) {
    double s = 0.0;
    size_t n = 0;
    for (size_t i = fromFrame * 2; i < toFrame * 2 && i < v.size(); ++i, ++n) s += v[i] * v[i];
    return n ? std::sqrt(s / n) : 0.0;
}

static void testScheduledNotes() {
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    leadPatches();
    Engine e(*sh, 48000.f);
    VClock vc{e, 1000.0, {}};
    vc.run(48000 / 4);  // settle
    vc.out.clear();
    const double start = vc.vt;

    // Stamped 100 ms ahead: held back, then lands exactly then, plus the usual one
    // block of latency (256) and the limiter's look-ahead (72).
    sh->cmds.push(noteCmd(0, 0, 1.f, 0.f, start + 0.1));
    vc.run(48000 / 2);
    int first = -1;
    for (size_t i = 0; i < vc.out.size() / 2; ++i) if (std::fabs(vc.out[2 * i]) > 1e-3f) { first = static_cast<int>(i); break; }
    CHECK(first >= 5120 && first <= 5136, "scheduled kick landed at sample %d (expected about 5128)", first);

    // A bass note with a 50 ms gate is let go; with a 400 ms gate it's still going.
    for (float gate : {0.05f, 0.4f}) {
        vc.out.clear();
        sh->cmds.push(noteCmd(kBassSlot, 0, 1.f, gate, vc.vt));
        vc.run(48000);
        if (gate < 0.1f) {
            const double late = rms(vc.out, 24000, 28800);  // 500-600 ms: release tail is over
            CHECK(late < 0.01, "short bass note should have been released (rms %.4f)", late);
        } else {
            const double held = rms(vc.out, 9600, 16800);  // 200-350 ms: still held
            CHECK(held > 0.1, "long bass note should still sound (rms %.4f)", held);
        }
        vc.run(48000);  // let it die away
    }

    // A release only lets go of its own note: a short lead note followed by a long
    // one on the same pitch must not cut the long one off.
    sh->cmds.push(noteCmd(kLeadSlot, 60, 1.f, 0.05f, vc.vt));
    sh->cmds.push(noteCmd(kLeadSlot, 60, 1.f, 1.0f, vc.vt + 0.02));
    vc.out.clear();
    vc.run(48000 / 2);
    // Pluck patch decays by itself, so compare against a lone short note.
    const double heldOn = rms(vc.out, 12000, 18000);
    vc.run(48000 * 2);
    sh->cmds.push(noteCmd(kLeadSlot, 60, 1.f, 0.05f, vc.vt));
    vc.out.clear();
    vc.run(48000 / 2);
    const double alone = rms(vc.out, 12000, 18000);
    CHECK(heldOn > alone * 2.0 + 1e-4, "a newer lead note was cut off by an older note's release (%.4f vs %.4f)", heldOn, alone);

    // Layer synths are separate: two mono sounds on two synths both play at once
    // (one shared mono synth would let the second note steal the first).
    int mono = -1;
    for (size_t i = 0; i < leadPatches().size(); ++i)
        if (leadPatches()[i].kind == LeadPatch::MONO) { mono = static_cast<int>(i); break; }
    CHECK(mono >= 0, "no mono lead patch");
    for (int k : {3, 4}) {
        Cmd c;
        c.type = Cmd::SynthPatch; c.a = k; c.b = mono; c.t = vc.vt;
        sh->cmds.push(c);
    }
    vc.run(48000 * 2);
    sh->cmds.push(noteCmd(kSynthSlot0 + 3, 48, 1.f, 1.f, vc.vt));
    vc.out.clear();
    vc.run(48000 / 2);
    const double one = rms(vc.out, 4800, 14400);
    vc.run(48000 * 2);
    sh->cmds.push(noteCmd(kSynthSlot0 + 3, 48, 1.f, 1.f, vc.vt));
    sh->cmds.push(noteCmd(kSynthSlot0 + 4, 55, 1.f, 1.f, vc.vt));
    vc.out.clear();
    vc.run(48000 / 2);
    const double two = rms(vc.out, 4800, 14400);
    CHECK(one > 0.01, "layer synth didn't sound (rms %.4f)", one);
    CHECK(two > one * 1.2, "two layer synths should sound together (%.4f vs %.4f alone)", two, one);
}

static int hitsDuring(Shared& sh, Engine& e, Tenori& t, int slot, int steps) {
    VClock vc{e, monotonicSec(), {}};
    vc.run(2048);  // style switch reaches the engine
    const uint32_t h0 = sh.hits[slot].load();
    t.setPlaying(true, vc.vt);
    const double stepSec = 60.0 / sh.bpm.load() / 4.0;
    // Stop half a step before the end so the next step isn't counted.
    vc.run(static_cast<int>((steps - 0.5) * stepSec * 48000.0) - 256, &t);
    t.setPlaying(false, vc.vt);
    vc.run(4096);
    return static_cast<int>(sh.hits[slot].load() - h0);
}

static void testTenori() {
    setEnv("LPTENORI_DATA", "tenori-test.txt");
    std::remove("tenori-test.txt");
    auto sh = std::make_unique<Shared>();
    loadAllPresets(*sh);
    leadPatches();
    Engine e(*sh, 48000.f);
    {
        // Bounce: a ball from row 3 lands every 2 * (7 - 3) = 8 steps, first at step 4.
        Tenori t(*sh);
        t.setStyle(1);  // House: straight timing
        for (int i = 0; i < kLayers; ++i) t.layer(i).clear();
        t.layer(1).sound = SND_DRUMS;
        t.layer(1).mode = MODE_BOUNCE;
        t.layer(1).ball[0] = 3;
        t.layer(1).resetMotion();
        const int n = hitsDuring(*sh, e, t, 0, 32);
        CHECK(n == 4, "bounce from row 3 should hit 4 times in 32 steps (got %d)", n);
    }
    {
        // Score with a 3-step loop: a note on its first step plays every 3 steps.
        Tenori t(*sh);
        for (int i = 0; i < kLayers; ++i) t.layer(i).clear();
        t.layer(0).sound = SND_DRUMS;
        t.layer(0).mode = MODE_SCORE;
        t.layer(0).length = 3;
        t.layer(0).score[6] = 1;  // second drum
        const int n = hitsDuring(*sh, e, t, 1, 30);
        CHECK(n == 10, "3-step loop should play 10 times in 30 steps (got %d)", n);
        // Muted layers stay quiet.
        t.layer(0).muted = true;
        CHECK(hitsDuring(*sh, e, t, 1, 16) == 0, "muted layer played");
    }
    {
        // Path: (0,7) -> (4,7) is 4 steps, back is 4: one note every 4 steps.
        Tenori t(*sh);
        for (int i = 0; i < kLayers; ++i) t.layer(i).clear();
        t.layer(6).sound = SND_DRUMS;
        t.layer(6).mode = MODE_PATH;
        t.layer(6).path = {{0, 7}, {4, 7}};
        t.layer(6).resetMotion();
        const int n = hitsDuring(*sh, e, t, 0, 16);
        CHECK(n == 4, "path with 4-step legs should play 4 times in 16 steps (got %d)", n);
    }
    {
        // Save file round trip.
        Tenori a(*sh);
        a.layer(2).sound = SND_CHORDS;
        a.layer(2).octave = -1;
        a.layer(2).length = 11;
        a.layer(2).score[3] = 0x8421;
        a.layer(4).ball[5] = 2;
        a.layer(6).path = {{1, 2}, {7, 7}, {0, 0}};
        a.layer(5).muted = true;
        a.setPatch(0, 2);
        a.setPatch(7, 9);
        const std::string text = a.serialize();
        Tenori b(*sh);
        CHECK(b.load(text), "tenori file didn't load");
        CHECK(b.serialize() == text, "tenori file doesn't round-trip:\n%s\n---\n%s", text.c_str(), b.serialize().c_str());
        a.saveIfChanged();
        std::ifstream f("tenori-test.txt");
        CHECK(static_cast<bool>(f), "tenori file wasn't written");
    }
    std::remove("tenori-test.txt");
}

int main() {
    styles();
    testWavFormats();
    testKitFolder();
    testTimingAndSamples();
    testLimiterAndStereo();
    testLongPatternsAndSong();
    testSaveRoundTrip();
    testScheduledNotes();
    testTenori();
    std::printf(failures ? "%d check(s) failed\n" : "All checks passed\n", failures);
    return failures ? 1 : 0;
}
