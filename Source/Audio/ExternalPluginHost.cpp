#include "ExternalPluginHost.h"
#include <algorithm>
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

    void detachEditor()
    {
        clearContentComponent();
        setVisible(false);
    }
};

ExternalPluginHost::ExternalPluginHost()
{
    formatManager.addDefaultFormats();
}

ExternalPluginHost::~ExternalPluginHost()
{
    ++editorGeneration;
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
    {
        captureSnareBus(inst);
        return;
    }

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
        captureSnareBus(inst);
        return;
    }

    // Last resort: keep every bus and fold them in process().
    captureSnareBus(inst);
}

void ExternalPluginHost::captureSnareBus(juce::AudioPluginInstance& inst)
{
    snareBusChannel = -1;
    snareBusChannels = 0;
    for (int i = 0; i < inst.getBusCount(false); ++i)
    {
        auto* bus = inst.getBus(false, i);
        if (bus == nullptr)
            continue;
        const auto name = bus->getName().toLowerCase();
        if (! name.contains("snare") && ! name.contains("snr"))
            continue;
        snareBusChannel = bus->getChannelIndexInProcessBlockBuffer(0);
        snareBusChannels = juce::jmax(1, bus->getNumberOfChannels());
        if (bus->isEnabled())
            break;
    }
}

void ExternalPluginHost::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = juce::jmax(1, bs);

    const juce::ScopedLock sl(pluginLock);
    if (plugin != nullptr)
    {
        plugin->setRateAndBufferSizeDetails(sampleRate, blockSize);
        plugin->prepareToPlay(sampleRate, blockSize);
        plugin->setNonRealtime(false);
        plugin->suspendProcessing(false);
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
    if (plugin != nullptr && pluginFile == file)
    {
        if (patches.empty())
            scanUjamPatches();
        error.clear();
        return true;
    }

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
    instance->suspendProcessing(false);

    const int outs = juce::jmax(2, instance->getTotalNumOutputChannels());
    pluginBuffer.setSize(outs, blockSize, false, false, true);

    {
        const juce::ScopedLock sl(pluginLock);
        plugin = std::move(instance);
        pluginFile = file;
    }

    scanUjamPatches();
    error.clear();
    return true;
}

void ExternalPluginHost::unload()
{
    ++editorGeneration;
    hideEditor();
    const juce::ScopedLock sl(pluginLock);
    if (plugin != nullptr)
    {
        plugin->suspendProcessing(true);
        plugin->releaseResources();
    }
    plugin.reset();
    pluginFile = juce::File();
    patches.clear();
    currentPatch = 0;
}

juce::String ExternalPluginHost::getName() const
{
    const juce::ScopedLock sl(pluginLock);
    return plugin != nullptr ? plugin->getName() : juce::String();
}

juce::File ExternalPluginHost::getFile() const
{
    const juce::ScopedLock sl(pluginLock);
    return pluginFile;
}

juce::AudioProcessorParameter* ExternalPluginHost::kitParameter() const
{
    if (plugin == nullptr)
        return nullptr;

    juce::AudioProcessorParameter* best = nullptr;
    int bestScore = 0;
    for (auto* p : plugin->getParameters())
    {
        if (p == nullptr)
            continue;
        const int steps = p->getNumSteps();
        if (steps < 4 || steps > 256)
            continue;
        const auto name = p->getName(64).toLowerCase();
        int score = 0;
        if (name.contains("kit")) score += 8;
        if (name.contains("sound") && ! name.contains("kick")) score += 7;
        if (name.contains("preset")) score += 6;
        if (name.contains("patch")) score += 5;
        if (name.contains("library")) score += 4;
        if (p->isDiscrete()) score += 2;
        if (score > bestScore)
        {
            bestScore = score;
            best = p;
        }
    }
    if (bestScore > 0)
        return best;

    juce::AudioProcessorParameter* biggest = nullptr;
    int biggestSteps = 0;
    for (auto* p : plugin->getParameters())
    {
        if (p == nullptr || ! p->isDiscrete())
            continue;
        const int steps = p->getNumSteps();
        if (steps < 8 || steps > 256)
            continue;
        const auto name = p->getName(64).toLowerCase();
        if (name.contains("mix") || name.contains("vol") || name.contains("gain")
            || name.contains("pan") || name.contains("output"))
            continue;
        if (steps > biggestSteps)
        {
            biggestSteps = steps;
            biggest = p;
        }
    }
    return biggest;
}

namespace
{
juce::String paramToken(const juce::String& s)
{
    return s.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

bool paramMatchesKey(const juce::AudioProcessorParameter& p, const juce::String& key)
{
    const auto keyToken = paramToken(key);
    if (keyToken.isEmpty())
        return false;

    if (auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*>(&p))
    {
        const auto id = hosted->getParameterID();
        if (id.equalsIgnoreCase(key) || paramToken(id) == keyToken)
            return true;
    }
    if (auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&p))
    {
        if (withId->paramID.equalsIgnoreCase(key) || paramToken(withId->paramID) == keyToken)
            return true;
    }

    const auto name = p.getName(64);
    return name.equalsIgnoreCase(key) || paramToken(name) == keyToken;
}

juce::String compactStyle(const juce::String& s)
{
    return s.toLowerCase().replace("bpm", {}).removeCharacters(" -_");
}

int patchApplyRank(const juce::String& key)
{
    if (key == "kit")
        return 0;
    if (key == "style")
        return 1;
    return 2;
}
}

void ExternalPluginHost::scanUjamPatches()
{
    patches.clear();
    currentPatch = 0;
    if (pluginFile.getFullPathName().isEmpty())
        return;

    const auto stem = pluginFile.getFileNameWithoutExtension();
    const juce::File factory = juce::File("/Library/Application Support/UJAM")
        .getChildFile(stem).getChildFile("Presets");
    const juce::File user = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile("UJAM")
        .getChildFile(stem)
        .getChildFile("Presets");

    juce::Array<juce::File> files;
    for (const auto& root : { factory, user })
        if (root.isDirectory())
            files.addArray(root.findChildFiles(juce::File::findFiles, true, "*.patch"));

    struct Order
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            const auto pa = a.getParentDirectory().getFileName() + "/" + a.getFileNameWithoutExtension();
            const auto pb = b.getParentDirectory().getFileName() + "/" + b.getFileNameWithoutExtension();
            return pa.compareNatural(pb);
        }
    };
    Order order;
    files.sort(order);

    for (const auto& f : files)
    {
        PatchEntry e;
        e.file = f;
        e.name = f.getFileNameWithoutExtension();
        e.category = f.getParentDirectory().getFileName();
        const auto parsed = juce::JSON::parse(f);
        if (auto* obj = parsed.getDynamicObject())
        {
            const auto settings = obj->getProperty("dsp_settings");
            if (auto* arr = settings.getArray())
            {
                for (const auto& item : *arr)
                {
                    auto* o = item.getDynamicObject();
                    if (o == nullptr)
                        continue;
                    if (o->getProperty("key").toString() == "style")
                    {
                        e.styleName = o->getProperty("valueName").toString();
                        break;
                    }
                }
            }
        }
        patches.push_back(std::move(e));
    }

    for (auto& e : patches)
    {
        int count = 0;
        for (const auto& other : patches)
            if (other.name == e.name)
                ++count;
        e.uniqueName = count <= 1;
    }

    guessCurrentPatchFromLiveState();
}

void ExternalPluginHost::guessCurrentPatchFromLiveState()
{
    if (patches.empty() || plugin == nullptr)
        return;

    juce::String liveStyle;
    {
        const juce::ScopedLock sl(pluginLock);
        if (plugin == nullptr)
            return;
        for (auto* p : plugin->getParameters())
        {
            if (p != nullptr && paramMatchesKey(*p, "style"))
            {
                liveStyle = p->getCurrentValueAsText();
                break;
            }
        }
    }
    if (liveStyle.isEmpty())
        return;

    const auto liveCompact = compactStyle(liveStyle);
    for (int i = 0; i < (int) patches.size(); ++i)
    {
        if (patches[(size_t) i].styleName.isEmpty())
            continue;
        if (compactStyle(patches[(size_t) i].styleName) == liveCompact)
        {
            currentPatch = i;
            lastMidiProgram.store(i);
            return;
        }
    }
}

void ExternalPluginHost::applyPatchFile(const juce::File& file)
{
    juce::AudioPluginInstance* inst = nullptr;
    {
        const juce::ScopedLock sl(pluginLock);
        inst = plugin.get();
    }
    if (inst == nullptr || ! file.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(file);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto dsp = obj->getProperty("dsp_settings");
    auto* arr = dsp.getArray();
    if (arr == nullptr)
        return;

    const auto& params = inst->getParameters();
    struct Setting { juce::String key; float value; };
    std::vector<Setting> toApply;
    toApply.reserve((size_t) arr->size());
    for (const auto& item : *arr)
    {
        auto* o = item.getDynamicObject();
        if (o == nullptr)
            continue;
        const auto key = o->getProperty("key").toString();
        if (key.isEmpty())
            continue;
        toApply.push_back({ key, juce::jlimit(0.0f, 1.0f, (float) o->getProperty("value")) });
    }

    std::stable_sort(toApply.begin(), toApply.end(),
        [](const Setting& a, const Setting& b)
        {
            return patchApplyRank(a.key) < patchApplyRank(b.key);
        });

    // Never fall back to parameter index. UJAM's mix_instselect_* values are 0 for
    // "sample 1"; writing those onto the Kick/Snare mix faders mutes them.
    for (const auto& setting : toApply)
    {
        juce::AudioProcessorParameter* p = nullptr;
        for (auto* candidate : params)
            if (candidate != nullptr && paramMatchesKey(*candidate, setting.key))
            {
                p = candidate;
                break;
            }
        if (p == nullptr)
            continue;

        p->beginChangeGesture();
        p->setValueNotifyingHost(setting.value);
        p->endChangeGesture();
    }
}

int ExternalPluginHost::getKitCount() const
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return 0;
    if (! patches.empty())
        return (int) patches.size();
    const int programs = plugin->getNumPrograms();
    if (programs > 1)
        return programs;
    if (auto* p = kitParameter())
        return juce::jmax(2, p->getNumSteps());
    return 128;
}

int ExternalPluginHost::getKitIndex() const
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return 0;
    if (! patches.empty())
        return juce::jlimit(0, (int) patches.size() - 1, currentPatch);
    const int programs = plugin->getNumPrograms();
    if (programs > 1)
        return juce::jlimit(0, programs - 1, plugin->getCurrentProgram());
    if (auto* p = kitParameter())
    {
        const int steps = juce::jmax(2, p->getNumSteps());
        return juce::jlimit(0, steps - 1, (int) std::round(p->getValue() * (float) (steps - 1)));
    }
    return juce::jlimit(0, 127, lastMidiProgram.load());
}

juce::String ExternalPluginHost::getKitName(int index) const
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return {};
    if (! patches.empty())
    {
        const auto& e = patches[(size_t) juce::jlimit(0, (int) patches.size() - 1, index)];
        if (e.uniqueName)
            return e.name;
        return e.category + " · " + e.name;
    }
    const int programs = plugin->getNumPrograms();
    if (programs > 1)
    {
        auto name = plugin->getProgramName(juce::jlimit(0, programs - 1, index));
        if (name.isNotEmpty())
            return name;
    }
    if (auto* p = kitParameter())
    {
        const int steps = juce::jmax(2, p->getNumSteps());
        const int i = juce::jlimit(0, steps - 1, index);
        const float v = steps > 1 ? (float) i / (float) (steps - 1) : 0.0f;
        auto text = p->getText(v, 64);
        if (text.isNotEmpty())
            return text;
    }
    auto live = plugin->getProgramName(plugin->getCurrentProgram());
    if (live.isNotEmpty() && index == lastMidiProgram.load())
        return live;
    return juce::String(index + 1).paddedLeft('0', 2);
}

juce::String ExternalPluginHost::getCurrentPatchName() const
{
    const juce::ScopedLock sl(pluginLock);
    if (patches.empty())
        return {};
    return patches[(size_t) juce::jlimit(0, (int) patches.size() - 1, currentPatch)].name;
}

juce::String ExternalPluginHost::getStyleName() const
{
    const juce::ScopedLock sl(pluginLock);
    if (patches.empty())
        return {};
    auto s = patches[(size_t) juce::jlimit(0, (int) patches.size() - 1, currentPatch)].styleName;
    if (s.containsChar(' ') && ! s.containsIgnoreCase("bpm"))
    {
        const auto bpmPart = s.upToFirstOccurrenceOf(" ", false, false);
        const auto rest = s.fromFirstOccurrenceOf(" ", false, false);
        if (bpmPart.containsOnly("0123456789"))
            return bpmPart + " bpm - " + rest;
    }
    return s;
}

void ExternalPluginHost::setKitIndex(int index)
{
    if (! patches.empty())
    {
        currentPatch = juce::jlimit(0, (int) patches.size() - 1, index);
        lastMidiProgram.store(currentPatch);
        applyPatchFile(patches[(size_t) currentPatch].file);
        return;
    }

    juce::AudioPluginInstance* inst = nullptr;
    juce::AudioProcessorParameter* param = nullptr;
    int programs = 0;
    int steps = 128;
    {
        const juce::ScopedLock sl(pluginLock);
        inst = plugin.get();
        if (inst == nullptr)
            return;
        programs = inst->getNumPrograms();
        param = kitParameter();
        if (param != nullptr)
            steps = juce::jmax(2, param->getNumSteps());
    }

    if (programs > 1)
    {
        inst->setCurrentProgram(juce::jlimit(0, programs - 1, index));
        lastMidiProgram.store(juce::jlimit(0, programs - 1, index));
        pendingProgramChange.store(lastMidiProgram.load());
        return;
    }
    if (param != nullptr)
    {
        const int i = juce::jlimit(0, steps - 1, index);
        const float v = steps > 1 ? (float) i / (float) (steps - 1) : 0.0f;
        lastMidiProgram.store(i);
        param->setValueNotifyingHost(v);
        pendingProgramChange.store(i);
        return;
    }

    lastMidiProgram.store(juce::jlimit(0, 127, index));
    pendingProgramChange.store(lastMidiProgram.load());
}

bool ExternalPluginHost::setKitByName(const juce::String& name)
{
    if (name.isEmpty() || patches.empty())
        return false;
    for (int i = 0; i < (int) patches.size(); ++i)
    {
        if (patches[(size_t) i].name == name)
        {
            setKitIndex(i);
            return true;
        }
    }
    return false;
}

void ExternalPluginHost::stepKit(int delta)
{
    const int n = juce::jmax(1, getKitCount());
    setKitIndex((getKitIndex() + delta % n + n) % n);
}

void ExternalPluginHost::showEditor()
{
    const auto gen = editorGeneration;
    juce::Timer::callAfterDelay(50, [this, gen]
    {
        if (gen != editorGeneration)
            return;
        presentEditorNow();
    });
}

void ExternalPluginHost::presentEditorNow()
{
    juce::AudioPluginInstance* inst = nullptr;
    {
        const juce::ScopedLock sl(pluginLock);
        inst = plugin.get();
        if (inst != nullptr)
            inst->suspendProcessing(false);
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
    ++editorGeneration;
    if (editorWindow == nullptr)
        return;
    editorWindow->detachEditor();
    editorWindow.reset();
}

void ExternalPluginHost::mixToStereo(juce::AudioBuffer<float>& dest, const juce::AudioBuffer<float>& src) const
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

    // Fold Kick/Snare/OH/Room buses into L/R, with the snare bus sitting on top.
    int boost0 = snareBusChannel;
    int boostN = snareBusChannels;
    if (boost0 < 0 && srcCh >= 4)
    {
        boost0 = 2;
        boostN = juce::jmin(2, srcCh - 2);
    }

    for (int ch = 0; ch < srcCh; ++ch)
    {
        const bool snare = boost0 >= 0 && ch >= boost0 && ch < boost0 + boostN;
        const float gain = snare ? 2.2f : (ch < 2 ? 0.85f : 0.7f);
        dest.addFrom(ch % dstCh, 0, src, ch, 0, n, gain);
    }
}

void ExternalPluginHost::process(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool replace)
{
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return;

    if (plugin->isSuspended())
        plugin->suspendProcessing(false);

    const int n = io.getNumSamples();
    const int outCh = juce::jmax(2, plugin->getTotalNumOutputChannels());
    if (pluginBuffer.getNumSamples() < n || pluginBuffer.getNumChannels() < outCh)
        pluginBuffer.setSize(outCh, juce::jmax(n, blockSize), false, false, true);

    juce::AudioBuffer<float> block(pluginBuffer.getArrayOfWritePointers(),
                                   pluginBuffer.getNumChannels(), n);
    block.clear();
    juce::MidiBuffer midiCopy(midi);
    const int pc = pendingProgramChange.exchange(-1);
    if (pc >= 0)
        midiCopy.addEvent(juce::MidiMessage::programChange(1, juce::jlimit(0, 127, pc)), 0);
    plugin->processBlock(block, midiCopy);

    if (replace)
    {
        mixToStereo(io, block);
    }
    else
    {
        juce::AudioBuffer<float> mixed(io.getNumChannels(), n);
        mixToStereo(mixed, block);
        for (int ch = 0; ch < io.getNumChannels(); ++ch)
            io.addFrom(ch, 0, mixed, ch, 0, n);
    }
}
}
