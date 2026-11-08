// Presets: a few factory presets, plus user presets saved as text files in a per-user folder.
// Values are normalized (0..1) per parameter tag, in the same order as the VST3 parameters.
#pragma once

#include <string>
#include <vector>

namespace naginata::presets {

constexpr int kParamCount = 19;

struct Preset {
    std::string name;
    std::vector<double> values;   // normalized, kParamCount entries
};

std::vector<Preset> factoryPresets();
std::string userPresetDirectory();
std::vector<Preset> loadUserPresets();
bool saveUserPreset(const Preset& preset);

}  // namespace naginata::presets
