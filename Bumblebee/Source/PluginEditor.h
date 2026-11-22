#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VintageLookAndFeel.h"

namespace beeui
{
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

juce::Colour layerColour (int l);

class SegmentBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentBar (BeeProcessor& p, juce::String fullId, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onPick;
    int value() const;
private:
    BeeProcessor& proc; juce::String pid; juce::StringArray labels;
};

class PulseRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    PulseRow (BeeProcessor& p, int cell);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int playingPulse = -1;
private:
    BeeProcessor& proc; int cell;
};

// one stage of the 4x4 grid
class CellView : public juce::Component
{
public:
    CellView (BeeProcessor&, int cell);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh (int layer, int playmode, int pathPos, bool inWindow, bool playing, bool layerHere, int flash);
    const int cell;
private:
    BeeProcessor& proc;
    std::array<juce::Slider, 9> knobs;   // pitch, oct, vel, gate, chance, mod A-D
    std::array<std::unique_ptr<SliderAtt>, 9> atts;
    SegmentBar modeBar, divBar;
    PulseRow pulses;
    int shown = 0, layer = 0, pathPos = 0, flash = 0;
    bool inWindow = true, playing = false, layerHere = false;
    juce::String readout; bool snapped = false;
};

// grid background: honeycomb well + the snake path(s)
class GridView : public juce::Component
{
public:
    explicit GridView (BeeProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    std::array<std::unique_ptr<CellView>, bee::NS> cells;
private:
    juce::Point<float> centreOf (int cell) const;
    BeeProcessor& proc;
    int sig = 0;
};

// small 4x4 path picture bound to a choice parameter (optionally with "Follow" first)
class PathPicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    PathPicker (BeeProcessor& p, std::function<juce::String()> idFn, bool hasFollow) : proc (p), id (std::move (idFn)), follow (hasFollow) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<int()> lengthFn, offsetFn, fallbackShape;
    juce::Colour colour = vintage::col::amber;
private:
    BeeProcessor& proc; std::function<juce::String()> id; bool follow;
};

class KeyRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    KeyRow (BeeProcessor& p, juce::String prefix) : proc (p), prefix (std::move (prefix)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<bool()> dimmed;
    std::function<int()> maskOverride;   // >= 0: show this mask (MIDI chord) and ignore clicks
private:
    juce::Rectangle<float> keyRect (int pc) const;
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    BeeProcessor& proc; juce::String prefix;
};

class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button ("Randomize") {}
    void paintButton (juce::Graphics&, bool, bool) override;
    int heat = 0, face = 5;
};

class SlotGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SlotGrid (BeeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { dragFrom = slotAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override;
    int blink = 0;
private:
    int slotAt (juce::Point<float>) const;
    juce::Rectangle<float> slotRect (int n) const;
    BeeProcessor& proc; int dragFrom = -1;
};

// all 8 layers at a glance; click a row to edit that layer
class LayerMatrix : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LayerMatrix (BeeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    BeeProcessor& proc;
};

class ClipButton : public juce::TextButton
{
public:
    explicit ClipButton (BeeProcessor& p) : juce::TextButton ("MIDI CLIP"), proc (p) {}
    void mouseDrag (const juce::MouseEvent&) override;
private:
    BeeProcessor& proc; bool dragging = false;
};

class Content : public juce::Component, private juce::Timer
{
public:
    explicit Content (BeeProcessor&);
    ~Content() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1400, H = 800;
private:
    void timerCallback() override;
    void renderBackground();
    void bindLayerControls();
    void setParam (const juce::String& fullId, float v);
    juce::Slider& knob (juce::Slider&, const char* style, const juce::String& tip);
    void attach (juce::Slider&, const juce::String& fullId);
    void attach (juce::ComboBox&, const juce::String& fullId);
    void attach (juce::Button&, const juce::String& fullId);

    BeeProcessor& proc;
    vintage::LookAndFeel lnf;
    juce::Image background;
    juce::TooltipWindow tooltips { this, 700 };

    GridView grid;
    std::array<juce::TextButton, 3> modeBtns;
    std::array<juce::TextButton, bee::kNumLayers> layerBtns;
    juce::Slider transpose, swing, bassnote, arpoct, length, shift, pulses, notes, octjump;
    juce::ComboBox clock, direction, countby, arporder, scale, root, randtarget, pool, evolve, evotarget, midich, midiin;
    juce::TextButton nudgeL { "<" }, nudgeR { ">" }, back { "< BACK" }, fwd { "FWD >" }, seqreset { "SEQ RESET" },
                     retrig { "RETRIG" }, latch { "LATCH" }, thru { "THRU" }, resetstop { "RESET STOP" },
                     saveArm { "SAVE" }, chord { "MIDI CHORD" };
    PathPicker seqPath;
    KeyRow userKeys, poolKeys;
    DiceButton dice;
    ClipButton clip;
    SlotGrid slots;
    LayerMatrix matrix;
    std::vector<std::unique_ptr<SliderAtt>> sAtts;
    std::vector<std::unique_ptr<ComboAtt>> cAtts;
    std::vector<std::unique_ptr<ButtonAtt>> bAtts;

    // layer panel: rebound whenever the layer changes
    int boundLayer = -1;
    juce::Slider lSteps, lShift, lLo, lHi, mLo, mHi;
    juce::ComboBox lPath, lDir, lRate, pitchMode, chroll, mKind, mCC, mGlide;
    juce::TextButton lLink { "UNLINK" }, lRandom { "RANDOM" }, lReset { "RESET" }, mOn { "ON" }, learn { "LEARN" };
    PathPicker layerPath;
    std::vector<std::unique_ptr<SliderAtt>> lsAtts;
    std::vector<std::unique_ptr<ComboAtt>> lcAtts;
    std::vector<std::unique_ptr<ButtonAtt>> lbAtts;

    struct Caption { juce::Rectangle<int> r; juce::String text; std::function<bool()> visible; };
    std::vector<Caption> captions;
    int lastHits = 0, flashFrames = 0;
    juce::String statusText, transposeText, nowText, layerTitle;
};
} // namespace beeui

class BeeEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer
{
public:
    explicit BeeEditor (BeeProcessor&);
    void resized() override;
private:
    beeui::Content content;
};
