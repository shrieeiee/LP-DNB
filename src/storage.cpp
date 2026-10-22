#include "storage.h"

#include <cstdlib>
#include <filesystem>
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
            for (int t = 0; t < kTracks; ++t) o << "row=" << encodeRow(sh, p, t) << "\n";
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
    struct Rec { std::string style, name; int builtin = -1; std::vector<std::string> rows; };
    std::vector<Rec> recs;
    std::string section, line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line;
            if (section == "[pattern]") recs.emplace_back();
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
            } catch (...) {
            }
        } else if (section == "[pattern]" && !recs.empty()) {
            Rec& r = recs.back();
            if (key == "style") r.style = val;
            else if (key == "name") r.name = val;
            else if (key == "builtin") { try { r.builtin = std::stoi(val); } catch (...) {} }
            else if (key == "row") r.rows.push_back(trim(val));
        }
    }

    int mine = 0, edited = 0, skipped = 0;
    for (const Rec& r : recs) {
        const int si = styleIndex(r.style);
        if (si < 0 || si >= kMaxStyles || r.rows.size() != static_cast<size_t>(kTracks)) { ++skipped; continue; }
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
        for (int t = 0; t < kTracks; ++t) setRow(sh, p, t, r.rows[static_cast<size_t>(t)]);
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
