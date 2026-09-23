#pragma once
#include "SquareWavePreset.h"

namespace cellq {
namespace preset {
void begin();
bool findPreset(uint16_t id, SquareWavePreset &preset);
bool loadPreset(uint16_t id, SquareWavePreset &preset);
bool loadFactoryPreset1(SquareWavePreset &preset);
bool saveUserPreset(SquareWavePreset &preset, const char *timestamp);
bool deleteUserPreset(uint16_t id);
bool firstVacantUserId(uint16_t &id);
}
}
