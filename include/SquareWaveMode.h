#pragma once
#include <Arduino.h>
#include "Display.h"

namespace cellq {
namespace square {
void begin();
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
bool isProcedureActive();
bool isChannelEnabled(uint8_t channel);
int16_t channelPressureTenths(uint8_t channel);
}
}
