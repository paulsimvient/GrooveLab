#include "CandidateGenerator.h"
#include "../../Sequencer/Sequencer.h"

namespace groove::ensemble
{
static HatCandidate fromMask(const std::array<bool, kHatSteps>& mask, float vel)
{
    HatCandidate c;
    for (int i = 0; i < kHatSteps; ++i)
    {
        c.hits[(size_t) i] = mask[(size_t) i];
        c.velocity[(size_t) i] = mask[(size_t) i] ? vel : 0.0f;
    }
    return c;
}

static void addUnique(std::vector<HatCandidate>& dest, HatCandidate c)
{
    for (const auto& e : dest)
    {
        bool same = true;
        for (int i = 0; i < kHatSteps; ++i)
            if (e.hits[(size_t) i] != c.hits[(size_t) i])
            {
                same = false;
                break;
            }
        if (same)
            return;
    }
    dest.push_back(c);
}

std::vector<HatCandidate> generateHatCandidates(const SeedFeatures& features, int variation)
{
    std::vector<HatCandidate> out;
    out.reserve(64);
    const int rotateBias = ((variation % 8) + 8) % 8;

    auto pushEuclid = [&](int pulses, int rotate)
    {
        std::array<bool, kHatSteps> mask {};
        for (int s = 0; s < kHatSteps; ++s)
            mask[(size_t) s] = groove::Sequencer::euclideanHit(s, kHatSteps, pulses, rotate);
        addUnique(out, fromMask(mask, 0.72f));
    };

    const int pulseChoices[] = { 4, 6, 8, 10, 12, 14 };
    for (int pulses : pulseChoices)
        for (int r = 0; r < 4; ++r)
            pushEuclid(pulses, (r + rotateBias) % kHatSteps);

    {
        std::array<bool, kHatSteps> complement {};
        for (int s = 0; s < kHatSteps; ++s)
            complement[(size_t) s] = ! features.occupied[(size_t) s];
        addUnique(out, fromMask(complement, 0.68f));
        std::array<bool, kHatSteps> eighthHoles {};
        for (int s = 0; s < kHatSteps; ++s)
            eighthHoles[(size_t) s] = ((s % 2) == 0) && ! features.occupied[(size_t) s];
        addUnique(out, fromMask(eighthHoles, 0.74f));
        std::array<bool, kHatSteps> offHoles {};
        for (int s = 0; s < kHatSteps; ++s)
            offHoles[(size_t) s] = ((s % 2) == 1) && ! features.kickMask[(size_t) s];
        addUnique(out, fromMask(offHoles, 0.66f));
    }

    {
        std::array<bool, kHatSteps> eights {};
        std::array<bool, kHatSteps> sixteenths {};
        std::array<bool, kHatSteps> sparse {};
        std::array<bool, kHatSteps> upbeat {};
        for (int s = 0; s < kHatSteps; ++s)
        {
            eights[(size_t) s] = (s % 2) == 0;
            sixteenths[(size_t) s] = true;
            sparse[(size_t) s] = (s % 4) == 0;
            upbeat[(size_t) s] = (s % 4) == 2;
        }
        addUnique(out, fromMask(eights, 0.70f));
        addUnique(out, fromMask(sixteenths, 0.55f));
        addUnique(out, fromMask(sparse, 0.78f));
        addUnique(out, fromMask(upbeat, 0.70f));
    }

    for (int shift = 1; shift <= 3; ++shift)
    {
        std::array<bool, kHatSteps> shifted {};
        for (int s = 0; s < kHatSteps; ++s)
            shifted[(size_t) s] = features.occupied[(size_t) ((s + kHatSteps - shift) % kHatSteps)];
        addUnique(out, fromMask(shifted, 0.64f));
    }

    {
        std::array<bool, kHatSteps> subdivided {};
        for (int s = 0; s < kHatSteps; ++s)
        {
            const bool here = features.occupied[(size_t) s];
            const bool next = features.occupied[(size_t) ((s + 1) % kHatSteps)];
            subdivided[(size_t) s] = ! here && ! next && ((s % features.impliedPulse) == 0);
        }
        addUnique(out, fromMask(subdivided, 0.62f));
    }

    if (out.empty())
    {
        std::array<bool, kHatSteps> fallback {};
        for (int s = 0; s < kHatSteps; ++s)
            fallback[(size_t) s] = (s % 2) == 0;
        out.push_back(fromMask(fallback, 0.7f));
    }
    return out;
}
}