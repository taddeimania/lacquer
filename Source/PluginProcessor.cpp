#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace lacquer;

LacquerProcessor::LacquerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LACQUER", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (p))
            params[wp->getParameterID()] = apvts.getRawParameterValue (wp->getParameterID());

    cacheParams();
    currentName = factoryPresets().front().name;
    loadedValues = snapshot();
    startTimerHz (10);
}

LacquerProcessor::~LacquerProcessor()
{
    stopTimer();
}

bool LacquerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s)
    {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };
    return ok (in) && ok (out);
}

void LacquerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.latency());
    monoScratch.setSize (1, juce::jmax (512, samplesPerBlock));
}

// ---------------------------------------------------------------------------
void LacquerProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void LacquerProcessor::cacheParams()
{
    auto ptr = [this] (const char* id) { return params.at (id); };
    fast.style = ptr (ids::style);
    fast.target = ptr (ids::target);
    fast.master = ptr (ids::master);
    fast.push = ptr (ids::push);
    fast.gain = ptr (ids::inputGain);
    fast.drive = ptr (ids::driveOffset);
    for (int s = 0; s < NumStages; ++s)
        fast.on[(size_t) s] = ptr (ids::stageOn (s));
    fast.compare = ptr (ids::compare);
    fast.mono = ptr (ids::mono);
}

// Audio thread: no lookups by name here.
void LacquerProcessor::readSettings()
{
    auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    settings.style = juce::roundToInt (get (fast.style));
    settings.target = juce::roundToInt (get (fast.target));
    settings.amount = get (fast.master);
    settings.trimDb = get (fast.push);
    settings.inputGainDb = get (fast.gain);
    settings.driveOffsetDb = get (fast.drive);
    for (int s = 0; s < NumStages; ++s)
        settings.on[(size_t) s] = get (fast.on[(size_t) s]) > 0.5f;
    settings.compare = get (fast.compare) > 0.5f;
    settings.mono = get (fast.mono) > 0.5f;
    settings.learnToggles = learnToggles.load (std::memory_order_relaxed);
    settings.meterResets = meterResets.load (std::memory_order_relaxed);
}

void LacquerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (numOut == 0 || n == 0)
        return;
    for (int c = numIn; c < numOut; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, n);   // mono in: both sides start the same

    // Starting playback starts a fresh integrated reading, so it measures
    // the song from where you pressed play.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying && ! engine.learning())
                meterResets.fetch_add (1);
            wasPlaying = playing;
        }

    readSettings();

    float* left = buffer.getWritePointer (0);
    if (numOut >= 2)
        engine.process (left, buffer.getWritePointer (1), n, settings);
    else
    {
        // Mono out: run stereo on a scratch right side, then fold down.
        float* right = monoScratch.getWritePointer (0);
        for (int start = 0; start < n; start += monoScratch.getNumSamples())
        {
            const int len = juce::jmin (monoScratch.getNumSamples(), n - start);
            std::copy (left + start, left + start + len, right);
            engine.process (left + start, right, len, settings);
            for (int i = 0; i < len; ++i)
                left[start + i] = 0.5f * (left[start + i] + right[i]);
        }
    }

    const auto res = engine.takeLearnResult();
    if (res.code != LearnResult::None)
    {
        pendingGain.store (res.inputGainDb);
        pendingDrive.store (res.driveOffsetDb);
        pendingCode.store (res.code);
    }

    if (telemetryLock.tryEnter())
    {
        const float inPk = juce::jmax (shared.inPeak, engine.telemetry().inPeak);
        const float outPk = juce::jmax (shared.outPeak, engine.telemetry().outPeak);
        shared = engine.telemetry();
        shared.inPeak = inPk;
        shared.outPeak = outPk;
        telemetryLock.exit();
        engine.clearPeaks();
    }
}

Telemetry LacquerProcessor::takeTelemetry()
{
    const juce::SpinLock::ScopedLockType lock (telemetryLock);
    auto t = shared;
    shared.inPeak = shared.outPeak = 0.0f;
    return t;
}

void LacquerProcessor::timerCallback()
{
    const int code = pendingCode.exchange (0);
    if (code == LearnResult::Done)
    {
        const float g = pendingGain.load(), d = pendingDrive.load();
        setParam (ids::inputGain, g);
        setParam (ids::driveOffset, d);
        learned = true;
        learnMessage = "Learned. Gain " + ids::decibels (g) + ", limiter drive "
                     + ids::decibels (d + targetSpec (geti (ids::target)).lufs) + ".";
    }
    else if (code == LearnResult::TooShort)
        learnMessage = "Too short to judge. Press LEARN and play at least 5 seconds, ideally the whole song.";
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
int LacquerProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String LacquerProcessor::getProgramName (int index)
{
    const auto& f = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

std::vector<float> LacquerProcessor::snapshot() const
{
    std::vector<float> v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! ids::notInPreset (rp->getParameterID()))
                v.push_back (rp->getValue());
    return v;
}

bool LacquerProcessor::presetEdited() const
{
    const auto now = snapshot();
    if (now.size() != loadedValues.size())
        return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (std::abs (now[i] - loadedValues[i]) > 0.002f)
            return true;
    return false;
}

void LacquerProcessor::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& preset = list[(size_t) index];

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (ids::notInPreset (rp->getParameterID()))
                continue;
            float v = rp->getDefaultValue();
            for (const auto& [id, plain] : preset.values)
                if (rp->getParameterID() == id)
                    v = rp->convertTo0to1 (plain);
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
    factoryIndex = index;
    userFile = juce::File();
    currentName = preset.name;
    loadedValues = snapshot();
}

std::unique_ptr<juce::XmlElement> LacquerProcessor::stateXml()
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr)
        return nullptr;
    xml->setAttribute ("presetName", currentName);
    xml->setAttribute ("presetFactory", userFile == juce::File() ? factoryIndex : -1);
    xml->setAttribute ("presetFile", userFile.getFullPathName());
    xml->setAttribute ("learned", learned);
    return xml;
}

bool LacquerProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (ids::notInPreset (id))
            continue;
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
            p->endChangeGesture();
        }
    }
    userFile = file;
    factoryIndex = -1;
    currentName = file.getFileNameWithoutExtension();
    loadedValues = snapshot();
    return true;
}

bool LacquerProcessor::saveUserPreset (const juce::String& rawName, juce::String& error)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
    {
        error = "Give the preset a name.";
        return false;
    }
    const auto folder = userPresetFolder();
    if (! folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName();
        return false;
    }
    const auto file = folder.getChildFile (name + kPresetExtension);
    currentName = name;
    userFile = file;
    factoryIndex = -1;
    auto xml = apvts.copyState().createXml();
    if (xml != nullptr)
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (ids::notInPreset (e->getStringAttribute ("id")))
                e->setAttribute ("value", 0.0);   // ignored on load
    if (xml == nullptr || ! xml->writeTo (file))
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }
    loadedValues = snapshot();
    return true;
}

void LacquerProcessor::stepPreset (int delta)
{
    const int numFactory = (int) factoryPresets().size();
    const auto users = userPresets();
    const int total = numFactory + users.size();
    int cur = presetNumber() - 1;
    if (cur < 0) cur = 0;
    const int next = (cur + delta + total) % total;
    if (next < numFactory)
        loadFactory (next);
    else
        loadUserPreset (users[next - numFactory]);
}

int LacquerProcessor::presetNumber() const
{
    if (userFile == juce::File())
        return factoryIndex >= 0 ? factoryIndex + 1 : 0;
    const auto users = userPresets();
    const int i = users.indexOf (userFile);
    return i >= 0 ? (int) factoryPresets().size() + i + 1 : 0;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void LacquerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateXml())
    {
        // COMPARE and MONO are for listening; a reopened set starts without them.
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id == ids::compare || id == ids::mono)
                e->setAttribute ("value", 0.0);
        }
        copyXmlToBinary (*xml, destData);
    }
}

void LacquerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    currentName = xml->getStringAttribute ("presetName", currentName);
    factoryIndex = xml->getIntAttribute ("presetFactory", -1);
    userFile = juce::File (xml->getStringAttribute ("presetFile"));
    if (userFile != juce::File() && ! userFile.existsAsFile())
        userFile = juce::File();
    learned = xml->getBoolAttribute ("learned", false);
    loadedValues = snapshot();
}

juce::AudioProcessorEditor* LacquerProcessor::createEditor()
{
    return new LacquerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LacquerProcessor();
}
