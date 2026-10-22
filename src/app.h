// Ties everything together: audio output, MIDI devices, the controller.
// Used by both the GUI and the console front-ends. All methods are called
// from the main/UI thread.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <vector>

#include "clock.h"
#include "common.h"
#include "controller.h"
#include "launchpad.h"
#include "storage.h"

namespace dnb {

class Engine;
struct Sample;

// Something other than the groovebox controller that drives the Launchpad
// (lp-tenori's sequencer). While one is set, App sends it the Launchpad's
// buttons and MIDI clock Start/Stop, and shows its lights.
class Surface {
public:
    virtual ~Surface() = default;
    virtual void onLaunchpad(const std::vector<unsigned char>& m, double now) = 0;
    virtual void render(double now, Frame& out) = 0;
    virtual void update(double /*now*/) {}
    virtual void releaseAll(double /*now*/) {}
    virtual void onClock(bool /*start*/) {}  // following another device's MIDI clock
};

class App {
public:
    explicit App(bool nullAudio = false);
    ~App();

    void tick(double now);

    // Hand the Launchpad to another surface (nullptr = back to the groovebox).
    void setSurface(Surface* s) { surface_ = s; }
    // Off: don't write the groovebox's save file (style, tempo... changed by another surface).
    void setAutosave(bool on) { autosave_ = on; }

    Shared& shared() { return *sh_; }
    Controller& controller() { return *ctl_; }
    MidiHub& midi() { return *midi_; }
    const Frame& frame() const { return frame_; }

    // Virtual Launchpad: x 0..8 (8 = right column), y 0..7, or y = -1 for the
    // top row of round buttons. Behaves exactly like the hardware.
    void virtualPress(int x, int y, bool down);

    // Audio output devices (refreshed every few seconds).
    std::vector<std::string> audioDevices() const;
    std::string preferredAudio() const;  // "" = system default
    void selectAudio(const std::string& name);                               // "" = system default
    std::string audioStatus() const;
    bool audioRunning() const;
    // Lower buffer = less delay between pressing a pad and hearing it (more CPU risk).
    int audioBuffer() const { std::lock_guard<std::mutex> lk(audioMu_); return audioBuffer_; }
    void setAudioBuffer(int frames);
    bool audioExclusive() const { std::lock_guard<std::mutex> lk(audioMu_); return audioExclusive_; }  // WASAPI exclusive
    void setAudioExclusive(bool on);
    double audioLatencyMs() const;

    // ---- Sampled drums: any drum sound can be a WAV instead of the synth.
    // path "" = back to the synth. Returns false (and sets err) if it couldn't load.
    bool setDrumSample(int style, int slot, const std::string& path, std::string* err = nullptr);
    std::string drumSample(int style, int slot) const;
    // Picks files from a folder by name (kick, snare, hat...). Returns how many were used.
    int loadKitFolder(int style, const std::string& folder);

    // ---- Recording and export (files go in outputFolder()).
    std::string outputFolder() const;
    bool startRecording();
    void stopRecording();
    bool recording() const { return recording_; }
    double recordingSeconds() const;
    const std::string& lastFile() const { return lastFile_; }
    std::string exportMidi();  // the playing pattern as a .mid file; returns its path ("" on failure)

    // ---- MIDI clock.
    void setClockOut(const std::string& port);
    const std::string& clockOutPort() const { return clock_->port(); }
    bool clockOutOpen() const { return clock_->isOpen(); }
    void setClockIn(const std::string& port) { midi_->setClockIn(port); nextSave_ = 0.0; }
    float clockInBpm() const { return clockBpm_; }

    // ---- Pattern bank (your own patterns, saved between sessions).
    // Returns the new pattern index, or -1 if the style already has kMaxSlots patterns.
    int newPattern(const std::string& name, bool copyCurrent);
    void renamePattern(int pattern, const std::string& name);
    bool deletePattern(int pattern);  // your own patterns only
    std::string dataPath() const { return dataFilePath(); }
    void saveNow();

    // ---- Lead synth (second MIDI instrument).
    int leadPatch() const { return leadPatch_; }
    void setLeadPatch(int index);
    int leadOctave() const { return leadOctave_; }
    void setLeadOctave(int o) { releaseLeadNotes(); leadOctave_ = o < -3 ? -3 : (o > 3 ? 3 : o); }
    bool leadSnap() const { return leadSnap_; }
    void setLeadSnap(bool on) { releaseLeadNotes(); leadSnap_ = on; }
    void leadKey(int note, bool down, int velocity = 100);  // on-screen keyboard
    bool leadKeyHeld(int note) const;                     // for drawing keyboards (input notes)

private:
    struct Audio;
    void audioLoop(bool nullAudio);  // the audio thread
    bool refreshAudioDevices();  // true if the list changed
    void openAudio();
    void handleKeyboard(const std::vector<unsigned char>& m, int route);
    void releaseKeyboardNotes();
    void releaseLeadNotes();
    void leadNote(int note, bool on, int velocity);
    void loadSaved();
    Settings currentSettings() const;

    std::unique_ptr<Shared> sh_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Controller> ctl_;
    std::unique_ptr<MidiHub> midi_;
    std::unique_ptr<Audio> audio_;
    Frame frame_{};
    double nextLed_ = 0.0;

    // Audio device state: written by the audio thread, guarded by audioMu_.
    mutable std::mutex audioMu_;
    std::thread audioThread_;
    std::atomic<bool> audioQuit_{false}, audioReopen_{false}, audioRunning_{false};
    std::vector<std::string> audioNames_;
    std::string preferredAudio_, openedAudio_, audioStatus_ = "Starting audio...";

    std::vector<std::pair<int, int>> kbNotes_;  // (MIDI note, semitone sent) held on keyboards
    size_t kbOpenCount_ = 0;

    int leadPatch_ = 0, leadOctave_ = 0;
    bool leadSnap_ = false;
    std::vector<std::pair<int, int>> leadNotes_;  // (input note, note sent) held on the lead
    bool sustainDown_ = false;

    std::string lastSaved_;
    double nextSave_ = 0.0;

    int audioBuffer_ = 256;
    bool audioExclusive_ = false;
    double latencyMs_ = 0.0;

    std::shared_ptr<Sample> samples_[kMaxStyles][kDrumSlots];
    std::vector<std::pair<double, std::shared_ptr<Sample>>> retired_;  // freed once the engine let go

    struct Recorder;
    std::unique_ptr<Recorder> rec_;
    bool recording_ = false;
    std::string lastFile_;

    std::unique_ptr<MidiClockOut> clock_;
    float clockBpm_ = 0.f;

    Surface* surface_ = nullptr;
    bool autosave_ = true;
};

}  // namespace dnb
