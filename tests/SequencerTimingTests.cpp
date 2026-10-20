// SPDX-License-Identifier: AGPL-3.0-only
#include "../SequencerTiming.h"
#include <cassert>
#include <cmath>

int main()
{
    assert(pulse::stepAtTime(0.0, 120.0) == 0);
    assert(pulse::stepAtTime(0.124, 120.0) == 0);
    assert(pulse::stepAtTime(0.125, 120.0) == 1);
    assert(pulse::stepAtTime(2.0, 120.0) == 16);
    assert(pulse::barAtStep(16) == 1);
    assert(pulse::stepInBar(16) == 0);
    assert(pulse::barAtStep(128) == 8);
    assert(pulse::barAtStep(512) == 0);
    const uint8_t arrangement[pulse::playlistBars] = { 1, 2, 1 };
    assert(pulse::patternAtStep(arrangement, 0) == 1);
    assert(pulse::patternAtStep(arrangement, 16) == 2);
    assert(pulse::patternAtStep(arrangement, 32) == 1);
    assert(pulse::patternAtStep(arrangement, 48) == 0);
    assert(pulse::stepAtTime(4.0, 90.0) == 24);
    assert(pulse::barAtStep(24) == 1);
    assert(pulse::stepInBar(24) == 8);
    assert(pulse::stepEnabled(0b10010001, 0));
    assert(pulse::stepEnabled(0b10010001, 4));
    assert(!pulse::stepEnabled(0b10010001, 1));
    assert(!pulse::stepEnabled(0xffff, 16));
    assert(pulse::panGain(0.0f, true) == 1.0f);
    assert(pulse::panGain(0.0f, false) == 1.0f);
    assert(pulse::panGain(-1.0f, false) == 0.0f);
    assert(pulse::panGain(1.0f, true) == 0.0f);
    assert(std::abs(pulse::panGain(0.25f, true) - 0.75f) < 0.0001f);
    assert(std::abs(pulse::noteFrequency(69) - 440.0) < 0.0001);
    assert(std::abs(pulse::noteFrequency(60) - 261.625565) < 0.001);
}
