#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <optional>
#include <cstring>
#include "TgbCore.h"
#include "TgbParams.h"

class TgbProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    TgbProcessor();
    ~TgbProcessor() override;

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

    // ---- actions (message thread), each undoable with Back
    void randomize();
    void goBack();
    void goForward();
    void nudge (int d);
    void seqReset();
    void slotClick (int n, int mod);   // mod: 0 recall, 1 save, 2 clear
    void copySlot (int from, int to);
    void armLearn (bool on)            { learnArm = on; learnArmChanged = true; setStatus (on ? "Turn a knob to learn" : "Learn off"); }
    juce::File exportClip();           // writes a .mid file, returns it (or an invalid File)

    // ---- UI read-outs
    int  stageVal (const char* key, int i) const;
    int  param (const juce::String& name) const;
    int  effLen (int layer) const;
    tgb::Snapshot readSnapshot() const;
    int  uiStage() const      { return uiStageA.load(); }
    int  uiPulse() const      { return uiPulseA.load(); }
    int  uiLayerPos (int l) const { return uiLayerPosA[(size_t) l].load(); }
    int  uiHits() const       { return uiHitCount.load(); }
    int  uiHitStage() const   { return uiHitStageA.load(); }
    bool slotFilled (int n) const { return slots[(size_t) n].has_value(); }
    int  activeSlot = -1, pendingSlot = -1;
    bool saveArm = false, learnArm = false;
    int  page = 0; bool follow = false;
    juce::String status;
    double statusTime = 0;
    int  rollsInWindow() const;
    int  histSize() const { return (int) hist.size(); }
    bool isPlaying() const { return playingA.load(); }

private:
    void timerCallback() override;
    void fillParams (tgb::Params&) const;
    void writeSnapshot (const tgb::Snapshot&);
    void setP (const juce::String& id, float plain);
    tgb::EditContext editContext() const;
    void pushHistory();
    void setStatus (const juce::String& s) { status = s; statusTime = juce::Time::getMillisecondCounterHiRes(); }

    tgb::Engine engine;
    tgb::Editor editor { (uint32_t) juce::Time::currentTimeMillis() };

    // cached parameter pointers
    std::array<std::array<std::atomic<float>*, tgb::NS>, tgb::ids::kNumStageKeys> stageP {};
    std::map<juce::String, std::atomic<float>*> gp;
    float g (const char* name) const { return gp.at (name)->load(); }

    // pattern slots and history
    std::array<std::optional<tgb::Snapshot>, 8> slots;
    struct Hist { bool isSlot = false; int slotN = -1; std::optional<tgb::Snapshot> before, after; tgb::Snapshot seq; };
    std::vector<Hist> hist, future;

    // slot queued while playing: the engine switches at the cycle start
    tgb::Snapshot pendingSlotData;
    std::atomic<bool> pendingSlotSet { false }, slotFiredA { false }, overrideActive { false };

    // audio -> UI
    std::atomic<int> uiStageA { -1 }, uiPulseA { 0 }, uiHitCount { 0 }, uiHitStageA { -1 };
    std::array<std::atomic<int>, tgb::kNumLayers> uiLayerPosA {};
    std::atomic<bool> playingA { false }, evolveA { false };
    std::atomic<int> learnedCC { -1 };
    std::atomic<bool> learnArmChanged { false };
    std::array<std::atomic<uint64_t>, 2> heldMask {};
    int lastPlaymode = 0, lastLatch = 0;
    std::vector<double> rollTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TgbProcessor)
};
