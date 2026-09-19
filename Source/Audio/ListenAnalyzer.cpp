#include "ListenAnalyzer.h"
#include <cmath>
#include <algorithm>

namespace groove
{
namespace
{
juce::String pcName(int pc)
{
    static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[juce::jlimit(0, 11, pc)];
}

void estimateHarmonyFromChroma(BarObservation& b)
{
    float best = -1.0f;
    int bestRoot = 0;
    bool bestMinor = false;
    for (int root = 0; root < 12; ++root)
    {
        const float maj = b.chroma[(size_t) root]
                        + 0.85f * b.chroma[(size_t) ((root + 4) % 12)]
                        + 0.75f * b.chroma[(size_t) ((root + 7) % 12)];
        const float min = b.chroma[(size_t) root]
                        + 0.85f * b.chroma[(size_t) ((root + 3) % 12)]
                        + 0.75f * b.chroma[(size_t) ((root + 7) % 12)];
        if (maj > best) { best = maj; bestRoot = root; bestMinor = false; }
        if (min > best) { best = min; bestRoot = root; bestMinor = true; }
    }
    b.rootPc = bestRoot;
    b.minor = bestMinor;
    b.harmonyLabel = pcName(bestRoot) + (bestMinor ? "m" : "");
}

void normalizeAudio(std::vector<float>& audio)
{
    float peak = 0.0f;
    for (float s : audio)
        peak = juce::jmax(peak, std::abs(s));
    if (peak < 1.0e-6f)
        return;
    const float gain = 0.9f / peak;
    for (float& s : audio)
        s *= gain;
}

// YIN absolute-threshold pitch tracker. Returns Hz or 0 if unvoiced.
float yinPitchHz(const float* x, int n, double sr, float& confidenceOut)
{
    confidenceOut = 0.0f;
    if (x == nullptr || n < 128 || sr < 1000.0)
        return 0.0f;

    // Practical melodic range ~E1..C7 — still covers bass + voice/keys.
    const int tauMax = juce::jmin(n / 2, (int) std::round(sr / 41.0));
    const int tauMin = juce::jmax(2, (int) std::round(sr / 1800.0));
    if (tauMax <= tauMin + 4)
        return 0.0f;

    std::vector<float> d((size_t) tauMax + 1, 0.0f);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float sum = 0.0f;
        const int count = n - tau;
        for (int i = 0; i < count; ++i)
        {
            const float diff = x[i] - x[i + tau];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }

    std::vector<float> cmnd((size_t) tauMax + 1, 1.0f);
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 1.0e-12f ? d[(size_t) tau] * (float) tau / running : 1.0f;
    }

    // Tighter than before — fewer false periods / wrong pitches.
    constexpr float threshold = 0.15f;
    int tauEst = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0)
    {
        float best = 1.0e9f;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cmnd[(size_t) tau] < best)
            {
                best = cmnd[(size_t) tau];
                tauEst = tau;
            }
        if (best > 0.40f)
            return 0.0f;
    }

    // Octave error fix: if 2×tau (fundamental period) is nearly as good, prefer it.
    // YIN often locks onto the first harmonic for bright synths / vocals.
    auto localMin = [&](int center) -> int
    {
        int t = juce::jlimit(tauMin, tauMax, center);
        while (t + 1 <= tauMax && cmnd[(size_t) (t + 1)] < cmnd[(size_t) t]) ++t;
        while (t - 1 >= tauMin && cmnd[(size_t) (t - 1)] < cmnd[(size_t) t]) --t;
        return t;
    };

    const int twice = localMin(tauEst * 2);
    if (twice > tauEst + 2 && twice <= tauMax)
    {
        const float c1 = cmnd[(size_t) tauEst];
        const float c2 = cmnd[(size_t) twice];
        if (c2 <= c1 * 1.15f + 0.02f)
            tauEst = twice;
    }
    const int t3 = localMin(tauEst * 3);
    if (t3 > tauEst + 4 && t3 <= tauMax
        && cmnd[(size_t) t3] <= cmnd[(size_t) tauEst] * 1.10f + 0.015f
        && cmnd[(size_t) t3] < threshold + 0.05f)
        tauEst = t3;

    float betterTau = (float) tauEst;
    if (tauEst > 1 && tauEst < tauMax)
    {
        const float s0 = cmnd[(size_t) (tauEst - 1)];
        const float s1 = cmnd[(size_t) tauEst];
        const float s2 = cmnd[(size_t) (tauEst + 1)];
        const float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs(denom) > 1.0e-9f)
            betterTau = (float) tauEst + (s0 - s2) / denom;
    }

    confidenceOut = juce::jlimit(0.0f, 1.0f, 1.0f - cmnd[(size_t) tauEst]);
    if (confidenceOut < 0.12f)
        return 0.0f;
    return (float) (sr / (double) juce::jmax(1.0f, betterTau));
}

int hzToPc(float hz)
{
    if (hz < 40.0f || hz > 1800.0f)
        return -1;
    const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    int note = (int) std::lround(midi);
    return ((note % 12) + 12) % 12;
}

int hzToMidi(float hz)
{
    if (hz < 40.0f || hz > 1800.0f)
        return -1;
    const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    return juce::jlimit(24, 96, (int) std::lround(midi));
}

// Fold midi into the octave nearest to anchor (fixes jump-octave flicker).
int foldMidiNear(int midi, int anchor)
{
    if (midi < 0) return -1;
    if (anchor < 0) return midi;
    while (midi - anchor > 6) midi -= 12;
    while (anchor - midi > 6) midi += 12;
    return juce::jlimit(24, 96, midi);
}

// Pick the more confident of a short and long YIN window, with octave continuity.
int estimateMidiDual(const float* audio, int available, double sr, float& confOut,
                     float energy, int prevMidi)
{
    confOut = 0.0f;
    if (audio == nullptr || available < 256 || energy < 0.0035f)
        return -1;

    // Prefer longer window for stability; short window as backup for fast notes.
    const int shortN = juce::jlimit(384, available, (int) std::round(sr * 0.040));
    const int longN  = juce::jlimit(512, available, (int) std::round(sr * 0.080));

    float cShort = 0.0f, cLong = 0.0f;
    const float hzShort = yinPitchHz(audio, shortN, sr, cShort);
    const float hzLong  = longN > shortN + 64 ? yinPitchHz(audio, longN, sr, cLong) : 0.0f;

    int midiShort = hzToMidi(hzShort);
    int midiLong  = hzToMidi(hzLong);
    if (midiShort >= 0) midiShort = foldMidiNear(midiShort, prevMidi);
    if (midiLong  >= 0) midiLong  = foldMidiNear(midiLong, prevMidi);

    // Prefer long-window result when both exist and agree within a semitone.
    if (midiLong >= 0 && midiShort >= 0)
    {
        if (std::abs(midiLong - midiShort) <= 1)
        {
            confOut = juce::jmax(cLong, cShort);
            return midiLong;
        }
        // Disagreement: pick higher confidence, but bias toward previous midi.
        float scoreL = cLong, scoreS = cShort;
        if (prevMidi >= 0)
        {
            scoreL += (std::abs(midiLong - prevMidi) <= 2) ? 0.12f : 0.0f;
            scoreS += (std::abs(midiShort - prevMidi) <= 2) ? 0.12f : 0.0f;
            // Prefer smaller leap from previous.
            scoreL -= 0.02f * (float) std::abs(midiLong - prevMidi);
            scoreS -= 0.02f * (float) std::abs(midiShort - prevMidi);
        }
        if (scoreL >= scoreS) { confOut = cLong; return midiLong; }
        confOut = cShort;
        return midiShort;
    }
    if (midiLong >= 0)  { confOut = cLong;  return midiLong; }
    if (midiShort >= 0) { confOut = cShort; return midiShort; }
    return -1;
}

// One threshold onset → one MIDI note (no invented fills).
std::vector<HeardNote> notesFromOnsets(const float* audio, int n, double sr,
                                       const std::vector<int>& onsets,
                                       int totalSteps, double samplesPerStep)
{
    std::vector<HeardNote> notes;
    if (audio == nullptr || n < 256 || onsets.empty() || totalSteps < 1 || samplesPerStep < 1.0)
        return notes;

    const int pitchDelay = juce::jmax(0, (int) std::round(sr * 0.012));
    const int pitchWin = juce::jlimit(384, 4096, (int) std::round(sr * 0.070));
    int prevMidi = -1;

    for (size_t oi = 0; oi < onsets.size(); ++oi)
    {
        const int os = onsets[oi];
        if (os < 0 || os >= n - 64)
            continue;

        const int nextOs = (oi + 1 < onsets.size()) ? onsets[oi + 1] : n;
        const int pitchAt = juce::jlimit(0, n - 256, os + pitchDelay);
        const int avail = n - pitchAt;

        float energy = 0.0f;
        const int eN = juce::jmin(pitchWin, avail);
        for (int k = 0; k < eN; ++k)
            energy += audio[pitchAt + k] * audio[pitchAt + k];
        energy = std::sqrt(energy / (float) juce::jmax(1, eN));

        float conf = 0.0f;
        int midi = estimateMidiDual(audio + pitchAt, avail, sr, conf, energy, prevMidi);
        if (midi < 0 || conf < 0.10f)
        {
            // Retry right at the attack.
            midi = estimateMidiDual(audio + os, n - os, sr, conf, energy, prevMidi);
        }
        if (midi < 0 || conf < 0.08f)
            continue;
        prevMidi = midi;

        // Length: until next onset or until energy falls off (~gate close).
        int endSample = nextOs;
        const float peakE = juce::jmax(energy, 0.01f);
        const float closeE = peakE * 0.35f;
        const int hop = juce::jmax(64, (int) std::round(sr * 0.005));
        int quietHops = 0;
        for (int s = os + hop; s + hop < nextOs; s += hop)
        {
            float e = 0.0f;
            for (int k = 0; k < hop; ++k)
                e += audio[s + k] * audio[s + k];
            e = std::sqrt(e / (float) hop);
            if (e < closeE)
            {
                if (++quietHops >= 4)
                {
                    endSample = s;
                    break;
                }
            }
            else
            {
                quietHops = 0;
            }
        }

        int step = (int) std::lround((double) os / samplesPerStep);
        step = juce::jlimit(0, totalSteps - 1, step);
        int endStep = (int) std::lround((double) endSample / samplesPerStep);
        endStep = juce::jlimit(step + 1, totalSteps, endStep);

        // Don't double-place if two onsets quantized to the same step.
        if (! notes.empty() && notes.back().step == step)
        {
            notes.back().midi = midi;
            notes.back().confidence = juce::jmax(notes.back().confidence, conf);
            notes.back().lengthSteps = juce::jmax(notes.back().lengthSteps, endStep - step);
            notes.back().velocity = juce::jmax(notes.back().velocity,
                                               juce::jlimit(0.35f, 1.0f, 0.45f + energy * 4.0f));
            continue;
        }

        HeardNote hn;
        hn.step = step;
        hn.midi = midi;
        hn.lengthSteps = juce::jmax(1, endStep - step);
        hn.confidence = conf;
        hn.velocity = juce::jlimit(0.35f, 1.0f, 0.45f + energy * 4.0f);
        notes.push_back(hn);
    }
    return notes;
}

// Frame-by-frame YIN → quantized MIDI notes (secondary / fill-in only).
std::vector<HeardNote> transcribeNotes(const float* audio, int n, double sr,
                                       int totalSteps, double samplesPerStep)
{
    std::vector<HeardNote> notes;
    if (audio == nullptr || n < 256 || totalSteps < 1 || samplesPerStep < 1.0)
        return notes;

    // Longer frames + denser hops catch short notes and low bass better.
    const int frame = juce::jlimit(512, 4096, (int) std::round(sr * 0.055));
    const int hop = juce::jmax(48, frame / 4);

    // Adaptive energy floor from the take (normalized audio still has quiet vs loud notes).
    float energyMean = 0.0f;
    int energyCount = 0;
    for (int i = 0; i + hop <= n; i += hop * 4)
    {
        float e = 0.0f;
        const int use = juce::jmin(hop, n - i);
        for (int k = 0; k < use; ++k)
            e += audio[i + k] * audio[i + k];
        energyMean += std::sqrt(e / (float) juce::jmax(1, use));
        ++energyCount;
    }
    energyMean = energyCount > 0 ? energyMean / (float) energyCount : 0.02f;
    const float energyFloor = juce::jmax(0.0020f, energyMean * 0.12f);

    struct FramePitch { int sample = 0; int midi = -1; float conf = 0.0f; float energy = 0.0f; };
    std::vector<FramePitch> frames;
    frames.reserve((size_t) (n / juce::jmax(1, hop)) + 1);

    int prevMidi = -1;
    for (int i = 0; i + 256 <= n; i += hop)
    {
        const int avail = n - i;
        float energy = 0.0f;
        const int eN = juce::jmin(frame, avail);
        for (int k = 0; k < eN; ++k)
            energy += audio[i + k] * audio[i + k];
        energy = std::sqrt(energy / (float) juce::jmax(1, eN));

        float conf = 0.0f;
        int midi = estimateMidiDual(audio + i, avail, sr, conf, energy, prevMidi);
        if (midi >= 0 && conf >= 0.12f)
            prevMidi = midi;
        else if (midi >= 0 && conf >= 0.08f && energy >= energyFloor * 1.5f)
            ; // keep weak-but-audible pitch
        else
            midi = -1;
        frames.push_back({ i, energy >= energyFloor ? midi : -1, conf, energy });
    }

    if (frames.empty())
        return notes;

    // Smooth pitch flicker: hold previous MIDI across brief unvoiced / ±1 jumps.
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int i = 1; i + 1 < (int) frames.size(); ++i)
        {
            auto& cur = frames[(size_t) i];
            const auto& prev = frames[(size_t) (i - 1)];
            const auto& next = frames[(size_t) (i + 1)];
            if (cur.midi < 0 && prev.midi >= 0 && next.midi == prev.midi && prev.conf > 0.12f)
            {
                cur.midi = prev.midi;
                cur.conf = 0.5f * (prev.conf + next.conf);
            }
            else if (cur.midi >= 0 && prev.midi >= 0
                     && std::abs(cur.midi - prev.midi) <= 1
                     && (next.midi == prev.midi || next.midi < 0)
                     && cur.conf + 0.05f < prev.conf)
            {
                cur.midi = prev.midi;
            }
            else if (cur.midi >= 0 && prev.midi >= 0 && next.midi >= 0
                     && prev.midi == next.midi && cur.midi != prev.midi)
            {
                // Median snap: middle frame disagreed — force majority.
                cur.midi = prev.midi;
            }
        }
    }

    auto flush = [&](int startIdx, int endIdx, int /*runMidi*/)
    {
        if (endIdx <= startIdx)
            return;

        // Majority vote by confidence — more accurate than the run's first pitch.
        std::array<float, 128> votes {};
        float confSum = 0.0f, energySum = 0.0f;
        int count = 0;
        for (int i = startIdx; i < endIdx; ++i)
        {
            const int m = frames[(size_t) i].midi;
            if (m < 0 || m > 127) continue;
            votes[(size_t) m] += juce::jmax(0.05f, frames[(size_t) i].conf);
            confSum += frames[(size_t) i].conf;
            energySum += frames[(size_t) i].energy;
            ++count;
        }
        if (count < 1)
            return;

        int midi = -1;
        float bestVote = 0.0f;
        for (int m = 0; m < 128; ++m)
            if (votes[(size_t) m] > bestVote)
            {
                bestVote = votes[(size_t) m];
                midi = m;
            }
        if (midi < 0)
            return;

        const int startSample = frames[(size_t) startIdx].sample;
        const int endSample = (endIdx < (int) frames.size())
                                ? frames[(size_t) endIdx].sample
                                : n;
        int step = (int) std::lround((double) startSample / samplesPerStep);
        step = juce::jlimit(0, totalSteps - 1, step);
        int endStep = (int) std::lround((double) endSample / samplesPerStep);
        endStep = juce::jlimit(step + 1, totalSteps, endStep);

        // Merge into previous note if same pitch and near in time.
        if (! notes.empty()
            && std::abs(midi - notes.back().midi) <= 1
            && step <= notes.back().step + notes.back().lengthSteps + 4)
        {
            auto& prev = notes.back();
            if (midi != prev.midi && bestVote < votes[(size_t) prev.midi] * 1.1f)
                midi = prev.midi;
            const int newEnd = juce::jmax(prev.step + prev.lengthSteps, endStep);
            prev.lengthSteps = juce::jmax(1, newEnd - prev.step);
            prev.confidence = juce::jmax(prev.confidence, confSum / (float) juce::jmax(1, count));
            prev.velocity = juce::jmax(prev.velocity,
                                       juce::jlimit(0.30f, 1.0f, 0.40f + energySum * 5.0f));
            return;
        }

        HeardNote hn;
        hn.step = step;
        hn.midi = midi;
        hn.lengthSteps = juce::jmax(1, endStep - step);
        hn.confidence = confSum / (float) juce::jmax(1, count);
        hn.velocity = juce::jlimit(0.30f, 1.0f, 0.40f + energySum * 5.0f / (float) juce::jmax(1, count));
        notes.push_back(hn);
    };

    int runStart = -1;
    int runMidi = -1;
    int holdGap = 0;
    int pendingChange = 0;
    int pendingMidi = -1;
    for (int i = 0; i < (int) frames.size(); ++i)
    {
        const int midi = frames[(size_t) i].midi;
        const bool voiced = midi >= 0 && frames[(size_t) i].conf >= 0.10f;

        if (! voiced)
        {
            // Bridge several hops of dropout inside a sustained note (~80–100ms).
            if (runStart >= 0 && holdGap < 6)
            {
                ++holdGap;
                continue;
            }
            if (runStart >= 0)
            {
                flush(runStart, i - holdGap, runMidi);
                runStart = -1;
                runMidi = -1;
            }
            holdGap = 0;
            pendingChange = 0;
            pendingMidi = -1;
            continue;
        }

        holdGap = 0;
        if (runStart < 0)
        {
            runStart = i;
            runMidi = midi;
            pendingChange = 0;
            pendingMidi = -1;
        }
        else if (midi == runMidi)
        {
            pendingChange = 0;
            pendingMidi = -1;
        }
        else
        {
            // Require 2 consecutive frames on the new pitch before splitting.
            if (pendingMidi == midi)
                ++pendingChange;
            else
            {
                pendingMidi = midi;
                pendingChange = 1;
            }
            if (pendingChange >= 2)
            {
                flush(runStart, i - pendingChange + 1, runMidi);
                runStart = i - pendingChange + 1;
                runMidi = midi;
                pendingChange = 0;
                pendingMidi = -1;
            }
        }
    }
    if (runStart >= 0)
        flush(runStart, (int) frames.size(), runMidi);

    // Final pass: collapse leftover same-pitch neighbors.
    if (notes.size() >= 2)
    {
        std::vector<HeardNote> merged;
        merged.push_back(notes.front());
        for (size_t i = 1; i < notes.size(); ++i)
        {
            auto& prev = merged.back();
            const auto& cur = notes[i];
            if (std::abs(prev.midi - cur.midi) <= 1
                && cur.step <= prev.step + prev.lengthSteps + 3)
            {
                const int newEnd = juce::jmax(prev.step + prev.lengthSteps, cur.step + cur.lengthSteps);
                prev.lengthSteps = juce::jmax(1, newEnd - prev.step);
                prev.confidence = juce::jmax(prev.confidence, cur.confidence);
                prev.velocity = juce::jmax(prev.velocity, cur.velocity);
            }
            else
            {
                merged.push_back(cur);
            }
        }
        notes.swap(merged);
    }

    // Only drop obvious garbage: tiny + very low confidence.
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [](const HeardNote& h)
                               {
                                   return h.confidence < 0.10f
                                       || (h.lengthSteps <= 1 && h.confidence < 0.18f);
                               }),
                notes.end());
    return notes;
}

void analyzeBarPitch(BarObservation& bar, const float* audio, int start, int use, double sr)
{
    bar.chroma.fill(0.0f);
    bar.heardPcs.clear();
    bar.confidence = 0.0f;
    if (audio == nullptr || use < 256)
        return;

    std::array<float, 12> pcHist {};
    float confSum = 0.0f;
    int voiced = 0;

    // Hop through the bar with ~40ms frames.
    const int frame = juce::jlimit(256, 2048, (int) std::round(sr * 0.04));
    const int hop = juce::jmax(64, frame / 2);
    for (int i = 0; i + frame <= use; i += hop)
    {
        float conf = 0.0f;
        const float hz = yinPitchHz(audio + start + i, frame, sr, conf);
        const int pc = hzToPc(hz);
        if (pc < 0)
            continue;
        pcHist[(size_t) pc] += conf;
        confSum += conf;
        ++voiced;
    }

    // Also fold a few longer windows for sustained notes / chords.
    const int longFrame = juce::jmin(use, (int) std::round(sr * 0.12));
    if (longFrame >= 256)
    {
        for (int i = 0; i + longFrame <= use; i += longFrame)
        {
            float conf = 0.0f;
            const float hz = yinPitchHz(audio + start + i, longFrame, sr, conf);
            const int pc = hzToPc(hz);
            if (pc < 0)
                continue;
            pcHist[(size_t) pc] += conf * 1.5f;
            confSum += conf;
            ++voiced;
        }
    }

    float histSum = 0.0f;
    for (float v : pcHist) histSum += v;
    if (histSum > 1.0e-6f)
    {
        for (int i = 0; i < 12; ++i)
            bar.chroma[(size_t) i] = pcHist[(size_t) i] / histSum;
    }

    // Rank pitch classes.
    std::vector<std::pair<float, int>> ranked;
    for (int i = 0; i < 12; ++i)
        if (pcHist[(size_t) i] > 0.0f)
            ranked.push_back({ pcHist[(size_t) i], i });
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (int i = 0; i < (int) ranked.size() && i < 4; ++i)
        bar.heardPcs.push_back(ranked[(size_t) i].second);

    bar.confidence = voiced > 0 ? juce::jlimit(0.0f, 1.0f, confSum / (float) voiced) : 0.0f;

    if (! bar.heardPcs.empty())
    {
        // Root = pitch class with the most voiced energy (most played).
        bar.rootPc = bar.heardPcs.front();
        const int m3 = (bar.rootPc + 3) % 12;
        const int M3 = (bar.rootPc + 4) % 12;
        const int p5 = (bar.rootPc + 7) % 12;
        // Score major vs minor triads using the established root.
        const float majScore = pcHist[(size_t) bar.rootPc]
                             + 0.9f * pcHist[(size_t) M3]
                             + 0.7f * pcHist[(size_t) p5];
        const float minScore = pcHist[(size_t) bar.rootPc]
                             + 0.9f * pcHist[(size_t) m3]
                             + 0.7f * pcHist[(size_t) p5];
        bar.minor = minScore > majScore;
        bar.harmonyLabel = pcName(bar.rootPc) + (bar.minor ? "m" : "");
    }
    else
    {
        estimateHarmonyFromChroma(bar);
        bar.confidence *= 0.35f;
    }
}
}

void ListenAnalyzer::prepare(double sampleRate, int maxBlockSize)
{
    sr = juce::jmax(8000.0, sampleRate);
    maxBlock = juce::jmax(64, maxBlockSize);
}

void ListenAnalyzer::reset()
{
    armed.store(0);
    waitingForNote.store(0);
    complete.store(0);
    samplesCaptured.store(0);
    triggerKind.store(0);
    samplesNeeded = 0;
    const juce::ScopedLock sl(lock);
    capture.clear();
    chromaAccum.fill(0.0f);
    onsetSamples.clear();
    onsetCountAtomic.store(0);
    prevEnergy = 0.0f;
    energyEnv = 0.0f;
    waitEnergyEnv = 0.0f;
    waitAmbientRms = 0.0f;
    noteGateOpen = false;
    noteGateQuietHops = 0;
    lastOnsetSample = -1000000;
}

void ListenAnalyzer::fireTrigger(int kind) noexcept
{
    triggerKind.store(kind);
    triggerSerial.fetch_add(1, std::memory_order_relaxed);
}

void ListenAnalyzer::arm(int numBars, double bpmIn, int spb, int meterQuarters)
{
    {
        const juce::ScopedLock sl(lock);
        capture.clear();
        chromaAccum.fill(0.0f);
        onsetSamples.clear();
    }
    onsetCountAtomic.store(0);
    prevEnergy = 0.0f;
    energyEnv = 0.0f;
    waitEnergyEnv = 0.0f;
    waitAmbientRms = 0.0f;
    noteGateOpen = false;
    noteGateQuietHops = 0;
    lastOnsetSample = -1000000;
    complete.store(0);
    samplesCaptured.store(0);
    triggerKind.store(0);

    bars = juce::jlimit(1, 16, numBars);
    bpm = juce::jlimit(40.0, 260.0, bpmIn);
    stepsPerBar = juce::jmax(1, spb);
    quartersPerBar = juce::jmax(1, meterQuarters);
    const double seconds = (double) bars * (double) quartersPerBar * 60.0 / bpm;
    samplesNeeded = juce::jmax(maxBlock, (int) std::ceil(seconds * sr));
    {
        const juce::ScopedLock sl(lock);
        capture.clear();
        capture.reserve((size_t) samplesNeeded + (size_t) maxBlock);
    }
    waitingForNote.store(1);
    armed.store(1);
}

float ListenAnalyzer::progress() const noexcept
{
    if (waitingForNote.load() != 0) return 0.0f;
    if (samplesNeeded <= 0) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (float) samplesCaptured.load() / (float) samplesNeeded);
}

void ListenAnalyzer::setStartThreshold(float peak01) noexcept
{
    startThreshold.store(juce::jlimit(0.003f, 0.60f, peak01));
}

bool ListenAnalyzer::blockLooksLikeNote(const float* mono, int numSamples) noexcept
{
    if (mono == nullptr || numSamples <= 0)
        return false;

    float peak = 0.0f;
    float sumSq = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = mono[i];
        peak = juce::jmax(peak, std::abs(s));
        sumSq += s * s;
    }
    const float rms = std::sqrt(sumSq / (float) numSamples);
    const float thresh = juce::jmax(0.003f, startThreshold.load());

    // Learn ambient only while BELOW the user threshold — so raising the
    // slider actually ignores room noise instead of adapting up into it.
    if (peak < thresh)
    {
        waitAmbientRms = waitAmbientRms * 0.97f + rms * 0.03f;
        waitEnergyEnv = waitEnergyEnv * 0.95f + rms * 0.05f;
        return false;
    }

    waitEnergyEnv = waitEnergyEnv * 0.85f + rms * 0.15f;

    // Must clearly exceed both the slider and the learned ambient floor.
    const float ambient = juce::jmax(waitAmbientRms, thresh * 0.35f);
    const bool aboveThresh = peak >= thresh && rms >= thresh * 0.35f;
    const bool aboveAmbient = rms >= ambient * 1.8f + thresh * 0.15f;
    const bool attack = rms > waitEnergyEnv * 1.12f + thresh * 0.08f;
    return aboveThresh && aboveAmbient && attack;
}

void ListenAnalyzer::pushBlock(const float* mono, int numSamples)
{
    if (armed.load() == 0 || mono == nullptr || numSamples <= 0)
        return;
    if (complete.load() != 0)
        return;

    if (waitingForNote.load() != 0)
    {
        if (! blockLooksLikeNote(mono, numSamples))
            return;
        waitingForNote.store(0);
        waitEnergyEnv = 0.0f;
        waitAmbientRms = 0.0f;
        fireTrigger(1); // capture start — first note heard
        // Fall through and start capturing from this first note block.
    }

    const int already = samplesCaptured.load();
    const int room = samplesNeeded - already;
    if (room <= 0)
    {
        armed.store(0);
        complete.store(1);
        return;
    }

    const int take = juce::jmin(numSamples, room);
    if (! lock.tryEnter())
        return;
    const size_t start = capture.size();
    capture.resize(start + (size_t) take);
    juce::FloatVectorOperations::copy(capture.data() + (int) start, mono, take);
    detectOnsets(mono, take, already);
    lock.exit();

    samplesCaptured.store(already + take);
    if (already + take >= samplesNeeded)
    {
        armed.store(0);
        complete.store(1);
    }
}

void ListenAnalyzer::accumulateChroma(const float* /*x*/, int /*n*/)
{
    // Pitch analysis is done offline in takeObservation via YIN.
}

void ListenAnalyzer::detectOnsets(const float* x, int n, int globalOffset)
{
    constexpr int hop = 64;
    const float thresh = juce::jmax(0.003f, startThreshold.load());
    const float openFloor = thresh;           // must rise through START THRESH
    const float closeFloor = thresh * 0.55f;  // must fall clearly below before next note
    // Refractory: ignore re-triggers for ~120ms after an onset.
    const int minGapSamples = (int) std::round(0.12 * sr);

    for (int i = 0; i + hop <= n; i += hop)
    {
        float e = 0.0f;
        for (int k = 0; k < hop; ++k)
            e += x[i + k] * x[i + k];
        e = std::sqrt(e / (float) hop);
        energyEnv = energyEnv * 0.92f + e * 0.08f;
        const float delta = e - prevEnergy;
        prevEnergy = e;

        const int at = globalOffset + i;

        if (noteGateOpen)
        {
            // Stay latched while the note rings; only reopen after ~50ms below close floor.
            const int quietNeeded = juce::jmax(8, (int) std::round(0.050 * sr / (double) hop));
            if (e < closeFloor)
            {
                ++noteGateQuietHops;
                if (noteGateQuietHops >= quietNeeded)
                    noteGateOpen = false;
            }
            else
            {
                noteGateQuietHops = 0;
            }
            continue;
        }

        // Gate closed — look for a fresh attack above threshold.
        noteGateQuietHops = 0;
        if (e < openFloor)
            continue;
        if (delta < juce::jmax(0.006f, thresh * 0.12f))
            continue;
        if (e < energyEnv * 1.15f && delta < thresh * 0.25f)
            continue;
        if (at - lastOnsetSample < minGapSamples)
            continue;

        noteGateOpen = true;
        noteGateQuietHops = 0;
        lastOnsetSample = at;
        onsetSamples.push_back(at);
        onsetCountAtomic.store((int) onsetSamples.size());
        fireTrigger(2);
    }
}

MusicalObservation ListenAnalyzer::takeObservation()
{
    MusicalObservation obs;
    obs.bpm = bpm;
    obs.bars = bars;
    obs.stepsPerBar = stepsPerBar;

    std::vector<float> audio;
    std::vector<int> onsets;
    {
        const juce::ScopedLock sl(lock);
        audio.swap(capture);
        onsets.swap(onsetSamples);
        chromaAccum.fill(0.0f);
        complete.store(0);
    }

    if (audio.empty() || samplesNeeded <= 0)
        return obs;

    float peak = 0.0f;
    for (float s : audio)
        peak = juce::jmax(peak, std::abs(s));
    obs.capturePeak = peak;
    if (peak < 0.0008f)
        return obs;

    normalizeAudio(audio);

    const double samplesPerBar = (double) samplesNeeded / (double) bars;
    const double samplesPerQuarter = samplesPerBar / (double) quartersPerBar;

    obs.barsObserved.resize((size_t) bars);
    for (int b = 0; b < bars; ++b)
    {
        auto& bar = obs.barsObserved[(size_t) b];
        bar.barIndex = b;

        const int start = (int) std::floor(b * samplesPerBar);
        const int end = (int) std::floor((b + 1) * samplesPerBar);
        const int len = juce::jmax(1, end - start);
        if (start >= (int) audio.size())
            continue;
        const int use = juce::jmin(len, (int) audio.size() - start);

        analyzeBarPitch(bar, audio.data(), start, use, sr);

        // Energy accents on 8th-note grid.
        const int bins = juce::jmax(4, quartersPerBar * 2);
        const int binLen = juce::jmax(1, use / bins);
        std::vector<float> binE((size_t) bins, 0.0f);
        for (int bin = 0; bin < bins; ++bin)
        {
            const int a = bin * binLen;
            const int bEnd = juce::jmin(use, a + binLen);
            float e = 0.0f;
            for (int i = a; i < bEnd; ++i)
                e += audio[(size_t) (start + i)] * audio[(size_t) (start + i)];
            binE[(size_t) bin] = std::sqrt(e / (float) juce::jmax(1, bEnd - a));
        }
        float mean = 0.0f;
        for (float e : binE) mean += e;
        mean /= (float) bins;
        const float thresh = juce::jmax(0.01f, mean * 1.08f);
        for (int bin = 0; bin < bins; ++bin)
        {
            if (binE[(size_t) bin] < thresh)
                continue;
            const float beat = (float) bin * ((float) quartersPerBar / (float) bins);
            bar.accentBeats.push_back(juce::jlimit(0.0f, (float) quartersPerBar, beat));
        }

        for (int os : onsets)
        {
            if (os < start || os >= end) continue;
            const float beat = (float) ((os - start) / samplesPerQuarter);
            const float clamped = juce::jlimit(0.0f, (float) quartersPerBar, beat);
            bool dup = false;
            for (float existing : bar.accentBeats)
                if (std::abs(existing - clamped) < 0.12f) { dup = true; break; }
            if (! dup)
                bar.accentBeats.push_back(clamped);
        }
        std::sort(bar.accentBeats.begin(), bar.accentBeats.end());
        if (bar.accentBeats.empty())
            bar.accentBeats.push_back(0.0f);
        bar.density = (float) bar.accentBeats.size() / (float) juce::jmax(1, quartersPerBar);
    }

    float dens = 0.0f;
    float conf = 0.0f;
    juce::String prog;
    std::array<float, 12> globalPc {};
    for (int i = 0; i < (int) obs.barsObserved.size(); ++i)
    {
        dens += obs.barsObserved[(size_t) i].density;
        conf += obs.barsObserved[(size_t) i].confidence;
        const auto& bar = obs.barsObserved[(size_t) i];
        const float w = juce::jmax(0.05f, bar.confidence);
        for (int pc = 0; pc < 12; ++pc)
            globalPc[(size_t) pc] += bar.chroma[(size_t) pc] * w;

        if (i > 0) prog += " -> ";
        prog += bar.harmonyLabel;
        if (! bar.heardPcs.empty())
        {
            prog += "[";
            for (int k = 0; k < (int) bar.heardPcs.size() && k < 3; ++k)
            {
                if (k) prog += " ";
                prog += pcName(bar.heardPcs[(size_t) k]);
            }
            prog += "]";
        }
    }

    // Global key: most-heard pitch class is the root; then major vs minor triad score.
    float bestPc = -1.0f;
    int root = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (globalPc[(size_t) pc] > bestPc)
        {
            bestPc = globalPc[(size_t) pc];
            root = pc;
        }
    }
    if (bestPc > 1.0e-6f)
    {
        obs.hasKeyDetect = true;
        obs.detectedKeyRoot = root;
        const float majScore = globalPc[(size_t) root]
                             + 0.9f * globalPc[(size_t) ((root + 4) % 12)]
                             + 0.7f * globalPc[(size_t) ((root + 7) % 12)];
        const float minScore = globalPc[(size_t) root]
                             + 0.9f * globalPc[(size_t) ((root + 3) % 12)]
                             + 0.7f * globalPc[(size_t) ((root + 7) % 12)];
        obs.detectedMinor = minScore > majScore;
    }

    // Continuous pitch track is primary (hears sustained notes).
    // Onset notes fill any attacks the stream missed — never invent extras.
    const int totalSteps = juce::jmax(1, bars * stepsPerBar);
    const double samplesPerStep = (double) samplesNeeded / (double) totalSteps;
    auto stream = transcribeNotes(audio.data(), (int) audio.size(), sr, totalSteps, samplesPerStep);
    auto onsetNotes = notesFromOnsets(audio.data(), (int) audio.size(), sr, onsets,
                                      totalSteps, samplesPerStep);

    obs.transcribed = std::move(stream);
    for (const auto& on : onsetNotes)
    {
        bool covered = false;
        for (const auto& existing : obs.transcribed)
        {
            if (std::abs(existing.step - on.step) <= 2
                && std::abs(existing.midi - on.midi) <= 1)
            {
                covered = true;
                break;
            }
        }
        if (covered)
            continue;
        // Insert keeping step order.
        auto it = obs.transcribed.begin();
        while (it != obs.transcribed.end() && it->step <= on.step)
            ++it;
        obs.transcribed.insert(it, on);
    }

    // Prefer transcribed MIDI histogram for global key (exact notes, not just chroma).
    if (! obs.transcribed.empty())
    {
        std::array<float, 12> notePc {};
        for (const auto& hn : obs.transcribed)
            notePc[(size_t) (((hn.midi % 12) + 12) % 12)]
                += juce::jmax(0.2f, hn.confidence) * (float) juce::jmax(1, hn.lengthSteps);
        float bestPc = -1.0f;
        int root = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (notePc[(size_t) pc] > bestPc)
            {
                bestPc = notePc[(size_t) pc];
                root = pc;
            }
        if (bestPc > 1.0e-6f)
        {
            obs.hasKeyDetect = true;
            obs.detectedKeyRoot = root;
            const float majScore = notePc[(size_t) root]
                                 + 0.9f * notePc[(size_t) ((root + 4) % 12)]
                                 + 0.7f * notePc[(size_t) ((root + 7) % 12)];
            const float minScore = notePc[(size_t) root]
                                 + 0.9f * notePc[(size_t) ((root + 3) % 12)]
                                 + 0.7f * notePc[(size_t) ((root + 7) % 12)];
            obs.detectedMinor = minScore > majScore;
        }
    }

    obs.rhythmicActivity = juce::jlimit(0.0f, 1.0f, dens / (float) juce::jmax(1, bars) / 3.0f);
    const float avgConf = conf / (float) juce::jmax(1, bars);
    const char* act = obs.rhythmicActivity < 0.33f ? "sparse"
                     : obs.rhythmicActivity < 0.66f ? "medium" : "busy";
    juce::String keyBit;
    if (obs.hasKeyDetect)
        keyBit = "Key: " + pcName(obs.detectedKeyRoot) + (obs.detectedMinor ? "m" : "")
               + " (most-heard)  |  ";
    juce::String learnBit;
    if (! obs.transcribed.empty())
        learnBit = "LEARNED " + juce::String((int) obs.transcribed.size()) + " notes  |  ";
    obs.summary = keyBit + learnBit + "Heard: " + prog + "  |  " + juce::String(bars) + " bars | "
                + juce::String((int) std::round(bpm)) + " BPM | " + act
                + " | conf " + juce::String(avgConf, 2);
    return obs;
}
}
