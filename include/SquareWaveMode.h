#pragma once
#include <Arduino.h>
#include "Display.h"
#include "SquareWavePreset.h"

namespace cellq {
namespace square {
void begin();
void update(uint32_t now);
void handleDisplayEvent(const display::Event &event, uint32_t now);
bool isProcedureActive();
bool isProcedurePreparing();
bool isProcedureRunning();
bool isChannelEnabled(uint8_t channel);
int16_t channelPressureTenths(uint8_t channel);
bool copyActivePreset(SquareWavePreset &preset);
bool copyPresetForEditing(uint16_t id, SquareWavePreset &preset);
bool setActivePreset(const SquareWavePreset &preset);
bool openPresetSelectorForSetup(uint32_t now);
void leaveRunPage();
void invalidatePresetList();
}
}
