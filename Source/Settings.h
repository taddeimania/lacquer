// Settings.h — per-user preferences shared by every Lacquer on the machine:
// tooltips on or off. User presets are kept in a folder beside this file.
//
//   macOS:   ~/Library/Application Support/Lacquer/Lacquer.settings
//   Windows: %APPDATA%\Lacquer\Lacquer.settings
//   Linux:   ~/.config/Lacquer/Lacquer.settings
//
// Message thread only.
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace lacquer
{
struct SettingsStore
{
    SettingsStore();
    juce::PropertiesFile props;
};

class Settings
{
public:
    bool tooltipsEnabled() const;          // default: on
    void setTooltipsEnabled (bool);

    static juce::File file();

private:
    juce::SharedResourcePointer<SettingsStore> store;
};
} // namespace lacquer
