#pragma once
#include "SquareWavePreset.h"

namespace cellq {
namespace preset {
void begin();
bool findPreset(uint16_t id, SquareWavePreset &preset);
bool loadPreset(uint16_t id, SquareWavePreset &preset);
bool loadFactoryPreset1(SquareWavePreset &preset);
}
}
