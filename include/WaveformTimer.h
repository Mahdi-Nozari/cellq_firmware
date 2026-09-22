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
void stop();
void pause();
void pauseAtCycleBoundary();
void resume();
uint32_t completedCycles(uint8_t group);
uint64_t remainingMilliseconds(uint8_t group);
bool takeGroupFinished(uint8_t group);
bool takeBoundaryPaused();
bool takeFinished();
}
}
