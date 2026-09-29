#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The LOWW mark: a soft bass wave that bends into a W, with a fainter echo of it beneath for the
// lower bands. Drawn as vectors so the editor header, app icon and README logo share one source.
namespace loww {
inline const juce::Colour logoCyan {0xff22ecff}, logoCyanDeep {0xff00b3cc};

inline juce::Path logoWave(juce::Rectangle<float> r, float lift = 0) {
    const auto p = [&](float x, float y) {
        return juce::Point<float>(r.getX() + x * r.getWidth(), r.getY() + (y + lift) * r.getHeight());
    };
    juce::Path wave;
    wave.startNewSubPath(p(.17f, .31f));
    wave.cubicTo(p(.23f, .31f), p(.26f, .63f), p(.335f, .63f));
    wave.cubicTo(p(.41f, .63f), p(.43f, .39f), p(.5f, .39f));
    wave.cubicTo(p(.57f, .39f), p(.59f, .63f), p(.665f, .63f));
    wave.cubicTo(p(.74f, .63f), p(.77f, .31f), p(.83f, .31f));
    return wave;
}

// withTile draws the standalone badge (app icon, docs); the editor supplies its own neumorphic tile.
inline void drawLogoMark(juce::Graphics& g, juce::Rectangle<float> r, bool withTile) {
    const auto size = r.getWidth();
    if (withTile) {
        juce::Path tile; tile.addRoundedRectangle(r, size * .235f);
        juce::DropShadow(juce::Colour(0x8c000000), juce::roundToInt(size * .07f), {0, juce::roundToInt(size * .03f)}).drawForPath(g, tile);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b414b), r.getTopLeft(), juce::Colour(0xff1f2228), r.getBottomRight(), false));
        g.fillPath(tile);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(.14f), r.getTopLeft(), juce::Colours::transparentWhite, r.getCentre(), false));
        g.strokePath(tile, juce::PathStrokeType(size * .012f));
    }
    const auto weight = size * .085f;
    const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;
    const auto round = juce::PathStrokeType::rounded;
    g.setColour(logoCyanDeep.withAlpha(.3f));
    g.strokePath(logoWave(r, .17f), juce::PathStrokeType(weight * .45f, joint, round));
    const auto wave = logoWave(r);
    juce::Path outline;
    juce::PathStrokeType(weight, joint, round).createStrokedPath(outline, wave);
    juce::DropShadow(logoCyan.withAlpha(.55f), juce::jmax(2, juce::roundToInt(size * .07f)), {}).drawForPath(g, outline);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff9ff8ff), r.getTopLeft(), logoCyan, r.getCentre(), false));
    g.fillPath(outline);
}
}
