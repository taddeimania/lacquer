// Presets.h — factory presets, and user presets saved as files. A preset is
// STYLE, TARGET, MASTER, PUSH and the stage switches. What LEARN found
// belongs to the song, so loading a preset keeps it.
//
// User presets live in a folder next to the settings file:
//   macOS:   ~/Library/Application Support/Lacquer/Presets
//   Windows: %APPDATA%\Lacquer\Presets
//   Linux:   ~/.config/Lacquer/Presets
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace lacquer
{
struct FactoryPreset
{
    const char* name;
    const char* note;                                    // one line: why these settings
    std::vector<std::pair<const char*, float>> values;   // plain values; anything not listed is default
};

const std::vector<FactoryPreset>& factoryPresets();

juce::File userPresetFolder();
juce::Array<juce::File> userPresets();   // sorted by name
inline constexpr const char* kPresetExtension = ".lacquer";
} // namespace lacquer
