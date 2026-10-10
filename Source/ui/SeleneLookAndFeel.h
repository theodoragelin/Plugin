#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour skyTop      { 0xff070914 };
    inline const juce::Colour skyMid      { 0xff11132e };
    inline const juce::Colour skyBottom   { 0xff221a45 };
    inline const juce::Colour panel       { 0xcc0b0d1f };
    inline const juce::Colour outline     { 0xff2c2a52 };
    inline const juce::Colour track       { 0xff2a2850 };
    inline const juce::Colour accent      { 0xffbfb2ff };
    inline const juce::Colour accentWarm  { 0xffffc79a };
    inline const juce::Colour text        { 0xffe9e6f5 };
    inline const juce::Colour textDim     { 0xff8c88b0 };
    inline const juce::Colour knobBody    { 0xff161733 };
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
        setColour (juce::ComboBox::backgroundColourId, Palette::panel);
        setColour (juce::ComboBox::outlineColourId, Palette::outline);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::ComboBox::arrowColourId, Palette::textDim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf0101228));
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxTextColourId, Palette::text);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0101228));
        setColour (juce::TooltipWindow::textColourId, Palette::text);
        setColour (juce::TooltipWindow::outlineColourId, Palette::outline);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const bool big = radius > 34.0f;
        const float trackW = big ? 3.0f : 2.4f;
        const float arcR = radius - trackW;
        const bool hot = slider.isMouseOverOrDragging();
        const bool bipolar = slider.getProperties()["bipolar"];
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, { trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Value arc
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        const auto arcColour = (bipolar && angle < from) ? Palette::accentWarm : Palette::accent;
        g.setColour (arcColour.withAlpha (0.25f));
        g.strokePath (value, { trackW * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour (arcColour);
        g.strokePath (value, { trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Body
        const float bodyR = arcR - (big ? 9.0f : 6.0f);
        juce::ColourGradient body (Palette::knobBody.brighter (0.35f), c.x - bodyR * 0.5f, c.y - bodyR * 0.7f,
                                   Palette::knobBody.darker (0.6f), c.x + bodyR * 0.6f, c.y + bodyR, true);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
        g.setColour (Palette::outline.brighter (hot ? 0.6f : 0.2f));
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), 1.0f);

        // Pointer
        const auto tip = c.getPointOnCircumference (bodyR - (big ? 7.0f : 5.0f), angle);
        const auto base = c.getPointOnCircumference (bodyR * 0.35f, angle);
        g.setColour (Palette::text);
        g.drawLine ({ base, tip }, big ? 2.5f : 2.0f);
        g.setColour (arcColour.withAlpha (hot ? 0.9f : 0.6f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (arcR, angle)));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).brighter (box.isMouseOver() ? 0.5f : 0.0f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (Palette::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return seleneFont (12.5f, true, 0.08f); }
    juce::Font getPopupMenuFont() override { return seleneFont (14.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 0, box.getWidth() - 26, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? Palette::accentWarm.withAlpha (0.22f) : Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour ((on ? Palette::accentWarm : Palette::outline).brighter (over || down ? 0.4f : 0.0f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return seleneFont (12.0f, true, 0.12f); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? Palette::accentWarm : Palette::text);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }
};
