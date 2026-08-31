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

    struct PatchInfo
    {
        juce::File file;
        juce::String name;
        juce::String collection;
        juce::String category;
        juce::String author;
        juce::String notes;
        juce::StringArray types;
        juce::StringArray timbres;
    };
    PatchInfo getPatchInfo(int index) const;

    void setEditorIdentity(const juce::String& title, juce::Point<int> screenPos);
    void setPluginMidiChannel(int channel);
    void showEditor();
    void hideEditor();
    bool isEditorOpen() const noexcept;

    // replace=true overwrites io with plugin audio; false mixes plugin into io.
    void process(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool replace);
    // wetAmount: 0 = dry only, 1 = fully replaced by plugin output (host-side blend).
    void processEffect(juce::AudioBuffer<float>& io, float wetAmount = 1.0f);

    // Set a hosted effect parameter by human-readable name fragments.
    // Returns false when the plug-in exposes no matching parameter.
    bool setParameterByName(const juce::StringArray& nameFragments, float normalizedValue);

private:
    class EditorWindow;

    struct PatchEntry
    {
        juce::File file;
        juce::String name;
        juce::String collection;
        juce::String category;
        juce::String author;
        juce::String notes;
        juce::StringArray types;
        juce::StringArray timbres;
        juce::String styleName;
        bool uniqueName = true;
    };

    void configureBuses(juce::AudioPluginInstance&);
    void captureSnareBus(juce::AudioPluginInstance&);
    void mixToStereo(juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& src) const;
    juce::AudioProcessorParameter* kitParameter() const;
    void scanUjamPatches();
    void scanUadPresets();
    void scanGforcePatches();
    void applyPatchFile(const juce::File&);
    void applyUadPreset(const juce::File&);
    void applyGforcePatch(const juce::File&);
    void rebuildGforceParamMap();
    void guessCurrentPatchFromLiveState();

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<EditorWindow> editorWindow;
    mutable juce::CriticalSection pluginLock;
    double sampleRate = 44100.0;
    int blockSize = 512;
    juce::AudioBuffer<float> pluginBuffer;
    juce::AudioBuffer<float> mixScratch;
    juce::MidiBuffer midiScratch;
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
    struct GforceParam
    {
        juce::String key;
        juce::AudioProcessorParameter* param = nullptr;
    };
    std::vector<GforceParam> gforceParams;
    void* gforceParamPlugin = nullptr;
    juce::AudioProcessorParameter* findGforceParam(const juce::String& xmlId,
                                                   const juce::String& layerPrefix) const;
    void presentEditorNow();
};
}
