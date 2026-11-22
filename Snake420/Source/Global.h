// Global.h - SNAKE-420 machine-wide settings shared by the modules
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "snake/SnakeEngine.h"

namespace s420
{
// Global scale list: Snake's 36 scales + natural minor (TGB-420's "Minor")
inline int globalScaleCount() { return (int) snake::scales().size() + 1; }
inline juce::String globalScaleName (int i)
{
    if (i < (int) snake::scales().size()) return snake::scales()[(size_t) i].name;
    return "NaturalMinor";
}
inline int globalScalePcs (int i, int* out)
{
    if (i >= 0 && i < (int) snake::scales().size())
    {
        const auto& v = snake::scales()[(size_t) i].pcs;
        for (size_t k = 0; k < v.size(); ++k) out[k] = v[k];
        return (int) v.size();
    }
    static const int nat[] = { 0, 2, 3, 5, 7, 8, 10 };
    for (int k = 0; k < 7; ++k) out[k] = nat[k];
    return 7;
}

enum Link { LinkOff = 0, LinkSnakePitchToTgb, LinkTgbClockToSnake };

namespace ids
{
inline const juce::String root       { "g_root" };
inline const juce::String scale      { "g_scale" };
inline const juce::String snakeSrc   { "g_snake_scale_src" };   // 0 Global, 1 Own
inline const juce::String tgbSrc     { "g_tgb_scale_src" };
inline const juce::String link       { "g_link" };
inline const juce::String snakeOut   { "g_snake_out" };
inline const juce::String tgbOut     { "g_tgb_out" };
inline const juce::String channel    { "g_midi_channel" };
inline const juce::String midiIn     { "g_midi_in" };            // 0 TGB keys, 1 Snake remote, 2 both
}

inline void addGlobalParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace juce;
    StringArray sc; for (int i = 0; i < globalScaleCount(); ++i) sc.add (globalScaleName (i));
    StringArray chans; for (int c = 1; c <= 16; ++c) chans.add (String (c));
    auto g = std::make_unique<AudioProcessorParameterGroup> ("global", "Machine", " | ");
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::root, 1 }, "Global Root",
                 StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Global Scale", sc, 1));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::snakeSrc, 1 }, "Snake Scale Source", StringArray { "Global", "Own" }, 0));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::tgbSrc, 1 }, "TGB Scale Source", StringArray { "Global", "Own" }, 1));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::link, 1 }, "Link",
                 StringArray { "Off", "Snake Pitch > TGB", "TGB Clock > Snake" }, 0));
    g->addChild (std::make_unique<AudioParameterBool> (ParameterID { ids::snakeOut, 1 }, "Snake Out", true));
    g->addChild (std::make_unique<AudioParameterBool> (ParameterID { ids::tgbOut, 1 }, "TGB Out", true));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::channel, 1 }, "MIDI Channel", chans, 0));
    g->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::midiIn, 1 }, "MIDI In",
                 StringArray { "TGB keys", "Snake remote", "Both" }, 0));
    layout.add (std::move (g));
}

// Read-only view of the machine settings the modules need
struct GlobalState
{
    std::atomic<float>* root = nullptr; std::atomic<float>* scale = nullptr;
    std::atomic<float>* snakeSrc = nullptr; std::atomic<float>* tgbSrc = nullptr;
    std::atomic<float>* link = nullptr; std::atomic<float>* channel = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s)
    {
        root = s.getRawParameterValue (ids::root); scale = s.getRawParameterValue (ids::scale);
        snakeSrc = s.getRawParameterValue (ids::snakeSrc); tgbSrc = s.getRawParameterValue (ids::tgbSrc);
        link = s.getRawParameterValue (ids::link); channel = s.getRawParameterValue (ids::channel);
    }
    int  rootV() const    { return (int) root->load(); }
    int  scaleV() const   { return (int) scale->load(); }
    bool snakeGlobal() const { return snakeSrc->load() < 0.5f; }
    bool tgbGlobal() const   { return tgbSrc->load() < 0.5f; }
    int  linkV() const    { return (int) link->load(); }
    int  channelV() const { return (int) channel->load() + 1; }
};
} // namespace s420
