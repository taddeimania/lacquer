#include "PluginEditor.h"
#include "Presets.h"

using namespace lacquer;
using namespace lacquer::ui;
using juce::Rectangle;
using juce::String;

namespace
{
// Section boxes (design space).
const Rectangle<float> kStyleBox  { 12.0f, 92.0f, 330.0f, 380.0f };
const Rectangle<float> kMasterBox { 354.0f, 92.0f, 492.0f, 380.0f };
const Rectangle<float> kTargetBox { 858.0f, 92.0f, 330.0f, 380.0f };
const Rectangle<float> kHoodBox   { 12.0f, 490.0f, 1176.0f, 150.0f };
const Rectangle<float> kLoudBox   { 12.0f, 658.0f, 1176.0f, 130.0f };

const Rectangle<int> kStyleText  { 30, 362, 294, 100 };
const Rectangle<int> kTargetText { 876, 362, 294, 100 };
const Rectangle<int> kMasterText { 380, 428, 440, 34 };
const Rectangle<int> kLearnText  { 30, 744, 150, 40 };
const Rectangle<int> kCompareText { 910, 726, 266, 56 };

const char* kStyleBlurbs[NumStyles] {
    "Light on everything: a touch of air and low end, barely any compression. Start here; it suits almost anything.",
    "More low end, a softer top and some tape-like saturation. For vocals, acoustic music and lo-fi.",
    "Clears the muddy low mids and lifts presence and air, a little wider. For pop, and for mixes that sound dull.",
    "Firmer low end, and a compressor slow enough to let drum hits through before it acts. For rock and hip-hop.",
    "Faster compression, more saturation, everything pushed together. For electronic and heavy music." };

const char* kTargetBlurbs[NumTargets] {
    "Spotify, Apple Music, YouTube and Tidal turn loud songs down to about -14 LUFS anyway. Going louder gains nothing and costs punch.",
    "Bandcamp and downloaded files play at the level you give them, so masters for them usually sit louder than streaming.",
    "A CD plays exactly what's on it. Loud is normal, and the ceiling can sit closer to 0 because nothing re-encodes it.",
    "For a cutting engineer: moderate level, bass in mono below 150 Hz so the needle tracks. They set the final level." };
} // namespace

// ===========================================================================
LacquerPanel::LacquerPanel (LacquerProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    setSize (designW, designH);

    // --- header ------------------------------------------------------------------
    presetChoice.getText = [this]
    {
        const int n = proc.presetNumber();
        return (n > 0 ? String (n).paddedLeft ('0', 2) + "   " : String()) + proc.presetName().toUpperCase()
             + (proc.presetEdited() ? "  *" : "");
    };
    presetChoice.fontH = 18.0f;
    presetChoice.onOpen = [this] { openPresetMenu(); };
    presetChoice.setTooltip ("Presets: a STYLE, TARGET and MASTER setting for a kind of music. "
                             "Loading one keeps what LEARN found. A * means you've changed it since loading.");
    addAndMakeVisible (presetChoice);
    presetChoice.setBounds (586, 14, 300, 46);

    addButton (prevBtn, { 540, 16, 40, 42 }, "Previous preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (-1); });
    addButton (nextBtn, { 892, 16, 40, 42 }, "Next preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (1); });
    prevBtn.setFontHeight (18.0f);
    nextBtn.setFontHeight (18.0f);
    addButton (saveBtn, { 946, 20, 66, 34 }, "Save these settings as a preset of your own.",
               [this] (const juce::ModifierKeys&) { savePresetDialog(); });
    saveBtn.setFontHeight (11.0f);
    addButton (tipsBtn, { 1022, 20, 62, 34 }, "Show or hide these tips (remembered for every Lacquer).",
               [this] (const juce::ModifierKeys&)
               {
                   settings.setTooltipsEnabled (! settings.tooltipsEnabled());
                   syncTooltips();
               });
    tipsBtn.setFontHeight (11.0f);

    // --- style -----------------------------------------------------------------------
    for (int s = 0; s < NumStyles; ++s)
    {
        auto& b = styleBtns[(size_t) s];
        b.setLabel (ids::styleNames()[s]);
        b.setFontHeight (14.0f);
        addButton (b, { 30, 114 + s * 46, 294, 38 }, kStyleBlurbs[s],
                   [this, s] (const juce::ModifierKeys&) { proc.setParam (ids::style, (float) s); });
    }

    // --- master ----------------------------------------------------------------------
    addKnob (masterKnob, ids::master, "MASTER", { 450, 106, 300, 316 },
             "How much of the STYLE to apply, to every stage at once. At 0 nothing is shaped and only "
             "the loudness is set. Most songs want somewhere between 30% and 70%.");
    masterKnob.big = true;
    masterKnob.setMouseDragSensitivity (360);

    // --- target ----------------------------------------------------------------------
    for (int t = 0; t < NumTargets; ++t)
    {
        auto& b = targetBtns[(size_t) t];
        b.setLabel (ids::targetNames()[t]);
        b.setFontHeight (14.0f);
        addButton (b, { 876, 114 + t * 46, 294, 38 }, kTargetBlurbs[t],
                   [this, t] (const juce::ModifierKeys&) { proc.setParam (ids::target, (float) t); });
    }

    // --- under the hood --------------------------------------------------------------
    addAndMakeVisible (strip);
    strip.setBounds (24, 504, 1152, 126);

    // --- loudness ----------------------------------------------------------------------
    addButton (learnBtn, { 30, 676, 150, 62 },
               "Press, then play the song from the start (the whole song is best). Lacquer listens and sets "
               "its levels so the master lands on the TARGET. It stops by itself when the song ends, "
               "or press LEARN again.",
               [this] (const juce::ModifierKeys&) { proc.toggleLearn(); });
    learnBtn.setFontHeight (20.0f);

    addKnob (pushKnob, ids::push, "PUSH", { 190, 668, 96, 116 },
             "Louder or quieter than the target, up to 6 dB either way. Louder squeezes more out of the limiter.");
    pushKnob.small = true;
    pushKnob.bipolar = true;

    addAndMakeVisible (loudness);
    loudness.setBounds (298, 672, 598, 108);
    loudness.setTooltip ("Loudness of the master, measured the way streaming services measure it. "
                         "INTEGRATED is the whole song since you pressed play; SHORT-TERM the last 3 seconds "
                         "(the bar, with the target in red); MOMENTARY the last 0.4 s. TRUE PEAK is the highest "
                         "peak, including between samples; red means it went over the ceiling.");

    addToggle (compareBtn, ids::compare, { 910, 676, 130, 44 },
               "Hear the original mix, turned to the same loudness as the master. Louder always sounds better, "
               "so this is the only fair way to tell whether the master is actually an improvement.");
    compareBtn.setFontHeight (14.0f);
    addToggle (monoBtn, ids::mono, { 1048, 676, 60, 44 },
               "Listen in mono, as on a phone speaker or a club system. If something disappears, it's too wide.");
    addButton (resetBtn, { 1116, 676, 60, 44 }, "Start the INTEGRATED reading and the TRUE PEAK hold again. "
               "They also reset whenever you press play.",
               [this] (const juce::ModifierKeys&) { proc.resetMeters(); });
    resetBtn.setFontHeight (11.0f);

    startTimerHz (30);
    timerCallback();
}

LacquerPanel::~LacquerPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void LacquerPanel::addKnob (Knob& k, const char* id, const String& caption, Rectangle<int> r, const String& tip)
{
    k.caption = caption;
    k.setTooltip (tip);
    if (auto* param = proc.apvts.getParameter (id))
        k.format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    addAndMakeVisible (k);
    k.setBounds (r);
    attachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, id, k));
}

void LacquerPanel::addToggle (PanelButton& b, const char* id, Rectangle<int> r, const String& tip)
{
    addButton (b, r, tip, [this, id] (const juce::ModifierKeys&)
    {
        proc.setParam (id, proc.getParam (id) > 0.5f ? 0.0f : 1.0f);
    });
    toggles.push_back ({ &b, id });
}

void LacquerPanel::addButton (PanelButton& b, Rectangle<int> r, const String& tip,
                              std::function<void (const juce::ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    b.setBounds (r);
}

String LacquerPanel::masterLine() const
{
    const int pct = juce::roundToInt (proc.getParam (ids::master) * 100.0f);
    if (pct == 0)
        return "Nothing shaped: only the limiter, setting the loudness.";
    return "Every stage at " + String (pct) + "% of the " + ids::styleNames()[proc.geti (ids::style)] + " recipe.";
}

// ---------------------------------------------------------------------------
void LacquerPanel::openPresetMenu()
{
    juce::PopupMenu m;
    const int cur = proc.presetNumber();
    m.addSectionHeader ("FACTORY");
    const auto& f = factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
        m.addItem (String (i + 1).paddedLeft ('0', 2) + "   " + f[(size_t) i].name, true, cur == i + 1,
                   [this, i] { proc.loadFactory (i); });

    const auto users = userPresets();
    m.addSectionHeader ("YOURS");
    if (users.isEmpty())
        m.addItem ("(none yet: press SAVE)", false, false, [] {});
    for (int i = 0; i < users.size(); ++i)
    {
        const auto file = users[i];
        const int n = (int) f.size() + i + 1;
        m.addItem (String (n).paddedLeft ('0', 2) + "   " + file.getFileNameWithoutExtension(), true, cur == n,
                   [this, file] { proc.loadUserPreset (file); });
    }
    m.addSeparator();
    m.addItem ("Save as...", [this] { savePresetDialog(); });
    m.addItem ("Show presets folder", []
    {
        const auto folder = userPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetChoice));
}

void LacquerPanel::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name it. Saving under an existing name replaces it.",
                                                      juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presetName());
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            String error;
            if (! proc.saveUserPreset (saveWindow->getTextEditorContents ("name"), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", error);
        }
        saveWindow.reset();
    }), false);
}

void LacquerPanel::syncTooltips()
{
    const bool on = settings.tooltipsEnabled();
    tipsBtn.setLit (on);
    if ((int) on != tooltipsApplied && onTooltips)
    {
        tooltipsApplied = (int) on;
        onTooltips (on);
    }
}

// ---------------------------------------------------------------------------
void LacquerPanel::timerCallback()
{
    presetChoice.refresh();
    for (auto& [b, id] : toggles)
        b->setLit (proc.getParam (id) > 0.5f);

    const int style = proc.geti (ids::style), target = proc.geti (ids::target);
    for (int s = 0; s < NumStyles; ++s)
        styleBtns[(size_t) s].setLit (s == style);
    for (int t = 0; t < NumTargets; ++t)
        targetBtns[(size_t) t].setLit (t == target);
    if (style != shownStyle)  { shownStyle = style;  repaint (kStyleText); }
    if (target != shownTarget) { shownTarget = target; repaint (kTargetText.withY (300).withHeight (170)); }

    const auto t = proc.takeTelemetry();
    strip.update (t);
    loudness.update (t);

    // LEARN lights while it listens, and blinks until the first LEARN.
    flash = (flash + 1) % 30;
    learnBtn.setLit (t.learning || (! proc.hasLearned() && flash < 15));

    const auto ml = masterLine();
    if (ml != shownMaster) { shownMaster = ml; repaint (kMasterText); }
    const auto ls = proc.learnStatus();
    if (ls != shownLearn) { shownLearn = ls; repaint (kLearnText); }
}

// ---------------------------------------------------------------------------
void LacquerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    // The name.
    drawText (g, "LACQUER", { 24.0f, 14.0f, 200.0f, 46.0f }, 38.0f, colours::brand, true, juce::Justification::centredLeft);
    drawText (g, "mastering assistant", { 188.0f, 22.0f, 170.0f, 34.0f }, 17.0f, colours::ink, true,
              juce::Justification::centredLeft);

    drawSection (g, kStyleBox, "STYLE");
    drawSection (g, kMasterBox, "MASTER");
    drawSection (g, kTargetBox, "TARGET");
    drawSection (g, kHoodBox, "UNDER THE HOOD");
    drawSection (g, kLoudBox, "LOUDNESS");

    auto blurb = [&] (const String& text, Rectangle<int> r, juce::Justification j = juce::Justification::topLeft)
    {
        g.setColour (colours::inkDim);
        g.setFont (panelFont (13.0f, false));
        g.drawFittedText (text, r, j, 5, 1.0f);
    };

    const int style = proc.geti (ids::style), target = proc.geti (ids::target);
    blurb (kStyleBlurbs[juce::jlimit (0, NumStyles - 1, style)], kStyleText);

    // Target: what it aims for, as an LCD line, then why.
    const auto& ts = targetSpec (target);
    const auto lcd = Rectangle<float> (876.0f, 306.0f, 294.0f, 40.0f);
    g.setColour (colours::black);
    g.fillRect (lcd);
    g.setColour (colours::inkDim);
    g.drawRect (lcd, 1.0f);
    drawText (g, String (ts.lufs, 0) + " LUFS     " + String (ts.ceilingDb, 1) + " dBTP", lcd, 18.0f, colours::lcd, true);
    blurb (kTargetBlurbs[juce::jlimit (0, NumTargets - 1, target)], kTargetText);

    // MASTER's scale ends and what it's doing.
    drawText (g, "0", { 452.0f, 362.0f, 40.0f, 18.0f }, 13.0f, colours::inkDim, true);
    drawText (g, "100", { 708.0f, 362.0f, 40.0f, 18.0f }, 13.0f, colours::inkDim, true);
    blurb (shownMaster, kMasterText, juce::Justification::centredTop);

    g.setColour (colours::inkDim);
    g.setFont (panelFont (11.0f, false));
    g.drawFittedText (shownLearn.isNotEmpty() ? shownLearn : String ("Play the song while it listens."),
                      kLearnText, juce::Justification::topLeft, 3, 1.0f);
    g.drawFittedText ("COMPARE plays your mix at the master's loudness: the fair test. MONO checks the mix "
                      "still works on one speaker.", kCompareText, juce::Justification::topLeft, 4, 1.0f);
}

// ===========================================================================
LacquerEditor::LacquerEditor (LacquerProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);

    panel.onTooltips = [this] (bool on)
    {
        if (on && tooltips == nullptr)
            tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
        else if (! on)
            tooltips.reset();
    };
    panel.syncTooltips();

    setResizable (true, true);
    const double ratio = (double) LacquerPanel::designW / LacquerPanel::designH;
    setResizeLimits (760, (int) (760 / ratio), 2400, (int) (2400 / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);
    setSize (1000, (int) (1000 / ratio));
}

void LacquerEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void LacquerEditor::resized()
{
    const float s = (float) getWidth() / (float) LacquerPanel::designW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, LacquerPanel::designW, LacquerPanel::designH);
}
