#pragma once
#include <Arduino.h>
#include "Display.h"
#include "SquareWavePreset.h"

namespace cellq {
namespace setup {
void begin();
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
void showSelectedPreset(const SquareWavePreset &preset, uint32_t now);
void returnFromPresetSelector(uint32_t now);
}
}
