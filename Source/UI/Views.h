// Views.h — Lacquer's custom panel pieces: the UNDER THE HOOD strip (five
// stages, each showing what it's doing; click one to switch it off) with
// input and output meters, and the loudness LCD.
#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

namespace lacquer::ui
{
// Segmented LED meter with a falling peak.
class Meter final : public juce::Component
{
public:
    void push (float peak);   // call from the panel timer
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f, hold = 0.0f;
    int holdFrames = 0, shownSegs = -1, shownHold = -1;
};

// IN -> TONE -> GLUE -> COLOR -> WIDTH -> LIMIT -> OUT.
class StageStrip final : public juce::Component, public juce::TooltipClient
{
public:
    explicit StageStrip (LacquerProcessor&);
    void update (const Telemetry&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    juce::Rectangle<float> blockBounds (int stage) const;

private:
    juce::String bigText (int stage) const;
    juce::String smallText (int stage) const;
    float barValue (int stage) const;          // 0..1, or -1 for none
    void drawToneCurve (juce::Graphics&, juce::Rectangle<float>, float alpha) const;

    LacquerProcessor& proc;
    Meter inMeter, outMeter;
    Telemetry t;
    juce::String shownKey;
};

// The loudness readout: integrated loudness large, a short-term bar with the
// target marked, momentary and true peak, and a status line.
class LoudnessDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LoudnessDisplay (LacquerProcessor&);
    void update (const Telemetry&);
    void paint (juce::Graphics&) override;

    juce::String statusText() const;

private:
    LacquerProcessor& proc;
    Telemetry t;
    float tpMax = -100.0f;
    juce::String shownKey;
};
} // namespace lacquer::ui
