// SnakeModule.h - the Snake device as a module of SNAKE-420 (was SnakeProcessor)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SnakeEngine.h"
#include "SnakeParams.h"
#include "../Global.h"

class SnakeModule : private juce::Timer
{
public:
    SnakeModule (juce::AudioProcessorValueTreeState& state, const s420::GlobalState& global);
    ~SnakeModule() override;

    juce::AudioProcessorValueTreeState& apvts;

    void prepare (double sampleRate) { engine.prepare (sampleRate); }
    void handleMidiInput (const juce::MidiBuffer&);

    // own transport clock (link Off / Snake pitch > TGB)
    void process (const snake::Transport&, int numSamples, juce::MidiBuffer& out, bool sendNotes);
    // TGB clock > Snake
    void processTriggers (const std::vector<snake::Engine::Trigger>&, bool playing, double ppq, double bpm,
                          int numSamples, juce::MidiBuffer& out, bool sendNotes);

    // ---- editor API (message thread)
    void randomizeLane (int lane);
    void resetLane (int lane);
    void randomizeAll() { for (int l = 0; l < snake::NumLanes; ++l) randomizeLane (l); }
    void resetAll()     { for (int l = 0; l < snake::NumLanes; ++l) resetLane (l); }
    int  getCurrentCell (int lane) const { return uiCell[(size_t) lane].load(); }
    int  getLastPitch() const            { return uiPitch.load(); }
    int  getNoteFlash() const            { return uiNoteCounter.load(); }
    uint16_t getMidiChordMask() const    { return midiChordMask.load(); }
    int  quantizedNoteForStep (int step) const;
    int  quantizeValue (float dialValue) const;
    bool usesGlobalScale() const         { return global.snakeGlobal(); }
    int  selectedLane = 0;

    void saveState (juce::ValueTree&) const;
    void loadState (const juce::ValueTree&);

    snake::Engine engine;   // public: the TGB module reads its pitch in the pitch link

private:
    void timerCallback() override;
    void fillParams (snake::EngineParams&) const;
    void setParam (const juce::String& id, float plainValue);
    template <typename Fn> void emitTo (juce::MidiBuffer& out, bool sendNotes, Fn&& run);

    const s420::GlobalState& global;
    struct LaneRefs { std::atomic<float>* dir; std::atomic<float>* speed; std::atomic<float>* len;
                      std::atomic<float>* shift; std::atomic<float>* shape; std::array<std::atomic<float>*, 16> steps; };
    std::array<LaneRefs, snake::NumLanes> laneRefs {};
    std::atomic<float>* pRoot = nullptr; std::atomic<float>* pScale = nullptr; std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pMidiChord = nullptr;
    std::array<std::atomic<float>*, 12> pUserKeys {};
    std::array<std::atomic<float>*, 3> pCcOn {}, pCcNum {}, pCcMin {}, pCcMax {};

    std::array<std::atomic<int>, snake::NumLanes> uiCell {};
    std::atomic<int> uiPitch { -1 }, uiNoteCounter { 0 };
    std::atomic<int> pendingRoot { -1 };
    std::atomic<bool> pendingRandomAll { false }, pendingRandomNotes { false };
    std::atomic<uint16_t> midiChordMask { 1 };
    int heldChordNotes = 0;
    bool newChordArmed = true;
};
