#include "SnakeUI.h"

using namespace snake;
namespace col = vintage::col;

namespace
{
constexpr float kRotStart = juce::MathConstants<float>::pi * -0.78f;
constexpr float kRotEnd   = juce::MathConstants<float>::pi *  0.78f;

void setupKnob (juce::Slider& s, const char* style, const juce::String& tooltip = {})
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (kRotStart, kRotEnd, true);
    s.setPopupDisplayEnabled (true, true, nullptr);
    s.setVelocityBasedMode (false);
    s.setMouseDragSensitivity (220);
    s.getProperties().set ("knob", style);
    s.setColour (juce::Slider::textBoxTextColourId, col::amber);
    if (tooltip.isNotEmpty()) s.setTooltip (tooltip);
}

void setupCombo (juce::ComboBox& b, juce::RangedAudioParameter* p)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        b.addItemList (c->choices, 1);
    b.setJustificationType (juce::Justification::centredLeft);
}

void engrave (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
              float size = 9.5f, juce::Justification j = juce::Justification::centred, juce::Colour c = col::cream)
{
    g.setFont (vintage::font (size, true, 0.14f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawText (text, r.translated (0, 1), j, false);
    g.setColour (c);
    g.drawText (text, r, j, false);
}

juce::String noteName (int n) { return snake::ids::midiNoteName (n); }
juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }
} // namespace

// ===========================================================================
// StepCell
// ===========================================================================
StepCell::StepCell (SnakeModule& p, int laneIn, int stepIn) : lane (laneIn), step (stepIn)
{
    const auto id = ids::stepParam (lane, step);
    if (lane == Gates)
    {
        addAndMakeVisible (toggle);
        toggleAtt = std::make_unique<ButtonAtt> (p.apvts, id, toggle);
        toggle.setTooltip ("Gate step " + juce::String (step + 1));
    }
    else
    {
        setupKnob (knob, "step");
        addAndMakeVisible (knob);
        knobAtt = std::make_unique<SliderAtt> (p.apvts, id, knob);
        if (lane == Notes)
            knob.textFromValueFunction = [&p] (double v) { return ids::midiNoteName (p.quantizeValue ((float) v)); };
        knob.setDoubleClickReturnValue (true, lane == Notes ? ids::noteDefaults[step] : (lane == Velocities ? 64.0 : 0.0));
    }
    setInterceptsMouseClicks (false, true);
}

void StepCell::resized()
{
    auto r = getLocalBounds();
    if (lane == Gates)
        toggle.setBounds (juce::Rectangle<int> (50, 70).withCentre ({ r.getCentreX(), 54 }));
    else
        knob.setBounds (juce::Rectangle<int> (66, 66).withCentre ({ r.getCentreX(), 54 }));
}

void StepCell::setState (bool nowPlaying, bool nowInWindow, int path, const juce::String& text)
{
    if (nowPlaying != playing || nowInWindow != inWindow || path != pathNumber || text != readout)
    {
        playing = nowPlaying; inWindow = nowInWindow; pathNumber = path; readout = text;
        repaint();
    }
}

void StepCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // step LED
    vintage::drawLed (g, { r.getCentreX(), 11.0f }, 3.6f, playing, col::ledRed);
    // path order (where this cell sits on the snake)
    engrave (g, juce::String (pathNumber), { 4, 3, 22, 14 }, 9.0f, juce::Justification::centredLeft,
             inWindow ? col::amber : col::creamDim.withAlpha (0.5f));
    engrave (g, juce::String (step + 1).paddedLeft ('0', 2), { (int) r.getRight() - 26, 3, 22, 14 }, 8.0f,
             juce::Justification::centredRight, col::creamDim);

    if (lane != Gates)
    {
        auto win = juce::Rectangle<float> (68.0f, 18.0f).withCentre ({ r.getCentreX(), 99.0f });
        vintage::drawGlassWindow (g, win, 2.5f);
        vintage::drawGlowText (g, readout, win, juce::Justification::centred,
                               playing ? col::amberHot : (inWindow ? col::amber : col::amber.withAlpha (0.35f)), 12.5f);
    }
}

void StepCell::paintOverChildren (juce::Graphics& g)
{
    if (! inWindow)
    {
        g.setColour (col::panelLo.withAlpha (0.45f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f);
    }
    if (playing)
    {
        g.setColour (col::amber.withAlpha (0.16f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f, 1.5f);
    }
}

// ===========================================================================
// ShapeView
// ===========================================================================
void ShapeView::set (int s, int len, int off, int playingCell)
{
    if (s != shape || len != length || off != offset || playingCell != cell)
    {
        shape = s; length = len; offset = off; cell = playingCell;
        repaint();
    }
}

void ShapeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    vintage::drawGlassWindow (g, r, 4.0f);
    auto inner = r.reduced (14.0f);
    auto centreOf = [&] (int c)
    {
        const float cx = inner.getX() + inner.getWidth()  * ((float) (c % 4) + 0.5f) / 4.0f;
        const float cy = inner.getY() + inner.getHeight() * ((float) (c / 4) + 0.5f) / 4.0f;
        return juce::Point<float> (cx, cy);
    };

    // whole path dim, active window bright
    juce::Path full, active;
    for (int pos = 0; pos < 16; ++pos)
    {
        const auto pt = centreOf (cellForPathPosition (shape, pos));
        if (pos == 0) full.startNewSubPath (pt); else full.lineTo (pt);
        if (pos == offset) active.startNewSubPath (pt);
        else if (pos > offset && pos < offset + length) active.lineTo (pt);
    }
    g.setColour (col::amber.withAlpha (0.18f));
    g.strokePath (full, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::amber.withAlpha (0.25f));
    g.strokePath (active, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::amber);
    g.strokePath (active, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int c = 0; c < 16; ++c)
    {
        const int pos = shapeTable()[(size_t) shape][(size_t) c] - 1;
        const bool in = pos >= offset && pos < offset + length;
        const auto pt = centreOf (c);
        g.setColour (in ? col::amber : col::amberDim);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pt));
        if (c == cell)
        {
            g.setColour (col::amberHot.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (pt));
            g.setColour (col::amberHot);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
        }
    }
    // head marker
    const auto head = centreOf (cellForPathPosition (shape, offset));
    g.setColour (col::cream);
    g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (head), 1.2f);
}

// ===========================================================================
// ScaleKeyboard
// ===========================================================================
juce::Rectangle<float> ScaleKeyboard::keyRect (int pc) const
{
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / 7.0f;
    static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    if (! isBlack (pc))
        return { r.getX() + ww * (float) whiteIndex[pc], r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.62f;
    return { r.getX() + ww * (float) (whiteIndex[pc] + 1) - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}

void ScaleKeyboard::refresh()
{
    midiMode = proc.apvts.getRawParameterValue (ids::midiChord)->load() > 0.5f;
    uint16_t m = 0;
    if (midiMode) m = proc.getMidiChordMask();
    else for (int k = 0; k < 12; ++k) if (proc.apvts.getRawParameterValue (ids::userKey (k))->load() > 0.5f) m |= (uint16_t) (1u << k);
    if (m != shownMask) { shownMask = m; repaint(); }
}

void ScaleKeyboard::paint (juce::Graphics& g)
{
    auto drawKey = [&] (int pc)
    {
        auto k = keyRect (pc).reduced (0.8f, 0.0f);
        const bool on = (shownMask >> pc) & 1;
        const bool black = isBlack (pc);
        juce::ColourGradient grad (black ? juce::Colour (0xff2c2a26) : juce::Colour (0xfff3ead2), k.getX(), k.getY(),
                                   black ? juce::Colour (0xff0a0908) : juce::Colour (0xffc9bea0), k.getX(), k.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (k, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (k, 2.0f, 1.0f);
        vintage::drawLed (g, { k.getCentreX(), k.getBottom() - (black ? 9.0f : 11.0f) }, 3.0f, on);
    };
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) drawKey (pc);
    if (midiMode)
    {
        g.setColour (col::panelLo.withAlpha (0.25f));
        g.fillRect (getLocalBounds());
    }
}

void ScaleKeyboard::mouseDown (const juce::MouseEvent& e)
{
    if (midiMode) return;
    int hit = -1;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (! isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    if (hit < 0) return;

    auto* prm = proc.apvts.getParameter (ids::userKey (hit));
    const bool newState = prm->getValue() < 0.5f;
    // keep at least one note (device "keepone")
    if (! newState)
    {
        int count = 0;
        for (int k = 0; k < 12; ++k) if (proc.apvts.getRawParameterValue (ids::userKey (k))->load() > 0.5f) ++count;
        if (count <= 1) return;
    }
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (newState ? 1.0f : 0.0f);
    prm->endChangeGesture();
    refresh();
}

// ===========================================================================
// LanePanel
// ===========================================================================
LanePanel::LanePanel (SnakeModule& p, int laneIn) : proc (p), lane (laneIn)
{
    auto& st = p.apvts;
    setupCombo (dirBox,   st.getParameter (ids::laneParam (lane, "dir")));
    setupCombo (speedBox, st.getParameter (ids::laneParam (lane, "speed")));
    setupCombo (shapeBox, st.getParameter (ids::laneParam (lane, "shape")));
    for (auto* b : { &dirBox, &speedBox, &shapeBox }) addAndMakeVisible (*b);
    dirAtt   = std::make_unique<ComboAtt> (st, ids::laneParam (lane, "dir"),   dirBox);
    speedAtt = std::make_unique<ComboAtt> (st, ids::laneParam (lane, "speed"), speedBox);
    shapeAtt = std::make_unique<ComboAtt> (st, ids::laneParam (lane, "shape"), shapeBox);

    auto stepShape = [this] (int d)
    {
        const int n = shapeBox.getNumItems();
        shapeBox.setSelectedItemIndex ((shapeBox.getSelectedItemIndex() + d + n) % n, juce::sendNotificationSync);
    };
    prevShape.onClick = [stepShape] { stepShape (-1); };
    nextShape.onClick = [stepShape] { stepShape (1); };
    shapeView.onClick = [stepShape] { stepShape (1); };
    for (auto* b : { &prevShape, &nextShape }) { b->getProperties().set ("cap", "arrow"); addAndMakeVisible (*b); }
    addAndMakeVisible (shapeView);
    shapeView.setTooltip ("Snake shape: the path the playhead takes through the 4x4 grid. Click to change.");

    auto knob = [&] (juce::Slider& s, const juce::String& id, const juce::String& tip)
    {
        setupKnob (s, "small", tip);
        addAndMakeVisible (s);
        sAtt.push_back (std::make_unique<SliderAtt> (st, id, s));
    };
    knob (steps, ids::laneParam (lane, "len"),   "Loop length (1-16 steps)");
    knob (shift, ids::laneParam (lane, "shift"), "Shift: slides the loop window along the snake path");

    if (lane != Gates)
    {
        knob (rmin, ids::laneParam (lane, "rmin"), "Lowest value RANDOM can produce");
        knob (rmax, ids::laneParam (lane, "rmax"), "Highest value RANDOM can produce");
    }
    if (lane >= Custom1)
    {
        const int ci = lane - Custom1;
        knob (ccNum, ids::ccParam (ci, "num"), "MIDI CC number sent by this lane");
        knob (ccMin, ids::ccParam (ci, "min"), "CC value sent when a step knob is fully left");
        knob (ccMax, ids::ccParam (ci, "max"), "CC value sent when a step knob is fully right");
        addAndMakeVisible (ccOn);
        ccOnAtt = std::make_unique<ButtonAtt> (st, ids::ccParam (ci, "on"), ccOn);
    }

    randomBtn.getProperties().set ("cap", "orange");
    for (auto* b : { &randomBtn, &resetBtn }) addAndMakeVisible (*b);
    randomBtn.onClick = [this] { proc.randomizeLane (lane); };
    resetBtn.onClick  = [this] { proc.resetLane (lane); };
}

void LanePanel::resized()
{
    captions.clear();
    shapeView.setBounds (12, 34, 124, 124);
    prevShape.setBounds (12, 164, 30, 28);
    shapeBox.setBounds  (44, 164, 62, 28);
    nextShape.setBounds (106, 164, 30, 28);

    captions.push_back ({ { 146, 30, 98, 14 }, "DIRECTION" });
    dirBox.setBounds (146, 44, 98, 28);
    captions.push_back ({ { 146, 80, 98, 14 }, "SPEED" });
    speedBox.setBounds (146, 94, 98, 28);
    randomBtn.setBounds (146, 136, 48, 56);
    resetBtn.setBounds (196, 136, 48, 56);

    steps.setBounds (16, 212, 72, 72);  captions.push_back ({ { 10, 284, 84, 14 }, "STEPS" });
    shift.setBounds (98, 212, 72, 72);  captions.push_back ({ { 92, 284, 84, 14 }, "SHIFT" });

    if (lane != Gates)
    {
        rmin.setBounds (20, 306, 60, 60); captions.push_back ({ { 8, 366, 84, 14 }, "RND LOW" });
        rmax.setBounds (102, 306, 60, 60); captions.push_back ({ { 90, 366, 84, 14 }, "RND HIGH" });
    }
    if (lane >= Custom1)
    {
        ccNum.setBounds (16, 416, 56, 56);  captions.push_back ({ { 4, 490, 80, 12 }, "CC #" });
        ccMin.setBounds (84, 416, 56, 56);  captions.push_back ({ { 72, 490, 80, 12 }, "OUT MIN" });
        ccMax.setBounds (152, 416, 56, 56); captions.push_back ({ { 140, 490, 80, 12 }, "OUT MAX" });
        ccOn.setBounds (212, 414, 36, 66);
    }
}

void LanePanel::paint (juce::Graphics& g)
{
    engrave (g, "LANE" + dot() + ids::laneTitle (lane), { 0, 6, getWidth(), 16 }, 11.0f);
    for (auto& c : captions) engrave (g, c.text, c.r, 9.0f);

    if (lane >= Custom1)
    {
        g.setColour (col::creamDim.withAlpha (0.5f));
        g.drawHorizontalLine (412, 14.0f, (float) getWidth() - 14.0f);
        engrave (g, "MIDI CC OUT", { 0, 395, getWidth(), 14 }, 9.0f);
        const juce::Slider* ks[] = { &ccNum, &ccMin, &ccMax };
        for (int i = 0; i < 3; ++i)
        {
            auto win = juce::Rectangle<float> (50.0f, 15.0f).withCentre ({ (float) ks[i]->getBounds().getCentreX(), 481.0f });
            vintage::drawGlassWindow (g, win, 2.0f);
            vintage::drawGlowText (g, juce::String ((int) ks[i]->getValue()), win, juce::Justification::centred, col::amber, 11.5f);
        }
    }
    else if (lane == Notes)
    {
        engrave (g, "KNOBS SPAN 7 OCTAVES (C0 - B6)", { 0, 420, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
        engrave (g, "SNAPPED TO SCALE + ROOT", { 0, 436, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
    }
    else if (lane == Velocities)
    {
        engrave (g, "VELOCITY 0 MUTES A STEP", { 0, 420, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
    }
    else if (lane == Gates)
    {
        engrave (g, "THE GATES LANE IS THE CLOCK:", { 0, 330, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
        engrave (g, "EVERY ON STEP PLAYS ONE NOTE", { 0, 346, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
        engrave (g, "LENGTH = HALF A GATE STEP", { 0, 362, getWidth(), 14 }, 8.5f, juce::Justification::centred, col::creamDim);
    }
}

void LanePanel::refresh()
{
    const int sh  = (int) proc.apvts.getRawParameterValue (ids::laneParam (lane, "shape"))->load();
    const int len = (int) proc.apvts.getRawParameterValue (ids::laneParam (lane, "len"))->load();
    const float s = proc.apvts.getRawParameterValue (ids::laneParam (lane, "shift"))->load();
    shapeView.set (sh, len, windowOffset (s, len), proc.getCurrentCell (lane));
    if (lane >= Custom1)
    {
        const int sig = (int) ccNum.getValue() * 65536 + (int) ccMin.getValue() * 256 + (int) ccMax.getValue();
        if (sig != ccSignature) { ccSignature = sig; repaint (0, 470, getWidth(), 24); }
    }
}

// ===========================================================================
// SnakeContent
// ===========================================================================
SnakeContent::SnakeContent (SnakeModule& p) : proc (p), keyboard (p)
{
    setLookAndFeel (&lnf);
    auto& st = p.apvts;

    for (int l = 0; l < NumLanes; ++l)
    {
        auto b = std::make_unique<juce::TextButton> (l == Velocities ? juce::String ("VELO") : juce::String (ids::laneTitle (l)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->onClick = [this, l] { selectLane (l); };
        addAndMakeVisible (*b);
        laneButtons[(size_t) l] = std::move (b);

        lanePanels[(size_t) l] = std::make_unique<LanePanel> (p, l);
        addChildComponent (*lanePanels[(size_t) l]);

        for (int s = 0; s < 16; ++s)
        {
            cells[(size_t) l][(size_t) s] = std::make_unique<StepCell> (p, l, s);
            addChildComponent (*cells[(size_t) l][(size_t) s]);
        }
    }

    setupKnob (rootKnob, "big", "Root note (also: play C4-B4 into the plug-in)");
    setupKnob (swingKnob, "small", "Swing for the gates lane (delays every 2nd step)");
    addAndMakeVisible (rootKnob);
    addAndMakeVisible (swingKnob);
    rootAtt  = std::make_unique<SliderAtt> (st, ids::root, rootKnob);
    swingAtt = std::make_unique<SliderAtt> (st, ids::swing, swingKnob);

    setupCombo (scaleBox, st.getParameter (ids::scale));
    setupCombo (chanBox,  st.getParameter (s420::ids::snakeSrc));
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (chanBox);
    scaleAtt = std::make_unique<ComboAtt> (st, ids::scale, scaleBox);
    chanAtt  = std::make_unique<ComboAtt> (st, s420::ids::snakeSrc, chanBox);
    chanBox.setTooltip ("Scale from: the machine's Global scale and root, or Snake's Own");

    randomAll.getProperties().set ("cap", "orange");
    randomAll.onClick = [this] { proc.randomizeAll(); };
    resetAll.onClick  = [this] { proc.resetAll(); };
    addAndMakeVisible (randomAll);
    addAndMakeVisible (resetAll);

    addAndMakeVisible (chordSwitch);
    chordAtt = std::make_unique<ButtonAtt> (st, ids::midiChord, chordSwitch);
    chordSwitch.setTooltip ("ON: hold a chord between C3 and B3 to set the User scale");
    addAndMakeVisible (keyboard);
    keyboard.setTooltip ("User scale: click keys to toggle notes");

    renderBackground();
    setSize (W, H);
    selectLane (p.selectedLane);
    startTimerHz (30);
}

SnakeContent::~SnakeContent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SnakeContent::resized()
{
    for (int l = 0; l < NumLanes; ++l)
        laneButtons[(size_t) l]->setBounds (36 + (l % 3) * 66, 104 + (l / 3) * 44, 62, 40);

    rootKnob.setBounds (32, 196, 92, 92);
    scaleBox.setBounds (36, 324, 188, 30);
    swingKnob.setBounds (40, 380, 72, 72);
    chanBox.setBounds (134, 396, 90, 30);
    randomAll.setBounds (36, 486, 92, 52);
    resetAll.setBounds (134, 486, 92, 52);

    for (auto& lp : lanePanels) lp->setBounds (238, 62, 250, 504);

    for (auto& laneCells : cells)
        for (int s = 0; s < 16; ++s)
            laneCells[(size_t) s]->setBounds (494 + (s % 4) * 98, 88 + (s / 4) * 118, 98, 118);

    keyboard.setBounds (902, 96, 162, 96);
    chordSwitch.setBounds (906, 214, 40, 70);
}

void SnakeContent::selectLane (int lane)
{
    shownLane = juce::jlimit (0, NumLanes - 1, lane);
    proc.selectedLane = shownLane;
    laneButtons[(size_t) shownLane]->setToggleState (true, juce::dontSendNotification);
    for (int l = 0; l < NumLanes; ++l)
    {
        lanePanels[(size_t) l]->setVisible (l == shownLane);
        for (auto& c : cells[(size_t) l]) c->setVisible (l == shownLane);
    }
    timerCallback();
}

void SnakeContent::timerCallback()
{
    const int l = shownLane;
    const int sh  = (int) proc.apvts.getRawParameterValue (ids::laneParam (l, "shape"))->load();
    const int len = (int) proc.apvts.getRawParameterValue (ids::laneParam (l, "len"))->load();
    const int off = windowOffset (proc.apvts.getRawParameterValue (ids::laneParam (l, "shift"))->load(), len);
    const int cur = proc.getCurrentCell (l);

    for (int s = 0; s < 16; ++s)
    {
        const int pathPos = shapeTable()[(size_t) sh][(size_t) s] - 1;
        const bool in = pathPos >= off && pathPos < off + len;
        juce::String text;
        const float v = proc.apvts.getRawParameterValue (ids::stepParam (l, s))->load();
        if (l == Notes)      text = noteName (proc.quantizedNoteForStep (s));
        else if (l != Gates) text = juce::String ((int) v);
        cells[(size_t) l][(size_t) s]->setState (s == cur, in, pathPos + 1, text);
    }
    lanePanels[(size_t) l]->refresh();
    keyboard.refresh();

    // root display, now-playing display, activity LED
    const bool globalScale = proc.usesGlobalScale();
    rootKnob.setAlpha (globalScale ? 0.35f : 1.0f); scaleBox.setAlpha (globalScale ? 0.35f : 1.0f);
    rootKnob.setEnabled (! globalScale); scaleBox.setEnabled (! globalScale);
    const auto newRoot = ids::noteNames()[(int) proc.apvts.getRawParameterValue (globalScale ? s420::ids::root : ids::root)->load()];
    if (newRoot != rootText) { rootText = newRoot; repaint (130, 206, 100, 70); }

    const int flash = proc.getNoteFlash();
    if (flash != lastFlash)
    {
        lastFlash = flash; flashFrames = 3;
        const int pitch = proc.getLastPitch();
        playingText = pitch >= 0 ? noteName (pitch) : juce::String ("--");
        repaint (896, 440, 176, 90);
        repaint (1010, 14, 60, 34);
    }
    else if (flashFrames > 0 && --flashFrames == 0)
    {
        repaint (896, 440, 176, 90);
        repaint (1010, 14, 60, 34);
    }
}

// ---------------------------------------------------------------------------
void SnakeContent::renderBackground()
{
    const float k = 2.0f; // render at 2x for retina / scaling
    background = juce::Image (juce::Image::ARGB, (int) (W * k), (int) (H * k), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (k));
    juce::Random rnd (1977);

    // --- panel with brushed finish
    {
        juce::ColourGradient base (col::panelHi, 0, 0, col::panel, 0, (float) H, false);
        g.setGradientFill (base);
        g.fillRect (0, 0, W, H);
        for (int i = 0; i < 900; ++i)
        {
            const float y = rnd.nextFloat() * H;
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + rnd.nextFloat() * 0.02f));
            g.drawHorizontalLine ((int) y, 0.0f, (float) W);
        }
    }

    // --- header plate
    {
        juce::Rectangle<float> hdr (24.0f, 0.0f, (float) W - 48.0f, 58.0f);
        juce::ColourGradient hg (juce::Colour (0xff15140f), 0, 0, juce::Colour (0xff23211b), 0, 58.0f, false);
        g.setGradientFill (hg);
        g.fillRect (hdr);
        g.setColour (col::metal.withAlpha (0.55f));
        g.drawHorizontalLine (57, hdr.getX(), hdr.getRight());
        g.setColour (juce::Colours::black);
        g.drawHorizontalLine (58, hdr.getX(), hdr.getRight());

        g.setFont (vintage::font (32.0f, true, 0.42f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText ("SNAKE", 46, 11, 220, 38, juce::Justification::centredLeft);
        g.setColour (col::cream);
        g.drawText ("SNAKE", 44, 9, 220, 38, juce::Justification::centredLeft);
        // amber stripe pair, 70s style
        g.setColour (col::amber);
        g.fillRect (236.0f, 22.0f, 46.0f, 4.0f);
        g.setColour (col::orangeCap);
        g.fillRect (236.0f, 30.0f, 46.0f, 4.0f);
        engrave (g, "4 x 4  PATH  SEQUENCER", { 294, 14, 300, 16 }, 12.0f, juce::Justification::centredLeft);
        engrave (g, "SIX LANES" + dot() + "FOURTEEN SNAKES" + dot() + "THIRTY-SEVEN SCALES", { 294, 31, 420, 14 }, 8.5f,
                 juce::Justification::centredLeft, col::creamDim);
        engrave (g, "MODEL S-16" + dot() + "VST3", { 760, 20, 236, 16 }, 9.5f, juce::Justification::centredRight, col::creamDim);
    }

    // --- modules: grooves, titles, screws
    const int xs[] = { 24, 236, 490, 890, W - 24 };
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawVerticalLine (xs[i], 60.0f, (float) H - 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (xs[i] + 1, 60.0f, (float) H - 14.0f);
    }
    for (int i = 0; i < 4; ++i)
    {
        vintage::drawScrew (g, { (float) xs[i] + 10.0f, 70.0f }, 4.0f, rnd.nextFloat() * 3.0f);
        vintage::drawScrew (g, { (float) xs[i + 1] - 10.0f, 70.0f }, 4.0f, rnd.nextFloat() * 3.0f);
        vintage::drawScrew (g, { (float) xs[i] + 10.0f, (float) H - 26.0f }, 4.0f, rnd.nextFloat() * 3.0f);
        vintage::drawScrew (g, { (float) xs[i + 1] - 10.0f, (float) H - 26.0f }, 4.0f, rnd.nextFloat() * 3.0f);
    }

    engrave (g, "GLOBAL", { 24, 68, 212, 16 }, 11.0f);
    engrave (g, "LANE SELECT", { 24, 88, 212, 14 }, 8.5f, juce::Justification::centred, col::creamDim);
    engrave (g, "ROOT", { 32, 290, 92, 14 }, 9.0f);
    engrave (g, "SCALE", { 36, 308, 188, 14 }, 9.0f);
    engrave (g, "SWING", { 36, 454, 80, 14 }, 9.0f);
    engrave (g, "SCALE FROM", { 134, 380, 90, 14 }, 9.0f);

    engrave (g, "STEPS" + dot() + "4 x 4 GRID", { 490, 68, 400, 16 }, 11.0f);

    engrave (g, "USER SCALE", { 890, 68, 186, 16 }, 11.0f);
    engrave (g, "MIDI CHORD", { 950, 228, 120, 14 }, 9.5f, juce::Justification::centredLeft);
    engrave (g, "HOLD C3 - B3", { 950, 244, 120, 14 }, 8.5f, juce::Justification::centredLeft, col::creamDim);
    engrave (g, "TO SET THE SCALE", { 950, 258, 120, 14 }, 8.5f, juce::Justification::centredLeft, col::creamDim);

    // MIDI remote legend
    g.setColour (col::creamDim.withAlpha (0.5f));
    g.drawHorizontalLine (304, 904.0f, 1062.0f);
    engrave (g, "MIDI REMOTE", { 890, 296, 186, 14 }, 9.0f);
    const char* legend[][2] = { { "C4 - B4", "ROOT" }, { "C2", "RANDOM ALL" }, { "D2", "RANDOM NOTES" } };
    for (int i = 0; i < 3; ++i)
    {
        engrave (g, legend[i][0], { 904, 320 + i * 20, 60, 14 }, 9.0f, juce::Justification::centredLeft, col::amber);
        engrave (g, legend[i][1], { 966, 320 + i * 20, 100, 14 }, 9.0f, juce::Justification::centredLeft);
    }
    g.drawHorizontalLine (396, 904.0f, 1062.0f);
    engrave (g, "NOW PLAYING", { 890, 424, 186, 14 }, 9.0f);

    // glass windows (static parts)
    vintage::drawGlassWindow (g, { 132.0f, 210.0f, 94.0f, 62.0f }, 4.0f);
    vintage::drawGlassWindow (g, { 904.0f, 444.0f, 160.0f, 64.0f }, 4.0f);

    // --- wood cheeks
    for (auto x : { 0, W - 24 })
    {
        juce::Rectangle<float> w ((float) x, 0.0f, 24.0f, (float) H);
        juce::ColourGradient wg (col::woodLight, w.getX(), 0, col::wood.darker (0.3f), w.getRight(), 0, false);
        wg.addColour (0.5, col::wood);
        g.setGradientFill (wg);
        g.fillRect (w);
        for (int i = 0; i < 26; ++i)
        {
            juce::Path grain;
            float gx = w.getX() + 2.0f + rnd.nextFloat() * 20.0f;
            grain.startNewSubPath (gx, 0.0f);
            for (float y = 0.0f; y <= (float) H; y += 40.0f)
                grain.lineTo (juce::jlimit (w.getX() + 1.0f, w.getRight() - 1.0f, gx + std::sin (y * 0.013f + (float) i) * 2.4f), y);
            g.setColour (juce::Colour (0xff2a1408).withAlpha (0.10f + rnd.nextFloat() * 0.18f));
            g.strokePath (grain, juce::PathStrokeType (0.6f + rnd.nextFloat() * 1.2f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawVerticalLine (x == 0 ? 23 : W - 24, 0.0f, (float) H);
    }

    // --- bottom trim
    g.setColour (juce::Colour (0xff100f0b));
    g.fillRect (24, H - 14, W - 48, 14);
    g.setColour (col::metal.withAlpha (0.4f));
    g.drawHorizontalLine (H - 14, 24.0f, (float) W - 24.0f);
}

void SnakeContent::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // root display
    vintage::drawGlowText (g, rootText, { 132.0f, 210.0f, 94.0f, 62.0f }, juce::Justification::centred, col::amber, 34.0f);

    // now playing
    const bool lit = flashFrames > 0;
    vintage::drawGlowText (g, playingText.isEmpty() ? juce::String ("--") : playingText,
                           { 904.0f, 444.0f, 120.0f, 64.0f }, juce::Justification::centred,
                           lit ? col::amberHot : col::amber.withAlpha (0.75f), 28.0f);
    vintage::drawLed (g, { 1046.0f, 476.0f }, 4.5f, lit, col::ledRed);

    // header activity LED
    vintage::drawLed (g, { 1040.0f, 29.0f }, 4.5f, lit, col::amber);
}

