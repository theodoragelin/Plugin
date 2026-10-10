#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/MoonReverb.h"

namespace ParamID
{
    inline constexpr const char* size     = "size";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* tape     = "tape";
    inline constexpr const char* drift    = "drift";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* preDelay = "predelay";
    inline constexpr const char* lowCut   = "lowcut";
    inline constexpr const char* highCut  = "highcut";
    inline constexpr const char* width    = "width";
    inline constexpr const char* output   = "output";
    inline constexpr const char* modMode  = "modmode";
    inline constexpr const char* bypass   = "bypass";
}

class SeleneProcessor : public juce::AudioProcessor
{
public:
    SeleneProcessor();
    ~SeleneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 32.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor for the animated moon.
    std::atomic<float> wetLevel { 0.0f };
    std::atomic<float> inputLevel { 0.0f };

    // Bumped whenever a preset loads so the editor can refresh its label.
    std::atomic<int> presetChangeCounter { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    selene::MoonReverb reverb;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> mixSmoothed, outputSmoothed, bypassSmoothed;

    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pTape = nullptr;
    std::atomic<float>* pDrift = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pPreDelay = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pModMode = nullptr;

    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeleneProcessor)
};
