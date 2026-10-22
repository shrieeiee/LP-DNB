// Ties everything together: audio output, MIDI devices, the controller.
// Used by both the GUI and the console front-ends. All methods are called
// from the main/UI thread.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "controller.h"
#include "launchpad.h"
#include "storage.h"

namespace dnb {

class Engine;

class App {
public:
    explicit App(bool nullAudio = false);
    ~App();

    void tick(double now);

    Shared& shared() { return *sh_; }
    Controller& controller() { return *ctl_; }
    MidiHub& midi() { return *midi_; }
    const Frame& frame() const { return frame_; }

    // Virtual Launchpad: x 0..8 (8 = right column), y 0..7, or y = -1 for the
    // top row of round buttons. Behaves exactly like the hardware.
    void virtualPress(int x, int y, bool down);

    // Audio output devices (refreshed every few seconds).
    const std::vector<std::string>& audioDevices() const { return audioNames_; }
    const std::string& preferredAudio() const { return preferredAudio_; }  // "" = system default
    void selectAudio(const std::string& name);                               // "" = system default
    const std::string& audioStatus() const { return audioStatus_; }
    bool audioRunning() const;

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
    double nextAudioScan_ = 0.0;

    std::vector<std::string> audioNames_;
    std::string preferredAudio_, openedAudio_, audioStatus_;

    std::vector<std::pair<int, int>> kbNotes_;  // (MIDI note, semitone sent) held on keyboards
    size_t kbOpenCount_ = 0;

    int leadPatch_ = 0, leadOctave_ = 0;
    bool leadSnap_ = false;
    std::vector<std::pair<int, int>> leadNotes_;  // (input note, note sent) held on the lead
    bool sustainDown_ = false;

    std::string lastSaved_;
    double nextSave_ = 0.0;
};

}  // namespace dnb
