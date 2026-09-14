#pragma once
#include <Arduino.h>

namespace cellq {
namespace waveform {
struct Config {
  uint8_t channelMask;
  uint16_t frequencyTenthsHz;
  uint8_t dutyPercent;
  uint16_t delayMs;
  uint32_t totalCycles;
};

void begin();
bool start(const Config &config);
void stop();
void pause();
void resume();
uint32_t completedCycles();
bool takeFinished();
}
}
