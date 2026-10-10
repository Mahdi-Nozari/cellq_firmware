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
void beginEventUpdate();
void endEventUpdate();
bool idle();
bool nextEvent(Event &event);
void setHomeVersionLabel();
void setManualSwitch(uint8_t channel, bool on);
void setManualGroupLinkSwitch(uint8_t group, bool linked);
void setManualPressureLabel(uint8_t regulator, int16_t tenths);
void setManualTimeLabel(uint8_t channel, uint8_t unit, uint8_t value);
void setManualTimeUnitImage(uint8_t channel, uint8_t unit);
void setMonitorChannelStatus(uint8_t channel, uint8_t image);
void setMonitorLeakStatus(uint8_t channel, bool detected);
void setMonitorPageStatus(uint8_t image);
void setMonitorPressureLabel(uint8_t channel, int16_t tenths);
void setMonitorPressureUnavailable(uint8_t channel);
void addMonitorScopeSample(uint8_t channel, uint16_t value);
bool monitorQueueHasRoom();
void setSquareLabel(uint8_t index, const char *text);
void setSquareChannelEnabled(uint8_t channel, bool enabled);
void setSquareStatus(uint8_t image);
void playSquareSound(uint8_t sound);
void setSquarePresetReady(bool ready);
void setSquareStartButton(bool stopImage);
void setSquarePauseButton(bool continueImage);
void setSquarePauseAtCycleSwitch(bool enabled);
void setSquareDataLogSwitch(bool enabled);
void setPresetSelectName(uint8_t slot, const char *text);
void setSquareSetupLabel(uint8_t index, const char *text);
void setSquareSetupCycleModeSwitch(bool cycleMode);
void setSquareSetupModeImage(uint8_t group, bool timeMode);
void setSquareSetupTimeFieldImage(uint8_t group, uint8_t image);
void setSquareSetupChannel(uint8_t channel, bool enabled);
void setSquareSetupSaveButton(bool savedImage);
void setPresetNameEntry(const char *text);
void setMaintenanceStatus(const char *text);
void activateForm(uint8_t form);
}
}
