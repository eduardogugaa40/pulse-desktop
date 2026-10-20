// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

namespace pulse
{
constexpr int stepsPerBar = 16;
constexpr int playlistBars = 8;

inline int stepAtTime(double seconds, double bpm)
{
    return seconds <= 0.0 || bpm <= 0.0 ? 0 : static_cast<int>(seconds * bpm / 15.0);
}

inline int barAtStep(int globalStep)
{
    return (globalStep / stepsPerBar) % playlistBars;
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
