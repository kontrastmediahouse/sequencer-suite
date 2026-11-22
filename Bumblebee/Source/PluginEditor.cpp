#include "PluginEditor.h"

using namespace bee;
namespace col = vintage::col;

namespace beeui
{
juce::Colour layerColour (int l)
{
    static const juce::uint32 c[] = { 0xffffc21f, 0xfff3e7b0, 0xff7ccf7a, 0xff5aaee8, 0xffb48ff0, 0xffef7fb0, 0xff4fd1c5, 0xffff8a3d };
    return juce::Colour (c[juce::jlimit (0, 7, l)]);
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
juce::String dash() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")); }
void setParamValue (BeeProcessor& p, const juce::String& fullId, float plain)
{
    if (auto* prm = p.apvts.getParameter (fullId))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}
void styleKnob (juce::Slider& s, const char* style)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (kRotStart, kRotEnd, true);
    s.setPopupDisplayEnabled (true, true, nullptr);
    s.getProperties().set ("knob", style);
}
juce::String layerReadout (const BeeProcessor& p, const Params& prm, int layer, int cell, bool& snapped)
{
    snapped = false;
    switch (layer)
    {
        case LVel:    return juce::String (p.cellVal ("vel", cell));
        case LGate:   { const int g = p.cellVal ("gate", cell); return g >= 100 ? juce::String ("Tie") : juce::String (g) + "%"; }
        case LChance: return juce::String (p.cellVal ("chance", cell)) + "%";
        case LPitch:
        {
            if (prm.playmode == Bass) { const int o = p.cellVal ("oct", cell); return (o > 0 ? "+" : "") + juce::String (o) + " oct"; }
            const int v = p.cellVal ("pitch", cell);
            if (prm.playmode == Arp) return (v > 0 ? "+" : "") + juce::String (v) + " st";
            const auto sc = activeScale (prm);
            const int raw = juce::jlimit (0, 127, (prm.pitchMode == 1 ? degreeNote (sc, v) : 60 + v) + prm.s.transpose);
            const int n = juce::jlimit (0, 127, snap (sc, raw));
            snapped = n != raw;
            return noteName (n);
        }
        default: return juce::String (p.cellVal (ids::cellKey (8 + layer - LModA), cell));
    }
}
} // namespace

// ===========================================================================
SegmentBar::SegmentBar (BeeProcessor& p, juce::String id, juce::StringArray l) : proc (p), pid (std::move (id)), labels (std::move (l)) {}
int SegmentBar::value() const { return (int) std::lround (proc.apvts.getRawParameterValue (pid)->load()); }
void SegmentBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int n = labels.size(), v = value();
    const float w = r.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto c = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (1.0f);
        const bool on = i == v;
        g.setColour (juce::Colours::black.withAlpha (0.65f)); g.fillRoundedRectangle (c, 2.5f);
        juce::ColourGradient cap (on ? col::amber : juce::Colour (0xff36332b), c.getX(), c.getY(), on ? col::amber.darker (0.5f) : juce::Colour (0xff1f1d18), c.getX(), c.getBottom(), false);
        g.setGradientFill (cap); g.fillRoundedRectangle (c.reduced (1.0f), 2.0f);
        g.setFont (vintage::font (9.0f, true, 0.0f));
        g.setColour (on ? juce::Colour (0xff1a1404) : col::creamDim);
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
PulseRow::PulseRow (BeeProcessor& p, int c) : proc (p), cell (c)
{
    setTooltip ("Pulses: click a cell to set the stage length; click the only lit cell again to skip the stage");
}
void PulseRow::paint (juce::Graphics& g)
{
    const int len = proc.cellVal ("len", cell);
    const float w = (float) getWidth() / 8.0f;
    for (int n = 0; n < 8; ++n)
    {
        auto c = juce::Rectangle<float> (w * (float) n, 0.0f, w, (float) getHeight()).reduced (1.2f);
        const bool lit = n < len, now = n == playingPulse && lit;
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRoundedRectangle (c, 1.5f);
        g.setColour (now ? col::amberHot : (lit ? col::amber.withAlpha (0.85f) : col::amberDim.darker (0.4f)));
        g.fillRoundedRectangle (c.reduced (1.2f), 1.0f);
    }
    if (len == 0) { g.setFont (vintage::font (8.0f)); g.setColour (col::cream); g.drawText ("SKIP", getLocalBounds(), juce::Justification::centred, false); }
}
void PulseRow::mouseDown (const juce::MouseEvent& e)
{
    int v = juce::jlimit (1, 8, (int) (e.position.x / ((float) getWidth() / 8.0f)) + 1);
    if (v == 1 && proc.cellVal ("len", cell) == 1) v = 0;
    setParamValue (proc, ids::cell ("len", cell), (float) v);
    repaint();
}

// ===========================================================================
CellView::CellView (BeeProcessor& p, int c)
    : cell (c), proc (p),
      modeBar (p, ids::cell ("mode", c), { dash(), "I", "II", "I" + dash() }),
      divBar (p, ids::cell ("div", c), { "/1", "/2", "/3", "/4" }),
      pulses (p, c)
{
    static const char* keys[] = { "pitch", "oct", "vel", "gate", "chance", "moda", "modb", "modc", "modd" };
    static const int layerOf[] = { 0, 0, 1, 2, 3, 4, 5, 6, 7 };
    static const double defaults[] = { 0, 0, 100, 50, 100, 0, 0, 0, 0 };
    for (int k = 0; k < 9; ++k)
    {
        auto& kn = knobs[(size_t) k];
        styleKnob (kn, "step");
        kn.setMouseDragSensitivity (k == 0 ? 360 : 220);
        kn.getProperties().set ("bipolar", k <= 1);
        kn.getProperties().set ("arc", (juce::int64) layerColour (layerOf[k]).getARGB());
        addChildComponent (kn);
        atts[(size_t) k] = std::make_unique<SliderAtt> (p.apvts, ids::cell (keys[k], c), kn);
        kn.setDoubleClickReturnValue (true, defaults[k]);
    }
    knobs[0].setVisible (true);
    modeBar.setTooltip ("Mode: rest, I one note, II a note every pulse (thinned by the divider), I- one held note");
    divBar.setTooltip ("Divider for mode II (picking one switches the stage to II)");
    divBar.onPick = [this] (int d)
    {
        if (modeBar.value() != 2) setParamValue (proc, ids::cell ("mode", cell), 2.0f);
        setParamValue (proc, ids::cell ("div", cell), (float) d);
        modeBar.repaint();
    };
    addAndMakeVisible (modeBar); addAndMakeVisible (divBar); addAndMakeVisible (pulses);
}
void CellView::resized()
{
    const int cx = getWidth() / 2;
    for (auto& k : knobs) k.setBounds (juce::Rectangle<int> (62, 62).withCentre ({ cx, 48 }));
    modeBar.setBounds (6, 103, getWidth() - 12, 18);
    divBar.setBounds (6, 122, getWidth() - 12, 15);
    pulses.setBounds (8, 139, getWidth() - 16, 9);
}
void CellView::refresh (int newLayer, int playmode, int pos, bool inWin, bool nowPlaying, bool here, int fl)
{
    int k;
    switch (newLayer) { case LPitch: k = playmode == Bass ? 1 : 0; break; case LVel: k = 2; break; case LGate: k = 3; break; case LChance: k = 4; break; default: k = 5 + newLayer - LModA; }
    if (k != shown) { knobs[(size_t) shown].setVisible (false); knobs[(size_t) k].setVisible (true); shown = k; }
    bool sn = false;
    const auto r = layerReadout (proc, proc.currentParams(), newLayer, cell, sn);
    if (r != readout || sn != snapped || newLayer != layer || pos != pathPos || inWin != inWindow || nowPlaying != playing || here != layerHere || fl != flash)
    {
        readout = r; snapped = sn; layer = newLayer; pathPos = pos; inWindow = inWin; playing = nowPlaying; layerHere = here; flash = fl;
        repaint();
    }
    const int pp = nowPlaying ? proc.uiPulse() : -1;
    if (pp != pulses.playingPulse) { pulses.playingPulse = pp; pulses.repaint(); }
    modeBar.repaint(); divBar.repaint();
}
void CellView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.reduced (3.0f), 7.0f);
    g.setColour (col::amber.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (3.0f), 7.0f, 1.0f);
    engrave (g, juce::String (pathPos + 1), { 9, 4, 30, 13 }, 9.5f, juce::Justification::centredLeft, inWindow ? col::amber : col::creamDim.withAlpha (0.45f));
    engrave (g, juce::String (cell + 1).paddedLeft ('0', 2), { (int) r.getRight() - 34, 4, 26, 13 }, 8.0f, juce::Justification::centredRight, col::creamDim);
    vintage::drawLed (g, { r.getCentreX(), 10.0f }, 3.4f, playing, col::ledRed);
    if (flash > 0)
    {
        const auto c = layerColour (layer);
        juce::ColourGradient glow (c.withAlpha (0.3f * (float) flash / 4.0f), r.getCentreX(), 48.0f, c.withAlpha (0.0f), r.getCentreX() + 44.0f, 48.0f, true);
        g.setGradientFill (glow); g.fillEllipse (juce::Rectangle<float> (90.0f, 90.0f).withCentre ({ r.getCentreX(), 48.0f }));
    }
    auto win = juce::Rectangle<float> (r.getWidth() - 22.0f, 17.0f).withCentre ({ r.getCentreX(), 89.0f });
    vintage::drawGlassWindow (g, win, 2.5f);
    vintage::drawGlowText (g, readout + (snapped ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) : juce::String()), win,
                           juce::Justification::centred, layerColour (layer), 12.0f);
    if (layerHere)
    {
        g.setColour (layerColour (layer));
        g.drawRoundedRectangle (r.reduced (5.0f), 6.0f, 2.0f);
    }
}
void CellView::paintOverChildren (juce::Graphics& g)
{
    if (! inWindow) { g.setColour (col::panelLo.withAlpha (0.6f)); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 7.0f); }
    if (playing) { g.setColour (col::amber.withAlpha (0.55f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 7.0f, 1.6f); }
}

// ===========================================================================
GridView::GridView (BeeProcessor& p) : proc (p)
{
    for (int c = 0; c < NS; ++c) { cells[(size_t) c] = std::make_unique<CellView> (p, c); addAndMakeVisible (*cells[(size_t) c]); }
    setInterceptsMouseClicks (false, true);
}
void GridView::resized()
{
    const int cw = getWidth() / 4, ch = getHeight() / 4;
    for (int c = 0; c < NS; ++c) cells[(size_t) c]->setBounds ((c % 4) * cw, (c / 4) * ch, cw, ch);
}
juce::Point<float> GridView::centreOf (int cell) const
{
    const float cw = (float) getWidth() / 4.0f, ch = (float) getHeight() / 4.0f;
    return { cw * ((float) (cell % 4) + 0.5f), ch * ((float) (cell / 4) + 0.5f) - 26.0f };
}
void GridView::paint (juce::Graphics& g)
{
    // honeycomb well
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::amber.withAlpha (0.045f));
    const float hs = 13.0f;
    for (float y = 0.0f, row = 0.0f; y < r.getBottom() + hs; y += hs * 1.5f, row += 1.0f)
        for (float x = (std::fmod (row, 2.0f) > 0.5f ? hs * 0.866f : 0.0f); x < r.getRight() + hs; x += hs * 1.732f)
        {
            juce::Path hex;
            for (int i = 0; i < 6; ++i)
            {
                const float a = juce::MathConstants<float>::pi / 3.0f * (float) i + juce::MathConstants<float>::pi / 6.0f;
                const juce::Point<float> pt (x + std::cos (a) * hs, y + std::sin (a) * hs);
                if (i == 0) hex.startNewSubPath (pt); else hex.lineTo (pt);
            }
            hex.closeSubPath();
            g.strokePath (hex, juce::PathStrokeType (1.0f));
        }

    const auto prm = proc.currentParams();
    const int L = Engine::stageCount (prm), off = windowOffset (prm.s.shift, L);
    juce::Path full, active;
    for (int pos = 0; pos < 16; ++pos)
    {
        const auto pt = centreOf (cellForPathPosition (prm.s.path, pos));
        if (pos == 0) full.startNewSubPath (pt); else full.lineTo (pt);
        if (pos == off) active.startNewSubPath (pt); else if (pos > off && pos < off + L) active.lineTo (pt);
    }
    g.setColour (col::amber.withAlpha (0.12f));
    g.strokePath (full, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::amber.withAlpha (0.22f));
    g.strokePath (active, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::amber.withAlpha (0.8f));
    g.strokePath (active, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // the selected layer's own path (dashed, in its colour)
    const int l = proc.param ("layer");
    const auto& lc = prm.s.layers[(size_t) l];
    if (lc.path >= 0 || lc.steps > 0 || lc.shift > 0)
    {
        const int n = Engine::effLen (prm, l), shape = lc.path >= 0 ? lc.path : prm.s.path, lo = windowOffset (lc.shift, n);
        juce::Path lp;
        for (int i = 0; i < n; ++i)
        {
            const auto pt = centreOf (cellForPathPosition (shape, juce::jlimit (0, 15, i + lo))).translated (6.0f, 6.0f);
            if (i == 0) lp.startNewSubPath (pt); else lp.lineTo (pt);
        }
        juce::Path dashed;
        const float d[] = { 7.0f, 5.0f };
        juce::PathStrokeType (2.2f).createDashedStroke (dashed, lp, d, 2);
        g.setColour (layerColour (l).withAlpha (0.9f));
        g.fillPath (dashed);
    }
}
void GridView::refresh()
{
    const auto prm = proc.currentParams();
    const int layer = proc.param ("layer"), pm = prm.playmode;
    const int L = Engine::stageCount (prm), off = windowOffset (prm.s.shift, L);
    const int playing = proc.isPlaying() ? proc.uiCell() : -1;
    const auto& lc = prm.s.layers[(size_t) layer];
    const int layerCell = lc.linked() && lc.path < 0 ? -1 : proc.uiLayerCell (layer);
    const int s = prm.s.path * 7 + prm.s.shift * 131 + L * 3001 + layer * 17 + (lc.path + 2) * 100003 + lc.steps * 7001 + lc.shift * 53;
    if (s != sig) { sig = s; repaint(); }
    static int lastHits = 0, flashFrames = 0;
    const int hits = proc.uiHits();
    if (hits != lastHits) { lastHits = hits; flashFrames = 4; } else if (flashFrames > 0) --flashFrames;
    for (int c = 0; c < NS; ++c)
    {
        const int pos = shapeTable()[(size_t) prm.s.path][(size_t) c] - 1;
        cells[(size_t) c]->refresh (layer, pm, pos, pos >= off && pos < off + L, c == playing, c == layerCell,
                                    (c == proc.uiHitCell() && proc.isPlaying()) ? flashFrames : 0);
    }
}

// ===========================================================================
void PathPicker::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    vintage::drawGlassWindow (g, r, 4.0f);
    const auto pid = id();
    const int v = (int) std::lround (proc.apvts.getRawParameterValue (pid)->load()) - (follow ? 1 : 0);
    const bool following = v < 0;
    const int shape = following ? (fallbackShape ? fallbackShape() : 0) : v;
    const int L = lengthFn ? lengthFn() : 16, off = offsetFn ? offsetFn() : 0;
    auto inner = r.reduced (r.getWidth() * 0.12f);
    auto pt = [&] (int c) { return juce::Point<float> (inner.getX() + inner.getWidth() * ((float) (c % 4) + 0.5f) / 4.0f, inner.getY() + inner.getHeight() * ((float) (c / 4) + 0.5f) / 4.0f); };
    juce::Path pth;
    for (int i = 0; i < L; ++i) { const auto p = pt (cellForPathPosition (shape, juce::jlimit (0, 15, i + off))); if (i == 0) pth.startNewSubPath (p); else pth.lineTo (p); }
    g.setColour (colour.withAlpha (following ? 0.3f : 0.95f));
    g.strokePath (pth, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int c = 0; c < 16; ++c) { g.setColour (colour.withAlpha (following ? 0.15f : 0.4f)); g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (pt (c))); }
    g.setColour (col::cream);
    g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt (cellForPathPosition (shape, juce::jlimit (0, 15, off)))), 1.2f);
    g.setFont (vintage::displayFont (10.0f));
    g.setColour (colour);
    g.drawText (following ? juce::String ("FOLLOW") : juce::String (shapeName (shape)), r.removeFromBottom (13.0f), juce::Justification::centred, false);
}
void PathPicker::mouseDown (const juce::MouseEvent& e)
{
    auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id()));
    if (! prm) return;
    const int n = prm->choices.size(), v = prm->getIndex();
    setParamValue (proc, id(), (float) ((v + (e.mods.isShiftDown() ? n - 1 : 1)) % n));
    repaint();
}

// ===========================================================================
juce::Rectangle<float> KeyRow::keyRect (int pc) const
{
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / 7.0f;
    static const int wi[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    if (! isBlack (pc)) return { r.getX() + ww * (float) wi[pc], r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.62f;
    return { r.getX() + ww * (float) (wi[pc] + 1) - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}
void KeyRow::paint (juce::Graphics& g)
{
    const int ov = maskOverride ? maskOverride() : -1;
    auto draw = [&] (int pc)
    {
        auto k = keyRect (pc).reduced (0.8f, 0.0f);
        const bool black = isBlack (pc);
        const bool on = ov >= 0 ? ((ov >> pc) & 1) : proc.param (prefix + juce::String (pc)) > 0;
        juce::ColourGradient grad (black ? juce::Colour (0xff2c2a26) : juce::Colour (0xfff5ecd2), k.getX(), k.getY(),
                                   black ? juce::Colour (0xff0a0908) : juce::Colour (0xffcbbf9f), k.getX(), k.getBottom(), false);
        g.setGradientFill (grad); g.fillRoundedRectangle (k, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRoundedRectangle (k, 2.0f, 1.0f);
        vintage::drawLed (g, { k.getCentreX(), k.getBottom() - (black ? 7.0f : 8.0f) }, 2.5f, on);
    };
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) draw (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) draw (pc);
    if (dimmed && dimmed()) { g.setColour (col::panelLo.withAlpha (0.5f)); g.fillRect (getLocalBounds()); }
}
void KeyRow::mouseDown (const juce::MouseEvent& e)
{
    if (maskOverride && maskOverride() >= 0) return;
    int hit = -1;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    for (int pc = 0; pc < 12 && hit < 0; ++pc) if (! isBlack (pc) && keyRect (pc).contains (e.position)) hit = pc;
    if (hit < 0) return;
    const bool on = proc.param (prefix + juce::String (hit)) > 0;
    if (on) { int count = 0; for (int k = 0; k < 12; ++k) if (proc.param (prefix + juce::String (k)) > 0) ++count; if (count <= 1) return; }
    setParamValue (proc, ids::id (prefix + juce::String (hit)), on ? 0.0f : 1.0f);
    repaint();
}

// ===========================================================================
void DiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f).translated (0.0f, down ? 1.5f : 0.0f);
    if (heat > 0)
    {
        const float h = juce::jmin (1.0f, (float) heat / 6.0f);
        juce::ColourGradient fire (juce::Colour (0xffffa000).withAlpha (0.6f * h), r.getCentreX(), r.getCentreY(), juce::Colour (0xffffe000).withAlpha (0.0f), r.getCentreX(), r.getY() - 18.0f * h, true);
        g.setGradientFill (fire); g.fillEllipse (r.expanded (10.0f + 8.0f * h));
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (0.0f, 2.5f), 10.0f);
    juce::ColourGradient body (col::orangeCap.brighter (over ? 0.3f : 0.15f), r.getX(), r.getY(), col::orangeCap.darker (0.45f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (body); g.fillRoundedRectangle (r, 10.0f);
    // bee stripes
    g.saveState();
    juce::Path clipP; clipP.addRoundedRectangle (r, 10.0f); g.reduceClipRegion (clipP);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 14.0f)
    {
        juce::Path s; s.addQuadrilateral (x, r.getBottom(), x + 6.0f, r.getBottom(), x + 6.0f + r.getHeight(), r.getY(), x + r.getHeight(), r.getY());
        g.fillPath (s);
    }
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 1.0f);
    const float s = r.getWidth() / 4.0f;
    auto pip = [&] (float gx, float gy) { g.setColour (juce::Colour (0xff1a1404)); g.fillEllipse (juce::Rectangle<float> (s * 0.55f, s * 0.55f).withCentre ({ r.getX() + s * gx, r.getY() + s * gy })); };
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
int SlotGrid::slotAt (juce::Point<float> p) const { for (int n = 0; n < 8; ++n) if (slotRect (n).contains (p)) return n; return -1; }
void SlotGrid::paint (juce::Graphics& g)
{
    for (int n = 0; n < 8; ++n)
    {
        auto r = slotRect (n);
        const bool filled = proc.slotFilled (n), active = proc.activeSlot == n, pending = proc.pendingSlot == n;
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRoundedRectangle (r.expanded (1.0f), 4.0f);
        const auto base = active ? col::amber : (filled ? col::keycap : juce::Colour (0xff36332b));
        juce::ColourGradient cap (base.brighter (0.15f), r.getX(), r.getY(), base.darker (0.4f), r.getX(), r.getBottom(), false);
        g.setGradientFill (cap); g.fillRoundedRectangle (r, 3.0f);
        if (pending && (blink / 6) % 2 == 0) { g.setColour (col::amberHot.withAlpha (0.7f)); g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 2.0f); }
        if (proc.saveArm) { g.setColour (col::ledRed.withAlpha (0.6f)); g.drawRoundedRectangle (r.expanded (1.0f), 4.0f, 1.0f); }
        g.setFont (vintage::font (12.0f));
        g.setColour (filled || active ? juce::Colour (0xff201c12) : col::creamDim);
        g.drawText (juce::String (n + 1), r, juce::Justification::centred, false);
    }
}
void SlotGrid::mouseUp (const juce::MouseEvent& e)
{
    const int n = slotAt (e.position);
    if (n >= 0)
    {
        if (dragFrom >= 0 && n != dragFrom) proc.copySlot (dragFrom, n);
        else if (n == dragFrom) proc.slotClick (n, e.mods.isShiftDown() ? 1 : (e.mods.isAltDown() ? 2 : 0));
    }
    dragFrom = -1;
    repaint();
}

// ===========================================================================
void LayerMatrix::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    vintage::drawGlassWindow (g, r, 4.0f);
    const auto prm = proc.currentParams();
    const int sel = proc.param ("layer");
    const float rh = (r.getHeight() - 22.0f) / (float) kNumLayers;
    g.setFont (vintage::displayFont (9.5f));
    g.setColour (col::creamDim);
    const float xs[] = { 22.0f, 74.0f, 104.0f, 132.0f };
    const char* heads[] = { "LAYER", "STEP", "PATH", "RATE" };
    for (int i = 0; i < 4; ++i) g.drawText (heads[i], juce::Rectangle<float> (r.getX() + xs[i], r.getY() + 4.0f, 50.0f, 13.0f), juce::Justification::centredLeft);
    static const char* rates[] = { "STG", "1/32", "1/16", "1/8", "1/4", "1/2" };
    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto row = juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + 20.0f + rh * (float) l, r.getWidth() - 6.0f, rh);
        if (l == sel) { g.setColour (layerColour (l).withAlpha (0.18f)); g.fillRoundedRectangle (row, 3.0f); }
        const auto& c = prm.s.layers[(size_t) l];
        const bool modOff = l >= LModA && ! prm.mods[(size_t) (l - LModA)].on;
        g.setColour (layerColour (l).withAlpha (modOff ? 0.3f : 1.0f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ row.getX() + 9.0f, row.getCentreY() }));
        g.setColour (modOff ? col::creamDim.withAlpha (0.5f) : col::cream);
        auto cellTxt = [&] (int i, const juce::String& t) { g.drawText (t, juce::Rectangle<float> (r.getX() + xs[i], row.getY(), 52.0f, rh), juce::Justification::centredLeft); };
        cellTxt (0, ids::layerName (l));
        cellTxt (1, c.steps > 0 ? juce::String (c.steps) : juce::String ("LNK"));
        cellTxt (2, c.path >= 0 ? juce::String (shapeName (c.path)) : juce::String ("--"));
        cellTxt (3, rates[juce::jlimit (0, 5, c.rate)]);
    }
}
void LayerMatrix::mouseDown (const juce::MouseEvent& e)
{
    const float rh = ((float) getHeight() - 22.0f) / (float) kNumLayers;
    const int l = (int) ((e.position.y - 20.0f) / rh);
    if (l >= 0 && l < kNumLayers) setParamValue (proc, ids::id ("layer"), (float) l);
}

// ===========================================================================
void ClipButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 8) return;
    dragging = true;
    auto f = proc.exportClip();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    else dragging = false;
}

// ===========================================================================
Content::Content (BeeProcessor& p)
    : proc (p), grid (p),
      seqPath (p, [] { return ids::id ("path"); }, false),
      userKeys (p, "user"), poolKeys (p, "cust"), clip (p), slots (p), matrix (p),
      layerPath (p, [&p] { return ids::layer (p.param ("layer"), "path"); }, true)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (grid);

    const char* modes[] = { "SEQ", "BASS", "ARP" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtns[(size_t) i];
        b.setButtonText (modes[i]); b.setClickingTogglesState (true); b.setRadioGroupId (4001);
        b.onClick = [this, i] { if (modeBtns[(size_t) i].getToggleState()) setParam (ids::id ("playmode"), (float) i); };
        addAndMakeVisible (b);
    }
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& b = layerBtns[(size_t) l];
        b.setButtonText (ids::layerName (l)); b.setClickingTogglesState (true); b.setRadioGroupId (4002);
        b.onClick = [this, l] { if (layerBtns[(size_t) l].getToggleState()) setParam (ids::id ("layer"), (float) l); };
        addAndMakeVisible (b);
    }

    knob (transpose, "big", "Transpose: +-24 semitones, before the scale");             attach (transpose, ids::id ("transpose"));
    knob (swing, "small", "Swing: delays every second pulse");                           attach (swing, ids::id ("swing"));
    knob (bassnote, "small", "Bass note (a key you play overrides it)");                  attach (bassnote, ids::id ("bassnote"));
    knob (arpoct, "small", "Arp: spread held notes over 1-4 octaves");                   attach (arpoct, ids::id ("arpoct"));
    knob (length, "big", "Stages: how many positions of the path play (1-16)");           attach (length, ids::id ("length"));
    knob (shift, "small", "Shift: slides the stage window along the path");               attach (shift, ids::id ("shift"));
    knob (pulses, "small", "Count by Pulse: the cycle restarts after this many pulses");  attach (pulses, ids::id ("pulses"));
    knob (notes, "small", "Notes: how many different pitches a roll may use");            attach (notes, ids::id ("notes"));
    knob (octjump, "small", "Oct: chance that a rolled stage jumps an octave");           attach (octjump, ids::id ("octjump"));

    for (auto [b, n] : std::initializer_list<std::pair<juce::ComboBox*, const char*>> {
            { &clock, "clock" }, { &direction, "direction" }, { &countby, "countby" }, { &arporder, "arporder" },
            { &scale, "scale" }, { &root, "root" }, { &randtarget, "randtarget" }, { &pool, "pool" },
            { &evolve, "evolve" }, { &evotarget, "evotarget" }, { &midich, "midich" }, { &midiin, "midiin" } })
        attach (*b, ids::id (n));
    midiin.setTooltip ("MIDI In: Play = keys feed Bass / Arp; Remote = C4-B4 root, C2 roll all, D2 roll pitch, C3-B3 chord -> User scale");
    scale.setTooltip ("Scale: Off, Snake's 36 scales, natural minor, or User (keys below / MIDI chord)");

    for (auto [b, n] : std::initializer_list<std::pair<juce::TextButton*, const char*>> {
            { &retrig, "retrig" }, { &latch, "latch" }, { &thru, "thru" }, { &resetstop, "resetstop" }, { &chord, "chord" } })
    { b->setClickingTogglesState (true); attach (*b, ids::id (n)); }
    chord.setTooltip ("MIDI Chord: with MIDI In on Remote, hold C3-B3 to set the User scale");

    for (auto* b : { &nudgeL, &nudgeR }) { b->getProperties().set ("cap", "arrow"); addAndMakeVisible (*b); }
    nudgeL.onClick = [this] { proc.nudge (-1); };
    nudgeR.onClick = [this] { proc.nudge (1); };
    nudgeL.setTooltip ("Nudge: move every stage one place back along the path");
    nudgeR.setTooltip ("Nudge: move every stage one place forward along the path");
    back.onClick = [this] { proc.goBack(); };
    fwd.onClick = [this] { proc.goForward(); };
    seqreset.onClick = [this] { proc.seqReset(); };
    for (auto* b : { &back, &fwd, &seqreset }) addAndMakeVisible (*b);
    saveArm.setClickingTogglesState (true);
    saveArm.onClick = [this] { proc.saveArm = saveArm.getToggleState(); slots.repaint(); };
    addAndMakeVisible (saveArm);

    dice.onClick = [this] { proc.randomize(); dice.face = 1 + juce::Random::getSystemRandom().nextInt (6); dice.repaint(); };
    dice.setTooltip ("Roll: Layer, Rhythm, All or Paths, using the pool and each layer's random range");
    addAndMakeVisible (dice);
    clip.getProperties().set ("cap", "orange");
    clip.onClick = [this] { auto f = proc.exportClip(); if (f.existsAsFile()) f.revealToUser(); };
    clip.setTooltip ("MIDI Clip: click to save a .mid of one cycle (Music/Bumblebee), or drag onto a DAW track");
    addAndMakeVisible (clip);
    slots.setTooltip ("Patterns: click recall (at the cycle end while playing), shift-click save, alt-click clear, drag to copy");
    addAndMakeVisible (slots);
    matrix.setTooltip ("All layers: steps (LNK = linked), own path, rate. Click a row to edit that layer.");
    addAndMakeVisible (matrix);

    seqPath.lengthFn = [this] { return proc.param ("length"); };
    seqPath.offsetFn = [this] { return windowOffset (proc.param ("shift"), proc.param ("length")); };
    seqPath.setTooltip ("Path: the order the stages play through the grid. Click for the next, shift-click for the previous.");
    addAndMakeVisible (seqPath);

    userKeys.dimmed = [this] { return proc.param ("scale") != kScaleUser; };
    userKeys.maskOverride = [this] { return proc.param ("chord") > 0 ? (int) proc.chordMask() : -1; };
    userKeys.setTooltip ("User scale: click keys (scale = User)");
    poolKeys.dimmed = [this] { return proc.param ("pool") != 7; };
    poolKeys.setTooltip ("Custom pool: the notes the Custom pool may roll");
    addAndMakeVisible (userKeys); addAndMakeVisible (poolKeys);

    // layer panel
    for (auto* s : { &lSteps, &lShift, &lLo, &lHi }) { styleKnob (*s, "small"); addAndMakeVisible (*s); }
    for (auto* s : { &mLo, &mHi }) { styleKnob (*s, "small"); addAndMakeVisible (*s); }
    lSteps.setTooltip ("Steps: 0 = linked to Stages; 1-16 = this layer loops on its own length");
    lShift.setTooltip ("Shift: slides this layer's window along its path");
    lLo.setTooltip ("Random low: the lowest value a roll gives this layer");
    lHi.setTooltip ("Random high: the highest value a roll gives this layer");
    for (auto* c : { &lPath, &lDir, &lRate, &pitchMode, &chroll, &mKind, &mCC, &mGlide }) addAndMakeVisible (*c);
    lRate.setTooltip ("Rate: Stage = moves once per stage; 1/32-1/2 = runs on its own clock (Snake lanes)");
    lDir.setTooltip ("Direction for this layer, or Follow the sequence");
    pitchMode.setTooltip ("Semitone: dial = semitones, snapped to the scale. Degree: dial counts scale notes from the root");
    for (auto* b : { &lLink, &lRandom, &lReset, &mOn, &learn }) addAndMakeVisible (*b);
    mOn.setClickingTogglesState (true);
    learn.setClickingTogglesState (true);
    lLink.onClick = [this]
    {
        const int l = proc.param ("layer");
        const juce::String k = "L" + juce::String (ids::layerKey (l)) + "_";
        if (proc.param (k + "steps") > 0 || proc.param (k + "rate") > 0 || proc.param (k + "dir") > 0)
        { setParam (ids::layer (l, "steps"), 0); setParam (ids::layer (l, "rate"), 0); setParam (ids::layer (l, "dir"), 0); setParam (ids::layer (l, "shift"), 0); }
        else setParam (ids::layer (l, "steps"), (float) proc.param ("length"));
    };
    lLink.setTooltip ("Link this layer back to the stages, or unlink it at the current length");
    lRandom.getProperties().set ("cap", "orange");
    lRandom.onClick = [this] { proc.randomizeLayer (proc.param ("layer")); };
    lReset.onClick = [this] { proc.resetLayer (proc.param ("layer")); };
    learn.onClick = [this] { proc.armLearn (learn.getToggleState()); };
    learn.setTooltip ("Learn: move a knob on your controller to set this MOD lane's CC");
    layerPath.fallbackShape = [this] { return proc.param ("path"); };
    layerPath.lengthFn = [this] { return Engine::effLen (proc.currentParams(), proc.param ("layer")); };
    layerPath.offsetFn = [this]
    {
        const auto prm = proc.currentParams(); const int l = proc.param ("layer");
        return prm.s.layers[(size_t) l].linked() ? windowOffset (prm.s.shift, Engine::stageCount (prm)) : windowOffset (prm.s.layers[(size_t) l].shift, Engine::effLen (prm, l));
    };
    layerPath.setTooltip ("This layer's path: Follow the sequence, or its own snake shape. Click to change.");
    addAndMakeVisible (layerPath);

    renderBackground();
    setSize (W, H);
    bindLayerControls();
    timerCallback();
    startTimerHz (30);
}

Content::~Content() { stopTimer(); setLookAndFeel (nullptr); }

void Content::setParam (const juce::String& fullId, float v) { setParamValue (proc, fullId, v); }
juce::Slider& Content::knob (juce::Slider& s, const char* style, const juce::String& tip) { styleKnob (s, style); s.setTooltip (tip); addAndMakeVisible (s); return s; }
void Content::attach (juce::Slider& s, const juce::String& id) { sAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, id, s)); }
void Content::attach (juce::ComboBox& b, const juce::String& id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) b.addItemList (c->choices, 1);
    addAndMakeVisible (b);
    cAtts.push_back (std::make_unique<ComboAtt> (proc.apvts, id, b));
}
void Content::attach (juce::Button& b, const juce::String& id) { addAndMakeVisible (b); bAtts.push_back (std::make_unique<ButtonAtt> (proc.apvts, id, b)); }

void Content::bindLayerControls()
{
    const int l = proc.param ("layer");
    if (l == boundLayer) return;
    boundLayer = l;
    lsAtts.clear(); lcAtts.clear(); lbAtts.clear();
    auto combo = [&] (juce::ComboBox& b, const juce::String& id)
    {
        b.clear (juce::dontSendNotification);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) b.addItemList (c->choices, 1);
        lcAtts.push_back (std::make_unique<ComboAtt> (proc.apvts, id, b));
    };
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::layer (l, "steps"), lSteps));
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::layer (l, "shift"), lShift));
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::layer (l, "rlo"), lLo));
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::layer (l, "rhi"), lHi));
    combo (lPath, ids::layer (l, "path")); combo (lDir, ids::layer (l, "dir")); combo (lRate, ids::layer (l, "rate"));
    combo (pitchMode, ids::id ("pitchmode")); combo (chroll, ids::id ("chroll"));
    const int m = l >= LModA ? l - LModA : 0;
    combo (mKind, ids::mod (m, "kind")); combo (mCC, ids::mod (m, "cc")); combo (mGlide, ids::mod (m, "glide"));
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::mod (m, "lo"), mLo));
    lsAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, ids::mod (m, "hi"), mHi));
    lbAtts.push_back (std::make_unique<ButtonAtt> (proc.apvts, ids::mod (m, "on"), mOn));
    const auto c = layerColour (l);
    for (auto* s : { &lSteps, &lShift, &lLo, &lHi, &mLo, &mHi }) { s->getProperties().set ("arc", (juce::int64) c.getARGB()); s->repaint(); }
    layerPath.colour = c;
    layerTitle = "LAYER" + dot() + ids::layerName (l);
    resized();
    repaint();
}

// ---- layout (design 1400 x 800) -------------------------------------------
void Content::resized()
{
    captions.clear();
    auto cap = [this] (int x, int y, int w, const juce::String& t, std::function<bool()> vis = {}) { captions.push_back ({ { x, y, w, 13 }, t, vis }); };
    auto pm = [this] { return proc.param ("playmode"); };
    const int l = proc.param ("layer");
    const bool isMod = l >= LModA;

    // PLAY (24..214)
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i].setBounds (32 + i * 60, 88, 56, 40);
    transpose.setBounds (28, 138, 84, 84);              cap (28, 224, 84, "TRANSPOSE");
    clock.setBounds (34, 266, 80, 28);                  cap (34, 250, 80, "CLOCK");
    swing.setBounds (134, 244, 56, 56);                 cap (122, 300, 80, "SWING");
    nudgeL.setBounds (34, 340, 42, 30); nudgeR.setBounds (78, 340, 42, 30); cap (34, 324, 86, "NUDGE");
    bassnote.setBounds (36, 398, 58, 58);               cap (26, 456, 78, "BASS NOTE", [pm] { return pm() == 1; });
    arporder.setBounds (34, 400, 170, 28);              cap (34, 384, 170, "ARP STYLE", [pm] { return pm() == 2; });
    arpoct.setBounds (36, 436, 52, 52);                 cap (26, 488, 72, "OCT", [pm] { return pm() == 2; });
    latch.setBounds (108, 444, 96, 36);
    retrig.setBounds (108, 490, 96, 36);
    midiin.setBounds (34, 586, 170, 28);                cap (34, 570, 170, "MIDI IN");
    thru.setBounds (34, 624, 82, 38); resetstop.setBounds (122, 624, 82, 38);
    midich.setBounds (34, 694, 170, 28);                cap (34, 678, 170, "MIDI CHANNEL");

    // GRID (214..734)
    for (int i = 0; i < kNumLayers; ++i) layerBtns[(size_t) i].setBounds (222 + i * 63, 66, 60, 38);
    grid.setBounds (226, 110, 496, 600);

    // SEQUENCE (734..974)
    length.setBounds (742, 86, 70, 70);                 cap (737, 156, 80, "STAGES");
    shift.setBounds (824, 92, 56, 56);                  cap (812, 156, 80, "SHIFT");
    direction.setBounds (888, 104, 78, 28);             cap (888, 88, 78, "DIRECTION");
    seqPath.setBounds (744, 176, 96, 96);               cap (744, 274, 96, "PATH");
    countby.setBounds (852, 194, 112, 28);              cap (852, 178, 112, "COUNT BY");
    pulses.setBounds (884, 228, 48, 48);                cap (868, 276, 80, "PULSES");

    // LAYER (734..974)
    lSteps.setBounds (744, 340, 56, 56);                cap (732, 396, 80, "STEPS");
    lLink.setBounds (806, 352, 58, 32);
    lRate.setBounds (872, 356, 92, 28);                 cap (872, 340, 92, "RATE");
    layerPath.setBounds (744, 420, 80, 80);
    lPath.setBounds (834, 430, 130, 28);                cap (834, 414, 130, "OWN PATH");
    lDir.setBounds (834, 478, 130, 28);                 cap (834, 462, 130, "DIRECTION");
    lShift.setBounds (744, 518, 52, 52);                cap (730, 570, 80, "SHIFT");
    lLo.setBounds (818, 518, 52, 52);                   cap (804, 570, 80, "RND LOW");
    lHi.setBounds (896, 518, 52, 52);                   cap (882, 570, 80, "RND HIGH");
    lRandom.setBounds (744, 590, 108, 34); lReset.setBounds (856, 590, 108, 34);
    pitchMode.setBounds (744, 660, 220, 28);            cap (744, 644, 220, "PITCH MODE", [this] { return proc.param ("layer") == LPitch; });
    chroll.setBounds (744, 660, 220, 28);               cap (744, 644, 220, "CHANCE ROLL", [this] { return proc.param ("layer") == LChance; });
    mOn.setBounds (744, 652, 58, 34);
    mKind.setBounds (808, 656, 156, 28);                cap (808, 640, 156, "TARGET", [this] { return proc.param ("layer") >= LModA; });
    mCC.setBounds (744, 700, 160, 28);
    learn.setBounds (908, 696, 56, 34);
    mGlide.setBounds (744, 744, 84, 28);
    mLo.setBounds (836, 736, 40, 40);                   cap (874, 750, 30, "MIN", [this] { return proc.param ("layer") >= LModA; });
    mHi.setBounds (904, 736, 40, 40);                   cap (942, 750, 30, "MAX", [this] { return proc.param ("layer") >= LModA; });
    for (auto* c : std::initializer_list<juce::Component*> { &mOn, &mKind, &mCC, &learn, &mGlide, &mLo, &mHi }) c->setVisible (isMod);
    pitchMode.setVisible (l == LPitch); chroll.setVisible (l == LChance);

    // SCALE + RANDOMIZE + PATTERNS (974..1188)
    scale.setBounds (984, 88, 194, 28);
    root.setBounds (984, 126, 70, 28);
    chord.setBounds (1062, 122, 116, 36);
    userKeys.setBounds (986, 182, 190, 54);             cap (984, 166, 194, "USER SCALE");
    dice.setBounds (986, 278, 80, 80);
    randtarget.setBounds (1076, 290, 102, 28);          cap (1076, 274, 102, "TARGET");
    pool.setBounds (1076, 336, 102, 28);                cap (1076, 320, 102, "POOL");
    notes.setBounds (996, 376, 50, 50);                 cap (982, 426, 80, "NOTES");
    octjump.setBounds (1102, 376, 50, 50);              cap (1088, 426, 80, "OCT %");
    back.setBounds (984, 446, 95, 34); fwd.setBounds (1083, 446, 95, 34);
    seqreset.setBounds (984, 486, 194, 34);
    slots.setBounds (984, 556, 194, 80);
    saveArm.setBounds (986, 640, 190, 30);
    evolve.setBounds (984, 712, 106, 28);               cap (984, 696, 194, "EVOLVE");
    evotarget.setBounds (1094, 712, 84, 28);

    // TOOLS (1188..1376)
    poolKeys.setBounds (1198, 100, 168, 54);           cap (1198, 84, 168, "CUSTOM POOL");
    clip.setBounds (1198, 162, 168, 40);
    matrix.setBounds (1198, 330, 168, 290);             cap (1198, 314, 168, "LAYER MATRIX");
}

// ---- refresh ---------------------------------------------------------------
void Content::timerCallback()
{
    bindLayerControls();
    const int layer = proc.param ("layer"), pm = proc.param ("playmode");
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i].setToggleState (i == pm, juce::dontSendNotification);
    for (int l = 0; l < kNumLayers; ++l) layerBtns[(size_t) l].setToggleState (l == layer, juce::dontSendNotification);
    bassnote.setVisible (pm == 1);
    arporder.setVisible (pm == 2); arpoct.setVisible (pm == 2); latch.setVisible (pm == 2);
    retrig.setVisible (pm != 0);
    pulses.setAlpha (proc.param ("countby") == 1 ? 1.0f : 0.4f);
    root.setAlpha (proc.param ("scale") == kScaleOff ? 0.4f : 1.0f);
    learn.setToggleState (proc.learnArm, juce::dontSendNotification);
    saveArm.setToggleState (proc.saveArm, juce::dontSendNotification);
    const juce::String k = "L" + juce::String (ids::layerKey (layer)) + "_";
    const bool unlinked = proc.param (k + "steps") > 0 || proc.param (k + "rate") > 0 || proc.param (k + "dir") > 0;
    lLink.setButtonText (unlinked ? "RELINK" : "UNLINK");

    grid.refresh();
    seqPath.repaint(); layerPath.repaint();
    userKeys.repaint(); poolKeys.repaint(); matrix.repaint();
    ++slots.blink; slots.repaint();
    dice.heat = proc.rollsInWindow() >= 4 ? proc.rollsInWindow() - 3 : 0; dice.repaint();

    const int tr = proc.param ("transpose");
    const auto newT = (tr > 0 ? "+" : "") + juce::String (tr) + " st";
    juce::String newS = proc.status;
    if (juce::Time::getMillisecondCounterHiRes() - proc.statusTime > 6000 || newS.isEmpty())
    {
        const int sc = proc.param ("scale");
        newS = sc == kScaleOff ? juce::String ("Chromatic, no scale")
                               : ids::noteNames()[proc.param ("root")] + " " + scaleList()[(size_t) sc].name
                                 + (proc.param ("pitchmode") == 1 ? " (degrees)" : "");
    }
    const int hits = proc.uiHits();
    if (hits != lastHits) { lastHits = hits; flashFrames = 3; const int lp = proc.lastPitch(); nowText = lp >= 0 ? juce::String (noteName (lp)) : "--"; }
    else if (flashFrames > 0) --flashFrames;
    if (newT != transposeText || newS != statusText || flashFrames > 0 || flashFrames == 0)
    {
        transposeText = newT; statusText = newS;
        repaint (24, 130, 190, 100); repaint (226, 714, 496, 40); repaint (1188, 210, 188, 100); repaint (734, 300, 240, 30);
    }
}

void Content::renderBackground()
{
    const float k = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * k), (int) (H * k), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (k));
    juce::Random rnd (1066);

    juce::ColourGradient base (col::panelHi, 0, 0, col::panel, 0, (float) H, false);
    g.setGradientFill (base); g.fillRect (0, 0, W, H);
    for (int i = 0; i < 1100; ++i)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.016f + rnd.nextFloat() * 0.018f));
        g.drawHorizontalLine ((int) (rnd.nextFloat() * H), 0.0f, (float) W);
    }
    // header with hazard stripes
    {
        juce::ColourGradient hg (juce::Colour (0xff0f0e0b), 0, 0, juce::Colour (0xff1c1a15), 0, 58.0f, false);
        g.setGradientFill (hg); g.fillRect (24.0f, 0.0f, (float) W - 48.0f, 58.0f);
        g.saveState();
        g.reduceClipRegion (juce::Rectangle<int> (306, 12, 96, 34));
        for (float x = 266.0f; x < 420.0f; x += 20.0f)
        {
            juce::Path s; s.addQuadrilateral (x, 46.0f, x + 10.0f, 46.0f, x + 44.0f, 12.0f, x + 34.0f, 12.0f);
            g.setColour (col::amber); g.fillPath (s);
        }
        g.restoreState();
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawRect (306, 12, 96, 34, 1);
        g.setFont (vintage::font (30.0f, true, 0.16f));
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawText ("BUMBLEBEE", 42, 11, 262, 38, juce::Justification::centredLeft);
        g.setColour (col::amber);                            g.drawText ("BUMBLEBEE", 40, 9, 262, 38, juce::Justification::centredLeft);
        engrave (g, "STAGE  x  PATH  SEQUENCER", { 420, 14, 380, 16 }, 12.5f, juce::Justification::centredLeft);
        engrave (g, "16 STAGES ON A 4x4 GRID" + dot() + "14 PATHS" + dot() + "8 LAYERS, EACH WITH ITS OWN LOOP" + dot() + "4 MOD LANES",
                 { 420, 32, 640, 14 }, 8.5f, juce::Justification::centredLeft, col::creamDim);
        engrave (g, "MODEL BB-1" + dot() + "VST3", { 1150, 20, 216, 16 }, 9.5f, juce::Justification::centredRight, col::creamDim);
        g.setColour (col::amber.withAlpha (0.7f)); g.drawHorizontalLine (57, 24.0f, (float) W - 24.0f);
    }
    const int xs[] = { 24, 214, 734, 974, 1188, W - 24 };
    for (int i = 1; i < 5; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawVerticalLine (xs[i], 60.0f, (float) H - 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawVerticalLine (xs[i] + 1, 60.0f, (float) H - 14.0f);
    }
    for (int i = 0; i < 5; ++i)
        for (auto y : { 68.0f, (float) H - 24.0f })
        {
            vintage::drawScrew (g, { (float) xs[i] + 9.0f, y }, 3.6f, rnd.nextFloat() * 3.0f);
            vintage::drawScrew (g, { (float) xs[i + 1] - 9.0f, y }, 3.6f, rnd.nextFloat() * 3.0f);
        }
    engrave (g, "PLAY", { 24, 64, 190, 16 }, 11.0f);
    engrave (g, "SEQUENCE", { 734, 64, 240, 16 }, 11.0f);
        engrave (g, "RANDOMIZE", { 974, 254, 214, 16 }, 11.0f);
    engrave (g, "PATTERNS", { 974, 534, 214, 16 }, 11.0f);
    engrave (g, "CLICK RECALL" + dot() + "SHIFT SAVE" + dot() + "ALT CLEAR", { 974, 674, 214, 12 }, 7.0f, juce::Justification::centred, col::creamDim);
        engrave (g, "CLICK SAVES" + dot() + "DRAG INTO YOUR DAW", { 1188, 204, 188, 12 }, 7.0f, juce::Justification::centred, col::creamDim);
    engrave (g, "NOW PLAYING", { 1188, 222, 188, 13 }, 9.0f);
    g.setColour (col::creamDim.withAlpha (0.35f));
    g.drawHorizontalLine (300, 744.0f, 964.0f);
    g.drawHorizontalLine (246, 984.0f, 1178.0f);
    g.drawHorizontalLine (526, 984.0f, 1178.0f);
    g.drawHorizontalLine (634, 744.0f, 964.0f);
    vintage::drawGlassWindow (g, { 116.0f, 158.0f, 90.0f, 42.0f }, 4.0f);
    vintage::drawGlassWindow (g, { 230.0f, 716.0f, 488.0f, 30.0f }, 3.0f);
    vintage::drawGlassWindow (g, { 1198.0f, 238.0f, 168.0f, 58.0f }, 4.0f);
    // a little bee badge at the bottom of the tools column
    {
        const juce::Point<float> c (1282.0f, 700.0f);
        g.setColour (col::amber.withAlpha (0.12f));
        for (int i = 0; i < 6; ++i)
        {
            juce::Path hex;
            for (int j = 0; j < 6; ++j)
            {
                const float a = juce::MathConstants<float>::pi / 3.0f * (float) j + juce::MathConstants<float>::pi / 6.0f;
                const auto o = c + juce::Point<float> (std::cos (juce::MathConstants<float>::pi / 3.0f * (float) i), std::sin (juce::MathConstants<float>::pi / 3.0f * (float) i)) * 38.0f;
                const juce::Point<float> pt (o.x + std::cos (a) * 20.0f, o.y + std::sin (a) * 20.0f);
                if (j == 0) hex.startNewSubPath (pt); else hex.lineTo (pt);
            }
            hex.closeSubPath(); g.strokePath (hex, juce::PathStrokeType (1.5f));
        }
        g.setColour (col::amber.withAlpha (0.9f)); g.fillEllipse (juce::Rectangle<float> (34.0f, 22.0f).withCentre (c));
        g.setColour (juce::Colours::black);
        for (float x : { -6.0f, 4.0f }) g.fillRect (juce::Rectangle<float> (5.0f, 20.0f).withCentre (c.translated (x, 0.0f)));
        g.setColour (col::cream.withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 9.0f).withCentre (c.translated (-4.0f, -13.0f)));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 9.0f).withCentre (c.translated (6.0f, -13.0f)));
        engrave (g, "BB-1", { 1232, 742, 100, 12 }, 8.0f, juce::Justification::centred, col::creamDim);
    }
    for (auto x : { 0, W - 24 })
    {
        juce::Rectangle<float> w ((float) x, 0.0f, 24.0f, (float) H);
        juce::ColourGradient wg (col::woodLight, w.getX(), 0, col::wood.darker (0.3f), w.getRight(), 0, false);
        wg.addColour (0.5, col::wood);
        g.setGradientFill (wg); g.fillRect (w);
        for (int i = 0; i < 26; ++i)
        {
            const float gx = w.getX() + 2.0f + rnd.nextFloat() * 20.0f;
            g.setColour (juce::Colour (0xff2a1408).withAlpha (0.1f + rnd.nextFloat() * 0.18f));
            g.drawLine (gx, 0.0f, gx + rnd.nextFloat() * 2.0f - 1.0f, (float) H, 0.6f + rnd.nextFloat());
        }
    }
    g.setColour (juce::Colour (0xff0d0c0a)); g.fillRect (24, H - 12, W - 48, 12);
}

void Content::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    engrave (g, "SCALE", { 974, 64, 214, 16 }, 11.0f);
    engrave (g, "TOOLS", { 1188, 64, 188, 16 }, 11.0f);
    engrave (g, layerTitle, { 734, 308, 240, 16 }, 11.0f, juce::Justification::centred, layerColour (proc.param ("layer")));
    for (auto& c : captions) if (! c.visible || c.visible()) engrave (g, c.text, c.r, 8.5f);
    vintage::drawGlowText (g, transposeText, { 116.0f, 158.0f, 90.0f, 42.0f }, juce::Justification::centred, col::amber, 17.0f);
    vintage::drawGlowText (g, statusText, { 240.0f, 716.0f, 470.0f, 30.0f }, juce::Justification::centredLeft, col::amber, 12.5f);
    vintage::drawGlowText (g, nowText.isEmpty() ? juce::String ("--") : nowText, { 1198.0f, 238.0f, 140.0f, 58.0f },
                           juce::Justification::centred, flashFrames > 0 ? col::amberHot : col::amber.withAlpha (0.75f), 26.0f);
    vintage::drawLed (g, { 1350.0f, 267.0f }, 4.0f, flashFrames > 0, col::ledRed);
    if (proc.param ("playmode") == 0)
    {
        engrave (g, "EACH STAGE PLAYS ITS", { 30, 408, 180, 13 }, 8.0f, juce::Justification::centred, col::creamDim);
        engrave (g, "OWN PITCH ALONG THE PATH", { 30, 422, 180, 13 }, 8.0f, juce::Justification::centred, col::creamDim);
    }
    else if (proc.param ("playmode") == 1)
    {
        auto win = juce::Rectangle<float> (108.0f, 410.0f, 96.0f, 32.0f);
        vintage::drawGlassWindow (g, win, 3.0f);
        vintage::drawGlowText (g, noteName (proc.param ("bassnote")), win, juce::Justification::centred, col::amber, 15.0f);
    }
}
} // namespace beeui

// ===========================================================================
BeeEditor::BeeEditor (BeeProcessor& p) : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (beeui::Content::W / 2, beeui::Content::H / 2, beeui::Content::W * 2, beeui::Content::H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) beeui::Content::W / beeui::Content::H);
    setSize (beeui::Content::W * 9 / 10, beeui::Content::H * 9 / 10);
}

void BeeEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) beeui::Content::W));
    content.setBounds (0, 0, beeui::Content::W, beeui::Content::H);
}
