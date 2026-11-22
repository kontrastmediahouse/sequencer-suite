#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <optional>
#include "BeeCore.h"
#include "BeeParams.h"

class BeeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BeeProcessor();
    ~BeeProcessor() override;

    void prepareToPlay (double sampleRate, int) override;
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

    juce::AudioProcessorValueTreeState apvts;

    // ---- actions (message thread); all undoable with Back
    void randomize();
    void randomizeLayer (int layer);
    void resetLayer (int layer);
    void goBack();
    void goForward();
    void nudge (int d);
    void seqReset();
    void slotClick (int n, int mod);   // 0 recall, 1 save, 2 clear
    void copySlot (int from, int to);
    void armLearn (bool on);
    juce::File exportClip();

    // ---- read-outs for the UI
    int  param (const juce::String& name) const { return (int) std::lround (apvts.getRawParameterValue (bee::ids::id (name))->load()); }
    int  cellVal (const char* key, int c) const;
    bee::Snapshot readSnapshot() const;
    bee::Params currentParams() const { bee::Params p; fillParams (p); return p; }
    int  uiCell() const  { return uiCellA.load(); }
    int  uiPulse() const { return uiPulseA.load(); }
    int  uiLayerCell (int l) const { return uiLayerCellA[(size_t) l].load(); }
    int  uiHits() const  { return uiHitCount.load(); }
    int  uiHitCell() const { return uiHitCellA.load(); }
    int  lastPitch() const { return uiLastPitch.load(); }
    bool isPlaying() const { return playingA.load(); }
    bool slotFilled (int n) const { return slots[(size_t) n].has_value(); }
    uint16_t chordMask() const { return chordMaskA.load(); }
    int  rollsInWindow() const;
    int  activeSlot = -1, pendingSlot = -1;
    bool saveArm = false, learnArm = false;
    juce::String status; double statusTime = 0;
    void setStatus (const juce::String& s) { status = s; statusTime = juce::Time::getMillisecondCounterHiRes(); }

private:
    void timerCallback() override;
    void fillParams (bee::Params&) const;
    void writeSnapshot (const bee::Snapshot&);
    void setP (const juce::String& fullId, float plain);
    bee::EditContext editContext() const;
    void pushHistory();
    void handleRemote (const juce::MidiMessage&);

    bee::Engine engine;
    bee::Editor editor { (uint32_t) juce::Time::currentTimeMillis() };
    std::array<std::array<std::atomic<float>*, bee::NS>, bee::ids::kNumCellKeys> cellP {};

    std::array<std::optional<bee::Snapshot>, 8> slots;
    struct Hist { bool isSlot = false; int slotN = -1; std::optional<bee::Snapshot> before, after; bee::Snapshot seq; };
    std::vector<Hist> hist, future;

    bee::Snapshot pendingSlotData;
    std::atomic<bool> pendingSlotSet { false }, slotFiredA { false }, overrideActive { false };
    std::atomic<int> uiCellA { -1 }, uiPulseA { 0 }, uiHitCount { 0 }, uiHitCellA { -1 }, uiLastPitch { -1 };
    std::array<std::atomic<int>, bee::kNumLayers> uiLayerCellA {};
    std::atomic<bool> playingA { false }, evolveA { false }, learnArmChanged { false };
    std::atomic<int> learnedCC { -1 }, remoteRoot { -1 };
    std::atomic<bool> remoteRandomAll { false }, remoteRandomPitch { false };
    std::atomic<uint16_t> chordMaskA { 1 };
    int heldChord = 0; bool newChordArmed = true;
    std::array<std::atomic<uint64_t>, 2> heldMask {};
    int lastPlaymode = 0, lastLatch = 0;
    std::vector<double> rollTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeeProcessor)
};
