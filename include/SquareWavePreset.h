#pragma once
#include <Arduino.h>
#include "HardwareConfig.h"

namespace cellq {
struct SquareWavePreset {
  uint16_t id;
  uint32_t revision;
  char name[21];
  int16_t pressureTenths[hw::REGULATOR_COUNT];
  uint8_t channelEnabled[hw::CHANNEL_COUNT];
  uint16_t frequencyTenthsHz;
  uint8_t dutyPercent;
  uint16_t delayMs;
  uint32_t totalCycles;
};
}
