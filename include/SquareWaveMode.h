#pragma once
#include <Arduino.h>
#include "Display.h"

namespace cellq {
namespace square {
void begin();
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
}
}
