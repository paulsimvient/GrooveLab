#pragma once
#include <JuceHeader.h>

namespace groove
{
class ExternalPluginHost
{
public:
    ExternalPluginHost();
    ~ExternalPluginHost();

    void prepare(double sampleRate, int blockSize);
    void release();

    bool loadFromFile(const juce::File& file, juce::String& error);
    void unload();
    bool isLoaded() const noexcept { return plugin != nullptr; }
    juce::String getName() const;
    juce::File getFile() const;

    void showEditor();
    void hideEditor();
    bool isEditorOpen() const noexcept { return editorWindow != nullptr; }

    // replace=true overwrites io with plugin audio; false mixes plugin into io.
    void process(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool replace);

private:
    class EditorWindow;

    void configureBuses(juce::AudioPluginInstance&);
    void captureSnareBus(juce::AudioPluginInstance&);
    void mixToStereo(juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& src) const;

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<EditorWindow> editorWindow;
    mutable juce::CriticalSection pluginLock;
    double sampleRate = 44100.0;
    int blockSize = 512;
    juce::AudioBuffer<float> pluginBuffer;
    juce::File pluginFile;
    int snareBusChannel = -1;
    int snareBusChannels = 0;
    uint32_t editorGeneration = 0;
    void presentEditorNow();
};
}
