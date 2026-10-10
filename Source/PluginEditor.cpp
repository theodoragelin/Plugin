#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    constexpr int kWidth = 860;
    constexpr int kHeight = 600;
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
    b.removeFromBottom (isBig ? 36 : 30);
    slider.setBounds (b);
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    auto labels = b.removeFromBottom (isBig ? 36 : 30);

    g.setColour (Palette::text);
    g.setFont (seleneFont (isBig ? 13.5f : 11.0f, true, 0.18f));
    g.drawText (name, labels.removeFromTop (isBig ? 18 : 15), juce::Justification::centred);

    g.setColour (slider.isMouseOverOrDragging() ? Palette::accent : Palette::textDim);
    g.setFont (seleneFont (isBig ? 12.0f : 10.5f));
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

    for (auto* k : { &size, &decay, &tape, &drift, &preDelay, &lowCut, &highCut, &width, &mix, &output })
        addAndMakeVisible (k);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the effect (smooth crossfade).");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    modBox.addItemList ({ "MOD: LUNAR", "MOD: DRIFT", "MOD: WARBLE" }, 1);
    modBox.setTooltip ("Modulation character used by DRIFT: smooth sines, random wander or tape wow & flutter.");
    modAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ParamID::modMode, modBox);
    addAndMakeVisible (modBox);

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this] { showPresetMenu(); };
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
    topBar = b.removeFromTop (50);
    {
        auto bar = topBar.reduced (16, 10);
        bypassButton.setBounds (bar.removeFromLeft (84));
        bar.removeFromLeft (8);
        modBox.setBounds (bar.removeFromLeft (136));

        auto presetArea = topBar.withSizeKeepingCentre (300, 30);
        prevButton.setBounds (presetArea.removeFromLeft (30));
        nextButton.setBounds (presetArea.removeFromRight (30));
        presetButton.setBounds (presetArea.reduced (4, 0));
    }

    titleArea = b.removeFromTop (58);

    const juce::Rectangle<int> moon (kWidth / 2 - 150, 126, 300, 290);
    sky.setMoonArea (moon.toFloat());

    // Side columns
    const int sideW = 104, sideH = 100, sideTop = 112, gap = 4;
    int y = sideTop;
    for (auto* k : { &preDelay, &lowCut, &highCut })
    {
        k->setBounds (54, y, sideW, sideH);
        y += sideH + gap;
    }
    y = sideTop;
    for (auto* k : { &mix, &width, &output })
    {
        k->setBounds (kWidth - 54 - sideW, y, sideW, sideH);
        y += sideH + gap;
    }

    // Big macro row
    const int bigW = 140, bigH = 148;
    const int rowX = (kWidth - 4 * bigW - 3 * 26) / 2;
    int x = rowX;
    for (auto* k : { &size, &decay, &tape, &drift })
    {
        k->setBounds (x, kHeight - bigH - 14, bigW, bigH);
        x += bigW + 26;
    }
}

void SeleneEditor::paintOverChildren (juce::Graphics& g)
{
    // Title
    g.setColour (Palette::text);
    g.setFont (seleneFont (30.0f, true, 0.42f));
    g.drawText ("SELENE", titleArea.withTrimmedBottom (18), juce::Justification::centredBottom);
    g.setColour (Palette::textDim);
    g.setFont (seleneFont (10.5f, false, 0.35f));
    g.drawText ("LUNAR ALGORITHMIC REVERB", titleArea.withTrimmedTop (titleArea.getHeight() - 18), juce::Justification::centredTop);

    // Top bar divider
    g.setColour (Palette::outline.withAlpha (0.6f));
    g.drawHorizontalLine (topBar.getBottom(), 16.0f, (float) getWidth() - 16.0f);
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
             || (int) get (ParamID::modMode) != pr.modMode;
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
    presetButton.setButtonText (presetDirty ? name + " *" : name);
}
