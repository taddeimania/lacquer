// Snapshot.cpp — dev tool: run Lacquer without a host and render its panel
// to a PNG. Given a WAV file it also masters it: one LEARN pass, then a
// render, with the loudness before and after printed.
//   LacquerSnapshot out.png [input.wav output.wav [style target master]]
// Without an input file the panel shows a short built-in phrase.
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
// A few seconds of plucked chords over a simple beat, at a quiet mix level.
juce::AudioBuffer<float> testPhrase (double rate, double seconds)
{
    const int n = (int) (rate * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (7);
    const float chords[4][3] { { 220.0f, 261.6f, 329.6f }, { 174.6f, 220.0f, 261.6f }, { 196.0f, 246.9f, 293.7f }, { 164.8f, 196.0f, 246.9f } };
    const int beat = (int) (rate * 0.5);
    for (int i = 0; i < n; ++i)
    {
        const int bar = (i / (beat * 4)) % 4;
        const int inBar = i % (beat * 4);
        const float t = (float) (inBar % (beat * 2)) / (float) rate;
        const float env = std::exp (-t * 2.5f);
        float x = 0.0f;
        for (float f : chords[bar])
        {
            const float ph = (float) i * f / (float) rate;
            x += env * (std::sin (juce::MathConstants<float>::twoPi * ph) + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 2.0f * ph));
        }
        x *= 0.05f;
        const float tb = (float) (i % beat) / (float) rate;
        x += 0.15f * std::exp (-tb * 18.0f) * std::sin (juce::MathConstants<float>::twoPi * 55.0f * tb * (1.0f + 2.0f * std::exp (-tb * 30.0f)));
        const float th = (float) ((i + beat / 2) % beat) / (float) rate;
        const float hat = 0.025f * std::exp (-th * 60.0f) * (rng.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, x + hat);
        b.setSample (1, i, x - hat * 0.5f);
    }
    return b;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (f), rate, 2, 24, {}, 0));
    if (w)
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

void runThrough (LacquerProcessor& proc, juce::AudioBuffer<float>& buf, int block, bool pump = false)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        const int len = juce::jmin (block, buf.getNumSamples() - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, len);
        proc.processBlock (view, midi);
        if (pump)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: LacquerSnapshot out.png [input.wav output.wav [style target master]]\n");
        return 1;
    }
    double rate = 48000.0;
    const int block = 512;

    LacquerProcessor proc;
    proc.setPlayConfigDetails (2, 2, rate, block);
    proc.prepareToPlay (rate, block);
    using namespace lacquer;

    if (argc > 3)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])));
        if (r == nullptr)
        {
            std::printf ("couldn't read %s\n", argv[2]);
            return 1;
        }
        juce::AudioBuffer<float> input (2, (int) r->lengthInSamples);
        r->read (&input, 0, (int) r->lengthInSamples, 0, true, true);
        rate = r->sampleRate;
        proc.setPlayConfigDetails (2, 2, rate, block);
        proc.prepareToPlay (rate, block);
        if (argc > 6)
        {
            proc.setParam (ids::style, (float) juce::String (argv[4]).getIntValue());
            proc.setParam (ids::target, (float) juce::String (argv[5]).getIntValue());
            proc.setParam (ids::master, juce::String (argv[6]).getFloatValue());
        }

        // LEARN over the whole file.
        auto pass = input;
        proc.toggleLearn();
        runThrough (proc, pass, block);
        proc.toggleLearn();
        juce::AudioBuffer<float> tail (2, block * 4);
        tail.clear();
        runThrough (proc, tail, block);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // the timer applies the result
        std::printf ("%s\n", proc.learnStatus().toRawUTF8());

        // Render, compensating the latency.
        proc.prepareToPlay (rate, block);
        const int lat = proc.getLatencySamples();
        juce::AudioBuffer<float> out (2, input.getNumSamples() + lat);
        out.clear();
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, 0, input, c, 0, input.getNumSamples());
        runThrough (proc, out, block);
        juce::AudioBuffer<float> trimmed (2, input.getNumSamples());
        for (int c = 0; c < 2; ++c)
            trimmed.copyFrom (c, 0, out, c, lat, input.getNumSamples());

        LoudnessMeter before, after;
        before.prepare (rate);
        after.prepare (rate);
        before.process (input.getReadPointer (0), input.getReadPointer (1), input.getNumSamples());
        after.process (trimmed.getReadPointer (0), trimmed.getReadPointer (1), trimmed.getNumSamples());
        std::printf ("before: %.1f LUFS, %.1f dBTP\nafter:  %.1f LUFS, %.1f dBTP\n",
                     before.integrated(), before.truePeakDb(), after.integrated(), after.truePeakDb());
        writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]), trimmed, rate);
        proc.prepareToPlay (rate, block);
    }
    else
    {
        // LEARN on the test phrase so the panel has real readings.
        auto phrase = testPhrase (rate, 12.0);
        proc.toggleLearn();
        runThrough (proc, phrase, block, true);
        proc.toggleLearn();
        juce::AudioBuffer<float> tail (2, block * 4);
        tail.clear();
        runThrough (proc, tail, block);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        proc.resetMeters();
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (LacquerPanel::designW, LacquerPanel::designH);
    auto more = testPhrase (rate, 8.0);
    runThrough (proc, more, block, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (img, stream);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
