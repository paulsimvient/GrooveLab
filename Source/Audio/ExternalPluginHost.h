#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>

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

    int getKitCount() const;
    int getKitIndex() const;
    juce::String getKitName(int index) const;
    juce::String getCurrentPatchName() const;
    juce::String getStyleName() const;
    void setKitIndex(int index);
    bool setKitByName(const juce::String& name);
    void stepKit(int delta);

    void setEditorIdentity(const juce::String& title, juce::Point<int> screenPos);
    void setPluginMidiChannel(int channel);
    void showEditor();
    void hideEditor();
    bool isEditorOpen() const noexcept;

    // replace=true overwrites io with plugin audio; false mixes plugin into io.
    void process(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool replace);

private:
    class EditorWindow;

    struct PatchEntry
    {
        juce::File file;
        juce::String name;
        juce::String category;
        juce::String styleName;
        bool uniqueName = true;
    };

    void configureBuses(juce::AudioPluginInstance&);
    void captureSnareBus(juce::AudioPluginInstance&);
    void mixToStereo(juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& src) const;
    juce::AudioProcessorParameter* kitParameter() const;
    void scanUjamPatches();
    void scanUadPresets();
    void applyPatchFile(const juce::File&);
    void applyUadPreset(const juce::File&);
    void guessCurrentPatchFromLiveState();

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<EditorWindow> editorWindow;
    mutable juce::CriticalSection pluginLock;
    double sampleRate = 44100.0;
    int blockSize = 512;
    juce::AudioBuffer<float> pluginBuffer;
    juce::File pluginFile;
    std::vector<PatchEntry> patches;
    int currentPatch = 0;
    int snareBusChannel = -1;
    int snareBusChannels = 0;
    uint32_t editorGeneration = 0;
    std::atomic<int> pendingProgramChange { -1 };
    std::atomic<int> lastMidiProgram { 0 };
    std::atomic<int> pluginMidiChannel { 1 };
    juce::String editorTitle;
    juce::Point<int> editorPos { 80, 80 };
    void presentEditorNow();
};
}
