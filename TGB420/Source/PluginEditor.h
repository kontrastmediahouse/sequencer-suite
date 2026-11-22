#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VintageLookAndFeel.h"

using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

namespace tgbui
{
juce::Colour layerColour (int l);

// A row of small segment buttons bound to a choice parameter (stage mode, divider)
class SegmentBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentBar (TgbProcessor& p, juce::String paramId, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onPick;   // optional override (divider switches mode to II)
    juce::Colour accent = vintage::col::amber;
    int value() const;
private:
    TgbProcessor& proc; juce::String pid; juce::StringArray labels;
};

// The 8 pulse cells of a stage: click a cell to set the length, the only lit cell again skips the stage
class PulseCells : public juce::Component, public juce::SettableTooltipClient
{
public:
    PulseCells (TgbProcessor& p, int stage);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int playingPulse = -1;
private:
    juce::Rectangle<float> cell (int n) const;
    TgbProcessor& proc; int stage;
};

class StageColumn : public juce::Component
{
public:
    StageColumn (TgbProcessor&, int stage);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh (int layer, int playmode, bool playing, int hitFlash, int layerPos, bool inLength);
    int stage;
private:
    TgbProcessor& proc;
    std::array<juce::Slider, 6> knobs;   // pitch, oct, vel, gate, chance, aux
    std::array<std::unique_ptr<SliderAtt>, 6> atts;
    SegmentBar modeBar, divBar;
    PulseCells cells;
    int shownKnob = 0, layer = 0, flash = 0, layerPos = -1;
    bool playing = false, inLength = true;
    juce::String readout; bool snapped = false;
};

class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize") {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    int heat = 0, face = 5;
};

class SlotGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SlotGrid (TgbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int blink = 0;
private:
    int slotAt (juce::Point<float>) const;
    juce::Rectangle<float> slotRect (int n) const;
    TgbProcessor& proc; int dragFrom = -1;
};

class PoolKeys : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PoolKeys (TgbProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> keyRect (int pc) const;
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    TgbProcessor& proc;
};

// Exports the sequence as a .mid file; drag the button into Cubase to drop the clip
class ClipButton : public juce::TextButton
{
public:
    explicit ClipButton (TgbProcessor& p) : juce::TextButton ("MIDI CLIP"), proc (p) {}
    void mouseDrag (const juce::MouseEvent&) override;
private:
    TgbProcessor& proc; bool dragging = false;
};

class Content : public juce::Component, private juce::Timer
{
public:
    explicit Content (TgbProcessor&);
    ~Content() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1320, H = 600;
private:
    void timerCallback() override;
    void renderBackground();
    void setParam (const juce::String& name, int v);
    juce::Slider& knob (juce::Slider&, const char* style, const juce::String& paramName, const juce::String& tip);
    juce::ComboBox& combo (juce::ComboBox&, const juce::String& paramName);

    TgbProcessor& proc;
    vintage::LookAndFeel lnf;
    juce::Image background;
    juce::TooltipWindow tooltips { this, 700 };

    std::array<juce::TextButton, 3> modeBtns;
    std::array<juce::TextButton, 5> layerBtns;
    juce::Slider transpose, swing, bassnote, arpoct, length, pulses, notes, octjump, steps;
    juce::ComboBox clock, direction, scalemode, root, scale, chroll, auxtype, auxcc, glide, arporder, countby, randtarget, pool, evolve, evotarget, midich;
    juce::TextButton nudgeL { "<" }, nudgeR { ">" }, back { "< BACK" }, fwd { "FWD >" }, seqreset { "SEQ RESET" }, learn { "LEARN" },
                     link { "LINK" }, page1 { "1-8" }, page2 { "9-16" }, followBtn { "FOLLOW" }, saveArm { "SAVE" },
                     retrig { "RETRIG" }, latch { "LATCH" }, resetstop { "RESET ON STOP" }, thru { "MIDI THRU" };
    DiceButton dice;
    ClipButton clip;
    SlotGrid slotGrid;
    PoolKeys poolKeys;
    std::array<std::unique_ptr<StageColumn>, tgb::NS> columns;
    std::vector<std::unique_ptr<SliderAtt>> sAtts;
    std::vector<std::unique_ptr<ComboAtt>> cAtts;
    std::vector<std::unique_ptr<ButtonAtt>> bAtts;
    struct Caption { juce::Rectangle<int> r; juce::String text; std::function<bool()> visible; };
    std::vector<Caption> captions;
    int lastHits = 0, flashFrames = 0, lastStatusHash = 0;
    juce::String statusText, transposeText, lengthText, bassText;
};
} // namespace tgbui

class TgbEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer
{
public:
    explicit TgbEditor (TgbProcessor&);
    void resized() override;
private:
    tgbui::Content content;
};
