#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VintageLookAndFeel.h"

using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

// One cell of the 4x4 grid: knob (or toggle switch for gates) + step LED + value readout
class StepCell : public juce::Component
{
public:
    StepCell (SnakeProcessor&, int lane, int step);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    void setState (bool playing, bool inWindow, int pathNumber, const juce::String& readout);

private:
    int lane, step;
    juce::Slider knob;
    juce::ToggleButton toggle;
    std::unique_ptr<SliderAtt> knobAtt;
    std::unique_ptr<ButtonAtt> toggleAtt;
    bool playing = false, inWindow = true;
    int pathNumber = 0;
    juce::String readout;
};

// 4x4 dot matrix that draws the snake path of the selected shape
class ShapeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (int shape, int length, int offset, int playingCell);
    void paint (juce::Graphics&) override;
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
private:
    int shape = 0, length = 16, offset = 0, cell = -1;
};

// 12-key user-scale keyboard
class ScaleKeyboard : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ScaleKeyboard (SnakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
private:
    juce::Rectangle<float> keyRect (int pc) const;
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    SnakeProcessor& proc;
    uint16_t shownMask = 0;
    bool midiMode = false;
};

// Lane controls (direction, speed, steps, shift, shape, random range, CC settings)
class LanePanel : public juce::Component
{
public:
    LanePanel (SnakeProcessor&, int lane);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    SnakeProcessor& proc;
    int lane;
    juce::ComboBox dirBox, speedBox, shapeBox;
    juce::TextButton prevShape { "<" }, nextShape { ">" }, randomBtn { "RANDOM" }, resetBtn { "RESET" };
    juce::Slider steps, shift, rmin, rmax, ccNum, ccMin, ccMax;
    juce::ToggleButton ccOn;
    ShapeView shapeView;
    std::vector<std::unique_ptr<SliderAtt>> sAtt;
    std::unique_ptr<ComboAtt> dirAtt, speedAtt, shapeAtt;
    std::unique_ptr<ButtonAtt> ccOnAtt;
    struct Caption { juce::Rectangle<int> r; juce::String text; };
    std::vector<Caption> captions;
    int ccSignature = -1;
};

// Everything at the 1100x580 design size; the editor scales it.
class SnakeContent : public juce::Component, private juce::Timer
{
public:
    explicit SnakeContent (SnakeProcessor&);
    ~SnakeContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1100, H = 580;

private:
    void timerCallback() override;
    void selectLane (int lane);
    void renderBackground();

    SnakeProcessor& proc;
    vintage::LookAndFeel lnf;
    juce::Image background;
    juce::TooltipWindow tooltips { this, 700 };

    std::array<std::unique_ptr<juce::TextButton>, snake::NumLanes> laneButtons;
    juce::Slider rootKnob, swingKnob;
    juce::ComboBox scaleBox, chanBox;
    juce::TextButton randomAll { "RANDOM ALL" }, resetAll { "RESET ALL" };
    juce::ToggleButton chordSwitch;
    std::unique_ptr<SliderAtt> rootAtt, swingAtt;
    std::unique_ptr<ComboAtt> scaleAtt, chanAtt;
    std::unique_ptr<ButtonAtt> chordAtt;

    std::array<std::unique_ptr<LanePanel>, snake::NumLanes> lanePanels;
    std::array<std::array<std::unique_ptr<StepCell>, 16>, snake::NumLanes> cells;
    ScaleKeyboard keyboard;

    int shownLane = 0;
    int lastFlash = 0, flashFrames = 0;
    juce::String rootText, playingText;
};

class SnakeEditor : public juce::AudioProcessorEditor
{
public:
    explicit SnakeEditor (SnakeProcessor&);
    void resized() override;
private:
    SnakeContent content;
};
