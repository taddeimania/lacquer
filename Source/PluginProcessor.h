#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#include "Engine/Master.h"
#include "Params.h"

class LacquerProcessor final : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    LacquerProcessor();
    ~LacquerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets double as the host's programs.
    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, factoryIndex); }
    void setCurrentProgram (int index) override { loadFactory (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- parameters (message thread) ---------------------------------------------
    void setParam (const juce::String& id, float plainValue);
    float getParam (const juce::String& id) const { return params.at (id)->load(); }
    int   geti (const char* id) const { return juce::roundToInt (getParam (id)); }

    // --- presets (message thread) --------------------------------------------------
    void loadFactory (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name, juce::String& error);
    void stepPreset (int delta);              // through factory, then user presets
    juce::String presetName() const { return currentName; }
    int  presetNumber() const;                // 1-based across factory and user; 0 if none
    bool presetEdited() const;

    // --- actions from the panel -----------------------------------------------------
    void toggleLearn()   { learnToggles.fetch_add (1); learnMessage = {}; }
    void resetMeters()   { meterResets.fetch_add (1); }
    juce::String learnStatus() const { return learnMessage; }
    bool hasLearned() const { return learned; }

    // --- read by the panel --------------------------------------------------------------
    // A copy of what the engine reports. The peaks are the largest since the
    // last call, then cleared.
    lacquer::Telemetry takeTelemetry();

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void cacheParams();
    void readSettings();
    std::unique_ptr<juce::XmlElement> stateXml();
    std::vector<float> snapshot() const;

    std::map<juce::String, std::atomic<float>*> params;
    struct
    {
        std::atomic<float>* style = nullptr; std::atomic<float>* target = nullptr; std::atomic<float>* master = nullptr;
        std::atomic<float>* push = nullptr; std::atomic<float>* gain = nullptr; std::atomic<float>* drive = nullptr;
        std::array<std::atomic<float>*, lacquer::NumStages> on {};
        std::atomic<float>* compare = nullptr; std::atomic<float>* mono = nullptr;
    } fast;

    lacquer::Master engine;
    lacquer::MasterSettings settings;
    juce::AudioBuffer<float> monoScratch;

    std::atomic<int> learnToggles { 0 }, meterResets { 0 };
    bool wasPlaying = false;

    // LEARN results, audio thread -> message thread.
    std::atomic<int> pendingCode { 0 };
    std::atomic<float> pendingGain { 0.0f }, pendingDrive { 0.0f };
    juce::String learnMessage;
    bool learned = false;

    // Telemetry, audio thread -> panel.
    juce::SpinLock telemetryLock;
    lacquer::Telemetry shared;

    // Presets.
    int factoryIndex = 0;
    juce::File userFile;
    juce::String currentName;
    std::vector<float> loadedValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerProcessor)
};
