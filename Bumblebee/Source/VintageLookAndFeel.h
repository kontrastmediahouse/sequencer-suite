// VintageLookAndFeel.h - 70s/80s hardware styling, drawn entirely in code (no image assets)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vintage
{
namespace col
{
    inline const juce::Colour panel      { 0xff1d1b17 };  // Bumblebee: deep black  // dark anodised
    inline const juce::Colour panelHi    { 0xff2b2822 };
    inline const juce::Colour panelLo    { 0xff100f0c };
    inline const juce::Colour cream      { 0xfff1e6c8 };  // engraved legend
    inline const juce::Colour creamDim   { 0xff9c9380 };
    inline const juce::Colour amber      { 0xffffc21f };  // honey
    inline const juce::Colour amberHot   { 0xffffe58a };
    inline const juce::Colour amberDim   { 0xff4a3a08 };
    inline const juce::Colour ledRed     { 0xffff4b2b };
    inline const juce::Colour glass      { 0xff0d0b08 };
    inline const juce::Colour wood       { 0xff5a3218 };
    inline const juce::Colour woodLight  { 0xff8a5428 };
    inline const juce::Colour metal      { 0xffb9b6ae };
    inline const juce::Colour metalDark  { 0xff55534d };
    inline const juce::Colour keycap     { 0xffd9cfb4 };
    inline const juce::Colour keycapDark { 0xff8e8670 };
    inline const juce::Colour orangeCap  { 0xfff2b21c };  // honey keycap
}

inline juce::Font font (float h, bool bold = true, float kerning = 0.08f)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}
inline juce::Font displayFont (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
}

// Glowing text, for the amber "VFD" displays
inline void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                          juce::Justification just, juce::Colour c, float fontH)
{
    g.setFont (displayFont (fontH));
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { -1.2f, 1.2f })
    {
        g.drawText (text, r.translated (d, 0), just, false);
        g.drawText (text, r.translated (0, d), just, false);
    }
    g.setColour (c);
    g.drawText (text, r, just, false);
}

inline void drawGlassWindow (juce::Graphics& g, juce::Rectangle<float> r, float corner = 3.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.expanded (1.5f), corner + 1.5f);
    g.setColour (col::glass);
    g.fillRoundedRectangle (r, corner);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.07f), r.getX(), r.getY(),
                                juce::Colours::transparentWhite, r.getX(), r.getCentreY(), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r.reduced (1.0f), corner);
    g.setColour (col::panelHi.withAlpha (0.8f));
    g.drawRoundedRectangle (r.expanded (1.5f), corner + 1.5f, 1.0f);
}

inline void drawLed (juce::Graphics& g, juce::Point<float> c, float radius, bool on, juce::Colour colour = col::amber)
{
    if (on)
    {
        juce::ColourGradient halo (colour.withAlpha (0.45f), c.x, c.y, colour.withAlpha (0.0f), c.x + radius * 3.2f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (radius * 6.4f, radius * 6.4f).withCentre (c));
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.6f, radius * 2.6f).withCentre (c));
    juce::ColourGradient body (on ? colour.brighter (0.6f) : colour.withMultipliedBrightness (0.22f), c.x - radius * 0.3f, c.y - radius * 0.4f,
                               on ? colour : colour.withMultipliedBrightness (0.12f), c.x, c.y + radius, true);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (on ? 0.7f : 0.15f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.5f).withCentre (c.translated (-radius * 0.3f, -radius * 0.45f)));
}

inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.4f, r * 2.4f).withCentre (c.translated (0.0f, 0.8f)));
    juce::ColourGradient grad (col::metal.brighter (0.3f), c.x - r, c.y - r, col::metalDark, c.x + r, c.y + r, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (col::panelLo);
    const auto d = juce::Point<float> (std::cos (angle), std::sin (angle)) * (r * 0.75f);
    g.drawLine ({ c - d, c + d }, r * 0.35f);
}

// ---------------------------------------------------------------------------
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, col::amber);
        setColour (juce::PopupMenu::backgroundColourId, col::glass);
        setColour (juce::PopupMenu::textColourId, col::amber);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amberDim);
        setColour (juce::PopupMenu::highlightedTextColourId, col::amberHot);
        setColour (juce::BubbleComponent::backgroundColourId, col::glass);
        setColour (juce::BubbleComponent::outlineColourId, col::amberDim);
        setColour (juce::TooltipWindow::backgroundColourId, col::glass);
        setColour (juce::TooltipWindow::textColourId, col::cream);
        setColour (juce::Label::textColourId, col::cream);
        setColour (juce::TextButton::textColourOffId, col::panelLo);
        setColour (juce::TextButton::textColourOnId, col::panelLo);
    }

    // ---- knobs ------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto style   = s.getProperties()["knob"].toString();      // "big", "step", "small"
        const bool big     = style == "big";
        const bool step    = style == "step";
        const bool enabled = s.isEnabled();

        auto area   = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = jmin (area.getWidth(), area.getHeight());
        const auto  c    = area.getCentre();
        const float rTicks = size * 0.5f;
        auto polar = [c] (float r, float a) { return c + Point<float> (r * std::sin (a), -r * std::cos (a)); };

        // printed scale
        const int ticks = big ? 11 : (step ? 9 : 11);
        for (int i = 0; i < ticks; ++i)
        {
            const float a = startAngle + (float) i / (float) (ticks - 1) * (endAngle - startAngle);
            const bool major = (i == 0 || i == ticks - 1 || i == ticks / 2);
            g.setColour ((major ? col::cream : col::creamDim).withAlpha (enabled ? 0.9f : 0.35f));
            g.drawLine ({ polar (rTicks * (major ? 0.80f : 0.86f), a), polar (rTicks, a) }, major ? 1.4f : 1.0f);
        }

        // lit value arc for step knobs; colour per layer, bipolar knobs light from the centre
        if (step)
        {
            const auto arcCol = s.getProperties().contains ("arc") ? Colour ((uint32) (int64) s.getProperties()["arc"]) : col::amber;
            const bool bipolar = (bool) s.getProperties()["bipolar"];
            const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            const float to   = startAngle + pos * (endAngle - startAngle);
            Path arc;
            arc.addCentredArc (c.x, c.y, rTicks * 0.74f, rTicks * 0.74f, 0.0f, jmin (from, to), jmax (from, to), true);
            g.setColour (arcCol.withAlpha (enabled ? 0.9f : 0.25f));
            g.strokePath (arc, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // shadow
        const float skirtR = size * (big ? 0.40f : 0.36f);
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillEllipse (Rectangle<float> (skirtR * 2.25f, skirtR * 2.25f).withCentre (c.translated (0.0f, skirtR * 0.12f)));

        // knurled metal skirt
        {
            ColourGradient metal (col::metal.brighter (0.25f), c.x - skirtR, c.y - skirtR, col::metalDark, c.x + skirtR, c.y + skirtR, false);
            g.setGradientFill (metal);
            g.fillEllipse (Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c));
            g.setColour (Colours::black.withAlpha (0.35f));
            const int teeth = big ? 48 : 32;
            for (int i = 0; i < teeth; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / (float) teeth;
                g.drawLine ({ polar (skirtR * 0.86f, a), polar (skirtR * 0.99f, a) }, 0.8f);
            }
        }

        // bakelite cap
        const float capR = skirtR * 0.80f;
        {
            ColourGradient cap (Colour (0xff3a3833), c.x - capR * 0.5f, c.y - capR * 0.7f, Colour (0xff060605), c.x + capR * 0.4f, c.y + capR, true);
            g.setGradientFill (cap);
            g.fillEllipse (Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
            g.setColour (Colours::white.withAlpha (0.10f));
            g.drawEllipse (Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c).reduced (0.8f), 1.0f);
            ColourGradient spec (Colours::white.withAlpha (0.16f), c.x, c.y - capR, Colours::transparentWhite, c.x, c.y, false);
            g.setGradientFill (spec);
            g.fillEllipse (Rectangle<float> (capR * 1.4f, capR * 0.9f).withCentre (c.translated (0.0f, -capR * 0.45f)));
        }

        const float a = startAngle + pos * (endAngle - startAngle);
        if (big)
        {
            // chicken-head pointer
            Path p;
            p.startNewSubPath (polar (capR * 1.18f, a));
            p.lineTo (polar (capR * 0.42f, a - 0.55f));
            p.lineTo (polar (capR * 0.55f, a + MathConstants<float>::pi));
            p.lineTo (polar (capR * 0.42f, a + 0.55f));
            p.closeSubPath();
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillPath (p, AffineTransform::translation (0.0f, 1.5f));
            ColourGradient ph (Colour (0xff2c2a26), c.x, c.y - capR, Colour (0xff0b0a09), c.x, c.y + capR, false);
            g.setGradientFill (ph);
            g.fillPath (p);
            g.setColour (Colours::white.withAlpha (0.12f));
            g.strokePath (p, PathStrokeType (1.0f));
        }
        g.setColour (enabled ? col::cream : col::creamDim);
        g.drawLine ({ polar (capR * 0.25f, a), polar (capR * (big ? 1.12f : 0.95f), a) }, big ? 2.4f : 1.8f);
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        juce::Slider::SliderLayout l;
        l.sliderBounds = s.getLocalBounds();
        return l;
    }

    // ---- push buttons (keycaps with LED) -----------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat().reduced (2.0f);
        const bool orange = b.getProperties()["cap"].toString() == "orange";
        const bool arrow  = b.getProperties()["cap"].toString() == "arrow";
        const bool on = b.getToggleState();
        const float press = down ? 1.5f : 0.0f;

        // bezel
        g.setColour (Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (r.expanded (1.5f), 4.0f);

        auto cap = r.reduced (1.5f).translated (0.0f, press);
        const auto base = arrow ? col::metalDark.brighter (0.4f) : (orange ? col::orangeCap : col::keycap);
        ColourGradient grad (base.brighter (over ? 0.25f : 0.12f), cap.getX(), cap.getY(),
                             base.darker (0.35f), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawLine (cap.getX() + 3.0f, cap.getY() + 1.0f, cap.getRight() - 3.0f, cap.getY() + 1.0f, 1.0f);

        // LED window on toggle-type buttons
        if (b.getClickingTogglesState())
        {
            auto led = Rectangle<float> (cap.getWidth() * 0.5f, 4.0f).withCentre ({ cap.getCentreX(), cap.getY() + 6.0f });
            g.setColour (Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (led.expanded (1.0f), 2.0f);
            g.setColour (on ? col::amber : col::amberDim.darker (0.4f));
            g.fillRoundedRectangle (led, 1.5f);
            if (on)
            {
                g.setColour (col::amber.withAlpha (0.25f));
                g.fillRoundedRectangle (led.expanded (3.0f, 2.0f), 3.0f);
            }
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (4.0f).translated (0.0f, down ? 1.5f : 0.0f);
        if (b.getClickingTogglesState()) r.removeFromTop (6.0f);
        const bool orange = b.getProperties()["cap"].toString() == "orange";
        const bool arrow  = b.getProperties()["cap"].toString() == "arrow";
        g.setColour (arrow ? col::cream : (orange ? juce::Colour (0xff2a1206) : juce::Colour (0xff2b2820)));
        g.setFont (font (arrow ? 13.0f : 10.5f));
        g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 2);
    }

    // ---- bat-handle toggle switch (gate steps & switches) -------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        using namespace juce;
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const auto c = r.getCentre();
        const float s = jmin (r.getWidth(), r.getHeight());

        // ON / OFF legends
        g.setFont (font (8.5f));
        g.setColour (on ? col::cream : col::creamDim.withAlpha (0.6f));
        g.drawText ("ON", Rectangle<float> (s, 10.0f).withCentre ({ c.x, r.getY() + 5.0f }), Justification::centred);
        g.setColour (! on ? col::cream : col::creamDim.withAlpha (0.6f));
        g.drawText ("OFF", Rectangle<float> (s, 10.0f).withCentre ({ c.x, r.getBottom() - 5.0f }), Justification::centred);

        // hex nut + washer
        const float nutR = s * 0.20f;
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillEllipse (Rectangle<float> (nutR * 2.6f, nutR * 2.6f).withCentre (c.translated (0.0f, 1.0f)));
        Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const float a = MathConstants<float>::pi / 3.0f * (float) i;
            const auto p = c + Point<float> (std::cos (a), std::sin (a)) * (nutR * 1.15f);
            if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        ColourGradient ng (col::metal.brighter (0.2f), c.x - nutR, c.y - nutR, col::metalDark, c.x + nutR, c.y + nutR, false);
        g.setGradientFill (ng);
        g.fillPath (hex);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.fillEllipse (Rectangle<float> (nutR * 1.1f, nutR * 1.1f).withCentre (c));

        // lever
        const float len = s * 0.34f;
        const float dir = on ? -1.0f : 1.0f;
        const auto tip = c.translated (0.0f, dir * len);
        Path lever;
        lever.startNewSubPath (c.translated (-nutR * 0.32f, 0.0f));
        lever.lineTo (tip.translated (-nutR * 0.55f, 0.0f));
        lever.lineTo (tip.translated (nutR * 0.55f, 0.0f));
        lever.lineTo (c.translated (nutR * 0.32f, 0.0f));
        lever.closeSubPath();
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillPath (lever, AffineTransform::translation (1.5f, 2.0f));
        ColourGradient lg (Colours::white, tip.x - nutR, tip.y, col::metalDark, tip.x + nutR, tip.y, false);
        lg.addColour (0.35, col::metal.brighter (0.4f));
        g.setGradientFill (lg);
        g.fillPath (lever);
        g.fillEllipse (Rectangle<float> (nutR * 1.25f, nutR * 1.25f).withCentre (tip));
        g.setColour (Colours::white.withAlpha (over ? 0.9f : 0.6f));
        g.fillEllipse (Rectangle<float> (nutR * 0.45f, nutR * 0.35f).withCentre (tip.translated (-nutR * 0.2f, -nutR * 0.2f)));
    }

    // ---- combo boxes as amber glass displays --------------------------------
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f);
        drawGlassWindow (g, r);
        juce::Path tri;
        const float tx = r.getRight() - 10.0f, ty = r.getCentreY();
        tri.addTriangle (tx - 4.0f, ty - 2.0f, tx + 4.0f, ty - 2.0f, tx, ty + 3.0f);
        g.setColour (col::amber.withAlpha (box.isEnabled() ? 0.8f : 0.3f));
        g.fillPath (tri);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return displayFont (13.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return displayFont (14.0f); }

    juce::Font getSliderPopupFont (juce::Slider&) override { return displayFont (13.0f); }
    int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }

    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        drawGlassWindow (g, body.reduced (1.0f));
    }
};
} // namespace vintage
