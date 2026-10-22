#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include "app.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

#include "engine.h"
#include "log.h"
#include "sample.h"
#include "styles.h"

namespace dnb {

constexpr int kSampleRate = 48000;  // fixed; miniaudio converts if the device differs

struct App::Audio {
    ma_context ctx{};
    bool ctxOk = false;
    ma_device dev{};
    bool devOk = false;
    std::atomic<bool> lost{false};
    std::atomic<bool> closing{false};
    Engine* engine = nullptr;
    std::vector<ma_device_id> ids;  // parallel to App::audioNames_

    static void dataCb(ma_device* d, void* out, const void*, ma_uint32 frames) {
        auto* a = static_cast<Audio*>(d->pUserData);
        a->engine->process(static_cast<float*>(out), static_cast<int>(frames));
    }
    static void notifyCb(const ma_device_notification* n) {
        auto* a = static_cast<Audio*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && !a->closing.load()) a->lost = true;
    }
    void close() {
        if (!devOk) return;
        closing = true;
        ma_device_uninit(&dev);
        closing = false;
        devOk = false;
    }
};

// Pulls recorded audio out of the engine's FIFO on its own thread (so dragging
// the window can't make it overflow) and writes a 24-bit stereo WAV.
struct App::Recorder {
    Shared& sh;
    std::ofstream f;
    std::thread th;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> frames{0};
    explicit Recorder(Shared& s) : sh(s) {}

    bool start(const std::string& path) {
        f.open(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        header(0);
        float drop[4096];
        while (sh.recFifo.read(drop, 4096) > 0) {}  // anything stale
        sh.recordAudio.store(true);
        th = std::thread([this] { run(); });
        return true;
    }
    void header(uint32_t nFrames) {
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t data = nFrames * 6;
        f.seekp(0);
        f.write("RIFF", 4); u32(36 + data); f.write("WAVE", 4);
        f.write("fmt ", 4); u32(16); u16(1); u16(2); u32(kSampleRate); u32(kSampleRate * 6); u16(6); u16(24);
        f.write("data", 4); u32(data);
    }
    void drain() {
        float buf[4096];
        unsigned char out[4096 * 3];
        size_t n;
        while ((n = sh.recFifo.read(buf, 4096)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                const float v = std::max(-1.f, std::min(1.f, buf[i]));
                const int32_t x = static_cast<int32_t>(std::lround(v * 8388607.f));
                out[3 * i] = static_cast<unsigned char>(x & 0xFF);
                out[3 * i + 1] = static_cast<unsigned char>((x >> 8) & 0xFF);
                out[3 * i + 2] = static_cast<unsigned char>((x >> 16) & 0xFF);
            }
            f.write(reinterpret_cast<const char*>(out), static_cast<std::streamsize>(n * 3));
            frames += n / 2;
        }
    }
    void run() {
        while (!stop.load()) {
            drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    void finish() {
        sh.recordAudio.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));  // let the last block land
        stop = true;
        if (th.joinable()) th.join();
        drain();
        header(static_cast<uint32_t>(frames.load()));
        f.close();
    }
};

App::App(bool nullAudio)
    : sh_(std::make_unique<Shared>()), audio_(std::make_unique<Audio>()) {
    loadAllPresets(*sh_);  // also builds the style table before audio starts
    leadPatches();
    sh_->bpm.store(styles()[0].bpm);
    midi_ = std::make_unique<MidiHub>();
    clock_ = std::make_unique<MidiClockOut>(*sh_);
    loadSaved();           // your patterns and settings, before the engine starts
    engine_ = std::make_unique<Engine>(*sh_, static_cast<float>(kSampleRate));
    ctl_ = std::make_unique<Controller>(*sh_);
    sh_->send(Cmd::LeadPatch, leadPatch_);
    audio_->engine = engine_.get();

    ma_result r;
    if (nullAudio) {
        ma_backend nb[] = {ma_backend_null};
        r = ma_context_init(nb, 1, nullptr, &audio_->ctx);
    } else {
        r = ma_context_init(nullptr, 0, nullptr, &audio_->ctx);
    }
    audio_->ctxOk = r == MA_SUCCESS;
    if (!audio_->ctxOk) {
        audioStatus_ = "No audio system available";
        logf("%s", audioStatus_.c_str());
        return;
    }
    refreshAudioDevices();
    openAudio();
    nextAudioScan_ = monotonicSec() + 3.0;
}

App::~App() {
    if (recording_) stopRecording();
    saveNow();
    clock_.reset();
    audio_->close();
    if (audio_->ctxOk) ma_context_uninit(&audio_->ctx);
}

bool App::audioRunning() const { return audio_->devOk && ma_device_is_started(&audio_->dev); }

// ------------------------------------------------------------------ audio --

bool App::refreshAudioDevices() {
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&audio_->ctx, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return false;
    std::vector<std::string> names;
    std::vector<ma_device_id> ids;
    for (ma_uint32 i = 0; i < count; ++i) {
        names.emplace_back(infos[i].name);
        ids.push_back(infos[i].id);
    }
    const bool changed = names != audioNames_;
    if (changed) {
        for (const auto& n : names)
            if (std::find(audioNames_.begin(), audioNames_.end(), n) == audioNames_.end() && !audioNames_.empty())
                logf("Audio device plugged in: %s", n.c_str());
    }
    audioNames_ = names;
    audio_->ids = ids;
    return changed;
}

void App::openAudio() {
    audio_->close();
    audio_->lost = false;

    const ma_device_id* id = nullptr;
    std::string target;
    if (!preferredAudio_.empty()) {
        auto it = std::find(audioNames_.begin(), audioNames_.end(), preferredAudio_);
        if (it != audioNames_.end()) {
            id = &audio_->ids[static_cast<size_t>(it - audioNames_.begin())];
            target = preferredAudio_;
        }
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kSampleRate;
    cfg.periodSizeInFrames = static_cast<ma_uint32>(audioBuffer_);
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.playback.shareMode = audioExclusive_ ? ma_share_mode_exclusive : ma_share_mode_shared;
    cfg.dataCallback = &Audio::dataCb;
    cfg.notificationCallback = &Audio::notifyCb;
    cfg.pUserData = audio_.get();

    bool exclusiveFailed = false;
    ma_result ir = ma_device_init(&audio_->ctx, &cfg, &audio_->dev);
    if (ir != MA_SUCCESS && audioExclusive_) {  // exclusive refused (device busy / format): fall back
        exclusiveFailed = true;
        cfg.playback.shareMode = ma_share_mode_shared;
        ir = ma_device_init(&audio_->ctx, &cfg, &audio_->dev);
    }
    if (ir != MA_SUCCESS) {
        const std::string msg = "Couldn't open audio output" + (target.empty() ? std::string() : ": " + target);
        if (msg != audioStatus_) logf("%s", msg.c_str());  // don't repeat every retry
        audioStatus_ = msg;
        openedAudio_.clear();
        return;
    }
    audio_->devOk = true;
    if (ma_device_start(&audio_->dev) != MA_SUCCESS) {
        audio_->close();
        if (audioStatus_ != "Couldn't start audio output") logf("Couldn't start audio output");
        audioStatus_ = "Couldn't start audio output";
        openedAudio_.clear();
        return;
    }
    openedAudio_ = target;
    char name[256] = {};
    ma_device_get_name(&audio_->dev, ma_device_type_playback, name, sizeof name, nullptr);
    audioStatus_ = std::string(target.empty() ? "System default: " : "") + name;
    const ma_uint32 period = audio_->dev.playback.internalPeriodSizeInFrames;
    const ma_uint32 periods = std::max<ma_uint32>(1, audio_->dev.playback.internalPeriods);
    const ma_uint32 rate = audio_->dev.playback.internalSampleRate ? audio_->dev.playback.internalSampleRate : kSampleRate;
    latencyMs_ = 1000.0 * period * periods / rate;
    if (exclusiveFailed) audioStatus_ += " (exclusive mode refused, using shared)";
    else if (audioExclusive_) audioStatus_ += " (exclusive)";
    logf("Audio output: %s, %u-frame buffer, about %.1f ms", audioStatus_.c_str(), period, latencyMs_);
}

void App::setAudioBuffer(int frames) {
    frames = std::max(32, std::min(2048, frames));
    if (frames == audioBuffer_) return;
    audioBuffer_ = frames;
    if (audio_->ctxOk) openAudio();
    nextSave_ = 0.0;
}

void App::setAudioExclusive(bool on) {
    if (on == audioExclusive_) return;
    audioExclusive_ = on;
    if (audio_->ctxOk) openAudio();
    nextSave_ = 0.0;
}

void App::selectAudio(const std::string& name) {
    if (name == preferredAudio_ && audioRunning()) return;
    preferredAudio_ = name;
    openAudio();
}

// ------------------------------------------------------------- keyboards --

void App::handleKeyboard(const std::vector<unsigned char>& m, int route) {
    if (m.size() < 2) return;
    const int status = m[0] & 0xF0, ch = m[0] & 0x0F;

    // Controllers for the lead synth: sustain pedal, mod wheel, pitch bend, all-notes-off.
    if (route == ROUTE_LEAD && m.size() >= 3) {
        if (status == 0xB0) {
            if (m[1] == 64) {
                sustainDown_ = m[2] >= 64;
                sh_->send(Cmd::LeadSustain, sustainDown_ ? 1 : 0);
            } else if (m[1] == 1) {
                sh_->send(Cmd::LeadMod, 0, 0, m[2] / 127.f);
            } else if (m[1] == 120 || m[1] == 123) {
                releaseLeadNotes();
            }
            return;
        }
        if (status == 0xE0) {
            const int v = (m[1] | (m[2] << 7)) - 8192;
            sh_->send(Cmd::LeadBend, 0, 0, v / 8192.f * 2.f);  // +/- 2 semitones
            return;
        }
    }
    if (m.size() < 3) return;
    const int note = m[1];
    const bool on = status == 0x90 && m[2] > 0;
    const bool off = status == 0x80 || (status == 0x90 && m[2] == 0);
    if (!on && !off) return;

    if (route == ROUTE_LEAD && ch != 9) {
        leadNote(note, on, m[2]);
        return;
    }

    if (ch == 9 || route == ROUTE_DRUMS) {  // drum channel / drum route -> this style's drum pads
        if (!on) return;
        int slot;
        switch (note) {
            case 35: case 36: slot = 0; break;
            case 38: case 40: slot = 1; break;
            case 42: case 44: slot = 2; break;
            case 46: slot = 3; break;
            case 39: case 51: case 53: case 59: slot = 4; break;
            case 37: case 41: case 43: case 45: case 47: case 48: case 50: slot = 5; break;
            case 49: case 52: case 55: case 56: case 57: slot = kExtraSlot; break;
            default: slot = (ch == 9 && route != ROUTE_DRUMS) ? kChordSlot : note % 8; break;
        }
        sh_->send(Cmd::Trigger, slot);
        return;
    }

    // Anything else plays the bass, relative to the style's key, folded into range.
    const Style& st = styleOf(sh_->pattern.load());
    int semi = note - st.root;
    while (semi < -12) semi += 12;
    while (semi > 36) semi -= 12;
    // Remember what each key sent, so its note-off matches even if the style changed.
    for (size_t i = 0; i < kbNotes_.size(); ++i) {
        if (kbNotes_[i].first == note) {
            sh_->send(Cmd::BassOff, kbNotes_[i].second);
            kbNotes_.erase(kbNotes_.begin() + static_cast<long>(i));
            break;
        }
    }
    if (on) {
        kbNotes_.push_back({note, semi});
        sh_->send(Cmd::BassOn, semi);
    }
}

void App::releaseKeyboardNotes() {
    for (const auto& n : kbNotes_) sh_->send(Cmd::BassOff, n.second);
    kbNotes_.clear();
    releaseLeadNotes();
}

// -------------------------------------------------------------- lead synth --

void App::leadNote(int note, bool on, int velocity) {
    // Note-off sends whatever this key turned on, even if octave/snap changed since.
    for (size_t i = 0; i < leadNotes_.size(); ++i) {
        if (leadNotes_[i].first == note) {
            sh_->send(Cmd::LeadOff, leadNotes_[i].second);
            leadNotes_.erase(leadNotes_.begin() + static_cast<long>(i));
            break;
        }
    }
    if (!on) return;
    int n = note + 12 * leadOctave_;
    if (leadSnap_) {  // move to the nearest note in the style's key (ties go down)
        const Style& st = styleOf(sh_->pattern.load());
        const int pc = (((n - st.root) % 12) + 12) % 12;
        int best = 0, bestDist = 99;
        for (int s : st.scale)
            for (int cand : {s, s - 12, s + 12}) {
                const int d = cand - pc;
                if (std::abs(d) < bestDist || (std::abs(d) == bestDist && d < best)) { bestDist = std::abs(d); best = d; }
            }
        n += best;
    }
    n = std::max(0, std::min(127, n));
    leadNotes_.push_back({note, n});
    sh_->send(Cmd::LeadOn, n, velocity);
}

void App::leadKey(int note, bool down, int velocity) { leadNote(note, down, velocity); }

bool App::leadKeyHeld(int note) const {
    for (const auto& n : leadNotes_)
        if (n.first == note) return true;
    return false;
}

void App::releaseLeadNotes() {
    for (const auto& n : leadNotes_) sh_->send(Cmd::LeadOff, n.second);
    leadNotes_.clear();
}

void App::setLeadPatch(int index) {
    const int n = static_cast<int>(leadPatches().size());
    if (index < 0 || index >= n) return;
    releaseLeadNotes();
    leadPatch_ = index;
    sh_->send(Cmd::LeadPatch, index);
    logf("Lead sound: %s", leadPatches()[static_cast<size_t>(index)].name.c_str());
}

// ----------------------------------------------------------- pattern bank --

int App::newPattern(const std::string& name, bool copyCurrent) {
    const int cur = sh_->pattern.load();
    const int style = cur / kMaxSlots;
    const int slot = sh_->slotCount[style].load();
    if (slot >= kMaxSlots) return -1;
    const int p = style * kMaxSlots + slot;
    if (copyCurrent) {
        copyPattern(*sh_, cur, p);
    } else {
        clearPattern(*sh_, p);
        for (int s = 0; s < kSteps; s += 4) sh_->on[p][0][s].store(STEP_ON);  // start from a simple kick
    }
    sh_->patternName[p] = name.empty() ? "My pattern" : name;
    sh_->slotCount[style].store(slot + 1);
    sh_->send(Cmd::QueuePattern, p);
    logf("New pattern: %s (%s)", sh_->patternName[p].c_str(), styleOf(p).name.c_str());
    nextSave_ = 0.0;
    return p;
}

void App::renamePattern(int p, const std::string& name) {
    if (p < 0 || p >= kMaxPatterns || p % kMaxSlots < kFactorySlots || name.empty()) return;
    sh_->patternName[p] = name;
    nextSave_ = 0.0;
}

bool App::deletePattern(int p) {
    const int style = p / kMaxSlots, slot = p % kMaxSlots;
    const int count = sh_->slotCount[style].load();
    if (slot < kFactorySlots || slot >= count) return false;
    const std::string gone = sh_->patternName[p];
    const int cur = sh_->pattern.load();
    // Shift the later patterns down one.
    for (int k = slot; k < count - 1; ++k) {
        copyPattern(*sh_, style * kMaxSlots + k + 1, style * kMaxSlots + k);
        sh_->patternName[style * kMaxSlots + k] = sh_->patternName[style * kMaxSlots + k + 1];
    }
    sh_->slotCount[style].store(count - 1);
    if (cur / kMaxSlots == style) {
        const int cs = cur % kMaxSlots;
        if (cs == slot) sh_->send(Cmd::JumpPattern, style * kMaxSlots + std::min(slot, count - 2));
        else if (cs > slot) sh_->send(Cmd::JumpPattern, cur - 1);
    }
    logf("Deleted pattern: %s", gone.c_str());
    nextSave_ = 0.0;
    return true;
}

// ----------------------------------------------------------- sampled drums --

bool App::setDrumSample(int style, int slot, const std::string& path, std::string* err) {
    if (style < 0 || style >= kMaxStyles || slot < 0 || slot >= kDrumSlots) return false;
    std::shared_ptr<Sample> smp;
    if (!path.empty()) {
        std::string e;
        smp = loadWav(path, static_cast<float>(kSampleRate), e);
        if (!smp) {
            logf("Couldn't load %s: %s", path.c_str(), e.c_str());
            if (err) *err = e;
            return false;
        }
    }
    sh_->send(Cmd::SetSample, style, slot, 0.f, smp.get());
    // The engine may still be playing the old one: keep it alive a little while.
    if (samples_[style][slot]) retired_.push_back({monotonicSec() + 2.0, samples_[style][slot]});
    samples_[style][slot] = smp;
    const DrumDef& d = styles()[static_cast<size_t>(style) % styles().size()].drums[static_cast<size_t>(slot)];
    if (smp) logf("%s / %s: sample %s", styles()[static_cast<size_t>(style)].name.c_str(), d.name.c_str(), smp->name.c_str());
    else logf("%s / %s: back to the synth sound", styles()[static_cast<size_t>(style)].name.c_str(), d.name.c_str());
    nextSave_ = 0.0;
    return true;
}

std::string App::drumSample(int style, int slot) const {
    if (style < 0 || style >= kMaxStyles || slot < 0 || slot >= kDrumSlots || !samples_[style][slot]) return "";
    return samples_[style][slot]->path;
}

int App::loadKitFolder(int style, const std::string& folder) {
    if (style < 0 || style >= styleCount()) return 0;
    const auto files = matchKitFolder(folder, styles()[static_cast<size_t>(style)]);
    int n = 0;
    for (int slot = 0; slot < kDrumSlots && slot < static_cast<int>(files.size()); ++slot)
        if (!files[static_cast<size_t>(slot)].empty() && setDrumSample(style, slot, files[static_cast<size_t>(slot)])) ++n;
    logf("Kit folder %s: %d of %d sounds matched", folder.c_str(), n, kDrumSlots);
    return n;
}

// ------------------------------------------------------- record / export --

std::string App::outputFolder() const { return outputDir(); }

bool App::startRecording() {
    if (recording_) return true;
    const int p = sh_->pattern.load();
    const std::string path = (std::filesystem::u8path(outputDir()) /
                              std::filesystem::u8path(safeFileName("LP-DNB " + styleOf(p).name + " " + timeStamp() + ".wav")))
                                 .u8string();
    rec_ = std::make_unique<Recorder>(*sh_);
    if (!rec_->start(path)) {
        logf("Couldn't create %s", path.c_str());
        rec_.reset();
        return false;
    }
    recording_ = true;
    lastFile_ = path;
    logf("Recording to %s", path.c_str());
    return true;
}

void App::stopRecording() {
    if (!recording_) return;
    rec_->finish();
    logf("Saved %s (%.1f s)", lastFile_.c_str(), rec_->frames.load() / static_cast<double>(kSampleRate));
    rec_.reset();
    recording_ = false;
}

double App::recordingSeconds() const {
    return rec_ ? rec_->frames.load() / static_cast<double>(kSampleRate) : 0.0;
}

namespace {

int gmDrumNote(const DrumDef& d, int slot) {
    switch (d.type) {
        case DrumType::Kick: return 36;
        case DrumType::Snare: return slot == 2 ? 40 : 38;
        case DrumType::Clap: return 39;
        case DrumType::Rim: return 37;
        case DrumType::Shaker: return 70;
        case DrumType::Cowbell: return 56;
        case DrumType::Metal:
            if (d.p[4] < 2.5f) return 49;            // crash
            if (d.p[4] < 5.f) return 51;             // ride
            if (d.name.find("Tamb") != std::string::npos) return 54;
            return d.p[4] > 30.f ? 42 : 46;          // closed / open hat
        case DrumType::Tom:
            if (d.name.find("Conga") != std::string::npos || d.name.find("Bongo") != std::string::npos)
                return d.p[0] > 280.f ? 63 : 64;
            return d.p[0] > 300.f ? 50 : (d.p[0] > 180.f ? 47 : 45);
    }
    return 36;
}

struct MidiEv { uint32_t tick; int order; unsigned char b[3]; };

void putVar(std::string& o, uint32_t v) {
    unsigned char buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7)) buf[n++] = static_cast<unsigned char>((v & 0x7F) | 0x80);
    while (n) o += static_cast<char>(buf[--n]);
}
void put32(std::string& o, uint32_t v) { for (int i = 3; i >= 0; --i) o += static_cast<char>((v >> (8 * i)) & 0xFF); }
void put16(std::string& o, uint32_t v) { o += static_cast<char>((v >> 8) & 0xFF); o += static_cast<char>(v & 0xFF); }

std::string trackChunk(const std::string& name, std::vector<MidiEv> ev, uint32_t endTick, const std::string& prefix = "") {
    std::sort(ev.begin(), ev.end(), [](const MidiEv& a, const MidiEv& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
    std::string d;
    putVar(d, 0);
    d += "\xFF\x03";
    putVar(d, static_cast<uint32_t>(name.size()));
    d += name;
    d += prefix;
    uint32_t last = 0;
    for (const MidiEv& e : ev) {
        putVar(d, e.tick - last);
        last = e.tick;
        d.append(reinterpret_cast<const char*>(e.b), 3);
    }
    putVar(d, endTick > last ? endTick - last : 0);  // ends exactly on the bar line, so it loops
    d += std::string("\xFF\x2F\x00", 3);
    std::string chunk = "MTrk";
    put32(chunk, static_cast<uint32_t>(d.size()));
    return chunk + d;
}

}  // namespace

std::string App::exportMidi() {
    const int p = sh_->pattern.load();
    const Style& st = styleOf(p);
    const int len = sh_->patternLength(p);
    constexpr int ppq = 96, perStep = ppq / 4;
    // Swing moves the off-beat 16ths, as in the sequencer.
    auto tickOf = [&](int s) -> uint32_t {
        return static_cast<uint32_t>((s / 2) * 2 * perStep + ((s & 1) ? std::lround(2 * perStep * st.swing) : 0));
    };
    auto velOf = [](uint8_t v) { const int k = stepKind(v); return k == STEP_ACCENT ? 120 : (k == STEP_SOFT ? 50 : 96); };
    auto noteLen = [&](int t, int s) { int n = 1; while (s + n < len && stepKind(sh_->on[p][t][s + n].load()) == STEP_TIE) ++n; return n; };
    auto add = [](std::vector<MidiEv>& v, uint32_t tick, int ch, int note, int vel, uint32_t dur) {
        note = std::max(0, std::min(127, note));
        v.push_back({tick, 1, {static_cast<unsigned char>(0x90 | ch), static_cast<unsigned char>(note), static_cast<unsigned char>(vel)}});
        v.push_back({tick + std::max<uint32_t>(1, dur), 0, {static_cast<unsigned char>(0x80 | ch), static_cast<unsigned char>(note), 0}});
    };

    const uint32_t endTick = static_cast<uint32_t>(len * perStep);
    std::vector<MidiEv> drums, bass, chords, lead;
    for (int t = 0; t < kDrumTracks; ++t) {
        if (sh_->mute[t].load()) continue;
        const int note = gmDrumNote(st.drums[static_cast<size_t>(t)], t);
        for (int s = 0; s < len; ++s) {
            const uint8_t v = sh_->on[p][t][s].load();
            if (!stepSounds(v)) continue;
            const int k = stepKind(v);
            const int hitsN = k == STEP_ROLL2 ? 2 : (k == STEP_ROLL3 ? 3 : 1);
            const uint32_t t0 = tickOf(s), span = tickOf(s + 1) - t0;
            for (int r = 0; r < hitsN; ++r) add(drums, t0 + span * r / hitsN, 9, note, velOf(v), span / hitsN / 2);
        }
    }
    for (int s = 0; s < len; ++s) {
        uint8_t v = sh_->on[p][T_BASS][s].load();
        if (stepSounds(v) && !sh_->mute[T_BASS].load()) {
            const int semis = degreeSemis(st, sh_->note[p][T_BASS][s].load()) + sh_->lock[p][T_BASS][s][LOCK_PITCH].load();
            const int n = noteLen(T_BASS, s);
            add(bass, tickOf(s), 0, st.root + semis + 12 * st.bass.oscOctave, velOf(v), tickOf(s + n) - tickOf(s) - 2);
        }
        v = sh_->on[p][T_CHORD][s].load();
        if (stepSounds(v) && !sh_->mute[T_CHORD].load()) {
            const int deg = sh_->note[p][T_CHORD][s].load(), n = noteLen(T_CHORD, s);
            const int lockP = sh_->lock[p][T_CHORD][s][LOCK_PITCH].load();
            for (int i = 0; i < std::clamp(st.chord.notes, 1, 5); ++i)
                add(chords, tickOf(s), 1, st.root + st.chord.octave + degreeSemis(st, deg + 2 * i) + lockP, velOf(v),
                    tickOf(s + n) - tickOf(s) - 2);
        }
        v = sh_->on[p][T_LEAD][s].load();
        if (stepSounds(v) && !sh_->mute[T_LEAD].load()) {
            const int n = noteLen(T_LEAD, s);
            add(lead, tickOf(s), 2, sh_->note[p][T_LEAD][s].load() + sh_->lock[p][T_LEAD][s][LOCK_PITCH].load(), 100,
                tickOf(s + n) - tickOf(s) - 2);
        }
    }

    // Tempo track: tempo, 4/4, and the pattern name.
    std::string tempo;
    const uint32_t uspq = static_cast<uint32_t>(std::lround(60000000.0 / std::max(40.f, sh_->bpm.load())));
    putVar(tempo, 0);
    tempo += "\xFF\x51\x03";
    tempo += static_cast<char>((uspq >> 16) & 0xFF);
    tempo += static_cast<char>((uspq >> 8) & 0xFF);
    tempo += static_cast<char>(uspq & 0xFF);
    putVar(tempo, 0);
    tempo += std::string("\xFF\x58\x04\x04\x02\x18\x08", 7);
    std::vector<std::string> chunks;
    chunks.push_back(trackChunk(st.name + " - " + sh_->patternName[p], {}, endTick, tempo));
    if (!drums.empty()) chunks.push_back(trackChunk("Drums", drums, endTick));
    if (!bass.empty()) chunks.push_back(trackChunk(st.bass.name, bass, endTick));
    if (!chords.empty()) chunks.push_back(trackChunk(st.chord.name, chords, endTick));
    if (!lead.empty()) chunks.push_back(trackChunk("Lead", lead, endTick));

    std::string file = "MThd";
    put32(file, 6);
    put16(file, 1);
    put16(file, static_cast<uint32_t>(chunks.size()));
    put16(file, ppq);
    for (const auto& c : chunks) file += c;

    const std::string path = (std::filesystem::u8path(outputDir()) /
                              std::filesystem::u8path(safeFileName("LP-DNB " + st.name + " - " + sh_->patternName[p] + " " + timeStamp() + ".mid")))
                                 .u8string();
    std::ofstream o(std::filesystem::u8path(path), std::ios::binary);
    o << file;
    if (!o) {
        logf("Couldn't write %s", path.c_str());
        return "";
    }
    lastFile_ = path;
    logf("Exported MIDI: %s", path.c_str());
    return path;
}

// -------------------------------------------------------------- MIDI clock --

void App::setClockOut(const std::string& port) {
    clock_->open(port);
    nextSave_ = 0.0;
}

// ------------------------------------------------------------ save / load --

Settings App::currentSettings() const {
    Settings st;
    const int p = sh_->pattern.load();
    st.styleName = styleOf(p).name;
    st.patternName = sh_->patternName[p];
    st.bpm = sh_->bpm.load();
    st.volume = sh_->volume.load();
    st.audioDevice = preferredAudio_;
    st.leadPatch = leadPatches()[static_cast<size_t>(leadPatch_)].name;
    st.leadVolume = sh_->leadVolume.load();
    st.leadOctave = leadOctave_;
    st.leadSnap = leadSnap_;
    st.routes = midi_->routes();
    st.disabledInputs = midi_->disabledInputs();
    st.audioBuffer = audioBuffer_;
    st.audioExclusive = audioExclusive_;
    st.clockOut = clock_->port();
    st.clockIn = midi_->clockIn();
    for (int si = 0; si < styleCount() && si < kMaxStyles; ++si)
        for (int k = 0; k < kDrumSlots; ++k)
            if (samples_[si][k]) st.samples[styles()[static_cast<size_t>(si)].name][k] = samples_[si][k]->path;
    return st;
}

void App::loadSaved() {
    Settings st;
    std::string msg;
    const bool found = loadData(*sh_, st, msg);
    logf("%s", msg.c_str());
    if (!found) return;
    // What was playing last time.
    for (int si = 0; si < styleCount(); ++si) {
        if (styles()[static_cast<size_t>(si)].name != st.styleName) continue;
        int p = si * kMaxSlots;
        for (int k = 0; k < sh_->slotCount[si].load(); ++k)
            if (sh_->patternName[si * kMaxSlots + k] == st.patternName) { p = si * kMaxSlots + k; break; }
        sh_->pattern.store(p);
        sh_->bpm.store(styles()[static_cast<size_t>(si)].bpm);
        break;
    }
    if (st.bpm >= 40.f && st.bpm <= 220.f) sh_->bpm.store(st.bpm);
    sh_->volume.store(std::max(0.f, std::min(1.f, st.volume)));
    sh_->leadVolume.store(std::max(0.f, std::min(1.f, st.leadVolume)));
    preferredAudio_ = st.audioDevice;
    for (size_t i = 0; i < leadPatches().size(); ++i)
        if (leadPatches()[i].name == st.leadPatch) leadPatch_ = static_cast<int>(i);
    leadOctave_ = std::max(-3, std::min(3, st.leadOctave));
    leadSnap_ = st.leadSnap;
    midi_->restore(st.routes, st.disabledInputs);
    audioBuffer_ = std::max(32, std::min(2048, st.audioBuffer));
    audioExclusive_ = st.audioExclusive;
    midi_->setClockIn(st.clockIn);
    if (!st.clockOut.empty()) clock_->open(st.clockOut);
    for (const auto& kv : st.samples) {
        int si = -1;
        for (int i = 0; i < styleCount(); ++i)
            if (styles()[static_cast<size_t>(i)].name == kv.first) si = i;
        if (si < 0 || si >= kMaxStyles) continue;
        for (const auto& sl : kv.second) setDrumSample(si, sl.first, sl.second);
    }
    lastSaved_ = serialize(*sh_, currentSettings());
}

void App::saveNow() {
    const std::string text = serialize(*sh_, currentSettings());
    if (text == lastSaved_) return;
    std::string err;
    if (writeData(text, err)) {
        lastSaved_ = text;
    } else {
        static std::string lastErr;
        if (err != lastErr) logf("Couldn't save: %s", err.c_str());
        lastErr = err;
    }
}

// ------------------------------------------------------------------- tick --

void App::virtualPress(int x, int y, bool down) {
    std::vector<unsigned char> m;
    if (y < 0) m = {0xB0, static_cast<unsigned char>(104 + x), static_cast<unsigned char>(down ? 127 : 0)};
    else m = {0x90, static_cast<unsigned char>(y * 16 + x), static_cast<unsigned char>(down ? 127 : 0)};
    ctl_->onMidi(m, monotonicSec());
}

void App::tick(double now) {
    for (auto& m : midi_->takeMessages()) {
        if (m.source == MidiMsg::LAUNCHPAD) ctl_->onMidi(m.bytes, now);
        else if (m.source == MidiMsg::CLOCK) {
            const unsigned char b = m.bytes.empty() ? 0 : m.bytes[0];
            if (b == 0xFA || (b == 0xFB && !sh_->playing.load())) sh_->send(Cmd::Play);
            else if (b == 0xFC && sh_->playing.load()) sh_->send(Cmd::Stop);
        } else handleKeyboard(m.bytes, m.route);
    }
    // Follow the incoming clock's tempo (smoothed, and only real changes).
    clockBpm_ = midi_->clockInBpm(now);
    if (clockBpm_ >= 40.f && clockBpm_ <= 250.f) {
        const float cur = sh_->bpm.load();
        const float want = std::round(clockBpm_ * 10.f) / 10.f;
        if (std::fabs(want - cur) >= 0.15f) sh_->bpm.store(want);
    }
    for (size_t i = 0; i < retired_.size();) {
        if (now >= retired_[i].first) retired_.erase(retired_.begin() + static_cast<long>(i));
        else ++i;
    }
    if (midi_->poll(now)) ctl_->releaseAll(now);

    size_t open = 0;
    for (const auto& k : midi_->keyboards()) open += k.open ? 1 : 0;
    if (open < kbOpenCount_) releaseKeyboardNotes();
    kbOpenCount_ = open;

    // Autosave: patterns and settings are written whenever they change (checked every 2 s).
    if (now >= nextSave_) {
        nextSave_ = now + 2.0;
        saveNow();
    }

    ctl_->update(now);
    if (now >= nextLed_) {
        ctl_->render(now, frame_);
        midi_->pushFrame(frame_);
        nextLed_ += 1.0 / 30.0;
        if (nextLed_ < now) nextLed_ = now + 1.0 / 30.0;
    }

    if (!audio_->ctxOk) return;
    if (audio_->lost.load()) {
        logf("Audio output lost%s", preferredAudio_.empty() ? ", reopening" : ", switching to system default");
        refreshAudioDevices();
        openAudio();
        nextAudioScan_ = now + 3.0;
    } else if (now >= nextAudioScan_) {
        nextAudioScan_ = now + 3.0;
        const bool changed = refreshAudioDevices();
        // Preferred device came back (or default was in use while it was away).
        const bool prefPresent = std::find(audioNames_.begin(), audioNames_.end(), preferredAudio_) != audioNames_.end();
        if (!preferredAudio_.empty() && prefPresent && openedAudio_ != preferredAudio_) openAudio();
        else if (!audioRunning() && changed) openAudio();  // something was plugged in: try again
    }
}

}  // namespace dnb
