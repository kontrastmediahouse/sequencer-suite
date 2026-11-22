// SNAKE-420: Snake + TGB-420 as linked modules in one MIDI machine
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Global.h"
#include "snake/SnakeModule.h"
#include "tgb/TgbModule.h"

class MachineProcessor : public juce::AudioProcessor
{
public:
    MachineProcessor();
    ~MachineProcessor() override = default;

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
    s420::GlobalState global;
    double sampleRateValue = 48000.0;
    SnakeModule snake;
    TgbModule tgb;
    int selectedModule = 1;          // UI: 0 Snake page, 1 TGB-420 page
    int lastPitchOut() const { return lastPitch.load(); }
    int notesOut() const     { return noteCounter.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int lastLink = -1;
    std::vector<snake::Engine::Trigger> trig;
    std::atomic<int> lastPitch { -1 }, noteCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachineProcessor)
};
