#pragma once
#include <Arduino.h>
#include "Display.h"
#include "HardwareConfig.h"

namespace cellq {
namespace monitor {
struct PressureSample {
  uint32_t timeMs;
  int16_t pressureTenths[hw::CHANNEL_COUNT];
  uint8_t valveOn[hw::CHANNEL_COUNT];
};

void begin();
void acquire(uint32_t now);
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
bool latestSample(PressureSample &sample);
}
}
