#include "storage.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "styles.h"

namespace fs = std::filesystem;

namespace dnb {
namespace {

fs::path dataDir() {
#ifdef _WIN32
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) return fs::path(appdata) / L"LP-DNB";
    return fs::current_path() / "LP-DNB";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / "Library" / "Application Support" / "LP-DNB";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) return fs::path(xdg) / "lp-dnb";
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".config" / "lp-dnb";
#endif
}

fs::path dataFile() {
    if (const char* over = std::getenv("LPDNB_DATA")) return fs::path(over);  // used by tests
    return dataDir() / "lp-dnb-data.txt";
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

int styleIndex(const std::string& name) {
    for (int i = 0; i < styleCount(); ++i)
        if (styles()[static_cast<size_t>(i)].name == name) return i;
    return -1;
}

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

}  // namespace

std::string dataFilePath() { return dataFile().u8string(); }

std::string outputDir() {
    fs::path dir;
    if (const char* over = std::getenv("LPDNB_OUT")) {
        dir = fs::u8path(over);
    } else {
#ifdef _WIN32
        if (const wchar_t* home = _wgetenv(L"USERPROFILE")) dir = fs::path(home) / L"Music" / L"LP-DNB";
#else
        if (const char* home = std::getenv("HOME")) dir = fs::path(home) / "Music" / "LP-DNB";
#endif
        if (dir.empty()) dir = dataDir() / "Recordings";
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        dir = dataDir() / "Recordings";
        fs::create_directories(dir, ec);
    }
    return dir.u8string();
}

std::string timeStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H-%M-%S", &tm);
    return buf;
}

std::string safeFileName(const std::string& s) {
    std::string o;
    for (char c : s) o += (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) ? '-' : c;
    return o;
}

std::string serialize(const Shared& sh, const Settings& st) {
    std::ostringstream o;
    o << "# LP-DNB saved data (patterns and settings). It's saved automatically.\n"
         "# Patterns use the same text as src/styles.cpp: drums  . x X o r t   bass/chords  . - 0-9 A-J a-j\n\n";
    o << "[settings]\n";
    o << "style=" << oneLine(st.styleName) << "\n";
    o << "pattern=" << oneLine(st.patternName) << "\n";
    o << "bpm=" << st.bpm << "\n";
    o << "volume=" << st.volume << "\n";
    o << "audio=" << oneLine(st.audioDevice) << "\n";
    o << "lead_patch=" << oneLine(st.leadPatch) << "\n";
    o << "lead_volume=" << st.leadVolume << "\n";
    o << "lead_octave=" << st.leadOctave << "\n";
    o << "lead_snap=" << (st.leadSnap ? 1 : 0) << "\n";
    for (const auto& r : st.routes) o << "route=" << r.second << "|" << oneLine(r.first) << "\n";
    for (const auto& d : st.disabledInputs) o << "disabled=" << oneLine(d) << "\n";
    o << "audio_buffer=" << st.audioBuffer << "\n";
    o << "audio_exclusive=" << (st.audioExclusive ? 1 : 0) << "\n";
    o << "clock_out=" << oneLine(st.clockOut) << "\n";
    o << "clock_in=" << oneLine(st.clockIn) << "\n";
    o << "humanize=" << sh.humanize.load() << "\n";
    o << "hat_swing=" << sh.hatSwing.load() << "\n";
    o << "delay_time=" << sh.delayDiv.load() << "\n";
    o << "delay_feedback=" << sh.delayFeedback.load() << "\n";
    o << "reverb_size=" << sh.reverbSize.load() << "\n";
    o << "chorus=" << sh.chorus.load() << "\n";
    o << "width=" << sh.width.load() << "\n";
    o << "glue=" << sh.glue.load() << "\n";
    o << "sidechain=" << sh.sidechain.load() << "\n";
    o << "fill_every=" << sh.fillEvery.load() << "\n";

    // Song chain (slots of whichever style is playing).
    if (sh.songLen.load() > 0 || sh.songMode.load()) {
        o << "\n[song]\n";
        o << "on=" << (sh.songMode.load() ? 1 : 0) << "\n";
        for (int i = 0; i < sh.songLen.load() && i < kMaxSong; ++i)
            o << "entry=" << sh.songSlot[i].load() << "|" << sh.songRepeats[i].load() << "\n";
    }

    // Mixer and sound settings that differ from the defaults, and sampled drums.
    for (int si = 0; si < styleCount() && si < kMaxStyles; ++si) {
        std::ostringstream m;
        for (int c = 0; c < kChannels; ++c) {
            const ChannelMix& x = sh.mix[si][c];
            const float v[7] = {x.volume.load(), x.pan.load(), x.reverb.load(), x.delay.load(), x.tune.load(), x.decay.load(), x.tone.load()};
            if (v[0] == 1.f && v[1] == 0.f && v[2] == 0.f && v[3] == 0.f && v[4] == 0.f && v[5] == 0.f && v[6] == 0.f) continue;
            m << "ch=" << c;
            for (float f : v) m << "|" << f;
            m << "\n";
        }
        auto it = st.samples.find(styles()[static_cast<size_t>(si)].name);
        if (it != st.samples.end())
            for (const auto& kv : it->second)
                if (!kv.second.empty()) m << "sample=" << kv.first << "|" << oneLine(kv.second) << "\n";
        if (m.str().empty()) continue;
        o << "\n[mix]\nstyle=" << styles()[static_cast<size_t>(si)].name << "\n" << m.str();
    }

    for (int si = 0; si < styleCount() && si < kMaxStyles; ++si) {
        const int count = sh.slotCount[si].load();
        for (int slot = 0; slot < count; ++slot) {
            const int p = si * kMaxSlots + slot;
            const bool builtin = slot < kFactorySlots;
            if (builtin && factoryUnchanged(sh, p)) continue;  // only save what differs
            o << "\n[pattern]\n";
            o << "style=" << styles()[static_cast<size_t>(si)].name << "\n";
            o << "name=" << oneLine(sh.patternName[p]) << "\n";
            if (builtin) o << "builtin=" << slot << "\n";
            o << "length=" << sh.patternLength(p) << "\n";
            for (int t = 0; t < kTracks; ++t) o << "row=" << encodeRow(sh, p, t) << "\n";
            const std::string locks = encodeLocks(sh, p);
            if (!locks.empty()) o << "locks=" << locks << "\n";
        }
    }
    return o.str();
}

bool loadData(Shared& sh, Settings& st, std::string& message) {
    std::ifstream f(dataFile(), std::ios::binary);
    if (!f) {
        message = "No saved data yet; it will be created at " + dataFilePath();
        return false;
    }
    struct Rec { std::string style, name, locks; int builtin = -1, length = 0; std::vector<std::string> rows; };
    std::vector<Rec> recs;
    std::string mixStyle;
    bool songSeen = false;
    int songN = 0;
    std::string section, line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line;
            if (section == "[pattern]") recs.emplace_back();
            if (section == "[mix]") mixStyle.clear();
            if (section == "[song]" && !songSeen) { songSeen = true; songN = 0; }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), val = line.substr(eq + 1);
        if (section == "[settings]") {
            try {
                if (key == "style") st.styleName = val;
                else if (key == "pattern") st.patternName = val;
                else if (key == "bpm") st.bpm = std::stof(val);
                else if (key == "volume") st.volume = std::stof(val);
                else if (key == "audio") st.audioDevice = val;
                else if (key == "lead_patch") st.leadPatch = val;
                else if (key == "lead_volume") st.leadVolume = std::stof(val);
                else if (key == "lead_octave") st.leadOctave = std::stoi(val);
                else if (key == "lead_snap") st.leadSnap = val == "1";
                else if (key == "route") {
                    const size_t bar = val.find('|');
                    if (bar != std::string::npos) st.routes[val.substr(bar + 1)] = std::stoi(val.substr(0, bar));
                } else if (key == "disabled") st.disabledInputs.insert(val);
                else if (key == "audio_buffer") st.audioBuffer = std::stoi(val);
                else if (key == "audio_exclusive") st.audioExclusive = val == "1";
                else if (key == "clock_out") st.clockOut = val;
                else if (key == "clock_in") st.clockIn = val;
                else if (key == "humanize") sh.humanize.store(std::clamp(std::stof(val), 0.f, 1.f));
                else if (key == "hat_swing") sh.hatSwing.store(std::clamp(std::stof(val), 0.f, 1.f));
                else if (key == "delay_time") sh.delayDiv.store(std::clamp(std::stoi(val), 0, DLY_COUNT - 1));
                else if (key == "delay_feedback") sh.delayFeedback.store(std::clamp(std::stof(val), 0.f, 0.9f));
                else if (key == "reverb_size") sh.reverbSize.store(std::clamp(std::stof(val), 0.f, 1.f));
                else if (key == "chorus") sh.chorus.store(std::clamp(std::stof(val), 0.f, 1.f));
                else if (key == "width") sh.width.store(std::clamp(std::stof(val), 0.f, 1.5f));
                else if (key == "glue") sh.glue.store(std::clamp(std::stof(val), 0.f, 1.f));
                else if (key == "sidechain") sh.sidechain.store(std::clamp(std::stof(val), 0.f, 1.5f));
                else if (key == "fill_every") sh.fillEvery.store(std::clamp(std::stoi(val), 0, 16));
            } catch (...) {
            }
        } else if (section == "[song]") {
            try {
                if (key == "on") sh.songMode.store(val == "1");
                else if (key == "entry" && songN < kMaxSong) {
                    const size_t bar = val.find('|');
                    sh.songSlot[songN].store(std::clamp(std::stoi(val.substr(0, bar)), 0, kMaxSlots - 1));
                    sh.songRepeats[songN].store(bar == std::string::npos ? 1 : std::clamp(std::stoi(val.substr(bar + 1)), 1, 16));
                    sh.songLen.store(++songN);
                }
            } catch (...) {
            }
        } else if (section == "[mix]") {
            try {
                if (key == "style") mixStyle = val;
                const int si = styleIndex(mixStyle);
                if (si < 0 || si >= kMaxStyles) continue;
                if (key == "ch") {
                    float v[8] = {0, 1, 0, 0, 0, 0, 0, 0};
                    std::stringstream ss(val);
                    std::string tok;
                    int n = 0;
                    while (n < 8 && std::getline(ss, tok, '|')) v[n++] = std::stof(tok);
                    const int c = static_cast<int>(v[0]);
                    if (c < 0 || c >= kChannels || n < 8) continue;
                    ChannelMix& x = sh.mix[si][c];
                    x.volume.store(std::clamp(v[1], 0.f, 1.5f));
                    x.pan.store(std::clamp(v[2], -1.f, 1.f));
                    x.reverb.store(std::clamp(v[3], 0.f, 1.f));
                    x.delay.store(std::clamp(v[4], 0.f, 1.f));
                    x.tune.store(std::clamp(v[5], -12.f, 12.f));
                    x.decay.store(std::clamp(v[6], -1.f, 1.f));
                    x.tone.store(std::clamp(v[7], -1.f, 1.f));
                } else if (key == "sample") {
                    const size_t bar = val.find('|');
                    if (bar == std::string::npos) continue;
                    const int slot = std::stoi(val.substr(0, bar));
                    if (slot >= 0 && slot < kDrumSlots) st.samples[mixStyle][slot] = val.substr(bar + 1);
                }
            } catch (...) {
            }
        } else if (section == "[pattern]" && !recs.empty()) {
            Rec& r = recs.back();
            if (key == "style") r.style = val;
            else if (key == "name") r.name = val;
            else if (key == "builtin") { try { r.builtin = std::stoi(val); } catch (...) {} }
            else if (key == "length") { try { r.length = std::stoi(val); } catch (...) {} }
            else if (key == "locks") r.locks = val;
            else if (key == "row") r.rows.push_back(trim(val));
        }
    }

    int mine = 0, edited = 0, skipped = 0;
    for (const Rec& r : recs) {
        const int si = styleIndex(r.style);
        // Older files have 8 rows (no lead track).
        if (si < 0 || si >= kMaxStyles || r.rows.size() < static_cast<size_t>(kPadTracks) ||
            r.rows.size() > static_cast<size_t>(kTracks)) { ++skipped; continue; }
        int p;
        if (r.builtin >= 0 && r.builtin < kFactorySlots) {
            p = si * kMaxSlots + r.builtin;
            ++edited;
        } else {
            const int slot = sh.slotCount[si].load();
            if (slot >= kMaxSlots) { ++skipped; continue; }
            p = si * kMaxSlots + slot;
            sh.patternName[p] = r.name.empty() ? "My pattern" : r.name;
            sh.slotCount[si].store(slot + 1);
            ++mine;
        }
        clearPattern(sh, p);
        int len = kSteps;
        for (int t = 0; t < kTracks; ++t)
            len = std::max(len, setRow(sh, p, t, t < static_cast<int>(r.rows.size()) ? r.rows[static_cast<size_t>(t)] : std::string()));
        if (r.length > 0) len = r.length;
        sh.length[p].store(static_cast<uint8_t>(std::clamp((len + kSteps - 1) / kSteps * kSteps, kSteps, kMaxSteps)));
        applyLocks(sh, p, r.locks);
    }
    message = "Loaded " + std::to_string(mine) + " of your patterns";
    if (edited) message += " and " + std::to_string(edited) + " edited built-in ones";
    if (skipped) message += " (" + std::to_string(skipped) + " couldn't be read)";
    return true;
}

bool writeData(const std::string& text, std::string& error) {
    try {
        const fs::path file = dataFile();
        if (file.has_parent_path()) fs::create_directories(file.parent_path());
        fs::path tmp = file;
        tmp += ".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            if (!o) { error = "can't write " + tmp.u8string(); return false; }
            o << text;
            if (!o) { error = "write failed"; return false; }
        }
        fs::rename(tmp, file);  // replace in one step so a crash can't leave half a file
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace dnb
