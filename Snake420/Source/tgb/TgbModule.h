#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <optional>
#include <cstring>
#include "TgbCore.h"
#include "TgbParams.h"
#include "../Global.h"

// The TGB-420 device as a module of SNAKE-420 (was TgbProcessor)
class TgbModule : private juce::Timer
{
public:
    TgbModule (juce::AudioProcessorValueTreeState& state, const s420::GlobalState& global, double& sampleRateRef);
    ~TgbModule() override;

    void prepare (double sr) { engine.prepare (sr); }
    // midiIn: keys/CC for Bass, Arp and Learn. extPitch: Snake's engine in the pitch link (else nullptr)
    void process (const juce::MidiBuffer& midiIn, bool playing, double ppq, double bpm, int numSamples,
                  juce::MidiBuffer& out, bool sendNotes, const snake::Engine* extPitch);
    const std::vector<tgb::Engine::Trigger>& triggers() const { return engine.triggers; }
    bool pitchLinked() const { return global.linkV() == s420::LinkSnakePitchToTgb; }
    bool usesGlobalScale() const { return global.tgbGlobal(); }
    void saveState (juce::ValueTree&) const;
    void loadState (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState& apvts;

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
    void setStatus (const juce::String& s) { status = s; statusTime = juce::Time::getMillisecondCounterHiRes(); }

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
    tgb::Engine engine;
    const s420::GlobalState& global;
    double& sampleRate;
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

};
