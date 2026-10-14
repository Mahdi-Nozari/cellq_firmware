#pragma once
#include <Arduino.h>

namespace cellq {
namespace waveform {
struct GroupConfig {
  uint8_t channelMask;
  uint16_t frequencyTenthsHz;
  uint8_t dutyPercent;
  uint16_t delayMs;
  uint32_t totalCycles;
};

struct Config {
  GroupConfig group[2];
};

void begin();
bool start(const Config &config);
uint32_t cyclesForDurationMinutes(uint32_t minutes,
                                  uint16_t frequencyTenthsHz,
                                  uint16_t delayMs);
uint32_t durationMinutesForCyclesRoundedUp(uint32_t cycles,
                                           uint16_t frequencyTenthsHz,
                                           uint16_t delayMs);
void stop();
void pause();
void pauseAtCycleBoundary();
void resume();
uint32_t completedCycles(uint8_t group);
uint64_t remainingMilliseconds(uint8_t group);
bool takeGroupFinished(uint8_t group);
bool takeBoundaryPaused();
bool takeFinished();
uint16_t outputRevision(uint8_t group);
bool onPhaseHasRemainingMargin(uint8_t group, uint16_t marginMs);
}
}
