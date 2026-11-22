// TgbParams.h - parameters of the TGB-420 port. Every ID starts with "tgb_" so the
// device can later live inside a combined plug-in next to other ports.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TgbCore.h"

namespace tgb::ids
{
inline juce::String id (const juce::String& name) { return "tgb_" + name; }
inline const char* stageKey (int k)
{
    static const char* keys[] = { "pitch", "oct", "vel", "gate", "chance", "aux", "mode", "div", "len" };
    return keys[k];
}
constexpr int kNumStageKeys = 9;
inline juce::String stage (const char* key, int i) { return id (juce::String (key) + juce::String (i + 1)); }
inline juce::String llen (int layer)               { return id ("llen" + juce::String (layer + 1)); }
inline juce::String cust (int pc)                  { return id ("cust" + juce::String (pc)); }

inline juce::StringArray noteNames() { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
inline juce::String layerName (int l) { static const char* n[] = { "PITCH", "VELO", "GATE", "CHANCE", "AUX" }; return n[l]; }

// common CC names for the Aux menu
inline juce::String ccName (int cc)
{
    switch (cc)
    {
        case 0: return "CC 0 Bank (!)"; case 1: return "CC 1 Mod Wheel"; case 2: return "CC 2 Breath";
        case 4: return "CC 4 Foot"; case 5: return "CC 5 Porta Time"; case 7: return "CC 7 Volume";
        case 8: return "CC 8 Balance"; case 10: return "CC 10 Pan"; case 11: return "CC 11 Expression";
        case 32: return "CC 32 Bank LSB (!)"; case 64: return "CC 64 Sustain"; case 65: return "CC 65 Portamento";
        case 71: return "CC 71 Resonance"; case 72: return "CC 72 Release"; case 73: return "CC 73 Attack";
        case 74: return "CC 74 Cutoff"; case 75: return "CC 75 Decay"; case 76: return "CC 76 Vib Rate";
        case 77: return "CC 77 Vib Depth"; case 91: return "CC 91 Reverb"; case 93: return "CC 93 Chorus";
        default: return cc >= 120 ? "CC " + juce::String (cc) + " (!)" : "CC " + juce::String (cc);
    }
}

inline void addParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace juce;
    auto pid = [] (const String& name) { return ParameterID { id (name), 1 }; };
    auto choice = [&] (const String& n, const String& label, StringArray items, int def)
    { return std::make_unique<AudioParameterChoice> (pid (n), label, items, def); };
    auto integer = [&] (const String& n, const String& label, int lo, int hi, int def)
    { return std::make_unique<AudioParameterInt> (pid (n), label, lo, hi, def); };
    auto boolean = [&] (const String& n, const String& label, bool def)
    { return std::make_unique<AudioParameterBool> (pid (n), label, def); };

    StringArray arps; for (int i = 0; i < 17; ++i) arps.add (arpName (i));
    StringArray scaleNames; for (auto& s : scales()) scaleNames.add (s.name);
    StringArray pools; for (int i = 0; i < 8; ++i) pools.add (poolName (i));
    StringArray ccs; for (int i = 0; i < 128; ++i) ccs.add (ccName (i));
    StringArray chans; for (int i = 1; i <= 16; ++i) chans.add (String (i));

    // ---- stages
    for (int i = 0; i < NS; ++i)
    {
        auto g = std::make_unique<AudioProcessorParameterGroup> ("stage" + String (i + 1), "Stage " + String (i + 1), " | ");
        const String s = " " + String (i + 1);
        g->addChild (integer ("pitch" + String (i + 1),  "Pitch" + s, -36, 36, 0));
        g->addChild (integer ("oct" + String (i + 1),    "Octave" + s, -2, 2, 0));
        g->addChild (integer ("vel" + String (i + 1),    "Velocity" + s, 1, 127, 100));
        g->addChild (integer ("gate" + String (i + 1),   "Gate" + s, 5, 100, 50));
        g->addChild (integer ("chance" + String (i + 1), "Chance" + s, 0, 100, 100));
        g->addChild (integer ("aux" + String (i + 1),    "Aux" + s, 0, 127, 0));
        g->addChild (choice  ("mode" + String (i + 1),   "Mode" + s, { "Rest", "I", "II", "I-" }, 1));
        g->addChild (choice  ("div" + String (i + 1),    "Divider" + s, { "1", "2", "3", "4" }, 0));
        g->addChild (integer ("len" + String (i + 1),    "Pulses" + s, 0, 8, 1));
        layout.add (std::move (g));
    }

    // ---- global
    layout.add (choice  ("layer", "Layer", { "Pitch", "Velocity", "Gate", "Chance", "Aux" }, 0));
    layout.add (choice  ("playmode", "Play Mode", { "Seq", "Bass", "Arp" }, 0));
    layout.add (choice  ("clock", "Clock", { "1/32", "1/16", "1/8", "1/4" }, 1));
    layout.add (integer ("swing", "Swing", 0, 100, 0));
    layout.add (integer ("transpose", "Transpose", -24, 24, 0));
    layout.add (integer ("bassnote", "Bass Note", 0, 127, 36));
    layout.add (boolean ("retrig", "Retrigger", true));
    layout.add (choice  ("arporder", "Arp Style", arps, 0));
    layout.add (integer ("arpoct", "Arp Octaves", 1, 4, 1));
    layout.add (boolean ("latch", "Arp Latch", false));
    layout.add (integer ("length", "Stages", 1, 16, 8));
    layout.add (choice  ("countby", "Count By", { "Stage", "Pulse" }, 0));
    layout.add (integer ("pulses", "Cycle Pulses", 1, 64, 16));
    layout.add (choice  ("direction", "Direction", { "Fwd", "Rev", "Ping", "Rand" }, 0));
    layout.add (choice  ("scalemode", "Scale Mode", { "Off", "Custom" }, 0));
    layout.add (choice  ("root", "Scale Root", noteNames(), 0));
    layout.add (choice  ("scale", "Scale", scaleNames, 0));
    layout.add (choice  ("chroll", "Chance Roll", { "Per pulse", "Per stage" }, 0));
    layout.add (choice  ("auxtype", "Aux Target", { "CC", "Pitch Bend", "Aftertouch" }, 0));
    layout.add (choice  ("auxcc", "Aux CC", ccs, 74));
    layout.add (choice  ("glide", "Aux Glide", { "Step", "Glide" }, 0));
    layout.add (boolean ("resetstop", "Reset On Stop", true));
    layout.add (boolean ("thru", "MIDI Thru", false));
    layout.add (choice  ("randtarget", "Randomize Target", { "Layer", "Rhythm", "All" }, 0));
    layout.add (choice  ("pool", "Note Pool", pools, 0));
    layout.add (integer ("notes", "Notes", 1, 8, 8));
    layout.add (integer ("octjump", "Octave Jumps", 0, 100, 0));
    layout.add (choice  ("evolve", "Evolve", { "Off", "8 bars", "16 bars", "32 bars", "64 bars" }, 0));
    layout.add (choice  ("evotarget", "Evolve Target", { "Pitch", "Rhythm", "Both" }, 0));
    layout.add (choice  ("midich", "MIDI Channel", chans, 0));
    for (int l = 0; l < kNumLayers; ++l)
        layout.add (integer ("llen" + String (l + 1), StringArray { "Pitch", "Velocity", "Gate", "Chance", "Aux" }[l] + " Steps", 0, 16, 0));
    for (int c = 0; c < 12; ++c)
        layout.add (boolean ("cust" + String (c), "Custom Pool " + noteNames()[c], c == 0 || c == 3 || c == 7));
}
} // namespace tgb::ids
