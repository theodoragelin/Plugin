#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SeleneLookAndFeel.h"

// Animated background: night sky, twinkling stars and a moon that reacts to
// the reverb parameters and the live wet signal.
//
//   Size   -> moon radius
//   Decay  -> expanding halo ripples (longer decay = more, slower rings)
//   Decay<0-> eclipse: a shadow sweeps across the moon, rim turns warm
//   Tape   -> warm amber tint
//   Drift  -> the moon wobbles and its craters drift across the surface
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
        for (int i = 0; i < 170; ++i)
            stars.push_back ({ rng.nextFloat(), rng.nextFloat() * 0.92f, 0.4f + rng.nextFloat() * 1.3f,
                               rng.nextFloat() * 6.28f, 0.4f + rng.nextFloat() * 2.2f });

        for (int i = 0; i < 16; ++i)
        {
            const float a = rng.nextFloat() * 6.28f, d = std::sqrt (rng.nextFloat()) * 0.82f;
            craters.push_back ({ d * std::cos (a), d * std::sin (a), 0.05f + rng.nextFloat() * rng.nextFloat() * 0.2f });
        }
    }

    void setMoonArea (juce::Rectangle<float> area) { moonArea = area; }

    void update (const State& s, float dt)
    {
        state = s;
        time += dt;
        smoothedLevel += (juce::jlimit (0.0f, 1.0f, s.level * 4.0f) - smoothedLevel) * 0.15f;
        rippleClock += dt / juce::jmap (juce::jmax (0.0f, s.decay), 2.0f, 6.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // Sky gradient + soft nebula haze behind the moon.
        juce::ColourGradient sky (Palette::skyTop, 0, 0, Palette::skyBottom, 0, b.getHeight(), false);
        sky.addColour (0.55, Palette::skyMid);
        g.setGradientFill (sky);
        g.fillAll();

        const auto mc = moonArea.getCentre();
        juce::ColourGradient haze (juce::Colour (0x40584a9e), mc.x, mc.y + 40.0f,
                                   juce::Colour (0x00584a9e), mc.x + b.getWidth() * 0.55f, mc.y + 40.0f, true);
        g.setGradientFill (haze);
        g.fillRect (b);

        // Stars
        for (const auto& s : stars)
        {
            const float tw = 0.55f + 0.45f * std::sin (time * s.speed + s.phase);
            g.setColour (juce::Colours::white.withAlpha (0.15f + 0.6f * tw * (s.size / 1.7f)));
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
        const float r = juce::jmap (state.size, maxR * 0.48f, maxR * 0.86f);
        const float wob = state.drift * state.drift;
        const juce::Point<float> c (moonArea.getCentreX() + std::sin (time * 0.7f) * wob * 6.0f,
                                    moonArea.getCentreY() + std::cos (time * 0.53f) * wob * 4.0f);
        const bool inverted = state.decay < 0.0f;
        const float invAmt = inverted ? -state.decay : 0.0f;

        const auto moonBase = juce::Colour (0xffd9d5e6).interpolatedWith (juce::Colour (0xffe8c39f), state.tape * 0.6f);
        const auto glowCol = (inverted ? juce::Colour (0xffff8a5c) : juce::Colour (0xffc9bfff))
                                 .interpolatedWith (Palette::accentWarm, state.tape * 0.5f);

        // Glow
        const float glowAmt = (0.12f + 0.4f * state.mix + 0.5f * smoothedLevel) * active;
        const float glowR = r * (1.6f + 0.6f * std::abs (state.decay));
        juce::ColourGradient glow (glowCol.withAlpha (juce::jmin (0.85f, glowAmt)), c.x, c.y,
                                   glowCol.withAlpha (0.0f), c.x + glowR, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (glowR * 2.0f, glowR * 2.0f).withCentre (c));

        // Halo ripples, expanding outward; count follows decay length.
        if (! inverted)
        {
            const int rings = 1 + (int) (state.decay * 4.0f);
            for (int k = 0; k < rings; ++k)
            {
                const float ph = std::fmod (rippleClock + (float) k / (float) rings, 1.0f);
                const float rr = r * 1.06f + ph * (maxR * 1.12f - r * 1.06f);
                g.setColour (glowCol.withAlpha ((1.0f - ph) * (1.0f - ph) * 0.4f * active));
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.2f);
            }
        }

        // Sphere
        const auto disc = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        juce::ColourGradient body (moonBase.brighter (0.15f), c.x - r * 0.35f, c.y - r * 0.4f,
                                   moonBase.darker (1.1f), c.x + r * 0.9f, c.y + r * 0.9f, true);
        g.setGradientFill (body);
        g.fillEllipse (disc);

        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addEllipse (disc);
            g.reduceClipRegion (clip);

            // Maria (big soft dark patches)
            g.setColour (juce::Colour (0xff6d6a85).withAlpha (0.22f));
            g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.35f, r * 0.7f, r * 0.5f);
            g.fillEllipse (c.x + r * 0.05f, c.y - r * 0.05f, r * 0.55f, r * 0.62f);
            g.fillEllipse (c.x - r * 0.25f, c.y + r * 0.3f, r * 0.45f, r * 0.3f);

            // Craters drift around the sphere with Drift.
            const float spin = time * (0.015f + 0.09f * state.drift);
            for (const auto& cr : craters)
            {
                float u = std::fmod (cr.u + spin + 1.0f, 2.0f) - 1.0f;
                const float fore = std::sqrt (juce::jmax (0.0f, 1.0f - u * u));
                const float w = cr.r * r * 2.0f * fore, h = cr.r * r * 2.0f;
                const auto e = juce::Rectangle<float> (w, h).withCentre ({ c.x + u * r, c.y + cr.v * r });
                g.setColour (juce::Colour (0xff4a475e).withAlpha (0.35f * fore));
                g.fillEllipse (e);
                g.setColour (juce::Colours::white.withAlpha (0.18f * fore));
                g.drawEllipse (e.translated (-0.6f, -0.6f), 0.8f);
            }

            // Tape: warm wash
            g.setColour (juce::Colour (0xffff9a5a).withAlpha (state.tape * 0.22f));
            g.fillEllipse (disc);

            // Terminator shading
            juce::ColourGradient shade (juce::Colours::transparentBlack, c.x - r * 0.2f, c.y - r * 0.2f,
                                        juce::Colour (0xc0070914), c.x + r * 1.1f, c.y + r * 1.0f, true);
            g.setGradientFill (shade);
            g.fillEllipse (disc);

            // Eclipse shadow for inverted decay.
            if (inverted)
            {
                const float off = r * (2.1f - 1.7f * invAmt);
                const auto shadow = disc.translated (off, -off * 0.12f).expanded (r * 0.04f);
                juce::ColourGradient umbra (Palette::skyTop.withAlpha (0.92f), shadow.getCentreX(), shadow.getCentreY(),
                                            Palette::skyTop.withAlpha (0.6f), shadow.getRight(), shadow.getCentreY(), true);
                g.setGradientFill (umbra);
                g.fillEllipse (shadow);
            }

            if (state.bypassed)
            {
                g.setColour (Palette::skyTop.withAlpha (0.45f));
                g.fillEllipse (disc);
            }
        }

        // Rim light
        g.setColour ((inverted ? juce::Colour (0xffff8a5c) : juce::Colours::white).withAlpha (inverted ? 0.55f * invAmt + 0.1f : 0.12f));
        g.drawEllipse (disc, inverted ? 1.6f : 1.0f);
    }

    juce::Rectangle<float> moonArea;
    std::vector<Star> stars;
    std::vector<Crater> craters;
    State state;
    float time = 0.0f, smoothedLevel = 0.0f, rippleClock = 0.0f;
};
