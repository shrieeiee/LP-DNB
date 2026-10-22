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
#include <cstring>

#include "engine.h"
#include "log.h"
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

App::App(bool nullAudio)
    : sh_(std::make_unique<Shared>()), audio_(std::make_unique<Audio>()) {
    loadAllPresets(*sh_);  // also builds the style table before audio starts
    sh_->bpm.store(styles()[0].bpm);
    engine_ = std::make_unique<Engine>(*sh_, static_cast<float>(kSampleRate));
    ctl_ = std::make_unique<Controller>(*sh_);
    midi_ = std::make_unique<MidiHub>();
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
    cfg.periodSizeInFrames = 256;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = &Audio::dataCb;
    cfg.notificationCallback = &Audio::notifyCb;
    cfg.pUserData = audio_.get();

    if (ma_device_init(&audio_->ctx, &cfg, &audio_->dev) != MA_SUCCESS) {
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
    logf("Audio output: %s", audioStatus_.c_str());
}

void App::selectAudio(const std::string& name) {
    if (name == preferredAudio_ && audioRunning()) return;
    preferredAudio_ = name;
    openAudio();
}

// ------------------------------------------------------------- keyboards --

void App::handleKeyboard(const std::vector<unsigned char>& m) {
    if (m.size() < 3) return;
    const int status = m[0] & 0xF0, ch = m[0] & 0x0F, note = m[1];
    const bool on = status == 0x90 && m[2] > 0;
    const bool off = status == 0x80 || (status == 0x90 && m[2] == 0);
    if (!on && !off) return;

    if (ch == 9) {  // General MIDI drum channel -> this style's drum pads
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
            default: slot = kChordSlot; break;
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
        else handleKeyboard(m.bytes);
    }
    if (midi_->poll(now)) ctl_->releaseAll(now);

    size_t open = 0;
    for (const auto& k : midi_->keyboards()) open += k.open ? 1 : 0;
    if (open < kbOpenCount_) releaseKeyboardNotes();
    kbOpenCount_ = open;

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
