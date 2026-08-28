#include "ExternalPluginHost.h"
#include <cmath>

namespace groove
{
class ExternalPluginHost::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow(juce::AudioProcessor& proc)
        : DocumentWindow(proc.getName(),
                         juce::Colour(0xff0a151e),
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar(true);
        if (auto* editor = proc.createEditorIfNeeded())
        {
            setContentNonOwned(editor, true);
            setResizable(true, false);
            centreWithSize(juce::jmax(400, editor->getWidth()),
                           juce::jmax(300, editor->getHeight()));
        }
        else
        {
            centreWithSize(400, 120);
        }
        setVisible(true);
        setAlwaysOnTop(false);
    }

    void closeButtonPressed() override { setVisible(false); }
};

ExternalPluginHost::ExternalPluginHost()
{
    formatManager.addDefaultFormats();
}

ExternalPluginHost::~ExternalPluginHost()
{
    hideEditor();
    const juce::ScopedLock sl(pluginLock);
    plugin.reset();
}

void ExternalPluginHost::configureBuses(juce::AudioPluginInstance& inst)
{
    // UJAM Virtual Drummer / Beatmaker expose Kick, Snare, OH, Room, etc.
    // Forcing a 2-channel layout often leaves only the Kick bus live.
    juce::AudioProcessor::BusesLayout stereoOnly;
    stereoOnly.inputBuses = inst.getBusesLayout().inputBuses;
    stereoOnly.outputBuses.add(juce::AudioChannelSet::stereo());
    if (inst.setBusesLayout(stereoOnly))
        return;

    inst.enableAllBuses();

    int mixBus = -1;
    for (int i = 0; i < inst.getBusCount(false); ++i)
    {
        auto* bus = inst.getBus(false, i);
        if (bus == nullptr)
            continue;
        const auto name = bus->getName().toLowerCase();
        if (name.contains("main") || name.contains("mix") || name.contains("master")
            || name.contains("stereo") || (name.contains("out") && ! name.contains("kick")
                                           && ! name.contains("snare") && ! name.contains("hat")))
        {
            mixBus = i;
            break;
        }
    }

    if (mixBus >= 0)
    {
        for (int i = 0; i < inst.getBusCount(false); ++i)
            if (auto* bus = inst.getBus(false, i))
                bus->enable(i == mixBus);
        return;
    }

    // Last resort: keep every bus and fold them in process().
}

void ExternalPluginHost::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = juce::jmax(1, bs);

    const juce::ScopedLock sl(pluginLock);
    if (plugin != nullptr)
    {
        configureBuses(*plugin);
        plugin->setRateAndBufferSizeDetails(sampleRate, blockSize);
        plugin->prepareToPlay(sampleRate, blockSize);
        const int outs = juce::jmax(2, plugin->getTotalNumOutputChannels());
        pluginBuffer.setSize(outs, blockSize, false, false, true);
    }
    else
    {
        pluginBuffer.setSize(2, blockSize, false, false, true);
    }
}

void ExternalPluginHost::release()
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin != nullptr)
        plugin->releaseResources();
}

bool ExternalPluginHost::loadFromFile(const juce::File& file, juce::String& error)
{
    hideEditor();

    juce::OwnedArray<juce::PluginDescription> types;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        formatManager.getFormat(i)->findAllTypesForFile(types, file.getFullPathName());

    juce::PluginDescription* chosen = nullptr;
    for (auto* type : types)
        if (type != nullptr && type->isInstrument)
        {
            chosen = type;
            break;
        }
    if (chosen == nullptr && ! types.isEmpty())
        chosen = types.getFirst();

    if (chosen == nullptr)
    {
        error = "No plugin found in " + file.getFileName();
        return false;
    }

    auto instance = formatManager.createPluginInstance(*chosen, sampleRate, blockSize, error);
    if (instance == nullptr)
        return false;

    configureBuses(*instance);
    instance->setRateAndBufferSizeDetails(sampleRate, blockSize);
    instance->prepareToPlay(sampleRate, blockSize);
    instance->setNonRealtime(false);

    const int outs = juce::jmax(2, instance->getTotalNumOutputChannels());
    pluginBuffer.setSize(outs, blockSize, false, false, true);

    {
        const juce::ScopedLock sl(pluginLock);
        plugin = std::move(instance);
    }

    error.clear();
    return true;
}

void ExternalPluginHost::unload()
{
    hideEditor();
    const juce::ScopedLock sl(pluginLock);
    plugin.reset();
}

juce::String ExternalPluginHost::getName() const
{
    const juce::ScopedLock sl(pluginLock);
    return plugin != nullptr ? plugin->getName() : juce::String();
}

void ExternalPluginHost::showEditor()
{
    juce::AudioPluginInstance* inst = nullptr;
    {
        const juce::ScopedLock sl(pluginLock);
        inst = plugin.get();
    }
    if (inst == nullptr)
        return;

    if (editorWindow != nullptr)
    {
        editorWindow->setVisible(true);
        editorWindow->toFront(true);
        return;
    }

    editorWindow = std::make_unique<EditorWindow>(*inst);
}

void ExternalPluginHost::hideEditor()
{
    editorWindow.reset();
}

void ExternalPluginHost::mixToStereo(juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& src)
{
    const int n = dest.getNumSamples();
    const int dstCh = dest.getNumChannels();
    const int srcCh = src.getNumChannels();
    dest.clear();
    if (srcCh <= 0 || dstCh <= 0)
        return;

    if (srcCh <= 2)
    {
        for (int ch = 0; ch < dstCh; ++ch)
            dest.copyFrom(ch, 0, src, juce::jmin(ch, srcCh - 1), 0, n);
        return;
    }

    // Fold Kick/Snare/OH/Room buses into L/R so the whole kit is audible.
    for (int ch = 0; ch < srcCh; ++ch)
        dest.addFrom(ch % dstCh, 0, src, ch, 0, n);

    dest.applyGain(1.0f / std::sqrt((float) ((srcCh + dstCh - 1) / dstCh)));
}

void ExternalPluginHost::process(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool replace)
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return;

    const int n = io.getNumSamples();
    const int outCh = juce::jmax(2, plugin->getTotalNumOutputChannels());
    if (pluginBuffer.getNumSamples() < n || pluginBuffer.getNumChannels() < outCh)
        pluginBuffer.setSize(outCh, n, false, false, true);

    pluginBuffer.clear();
    juce::MidiBuffer midiCopy(midi);
    plugin->processBlock(pluginBuffer, midiCopy);

    if (replace)
    {
        mixToStereo(io, pluginBuffer);
    }
    else
    {
        juce::AudioBuffer<float> mixed(io.getNumChannels(), n);
        mixToStereo(mixed, pluginBuffer);
        for (int ch = 0; ch < io.getNumChannels(); ++ch)
            io.addFrom(ch, 0, mixed, ch, 0, n);
    }
}
}
