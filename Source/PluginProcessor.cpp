#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    juce::String secondsText (float seconds)
    {
        return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms"
                              : juce::String (seconds, seconds < 10.0f ? 2 : 1) + " s";
    }

    // Mirrors MoonReverb::targetRT60 / swell length so the UI shows real times.
    juce::String decayText (float v, int)
    {
        const float a = std::abs (v);
        if (v < 0.0f)
            return "INV " + secondsText (0.15f + a * 2.35f);
        return secondsText (0.25f * std::pow (120.0f, a));
    }

    juce::String hzText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String msText (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String dbText (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }

    float rms (const float* d, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
        return n > 0 ? (float) std::sqrt (s / n) : 0.0f;
    }
}

//==============================================================================
SeleneProcessor::SeleneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SELENE", createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));
    pSize     = apvts.getRawParameterValue (ParamID::size);
    pDecay    = apvts.getRawParameterValue (ParamID::decay);
    pTape     = apvts.getRawParameterValue (ParamID::tape);
    pDrift    = apvts.getRawParameterValue (ParamID::drift);
    pMix      = apvts.getRawParameterValue (ParamID::mix);
    pPreDelay = apvts.getRawParameterValue (ParamID::preDelay);
    pLowCut   = apvts.getRawParameterValue (ParamID::lowCut);
    pHighCut  = apvts.getRawParameterValue (ParamID::highCut);
    pWidth    = apvts.getRawParameterValue (ParamID::width);
    pOutput   = apvts.getRawParameterValue (ParamID::output);
    pModMode  = apvts.getRawParameterValue (ParamID::modMode);
}

juce::AudioProcessorValueTreeState::ParameterLayout SeleneProcessor::createLayout()
{
    using namespace juce;
    using FloatAttr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto addFloat = [&] (const char* id, const char* name, NormalisableRange<float> range, float def,
                         std::function<String (float, int)> toText)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                            FloatAttr().withStringFromValueFunction (std::move (toText))));
    };

    addFloat (ParamID::size,  "Size",  { 0.0f, 1.0f }, 0.5f, percentText);
    addFloat (ParamID::decay, "Decay", { -1.0f, 1.0f }, 0.45f, decayText);
    addFloat (ParamID::tape,  "Tape",  { 0.0f, 1.0f }, 0.2f, percentText);
    addFloat (ParamID::drift, "Drift", { 0.0f, 1.0f }, 0.3f, percentText);
    addFloat (ParamID::mix,   "Mix",   { 0.0f, 1.0f }, 0.35f, percentText);

    NormalisableRange<float> preDelayRange (0.0f, 500.0f);
    preDelayRange.setSkewForCentre (80.0f);
    addFloat (ParamID::preDelay, "Pre-Delay", preDelayRange, 0.0f, msText);

    NormalisableRange<float> lowCutRange (20.0f, 1000.0f);
    lowCutRange.setSkewForCentre (150.0f);
    addFloat (ParamID::lowCut, "Low Cut", lowCutRange, 20.0f, hzText);

    NormalisableRange<float> highCutRange (1000.0f, 20000.0f);
    highCutRange.setSkewForCentre (6000.0f);
    addFloat (ParamID::highCut, "High Cut", highCutRange, 20000.0f, hzText);

    addFloat (ParamID::width,  "Width",  { 0.0f, 1.5f }, 1.0f, percentText);
    addFloat (ParamID::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, dbText);

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::modMode, 1 }, "Mod",
                                                         StringArray { "Lunar", "Drift", "Warble" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

//==============================================================================
bool SeleneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    // mono->mono, mono->stereo, stereo->stereo
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void SeleneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    reverb.prepare (sampleRate, samplesPerBlock);
    wetBuffer.setSize (2, samplesPerBlock);

    mixSmoothed.reset (sampleRate, 0.03);
    outputSmoothed.reset (sampleRate, 0.03);
    bypassSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (pMix->load());
    outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    bypassSmoothed.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
}

void SeleneProcessor::reset()
{
    reverb.reset();
}

void SeleneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut == 0)
        return;

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples); // mono in -> duplicate

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize (2, numSamples, false, false, true);

    selene::ReverbParams rp;
    rp.size = pSize->load();
    rp.decay = pDecay->load();
    rp.tape = pTape->load();
    rp.drift = pDrift->load();
    rp.preDelayMs = pPreDelay->load();
    rp.lowCutHz = pLowCut->load();
    rp.highCutHz = pHighCut->load();
    rp.width = pWidth->load();
    rp.modMode = (int) pModMode->load();
    reverb.setParams (rp);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (numOut > 1 ? 1 : 0);
    float* wetL = wetBuffer.getWritePointer (0);
    float* wetR = wetBuffer.getWritePointer (1);

    inputLevel.store (rms (outL, numSamples));
    reverb.process (outL, outR, wetL, wetR, numSamples);
    wetLevel.store (0.5f * (rms (wetL, numSamples) + rms (wetR, numSamples)));

    mixSmoothed.setTargetValue (pMix->load());
    outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    bypassSmoothed.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    const float halfPi = juce::MathConstants<float>::halfPi;
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        const float g = outputSmoothed.getNextValue();
        const float b = bypassSmoothed.getNextValue();
        const float dryGain = std::cos (m * halfPi);
        const float wetGain = std::sin (m * halfPi);

        const float dl = outL[i], dr = outR[i];
        const float l = (dl * dryGain + wetL[i] * wetGain) * g;
        const float r = (dr * dryGain + wetR[i] * wetGain) * g;

        if (numOut > 1)
        {
            outL[i] = l + (dl - l) * b;
            outR[i] = r + (dr - r) * b;
        }
        else
        {
            const float monoWet = 0.5f * (l + r);
            outL[i] = monoWet + (dl - monoWet) * b;
        }
    }
}

//==============================================================================
int SeleneProcessor::getNumPrograms() { return (int) kFactoryPresets.size(); }

const juce::String SeleneProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) kFactoryPresets.size()))
        return kFactoryPresets[(size_t) index].name;
    return {};
}

void SeleneProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) kFactoryPresets.size()))
        return;

    currentPreset = index;
    const auto& pr = kFactoryPresets[(size_t) index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ParamID::size, pr.size);
    set (ParamID::decay, pr.decay);
    set (ParamID::tape, pr.tape);
    set (ParamID::drift, pr.drift);
    set (ParamID::mix, pr.mix);
    set (ParamID::preDelay, pr.preDelay);
    set (ParamID::lowCut, pr.lowCut);
    set (ParamID::highCut, pr.highCut);
    set (ParamID::width, pr.width);
    set (ParamID::output, pr.output);
    set (ParamID::modMode, (float) pr.modMode);

    ++presetChangeCounter;
}

void SeleneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SeleneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset = state.getProperty ("preset", 0);
            apvts.replaceState (state);
            ++presetChangeCounter;
        }
}

juce::AudioProcessorEditor* SeleneProcessor::createEditor() { return new SeleneEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeleneProcessor(); }
