#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SeleneLookAndFeel.h"

// Animated background: flat indigo night, a few faint stars and a big matte moon
// that reacts to the reverb parameters and the live wet signal.
//
//   Size    -> moon radius
//   Decay   -> soft halo rings breathing outward (longer decay = more, slower rings)
//   Decay<0 -> eclipse: a shadow sweeps across the moon, rim turns warm
//   Tape    -> warm amber tint
//   Drift   -> the moon floats gently and its surface slowly turns
//   Mix / wet level -> glow intensity
class SkyView : public juce::Component
{
public:
    struct State
    {
        float size = 0.5f, decay = 0.45f, tape = 0.2f, drift = 0.3f, mix = 0.35f, level = 0.0f;
        bool bypassed = false;
    };

    SkyView()
    {
        setOpaque (true);
        setInterceptsMouseClicks (false, false);

        juce::Random rng (20260829);
        for (int i = 0; i < 70; ++i)
            stars.push_back ({ rng.nextFloat(), rng.nextFloat(), 0.6f + rng.nextFloat() * 1.0f,
                               rng.nextFloat() * 6.28f, 0.3f + rng.nextFloat() * 1.2f });

        for (int i = 0; i < 11; ++i)
        {
            const float a = rng.nextFloat() * 6.28f, d = std::sqrt (rng.nextFloat()) * 0.78f;
            craters.push_back ({ d * std::cos (a), d * std::sin (a), 0.06f + rng.nextFloat() * rng.nextFloat() * 0.18f });
        }
    }

    void setMoonArea (juce::Rectangle<float> area) { moonArea = area; }
    void setTopBarHeight (float h) { topBarHeight = h; }

    void update (const State& s, float dt)
    {
        state = s;
        time += dt;
        smoothedLevel += (juce::jlimit (0.0f, 1.0f, s.level * 4.0f) - smoothedLevel) * 0.12f;
        rippleClock += dt / juce::jmap (juce::jmax (0.0f, s.decay), 2.5f, 7.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        juce::ColourGradient sky (Palette::skyTop, 0, 0, Palette::skyBottom, 0, b.getHeight(), false);
        g.setGradientFill (sky);
        g.fillAll();
        g.setColour (Palette::bar);
        g.fillRect (b.withHeight (topBarHeight));

        for (const auto& s : stars)
        {
            const float tw = 0.5f + 0.5f * std::sin (time * s.speed + s.phase);
            g.setColour (juce::Colours::white.withAlpha (0.06f + 0.22f * tw));
            g.fillEllipse (s.x * b.getWidth(), s.y * b.getHeight(), s.size, s.size);
        }

        drawMoon (g);
    }

private:
    struct Star { float x, y, size, phase, speed; };
    struct Crater { float u, v, r; };

    void drawMoon (juce::Graphics& g)
    {
        const float active = state.bypassed ? 0.35f : 1.0f;
        const float maxR = juce::jmin (moonArea.getWidth(), moonArea.getHeight()) * 0.5f;
        const float r = juce::jmap (state.size, maxR * 0.62f, maxR * 0.9f);
        const float wob = state.drift * state.drift;
        const juce::Point<float> c (moonArea.getCentreX() + std::sin (time * 0.5f) * wob * 5.0f,
                                    moonArea.getCentreY() + std::cos (time * 0.37f) * wob * 4.0f);
        const bool inverted = state.decay < 0.0f;
        const float invAmt = inverted ? -state.decay : 0.0f;
        const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);

        const auto light = Palette::moonLight.interpolatedWith (juce::Colour (0xffe2c4a6), state.tape * 0.55f);
        const auto dark  = Palette::moonDark.interpolatedWith (juce::Colour (0xff6e5a50), state.tape * 0.5f);
        const auto glowCol = (inverted ? juce::Colour (0xffff9a70) : juce::Colour (0xff8f86e0))
                                 .interpolatedWith (Palette::accentWarm, state.tape * 0.4f);

        // Soft atmospheric glow
        const float glowAmt = (0.10f + 0.25f * state.mix + 0.35f * smoothedLevel) * active;
        const float glowR = r * 1.75f;
        juce::ColourGradient glow (glowCol.withAlpha (juce::jmin (0.6f, glowAmt)), c.x, c.y,
                                   glowCol.withAlpha (0.0f), c.x + glowR, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (c));

        // Breathing halo rings
        if (! inverted)
        {
            const int rings = 1 + (int) (state.decay * 3.0f);
            for (int k = 0; k < rings; ++k)
            {
                const float ph = std::fmod (rippleClock + (float) k / (float) rings, 1.0f);
                const float rr = r * (1.04f + ph * 0.42f);
                g.setColour (Palette::text.withAlpha ((1.0f - ph) * (1.0f - ph) * 0.16f * active));
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.0f);
            }
        }

        // Matte sphere: broad soft light from upper left.
        juce::ColourGradient body (light, c.x - r * 0.45f, c.y - r * 0.55f, dark, c.x + r * 0.85f, c.y + r * 0.95f, true);
        g.setGradientFill (body);
        g.fillEllipse (disc);

        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addEllipse (disc);
            g.reduceClipRegion (clip);

            // Very subtle maria + craters for texture, slowly turning with Drift.
            g.setColour (dark.withAlpha (0.1f));
            g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.4f, r * 0.75f, r * 0.55f);
            g.fillEllipse (c.x + r * 0.02f, c.y - r * 0.08f, r * 0.6f, r * 0.66f);

            const float spin = time * (0.01f + 0.06f * state.drift);
            for (const auto& cr : craters)
            {
                const float u = std::fmod (cr.u + spin + 1.0f, 2.0f) - 1.0f;
                const float fore = std::sqrt (juce::jmax (0.0f, 1.0f - u * u));
                const auto e = juce::Rectangle<float> (cr.r * r * 2.0f * fore, cr.r * r * 2.0f)
                                   .withCentre ({ c.x + u * r, c.y + cr.v * r });
                g.setColour (dark.withAlpha (0.18f * fore));
                g.fillEllipse (e);
            }

            if (inverted)
            {
                const float off = r * (2.1f - 1.7f * invAmt);
                const auto shadow = disc.translated (off, -off * 0.12f).expanded (r * 0.04f);
                juce::ColourGradient umbra (Palette::skyTop.withAlpha (0.94f), shadow.getCentreX(), shadow.getCentreY(),
                                            Palette::skyTop.withAlpha (0.7f), shadow.getRight(), shadow.getCentreY(), true);
                g.setGradientFill (umbra);
                g.fillEllipse (shadow);
            }

            if (state.bypassed)
            {
                g.setColour (Palette::skyTop.withAlpha (0.5f));
                g.fillEllipse (disc);
            }
        }

        if (inverted)
        {
            g.setColour (juce::Colour (0xffff9a70).withAlpha (0.15f + 0.45f * invAmt));
            g.drawEllipse (disc, 1.4f);
        }
    }

    juce::Rectangle<float> moonArea;
    std::vector<Star> stars;
    std::vector<Crater> craters;
    State state;
    float topBarHeight = 0.0f;
    float time = 0.0f, smoothedLevel = 0.0f, rippleClock = 0.0f;
};
