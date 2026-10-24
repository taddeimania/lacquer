// Params.h — Lacquer's host parameters. STYLE, TARGET, MASTER and PUSH are
// the sound; the stage switches, COMPARE and MONO are for listening; the
// learned GAIN and DRIVE are saved with the set so the master comes back as
// you left it.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Styles.h"

namespace lacquer::ids
{
inline constexpr const char* style       = "style";
inline constexpr const char* target      = "target";
inline constexpr const char* master      = "master";
inline constexpr const char* push        = "push";
inline constexpr const char* inputGain   = "inputGain";
inline constexpr const char* driveOffset = "driveOffset";
inline constexpr const char* toneOn      = "toneOn";
inline constexpr const char* glueOn      = "glueOn";
inline constexpr const char* colorOn     = "colorOn";
inline constexpr const char* widthOn     = "widthOn";
inline constexpr const char* limitOn     = "limitOn";
inline constexpr const char* compare     = "compare";
inline constexpr const char* mono        = "mono";

inline const char* stageOn (int stage)
{
    static const char* ids[NumStages] { toneOn, glueOn, colorOn, widthOn, limitOn };
    return ids[juce::jlimit (0, NumStages - 1, stage)];
}

inline const juce::StringArray& styleNames()  { static const juce::StringArray n { "GENTLE", "WARM", "BRIGHT", "PUNCH", "DENSE" }; return n; }
inline const juce::StringArray& targetNames() { static const juce::StringArray n { "STREAMING", "DOWNLOAD", "CD", "VINYL" }; return n; }
inline const juce::StringArray& stageNames()  { static const juce::StringArray n { "TONE", "GLUE", "COLOR", "WIDTH", "LIMIT" }; return n; }

// Not part of a preset: they belong to the song (learned) or to listening.
inline bool notInPreset (const juce::String& id)
{
    return id == inputGain || id == driveOffset || id == compare || id == mono;
}

inline juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
inline juce::String decibels (float v, int places = 1)
{
    const float k = std::pow (10.0f, (float) places);
    const float r = std::round (v * k) / k;
    return (r > 0.0f ? "+" : "") + juce::String (r, places) + " dB";
}
} // namespace lacquer::ids

namespace lacquer
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return ids::decibels (v); }));
    };
    auto toggle = [] (const char* id, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::style, 1 }, "Style", ids::styleNames(), Gentle));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::target, 1 }, "Target", ids::targetNames(), Streaming));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::master, 1 }, "Master", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return ids::percent (v); })));
    layout.add (db (ids::push, "Push", -6.0f, 6.0f, 0.0f));
    layout.add (db (ids::inputGain, "Learned Gain", -24.0f, 24.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::driveOffset, 1 }, "Learned Drive",
        NormalisableRange<float> (-6.0f, 40.0f), 14.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    layout.add (toggle (ids::toneOn, "Tone On", true));
    layout.add (toggle (ids::glueOn, "Glue On", true));
    layout.add (toggle (ids::colorOn, "Color On", true));
    layout.add (toggle (ids::widthOn, "Width On", true));
    layout.add (toggle (ids::limitOn, "Limit On", true));
    layout.add (toggle (ids::compare, "Compare", false));
    layout.add (toggle (ids::mono, "Mono", false));
    return layout;
}
} // namespace lacquer
