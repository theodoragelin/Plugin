#pragma once

#include <array>

// Factory presets. Values are in real (denormalised) parameter units.
struct Preset
{
    const char* name;
    float size, decay, tape, drift, mix, preDelay, lowCut, highCut, width, output;
    int modMode; // 0 Lunar, 1 Drift, 2 Warble
};

inline constexpr std::array<Preset, 12> kFactoryPresets {{
    //  name                 size   decay  tape   drift  mix   preDly lowCut  highCut  width output mod
    { "Initialize",          0.50f, 0.45f, 0.20f, 0.30f, 0.35f,  0.0f,  20.0f, 20000.0f, 1.00f, 0.0f, 0 },
    { "Moonrise Vocal",      0.55f, 0.50f, 0.25f, 0.25f, 0.22f, 35.0f, 180.0f, 11000.0f, 1.10f, 0.0f, 0 },
    { "Lunar Pad",           0.85f, 0.80f, 0.30f, 0.55f, 0.55f, 10.0f, 120.0f, 14000.0f, 1.30f, 0.0f, 0 },
    { "Tape Haze",           0.60f, 0.60f, 0.85f, 0.60f, 0.45f,  0.0f, 150.0f,  8000.0f, 1.00f, 0.0f, 2 },
    { "Inverted Tide",       0.65f,-0.55f, 0.30f, 0.30f, 0.50f,  0.0f, 100.0f, 15000.0f, 1.10f, 0.0f, 1 },
    { "Crater Drums",        0.30f, 0.30f, 0.40f, 0.10f, 0.25f,  5.0f, 200.0f, 10000.0f, 0.90f, 0.0f, 0 },
    { "Endless Night",       1.00f, 0.97f, 0.35f, 0.45f, 0.60f,  0.0f, 150.0f, 12000.0f, 1.40f,-2.0f, 0 },
    { "Silver Room",         0.20f, 0.25f, 0.10f, 0.15f, 0.30f,  8.0f, 150.0f, 16000.0f, 1.00f, 0.0f, 0 },
    { "Dream Guitar",        0.75f, 0.70f, 0.45f, 0.70f, 0.40f, 25.0f, 200.0f, 10000.0f, 1.25f, 0.0f, 1 },
    { "Broken Cassette",     0.50f, 0.55f, 1.00f, 0.95f, 0.50f,  0.0f, 250.0f,  6000.0f, 0.80f, 0.0f, 2 },
    { "Swell Keys",          0.55f,-0.35f, 0.20f, 0.35f, 0.40f,  0.0f, 120.0f, 14000.0f, 1.10f, 0.0f, 0 },
    { "Far Side Strings",    0.90f, 0.75f, 0.15f, 0.40f, 0.45f, 60.0f, 250.0f, 13000.0f, 1.35f, 0.0f, 1 },
}};
