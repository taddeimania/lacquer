// EngineTests.cpp — checks Lacquer's signal path without JUCE or a host.
// Run with ctest, or directly: ./LacquerEngineTests
#include "Engine/Master.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <vector>

using namespace lacquer;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL: %s\n", what);
    }
}

void checkNear (double got, double want, double tol, const char* what)
{
    ++checks;
    if (! (std::fabs (got - want) <= tol))
    {
        ++failures;
        std::printf ("FAIL: %s (got %.3f, want %.3f +- %.3f)\n", what, got, want, tol);
    }
}

constexpr double kTwoPi = 6.283185307179586;

// A few bars of something like a song: a bass line, chords, kick and hat,
// with a quieter verse and a louder chorus. `gain` scales all of it.
struct Song
{
    double sr;
    float gain;
    long n = 0;
    uint32_t seed = 3;
    double pb = 0, pc[3] {};

    Song (double rate, float g) : sr (rate), gain (g) {}

    void next (float& l, float& r)
    {
        const double t = (double) n / sr;
        const double beat = 0.5;                           // 120 bpm
        const int bar = (int) (t / (4 * beat));
        const bool chorus = (bar / 4) % 2 == 1;
        const double tb = std::fmod (t, beat);
        const double chords[4][3] { { 220.0, 261.6, 329.6 }, { 174.6, 220.0, 261.6 }, { 196.0, 246.9, 293.7 }, { 164.8, 196.0, 246.9 } };
        const auto& ch = chords[bar % 4];

        const double kick = std::exp (-tb * 20.0) * std::sin (kTwoPi * 50.0 * tb * (1.0 + 3.0 * std::exp (-tb * 40.0)));
        seed = seed * 1664525u + 1013904223u;
        const double noise = ((double) (seed >> 8) / 8388608.0 - 1.0);
        const double th = std::fmod (t + beat * 0.5, beat);
        const double hat = noise * std::exp (-th * 70.0);

        pb += ch[0] * 0.25 / sr;
        const double bass = std::sin (kTwoPi * pb) * (0.6 + 0.4 * std::exp (-tb * 6.0));
        double pad = 0;
        for (int k = 0; k < 3; ++k)
        {
            pc[k] += ch[k] / sr;
            pad += std::sin (kTwoPi * pc[k]) + 0.25 * std::sin (kTwoPi * 2.0 * pc[k]);
        }
        const double sec = chorus ? 1.0 : 0.5;
        const double mono = sec * (0.5 * kick + 0.25 * bass + 0.07 * pad) + 0.06 * hat;
        const double side = 0.03 * sec * std::sin (kTwoPi * pc[1] * 1.003) + 0.03 * hat;
        l = (float) (gain * (mono + side));
        r = (float) (gain * (mono - side));
        ++n;
    }
};

using Fill = std::function<void (float&, float&)>;

// Runs `seconds` of `src` through `m` with settings `s` (which `each` may
// change per block), feeding every output block to `out`.
void run (Master& m, MasterSettings& s, double sr, double seconds, Fill src,
          std::function<void (const float*, const float*, int)> out = nullptr,
          std::function<void (long, MasterSettings&)> each = nullptr)
{
    const int block = 480;
    std::vector<float> l (block), r (block);
    const long total = (long) (seconds * sr);
    for (long pos = 0; pos < total; pos += block)
    {
        if (each) each (pos, s);
        for (int i = 0; i < block; ++i) src (l[(size_t) i], r[(size_t) i]);
        m.process (l.data(), r.data(), block, s);
        if (out) out (l.data(), r.data(), block);
    }
}

// ---------------------------------------------------------------------------
void testLoudnessMeter()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m;
        m.prepare (sr);
        std::vector<float> l (1000), r (1000), z (1000, 0.0f);
        const float a = dbToGain (-20.0f);
        long n = 0;
        for (int b = 0; b < (int) (sr * 10 / 1000); ++b)
        {
            for (int i = 0; i < 1000; ++i, ++n)
                l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (kTwoPi * 1000.0 * n / sr);
            m.process (l.data(), r.data(), 1000);
        }
        checkNear (m.integrated(), -20.0, 0.1, "1 kHz at -20 dBFS in both channels reads -20 LUFS");
        checkNear (m.shortTerm(), -20.0, 0.1, "short-term agrees on a steady tone");
        checkNear (m.momentary(), -20.0, 0.1, "momentary agrees on a steady tone");

        m.reset();
        n = 0;
        for (int b = 0; b < (int) (sr * 10 / 1000); ++b)
        {
            for (int i = 0; i < 1000; ++i, ++n)
                l[(size_t) i] = a * (float) std::sin (kTwoPi * 1000.0 * n / sr);
            m.process (l.data(), z.data(), 1000);
        }
        checkNear (m.integrated(), -23.01, 0.1, "the same tone in one channel reads 3 LU lower");
    }

    // Gating: 20 s at -20 then 20 s at -36. The quiet part is under the
    // relative gate (mean minus 10 LU), so it doesn't pull the reading down.
    {
        const double sr = 48000.0;
        LoudnessMeter m;
        m.prepare (sr);
        std::vector<float> l (480), r (480);
        long n = 0;
        for (int part = 0; part < 2; ++part)
        {
            const float a = dbToGain (part == 0 ? -20.0f : -36.0f);
            for (int b = 0; b < 2000; ++b)
            {
                for (int i = 0; i < 480; ++i, ++n)
                    l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (kTwoPi * 1000.0 * n / sr);
                m.process (l.data(), r.data(), 480);
            }
        }
        checkNear (m.integrated(), -20.0, 0.25, "relative gate ignores the quiet half");
    }

    // True peak: a sine at a quarter of the sample rate, 45 degrees off the
    // samples, peaks 3 dB above its largest sample.
    {
        const double sr = 48000.0;
        LoudnessMeter m;
        m.prepare (sr);
        std::vector<float> l (480), r (480);
        long n = 0;
        float samplePeak = 0.0f;
        for (int b = 0; b < 100; ++b)
        {
            for (int i = 0; i < 480; ++i, ++n)
            {
                l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (kTwoPi * 0.25 * n + kTwoPi / 8.0);
                samplePeak = std::max (samplePeak, std::fabs (l[(size_t) i]));
            }
            m.process (l.data(), r.data(), 480);
        }
        checkNear (gainToDb (samplePeak), -9.03, 0.05, "test tone's sample peak");
        checkNear (m.truePeakDb(), -6.02, 0.25, "true peak finds the peak between the samples");
    }
}

// ---------------------------------------------------------------------------
MasterSettings base (int style, int target, float amount)
{
    MasterSettings s;
    s.style = style;
    s.target = target;
    s.amount = amount;
    return s;
}

void testCeilingEverywhere()
{
    const char* targetNames[] { "STREAMING", "DOWNLOAD", "CD", "VINYL" };
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int target = 0; target < NumTargets; ++target)
            for (int style = 0; style < NumStyles; ++style)
                for (float amount : { 0.0f, 1.0f })
                {
                    Master m;
                    m.prepare (sr);
                    auto s = base (style, target, amount);
                    s.inputGainDb = 6.0f;
                    s.driveOffsetDb = 20.0f - targetSpec (target).lufs;   // +20 dB into the limiter: far too hot
                    Song song (sr, 0.5f);
                    LoudnessMeter tpMeter;
                    tpMeter.prepare (sr);
                    bool finite = true;
                    run (m, s, sr, 4.0, [&] (float& l, float& r) { song.next (l, r); },
                         [&] (const float* l, const float* r, int n)
                         {
                             for (int i = 0; i < n; ++i)
                                 finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                             tpMeter.process (l, r, n);
                         });
                    char what[160];
                    std::snprintf (what, sizeof what, "%s style %d amount %.1f at %.0f Hz stays under %.1f dBTP",
                                   targetNames[target], style, amount, sr, targetSpec (target).ceilingDb);
                    check (finite, "output stays finite");
                    check (tpMeter.truePeakDb() <= targetSpec (target).ceilingDb + 0.15f, what);
                }
}

// LEARN, then measure a fresh play-through with what it learned.
void testLearn()
{
    const double sr = 48000.0;
    for (int target = 0; target < NumTargets; ++target)
        for (int style : { (int) Gentle, (int) Punch, (int) Dense })
            for (float level : { 0.05f, 0.4f })      // a quiet mix and a hot one
            {
                Master m;
                m.prepare (sr);
                auto s = base (style, target, 0.6f);
                Song song (sr, level);
                auto src = [&] (float& l, float& r) { song.next (l, r); };

                s.learnToggles++;
                run (m, s, sr, 34.0, src);
                s.learnToggles++;
                run (m, s, sr, 0.02, src);
                const auto res = m.takeLearnResult();
                check (res.code == LearnResult::Done, "LEARN finishes when pressed again");
                s.inputGainDb = res.inputGainDb;
                s.driveOffsetDb = res.driveOffsetDb;

                // Play it again from the top and measure.
                m.reset();
                Song again (sr, level);
                LoudnessMeter meter;
                meter.prepare (sr);
                run (m, s, sr, 32.0, [&] (float& l, float& r) { again.next (l, r); },
                     [&] (const float* l, const float* r, int n) { meter.process (l, r, n); });

                char what[160];
                std::snprintf (what, sizeof what, "LEARN lands on target %d (style %d, mix at %.2f)", target, style, level);
                checkNear (meter.integrated(), targetSpec (target).lufs, 1.0, what);
                check (meter.truePeakDb() <= targetSpec (target).ceilingDb + 0.15f, "and under the ceiling");
            }
}

void testLearnEdges()
{
    const double sr = 48000.0;
    // Too short: two seconds isn't enough to judge a song.
    {
        Master m;
        m.prepare (sr);
        auto s = base (Gentle, Streaming, 0.5f);
        s.inputGainDb = 1.5f;
        s.driveOffsetDb = 15.0f;
        Song song (sr, 0.2f);
        s.learnToggles++;
        run (m, s, sr, 2.0, [&] (float& l, float& r) { song.next (l, r); });
        s.learnToggles++;
        run (m, s, sr, 0.02, [&] (float& l, float& r) { song.next (l, r); });
        const auto res = m.takeLearnResult();
        check (res.code == LearnResult::TooShort, "a two-second LEARN is refused");
        checkNear (res.inputGainDb, 1.5, 1.0e-4, "and keeps the old gain");
        checkNear (res.driveOffsetDb, 15.0, 1.0e-4, "and the old drive");
    }
    // Stops by itself after three seconds of silence once it has heard enough.
    {
        Master m;
        m.prepare (sr);
        auto s = base (Gentle, Streaming, 0.5f);
        Song song (sr, 0.2f);
        s.learnToggles++;
        run (m, s, sr, 12.0, [&] (float& l, float& r) { song.next (l, r); });
        check (m.learning(), "still listening while the song plays");
        run (m, s, sr, 4.0, [] (float& l, float& r) { l = r = 0.0f; });
        check (! m.learning(), "stops after the song ends");
        check (m.takeLearnResult().code == LearnResult::Done, "and keeps what it heard");
    }
}

// MASTER at 0 with no drive: the stages do nothing, so the output is the
// input, late by the reported latency.
void testTransparentAndLatency()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Master m;
        m.prepare (sr);
        auto s = base (Gentle, Streaming, 0.0f);
        s.driveOffsetDb = -targetSpec (Streaming).lufs;   // drive 0 dB
        const int lat = m.latency();
        check (lat > 0 && lat < (int) (sr * 0.003) + 8, "latency is a few milliseconds");

        // Impulse: the biggest output sample is at the latency.
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        l[100] = r[100] = 0.25f;
        m.process (l.data(), r.data(), 4096, s);
        int at = 0;
        for (int i = 0; i < 4096; ++i)
            if (std::fabs (l[(size_t) i]) > std::fabs (l[(size_t) at])) at = i;
        checkNear (at - 100, lat, 0, "impulse comes out at the reported latency");

        // Quiet mid-range material passes untouched.
        m.reset();
        std::vector<float> inL, outL;
        long n = 0;
        run (m, s, sr, 2.0, [&] (float& a, float& b)
             {
                 a = 0.1f * (float) (std::sin (kTwoPi * 440.0 * n / sr) + 0.5 * std::sin (kTwoPi * 1250.0 * n / sr));
                 b = a * 0.8f;
                 inL.push_back (a);
                 ++n;
             },
             [&] (const float* a, const float*, int k) { outL.insert (outL.end(), a, a + k); });
        // The always-on 20 Hz filters shift phase a little, so compare level, not waveform.
        double eo = 0, ei = 0;
        for (size_t i = (size_t) sr / 2; i < outL.size(); ++i)
        {
            eo += (double) outL[i] * outL[i];
            ei += (double) inL[i - (size_t) lat] * inL[i - (size_t) lat];
        }
        char what[96];
        std::snprintf (what, sizeof what, "MASTER 0 leaves the level alone at %.0f Hz", sr);
        checkNear (10 * std::log10 (eo / ei), 0.0, 0.05, what);
    }
}

void testCompareAndMono()
{
    const double sr = 48000.0;
    Master m;
    m.prepare (sr);
    auto s = base (Punch, Cd, 0.7f);
    s.inputGainDb = 8.0f;
    s.driveOffsetDb = 14.0f;
    Song song (sr, 0.1f);
    auto src = [&] (float& l, float& r) { song.next (l, r); };
    run (m, s, sr, 8.0, src);
    const float master = m.telemetry().shortTerm;
    check (master - m.telemetry().inShortTerm > 6.0f, "the master is much louder than the mix");

    s.compare = true;
    LoudnessMeter heard;
    heard.prepare (sr);
    run (m, s, sr, 6.0, src, [&] (const float* l, const float* r, int n) { heard.process (l, r, n); });
    checkNear (heard.shortTerm(), m.telemetry().shortTerm, 1.0, "COMPARE plays the original at the master's loudness");

    // MONO: a signal that's all side cancels.
    Master mm;
    mm.prepare (sr);
    auto ms = base (Gentle, Streaming, 0.0f);
    ms.driveOffsetDb = -targetSpec (Streaming).lufs;
    ms.mono = true;
    long n = 0;
    float peak = 0.0f;
    run (mm, ms, sr, 1.0, [&] (float& l, float& r) { l = 0.2f * (float) std::sin (kTwoPi * 500.0 * n++ / sr); r = -l; },
         [&] (const float* l, const float*, int k) { for (int i = 0; i < k; ++i) peak = std::max (peak, std::fabs (l[i])); });
    check (peak < 1.0e-3f, "MONO cancels a signal that's all side");
}

void testStagesDoSomething()
{
    const double sr = 48000.0;
    // Every stage switched off and no gains: a clean delay.
    {
        Master m;
        m.prepare (sr);
        auto s = base (Dense, Streaming, 1.0f);
        s.on = { false, false, false, false, false };
        std::vector<float> inL, outL;
        Song song (sr, 0.4f);
        run (m, s, sr, 2.0, [&] (float& l, float& r) { song.next (l, r); inL.push_back (l); },
             [&] (const float* l, const float*, int k) { outL.insert (outL.end(), l, l + k); });
        double err = 0;
        for (size_t i = (size_t) m.latency(); i < outL.size(); ++i)
            err = std::max (err, (double) std::fabs (outL[i] - inL[i - (size_t) m.latency()]));
        check (err < 1.0e-5, "all stages off is a clean delay");
    }
    // GLUE compresses at MASTER 100% on a calibrated mix, and not at 0%.
    for (float amount : { 0.0f, 1.0f })
    {
        Master m;
        m.prepare (sr);
        auto s = base (Punch, Streaming, amount);
        s.inputGainDb = 13.0f;                 // this mix is about -29 LUFS; LEARN would bring it to -16
        Song song (sr, 0.25f);
        float maxGr = 0.0f;
        run (m, s, sr, 8.0, [&] (float& l, float& r) { song.next (l, r); }, [&] (const float*, const float*, int)
             { maxGr = std::max (maxGr, m.telemetry().glueGrDb); });
        if (amount == 0.0f) check (maxGr < 0.01f, "GLUE does nothing at MASTER 0");
        else                check (maxGr > 1.0f && maxGr < 12.0f, "GLUE compresses a few dB at MASTER 100");
    }
    // Every style changes the tone a little, never by more than a few dB.
    for (int style = 0; style < NumStyles; ++style)
    {
        const auto& r = recipe (style);
        for (float v : { r.lowDb, r.mudDb, r.presenceDb, r.airDb })
            check (std::fabs (v) <= 3.0f, "TONE moves stay within 3 dB");
    }
}
} // namespace

int main()
{
    testLoudnessMeter();
    testTransparentAndLatency();
    testStagesDoSomething();
    testCompareAndMono();
    testLearnEdges();
    testLearn();
    testCeilingEverywhere();

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
