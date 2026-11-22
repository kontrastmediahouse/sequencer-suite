// Params.h - parameter IDs and layout for the Snake VST3 port
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SnakeEngine.h"

namespace snake::ids
{
inline const char* lanePrefix (int lane)
{
    static const char* p[] = { "notes", "gates", "velo", "cc1", "cc2", "cc3" };
    return p[lane];
}
inline const char* laneTitle (int lane)
{
    static const char* t[] = { "NOTES", "GATES", "VELOCITY", "CC 1", "CC 2", "CC 3" };
    return t[lane];
}

// SNAKE-420: every Snake parameter ID starts with "snake_"
inline juce::String laneParam (int lane, const char* what) { return "snake_" + juce::String (lanePrefix (lane)) + "_" + what; }
inline juce::String stepParam (int lane, int step)         { return "snake_" + juce::String (lanePrefix (lane)) + "_step" + juce::String (step + 1).paddedLeft ('0', 2); }
inline juce::String userKey (int pc)                       { return "snake_user_key" + juce::String (pc); }
inline juce::String ccParam (int ci, const char* what)     { return "snake_cc" + juce::String (ci + 1) + "_" + what; }

inline const juce::String root      { "snake_root" };
inline const juce::String scale     { "snake_scale" };
inline const juce::String swing     { "snake_swing" };
inline const juce::String midiCh    { "snake_midi_channel" };
inline const juce::String midiChord { "snake_user_midi_chord" };

// Device defaults for the 16 note dials (Note_01..Note_16 initial values)
inline constexpr float noteDefaults[16] = { 0.0f, 1.960629921f, 3.267716535f, 5.228346457f, 6.535433071f, 7.995287085f,
                                            11.77734353f, 12.65325593f, 14.56963044f, 15.44554285f, 17.36726249f,
                                            19.42284039f, 21.03546181f, 22.26116443f, 25.11127076f, 25.59670096f };

inline juce::StringArray noteNames() { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }

inline juce::String midiNoteName (int n) // 60 = C3 (Cubase / Live convention)
{
    return noteNames()[n % 12] + juce::String (n / 12 - 2);
}

inline void addParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace juce;
    int version = 1;
    auto pid = [&] (const String& id) { return ParameterID { id, version }; };

    const StringArray dirs   { "Forward", "Backward", "Fore/Back", "Back/Fore", "Random" };
    const StringArray speeds { "1/32", "1/16", "1/8", "1/4", "1/2" };
    StringArray shapes;
    for (int s = 0; s < kNumShapes; ++s) shapes.add (shapeName (s));
    StringArray scaleNames;
    for (auto& s : scales()) scaleNames.add (s.name);
    scaleNames.add ("User");
    StringArray chans;
    for (int c = 1; c <= 16; ++c) chans.add (String (c));

    // ---- global
    layout.add (std::make_unique<AudioParameterChoice> (pid (root),  "Snake Root",  noteNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (pid (scale), "Snake Scale", scaleNames, 1));
    layout.add (std::make_unique<AudioParameterFloat>  (pid (swing), "Snake Swing", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterChoice> (pid (midiCh), "MIDI Channel", chans, 0));
    layout.add (std::make_unique<AudioParameterBool>   (pid (midiChord), "User Scale from MIDI (C3-B3)", false));
    for (int k = 0; k < 12; ++k)
        layout.add (std::make_unique<AudioParameterBool> (pid (userKey (k)), "User Scale " + noteNames()[k], k == 0));

    // ---- lanes
    for (int lane = 0; lane < NumLanes; ++lane)
    {
        const String t = laneTitle (lane);
        auto group = std::make_unique<AudioProcessorParameterGroup> (lanePrefix (lane), t, " | ");

        group->addChild (std::make_unique<AudioParameterChoice> (pid (laneParam (lane, "dir")),   t + " Direction", dirs, 0));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (laneParam (lane, "speed")), t + " Speed", speeds, 1));
        group->addChild (std::make_unique<AudioParameterInt>    (pid (laneParam (lane, "len")),   t + " Steps", 1, 16, 16));
        group->addChild (std::make_unique<AudioParameterFloat>  (pid (laneParam (lane, "shift")), t + " Shift",
                                                                 NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                                                                 AudioParameterFloatAttributes().withLabel ("%")));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (laneParam (lane, "shape")), t + " Shape", shapes, 0));

        if (lane != Gates)
        {
            const int hi = lane == Notes ? 83 : 127;
            group->addChild (std::make_unique<AudioParameterInt> (pid (laneParam (lane, "rmin")), t + " Random Min", 0, hi, 0));
            group->addChild (std::make_unique<AudioParameterInt> (pid (laneParam (lane, "rmax")), t + " Random Max", 0, hi, hi));
        }

        for (int s = 0; s < kNumSteps; ++s)
        {
            const String name = t + " Step " + String (s + 1);
            if (lane == Notes)
                group->addChild (std::make_unique<AudioParameterFloat> (pid (stepParam (lane, s)), name,
                                                                        NormalisableRange<float> (0.0f, 83.0f), noteDefaults[s]));
            else if (lane == Gates)
                group->addChild (std::make_unique<AudioParameterBool> (pid (stepParam (lane, s)), name, true));
            else
                group->addChild (std::make_unique<AudioParameterInt> (pid (stepParam (lane, s)), name, 0, 127, lane == Velocities ? 64 : 0));
        }

        if (lane >= Custom1)
        {
            const int ci = lane - Custom1;
            group->addChild (std::make_unique<AudioParameterBool> (pid (ccParam (ci, "on")),  t + " Enabled", true));
            group->addChild (std::make_unique<AudioParameterInt>  (pid (ccParam (ci, "num")), t + " CC Number", 0, 127, 20 + ci));
            group->addChild (std::make_unique<AudioParameterInt>  (pid (ccParam (ci, "min")), t + " Out Min", 0, 127, 0));
            group->addChild (std::make_unique<AudioParameterInt>  (pid (ccParam (ci, "max")), t + " Out Max", 0, 127, 127));
        }

        layout.add (std::move (group));
    }
}
} // namespace snake::ids
