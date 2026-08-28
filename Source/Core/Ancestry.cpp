#include "Ancestry.h"

namespace groove
{
int Ancestry::capture(const GrooveState& state, const juce::String& label)
{
    Node n;
    n.id = nextId++;
    n.parentId = currentId;
    n.label = label;
    n.state = state.toVar();
    n.timestampMs = juce::Time::currentTimeMillis();

    history.push_back(n);
    currentId = n.id;
    return n.id;
}

bool Ancestry::restoreLast(GrooveState& state)
{
    if (history.empty())
        return false;

    int targetParent = currentId;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (it->id == currentId)
        {
            targetParent = it->parentId;
            break;
        }
    }

    if (targetParent < 0)
        return false;

    return restoreNode(state, targetParent);
}

bool Ancestry::restoreNode(GrooveState& state, int id)
{
    for (const auto& n : history)
    {
        if (n.id == id)
        {
            if (state.fromVar(n.state))
            {
                currentId = id;
                return true;
            }
        }
    }
    return false;
}

juce::var Ancestry::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("nextId", nextId);
    root->setProperty("currentId", currentId);

    juce::Array<juce::var> arr;
    for (const auto& n : history)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", n.id);
        o->setProperty("parentId", n.parentId);
        o->setProperty("label", n.label);
        o->setProperty("timestampMs", n.timestampMs);
        o->setProperty("state", n.state);
        arr.add(juce::var(o));
    }
    root->setProperty("nodes", arr);
    return juce::var(root);
}

void Ancestry::fromVar(const juce::var& v)
{
    history.clear();

    auto* root = v.getDynamicObject();
    if (! root) return;

    nextId = (int) root->getProperty("nextId");
    currentId = (int) root->getProperty("currentId");

    auto arr = root->getProperty("nodes");
    if (! arr.isArray()) return;

    for (int i = 0; i < arr.size(); ++i)
    {
        auto* o = arr[i].getDynamicObject();
        if (! o) continue;

        Node n;
        n.id = (int) o->getProperty("id");
        n.parentId = (int) o->getProperty("parentId");
        n.label = o->getProperty("label").toString();
        n.timestampMs = (int64) o->getProperty("timestampMs");
        n.state = o->getProperty("state");
        history.push_back(n);
    }
}
}