#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Settings.h"
#include "UI/Views.h"
#include "UI/Widgets.h"

// The panel is laid out in a fixed design space and scaled to fit the window,
// as on Beadwork, Posy, Seyir and Mottle.
class LacquerPanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1200;
    static constexpr int designH = 800;

    explicit LacquerPanel (LacquerProcessor&);
    ~LacquerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override {}

    std::function<void (bool)> onTooltips;
    void syncTooltips();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using Knob = lacquer::ui::Knob;
    using PanelButton = lacquer::ui::PanelButton;
    using ChoiceDisplay = lacquer::ui::ChoiceDisplay;

    void timerCallback() override;

    void addKnob (Knob&, const char* id, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addToggle (PanelButton&, const char* id, juce::Rectangle<int>, const juce::String& tip);
    void addButton (PanelButton&, juce::Rectangle<int>, const juce::String& tip,
                    std::function<void (const juce::ModifierKeys&)> click);

    void openPresetMenu();
    void savePresetDialog();
    juce::String masterLine() const;

    LacquerProcessor& proc;
    lacquer::ui::PanelLookAndFeel lnf;
    lacquer::Settings settings;

    // Header
    ChoiceDisplay presetChoice;
    PanelButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, tipsBtn { "TIPS" };

    // Style, master, target
    std::array<PanelButton, lacquer::NumStyles> styleBtns;
    std::array<PanelButton, lacquer::NumTargets> targetBtns;
    Knob masterKnob;

    // Under the hood
    lacquer::ui::StageStrip strip { proc };

    // Loudness
    PanelButton learnBtn { "LEARN" }, compareBtn { "COMPARE" }, monoBtn { "MONO" }, resetBtn { "RESET" };
    Knob pushKnob;
    lacquer::ui::LoudnessDisplay loudness { proc };

    std::vector<std::pair<PanelButton*, const char*>> toggles;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    int tooltipsApplied = -1;
    int shownStyle = -1, shownTarget = -1;
    juce::String shownMaster, shownLearn;
    int flash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerPanel)
};

class LacquerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LacquerEditor (LacquerProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LacquerPanel panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerEditor)
};
