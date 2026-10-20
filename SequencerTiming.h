// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <cmath>

namespace pulse
{
constexpr int stepsPerBar = 16;
constexpr int playlistBars = 32;

inline int stepAtTime(double seconds, double bpm)
{
    return seconds <= 0.0 || bpm <= 0.0 ? 0 : static_cast<int>(seconds * bpm / 15.0);
}

inline int barAtStep(int globalStep)
{
    return (globalStep / stepsPerBar) % playlistBars;
}

inline int patternAtStep(const uint8_t* arrangement, int globalStep)
{
    return arrangement[barAtStep(globalStep)];
}

inline double noteFrequency(int midiNote)
{
    // A4 = MIDI 69. Kept outside the GUI/audio device code for deterministic tests.
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

inline int stepInBar(int globalStep)
{
    return globalStep % stepsPerBar;
}

inline bool stepEnabled(uint16_t mask, int step)
{
    return step >= 0 && step < stepsPerBar && (mask & (uint16_t{1} << step)) != 0;
}

inline float panGain(float pan, bool left)
{
    return left ? (pan > 0.0f ? 1.0f - pan : 1.0f)
                : (pan < 0.0f ? 1.0f + pan : 1.0f);
}
}
