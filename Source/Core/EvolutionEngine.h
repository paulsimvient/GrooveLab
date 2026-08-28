#pragma once
#include "GrooveState.h"

namespace groove
{
class EvolutionEngine
{
public:
    enum class Mode
    {
        sparse,
        syncopate,
        human,
        dense,
        sound
    };

    struct Change
    {
        juce::String description;
    };

    struct Result
    {
        int requestedBudget = 0;
        int appliedChanges = 0;
        std::vector<Change> changes;
    };

    Result evolve(GrooveState& state, Mode mode);

private:
    bool trackAllowsMutation(const Track&, const Step&) const;
    bool canMutateStepLock(const GrooveState&, const Step&) const;
    int weightedCandidate(std::vector<std::pair<int,float>>& candidates);

    mutable juce::Random random;
};
}