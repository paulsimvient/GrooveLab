#include "ExternalPluginHost.h"
#include <algorithm>
#include <cmath>

namespace groove
{
class ExternalPluginHost::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow(juce::AudioProcessor& proc, const juce::String& title, juce::Point<int> pos)
        : DocumentWindow(title.isNotEmpty() ? title : proc.getName(),
                         juce::Colour(0xff0a151e),
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar(true);
        if (auto* editor = proc.createEditorIfNeeded())
        {
            setContentNonOwned(editor, true);
            setResizable(true, false);
            const int w = juce::jmax(400, editor->getWidth());
            const int h = juce::jmax(300, editor->getHeight());
            setSize(w, h);
            if (pos.x > 0 || pos.y > 0)
                setTopLeftPosition(pos);
            else
                centreWithSize(w, h);
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
        gforceParams.clear();
        gforceParamPlugin = nullptr;
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
    gforceParams.clear();
    gforceParamPlugin = nullptr;
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

juce::String gforceExpandId(const juce::String& xmlId)
{
    auto s = xmlId.replace("Xlfo", "|xlfo|").replace("Xadsr", "|xadsr|");
    juce::String split;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        if (i > 0 && split.getLastCharacter() != '|')
        {
            const auto prev = s[i - 1];
            const bool capAfterLower = juce::CharacterFunctions::isUpperCase(c)
                                       && juce::CharacterFunctions::isLowerCase(prev);
            const bool digitStart = juce::CharacterFunctions::isDigit(c)
                                    && ! juce::CharacterFunctions::isDigit(prev);
            const bool digitEnd = ! juce::CharacterFunctions::isDigit(c)
                                  && juce::CharacterFunctions::isDigit(prev)
                                  && c != '|';
            if (capAfterLower || digitStart || digitEnd)
                split << '|';
        }
        split << c;
    }

    juce::String compact;
    for (auto part : juce::StringArray::fromTokens(split, "|", {}))
    {
        auto p = part.toLowerCase();
        if (p.isEmpty())
            continue;
        if (p == "dpth") p = "depth";
        else if (p == "freq") p = "frequency";
        else if (p == "att") p = "attack";
        else if (p == "dec") p = "decay";
        else if (p == "rel") p = "release";
        else if (p == "sus") p = "sustain";
        else if (p == "amnt" || p == "amt") p = "amount";
        else if (p == "rtrg") p = "retrigger";
        else if (p == "phs") p = "phase";
        else if (p == "lck") p = "lock";
        else if (p == "shp") p = "shape";
        else if (p == "noi") p = "noise";
        else if (p == "int") p = "intro";
        else if (p == "vel") p = "velocity";
        else if (p == "del") p = "delay";
        else if (p == "crv") p = "curve";
        else if (p == "fb") p = "feedback";
        else if (p == "dist") p = "distortion";
        else if (p == "env") p = "envelope";
        compact << p;
    }
    return compact;
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

bool isHostedAutomationJunk(const juce::AudioProcessorParameter& p)
{
    const auto name = p.getName(64);
    return name.startsWithIgnoreCase("MIDI CC")
        || name.containsIgnoreCase("Bypass")
        || name.equalsIgnoreCase("POWER");
}

juce::String uadWantedToken(const juce::String& key)
{
    const auto token = paramToken(key);
    static const char* aliases[][2] = {
        { "modamt", "modamount" },
        { "osc1type", "osc1wave" },
        { "osc2type", "osc2wave" },
        { "osc3type", "osc3wave" },
        { "extfbonoff", "inputsource" },
        { "aenvattack", "ampenvattack" },
        { "aenvdecay", "ampenvdecay" },
        { "aenvsustain", "ampenvsustain" },
        { "fenvattack", "filtenvattack" },
        { "fenvdecay", "filtenvdecay" },
        { "fenvsustain", "filtenvsustain" },
        { "filtmod", "filtermod" },
        { "filtkbd13", "filterkbd13" },
        { "filtkbd23", "filterkbd23" },
        { "filtcutoff", "filtercutoff" },
        { "filtres", "filterres" },
        { "filtcontamt", "filtercontour" },
        { "modmix", "modulationmix" },
        { "monopriority", "notepriority" },
        { "trigmode", "triggermode" },
        { "pbrange", "pbendrange" },
        { "modsrc1", "modsourcea" },
        { "modsrc2", "modsourceb" },
        { "lfosync", "temposync" },
        { "lfofreq", "lforate" },
        { "lfoshape", "lfowave" },
        { "velcutoff", "veltofiltercut" },
        { "velfenv", "veltofilterenv" },
        { "velaenv", "veltoampenv" },
    };
    for (const auto& alias : aliases)
        if (token == alias[0])
            return alias[1];
    return token;
}

juce::AudioProcessorParameter* findUadControl(juce::AudioPluginInstance& inst,
                                              const juce::String& key)
{
    const auto want = uadWantedToken(key);
    if (want.isEmpty())
        return nullptr;

    juce::AudioProcessorParameter* best = nullptr;
    int bestScore = 0;
    for (auto* p : inst.getParameters())
    {
        if (p == nullptr || isHostedAutomationJunk(*p))
            continue;
        const auto name = paramToken(p->getName(64));
        int score = 0;
        if (name == want)
            score = 1000;
        else if (name.startsWith(want))
            score = 500 - (name.length() - want.length());
        else if (want.startsWith(name) && name.length() >= 5)
            score = 300;
        if (score > bestScore)
        {
            bestScore = score;
            best = p;
        }
    }
    return bestScore >= 400 ? best : nullptr;
}

bool uadTextMatches(const juce::String& text, double real)
{
    const double parsed = text.getDoubleValue();
    const double scale = juce::jmax(1.0, std::abs(real));
    return std::abs(parsed - real) <= 0.02 * scale + 0.002;
}

bool uadTextLooksDefault(const juce::AudioProcessorParameter& p)
{
    const auto mid = p.getText(0.5f, 8).trim();
    return mid == "0.50" || mid == "0.5";
}

float uadRealToNorm(juce::AudioProcessorParameter& p, const juce::var& real)
{
    if (real.isBool())
        return (bool) real ? 1.0f : 0.0f;

    const int steps = p.getNumSteps();
    if (p.isDiscrete() && steps > 1 && steps < 64)
    {
        const int idx = juce::jlimit(0, steps - 1, (int) std::lround((double) real));
        return (float) idx / (float) (steps - 1);
    }

    const double target = (double) real;
    const juce::String candidates[] = {
        real.toString(),
        juce::String(target, 6),
        juce::String(target, 3),
    };
    if (! uadTextLooksDefault(p))
    {
        for (const auto& text : candidates)
        {
            const float guess = p.getValueForText(text);
            if (guess < 0.0f || guess > 1.0f)
                continue;
            if (uadTextMatches(p.getText(guess, 32), target))
                return guess;
        }

        const double v0 = p.getText(0.0f, 32).getDoubleValue();
        const double v1 = p.getText(1.0f, 32).getDoubleValue();
        if (std::abs(v1 - v0) > 0.05)
        {
            const bool rising = v1 >= v0;
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 28; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                const double vm = p.getText(mid, 32).getDoubleValue();
                if ((rising && vm < target) || (! rising && vm > target))
                    lo = mid;
                else
                    hi = mid;
            }
            return 0.5f * (lo + hi);
        }
    }

    const double shown = p.getCurrentValueAsText().getDoubleValue();
    const float current = p.getValue();
    if (current > 0.001f && std::abs(shown / (double) current - 10.0) < 1.5)
        return juce::jlimit(0.0f, 1.0f, (float) (target / 10.0));

    return juce::jlimit(0.0f, 1.0f, (float) target);
}

float gforceXmlToNorm(juce::AudioProcessorParameter& p, float raw)
{
    const int steps = p.getNumSteps();
    const bool discrete = p.isDiscrete() && steps > 1 && steps < 512;

    // Fractional 0–1 values are already normalized (Osc1Freq=0.5, Osc2Freq=0.25).
    // Treating those as step indices slammed oscillator range to the bottom.
    if (raw > 0.0f && raw < 1.0f)
        return raw;

    if (discrete)
    {
        for (int i = 0; i < steps; ++i)
        {
            const float n = (float) i / (float) (steps - 1);
            if (uadTextMatches(p.getText(n, 32).trim(), (double) raw))
                return n;
        }

        const float nearest = std::round(raw);
        const bool whole = std::abs(raw - nearest) < 0.001f
                           && nearest >= 0.0f && nearest < (float) steps;
        // ChorusMode=2 is a real index. 0 and 1 on multi-step knobs are
        // usually musical values (OctaveTranspose=0 → unison, not −2 oct).
        if (whole && (steps == 2 || nearest > 1.0f))
            return nearest / (float) (steps - 1);
    }

    if (raw >= 0.0f && raw <= 1.0f)
        return raw;

    // Bipolar knobs stored as real values (LayersPanB = -0.38).
    {
        const double lo = p.getText(0.0f, 16).getDoubleValue();
        const double hi = p.getText(1.0f, 16).getDoubleValue();
        const double span = hi - lo;
        if (std::abs(span) > 0.05
            && raw >= (float) juce::jmin(lo, hi) - 0.02f
            && raw <= (float) juce::jmax(lo, hi) + 0.02f)
            return juce::jlimit(0.0f, 1.0f, (float) ((raw - lo) / span));
    }
    return uadRealToNorm(p, raw);
}

juce::String compactStyle(const juce::String& s)
{
    return s.toLowerCase().replace("bpm", {}).removeCharacters(" -_");
}

juce::StringArray splitPatchTags(const juce::String& s)
{
    juce::StringArray out;
    for (auto t : juce::StringArray::fromTokens(s, ",", {}))
    {
        t = t.trim();
        if (t.isNotEmpty())
            out.addIfNotAlreadyThere(t);
    }
    return out;
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

    if (patches.empty())
        scanUadPresets();
    if (patches.empty())
        scanGforcePatches();

    guessCurrentPatchFromLiveState();
}

void ExternalPluginHost::scanUadPresets()
{
    patches.clear();
    currentPatch = 0;
    const auto stem = pluginFile.getFileNameWithoutExtension();
    if (stem.isEmpty())
        return;

    juce::StringArray seenRoots;
    juce::Array<juce::File> files;
    const juce::File roots[] = {
        juce::File("/Library/Application Support/Universal Audio/Plug-Ins")
            .getChildFile(stem + ".lunacomponent")
            .getChildFile("algo.bundle/Contents/Resources/presets"),
        juce::File("/Library/Application Support/Universal Audio/Factory Presets")
            .getChildFile(stem),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Application Support/Universal Audio/workspace/component_factory_presets")
            .getChildFile(stem)
    };

    for (auto root : roots)
    {
        if (root.isSymbolicLink())
            root = root.getLinkedTarget();
        const auto key = root.getFullPathName();
        if (key.isEmpty() || seenRoots.contains(key) || ! root.isDirectory())
            continue;
        seenRoots.add(key);
        files.addArray(root.findChildFiles(juce::File::findFiles, true, "*.json"));
    }

    struct Order
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension());
        }
    };
    Order order;
    files.sort(order);

    int defaultIndex = 0;
    for (const auto& f : files)
    {
        const auto parsed = juce::JSON::parse(f);
        auto* obj = parsed.getDynamicObject();
        if (obj == nullptr)
            continue;
        const auto pluginId = obj->getProperty("plugin_id").toString();
        if (obj->getProperty("chunk").getDynamicObject() == nullptr && pluginId.isEmpty())
            continue;
        if (pluginId.isNotEmpty() && pluginId != stem)
            continue;

        PatchEntry e;
        e.file = f;
        e.name = obj->getProperty("name").toString();
        if (e.name.isEmpty())
            e.name = f.getFileNameWithoutExtension().replaceCharacter('_', ' ');
        if (auto* tags = obj->getProperty("tags").getArray(); tags != nullptr && ! tags->isEmpty())
        {
            e.category = tags->getFirst().toString();
            for (const auto& tag : *tags)
                e.types.addIfNotAlreadyThere(tag.toString());
        }
        e.collection = "Factory";
        e.author = obj->getProperty("author").toString();
        e.notes = obj->getProperty("description").toString();
        if (e.notes.isEmpty())
            e.notes = obj->getProperty("notes").toString();
        if (e.category.isEmpty())
            e.category = f.getParentDirectory().getFileName();
        e.uniqueName = true;
        if (e.name == "Default")
            defaultIndex = (int) patches.size();
        patches.push_back(std::move(e));
    }

    currentPatch = juce::jlimit(0, juce::jmax(0, (int) patches.size() - 1), defaultIndex);
    lastMidiProgram.store(currentPatch);
}

void ExternalPluginHost::scanGforcePatches()
{
    patches.clear();
    currentPatch = 0;
    const auto stem = pluginFile.getFileNameWithoutExtension();
    if (stem.isEmpty())
        return;

    const juce::File roots[] = {
        juce::File("/Library/Application Support/GForce").getChildFile(stem).getChildFile("Patches"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Application Support/GForce")
            .getChildFile(stem)
            .getChildFile("Patches"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Presets/GForce")
            .getChildFile(stem)
    };

    juce::StringArray seenRoots;
    juce::Array<juce::File> files;
    for (auto root : roots)
    {
        if (root.isSymbolicLink())
            root = root.getLinkedTarget();
        const auto key = root.getFullPathName();
        if (key.isEmpty() || seenRoots.contains(key) || ! root.isDirectory())
            continue;
        seenRoots.add(key);
        files.addArray(root.findChildFiles(juce::File::findFiles, true, "*.xml"));
    }

    struct Order
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension());
        }
    };
    Order order;
    files.sort(order);

    int defaultIndex = 0;
    for (const auto& f : files)
    {
        PatchEntry e;
        e.file = f;
        e.name = f.getFileNameWithoutExtension();
        juce::FileInputStream in(f);
        if (in.openedOk())
        {
            juce::MemoryBlock mb;
            in.readIntoMemoryBlock(mb, 2400);
            const auto header = mb.toString();
            auto grab = [&header](const juce::String& key)
            {
                const auto after = header.fromFirstOccurrenceOf(key + "=\"", false, false);
                return after.isEmpty() ? juce::String()
                                       : after.upToFirstOccurrenceOf("\"", false, false);
            };
            const auto metaName = grab("name");
            if (metaName.isNotEmpty())
                e.name = metaName;
            e.author = grab("author");
            e.notes = grab("notes");
            e.collection = grab("collection");
            e.category = grab("category");
            e.types = splitPatchTags(grab("types"));
            e.timbres = splitPatchTags(grab("timbres"));
        }
        if (e.collection.isEmpty())
        {
            const auto path = f.getFullPathName();
            e.collection = path.containsIgnoreCase("/Users/") ? "User" : stem;
        }
        if (e.category.isEmpty())
            e.category = e.collection;
        e.uniqueName = true;
        patches.push_back(std::move(e));
    }

    std::sort(patches.begin(), patches.end(),
              [](const PatchEntry& a, const PatchEntry& b)
              {
                  const int c = a.category.compareNatural(b.category);
                  return c != 0 ? c < 0 : a.name.compareNatural(b.name) < 0;
              });

    for (int i = 0; i < (int) patches.size(); ++i)
        if (patches[(size_t) i].name.equalsIgnoreCase("Init")
            || patches[(size_t) i].name.equalsIgnoreCase("Default"))
        {
            defaultIndex = i;
            break;
        }

    currentPatch = juce::jlimit(0, juce::jmax(0, (int) patches.size() - 1), defaultIndex);
    lastMidiProgram.store(currentPatch);
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

void ExternalPluginHost::applyUadPreset(const juce::File& file)
{
    const juce::ScopedLock sl(pluginLock);
    auto* inst = plugin.get();
    if (inst == nullptr || ! file.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(file);
    auto* presetObj = parsed.getDynamicObject();
    if (presetObj == nullptr)
        return;

    auto* chunk = presetObj->getProperty("chunk").getDynamicObject();
    if (chunk == nullptr)
        return;
    auto* controls = chunk->getProperty("controls").getDynamicObject();
    if (controls == nullptr)
        return;

    // Factory JSON is not the plugin's VC2! state. Push each control onto the
    // hosted Mini-Moog parameters (the ~50 real knobs, not the MIDI CC list).
    for (const auto& prop : controls->getProperties())
    {
        auto* ctrl = prop.value.getDynamicObject();
        if (ctrl == nullptr || ! ctrl->hasProperty("real_value"))
            continue;
        auto* param = findUadControl(*inst, prop.name.toString());
        if (param == nullptr)
            continue;

        const float norm = uadRealToNorm(*param, ctrl->getProperty("real_value"));
        param->setValueNotifyingHost(norm);
    }
}

void ExternalPluginHost::rebuildGforceParamMap()
{
    gforceParams.clear();
    gforceParamPlugin = plugin.get();
    if (plugin == nullptr)
        return;

    for (auto* p : plugin->getParameters())
    {
        if (p == nullptr || isHostedAutomationJunk(*p))
            continue;
        const auto key = paramToken(p->getName(80));
        if (key.isEmpty() || key.startsWith("midicc"))
            continue;
        gforceParams.push_back({ key, p });
    }
}

juce::AudioProcessorParameter* ExternalPluginHost::findGforceParam(const juce::String& xmlId,
                                                                   const juce::String& layerPrefix) const
{
    juce::String want = gforceExpandId(xmlId);
    int wantSteps = 0;
    int occurrence = 0;
    bool skipTranspose = false;

    if (xmlId == "ArpEnable")          { want = "arpenable"; wantSteps = 2; }
    else if (xmlId == "ChordsEnable")  { want = "chordsenable"; wantSteps = 2; }
    else if (xmlId == "ArpMode")       { want = "arpmode"; }
    else if (xmlId == "ChordsMode")    { want = "chordsmode"; }
    else if (xmlId == "ArpRate")       { want = "arprate"; }
    else if (xmlId == "ArpOctave")     { want = "arpoctave"; skipTranspose = true; }
    else if (xmlId == "ArpGateLength") { want = "arpgatelength"; }
    else if (xmlId == "ArpPattern")    { want = "arppattern"; }
    else if (xmlId == "ArpChance")     { want = "arpchance"; }
    else if (xmlId == "ArpSwing")      { want = "arpswing"; }
    else if (xmlId == "ChordsStrum")   { want = "chordsstrum"; }
    else if (xmlId == "ChordsPosition"){ want = "chordsposition"; }
    else if (xmlId == "LayerMode")     { want = "layermode"; }
    else if (xmlId == "Osc1Freq")      { want = "freq1"; }
    else if (xmlId == "Osc2Freq")      { want = "freq2"; }
    else if (xmlId == "OctaveTranspose") { want = "octavetranspose"; }
    else if (xmlId == "LayerVolume")   { want = "volume"; }
    else if (xmlId == "FilterEnvAmount") { want = "filterenvelopeamount"; }
    else if (xmlId == "GlobalHold")    { want = "hold"; }
    else if (xmlId == "GlobalRel")     { want = "release"; }
    else if (xmlId == "FxDelayEnableA")  { want = "delayenablea"; }
    else if (xmlId == "FxDelayEnableB")  { want = "delayenableb"; }
    else if (xmlId == "FxReverbEnableA") { want = "reverbenablea"; }
    else if (xmlId == "FxReverbEnableB") { want = "reverbenableb"; }
    else if (xmlId.startsWith("Controls"))
    {
        want = gforceExpandId(xmlId.fromFirstOccurrenceOf("Controls", false, false));
        if (want == "poly") want = "polyphony";
        else if (want == "polymode") want = "polyphonymode";
        else if (want == "portagliss") want = "portamentoglissando";
    }

    if (want.isEmpty())
        return nullptr;

    auto search = [&](const juce::String& key, int occ) -> juce::AudioProcessorParameter*
    {
        int seen = 0;
        for (const auto& entry : gforceParams)
        {
            if (entry.key != key || entry.param == nullptr)
                continue;
            if (skipTranspose && entry.key.contains("transpose"))
                continue;
            if (wantSteps > 0 && entry.param->getNumSteps() != wantSteps)
                continue;
            if (seen == occ)
                return entry.param;
            ++seen;
        }
        return nullptr;
    };

    if (layerPrefix.isNotEmpty())
        if (auto* p = search(layerPrefix + want, 0))
            return p;
    return search(want, layerPrefix == "layerb" ? 1 : occurrence);
}

void ExternalPluginHost::applyGforcePatch(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (xml == nullptr || ! xml->hasTagName("patch"))
        return;

    juce::AudioPluginInstance* inst = nullptr;
    {
        const juce::ScopedLock sl(pluginLock);
        inst = plugin.get();
    }
    if (inst == nullptr)
        return;

    if (gforceParamPlugin != inst || gforceParams.empty())
        rebuildGforceParamMap();

    // Prefer the plugin's own preset load (same bytes its browser uses).
    {
        juce::MemoryBlock raw;
        if (file.loadFileAsData(raw) && raw.getSize() > 0)
            inst->setStateInformation(raw.getData(), (int) raw.getSize());
        juce::MemoryBlock juceBin;
        juce::AudioProcessor::copyXmlToBinary(*xml, juceBin);
        inst->setStateInformation(juceBin.getData(), (int) juceBin.getSize());
    }

    auto applyParam = [this](juce::XmlElement* el, const juce::String& layerPrefix)
    {
        if (el == nullptr)
            return;
        const auto xmlId = el->getStringAttribute("id");
        if (xmlId.contains("Xlfo") || xmlId.contains("Xadsr") || xmlId == "OctaveTranspose")
            return;
        auto* p = findGforceParam(xmlId, layerPrefix);
        if (p == nullptr)
            return;
        const float raw = (float) el->getDoubleAttribute("value");
        const float norm = juce::jlimit(0.0f, 1.0f, gforceXmlToNorm(*p, raw));
        if (std::abs(p->getValue() - norm) < 0.0008f)
            return;
        p->setValueNotifyingHost(norm);
    };

    if (auto* data = xml->getChildByName("parameter_data"))
    {
        for (auto* el : data->getChildIterator())
        {
            if (el->hasTagName("PARAM"))
            {
                applyParam(el, {});
            }
            else if (el->hasTagName("parameter_layer")
                     && el->getIntAttribute("empty", 0) == 0)
            {
                const auto layerId = el->getStringAttribute("id");
                const juce::String prefix = (layerId == "2") ? "layerb" : "layera";
                for (auto* param : el->getChildIterator())
                    if (param->hasTagName("PARAM"))
                        applyParam(param, prefix);
            }
        }
    }

    for (const auto& entry : gforceParams)
    {
        if (entry.param == nullptr || ! entry.key.contains("octavetranspose"))
            continue;
        float n = entry.param->getValueForText("0");
        if (n < 0.0f || n > 1.0f)
            n = 0.0f;
        entry.param->setValueNotifyingHost(n);
    }
    inst->updateHostDisplay();
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

    if (file.hasFileExtension("xml"))
    {
        applyGforcePatch(file);
        return;
    }

    const auto parsed = juce::JSON::parse(file);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    if (obj->getProperty("chunk").getDynamicObject() != nullptr)
    {
        applyUadPreset(file);
        return;
    }

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
    if (! patches.empty())
        return (int) patches.size();
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return 0;
    const int programs = plugin->getNumPrograms();
    if (programs > 1)
        return programs;
    if (auto* p = kitParameter())
        return juce::jmax(2, p->getNumSteps());
    return 128;
}

int ExternalPluginHost::getKitIndex() const
{
    if (! patches.empty())
        return juce::jlimit(0, (int) patches.size() - 1, currentPatch);
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return 0;
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

ExternalPluginHost::PatchInfo ExternalPluginHost::getPatchInfo(int index) const
{
    PatchInfo info;
    if (patches.empty())
    {
        info.name = getKitName(index);
        return info;
    }
    const auto& e = patches[(size_t) juce::jlimit(0, (int) patches.size() - 1, index)];
    info.file = e.file;
    info.name = e.name;
    info.collection = e.collection;
    info.category = e.category;
    info.author = e.author;
    info.notes = e.notes;
    info.types = e.types;
    info.timbres = e.timbres;
    return info;
}

juce::String ExternalPluginHost::getKitName(int index) const
{
    if (! patches.empty())
    {
        const auto& e = patches[(size_t) juce::jlimit(0, (int) patches.size() - 1, index)];
        if (e.uniqueName)
            return e.name;
        return e.category + " · " + e.name;
    }
    const juce::ScopedLock sl(pluginLock);
    if (plugin == nullptr)
        return {};
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
        if (patches[(size_t) i].name == name
            || getKitName(i) == name)
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

void ExternalPluginHost::setEditorIdentity(const juce::String& title, juce::Point<int> screenPos)
{
    editorTitle = title;
    editorPos = screenPos;
    if (editorWindow != nullptr)
        editorWindow->setName(title);
}

void ExternalPluginHost::setPluginMidiChannel(int channel)
{
    pluginMidiChannel.store(juce::jlimit(1, 16, channel));
}

bool ExternalPluginHost::isEditorOpen() const noexcept
{
    return editorWindow != nullptr && editorWindow->isVisible();
}

void ExternalPluginHost::showEditor()
{
    presentEditorNow();
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

    if (editorWindow != nullptr && editorWindow->getContentComponent() == nullptr)
    {
        editorWindow->detachEditor();
        editorWindow.reset();
    }

    if (editorWindow != nullptr)
    {
        editorWindow->setVisible(true);
        editorWindow->toFront(true);
        return;
    }

    editorWindow = std::make_unique<EditorWindow>(*inst, editorTitle, editorPos);
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
    // Each host has its own buffer. UAD instruments listen on Omni / CH1,
    // so stamp this instance's channel here — not the app's routing channel.
    juce::MidiBuffer midiCopy;
    const int listenCh = juce::jlimit(1, 16, pluginMidiChannel.load());
    for (const auto metadata : midi)
    {
        auto msg = metadata.getMessage();
        if (msg.getChannel() > 0)
            msg.setChannel(listenCh);
        midiCopy.addEvent(msg, metadata.samplePosition);
    }
    const int pc = pendingProgramChange.exchange(-1);
    if (pc >= 0)
        midiCopy.addEvent(juce::MidiMessage::programChange(listenCh, juce::jlimit(0, 127, pc)), 0);
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
