#pragma once

// Selene reverb engine — framework-free so it can be unit-tested without JUCE.
//
// Signal flow (per channel pair):
//   input -> pre-delay -+-> early reflections (size-scaled multi-tap) ----------+
//                       +-> input diffusion (6 allpasses / channel)               |
//         -> [optional "inverted" swell: rising multi-tap delay]                  |
//         -> 8-line modulated feedback delay network (Hadamard mix, in-loop       |
//            allpass per line for density, two-band RT60-matched decay with       |
//            shorter bass decay, tape saturation + damping)                       |
//         -> stereo taps (+ ER) -> lo-fi (high Drift) -> width -> low/high cut ->-+-> loudness-normalised wet out
//
// Macro controls:
//   size  0..1   scales every delay line (room scale)
//   decay -1..1  |decay| = tail length; negative values invert the tail into
//                a reverse-style swell that builds up instead of dying away
//   tape  0..1   loop saturation + high-frequency roll-off (warmth)
//   drift 0..1   depth of delay-time modulation (pitch drift, chorusing, lo-fi)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace selene
{

constexpr float kPi = 3.14159265358979f;

//==============================================================================
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int n = 1;
        while (n < maxDelaySamples + 8)
            n <<= 1;
        buffer.assign ((size_t) n, 0.0f);
        mask = n - 1;
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // Sample written `delay` samples ago (call before push for the current sample).
    float readInt (int delay) const { return buffer[(size_t) ((writePos - delay) & mask)]; }

    // Fractional read with 4-point Hermite interpolation. delay must be >= 3.
    float readFrac (float delay) const
    {
        const float pos = (float) writePos - delay;
        const float fl = std::floor (pos);
        const int i = (int) fl;
        const float f = pos - fl;

        const float xm1 = buffer[(size_t) ((i - 1) & mask)];
        const float x0  = buffer[(size_t) (i & mask)];
        const float x1  = buffer[(size_t) ((i + 1) & mask)];
        const float x2  = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    int capacity() const { return mask + 1; }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

//==============================================================================
class Allpass
{
public:
    void prepare (int maxDelay) { line.prepare (maxDelay + 4); }
    void clear() { line.clear(); }

    float process (float x, float delaySamples, float g)
    {
        const float d = line.readFrac (std::max (3.0f, delaySamples));
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

private:
    DelayLine line;
};

//==============================================================================
class Biquad
{
public:
    void setLowpass (double fs, double fc, double q = 0.7071) { design (fs, fc, q, false); }
    void setHighpass (double fs, double fc, double q = 0.7071) { design (fs, fc, q, true); }
    void clear() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double fs, double fc, double q, bool highpass)
    {
        fc = std::clamp (fc, 10.0, 0.49 * fs);
        const double w0 = 2.0 * 3.141592653589793 * fc / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        double nb0, nb1, nb2;
        if (highpass) { nb0 = (1.0 + cw) * 0.5; nb1 = -(1.0 + cw); nb2 = nb0; }
        else          { nb0 = (1.0 - cw) * 0.5; nb1 = 1.0 - cw;    nb2 = nb0; }
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - alpha) / a0);
    }

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

//==============================================================================
// Small deterministic RNG so the reverb sounds identical on every machine.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 1) : s (seed * 2654435761u + 1u) {}
    float next01()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 16777216.0f);
    }
    float nextBipolar() { return next01() * 2.0f - 1.0f; }
};

//==============================================================================
// One modulation source per delay line. Three characters:
//   0 Lunar  - slow sines, smooth chorus shimmer
//   1 Drift  - smoothed random walk, unpredictable lo-fi pitch drift
//   2 Warble - shared tape wow + flutter, like a worn cassette
class Modulator
{
public:
    void prepare (double sampleRate, int index)
    {
        fs = (float) sampleRate;
        rng = Rng ((uint32_t) (index + 11) * 7919u);
        phase = rng.next01();
        sineRate = 0.11f + 0.09f * (float) index + 0.05f * rng.next01();
        randRate = 0.35f + 0.25f * rng.next01();
        current = target = rng.nextBipolar();
        start = current;
        randPhase = 0.0f;
    }

    // Returns a value in roughly [-1, 1].
    float next (int mode, float wowPhase, float flutterPhase)
    {
        phase += sineRate / fs;
        if (phase >= 1.0f) phase -= 1.0f;

        randPhase += randRate / fs;
        if (randPhase >= 1.0f)
        {
            randPhase -= 1.0f;
            start = target;
            target = rng.nextBipolar();
            randRate = 0.25f + 0.6f * rng.next01();
        }
        const float t = 0.5f - 0.5f * std::cos (kPi * randPhase);
        current = start + (target - start) * t;

        switch (mode)
        {
            case 1:  lastValue = current; break;
            case 2:  lastValue = 0.75f * std::sin (2.0f * kPi * wowPhase)
                               + 0.12f * std::sin (2.0f * kPi * flutterPhase + phase * 6.0f)
                               + 0.25f * current; break;
            default: lastValue = std::sin (2.0f * kPi * phase); break;
        }
        return lastValue;
    }

    float last() const { return lastValue; }

private:
    float fs = 48000.0f, phase = 0.0f, sineRate = 0.2f, lastValue = 0.0f;
    float randPhase = 0.0f, randRate = 0.5f, start = 0.0f, current = 0.0f, target = 0.0f;
    Rng rng;
};

//==============================================================================
struct ReverbParams
{
    float size = 0.5f;       // 0..1
    float decay = 0.45f;     // -1..1
    float tape = 0.2f;       // 0..1
    float drift = 0.3f;      // 0..1
    float preDelayMs = 0.0f; // 0..500
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    float width = 1.0f;      // 0..1.5
    int modMode = 0;         // 0 Lunar, 1 Drift, 2 Warble
};

//==============================================================================
class MoonReverb
{
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 6;
    static constexpr int kSwellTaps = 40;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;
        const float msToS = (float) fs * 0.001f;

        for (int i = 0; i < kLines; ++i)
        {
            const float maxMs = kLineMs[i] * kMaxSizeScale + kMaxModMs + 2.0f;
            lines[i].prepare ((int) (maxMs * msToS) + 8);
            mods[i].prepare (fs, i);
            loopAp[i].prepare ((int) (kLoopApMs[i] * std::sqrt (kMaxSizeScale) * msToS) + 8);
            damp[i] = bass[i] = 0.0f;
        }

        for (int c = 0; c < 2; ++c)
        {
            preDelay[c].prepare ((int) (0.52 * fs) + 8);
            erLine[c].prepare ((int) (kErMaxMs * 0.001 * fs * (0.4 + 0.6 * kMaxSizeScale)) + 8);
            holdValue[c] = 0.0f;
            swellLine[c].prepare ((int) (kMaxSwellSeconds * fs) + 8);
            for (int d = 0; d < kDiffusers; ++d)
                diffusers[c][d].prepare ((int) (kDiffMs[c][d] * kMaxSizeScale * msToS) + 8);
            lowCut[c].clear();
            highCut[c].clear();
            dcState[c] = dcPrev[c] = 0.0f;
        }

        // Smoothing coefficients (one-pole).
        bassCoeff = 1.0f - std::exp (-2.0f * kPi * 220.0f / (float) fs);
        slowCoeff = 1.0f - std::exp (-1.0f / (0.25f * (float) fs)); // ~250 ms
        fastCoeff = 1.0f - std::exp (-1.0f / (0.03f * (float) fs)); // ~30 ms

        lastLowCut = lastHighCut = -1.0f;
        swellLengthUsed = -1.0f;
        primed = false;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& a : loopAp) a.clear();
        for (int c = 0; c < 2; ++c)
        {
            preDelay[c].clear();
            erLine[c].clear();
            holdValue[c] = 0.0f;
            swellLine[c].clear();
            for (auto& d : diffusers[c]) d.clear();
            lowCut[c].clear();
            highCut[c].clear();
            dcState[c] = dcPrev[c] = 0.0f;
        }
        damp.fill (0.0f);
        bass.fill (0.0f);
        holdPhase = 1.0f;
    }

    void setParams (const ReverbParams& p)
    {
        params = p;
        if (! primed)
        {
            // Jump smoothers to their targets on the first block.
            sizeScaleS = sizeToScale (p.size);
            preDelayS = p.preDelayMs;
            invertS = p.decay < 0.0f ? 1.0f : 0.0f;
            rtS = targetRT60();
            tapeS = p.tape;
            driftS = p.drift;
            widthS = p.width;
            primed = true;
        }
        updateFilters();
        updateSwellTaps();
    }

    // Processes in place: wetL/wetR receive the 100% wet signal for the input inL/inR.
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples)
    {
        const float sizeTarget = sizeToScale (params.size);
        const float invertTarget = params.decay < 0.0f ? 1.0f : 0.0f;
        const float rtTarget = targetRT60();
        const float msToS = (float) fs * 0.001f;
        const float wowInc = 0.55f / (float) fs, flutterInc = 5.7f / (float) fs;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- parameter smoothing
            sizeScaleS += (sizeTarget - sizeScaleS) * slowCoeff;
            preDelayS  += (params.preDelayMs - preDelayS) * slowCoeff;
            invertS    += (invertTarget - invertS) * fastCoeff;
            rtS        += (rtTarget - rtS) * fastCoeff;
            tapeS      += (params.tape - tapeS) * fastCoeff;
            driftS     += (params.drift - driftS) * fastCoeff;
            widthS     += (params.width - widthS) * fastCoeff;

            wowPhase += wowInc;         if (wowPhase >= 1.0f) wowPhase -= 1.0f;
            flutterPhase += flutterInc; if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;

            // --- pre-delay
            float x[2] = { inL[n], inR[n] };
            const float pd = std::max (3.0f, preDelayS * msToS);
            for (int c = 0; c < 2; ++c)
            {
                preDelay[c].push (x[c]);
                x[c] = preDelayS < 0.05f ? x[c] : preDelay[c].readFrac (pd + 1.0f);
            }

            // --- early reflections: sparse, panned taps that give the room its shape
            const float erScale = 0.4f + 0.6f * sizeScaleS;
            float er[2] = { 0.0f, 0.0f };
            for (int c = 0; c < 2; ++c)
            {
                erLine[c].push (x[c]);
                for (int t = 0; t < kErTaps; ++t)
                    er[c] += erLine[c].readFrac (kErMs[c][t] * erScale * msToS + 3.0f) * kErGain[t];
            }

            // --- input diffusion
            // Diffusion is backed off for short decays so tight settings stay tight.
            const float diffScale = 0.35f + 0.65f * sizeScaleS;
            const float diffAmt = 0.45f + 0.55f * std::clamp ((rtS - 0.25f) / 1.5f, 0.0f, 1.0f);
            for (int c = 0; c < 2; ++c)
                for (int d = 0; d < kDiffusers; ++d)
                    x[c] = diffusers[c][d].process (x[c], kDiffMs[c][d] * diffScale * msToS, kDiffGain[d] * diffAmt);

            // --- inverted swell (reverse-style build-up)
            if (invertS > 0.001f)
            {
                for (int c = 0; c < 2; ++c)
                {
                    swellLine[c].push (x[c]);
                    float s = 0.0f;
                    for (int t = 0; t < kSwellTaps; ++t)
                        s += swellLine[c].readInt (swellPos[c][t]) * swellGain[c][t];
                    x[c] += (s - x[c]) * invertS;
                }
            }
            else
            {
                swellLine[0].push (x[0]);
                swellLine[1].push (x[1]);
            }

            // --- feedback delay network
            if ((n & 15) == 0)
                updateLoopGains (msToS);

            const float modDepth = (kBaseModMs + std::pow (driftS, 1.5f) * (kMaxModMs - kBaseModMs)) * msToS;
            const float dampCoeff = dampingCoeff (tapeS);
            const float drive = 1.0f + 3.0f * tapeS;
            const float invDrive = 1.0f / drive;

            std::array<float, kLines> y {};
            for (int i = 0; i < kLines; ++i)
            {
                const float m = mods[i].next (params.modMode, wowPhase, flutterPhase);
                const float delay = kLineMs[i] * sizeScaleS * msToS + modDepth * (1.0f + m) + 3.0f;
                y[i] = lines[i].readFrac (delay);
            }

            // Stereo output taps taken before the mixing matrix.
            float outL = (y[0] - y[2] + y[4] - y[6]) * 0.5f + (y[1] + y[5]) * 0.18f;
            float outR = (y[1] - y[3] + y[5] - y[7]) * 0.5f + (y[2] + y[6]) * 0.18f;

            hadamard8 (y);

            const float apScale = std::sqrt (sizeScaleS) * msToS;
            for (int i = 0; i < kLines; ++i)
            {
                // Tape: one-pole loop damping.
                damp[i] += (y[i] - damp[i]) * dampCoeff;

                // Two-band decay: lows die a little faster than mids so long tails stay clear.
                bass[i] += (damp[i] - bass[i]) * bassCoeff;
                float v = damp[i] * loopGain[i] + bass[i] * (loopGainBass[i] - loopGain[i]);

                // Tape: soft saturation inside the loop.
                v = std::tanh (v * drive) * invDrive;

                // In-loop allpass multiplies echo density without colouring the tone.
                v = loopAp[i].process (v, kLoopApMs[i] * apScale, (i & 1) ? 0.55f : -0.55f);

                const float inject = (i < kLines / 2 ? x[0] : x[1]) * 0.35f;
                lines[i].push (v + inject);
            }

            // Early reflections fade out as the space grows; inverted mode skips them.
            const float erAmt = (0.3f - 0.18f * params.size) * (1.0f - invertS);
            outL += er[0] * erAmt;
            outR += er[1] * erAmt;

            // Lo-fi: at high Drift the tail is resampled with a drifting clock.
            const float lofi = std::clamp ((driftS - 0.55f) / 0.45f, 0.0f, 1.0f);
            if (lofi > 0.001f)
            {
                holdPhase += 1.0f - 0.72f * lofi * (0.85f + 0.15f * mods[0].last());
                if (holdPhase >= 1.0f)
                {
                    holdPhase -= 1.0f;
                    holdValue[0] = outL;
                    holdValue[1] = outR;
                }
                const float blend = 0.6f * lofi;
                outL += (holdValue[0] - outL) * blend;
                outR += (holdValue[1] - outR) * blend;
            }

            // --- width, DC block, cut filters
            const float mid = 0.5f * (outL + outR);
            const float side = 0.5f * (outL - outR) * widthS;
            outL = mid + side;
            outR = mid - side;

            float o[2] = { outL, outR };
            for (int c = 0; c < 2; ++c)
            {
                const float dc = o[c] - dcPrev[c] + 0.9995f * dcState[c];
                dcPrev[c] = o[c];
                dcState[c] = dc;
                o[c] = highCut[c].process (lowCut[c].process (dc));
            }

            wetL[n] = safetyClip (o[0] * kWetGain * loudnessGain);
            wetR[n] = safetyClip (o[1] * kWetGain * loudnessGain);
        }
    }

    double getTailSeconds() const { return std::max (1.0, (double) rtS * 1.2 + kMaxSwellSeconds); }

private:
    // Transparent below 1.0, smoothly saturates above so huge builds stay tame.
    static float safetyClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 1.0f) return x;
        const float y = 1.0f + 0.5f * std::tanh (2.0f * (a - 1.0f));
        return x < 0.0f ? -y : y;
    }

    static float sizeToScale (float size) { return 0.45f + 2.05f * size * size + 0.25f * size; }

    // Per-line feedback gains for the target RT60 (mid band, and a shorter bass band),
    // plus a loudness normaliser so long/large settings don't jump out in level.
    void updateLoopGains (float msToS)
    {
        const float apScale = std::sqrt (sizeScaleS);
        float gSum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float lenS = (kLineMs[i] * sizeScaleS + kLoopApMs[i] * apScale) * msToS;
            const float g = std::pow (10.0f, -3.0f * lenS / (rtS * (float) fs));
            loopGain[i] = g;
            loopGainBass[i] = std::pow (g, 1.0f / kBassDecayRatio);
            gSum += g * g;
        }
        const float energy = 1.0f / std::max (1.0e-4f, 1.0f - gSum / (float) kLines);
        const float scaleComp = std::sqrt (sizeScaleS);
        loudnessGain = std::clamp (std::pow (energy, -0.5f * kLoudnessComp) * kLoudnessRef * std::pow (scaleComp, 0.5f), 0.2f, 4.0f);
    }

    // RT60 in seconds. Normal: 0.25 s .. ~30 s. Inverted: short smear behind the swell.
    float targetRT60() const
    {
        const float a = std::abs (params.decay);
        if (params.decay < 0.0f)
            return 0.35f + 0.9f * a;
        return 0.25f * std::pow (120.0f, a);
    }

    float dampingCoeff (float tape) const
    {
        const float fc = 17000.0f * std::pow (2600.0f / 17000.0f, tape);
        return 1.0f - std::exp (-2.0f * kPi * fc / (float) fs);
    }

    void updateFilters()
    {
        if (params.lowCutHz != lastLowCut)
        {
            for (auto& f : lowCut) f.setHighpass (fs, params.lowCutHz);
            lastLowCut = params.lowCutHz;
        }
        if (params.highCutHz != lastHighCut)
        {
            for (auto& f : highCut) f.setLowpass (fs, params.highCutHz);
            lastHighCut = params.highCutHz;
        }
    }

    // Tap layout for the inverted swell: taps spread over the window with
    // gains rising towards the end so the tail grows, then stops abruptly.
    void updateSwellTaps()
    {
        const float a = std::abs (params.decay);
        const float lengthSec = 0.15f + a * (kMaxSwellSeconds - 0.25f);
        if (swellLengthUsed > 0.0f && std::abs (lengthSec - swellLengthUsed) < 0.01f * swellLengthUsed)
            return;
        swellLengthUsed = lengthSec;

        const float lenSamples = lengthSec * (float) fs;
        for (int c = 0; c < 2; ++c)
        {
            Rng rng ((uint32_t) (c + 3) * 104729u);
            float energy = 0.0f;
            for (int t = 0; t < kSwellTaps; ++t)
            {
                const float frac = ((float) t + 0.2f + 0.6f * rng.next01()) / (float) kSwellTaps;
                swellPos[c][t] = std::max (1, (int) (frac * lenSamples));
                const float gain = frac * frac * (rng.next01() < 0.5f ? -1.0f : 1.0f);
                swellGain[c][t] = gain;
                energy += gain * gain;
            }
            const float norm = 1.6f / std::sqrt (std::max (1.0e-6f, energy));
            for (auto& g : swellGain[c]) g *= norm;
        }
    }

    static void hadamard8 (std::array<float, kLines>& v)
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[(size_t) j], b = v[(size_t) (j + h)];
                    v[(size_t) j] = a + b;
                    v[(size_t) (j + h)] = a - b;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (auto& s : v) s *= norm;
    }

    // Mutually prime-ish base lengths (ms) at size scale 1.
    static constexpr float kLineMs[kLines] = { 29.7f, 37.1f, 41.9f, 47.3f, 53.9f, 61.1f, 67.7f, 79.3f };
    static constexpr float kDiffMs[2][kDiffusers] = { { 4.71f, 3.59f, 12.73f, 9.31f, 23.3f, 17.9f }, { 4.97f, 3.37f, 13.19f, 8.83f, 22.1f, 18.7f } };
    static constexpr float kDiffGain[kDiffusers] = { 0.72f, 0.72f, 0.65f, 0.65f, 0.6f, 0.6f };
    static constexpr float kLoopApMs[kLines] = { 3.13f, 4.07f, 4.91f, 5.83f, 6.71f, 7.39f, 8.27f, 9.11f };
    static constexpr int kErTaps = 10;
    static constexpr float kErMaxMs = 48.0f;
    static constexpr float kErMs[2][kErTaps] = { { 4.3f, 7.9f, 11.2f, 15.7f, 19.1f, 23.9f, 29.3f, 34.1f, 40.3f, 47.2f },
                                                 { 5.1f, 8.8f, 12.9f, 14.6f, 21.4f, 26.2f, 27.9f, 36.6f, 42.8f, 45.9f } };
    static constexpr float kErGain[kErTaps] = { 0.62f, -0.5f, 0.45f, 0.38f, -0.33f, 0.28f, -0.24f, 0.2f, 0.16f, -0.13f };
    static constexpr float kBassDecayRatio = 0.8f;
    static constexpr float kLoudnessComp = 0.35f;
    static constexpr float kLoudnessRef = 1.0f;
    static constexpr float kMaxSizeScale = 2.75f;
    static constexpr float kBaseModMs = 0.12f;
    static constexpr float kMaxModMs = 3.5f;
    static constexpr float kMaxSwellSeconds = 2.6f;
    static constexpr float kWetGain = 2.2f;

    double fs = 48000.0;
    ReverbParams params;
    bool primed = false;

    std::array<DelayLine, kLines> lines;
    std::array<Modulator, kLines> mods;
    std::array<float, kLines> damp {}, bass {}, loopGain {}, loopGainBass {};
    std::array<Allpass, kLines> loopAp;
    DelayLine erLine[2];
    float holdValue[2] {}, holdPhase = 1.0f, bassCoeff = 0.03f, loudnessGain = 1.0f;

    DelayLine preDelay[2];
    DelayLine swellLine[2];
    Allpass diffusers[2][kDiffusers];
    Biquad lowCut[2], highCut[2];
    float dcState[2] {}, dcPrev[2] {};

    int swellPos[2][kSwellTaps] {};
    float swellGain[2][kSwellTaps] {};
    float swellLengthUsed = -1.0f;

    float lastLowCut = -1.0f, lastHighCut = -1.0f;
    float slowCoeff = 0.001f, fastCoeff = 0.01f;
    float sizeScaleS = 1.0f, preDelayS = 0.0f, invertS = 0.0f, rtS = 2.0f;
    float tapeS = 0.0f, driftS = 0.0f, widthS = 1.0f;
    float wowPhase = 0.0f, flutterPhase = 0.0f;
};

} // namespace selene
