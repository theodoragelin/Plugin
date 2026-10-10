#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    constexpr int kWidth = 860;
    constexpr int kHeight = 600;

    // Small four-point sparkle used as the brand mark.
    void drawSparkle (juce::Graphics& g, juce::Point<float> c, float r)
    {
        juce::Path p;
        p.startNewSubPath (c.x, c.y - r);
        p.quadraticTo (c.x, c.y, c.x + r, c.y);
        p.quadraticTo (c.x, c.y, c.x, c.y + r);
        p.quadraticTo (c.x, c.y, c.x - r, c.y);
        p.quadraticTo (c.x, c.y, c.x, c.y - r);
        p.closeSubPath();
        g.fillPath (p);
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& title, bool big)
    : name (title.toUpperCase()),
      isBig (big),
      param (state.getParameter (paramID)),
      attachment (state, paramID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setMouseDragSensitivity (big ? 260 : 200);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto b = getLocalBounds();
    b.removeFromBottom (isBig ? 34 : 30);
    slider.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), b.getHeight()), b.getHeight()));
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    auto labels = b.removeFromBottom (isBig ? 34 : 30);

    g.setColour (Palette::text);
    g.setFont (seleneFont (isBig ? 11.5f : 9.5f, true, 0.22f));
    g.drawText (name, labels.removeFromTop (isBig ? 18 : 15), juce::Justification::centred);

    g.setColour (slider.isMouseOverOrDragging() ? Palette::accent : Palette::textDim);
    g.setFont (seleneFont (isBig ? 10.5f : 9.5f));
    g.drawText (param->getCurrentValueAsText(), labels, juce::Justification::centredTop);
}

//==============================================================================
SeleneEditor::SeleneEditor (SeleneProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      size     (p.apvts, ParamID::size, "Size", true),
      decay    (p.apvts, ParamID::decay, "Decay", true),
      tape     (p.apvts, ParamID::tape, "Tape", true),
      drift    (p.apvts, ParamID::drift, "Drift", true),
      preDelay (p.apvts, ParamID::preDelay, "Pre-Delay", false),
      lowCut   (p.apvts, ParamID::lowCut, "Low Cut", false),
      highCut  (p.apvts, ParamID::highCut, "High Cut", false),
      width    (p.apvts, ParamID::width, "Width", false),
      mix      (p.apvts, ParamID::mix, "Mix", false),
      duck     (p.apvts, ParamID::duck, "Duck", false),
      output   (p.apvts, ParamID::output, "Output", false)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (sky);

    decay.slider.getProperties().set ("bipolar", true);

    size.slider.setTooltip ("Scale of the space, from a small room to a vast lunar crater.");
    decay.slider.setTooltip ("Tail length. Turn left of centre to invert the tail into a reverse-style swell.");
    tape.slider.setTooltip ("Saturation and high-frequency roll-off inside the tail for warmth.");
    drift.slider.setTooltip ("Pitch drift and movement in the tail. Character is set by the MOD menu.");
    mix.slider.setTooltip ("Dry/wet balance.");
    width.slider.setTooltip ("Stereo width of the reverb.");
    duck.slider.setTooltip ("Ducks the reverb while the input plays so vocals stay clear; the tail blooms in the gaps.");

    for (auto* k : { &size, &decay, &tape, &drift, &preDelay, &lowCut, &highCut, &width, &mix, &duck, &output })
        addAndMakeVisible (k);

    bypassButton.setClickingTogglesState (true);
    bypassButton.getProperties().set ("power", true);
    bypassButton.setTooltip ("Bypass the effect (smooth crossfade).");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    limitButton.setClickingTogglesState (true);
    limitButton.setTooltip ("Transparent output limiter: catches peaks above -1 dBFS so the output never clips.");
    limitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::limit, limitButton);
    addAndMakeVisible (limitButton);

    modBox.addItemList ({ "MOD  LUNAR", "MOD  DRIFT", "MOD  WARBLE" }, 1);
    modBox.setTooltip ("Modulation character used by DRIFT: smooth sines, random wander or tape wow & flutter.");
    modAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ParamID::modMode, modBox);
    addAndMakeVisible (modBox);

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    presetButton.getProperties().set ("field", true);
    prevButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x82"));
    nextButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb8"));
    for (auto* b : { &prevButton, &nextButton, &presetButton })
        addAndMakeVisible (b);

    setSize (kWidth, kHeight);
    refreshPresetLabel();
    startTimerHz (30);
}

SeleneEditor::~SeleneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SeleneEditor::resized()
{
    sky.setBounds (getLocalBounds());

    auto b = getLocalBounds();
    topBar = b.removeFromTop (34);
    sky.setTopBarHeight ((float) topBar.getHeight());
    {
        auto bar = topBar.reduced (14, 6);
        bypassButton.setBounds (bar.removeFromLeft (78));
        bar.removeFromLeft (10);
        modBox.setBounds (bar.removeFromLeft (112));
        limitButton.setBounds (bar.removeFromRight (52));

        auto presetArea = topBar.withSizeKeepingCentre (220, 22);
        presetButton.setBounds (presetArea);
        prevButton.setBounds (presetArea.getRight() + 6, presetArea.getY(), 20, 22);
        nextButton.setBounds (presetArea.getRight() + 26, presetArea.getY(), 20, 22);
    }

    titleArea = b.removeFromTop (78);

    sky.setMoonArea (juce::Rectangle<float> (kWidth / 2.0f - 160.0f, 128.0f, 320.0f, 320.0f));

    // Side columns
    const int sideW = 96, sideH = 96, sideTop = 130, step = 104;
    int y = sideTop;
    for (auto* k : { &preDelay, &lowCut, &highCut, &width })
    {
        k->setBounds (36, y, sideW, sideH);
        y += step;
    }
    y = sideTop;
    for (auto* k : { &mix, &duck, &output })
    {
        k->setBounds (kWidth - 36 - sideW, y, sideW, sideH);
        y += step;
    }

    // Macro row under the moon
    const int bigW = 118, bigH = 122, gap = 26;
    int x = (kWidth - 4 * bigW - 3 * gap) / 2;
    for (auto* k : { &size, &decay, &tape, &drift })
    {
        k->setBounds (x, kHeight - bigH - 14, bigW, bigH);
        x += bigW + gap;
    }
}

void SeleneEditor::paintOverChildren (juce::Graphics& g)
{
    // Top bar
    g.setColour (Palette::barLine);
    g.drawHorizontalLine (topBar.getBottom(), 0.0f, (float) getWidth());

    // Title
    auto t = titleArea.withTrimmedTop (10);
    g.setColour (Palette::text);
    g.setFont (seleneFont (40.0f, true, 0.06f));
    g.drawText ("SELENE", t.removeFromTop (46), juce::Justification::centredBottom);
    g.setColour (Palette::textDim);
    g.setFont (seleneFont (8.5f, true, 0.32f));
    g.drawText ("by AGELIN AUDIO", t.removeFromTop (16), juce::Justification::centred);

    // Brand sparkle, bottom-right like a signature
    g.setColour (Palette::text.withAlpha (0.55f));
    drawSparkle (g, { (float) getWidth() - 84.0f, (float) getHeight() - 74.0f }, 9.0f);
}

void SeleneEditor::timerCallback()
{
    auto get = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };

    SkyView::State s;
    s.size = get (ParamID::size);
    s.decay = get (ParamID::decay);
    s.tape = get (ParamID::tape);
    s.drift = get (ParamID::drift);
    s.mix = get (ParamID::mix);
    s.level = processor.wetLevel.load() * s.mix;
    s.bypassed = get (ParamID::bypass) > 0.5f;
    sky.update (s, 1.0f / 30.0f);

    // Mark the preset as edited once any value moves away from it.
    const int counter = processor.presetChangeCounter.load();
    bool dirty = false;
    if (counter == lastPresetCounter)
    {
        const auto& pr = kFactoryPresets[(size_t) juce::jlimit (0, (int) kFactoryPresets.size() - 1, processor.getCurrentProgram())];
        auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-3f * juce::jmax (1.0f, std::abs (b)); };
        dirty = differs (s.size, pr.size) || differs (s.decay, pr.decay) || differs (s.tape, pr.tape)
             || differs (s.drift, pr.drift) || differs (s.mix, pr.mix) || differs (get (ParamID::preDelay), pr.preDelay)
             || differs (get (ParamID::lowCut), pr.lowCut) || differs (get (ParamID::highCut), pr.highCut)
             || differs (get (ParamID::width), pr.width) || differs (get (ParamID::output), pr.output)
             || (int) get (ParamID::modMode) != pr.modMode || differs (get (ParamID::duck), pr.duck);
    }
    if (counter != lastPresetCounter || dirty != presetDirty)
    {
        lastPresetCounter = counter;
        presetDirty = dirty;
        refreshPresetLabel();
    }
}

void SeleneEditor::stepPreset (int delta)
{
    const int n = processor.getNumPrograms();
    processor.setCurrentProgram ((processor.getCurrentProgram() + delta + n) % n);
}

void SeleneEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        menu.addItem (i + 1, processor.getProgramName (i), true, i == processor.getCurrentProgram());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton).withMinimumWidth (presetButton.getWidth()),
                        [safe = juce::Component::SafePointer<SeleneEditor> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->processor.setCurrentProgram (result - 1);
                        });
}

void SeleneEditor::refreshPresetLabel()
{
    auto name = processor.getProgramName (processor.getCurrentProgram());
    presetButton.setButtonText ((presetDirty ? name + " *" : name) + juce::String::fromUTF8 ("  \xe2\x96\xbe"));
}
