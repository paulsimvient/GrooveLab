#pragma once
#include "GrooveState.h"

namespace groove
{
class Ancestry
{
public:
    struct Node
    {
        int id = 0;
        int parentId = -1;
        juce::String label;
        juce::var state;
        int64 timestampMs = 0;
    };

    int capture(const GrooveState&, const juce::String& label);
    bool restoreLast(GrooveState&);
    bool restoreNode(GrooveState&, int id);

    const std::vector<Node>& nodes() const noexcept { return history; }
    int currentNodeId() const noexcept { return currentId; }

    juce::var toVar() const;
    void fromVar(const juce::var&);

private:
    std::vector<Node> history;
    int nextId = 1;
    int currentId = -1;
};
}