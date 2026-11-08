#include "naginata_presets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <utility>

namespace naginata::presets {
namespace {

// Plain defaults in tag order. These match the defaults in naginata_vst3.cpp.
constexpr std::array<double, kParamCount> kPlainDefaults = {
    0, 0.7, 0.45, 0.6, 1, 0, 0, 1,            // 0..7   Unit I: engage, level, tone, gain, wet, mid, HPF, LPF
    0, 0, 0.4, 0.4, 0.7, 0.7, 1, 1, 1,        // 8..16  Unit II: engage, clean, pos, neg, thresholds, wet, sum, sym
    0.4, 1.0,                                 // 17..18 Unit I: harmonics, harmonics depth
};

// Mid (tag 5) and Sum (tag 15) are three-position switches (0..2); everything else is 0..1.
double rangeMax(int tag) { return (tag == 5 || tag == 15) ? 2.0 : 1.0; }

Preset makePreset(const char* name, std::initializer_list<std::pair<int, double>> changes) {
    std::array<double, kParamCount> plain = kPlainDefaults;
    for (const auto& [tag, value] : changes) plain[tag] = value;
    Preset p{name, {}};
    for (int t = 0; t < kParamCount; ++t) p.values.push_back(plain[t] / rangeMax(t));
    return p;
}

std::filesystem::path baseDirectory() {
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
#else
    const char* base = std::getenv("HOME");
#endif
    return std::filesystem::path(base ? base : ".") / "Naginata" / "Presets";
}

std::string sanitize(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += std::isalnum(c) ? char(c) : '_';
    return out.empty() ? std::string("preset") : out;
}

}  // namespace

std::vector<Preset> factoryPresets() {
    return {
        makePreset("Init", {}),
        makePreset("Pedal Match", {{0, 1.0}}),
        makePreset("Mid Boost Crunch", {{0, 1.0}, {5, 1.0}, {2, 0.6}, {3, 0.5}, {17, 0.2}, {18, 0.5}}),
        makePreset("Scooped Lead", {{0, 1.0}, {5, 2.0}, {2, 0.4}, {3, 0.8}, {7, 0.8}, {17, 0.6}, {18, 0.8}}),
        makePreset("Fold Bite", {{0, 1.0}, {8, 1.0}, {15, 2.0}, {10, 0.6}, {11, 0.6}, {12, 0.5}, {13, 0.5}}),
    };
}

std::string userPresetDirectory() { return baseDirectory().string(); }

std::vector<Preset> loadUserPresets() {
    std::vector<Preset> result;
    std::error_code ec;
    const auto dir = baseDirectory();
    if (!std::filesystem::is_directory(dir, ec)) return result;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".ngnt") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        std::ifstream in(file);
        std::string header, name, line;
        if (!std::getline(in, header) || header != "NGNT1") continue;
        if (!std::getline(in, name)) continue;
        Preset p{name, {}};
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            double v = 0;
            if (ss >> v) p.values.push_back(v);
        }
        if (int(p.values.size()) == kParamCount) result.push_back(p);
    }
    return result;
}

bool saveUserPreset(const Preset& preset) {
    if (int(preset.values.size()) != kParamCount) return false;
    std::error_code ec;
    const auto dir = baseDirectory();
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    const auto path = dir / (sanitize(preset.name) + ".ngnt");
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "NGNT1\n" << preset.name << "\n";
    char buf[64];
    for (double v : preset.values) {
        std::snprintf(buf, sizeof(buf), "%.9f", v);
        out << buf << "\n";
    }
    return bool(out);
}

}  // namespace naginata::presets
