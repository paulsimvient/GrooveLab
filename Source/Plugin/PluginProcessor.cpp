#include "PluginProcessor.h"
#include "PluginEditor.h"

GrooveLabAudioProcessor::GrooveLabAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void GrooveLabAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    grooveEngine.prepare(sampleRate, samplesPerBlock);
}

bool GrooveLabAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GrooveLabAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    grooveEngine.process(buffer, midi);
}

juce::AudioProcessorEditor* GrooveLabAudioProcessor::createEditor()
{
    return new GrooveLabAudioProcessorEditor(*this);
}

void GrooveLabAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto json = juce::JSON::toString(grooveEngine.state().toVar(), true);
    dest.append(json.toRawUTF8(), (size_t) json.getNumBytesAsUTF8());
}

void GrooveLabAudioProcessor::setStateInformation(const void* data, int size)
{
    juce::String json = juce::String::fromUTF8((const char*) data, size);
    auto v = juce::JSON::parse(json);
    if (! v.isVoid())
        grooveEngine.state().fromVar(v);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrooveLabAudioProcessor();
}