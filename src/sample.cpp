#include "sample.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "styles.h"

namespace fs = std::filesystem;

namespace dnb {
namespace {

uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::shared_ptr<Sample> loadWav(const std::string& path, float targetRate, std::string& error) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) { error = "can't open the file"; return nullptr; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) {
        error = "not a WAV file";
        return nullptr;
    }
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* data = nullptr;
    size_t dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const uint32_t len = le32(&d[pos + 4]);
        const unsigned char* c = &d[pos + 8];
        const size_t avail = std::min<size_t>(len, d.size() - pos - 8);
        if (!std::memcmp(&d[pos], "fmt ", 4) && avail >= 16) {
            format = le16(c);
            channels = le16(c + 2);
            rate = le32(c + 4);
            bits = le16(c + 14);
            if (format == 0xFFFE && avail >= 26) format = le16(c + 24);  // WAVE_FORMAT_EXTENSIBLE
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            data = c;
            dataLen = avail;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || !channels || !rate) { error = "WAV has no audio"; return nullptr; }
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))) {
        error = "unsupported WAV format (use 16/24-bit PCM or 32-bit float)";
        return nullptr;
    }
    const int bps = bits / 8;
    const size_t frames = dataLen / static_cast<size_t>(bps * channels);
    if (frames == 0) { error = "WAV is empty"; return nullptr; }
    auto read = [&](size_t frame, int ch) -> float {
        const unsigned char* p = data + (frame * channels + ch) * bps;
        switch (bits) {
            case 8: return (p[0] - 128) / 128.f;
            case 16: return static_cast<int16_t>(le16(p)) / 32768.f;
            case 24: {
                int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000) v |= ~0xFFFFFF;
                return v / 8388608.f;
            }
            default:
                if (format == 3) { float v; std::memcpy(&v, p, 4); return v; }
                return static_cast<int32_t>(le32(p)) / 2147483648.f;
        }
    };
    // Resample (linear) to the engine rate; cap at 20 s.
    const double ratio = static_cast<double>(rate) / targetRate;
    const size_t outFrames = std::min<size_t>(static_cast<size_t>(frames / ratio), static_cast<size_t>(targetRate * 20));
    auto s = std::make_shared<Sample>();
    s->path = path;
    s->name = fs::u8path(path).filename().u8string();
    s->L.resize(outFrames);
    s->R.resize(outFrames);
    for (size_t i = 0; i < outFrames; ++i) {
        const double src = i * ratio;
        const size_t i0 = std::min(static_cast<size_t>(src), frames - 1), i1 = std::min(i0 + 1, frames - 1);
        const float fr = static_cast<float>(src - static_cast<double>(i0));
        const float l = read(i0, 0) + (read(i1, 0) - read(i0, 0)) * fr;
        const float r = channels > 1 ? read(i0, 1) + (read(i1, 1) - read(i0, 1)) * fr : l;
        s->L[i] = l;
        s->R[i] = r;
    }
    // Short fade-out at the end so a trimmed sample can't click.
    const size_t fade = std::min<size_t>(outFrames, 64);
    for (size_t i = 0; i < fade; ++i) {
        const float g = static_cast<float>(i) / fade;
        s->L[outFrames - 1 - i] *= g;
        s->R[outFrames - 1 - i] *= g;
    }
    return s;
}

// Long keywords match anywhere in the name ("909 Kick.wav"); short ones like "bd" or
// "oh" only as their own word, optionally followed by a number ("bd01", "oh_2").
static bool nameHas(const std::string& fn, const std::string& key) {
    if (key.size() > 3) return fn.find(key) != std::string::npos;
    size_t i = 0;
    while (i < fn.size()) {
        while (i < fn.size() && !std::isalpha(static_cast<unsigned char>(fn[i]))) ++i;
        size_t j = i;
        while (j < fn.size() && std::isalpha(static_cast<unsigned char>(fn[j]))) ++j;
        if (j > i && fn.compare(i, j - i, key) == 0 && j - i == key.size()) return true;
        i = j;
    }
    return false;
}

std::vector<std::string> matchKitFolder(const std::string& folder, const Style& st) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(fs::u8path(folder), ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (lower(e.path().extension().u8string()) == ".wav") files.push_back(e.path().u8string());
    }
    std::sort(files.begin(), files.end());
    // Keywords to look for, per kind of sound (checked against the file name).
    auto keysFor = [&](const DrumDef& d) -> std::vector<std::string> {
        const std::string n = lower(d.name);
        switch (d.type) {
            case DrumType::Kick: return {"kick", "bd", "bassdrum", "kik"};
            case DrumType::Snare: return {"snare", "sd", "snr"};
            case DrumType::Clap: return {"clap", "clp", "cp"};
            case DrumType::Rim:
                if (n.find("clave") != std::string::npos) return {"clave", "rim", "rs"};
                if (n.find("snap") != std::string::npos) return {"snap", "rim", "rs"};
                return {"rim", "rs", "stick", "side"};
            case DrumType::Tom:
                if (n.find("conga") != std::string::npos) return {"conga", "bongo", "tom"};
                if (n.find("timb") != std::string::npos) return {"timb", "tom", "perc"};
                if (n.find("perc") != std::string::npos) return {"perc", "tom"};
                return {"tom"};
            case DrumType::Shaker: return {"shaker", "shk", "maraca", "shake"};
            case DrumType::Cowbell: return {"cowbell", "cow", "bell"};
            case DrumType::Metal:
                if (n.find("ride") != std::string::npos) return {"ride"};
                if (n.find("crash") != std::string::npos) return {"crash", "cym"};
                if (n.find("tamb") != std::string::npos) return {"tamb", "tambourine"};
                if (n.find("open") != std::string::npos) return {"open", "ohh", "oh", "hat"};
                return {"closed", "chh", "hihat", "hh", "hat"};
        }
        return {};
    };
    std::vector<std::string> out;
    for (size_t slot = 0; slot < st.drums.size(); ++slot) {
        std::string pick;
        const bool wantSoft = lower(st.drums[slot].name).find("ghost") != std::string::npos;
        for (const auto& key : keysFor(st.drums[slot])) {
            for (const auto& fpath : files) {
                const std::string fn = lower(fs::u8path(fpath).filename().u8string());
                const std::string stem = lower(fs::u8path(fpath).stem().u8string());
                if (!nameHas(stem, key)) continue;
                // keep closed hats away from open-hat files and vice versa
                const bool isOpen = fn.find("open") != std::string::npos || nameHas(stem, "ohh") || nameHas(stem, "oh");
                if (key == "closed" || key == "chh" || key == "hihat" || key == "hh" || key == "hat")
                    if (isOpen && lower(st.drums[slot].name).find("open") == std::string::npos) continue;
                pick = fpath;
                break;
            }
            if (!pick.empty()) break;
        }
        (void)wantSoft;  // the ghost snare reuses the snare sample, played softer
        out.push_back(pick);
    }
    return out;
}

}  // namespace dnb
