#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/SeleneLookAndFeel.h"
#include "ui/SkyView.h"

// A rotary knob with its name and live value underneath.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& title, bool big);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String name;
    bool isBig;
    juce::RangedAudioParameter* param;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
class SeleneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeleneEditor (SeleneProcessor&);
    ~SeleneEditor() override;

    void paint (juce::Graphics&) override {}
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepPreset (int delta);
    void showPresetMenu();
    void refreshPresetLabel();

    SeleneProcessor& processor;
    SeleneLookAndFeel lnf;
    SkyView sky;

    // Top bar
    juce::TextButton bypassButton { "BYPASS" };
    juce::ComboBox modBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modAttachment;

    // Main controls
    Knob size, decay, tape, drift;
    Knob preDelay, lowCut, highCut, width, mix, output;

    juce::TooltipWindow tooltips { this, 600 };
    int lastPresetCounter = -1;
    bool presetDirty = false;
    juce::Rectangle<int> topBar, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeleneEditor)
};
