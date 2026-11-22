#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VintageLookAndFeel.h"
#include "snake/SnakeUI.h"
#include "tgb/TgbUI.h"

// Live view of both modules: Snake's 4x4 grid and TGB-420's 16 stages
class MonitorView : public juce::Component
{
public:
    explicit MonitorView (MachineProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    MachineProcessor& proc;
    int lastSig = -1;
};

// The drawing that shows which way the link runs
class LinkDiagram : public juce::Component
{
public:
    explicit LinkDiagram (MachineProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    MachineProcessor& proc;
};

class MachineContent : public juce::Component, private juce::Timer
{
public:
    explicit MachineContent (MachineProcessor&);
    ~MachineContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1320, H = 800, StripH = 214;
private:
    void timerCallback() override;
    void showModule (int m);
    void renderBackground();

    MachineProcessor& proc;
    vintage::LookAndFeel lnf;
    juce::Image background;
    juce::TooltipWindow tooltips { this, 700 };

    juce::ComboBox rootBox, scaleBox, chanBox, inBox;
    std::array<juce::TextButton, 3> linkBtns;
    juce::TextButton snakeOut { "SNAKE OUT" }, tgbOut { "TGB OUT" };
    std::array<juce::TextButton, 2> pageBtns;
    LinkDiagram diagram;
    MonitorView monitor;
    SnakeContent snakePage;
    tgbui::Content tgbPage;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtts;
    int lastLink = -1, lastNotes = 0, flash = 0;
    juce::String nowText;
};

class MachineEditor : public juce::AudioProcessorEditor
{
public:
    explicit MachineEditor (MachineProcessor&);
    void resized() override;
private:
    MachineContent content;
};
