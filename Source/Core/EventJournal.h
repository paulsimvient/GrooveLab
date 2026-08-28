#pragma once
#include <JuceHeader.h>

namespace groove
{
struct GrooveEvent
{
    int64 timestampMs = 0;
    juce::String type;
    juce::String detail;
};

class EventJournal
{
public:
    EventJournal();

    void append(const juce::String& type, const juce::String& detail);
    juce::File getJournalFile() const;
    juce::File getStateFile() const;
    juce::File getGroovesDir() const;
    juce::File getAppDataDir() const;

private:
    juce::CriticalSection lock;
};
}