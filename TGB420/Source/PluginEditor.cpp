#include "PluginEditor.h"

using namespace tgb;
namespace col = vintage::col;

namespace tgbui
{
juce::Colour layerColour (int l)
{
    static const float c[5][3] = { { 0.96f, 0.64f, 0.23f }, { 0.94f, 0.79f, 0.29f }, { 0.42f, 0.78f, 0.52f },
                                   { 0.35f, 0.68f, 0.91f }, { 0.71f, 0.56f, 0.95f } };
    l = juce::jlimit (0, 4, l);
    return juce::Colour::fromFloatRGBA (c[l][0], c[l][1], c[l][2], 1.0f);
}

namespace
{
constexpr float kRotStart = juce::MathConstants<float>::pi * -0.78f;
constexpr float kRotEnd   = juce::MathConstants<float>::pi *  0.78f;

void engrave (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, float size = 9.5f,
              juce::Justification j = juce::Justification::centred, juce::Colour c = col::cream)
{
    g.setFont (vintage::font (size, true, 0.14f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawText (text, r.translated (0, 1), j, false);
    g.setColour (c);
    g.drawText (text, r, j, false);
}
juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }
void setParamValue (TgbProcessor& p, const juce::String& pid, float plain)
{
    if (auto* prm = p.apvts.getParameter (pid))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}
} // namespace

// ===========================================================================
SegmentBar::SegmentBar (TgbProcessor& p, juce::String id, juce::StringArray l) : proc (p), pid (std::move (id)), labels (std::move (l)) {}

int SegmentBar::value() const { return (int) std::lround (proc.apvts.getRawParameterValue (pid)->load()); }

void SegmentBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int n = labels.size();
    const float w = r.getWidth() / (float) n;
    const int v = value();
    for (int i = 0; i < n; ++i)
    {
        auto c = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (1.0f);
        const bool on = i == v;
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (c, 2.5f);
        juce::ColourGradient cap (on ? accent.withAlpha (0.95f) : juce::Colour (0xff3b3830), c.getX(), c.getY(),
                                  on ? accent.darker (0.5f) : juce::Colour (0xff22201b), c.getX(), c.getBottom(), false);
        g.setGradientFill (cap);
        g.fillRoundedRectangle (c.reduced (1.0f), 2.0f);
        g.setFont (vintage::font (9.5f, true, 0.0f));
        g.setColour (on ? juce::Colour (0xff1a1208) : col::creamDim);
        g.drawText (labels[i], c, juce::Justification::centred, false);
    }
}

void SegmentBar::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    if (onPick) onPick (i); else setParamValue (proc, pid, (float) i);
    repaint();
}

// ===========================================================================
PulseCells::PulseCells (TgbProcessor& p, int s) : proc (p), stage (s)
{
    setTooltip ("Pulses: click a cell to set the stage length. Click the only lit cell again to skip the stage.");
}

juce::Rectangle<float> PulseCells::cell (int n) const   // n = 0..7, two rows of four
{
    const float w = (float) getWidth() / 4.0f, h = (float) getHeight() / 2.0f;
    return { w * (float) (n % 4), h * (float) (n / 4), w, h };
}

void PulseCells::paint (juce::Graphics& g)
{
    const int len = proc.stageVal ("len", stage);
    for (int n = 0; n < 8; ++n)
    {
        auto c = cell (n).reduced (1.5f);
        const bool lit = n < len, now = n == playingPulse && lit;
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (c, 2.0f);
        auto inner = c.reduced (1.5f);
        g.setColour (now ? col::amberHot : (lit ? col::amber.withAlpha (0.85f) : col::amberDim.darker (0.5f)));
        g.fillRoundedRectangle (inner, 1.5f);
        if (now) { g.setColour (col::amberHot.withAlpha (0.3f)); g.fillRoundedRectangle (c.expanded (1.5f), 3.0f); }
    }
    if (len == 0)
    {
        g.setFont (vintage::font (9.0f));
        g.setColour (col::cream);
        g.drawText ("SKIP", getLocalBounds(), juce::Justification::centred, false);
    }
}

void PulseCells::mouseDown (const juce::MouseEvent& e)
{
    for (int n = 0; n < 8; ++n)
        if (cell (n).contains (e.position))
        {
            int v = n + 1;
            if (v == 1 && proc.stageVal ("len", stage) == 1) v = 0;
            setParamValue (proc, ids::stage ("len", stage), (float) v);
            repaint();
            return;
        }
}

// ===========================================================================
StageColumn::StageColumn (TgbProcessor& p, int s)
    : stage (s), proc (p),
      modeBar (p, ids::stage ("mode", s), { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), "I", "II", juce::String (juce::CharPointer_UTF8 ("I\xe2\x80\x93")) }),
      divBar (p, ids::stage ("div", s), { "/1", "/2", "/3", "/4" }),
      cells (p, s)
{
    static const char* keys[] = { "pitch", "oct", "vel", "gate", "chance", "aux" };
    for (int k = 0; k < 6; ++k)
    {
        auto& kn = knobs[(size_t) k];
        kn.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        kn.setRotaryParameters (kRotStart, kRotEnd, true);
        kn.setPopupDisplayEnabled (true, true, nullptr);
        kn.setMouseDragSensitivity (k == 0 ? 360 : 220);
        kn.getProperties().set ("knob", "step");
        kn.getProperties().set ("bipolar", k <= 1);
        const int layerOf[] = { 0, 0, 1, 2, 3, 4 };
        kn.getProperties().set ("arc", (juce::int64) layerColour (layerOf[k]).getARGB());
        addChildComponent (kn);
        atts[(size_t) k] = std::make_unique<SliderAtt> (p.apvts, ids::stage (keys[k], s), kn);
    }
    knobs[0].setDoubleClickReturnValue (true, 0); knobs[1].setDoubleClickReturnValue (true, 0);
    knobs[2].setDoubleClickReturnValue (true, 100); knobs[3].setDoubleClickReturnValue (true, 50);
    knobs[4].setDoubleClickReturnValue (true, 100); knobs[5].setDoubleClickReturnValue (true, 0);
    knobs[0].setVisible (true);

    modeBar.setTooltip (juce::String (juce::CharPointer_UTF8 ("Mode: \xe2\x80\x93 rest, I one note, II a note every pulse (thinned by the divider), I\xe2\x80\x93 one long note")));
    divBar.setTooltip ("Divider for mode II: play every 1st, 2nd, 3rd or 4th pulse (switches the stage to II)");
    divBar.onPick = [this] (int d)
    {
        if (modeBar.value() != 2) setParamValue (proc, ids::stage ("mode", stage), 2.0f);
        setParamValue (proc, ids::stage ("div", stage), (float) d);
        modeBar.repaint();
    };
    addAndMakeVisible (modeBar);
    addAndMakeVisible (divBar);
    addAndMakeVisible (cells);
}

void StageColumn::resized()
{
    const int cx = getWidth() / 2;
    for (auto& k : knobs) k.setBounds (juce::Rectangle<int> (70, 70).withCentre ({ cx, 64 }));
    modeBar.setBounds (5, 124, getWidth() - 10, 20);
    divBar.setBounds (5, 148, getWidth() - 10, 20);
    cells.setBounds (8, 174, getWidth() - 16, 34);
}

void StageColumn::refresh (int newLayer, int playmode, bool nowPlaying, int hitFlash, int lpos, bool inLen)
{
    int k = 0;
    switch (newLayer) { case 0: k = playmode == Bass ? 1 : 0; break; case 1: k = 2; break; case 2: k = 3; break; case 3: k = 4; break; default: k = 5; }
    if (k != shownKnob) { knobs[(size_t) shownKnob].setVisible (false); knobs[(size_t) k].setVisible (true); shownKnob = k; }

    // readout, as the device's sendNames()
    juce::String r; bool sn = false;
    const int i = stage;
    if (newLayer == 1) r = juce::String (proc.stageVal ("vel", i));
    else if (newLayer == 2) { const int gt = proc.stageVal ("gate", i); r = gt >= 100 ? "Tie" : juce::String (gt) + "%"; }
    else if (newLayer == 3) r = juce::String (proc.stageVal ("chance", i)) + "%";
    else if (newLayer == 4) r = juce::String (proc.stageVal ("aux", i));
    else if (playmode == Bass) { const int o = proc.stageVal ("oct", i); r = (o > 0 ? "+" : "") + juce::String (o) + " oct"; }
    else if (playmode == Arp)  { const int pt = proc.stageVal ("pitch", i); r = (pt > 0 ? "+" : "") + juce::String (pt) + " st"; }
    else
    {
        Params p; p.scalemode = proc.param ("scalemode"); p.root = proc.param ("root"); p.scale = proc.param ("scale");
        const int raw = juce::jlimit (0, 127, 60 + proc.stageVal ("pitch", i) + proc.param ("transpose"));
        const int n = juce::jlimit (0, 127, snap (p, raw));
        r = noteName (n); sn = n != raw;
    }
    const int pulseNow = nowPlaying ? proc.uiPulse() : -1;
    if (r != readout || sn != snapped || newLayer != layer || nowPlaying != playing || hitFlash != flash || lpos != layerPos || inLen != inLength)
    {
        readout = r; snapped = sn; layer = newLayer; playing = nowPlaying; flash = hitFlash; layerPos = lpos; inLength = inLen;
        repaint();
    }
    if (cells.playingPulse != pulseNow) { cells.playingPulse = pulseNow; cells.repaint(); }
    modeBar.accent = divBar.accent = layerColour (0);
    modeBar.repaint(); divBar.repaint();
}

void StageColumn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // stage number + playhead LED
    engrave (g, juce::String (stage + 1), { 4, 2, 24, 14 }, 10.0f, juce::Justification::centredLeft, playing ? col::amber : col::cream);
    vintage::drawLed (g, { r.getRight() - 12.0f, 9.0f }, 3.6f, playing, col::ledRed);
    // knob glow on a hit
    if (flash > 0)
    {
        const auto c = layerColour (layer);
        juce::ColourGradient glow (c.withAlpha (0.35f * (float) flash / 4.0f), r.getCentreX(), 64.0f, c.withAlpha (0.0f), r.getCentreX() + 46.0f, 64.0f, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (96.0f, 96.0f).withCentre ({ r.getCentreX(), 64.0f }));
    }
    // readout
    auto win = juce::Rectangle<float> (r.getWidth() - 14.0f, 18.0f).withCentre ({ r.getCentreX(), 109.0f });
    vintage::drawGlassWindow (g, win, 2.5f);
    vintage::drawGlowText (g, readout + (snapped ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) : juce::String()), win,
                           juce::Justification::centred, layerColour (layer), 12.0f);
    // where an unlinked layer is right now
    if (layerPos == stage)
    {
        g.setColour (layerColour (layer));
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getWidth() - 20.0f, 3.0f).withCentre ({ r.getCentreX(), r.getBottom() - 4.0f }), 1.5f);
    }
}

void StageColumn::paintOverChildren (juce::Graphics& g)
{
    if (! inLength)
    {
        g.setColour (col::panelLo.withAlpha (0.62f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f);
    }
    if (playing)
    {
        g.setColour (col::amber.withAlpha (0.18f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f, 1.5f);
    }
}

// ===========================================================================
void DiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f).translated (0.0f, down ? 1.5f : 0.0f);
    if (heat > 0)   // hot streak
    {
        const float h = juce::jmin (1.0f, (float) heat / 6.0f);
        juce::ColourGradient fire (juce::Colour (0xffff5a1f).withAlpha (0.55f * h), r.getCentreX(), r.getCentreY(),
                                   juce::Colour (0xffffb000).withAlpha (0.0f), r.getCentreX(), r.getY() - 18.0f * h, true);
        g.setGradientFill (fire);
        g.fillEllipse (r.expanded (10.0f + 8.0f * h));
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.5f), 10.0f);
    juce::ColourGradient body (col::orangeCap.brighter (over ? 0.35f : 0.2f), r.getX(), r.getY(), col::orangeCap.darker (0.45f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 1.0f);
    // pips
    const float s = r.getWidth() / 4.0f;
    auto pip = [&] (float gx, float gy)
    {
        const juce::Point<float> c (r.getX() + s * gx, r.getY() + s * gy);
        g.setColour (juce::Colour (0xff2a1206));
        g.fillEllipse (juce::Rectangle<float> (s * 0.55f, s * 0.55f).withCentre (c));
    };
    switch (face)
    {
        case 1: pip (2, 2); break;
        case 2: pip (1, 1); pip (3, 3); break;
        case 3: pip (1, 1); pip (2, 2); pip (3, 3); break;
        case 4: pip (1, 1); pip (3, 1); pip (1, 3); pip (3, 3); break;
        case 5: pip (1, 1); pip (3, 1); pip (2, 2); pip (1, 3); pip (3, 3); break;
        default: pip (1, 1); pip (3, 1); pip (1, 2); pip (3, 2); pip (1, 3); pip (3, 3); break;
    }
}

// ===========================================================================
juce::Rectangle<float> SlotGrid::slotRect (int n) const
{
    const float w = (float) getWidth() / 4.0f, h = (float) getHeight() / 2.0f;
    return juce::Rectangle<float> (w * (float) (n % 4), h * (float) (n / 4), w, h).reduced (3.0f);
}
int SlotGrid::slotAt (juce::Point<float> p) const
{
    for (int n = 0; n < 8; ++n) if (slotRect (n).contains (p)) return n;
    return -1;
}
void SlotGrid::paint (juce::Graphics& g)
{
    for (int n = 0; n < 8; ++n)
    {
        auto r = slotRect (n);
        const bool filled = proc.slotFilled (n), active = proc.activeSlot == n, pending = proc.pendingSlot == n;
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (r.expanded (1.0f), 4.0f);
        const auto base = active ? col::amber : (filled ? col::keycap : juce::Colour (0xff3a372f));
        juce::ColourGradient cap (base.brighter (0.15f), r.getX(), r.getY(), base.darker (0.4f), r.getX(), r.getBottom(), false);
        g.setGradientFill (cap);
        g.fillRoundedRectangle (r, 3.0f);
        if (pending && (blink / 6) % 2 == 0) { g.setColour (col::amberHot.withAlpha (0.6f)); g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 2.0f); }
        if (proc.saveArm) { g.setColour (col::ledRed.withAlpha (0.5f)); g.drawRoundedRectangle (r.expanded (1.0f), 4.0f, 1.0f); }
        g.setFont (vintage::font (12.0f));
        g.setColour (filled || active ? juce::Colour (0xff2b2820) : col::creamDim);
        g.drawText (juce::String (n + 1), r, juce::Justification::centred, false);
    }
}
void SlotGrid::mouseDown (const juce::MouseEvent& e) { dragFrom = slotAt (e.position); }
void SlotGrid::mouseUp (const juce::MouseEvent& e)
{
    const int n = slotAt (e.position);
    if (n < 0) return;
    if (dragFrom >= 0 && n != dragFrom) proc.copySlot (dragFrom, n);
    else if (n == dragFrom)
    {
        const int mod = e.mods.isShiftDown() ? 1 : (e.mods.isAltDown() ? 2 : 0);
        proc.slotClick (n, mod);
    }
    dragFrom = -1;
    repaint();
}

// ===========================================================================
juce::Rectangle<float> PoolKeys::keyRect (int pc) const
{
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / 7.0f;
    static const int wi[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    if (! isBlack (pc)) return { r.getX() + ww * (float) wi[pc], r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.62f;
    return { r.getX() + ww * (float) (wi[pc] + 1) - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}
void PoolKeys::paint (juce::Graphics& g)
{
    auto draw = [&] (int pc)
    {
        auto k = keyRect (pc).reduced (0.8f, 0.0f);
        const bool black = isBlack (pc), on = proc.param ("cust" + juce::String (pc)) > 0;
        juce::ColourGradient grad (black ? juce::Colour (0xff2c2a26) : juce::Colour (0xfff3ead2), k.getX(), k.getY(),
                                   black ? juce::Colour (0xff0a0908) : juce::Colour (0xffc9bea0), k.getX(), k.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (k, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (k, 2.0f, 1.0f);
        vintage::drawLed (g, { k.getCentreX(), k.getBottom() - (black ? 8.0f : 9.0f) }, 2.7f, on);
    };
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) draw (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) draw (pc);
    if (proc.param ("pool") != 7) { g.setColour (col::panelLo.withAlpha (0.45f)); g.fillRect (getLocalBounds()); }
}
void PoolKeys::mouseDown (const juce::MouseEvent& e)
{
    int hit = -1;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (! isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    if (hit < 0) return;
    setParamValue (proc, ids::cust (hit), proc.param ("cust" + juce::String (hit)) > 0 ? 0.0f : 1.0f);
    repaint();
}

// ===========================================================================
void ClipButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 8) return;
    dragging = true;
    auto f = proc.exportClip();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    else dragging = false;
}

// ===========================================================================
Content::Content (TgbProcessor& p) : proc (p), clip (p), slotGrid (p), poolKeys (p)
{
    setLookAndFeel (&lnf);

    // ---- play mode + layer tabs (radio keycaps bound to choice params in the timer)
    const char* modes[] = { "SEQ", "BASS", "ARP" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtns[(size_t) i];
        b.setButtonText (modes[i]); b.setClickingTogglesState (true); b.setRadioGroupId (2001);
        b.onClick = [this, i] { if (modeBtns[(size_t) i].getToggleState()) setParam ("playmode", i); };
        addAndMakeVisible (b);
    }
    for (int l = 0; l < 5; ++l)
    {
        auto& b = layerBtns[(size_t) l];
        b.setButtonText (ids::layerName (l)); b.setClickingTogglesState (true); b.setRadioGroupId (2002);
        b.onClick = [this, l] { if (layerBtns[(size_t) l].getToggleState()) setParam ("layer", l); };
        addAndMakeVisible (b);
    }

    knob (transpose, "big", "transpose", "Transpose: +-24 semitones, before the scale");
    knob (swing, "small", "swing", "Swing: delays every second pulse (up to half a pulse)");
    knob (bassnote, "small", "bassnote", "Bass note (a key you play overrides it)");
    knob (arpoct, "small", "arpoct", "Arp: spread the held notes over 1-4 octaves");
    knob (length, "big", "length", "Stages: 1-16");
    knob (pulses, "small", "pulses", "Count by Pulse: the cycle restarts after this many pulses");
    knob (notes, "small", "notes", "Notes: how many different pitches a roll may use");
    knob (octjump, "small", "octjump", "Oct: chance that a rolled stage jumps an octave");

    // Steps of the layer on screen (not a parameter itself: writes the layer's llen)
    steps.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    steps.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    steps.setRotaryParameters (kRotStart, kRotEnd, true);
    steps.setRange (1, 16, 1);
    steps.setPopupDisplayEnabled (true, true, nullptr);
    steps.getProperties().set ("knob", "small");
    steps.setTooltip ("Steps: this layer's own loop length. Change it to unlink the layer from Stages.");
    steps.onValueChange = [this]
    {
        const int l = proc.param ("layer"), v = (int) steps.getValue();
        if (v != proc.effLen (l)) setParamValue (proc, ids::llen (l), (float) v);
    };
    addAndMakeVisible (steps);
    link.setClickingTogglesState (false);
    link.setTooltip ("Chain: link this layer back to Stages, or unlink it at the current length");
    link.onClick = [this]
    {
        const int l = proc.param ("layer");
        setParamValue (proc, ids::llen (l), proc.param ("llen" + juce::String (l + 1)) > 0 ? 0.0f : (float) proc.param ("length"));
    };
    addAndMakeVisible (link);

    combo (clock, "clock"); combo (direction, "direction"); combo (scalemode, "scalemode"); combo (root, "root"); combo (scale, "scale");
    combo (chroll, "chroll"); combo (auxtype, "auxtype"); combo (auxcc, "auxcc"); combo (glide, "glide"); combo (arporder, "arporder");
    combo (countby, "countby"); combo (randtarget, "randtarget"); combo (pool, "pool"); combo (evolve, "evolve"); combo (evotarget, "evotarget");
    combo (midich, "midich");

    for (auto* b : { &retrig, &latch, &resetstop, &thru })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    bAtts.push_back (std::make_unique<ButtonAtt> (p.apvts, ids::id ("retrig"), retrig));
    bAtts.push_back (std::make_unique<ButtonAtt> (p.apvts, ids::id ("latch"), latch));
    bAtts.push_back (std::make_unique<ButtonAtt> (p.apvts, ids::id ("resetstop"), resetstop));
    bAtts.push_back (std::make_unique<ButtonAtt> (p.apvts, ids::id ("thru"), thru));

    for (auto* b : { &nudgeL, &nudgeR }) { b->getProperties().set ("cap", "arrow"); addAndMakeVisible (*b); }
    nudgeL.onClick = [this] { proc.nudge (-1); };
    nudgeR.onClick = [this] { proc.nudge (1); };
    nudgeL.setTooltip ("Nudge: shift every stage one place left (the end wraps around)");
    nudgeR.setTooltip ("Nudge: shift every stage one place right");
    back.onClick = [this] { proc.goBack(); };
    fwd.onClick = [this] { proc.goForward(); };
    seqreset.onClick = [this] { proc.seqReset(); };
    for (auto* b : { &back, &fwd, &seqreset }) addAndMakeVisible (*b);
    back.setTooltip ("Back: undo a roll, recall, save, clear, copy, nudge or Evolve step (16 steps)");

    learn.setClickingTogglesState (true);
    learn.onClick = [this] { proc.armLearn (learn.getToggleState()); };
    learn.setTooltip ("Learn: move a knob on your controller to pick the Aux CC");
    addAndMakeVisible (learn);

    for (auto* b : { &page1, &page2 }) { b->setClickingTogglesState (true); b->setRadioGroupId (2003); addAndMakeVisible (*b); }
    page1.onClick = [this] { if (page1.getToggleState()) proc.page = 0; };
    page2.onClick = [this] { if (page2.getToggleState()) proc.page = 1; };
    followBtn.setClickingTogglesState (true);
    followBtn.onClick = [this] { proc.follow = followBtn.getToggleState(); };
    followBtn.setTooltip ("Follow: the page follows the playhead");
    addAndMakeVisible (followBtn);

    saveArm.setClickingTogglesState (true);
    saveArm.onClick = [this] { proc.saveArm = saveArm.getToggleState(); slotGrid.repaint(); };
    saveArm.setTooltip ("Save: the next slot you click stores the current sequence (or shift-click a slot)");
    addAndMakeVisible (saveArm);

    dice.onClick = [this] { proc.randomize(); dice.face = 1 + juce::Random::getSystemRandom().nextInt (6); dice.repaint(); };
    dice.setTooltip ("Roll the dice: randomize the Target with the note Pool");
    addAndMakeVisible (dice);

    clip.getProperties().set ("cap", "orange");
    clip.onClick = [this] { auto f = proc.exportClip(); if (f.existsAsFile()) f.revealToUser(); };
    clip.setTooltip ("MIDI Clip: click to save a .mid file of one cycle (Music/TGB-420), or drag this button into Cubase");
    addAndMakeVisible (clip);

    slotGrid.setTooltip ("Patterns: click to recall (waits for the cycle end while playing), shift-click to save, alt-click to clear, drag to copy");
    addAndMakeVisible (slotGrid);
    poolKeys.setTooltip ("Custom pool: the notes the Custom pool may use");
    addAndMakeVisible (poolKeys);

    for (int i = 0; i < NS; ++i)
    {
        columns[(size_t) i] = std::make_unique<StageColumn> (p, i);
        addChildComponent (*columns[(size_t) i]);
    }

    renderBackground();
    setSize (W, H);
    timerCallback();
    startTimerHz (30);
}

Content::~Content() { stopTimer(); setLookAndFeel (nullptr); }

void Content::setParam (const juce::String& name, int v) { setParamValue (proc, ids::id (name), (float) v); }

juce::Slider& Content::knob (juce::Slider& s, const char* style, const juce::String& name, const juce::String& tip)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (kRotStart, kRotEnd, true);
    s.setPopupDisplayEnabled (true, true, nullptr);
    s.getProperties().set ("knob", style);
    s.setTooltip (tip);
    addAndMakeVisible (s);
    sAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::id (name), s));
    return s;
}

juce::ComboBox& Content::combo (juce::ComboBox& b, const juce::String& name)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ids::id (name))))
        b.addItemList (c->choices, 1);
    addAndMakeVisible (b);
    cAtts.push_back (std::make_unique<ComboAtt> (proc.apvts, ids::id (name), b));
    return b;
}

// ---- layout (design size 1320 x 600) --------------------------------------
void Content::resized()
{
    captions.clear();
    auto cap = [this] (int x, int y, int w, const char* t, std::function<bool()> vis = {}) { captions.push_back ({ { x, y, w, 14 }, t, vis }); };
    auto pm = [this] { return proc.param ("playmode"); };

    // PLAY (24..234)
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i].setBounds (36 + i * 64, 92, 60, 40);
    transpose.setBounds (34, 146, 88, 88);              cap (34, 236, 88, "TRANSPOSE");
    clock.setBounds (36, 278, 86, 28);                  cap (36, 262, 86, "CLOCK");
    swing.setBounds (146, 252, 62, 62);                 cap (136, 314, 82, "SWING");
    nudgeL.setBounds (36, 352, 42, 30); nudgeR.setBounds (80, 352, 42, 30); cap (36, 336, 86, "NUDGE");
    bassnote.setBounds (40, 418, 62, 62);               cap (30, 480, 82, "BASS NOTE", [pm] { return pm() == 1; });
    arporder.setBounds (36, 422, 172, 28);              cap (36, 406, 172, "ARP STYLE", [pm] { return pm() == 2; });
    arpoct.setBounds (40, 460, 56, 56);                 cap (30, 516, 76, "OCT", [pm] { return pm() == 2; });
    latch.setBounds (112, 472, 96, 40);
    retrig.setBounds (112, 528, 96, 40);

    // STAGES (234..934)
    for (int l = 0; l < 5; ++l) layerBtns[(size_t) l].setBounds (248 + l * 76, 84, 72, 40);
    steps.setBounds (632, 80, 48, 48);                  cap (622, 126, 68, "STEPS");
    link.setBounds (686, 88, 60, 32);
    page1.setBounds (752, 88, 46, 32); page2.setBounds (800, 88, 46, 32);
    followBtn.setBounds (850, 88, 74, 32);
    for (int i = 0; i < NS; ++i) columns[(size_t) i]->setBounds (246 + (i % PAGE) * 85, 146, 85, 222);
    direction.setBounds (250, 404, 86, 28);             cap (250, 388, 86, "DIRECTION");
    scalemode.setBounds (346, 404, 86, 28);             cap (346, 388, 86, "SCALE");
    root.setBounds (442, 404, 64, 28);                  cap (442, 388, 64, "ROOT", [this] { return proc.param ("scalemode") == 1; });
    scale.setBounds (516, 404, 160, 28);                cap (516, 388, 160, "SCALE TYPE", [this] { return proc.param ("scalemode") == 1; });
    chroll.setBounds (250, 470, 120, 28);               cap (250, 454, 120, "CHANCE ROLL", [this] { return proc.param ("layer") == 3; });
    auxtype.setBounds (250, 470, 120, 28);              cap (250, 454, 120, "AUX TARGET", [this] { return proc.param ("layer") == 4; });
    auxcc.setBounds (380, 470, 170, 28);                cap (380, 454, 170, "CC", [this] { return proc.param ("layer") == 4 && proc.param ("auxtype") == 0; });
    learn.setBounds (560, 466, 70, 36);
    glide.setBounds (640, 470, 90, 28);                 cap (640, 454, 90, "STEP / GLIDE", [this] { return proc.param ("layer") == 4; });

    // LENGTH + RANDOMIZE (934..1124)
    length.setBounds (944, 88, 80, 80);                 cap (944, 168, 80, "STAGES");
    countby.setBounds (1034, 104, 80, 28);              cap (1034, 88, 80, "COUNT BY");
    pulses.setBounds (1046, 140, 56, 56);               cap (1034, 196, 80, "PULSES");
    dice.setBounds (946, 246, 80, 80);
    randtarget.setBounds (1034, 254, 80, 28);           cap (1034, 238, 80, "TARGET");
    pool.setBounds (1034, 304, 80, 28);                 cap (1034, 288, 80, "POOL");
    notes.setBounds (952, 346, 56, 56);                 cap (940, 402, 80, "NOTES");
    octjump.setBounds (1046, 346, 56, 56);              cap (1034, 402, 80, "OCT %");
    back.setBounds (944, 428, 84, 36); fwd.setBounds (1032, 428, 84, 36);
    seqreset.setBounds (944, 470, 172, 36);
    resetstop.setBounds (944, 520, 84, 46); thru.setBounds (1032, 520, 84, 46);

    // PATTERNS (1124..1296)
    slotGrid.setBounds (1134, 88, 152, 88);
    saveArm.setBounds (1136, 182, 148, 34);
    evolve.setBounds (1136, 266, 148, 28);              cap (1136, 250, 148, "EVOLVE");
    evotarget.setBounds (1136, 298, 148, 28);
    poolKeys.setBounds (1138, 352, 144, 66);            cap (1136, 336, 148, "CUSTOM POOL");
    clip.setBounds (1136, 430, 148, 42);
    midich.setBounds (1136, 520, 148, 28);              cap (1136, 504, 148, "MIDI CHANNEL");
}

// ---- refresh -------------------------------------------------------------
void Content::timerCallback()
{
    const int layer = proc.param ("layer"), pm = proc.param ("playmode");
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i].setToggleState (i == pm, juce::dontSendNotification);
    for (int l = 0; l < 5; ++l) layerBtns[(size_t) l].setToggleState (l == layer, juce::dontSendNotification);

    // play-mode controls
    bassnote.setVisible (pm == 1);
    arporder.setVisible (pm == 2); arpoct.setVisible (pm == 2); latch.setVisible (pm == 2);
    retrig.setVisible (pm != 0);
    // scale + layer options
    const bool custom = proc.param ("scalemode") == 1;
    root.setVisible (custom); scale.setVisible (custom);
    chroll.setVisible (layer == 3);
    auxtype.setVisible (layer == 4); glide.setVisible (layer == 4);
    auxcc.setVisible (layer == 4 && proc.param ("auxtype") == 0); learn.setVisible (layer == 4 && proc.param ("auxtype") == 0);
    learn.setToggleState (proc.learnArm, juce::dontSendNotification);
    pulses.setEnabled (proc.param ("countby") == 1);
    pulses.setAlpha (proc.param ("countby") == 1 ? 1.0f : 0.4f);

    // Steps + chain
    const int view = proc.effLen (layer);
    const bool unlinked = proc.param ("llen" + juce::String (layer + 1)) > 0;
    steps.setValue (view, juce::dontSendNotification);
    steps.getProperties().set ("arc", (juce::int64) layerColour (layer).getARGB());
    link.setButtonText (unlinked ? "UNLINK" : "LINK");
    link.setColour (juce::TextButton::buttonColourId, unlinked ? layerColour (layer) : col::keycap);

    // page
    if (view <= PAGE) proc.page = 0;
    const int stg = proc.uiStage();
    const int shown = unlinked && proc.uiLayerPos (layer) >= 0 ? proc.uiLayerPos (layer) : stg;
    if (proc.follow && view > PAGE && shown >= 0) proc.page = shown / PAGE;
    page1.setVisible (view > PAGE); page2.setVisible (view > PAGE); followBtn.setVisible (view > PAGE);
    followBtn.setToggleState (proc.follow, juce::dontSendNotification);
    page1.setToggleState (proc.page == 0, juce::dontSendNotification);
    page2.setToggleState (proc.page == 1, juce::dontSendNotification);

    // stage columns
    const int hits = proc.uiHits();
    if (hits != lastHits) { lastHits = hits; flashFrames = 4; }
    else if (flashFrames > 0) --flashFrames;
    const int hitStage = proc.uiHitStage();
    for (int i = 0; i < NS; ++i)
    {
        auto& c = *columns[(size_t) i];
        const bool onPage = i / PAGE == proc.page;
        c.setVisible (onPage);
        if (! onPage) continue;
        c.refresh (layer, pm, stg == i && proc.isPlaying(), (i == hitStage && proc.isPlaying()) ? flashFrames : 0,
                   unlinked ? proc.uiLayerPos (layer) : -1, i < view);
    }

    // displays
    const int tr = proc.param ("transpose");
    const auto newT = (tr > 0 ? "+" : "") + juce::String (tr) + " st";
    const auto newL = juce::String (proc.param ("length"));
    juce::String newS = proc.status;
    if (juce::Time::getMillisecondCounterHiRes() - proc.statusTime > 6000 || newS.isEmpty())
        newS = proc.param ("scalemode") == 0 ? "No scale (chromatic)"
             : ids::noteNames()[proc.param ("root")] + " " + scales()[(size_t) proc.param ("scale")].name;
    if (newT != transposeText || newL != lengthText || newS != statusText)
    {
        transposeText = newT; lengthText = newL; statusText = newS;
        repaint();
    }
    dice.heat = proc.rollsInWindow() >= 4 ? proc.rollsInWindow() - 3 : 0;
    dice.repaint();
    saveArm.setToggleState (proc.saveArm, juce::dontSendNotification);
    ++slotGrid.blink; slotGrid.repaint();
    poolKeys.repaint();
}

// ---- drawing ---------------------------------------------------------------
void Content::renderBackground()
{
    const float k = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * k), (int) (H * k), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (k));
    juce::Random rnd (420);

    juce::ColourGradient base (col::panelHi, 0, 0, col::panel, 0, (float) H, false);
    g.setGradientFill (base);
    g.fillRect (0, 0, W, H);
    for (int i = 0; i < 1000; ++i)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + rnd.nextFloat() * 0.02f));
        g.drawHorizontalLine ((int) (rnd.nextFloat() * H), 0.0f, (float) W);
    }

    // header
    {
        juce::ColourGradient hg (juce::Colour (0xff15140f), 0, 0, juce::Colour (0xff23211b), 0, 58.0f, false);
        g.setGradientFill (hg);
        g.fillRect (24.0f, 0.0f, (float) W - 48.0f, 58.0f);
        g.setColour (col::metal.withAlpha (0.55f)); g.drawHorizontalLine (57, 24.0f, (float) W - 24.0f);
        g.setFont (vintage::font (32.0f, true, 0.30f));
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawText ("TGB-420", 46, 11, 260, 38, juce::Justification::centredLeft);
        g.setColour (col::cream);                            g.drawText ("TGB-420", 44, 9, 260, 38, juce::Justification::centredLeft);
        for (int l = 0; l < 5; ++l) { g.setColour (layerColour (l)); g.fillRect (276.0f + (float) l * 12.0f, 20.0f, 8.0f, 18.0f); }
        engrave (g, "STAGE  SEQUENCER", { 348, 14, 300, 16 }, 12.0f, juce::Justification::centredLeft);
        engrave (g, "16 STAGES" + dot() + "5 LAYERS" + dot() + "SEQ / BASS / ARP" + dot() + "BUILT FOR HYPNOTIC TECHNO", { 348, 31, 560, 14 }, 8.5f,
                 juce::Justification::centredLeft, col::creamDim);
        engrave (g, "AFTER RYK M-185" + dot() + "VST3", { 1000, 20, 266, 16 }, 9.5f, juce::Justification::centredRight, col::creamDim);
    }

    const int xs[] = { 24, 234, 934, 1124, W - 24 };
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawVerticalLine (xs[i], 60.0f, (float) H - 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawVerticalLine (xs[i] + 1, 60.0f, (float) H - 14.0f);
    }
    for (int i = 0; i < 4; ++i)
        for (auto y : { 70.0f, (float) H - 26.0f })
        {
            vintage::drawScrew (g, { (float) xs[i] + 10.0f, y }, 4.0f, rnd.nextFloat() * 3.0f);
            vintage::drawScrew (g, { (float) xs[i + 1] - 10.0f, y }, 4.0f, rnd.nextFloat() * 3.0f);
        }
    engrave (g, "PLAY", { 24, 66, 210, 16 }, 11.0f);
    engrave (g, "STAGES", { 234, 66, 700, 16 }, 11.0f);
    engrave (g, "LENGTH", { 934, 66, 190, 16 }, 11.0f);
    engrave (g, "RANDOMIZE", { 934, 216, 190, 16 }, 11.0f);
    engrave (g, "PATTERNS", { 1124, 66, 172, 16 }, 11.0f);
    engrave (g, "CLICK RECALL" + dot() + "SHIFT SAVE", { 1124, 220, 172, 12 }, 7.5f, juce::Justification::centred, col::creamDim);
    engrave (g, "ALT CLEAR" + dot() + "DRAG COPY", { 1124, 232, 172, 12 }, 7.5f, juce::Justification::centred, col::creamDim);
    engrave (g, "CLICK SAVES" + dot() + "DRAG INTO YOUR DAW", { 1124, 476, 172, 14 }, 7.5f, juce::Justification::centred, col::creamDim);

    // stage area well + bottom bar separator
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (242.0f, 140.0f, 686.0f, 234.0f, 6.0f);
    g.setColour (col::creamDim.withAlpha (0.35f));
    g.drawHorizontalLine (382, 250.0f, 920.0f);

    // glass windows
    vintage::drawGlassWindow (g, { 130.0f, 168.0f, 92.0f, 44.0f }, 4.0f);   // transpose
    vintage::drawGlassWindow (g, { 686.0f, 404.0f, 234.0f, 28.0f }, 3.0f);  // status line

    // wood cheeks
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
            const float gx = w.getX() + 2.0f + rnd.nextFloat() * 20.0f;
            grain.startNewSubPath (gx, 0.0f);
            for (float y = 0.0f; y <= (float) H; y += 40.0f)
                grain.lineTo (juce::jlimit (w.getX() + 1.0f, w.getRight() - 1.0f, gx + std::sin (y * 0.013f + (float) i) * 2.4f), y);
            g.setColour (juce::Colour (0xff2a1408).withAlpha (0.10f + rnd.nextFloat() * 0.18f));
            g.strokePath (grain, juce::PathStrokeType (0.6f + rnd.nextFloat() * 1.2f));
        }
    }
    g.setColour (juce::Colour (0xff100f0b)); g.fillRect (24, H - 14, W - 48, 14);
}

void Content::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    for (auto& c : captions)
        if (! c.visible || c.visible()) engrave (g, c.text, c.r, 9.0f);
    vintage::drawGlowText (g, transposeText, { 130.0f, 168.0f, 92.0f, 44.0f }, juce::Justification::centred, layerColour (0), 18.0f);
    vintage::drawGlowText (g, statusText, { 692.0f, 404.0f, 224.0f, 28.0f }, juce::Justification::centredLeft, col::amber, 12.0f);
    {
        const int l = proc.param ("layer"), pmode = proc.param ("playmode");
        const char* hints[] = { "PITCH: NOTE PER STAGE", "VELOCITY: 1-127", "GATE: LENGTH PER PULSE, 100% TIES", "", "" };
        juce::String h = hints[l];
        if (l == 0 && pmode == 1) h << " (OCTAVE IN BASS)";
        if (l == 0 && pmode == 2) h << " (TRANSPOSE IN ARP)";
        if (h.isNotEmpty()) engrave (g, h, { 250, 462, 670, 14 }, 9.0f, juce::Justification::centredLeft, col::creamDim);
    }
    if (proc.param ("playmode") == 0)
    {
        engrave (g, "EACH STAGE PLAYS", { 30, 420, 180, 14 }, 8.5f, juce::Justification::centred, col::creamDim);
        engrave (g, "ITS OWN PITCH", { 30, 436, 180, 14 }, 8.5f, juce::Justification::centred, col::creamDim);
    }
    else if (proc.param ("playmode") == 1)
    {
        Params p; p.scalemode = proc.param ("scalemode"); p.root = proc.param ("root"); p.scale = proc.param ("scale");
        auto win = juce::Rectangle<float> (112.0f, 432.0f, 96.0f, 34.0f);
        vintage::drawGlassWindow (g, win, 3.0f);
        vintage::drawGlowText (g, noteName (proc.param ("bassnote")), win, juce::Justification::centred, layerColour (0), 16.0f);
    }
    // Steps chain + layer badges
    for (int l = 0; l < 5; ++l)
    {
        const int ll = proc.param ("llen" + juce::String (l + 1));
        if (ll > 0)
        {
            const auto r = layerBtns[(size_t) l].getBounds().toFloat();
            g.setColour (layerColour (l));
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getRight() - 4.0f, r.getY() + 2.0f }));
            g.setFont (vintage::font (8.5f, true, 0.0f));
            g.setColour (juce::Colour (0xff1a1208));
            g.drawText (juce::String (ll), juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getRight() - 4.0f, r.getY() + 2.0f }), juce::Justification::centred, false);
        }
    }
}
} // namespace tgbui

// ===========================================================================
TgbEditor::TgbEditor (TgbProcessor& p) : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (tgbui::Content::W * 6 / 10, tgbui::Content::H * 6 / 10, tgbui::Content::W * 2, tgbui::Content::H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) tgbui::Content::W / tgbui::Content::H);
    setSize (tgbui::Content::W, tgbui::Content::H);
}

void TgbEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) tgbui::Content::W));
    content.setBounds (0, 0, tgbui::Content::W, tgbui::Content::H);
}
