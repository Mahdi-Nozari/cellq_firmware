#pragma once
#include <Arduino.h>

namespace cellq {
namespace display {
struct Event {
  uint8_t object;
  uint8_t index;
  uint16_t value;
};

void begin();
void service();
bool nextEvent(Event &event);
void setManualSwitch(uint8_t channel,bool on);
}
}
