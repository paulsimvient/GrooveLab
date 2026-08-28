#include "EvolutionEngine.h"
#include "../Sequencer/Sequencer.h"

namespace groove
{
bool EvolutionEngine::trackAllowsMutation(const Track& tr, const Step& st) const
{
    if (tr.evolutionPolicy == EvolutionPolicy::protect)
        return false;

    if (tr.evolutionPolicy == EvolutionPolicy::anchorsOnly && st.role == StepRole::anchor)
        return false;

    return true;
}

bool EvolutionEngine::canMutateStepLock(const GrooveState& state, const Step& st) const
{
    if (! st.hasAnyLock())
        return true;

    if (state.lockResistance >= 0.999f)
        return false;

    return random.nextFloat() > state.lockResistance;
}

int EvolutionEngine::weightedCandidate(std::vector<std::pair<int,float>>& candidates)
{
    if (candidates.empty())
        return -1;

    float total = 0.0f;
    for (auto& c : candidates)
        total += juce::jmax(0.001f, c.second);

    float pick = random.nextFloat() * total;
    for (auto& c : candidates)
    {
        pick -= juce::jmax(0.001f, c.second);
        if (pick <= 0.0f)
            return c.first;
    }

    return candidates.back().first;
}

EvolutionEngine::Result EvolutionEngine::evolve(GrooveState& state, Mode mode)
{
    Result result;
    result.requestedBudget = state.surpriseBudget;

    const int maxChanges = juce::jlimit(1, 32,
        (int) std::round((1.0f - state.similarity) * 16.0f) + state.surpriseBudget);

    int budget = juce::jmin(state.surpriseBudget, maxChanges);

    struct Target { int track; int step; float weight; };
    std::vector<Target> targets;

    for (int t = 0; t < kTracks; ++t)
    {
        const auto& tr = state.tracks[t];
        for (int s = 0; s < tr.generatorSteps; ++s)
        {
            const auto& st = tr.steps[s];
            if (! trackAllowsMutation(tr, st))
                continue;

            float weight = juce::jlimit(0.05f, 1.0f, tr.evolveAmount);
            if (st.role == StepRole::ghost) weight *= 1.25f;
            if (st.role == StepRole::fill)  weight *= 1.4f;
            targets.push_back({t, s, weight});
        }
    }

    while (budget > 0 && ! targets.empty())
    {
        std::vector<std::pair<int,float>> weighted;
        weighted.reserve(targets.size());
        for (int i = 0; i < (int) targets.size(); ++i)
            weighted.push_back({i, targets[i].weight});

        int idx = weightedCandidate(weighted);
        if (idx < 0) break;

        auto target = targets[(size_t) idx];
        targets.erase(targets.begin() + idx);

        auto& tr = state.tracks[target.track];
        auto& st = tr.steps[target.step];

        bool changed = false;
        juce::String desc;

        switch (mode)
        {
            case Mode::sparse:
                if (tr.pulses > 0)
                {
                    tr.pulses--;
                    changed = true;
                    desc = voiceName(target.track) + " pulses -> " + juce::String(tr.pulses);
                }
                break;

            case Mode::dense:
                if (tr.pulses < tr.generatorSteps)
                {
                    tr.pulses++;
                    changed = true;
                    desc = voiceName(target.track) + " pulses -> " + juce::String(tr.pulses);
                }
                break;

            case Mode::syncopate:
            {
                const int before = tr.rotate;
                tr.rotate += random.nextBool() ? 1 : -1;
                changed = (tr.rotate != before);
                desc = voiceName(target.track) + " rotate -> " + juce::String(tr.rotate);
                break;
            }

            case Mode::human:
            {
                if (Sequencer::resolvedStepActive(tr, target.step))
                {
                    float before = st.velocity;
                    st.velocity = juce::jlimit(0.25f, 1.0f, before + (random.nextFloat() - 0.5f) * 0.18f);
                    st.probability = juce::jlimit(0.45f, 1.0f, st.probability + (random.nextFloat() - 0.5f) * 0.12f);
                    changed = std::abs(st.velocity - before) > 0.001f;
                    desc = voiceName(target.track) + " step " + juce::String(target.step + 1)
                         + " dynamics varied";
                }
                break;
            }

            case Mode::sound:
            {
                // Locked steps protect the corresponding track timbre at full resistance.
                if (! canMutateStepLock(state, st))
                    break;

                auto& b = tr.base;
                int which = random.nextInt(paramCount);

                switch ((Param) which)
                {
                    case Param::pitch:
                    {
                        auto before = b.pitchHz;
                        b.pitchHz = juce::jlimit(30.0f, 1800.0f,
                            before * (0.90f + random.nextFloat() * 0.20f));
                        changed = true;
                        desc = voiceName(target.track) + " BODY/PITCH evolved";
                        break;
                    }
                    case Param::decay:
                    {
                        auto before = b.decayMs;
                        b.decayMs = juce::jlimit(20.0f, 1800.0f,
                            before * (0.82f + random.nextFloat() * 0.36f));
                        changed = true;
                        desc = voiceName(target.track) + " DECAY evolved";
                        break;
                    }
                    case Param::transient:
                    {
                        b.transient = juce::jlimit(0.0f, 1.0f,
                            b.transient + (random.nextFloat() - 0.5f) * 0.16f);
                        changed = true;
                        desc = voiceName(target.track) + " TRANSIENT evolved";
                        break;
                    }
                    case Param::noise:
                    {
                        b.noise = juce::jlimit(0.0f, 1.0f,
                            b.noise + (random.nextFloat() - 0.5f) * 0.16f);
                        changed = true;
                        desc = voiceName(target.track) + " TEXTURE evolved";
                        break;
                    }
                    case Param::filter:
                    {
                        b.filter = juce::jlimit(0.02f, 0.99f,
                            b.filter + (random.nextFloat() - 0.5f) * 0.14f);
                        changed = true;
                        desc = voiceName(target.track) + " FILTER evolved";
                        break;
                    }
                    case Param::drive:
                    {
                        b.drive = juce::jlimit(0.0f, 1.0f,
                            b.drive + (random.nextFloat() - 0.5f) * 0.12f);
                        changed = true;
                        desc = voiceName(target.track) + " SATURATION evolved";
                        break;
                    }
                    case Param::space:
                    {
                        b.space = juce::jlimit(0.0f, 1.0f,
                            b.space + (random.nextFloat() - 0.5f) * 0.12f);
                        changed = true;
                        desc = voiceName(target.track) + " SPACE evolved";
                        break;
                    }
                    case Param::blend:
                    {
                        b.blend = juce::jlimit(0.0f, 1.0f,
                            b.blend + (random.nextFloat() - 0.5f) * 0.12f);
                        changed = true;
                        desc = voiceName(target.track) + " BODY/TEXTURE blend evolved";
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
        }

        if (changed)
        {
            result.appliedChanges++;
            result.changes.push_back({desc});
            --budget;
        }
    }

    return result;
}
}