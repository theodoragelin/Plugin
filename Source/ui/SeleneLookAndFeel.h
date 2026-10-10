#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Minimal, flat "night sky" style: deep indigo, thin outlined controls, small spaced caps.
namespace Palette
{
    inline const juce::Colour skyTop      { 0xff131433 };
    inline const juce::Colour skyBottom   { 0xff2a2459 };
    inline const juce::Colour bar         { 0xff0b0c20 };
    inline const juce::Colour barLine     { 0xff23244a };
    inline const juce::Colour field       { 0xff17183c };
    inline const juce::Colour outline     { 0xff34356a };
    inline const juce::Colour accent      { 0xffc9c2ff };
    inline const juce::Colour accentWarm  { 0xffffc79a };
    inline const juce::Colour text        { 0xffe6e4f2 };
    inline const juce::Colour textDim     { 0xff8b89ad };
    inline const juce::Colour moonLight   { 0xffc4c2cc };
    inline const juce::Colour moonDark    { 0xff5f5d6c };
}

inline juce::Font seleneFont (float height, bool bold = false, float kerning = 0.0f)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain))
        .withExtraKerningFactor (kerning);
}

class SeleneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SeleneLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::ComboBox::arrowColourId, Palette::textDim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf50e0f26));
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf50e0f26));
        setColour (juce::TooltipWindow::textColourId, Palette::text);
        setColour (juce::TooltipWindow::outlineColourId, Palette::outline);
    }

    // Thin outlined knob: faint ring, accent value arc, short pointer.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const bool big = radius > 28.0f;
        const bool hot = slider.isMouseOverOrDragging();
        const bool bipolar = slider.getProperties()["bipolar"];
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float ringR = radius - 2.0f;

        // Faint body
        g.setColour (juce::Colours::white.withAlpha (hot ? 0.06f : 0.035f));
        g.fillEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (c));

        // Ring
        g.setColour (Palette::text.withAlpha (hot ? 0.45f : 0.28f));
        g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (c), 1.1f);

        // Value arc on the ring
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            const auto col = (bipolar && angle < from) ? Palette::accentWarm : Palette::accent;
            g.setColour (col);
            g.strokePath (value, { big ? 2.2f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // Pointer
        const auto a = c.getPointOnCircumference (ringR * 0.45f, angle);
        const auto b = c.getPointOnCircumference (ringR * 0.82f, angle);
        g.setColour (Palette::text);
        g.drawLine ({ a, b }, big ? 1.8f : 1.5f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::Path arrow;
        const float ax = (float) width - 8.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 3.5f, ay - 1.8f, ax + 3.5f, ay - 1.8f, ax, ay + 2.2f);
        g.setColour (box.isMouseOver() ? Palette::text : Palette::textDim);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return seleneFont (10.5f, true, 0.16f); }
    juce::Font getPopupMenuFont() override { return seleneFont (13.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (0, 0, box.getWidth() - 14, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setColour (juce::Label::textColourId, box.isMouseOver() ? juce::Colours::white : Palette::text);
    }

    // Top-bar buttons are text-only; "preset" style buttons get a subtle field.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) override
    {
        if (b.getProperties()["field"])
        {
            const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (Palette::field);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Palette::outline.brighter (over ? 0.4f : 0.0f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int) override
    {
        return b.getProperties()["field"] ? seleneFont (11.5f, true, 0.06f) : seleneFont (10.5f, true, 0.16f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        auto col = b.getToggleState() ? Palette::accentWarm : (over ? juce::Colours::white : Palette::text);
        if (! b.isEnabled()) col = col.withAlpha (0.4f);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (col);

        auto area = b.getLocalBounds();
        if (b.getProperties()["power"])
        {
            // Small power glyph in front of BYPASS.
            auto icon = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            juce::Path p;
            p.addCentredArc (icon.getCentreX(), icon.getCentreY(), 4.2f, 4.2f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            p.startNewSubPath (icon.getCentreX(), icon.getY() - 0.5f);
            p.lineTo (icon.getCentreX(), icon.getCentreY());
            g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            area.removeFromLeft (4);
            g.drawText (b.getButtonText(), area, juce::Justification::centredLeft);
            return;
        }
        g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1);
    }
};
