#include "EventJournal.h"

namespace groove
{
EventJournal::EventJournal()
{
    getAppDataDir().createDirectory();
}

juce::File EventJournal::getAppDataDir() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Groove Lab");
}

juce::File EventJournal::getJournalFile() const
{
    return getAppDataDir().getChildFile("events.jsonl");
}

juce::File EventJournal::getStateFile() const
{
    return getAppDataDir().getChildFile("autosave.groove.json");
}

void EventJournal::append(const juce::String& type, const juce::String& detail)
{
    const juce::ScopedLock sl(lock);

    auto* o = new juce::DynamicObject();
    o->setProperty("timestampMs", juce::Time::currentTimeMillis());
    o->setProperty("type", type);
    o->setProperty("detail", detail);

    auto line = juce::JSON::toString(juce::var(o), true) + "\n";
    getJournalFile().appendText(line, false, false, "\n");
}
}