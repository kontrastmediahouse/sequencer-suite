#include "PluginEditor.h"

namespace col = vintage::col;

namespace
{
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
const juce::Colour snakeCol { 0xff8fd16a };   // Snake's module colour (green), TGB-420 uses its amber
}

// ===========================================================================
void MonitorView::tick()
{
    int sig = proc.snake.getCurrentCell (snake::Gates) * 31 + proc.snake.getCurrentCell (snake::Notes) * 977 + proc.tgb.uiStage() * 7919;
    sig += proc.tgb.isPlaying() ? 1 : 0;
    if (sig != lastSig) { lastSig = sig; repaint(); }
}

void MonitorView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    vintage::drawGlassWindow (g, r, 4.0f);
    // Snake: 4x4
    const auto grid = juce::Rectangle<float> (r.getX() + 12.0f, r.getY() + 22.0f, 64.0f, 64.0f);
    g.setFont (vintage::font (8.0f));
    g.setColour (snakeCol.withAlpha (0.8f));
    g.drawText ("SNAKE", juce::Rectangle<float> (grid.getX(), r.getY() + 5.0f, 64.0f, 12.0f), juce::Justification::centred);
    const int gc = proc.snake.getCurrentCell (snake::Gates), nc = proc.snake.getCurrentCell (snake::Notes);
    for (int c = 0; c < 16; ++c)
    {
        const auto pt = juce::Point<float> (grid.getX() + 8.0f + (float) (c % 4) * 16.0f, grid.getY() + 8.0f + (float) (c / 4) * 16.0f);
        const bool on = c == gc;
        g.setColour (on ? snakeCol : (c == nc ? snakeCol.withAlpha (0.45f) : snakeCol.withAlpha (0.12f)));
        g.fillEllipse (juce::Rectangle<float> (on ? 9.0f : 6.0f, on ? 9.0f : 6.0f).withCentre (pt));
    }
    // TGB-420: 16 stages in two rows
    const float x0 = grid.getRight() + 18.0f;
    g.setColour (col::amber.withAlpha (0.85f));
    g.drawText ("TGB-420", juce::Rectangle<float> (x0, r.getY() + 5.0f, 150.0f, 12.0f), juce::Justification::centredLeft);
    const int stage = proc.tgb.uiStage(), len = proc.tgb.param ("length");
    for (int s = 0; s < 16; ++s)
    {
        const auto c = juce::Rectangle<float> (x0 + (float) (s % 8) * 19.0f, r.getY() + 26.0f + (float) (s / 8) * 22.0f, 15.0f, 15.0f);
        const bool on = s == stage && proc.tgb.isPlaying();
        g.setColour (on ? col::amberHot : (s < len ? col::amber.withAlpha (0.28f) : col::amber.withAlpha (0.07f)));
        g.fillRoundedRectangle (c, 2.5f);
    }
}

// ===========================================================================
void LinkDiagram::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int link = proc.global.linkV();
    const auto a = juce::Rectangle<float> (r.getX() + 4.0f, r.getCentreY() - 15.0f, 92.0f, 30.0f);
    const auto b = juce::Rectangle<float> (r.getRight() - 96.0f, r.getCentreY() - 15.0f, 92.0f, 30.0f);
    auto box = [&] (juce::Rectangle<float> bx, const juce::String& t, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.14f)); g.fillRoundedRectangle (bx, 5.0f);
        g.setColour (c); g.drawRoundedRectangle (bx, 5.0f, 1.4f);
        g.setFont (vintage::font (10.5f)); g.drawText (t, bx, juce::Justification::centred);
    };
    box (a, "SNAKE", snakeCol);
    box (b, "TGB-420", col::amber);
    const float y = r.getCentreY();
    juce::String label;
    if (link == s420::LinkOff)
    {
        g.setColour (col::creamDim.withAlpha (0.5f));
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine ({ a.getRight() + 8.0f, y, b.getX() - 8.0f, y }, dashes, 2, 1.2f);
        label = "INDEPENDENT";
    }
    else
    {
        const bool toTgb = link == s420::LinkSnakePitchToTgb;
        const float x1 = toTgb ? a.getRight() + 6.0f : b.getX() - 6.0f, x2 = toTgb ? b.getX() - 6.0f : a.getRight() + 6.0f;
        juce::Path p; p.addArrow ({ x1, y, x2, y }, 2.2f, 10.0f, 9.0f);
        g.setColour (toTgb ? snakeCol : col::amber);
        g.fillPath (p);
        label = toTgb ? "PITCH" : "CLOCK";
    }
    g.setFont (vintage::font (9.0f));
    g.setColour (col::cream);
    g.drawText (label, juce::Rectangle<float> (a.getRight(), y - 22.0f, b.getX() - a.getRight(), 12.0f), juce::Justification::centred);
}

// ===========================================================================
MachineContent::MachineContent (MachineProcessor& p)
    : proc (p), diagram (p), monitor (p), snakePage (p.snake), tgbPage (p.tgb)
{
    setLookAndFeel (&lnf);
    auto& st = p.apvts;
    auto combo = [&] (juce::ComboBox& b, const juce::String& id, const juce::String& tip)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (id))) b.addItemList (c->choices, 1);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        cAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, id, b));
    };
    combo (rootBox, s420::ids::root, "Global root: used by every module set to Scale from Global");
    combo (scaleBox, s420::ids::scale, "Global scale: used by every module set to Scale from Global");
    combo (chanBox, s420::ids::channel, "The machine's MIDI output channel");
    combo (inBox, s420::ids::midiIn, "Where incoming MIDI goes: TGB-420's keys (Bass/Arp/Learn), Snake's remote (root, random, chord scale), or both");

    const char* linkNames[] = { "OFF", "SNAKE PITCH > TGB", "TGB CLOCK > SNAKE" };
    const char* linkTips[] = { "Both sequencers run on their own and merge into one output",
                               "TGB-420 plays its rhythm with Snake's current pitch; TGB's stage dials become offsets",
                               "Every TGB-420 note steps Snake once: Snake's 4x4 paths play in TGB's rhythm" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = linkBtns[(size_t) i];
        b.setButtonText (linkNames[i]); b.setClickingTogglesState (true); b.setRadioGroupId (3001);
        b.setTooltip (linkTips[i]);
        b.onClick = [this, i]
        {
            if (! linkBtns[(size_t) i].getToggleState()) return;
            auto* prm = proc.apvts.getParameter (s420::ids::link);
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture();
        };
        addAndMakeVisible (b);
    }
    for (auto* b : { &snakeOut, &tgbOut }) { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
    bAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, s420::ids::snakeOut, snakeOut));
    bAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, s420::ids::tgbOut, tgbOut));
    snakeOut.setTooltip ("Send Snake's notes (its CC lanes always play)");
    tgbOut.setTooltip ("Send TGB-420's notes (in TGB CLOCK > SNAKE they clock Snake instead)");

    const char* pages[] = { "SNAKE   4 x 4 PATH", "TGB-420   STAGES" };
    for (int i = 0; i < 2; ++i)
    {
        auto& b = pageBtns[(size_t) i];
        b.setButtonText (pages[i]); b.setClickingTogglesState (true); b.setRadioGroupId (3002);
        b.onClick = [this, i] { if (pageBtns[(size_t) i].getToggleState()) showModule (i); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (diagram);
    addAndMakeVisible (monitor);
    addChildComponent (snakePage);
    addChildComponent (tgbPage);

    renderBackground();
    setSize (W, H);
    showModule (proc.selectedModule);
    timerCallback();
    startTimerHz (30);
}

MachineContent::~MachineContent() { stopTimer(); setLookAndFeel (nullptr); }

void MachineContent::showModule (int m)
{
    proc.selectedModule = m;
    pageBtns[(size_t) m].setToggleState (true, juce::dontSendNotification);
    snakePage.setVisible (m == 0);
    tgbPage.setVisible (m == 1);
}

void MachineContent::resized()
{
    // master strip
    rootBox.setBounds (40, 98, 64, 28);
    scaleBox.setBounds (112, 98, 168, 28);
    for (int i = 0; i < 3; ++i) linkBtns[(size_t) i].setBounds (304 + i * 146, 82, 140, 40);
    diagram.setBounds (318, 126, 410, 46);
    snakeOut.setBounds (760, 82, 104, 40);
    tgbOut.setBounds (868, 82, 104, 40);
    chanBox.setBounds (760, 144, 100, 28);
    inBox.setBounds (868, 144, 104, 28);
    monitor.setBounds (996, 74, 296, 98);
    for (int i = 0; i < 2; ++i) pageBtns[(size_t) i].setBounds (36 + i * 236, 178, 228, 34);

    // module area: each page keeps its own design size, scaled to fit
    const auto area = juce::Rectangle<int> (0, StripH, W, H - StripH);
    auto place = [&] (juce::Component& c, int w, int h)
    {
        const float s = juce::jmin ((float) area.getWidth() / (float) w, (float) area.getHeight() / (float) h);
        c.setBounds (0, 0, w, h);
        c.setTransform (juce::AffineTransform::scale (s).translated ((float) area.getX() + ((float) area.getWidth() - (float) w * s) * 0.5f,
                                                                    (float) area.getY()));
    };
    place (snakePage, SnakeContent::W, SnakeContent::H);
    place (tgbPage, tgbui::Content::W, tgbui::Content::H);
}

void MachineContent::timerCallback()
{
    const int link = proc.global.linkV();
    if (link != lastLink)
    {
        lastLink = link;
        for (int i = 0; i < 3; ++i) linkBtns[(size_t) i].setToggleState (i == link, juce::dontSendNotification);
        diagram.repaint();
        repaint (0, 0, W, StripH);
    }
    monitor.tick();
    const int n = proc.notesOut();
    if (n != lastNotes)
    {
        lastNotes = n; flash = 3;
        const int pitch = proc.lastPitchOut();
        nowText = pitch >= 0 ? juce::String (snake::ids::midiNoteName (pitch)) : juce::String ("--");
        repaint (1180, 18, 112, 30);
    }
    else if (flash > 0 && --flash == 0) repaint (1180, 18, 112, 30);
}

void MachineContent::renderBackground()
{
    const float k = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * k), (int) (StripH * k), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (k));
    juce::Random rnd (2026);

    juce::ColourGradient base (col::panelHi.brighter (0.05f), 0, 0, col::panel, 0, (float) StripH, false);
    g.setGradientFill (base);
    g.fillRect (0, 0, W, StripH);
    for (int i = 0; i < 500; ++i)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + rnd.nextFloat() * 0.02f));
        g.drawHorizontalLine ((int) (rnd.nextFloat() * StripH), 0.0f, (float) W);
    }
    // header plate
    juce::ColourGradient hg (juce::Colour (0xff121110), 0, 0, juce::Colour (0xff201e19), 0, 62.0f, false);
    g.setGradientFill (hg);
    g.fillRect (24.0f, 0.0f, (float) W - 48.0f, 62.0f);
    g.setColour (col::metal.withAlpha (0.6f)); g.drawHorizontalLine (61, 24.0f, (float) W - 24.0f);
    g.setFont (vintage::font (34.0f, true, 0.32f));
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawText ("SNAKE-420", 46, 12, 330, 40, juce::Justification::centredLeft);
    g.setColour (col::cream);                            g.drawText ("SNAKE-420", 44, 10, 330, 40, juce::Justification::centredLeft);
    g.setColour (snakeCol);  g.fillRect (356.0f, 20.0f, 40.0f, 5.0f);
    g.setColour (col::amber); g.fillRect (356.0f, 29.0f, 40.0f, 5.0f);
    g.setColour (col::orangeCap); g.fillRect (356.0f, 38.0f, 40.0f, 5.0f);
    engrave (g, "LINKED SEQUENCER MACHINE", { 410, 16, 400, 16 }, 12.5f, juce::Justification::centredLeft);
    engrave (g, "4 x 4 PATHS" + dot() + "16 STAGES" + dot() + "ONE CLOCK, ONE SCALE, ONE OUTPUT", { 410, 34, 500, 14 }, 8.5f,
             juce::Justification::centredLeft, col::creamDim);
    engrave (g, "NOW", { 1120, 26, 56, 14 }, 9.0f, juce::Justification::centredRight, col::creamDim);
    vintage::drawGlassWindow (g, { 1182.0f, 18.0f, 108.0f, 30.0f }, 3.0f);

    // strip sections
    engrave (g, "GLOBAL SCALE", { 36, 70, 248, 14 }, 10.0f);
    engrave (g, "ROOT", { 40, 84, 64, 12 }, 8.0f, juce::Justification::centred, col::creamDim);
    engrave (g, "SCALE", { 112, 84, 168, 12 }, 8.0f, juce::Justification::centred, col::creamDim);
    engrave (g, "FOLLOWED BY MODULES SET TO", { 36, 132, 248, 12 }, 7.5f, juce::Justification::centred, col::creamDim);
    engrave (g, "SCALE FROM: GLOBAL", { 36, 144, 248, 12 }, 7.5f, juce::Justification::centred, col::creamDim);
    engrave (g, "LINK", { 300, 66, 440, 14 }, 10.0f);
    engrave (g, "OUTPUT", { 756, 66, 220, 14 }, 10.0f);
    engrave (g, "MIDI CH", { 760, 130, 100, 12 }, 8.0f, juce::Justification::centred, col::creamDim);
    engrave (g, "MIDI IN", { 868, 130, 104, 12 }, 8.0f, juce::Justification::centred, col::creamDim);
    for (int x : { 292, 746, 984 })
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawVerticalLine (x, 68.0f, 172.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawVerticalLine (x + 1, 68.0f, 172.0f);
    }
    for (float x : { 34.0f, (float) W - 34.0f }) vintage::drawScrew (g, { x, 74.0f }, 4.0f, rnd.nextFloat() * 3.0f);
    // seam between strip and module pages
    g.setColour (juce::Colours::black); g.fillRect (24, StripH - 2, W - 48, 2);
    // wood cheeks
    for (auto x : { 0, W - 24 })
    {
        juce::Rectangle<float> w ((float) x, 0.0f, 24.0f, (float) StripH);
        juce::ColourGradient wg (col::woodLight, w.getX(), 0, col::wood.darker (0.3f), w.getRight(), 0, false);
        wg.addColour (0.5, col::wood);
        g.setGradientFill (wg); g.fillRect (w);
        for (int i = 0; i < 18; ++i)
        {
            const float gx = w.getX() + 2.0f + rnd.nextFloat() * 20.0f;
            g.setColour (juce::Colour (0xff2a1408).withAlpha (0.1f + rnd.nextFloat() * 0.18f));
            g.drawLine (gx, 0.0f, gx + rnd.nextFloat() * 2.0f - 1.0f, (float) StripH, 0.6f + rnd.nextFloat());
        }
    }
}

void MachineContent::paint (juce::Graphics& g)
{
    g.fillAll (col::panelLo);
    g.drawImage (background, juce::Rectangle<float> (0.0f, 0.0f, (float) W, (float) StripH));
    vintage::drawGlowText (g, nowText.isEmpty() ? juce::String ("--") : nowText, { 1182.0f, 18.0f, 84.0f, 30.0f },
                           juce::Justification::centred, flash > 0 ? col::amberHot : col::amber, 17.0f);
    vintage::drawLed (g, { 1278.0f, 33.0f }, 4.0f, flash > 0, col::ledRed);
}

// ===========================================================================
MachineEditor::MachineEditor (MachineProcessor& p) : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (MachineContent::W / 2, MachineContent::H / 2, MachineContent::W * 2, MachineContent::H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) MachineContent::W / MachineContent::H);
    setSize (MachineContent::W * 4 / 5, MachineContent::H * 4 / 5);
}

void MachineEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) MachineContent::W));
    content.setBounds (0, 0, MachineContent::W, MachineContent::H);
}
