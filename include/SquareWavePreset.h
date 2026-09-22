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
  uint16_t frequencyTenthsHz[hw::REGULATOR_COUNT];
  uint8_t dutyPercent[hw::REGULATOR_COUNT];
  uint16_t delayMs[hw::REGULATOR_COUNT];
  uint32_t totalCycles[hw::REGULATOR_COUNT];
};
}
