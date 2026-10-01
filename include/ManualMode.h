#pragma once
#include <Arduino.h>
#include "Display.h"

namespace cellq {
namespace manual {
void begin();
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
int16_t pressureTenths(uint8_t regulator);
uint32_t onTimeSeconds(uint8_t channel);
bool valveIsOn(uint8_t channel);
}
}
