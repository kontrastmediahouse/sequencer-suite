// BeeParams.h - Bumblebee parameters (all IDs start with "bee_")
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BeeCore.h"

namespace bee::ids
{
inline juce::String id (const juce::String& n) { return "bee_" + n; }
// per-cell values
inline const char* cellKey (int k)
{
    static const char* keys[] = { "pitch", "oct", "vel", "gate", "chance", "mode", "div", "len", "moda", "modb", "modc", "modd" };
    return keys[k];
}
constexpr int kNumCellKeys = 12;
inline juce::String cell (const char* key, int c) { return id (juce::String (key) + juce::String (c + 1)); }
// layer settings
inline const char* layerKey (int l) { static const char* k[] = { "pitch", "vel", "gate", "chance", "moda", "modb", "modc", "modd" }; return k[l]; }
inline juce::String layerName (int l) { static const char* n[] = { "PITCH", "VELO", "GATE", "CHANCE", "MOD A", "MOD B", "MOD C", "MOD D" }; return n[l]; }
inline juce::String layer (int l, const char* what) { return id ("L" + juce::String (layerKey (l)) + "_" + what); }
inline juce::String mod (int m, const char* what) { return id ("mod" + juce::String::charToString ((juce::juce_wchar) ('a' + m)) + "_" + what); }
inline juce::String userKey (int pc) { return id ("user" + juce::String (pc)); }
inline juce::String cust (int pc)    { return id ("cust" + juce::String (pc)); }

inline juce::StringArray noteNames() { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
inline juce::StringArray shapeNames (bool withFollow)
{
    juce::StringArray s; if (withFollow) s.add ("Follow");
    for (int i = 0; i < kNumShapes; ++i) s.add (shapeName (i));
    return s;
}
inline juce::String ccName (int cc)
{
    switch (cc)
    {
        case 0: return "CC 0 Bank (!)"; case 1: return "CC 1 Mod Wheel"; case 2: return "CC 2 Breath"; case 7: return "CC 7 Volume";
        case 10: return "CC 10 Pan"; case 11: return "CC 11 Expression"; case 32: return "CC 32 Bank LSB (!)"; case 64: return "CC 64 Sustain";
        case 71: return "CC 71 Resonance"; case 72: return "CC 72 Release"; case 73: return "CC 73 Attack"; case 74: return "CC 74 Cutoff";
        case 75: return "CC 75 Decay"; case 91: return "CC 91 Reverb"; case 93: return "CC 93 Chorus";
        default: return cc >= 120 ? "CC " + juce::String (cc) + " (!)" : "CC " + juce::String (cc);
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const String& n) { return ParameterID { id (n), 1 }; };
    auto choice  = [&] (const String& n, const String& l, StringArray items, int d) { return std::make_unique<AudioParameterChoice> (pid (n), l, items, d); };
    auto integer = [&] (const String& n, const String& l, int lo, int hi, int d) { return std::make_unique<AudioParameterInt> (pid (n), l, lo, hi, d); };
    auto boolean = [&] (const String& n, const String& l, bool d) { return std::make_unique<AudioParameterBool> (pid (n), l, d); };

    StringArray scales; for (auto& s : scaleList()) scales.add (s.name);
    StringArray arps; for (int i = 0; i < 17; ++i) arps.add (arpName (i));
    StringArray pools; for (int i = 0; i < 8; ++i) pools.add (poolName (i));
    StringArray ccs; for (int i = 0; i < 128; ++i) ccs.add (ccName (i));
    StringArray chans; for (int i = 1; i <= 16; ++i) chans.add (String (i));
    const StringArray dirs { "Fwd", "Rev", "Ping", "Rand" };

    for (int c = 0; c < NS; ++c)
    {
        auto g = std::make_unique<AudioProcessorParameterGroup> ("cell" + String (c + 1), "Stage " + String (c + 1), " | ");
        const String n = String (c + 1), s = " " + n;
        g->addChild (integer ("pitch" + n, "Pitch" + s, -36, 36, 0));
        g->addChild (integer ("oct" + n, "Octave" + s, -2, 2, 0));
        g->addChild (integer ("vel" + n, "Velocity" + s, 1, 127, 100));
        g->addChild (integer ("gate" + n, "Gate" + s, 5, 100, 50));
        g->addChild (integer ("chance" + n, "Chance" + s, 0, 100, 100));
        g->addChild (choice  ("mode" + n, "Mode" + s, { "Rest", "I", "II", "I-" }, 1));
        g->addChild (choice  ("div" + n, "Divider" + s, { "1", "2", "3", "4" }, 0));
        g->addChild (integer ("len" + n, "Pulses" + s, 0, 8, 1));
        for (int m = 0; m < kNumMods; ++m)
            g->addChild (integer (String ("mod") + String::charToString ((juce_wchar) ('a' + m)) + n, "Mod " + String::charToString ((juce_wchar) ('A' + m)) + s, 0, 127, 0));
        layout.add (std::move (g));
    }
    for (int l = 0; l < kNumLayers; ++l)
    {
        const String k = "L" + String (layerKey (l)) + "_", nm = layerName (l) + " ";
        auto g = std::make_unique<AudioProcessorParameterGroup> ("layer" + String (l), nm + "Layer", " | ");
        g->addChild (integer (k + "steps", nm + "Steps", 0, 16, 0));
        g->addChild (choice  (k + "path", nm + "Path", shapeNames (true), 0));
        g->addChild (integer (k + "shift", nm + "Shift", 0, 100, 0));
        g->addChild (choice  (k + "dir", nm + "Direction", { "Follow", "Fwd", "Rev", "Ping", "Rand" }, 0));
        g->addChild (choice  (k + "rate", nm + "Rate", { "Stage", "1/32", "1/16", "1/8", "1/4", "1/2" }, 0));
        const int lo[] = { -36, 1, 5, 0, 0, 0, 0, 0 }, hi[] = { 36, 127, 100, 100, 127, 127, 127, 127 };
        g->addChild (integer (k + "rlo", nm + "Random Low", lo[l], hi[l], lo[l]));
        g->addChild (integer (k + "rhi", nm + "Random High", lo[l], hi[l], hi[l]));
        layout.add (std::move (g));
    }
    for (int m = 0; m < kNumMods; ++m)
    {
        const String k = String ("mod") + String::charToString ((juce_wchar) ('a' + m)) + "_", nm = "Mod " + String::charToString ((juce_wchar) ('A' + m)) + " ";
        layout.add (boolean (k + "on", nm + "On", m == 0));
        layout.add (choice  (k + "kind", nm + "Target", { "CC", "Pitch Bend", "Aftertouch" }, 0));
        layout.add (choice  (k + "cc", nm + "CC", ccs, m == 0 ? 74 : 19 + m));
        layout.add (integer (k + "lo", nm + "Out Min", 0, 127, 0));
        layout.add (integer (k + "hi", nm + "Out Max", 0, 127, 127));
        layout.add (choice  (k + "glide", nm + "Glide", { "Step", "Glide" }, 0));
    }
    // sequence
    layout.add (integer ("length", "Stages", 1, 16, 8));
    layout.add (choice  ("direction", "Direction", dirs, 0));
    layout.add (integer ("transpose", "Transpose", -24, 24, 0));
    layout.add (choice  ("path", "Path", shapeNames (false), 0));
    layout.add (integer ("shift", "Shift", 0, 100, 0));
    layout.add (choice  ("countby", "Count By", { "Stage", "Pulse" }, 0));
    layout.add (integer ("pulses", "Cycle Pulses", 1, 64, 16));
    // play
    layout.add (choice  ("layer", "Layer", { "Pitch", "Velocity", "Gate", "Chance", "Mod A", "Mod B", "Mod C", "Mod D" }, 0));
    layout.add (choice  ("playmode", "Play Mode", { "Seq", "Bass", "Arp" }, 0));
    layout.add (choice  ("clock", "Clock", { "1/32", "1/16", "1/8", "1/4" }, 1));
    layout.add (integer ("swing", "Swing", 0, 100, 0));
    layout.add (integer ("bassnote", "Bass Note", 0, 127, 36));
    layout.add (boolean ("retrig", "Retrigger", true));
    layout.add (choice  ("arporder", "Arp Style", arps, 0));
    layout.add (integer ("arpoct", "Arp Octaves", 1, 4, 1));
    layout.add (boolean ("latch", "Arp Latch", false));
    layout.add (choice  ("chroll", "Chance Roll", { "Per pulse", "Per stage" }, 0));
    // scale
    layout.add (choice  ("scale", "Scale", scales, 0));
    layout.add (choice  ("root", "Root", noteNames(), 0));
    layout.add (choice  ("pitchmode", "Pitch Mode", { "Semitone", "Degree" }, 0));
    layout.add (boolean ("chord", "User Scale from MIDI Chord", false));
    for (int k = 0; k < 12; ++k) layout.add (boolean ("user" + String (k), "User Scale " + noteNames()[k], k == 0 || k == 2 || k == 4 || k == 7 || k == 9));
    // randomize + patterns
    layout.add (choice  ("randtarget", "Randomize Target", { "Layer", "Rhythm", "All", "Paths" }, 0));
    layout.add (choice  ("pool", "Note Pool", pools, 0));
    layout.add (integer ("notes", "Notes", 1, 8, 8));
    layout.add (integer ("octjump", "Octave Jumps", 0, 100, 0));
    layout.add (choice  ("evolve", "Evolve", { "Off", "8 bars", "16 bars", "32 bars", "64 bars" }, 0));
    layout.add (choice  ("evotarget", "Evolve Target", { "Pitch", "Rhythm", "Both" }, 0));
    for (int c = 0; c < 12; ++c) layout.add (boolean ("cust" + String (c), "Custom Pool " + noteNames()[c], c == 0 || c == 3 || c == 7));
    // I/O
    layout.add (choice  ("midich", "MIDI Channel", chans, 0));
    layout.add (choice  ("midiin", "MIDI In", { "Play", "Remote" }, 0));
    layout.add (boolean ("thru", "MIDI Thru", false));
    layout.add (boolean ("resetstop", "Reset On Stop", true));
    return layout;
}
} // namespace bee::ids
