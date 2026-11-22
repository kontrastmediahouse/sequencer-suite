#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SnakeEngine.h"
#include "Params.h"

class SnakeProcessor : public juce::AudioProcessor,
                       private juce::Timer
{
public:
    SnakeProcessor();
    ~SnakeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    void randomizeLane (int lane);
    void resetLane (int lane);
    void randomizeAll() { for (int l = 0; l < snake::NumLanes; ++l) randomizeLane (l); }
    void resetAll()     { for (int l = 0; l < snake::NumLanes; ++l) resetLane (l); }

    int  getCurrentCell (int lane) const { return uiCell[(size_t) lane].load(); }
    int  getLastPitch() const            { return uiPitch.load(); }
    int  getNoteFlash() const            { return uiNoteCounter.load(); }
    uint16_t getMidiChordMask() const    { return midiChordMask.load(); }
    int  quantizedNoteForStep (int step) const;   // for the note-name displays
    int  quantizeValue (float dialValue) const;

    int selectedLane = 0;  // UI state, saved with the project

private:
    void timerCallback() override;
    void fillParams (snake::EngineParams&) const;
    void setParam (const juce::String& id, float plainValue);
    void handleMidiInput (const juce::MidiBuffer&);

    snake::Engine engine;

    // cached parameter pointers
    struct LaneRefs { std::atomic<float>* dir; std::atomic<float>* speed; std::atomic<float>* len;
                      std::atomic<float>* shift; std::atomic<float>* shape; std::array<std::atomic<float>*, 16> steps; };
    std::array<LaneRefs, snake::NumLanes> laneRefs {};
    std::atomic<float>* pRoot = nullptr; std::atomic<float>* pScale = nullptr; std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pMidiCh = nullptr; std::atomic<float>* pMidiChord = nullptr;
    std::array<std::atomic<float>*, 12> pUserKeys {};
    std::array<std::atomic<float>*, 3> pCcOn {}, pCcNum {}, pCcMin {}, pCcMax {};

    // audio -> message thread
    std::array<std::atomic<int>, snake::NumLanes> uiCell {};
    std::atomic<int> uiPitch { -1 }, uiNoteCounter { 0 };
    std::atomic<int> pendingRoot { -1 };
    std::atomic<bool> pendingRandomAll { false }, pendingRandomNotes { false };
    std::atomic<uint16_t> midiChordMask { 1 };
    int heldChordNotes = 0;
    bool newChordArmed = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnakeProcessor)
};
