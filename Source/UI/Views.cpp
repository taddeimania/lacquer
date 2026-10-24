#include "Views.h"

namespace lacquer::ui
{
using namespace juce;

namespace
{
constexpr int kSegments = 14;

String db1 (float v)
{
    const float r = std::round (v * 10.0f) / 10.0f;
    return (r > 0.0f ? "+" : r < 0.0f ? "-" : "") + String (std::abs (r), 1);
}

const char* kStageTips[NumStages] {
    "TONE: a gentle EQ. It clears rumble below 20 Hz, then shapes the lows, the muddy low mids, "
    "the presence and the air, by a dB or two at most. The curve is what it's doing now. Click to switch it off.",
    "GLUE: a slow compressor that pulls loud moments down a little so the mix holds together. "
    "The number is how much it's turning down right now. Click to switch it off.",
    "COLOR: soft saturation, like pushing tape. It rounds off peaks and adds harmonics, "
    "which reads as warmth and density. Click to switch it off.",
    "WIDTH: makes the sides a touch wider, and puts the deep bass in the middle so it's solid on "
    "any speaker (and cuttable on vinyl). Click to switch it off.",
    "LIMIT: turns the whole thing up to the target loudness and stops any peak going over the "
    "ceiling. Off also removes that boost, so you'll hear it get quieter. Click to switch it off." };
} // namespace

// ===========================================================================
// Meter
// ===========================================================================
void Meter::push (float peak)
{
    level = std::max (peak, level * 0.82f);
    if (peak >= hold) { hold = peak; holdFrames = 24; }
    else if (--holdFrames < 0) hold *= 0.9f;

    auto segs = [] (float v)
    {
        if (v <= 0.0f) return 0;
        const float db = gainToDb (v);
        return jlimit (0, kSegments, (int) std::ceil ((db + 42.0f) / 42.0f * kSegments));
    };
    const int s = segs (level), h = segs (hold);
    if (s != shownSegs || h != shownHold)
    {
        shownSegs = s;
        shownHold = h;
        repaint();
    }
}

void Meter::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float segH = r.getHeight() / (float) kSegments;
    for (int i = 0; i < kSegments; ++i)
    {
        const auto seg = Rectangle<float> (r.getX(), r.getBottom() - (float) (i + 1) * segH, r.getWidth(), segH).reduced (0.0f, 1.5f);
        const Colour c = i >= kSegments - 1 ? colours::ledRed : i >= kSegments - 4 ? colours::ledYellow : colours::ledGreen;
        const bool lit = i < shownSegs || i == shownHold - 1;
        g.setColour (lit ? c : colours::socket);
        g.fillRect (seg);
    }
}

// ===========================================================================
// StageStrip
// ===========================================================================
StageStrip::StageStrip (LacquerProcessor& p) : proc (p)
{
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    inMeter.setInterceptsMouseClicks (false, false);
    outMeter.setInterceptsMouseClicks (false, false);
}

void StageStrip::resized()
{
    inMeter.setBounds (14, 18, 12, getHeight() - 28);
    outMeter.setBounds (getWidth() - 26, 18, 12, getHeight() - 28);
}

Rectangle<float> StageStrip::blockBounds (int s) const
{
    const float left = 56.0f, right = (float) getWidth() - 56.0f;
    const float gap = 30.0f;
    const float w = (right - left - gap * (float) (NumStages - 1)) / (float) NumStages;
    return { left + (float) s * (w + gap), 6.0f, w, (float) getHeight() - 12.0f };
}

String StageStrip::bigText (int s) const
{
    switch (s)
    {
        case ToneStage:
        {
            // The two biggest moves.
            const char* names[] { "LOW", "MUD", "PRES", "AIR" };
            int a = 0, b = 1;
            for (int i = 0; i < 4; ++i)
                if (std::abs (t.toneDb[(size_t) i]) > std::abs (t.toneDb[(size_t) a])) a = i;
            b = a == 0 ? 1 : 0;
            for (int i = 0; i < 4; ++i)
                if (i != a && std::abs (t.toneDb[(size_t) i]) > std::abs (t.toneDb[(size_t) b])) b = i;
            if (std::abs (t.toneDb[(size_t) a]) < 0.05f) return "FLAT";
            String r = String (names[a]) + " " + db1 (t.toneDb[(size_t) a]);
            if (std::abs (t.toneDb[(size_t) b]) >= 0.05f) r << "   " << names[b] << " " << db1 (t.toneDb[(size_t) b]);
            return r;
        }
        case GlueStage:  return t.glueGrDb < 0.05f ? String ("0.0 dB") : "-" + String (t.glueGrDb, 1) + " dB";
        case ColorStage: return String (roundToInt (t.colorDrive * 100.0f)) + "%";
        case WidthStage: return String (roundToInt (t.widthPct)) + "%";
        case LimitStage: return t.limitGrDb < 0.05f ? String ("0.0 dB") : "-" + String (t.limitGrDb, 1) + " dB";
        default: return {};
    }
}

String StageStrip::smallText (int s) const
{
    switch (s)
    {
        case ToneStage:  return "rumble cut below 20 Hz";
        case GlueStage:  return t.glueRatio < 1.02f ? String ("resting (MASTER at 0)")
                                                    : String (t.glueRatio, 1) + ":1 from " + String (roundToInt (t.glueThresholdDb)) + " dB";
        case ColorStage: return t.colorDb < 0.05f ? String ("clean") : "rounds peaks by " + String (t.colorDb, 1) + " dB";
        case WidthStage: return "bass in mono below " + String (roundToInt (t.monoBassHz)) + " Hz";
        case LimitStage: return "push " + db1 (t.driveDb) + " dB, ceiling " + db1 (t.ceilingDb);
        default: return {};
    }
}

float StageStrip::barValue (int s) const
{
    if (s == GlueStage)  return jlimit (0.0f, 1.0f, t.glueGrDb / 8.0f);
    if (s == LimitStage) return jlimit (0.0f, 1.0f, t.limitGrDb / 8.0f);
    if (s == ColorStage) return jlimit (0.0f, 1.0f, t.colorDb / 4.0f);
    return -1.0f;
}

void StageStrip::update (const Telemetry& tel)
{
    t = tel;
    inMeter.push (t.inPeak);
    outMeter.push (t.outPeak);
    String key;
    for (int s = 0; s < NumStages; ++s)
        key << (proc.getParam (ids::stageOn (s)) > 0.5f ? "1" : "0") << bigText (s) << smallText (s)
            << roundToInt (barValue (s) * 40.0f) << "|";
    if (key != shownKey)
    {
        shownKey = key;
        repaint();
    }
}

void StageStrip::drawToneCurve (Graphics& g, Rectangle<float> r, float alpha) const
{
    const double sr = 48000.0;
    Biquad f[5];
    f[0].highPass (kRumbleHz, 0.7071, sr);
    f[1].lowShelf (kLowHz, t.toneDb[0], sr);
    f[2].peak (kMudHz, 0.9, t.toneDb[1], sr);
    f[3].peak (kPresenceHz, 0.8, t.toneDb[2], sr);
    f[4].highShelf (kAirHz, t.toneDb[3], sr);

    const float range = 4.0f;   // +-4 dB fills the box
    g.setColour (colours::inkDim.withAlpha (0.4f * alpha));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    Path p;
    const int steps = 64;
    for (int i = 0; i <= steps; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / steps);   // 20 Hz .. 20 kHz
        double db = 0;
        for (auto& b : f) db += b.responseDb (hz, sr);
        const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
        const float y = r.getCentreY() - r.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, (float) db / range);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colours::lcd.withAlpha (alpha));
    g.strokePath (p, PathStrokeType (2.0f));
}

void StageStrip::paint (Graphics& g)
{
    drawText (g, "IN", { 0.0f, 0.0f, 40.0f, 14.0f }, 10.0f, colours::inkDim, true);
    drawText (g, "OUT", { (float) getWidth() - 40.0f, 0.0f, 40.0f, 14.0f }, 10.0f, colours::inkDim, true);

    // The signal line, with arrows into each stage.
    const float cy = (float) getHeight() * 0.5f;
    g.setColour (colours::ink.withAlpha (0.7f));
    g.drawLine (32.0f, cy, (float) getWidth() - 32.0f, cy, 1.5f);
    for (int s = 0; s <= NumStages; ++s)
    {
        const float x = s < NumStages ? blockBounds (s).getX() - 4.0f : (float) getWidth() - 34.0f;
        Path arrow;
        arrow.addTriangle (x - 9.0f, cy - 5.0f, x - 9.0f, cy + 5.0f, x, cy);
        g.fillPath (arrow);
    }

    for (int s = 0; s < NumStages; ++s)
    {
        const auto r = blockBounds (s);
        const bool on = proc.getParam (ids::stageOn (s)) > 0.5f;
        const float a = on ? 1.0f : 0.35f;
        g.setColour (colours::black);
        g.fillRect (r);
        g.setColour (colours::inkDim);
        g.drawRect (r, 1.0f);
        drawLed (g, { r.getX() + 16.0f, r.getY() + 16.0f }, 5.0f, colours::stage (s), on);
        drawText (g, ids::stageNames()[s], { r.getX() + 28.0f, r.getY() + 7.0f, r.getWidth() - 36.0f, 18.0f }, 13.0f,
                  colours::stage (s).withAlpha (a), true, Justification::centredLeft);

        auto body = r.withTrimmedTop (28.0f).reduced (10.0f, 6.0f);
        if (! on)
        {
            drawText (g, "OFF", body, 24.0f, colours::lcd.withAlpha (a), true);
            continue;
        }
        if (s == ToneStage)
        {
            drawToneCurve (g, body.removeFromTop (body.getHeight() - 30.0f).reduced (2.0f, 2.0f), 1.0f);
            drawText (g, bigText (s), body.removeFromTop (16.0f), 12.0f, colours::lcd, true);
            drawText (g, smallText (s), body, 10.0f, colours::inkDim, false);
            continue;
        }
        const float bar = barValue (s);
        drawText (g, bigText (s), body.removeFromTop (body.getHeight() * 0.48f), 26.0f, colours::lcd, true);
        if (bar >= 0.0f)
        {
            auto br = body.removeFromTop (10.0f).reduced (6.0f, 2.0f);
            g.setColour (colours::socket);
            g.fillRect (br);
            g.setColour (colours::stage (s));
            g.fillRect (br.withWidth (br.getWidth() * bar));
        }
        else
            body.removeFromTop (10.0f);
        drawText (g, smallText (s), body, 11.0f, colours::inkDim, false);
    }
}

void StageStrip::mouseDown (const MouseEvent& e)
{
    for (int s = 0; s < NumStages; ++s)
        if (blockBounds (s).contains (e.position))
        {
            const char* id = ids::stageOn (s);
            proc.setParam (id, proc.getParam (id) > 0.5f ? 0.0f : 1.0f);
            shownKey = {};
            update (t);
        }
}

String StageStrip::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();
    for (int s = 0; s < NumStages; ++s)
        if (blockBounds (s).contains (pos))
            return kStageTips[s];
    return "What each stage is doing to your track, in the order the sound goes through them. "
           "Click a stage to switch it off and hear what it adds.";
}

// ===========================================================================
// LoudnessDisplay
// ===========================================================================
LoudnessDisplay::LoudnessDisplay (LacquerProcessor& p) : proc (p) {}

String LoudnessDisplay::statusText() const
{
    if (t.learning)
        return "LEARNING  " + String (roundToInt (t.learnSeconds)) + " s   play the whole song, then press LEARN again";
    if (proc.getParam (ids::compare) > 0.5f)
        return "COMPARE: the original mix, turned " + db1 (t.matchDb) + " dB to match";
    if (t.integrated <= LoudnessMeter::kSilence)
        return proc.hasLearned() ? "play the song to measure it" : "press LEARN, then play the song";
    const float target = targetSpec (proc.geti (ids::target)).lufs + proc.getParam (ids::push);
    const float diff = t.integrated - target;
    if (std::abs (diff) < 0.5f)
        return "ON TARGET";
    return String (std::abs (diff), 1) + (diff < 0 ? " LU UNDER TARGET" : " LU OVER TARGET")
         + (proc.hasLearned() ? "" : "   (press LEARN)");
}

void LoudnessDisplay::update (const Telemetry& tel)
{
    t = tel;
    tpMax = t.truePeakDb;
    const String key = String (t.integrated, 1) + String (t.shortTerm, 1) + String (t.momentary, 0) + String (tpMax, 1)
                     + statusText() + String (proc.geti (ids::target)) + String (proc.getParam (ids::push), 1);
    if (key != shownKey)
    {
        shownKey = key;
        repaint();
    }
}

void LoudnessDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::black);
    g.fillRect (r);
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
    r = r.reduced (14.0f, 8.0f);

    const auto& ts = targetSpec (proc.geti (ids::target));
    const float target = ts.lufs + proc.getParam (ids::push);
    auto lufsText = [] (float v) { return v <= LoudnessMeter::kSilence ? String ("--.-") : String (v, 1); };

    // Integrated, large, on the left.
    auto left = r.removeFromLeft (150.0f);
    drawText (g, "INTEGRATED", left.removeFromTop (14.0f), 11.0f, colours::inkDim, true, Justification::centredLeft);
    drawText (g, lufsText (t.integrated), left.removeFromTop (44.0f), 42.0f, colours::lcd, true, Justification::centredLeft);
    drawText (g, "LUFS   TARGET " + String (target, 1), left.removeFromTop (14.0f), 11.0f, colours::inkDim, true,
              Justification::centredLeft);

    r.removeFromLeft (16.0f);
    auto status = r.removeFromBottom (18.0f);

    // Short-term bar from -36 to -4 LUFS with the target marked.
    auto bar = r.removeFromTop (24.0f).withTrimmedTop (4.0f);
    const float lo = -36.0f, hi = -4.0f;
    auto xOf = [&] (float v) { return bar.getX() + bar.getWidth() * jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); };
    g.setColour (colours::socket);
    g.fillRect (bar);
    if (t.shortTerm > lo)
    {
        const float over = t.shortTerm - target;
        g.setColour (std::abs (over) < 1.0f ? colours::ledGreen : over > 0 ? colours::ledYellow : colours::lcd.withAlpha (0.8f));
        g.fillRect (bar.withRight (xOf (t.shortTerm)));
    }
    if (t.momentary > lo)
    {
        g.setColour (colours::ink.withAlpha (0.8f));
        g.fillRect (Rectangle<float> (xOf (t.momentary) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
    }
    g.setColour (colours::brand);
    g.fillRect (Rectangle<float> (xOf (target) - 1.5f, bar.getY() - 4.0f, 3.0f, bar.getHeight() + 8.0f));
    for (float v = -36.0f; v <= -4.0f; v += 4.0f)
        drawText (g, String ((int) v), { xOf (v) - 14.0f, bar.getBottom() + 1.0f, 28.0f, 11.0f }, 9.0f, colours::inkDim, false);

    // Readouts.
    r.removeFromTop (14.0f);
    auto row = r.removeFromTop (34.0f);
    const float colW = row.getWidth() / 3.0f;
    auto readout = [&] (const String& label, const String& value, Colour c)
    {
        auto cell = row.removeFromLeft (colW);
        drawText (g, label, cell.removeFromTop (12.0f), 10.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, value, cell, 20.0f, c, true, Justification::centredLeft);
    };
    readout ("SHORT-TERM", lufsText (t.shortTerm), colours::lcd);
    readout ("MOMENTARY", lufsText (t.momentary), colours::lcd);
    const bool over = tpMax > ts.ceilingDb + 0.05f;
    readout ("TRUE PEAK", tpMax < -99.0f ? String ("--.-") : String (tpMax, 1), over ? colours::ledRed : colours::lcd);

    const auto st = statusText();
    const bool good = st == "ON TARGET";
    drawText (g, st, status, 12.0f, good ? colours::ledGreen : colours::lcd, true, Justification::centredLeft);
}
} // namespace lacquer::ui
