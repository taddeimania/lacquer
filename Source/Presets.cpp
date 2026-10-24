#include "Presets.h"
#include "Params.h"
#include "Settings.h"

namespace lacquer
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ids;
    static const std::vector<FactoryPreset> list {
        { "Start Here", "Light all round, aimed at streaming. A safe first pass for anything.",
            { { style, Gentle }, { target, Streaming }, { master, 0.5f } } },
        { "Acoustic", "Guitar and voice: barely touched, so the room and the dynamics survive.",
            { { style, Gentle }, { target, Streaming }, { master, 0.35f } } },
        { "Singer-Songwriter", "A little low-end warmth and a softer top for an intimate vocal.",
            { { style, Warm }, { target, Streaming }, { master, 0.4f } } },
        { "Jazz", "Very light. Jazz listeners notice squashed dynamics before anything else.",
            { { style, Gentle }, { target, Streaming }, { master, 0.25f } } },
        { "Pop Shine", "Clear top end and a slightly wider image for a polished vocal mix.",
            { { style, Bright }, { target, Streaming }, { master, 0.6f } } },
        { "Indie Rock", "Kick and snare kept sharp, the mud between bass and guitars cleared.",
            { { style, Punch }, { target, Streaming }, { master, 0.55f } } },
        { "Hip-Hop", "Weight in the low end and drums that hit, for a release played loud.",
            { { style, Punch }, { target, Download }, { master, 0.65f } } },
        { "Electronic", "Dense and full for a club or a download store, where loud is expected.",
            { { style, Dense }, { target, Download }, { master, 0.6f } } },
        { "Heavy", "Guitars and drums pushed together into one wall, loud enough for CD.",
            { { style, Dense }, { target, Cd }, { master, 0.7f } } },
        { "Lo-Fi Beats", "Warm and a little saturated, top end rolled off, nothing sharp.",
            { { style, Warm }, { target, Streaming }, { master, 0.75f } } },
        { "Ambient", "Barely any compression so swells stay swells; a touch of air.",
            { { style, Gentle }, { target, Streaming }, { master, 0.3f } } },
        { "Bandcamp", "For downloads, which nobody turns down for you: louder than streaming.",
            { { style, Gentle }, { target, Download }, { master, 0.5f } } },
        { "CD Master", "A CD plays at the level you give it, so it can sit louder still.",
            { { style, Punch }, { target, Cd }, { master, 0.55f } } },
        { "Vinyl Pre-Master", "Bass in mono, gentle limiting: something a cutting engineer can work with.",
            { { style, Warm }, { target, Vinyl }, { master, 0.4f } } },
    };
    return list;
}

juce::File userPresetFolder()
{
    return Settings::file().getParentDirectory().getChildFile ("Presets");
}

juce::Array<juce::File> userPresets()
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExtension);
    files.sort();
    return files;
}
} // namespace lacquer
