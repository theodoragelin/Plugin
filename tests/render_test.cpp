// Offline test for the Selene reverb engine (no JUCE needed).
//  - checks the output is finite and bounded for every preset-like setting
//  - estimates the RT60 of the tail and verifies Decay changes it
//  - checks inverted decay actually swells (energy peaks late)
//  - writes impulse responses + a drum-ish render to WAV for listening
//
// Build: g++ -O2 -std=c++17 -I../Source render_test.cpp -o render_test

#include "dsp/MoonReverb.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__)
 #include <xmmintrin.h>
#endif

using selene::MoonReverb;
using selene::ReverbParams;

static constexpr double kFs = 48000.0;

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, rate = (uint32_t) kFs;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 (rate); w32 (rate * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : { l[i], r[i] })
        {
            const float c = std::clamp (s, -1.0f, 1.0f);
            w16 ((uint16_t) (int16_t) (c * 32767.0f));
        }
    std::fclose (f);
}

struct Result
{
    std::vector<float> l, r;
    bool finite = true;
    float peak = 0.0f;
};

static Result render (const ReverbParams& p, const std::vector<float>& inL, const std::vector<float>& inR)
{
    MoonReverb rev;
    rev.prepare (kFs, 512);
    rev.setParams (p);

    Result res;
    res.l.resize (inL.size());
    res.r.resize (inL.size());
    const int block = 512;
    for (size_t pos = 0; pos < inL.size(); pos += block)
    {
        const int n = (int) std::min<size_t> (block, inL.size() - pos);
        rev.setParams (p);
        rev.process (inL.data() + pos, inR.data() + pos, res.l.data() + pos, res.r.data() + pos, n);
    }
    for (size_t i = 0; i < res.l.size(); ++i)
    {
        if (! std::isfinite (res.l[i]) || ! std::isfinite (res.r[i])) res.finite = false;
        res.peak = std::max ({ res.peak, std::abs (res.l[i]), std::abs (res.r[i]) });
    }
    return res;
}

// Energy envelope in 10 ms windows (dB).
static std::vector<float> envelopeDb (const Result& r)
{
    const size_t win = (size_t) (0.01 * kFs);
    std::vector<float> env;
    for (size_t i = 0; i + win <= r.l.size(); i += win)
    {
        double e = 0;
        for (size_t j = i; j < i + win; ++j) e += r.l[j] * r.l[j] + r.r[j] * r.r[j];
        env.push_back ((float) (10.0 * std::log10 (e / (double) win + 1e-20)));
    }
    return env;
}

// RT60 estimate from the late-tail slope between -10 dB and -30 dB below peak (T20 * 3),
// skipping the early-reflection region so it measures the reverb tail itself.
static float estimateRT60 (const Result& r)
{
    auto env = envelopeDb (r);
    size_t peakIdx = 12; // start after the first 120 ms (early reflections)
    for (size_t i = 12; i < env.size(); ++i) if (env[i] > env[peakIdx]) peakIdx = i;
    const float pk = env[peakIdx];
    int a = -1, b = -1;
    for (size_t i = peakIdx; i < env.size(); ++i)
    {
        if (a < 0 && env[i] < pk - 10.0f) a = (int) i;
        if (b < 0 && env[i] < pk - 30.0f) { b = (int) i; break; }
    }
    if (a < 0 || b < 0) return -1.0f;
    return (float) ((b - a) * 0.01 * 3.0);
}

static float peakTimeSec (const Result& r)
{
    auto env = envelopeDb (r);
    size_t peakIdx = 0;
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > env[peakIdx]) peakIdx = i;
    return (float) peakIdx * 0.01f;
}

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(__x86_64__)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, like a DAW would
#endif
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;
    auto check = [&] (bool ok, const std::string& what) {
        std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
        if (! ok) ++failures;
    };

    const size_t len = (size_t) (kFs * 12.0);
    std::vector<float> impL (len, 0.0f), impR (len, 0.0f);
    impL[100] = impR[100] = 0.8f;

    // ---- Decay sweep
    std::printf ("Decay sweep (RT60 estimates):\n");
    float prevRT = 0.0f;
    for (float d : { 0.1f, 0.3f, 0.5f, 0.7f })
    {
        ReverbParams p; p.decay = d; p.size = 0.5f; p.tape = 0.0f; p.drift = 0.2f;
        auto r = render (p, impL, impR);
        const float rt = estimateRT60 (r);
        const float expected = 0.25f * std::pow (120.0f, d);
        std::printf ("  decay %.1f -> RT60 %.2fs (target %.2fs) peak %.3f\n", d, rt, expected, r.peak);
        check (r.finite, "finite output");
        check (rt > prevRT, "RT60 increases with Decay");
        check (rt > expected * 0.5f && rt < expected * 1.6f, "RT60 near target");
        prevRT = rt;
        writeWav (outDir + "/ir_decay_" + std::to_string ((int) (d * 100)) + ".wav", r.l, r.r);
    }

    // ---- Inverted decay swells
    std::printf ("Inverted decay:\n");
    {
        ReverbParams p; p.decay = -0.6f; p.size = 0.5f;
        auto r = render (p, impL, impR);
        const float t = peakTimeSec (r);
        std::printf ("  energy peak at %.2fs\n", t);
        check (r.finite, "finite output");
        check (t > 0.6f, "energy peaks late (swell)");
        writeWav (outDir + "/ir_inverted.wav", r.l, r.r);
    }

    // ---- Stress: extreme settings, loud noise, every mod mode
    std::printf ("Stress test:\n");
    {
        std::vector<float> nL (len), nR (len);
        selene::Rng rng (5);
        for (size_t i = 0; i < len; ++i)
        {
            const float g = i < (size_t) (kFs * 4) ? 1.0f : 0.0f;
            nL[i] = rng.nextBipolar() * g;
            nR[i] = rng.nextBipolar() * g;
        }
        for (int mode = 0; mode < 3; ++mode)
            for (float d : { -1.0f, 1.0f })
            {
                ReverbParams p; p.size = 1.0f; p.decay = d; p.tape = 1.0f; p.drift = 1.0f; p.modMode = mode;
                p.width = 1.5f; p.preDelayMs = 500.0f;
                auto r = render (p, nL, nR);
                std::printf ("  mode %d decay %+.0f: peak %.2f\n", mode, d, r.peak);
                check (r.finite && r.peak < 8.0f, "finite and bounded");
            }
    }

    // ---- Loudness: wet RMS vs dry RMS on sustained noise
    std::printf ("Loudness:\n");
    {
        std::vector<float> nL (len), nR (len);
        selene::Rng rng (9);
        double dry = 0;
        for (size_t i = 0; i < len; ++i) { nL[i] = rng.nextBipolar() * 0.3f; nR[i] = rng.nextBipolar() * 0.3f; dry += nL[i] * nL[i]; }
        ReverbParams p;
        auto r = render (p, nL, nR);
        double wet = 0;
        for (size_t i = len / 2; i < len; ++i) wet += r.l[i] * r.l[i];
        const double ratioDb = 10.0 * std::log10 ((wet / (len / 2)) / (dry / len));
        std::printf ("  wet/dry level %.1f dB\n", ratioDb);
        check (ratioDb > -4.0 && ratioDb < 3.0, "wet level roughly matched to dry");
    }

    // ---- Musical demo: a few percussive hits through a few characters
    {
        std::vector<float> dL (len, 0.0f), dR (len, 0.0f);
        selene::Rng rng (3);
        for (int hit = 0; hit < 4; ++hit)
        {
            const size_t start = (size_t) (kFs * (0.2 + hit * 0.6));
            for (size_t i = 0; i < 6000; ++i)
            {
                const float env = std::exp (-(float) i / 900.0f);
                const float s = (std::sin (2.0f * selene::kPi * 180.0f * i / (float) kFs) * 0.7f + rng.nextBipolar() * 0.3f) * env * 0.6f;
                dL[start + i] += s; dR[start + i] += s;
            }
        }
        const struct { const char* name; ReverbParams p; } demos[] = {
            { "demo_lush",     { 0.7f, 0.6f, 0.25f, 0.4f, 20.0f, 120.0f, 14000.0f, 1.2f, 0 } },
            { "demo_tape",     { 0.5f, 0.5f, 0.9f, 0.6f, 0.0f, 150.0f, 9000.0f, 1.0f, 2 } },
            { "demo_inverted", { 0.6f, -0.5f, 0.3f, 0.3f, 0.0f, 80.0f, 16000.0f, 1.0f, 1 } },
        };
        for (auto& demo : demos)
        {
            auto r = render (demo.p, dL, dR);
            std::vector<float> mixL (len), mixR (len);
            for (size_t i = 0; i < len; ++i) { mixL[i] = dL[i] * 0.7f + r.l[i] * 0.5f; mixR[i] = dR[i] * 0.7f + r.r[i] * 0.5f; }
            writeWav (outDir + "/" + demo.name + ".wav", mixL, mixR);
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
