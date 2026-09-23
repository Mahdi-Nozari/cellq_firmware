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
void setManualSwitch(uint8_t channel, bool on);
void setManualPressureLabel(uint8_t regulator, int16_t tenths);
void setManualTimeLabel(uint8_t channel, uint16_t seconds);
void setMonitorChannelStatus(uint8_t channel, uint8_t image);
void setMonitorPressureLabel(uint8_t channel, int16_t tenths);
void addMonitorScopeSample(uint8_t channel, uint16_t value);
void setSquareLabel(uint8_t index, const char *text);
void setSquareChannelEnabled(uint8_t channel, bool enabled);
void setSquareStatus(uint8_t image);
void setSquarePresetReady(bool ready);
void setSquareStartButton(bool stopImage);
void setSquarePauseButton(bool continueImage);
void setSquarePauseAtCycleSwitch(bool enabled);
void addSquareScopeSample(uint8_t group, uint16_t value);
void setPresetSelectName(uint8_t slot, const char *text);
void setSquareSetupLabel(uint8_t index, const char *text);
void setSquareSetupChannel(uint8_t channel, bool enabled);
void setPresetNameEntry(const char *text);
void activateForm(uint8_t form);
}
}
