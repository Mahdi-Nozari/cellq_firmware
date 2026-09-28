#include "Display.h"
#include <genieArduino.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"

namespace {
Genie genie;
cellq::display::Event eventQueue[cellq::config::DISPLAY_EVENT_QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;

static_assert(cellq::gui::PRESET_SELECT_NAME_LABEL_LAST -
                      cellq::gui::PRESET_SELECT_NAME_LABEL_FIRST +
                  1U ==
              cellq::config::SQUARE_PRESET_SLOT_COUNT,
              "Preset selector label count must match preset slots");

uint8_t appendUnsigned(char *text, uint16_t value) {
  char reverse[5];
  uint8_t count = 0;
  do {
    reverse[count++] = static_cast<char>('0' + value % 10U);
    value /= 10U;
  } while (value > 0);
  for (uint8_t i = 0; i < count; ++i) {
    text[i] = reverse[count - 1U - i];
  }
  return count;
}

void formatSignedTenths(char *text, int16_t tenths) {
  uint8_t pos = 0;
  if (tenths < 0) {
    text[pos++] = '-';
  }
  const uint16_t magnitude =
      tenths < 0
          ? static_cast<uint16_t>(-static_cast<int32_t>(tenths))
          : static_cast<uint16_t>(tenths);
  pos += appendUnsigned(text + pos, magnitude / 10U);
  text[pos++] = '.';
  text[pos++] = static_cast<char>('0' + magnitude % 10U);
  text[pos] = '\0';
}

void onGenieEvent() {
  genieFrame frame;
  if (!genie.DequeueEvent(&frame)) {
    return;
  }
  const uint16_t value = genie.GetEventData(&frame);
  DBG_EVENT(frame.reportObject.cmd, frame.reportObject.object,
            frame.reportObject.index, value);
  if (frame.reportObject.cmd != GENIE_REPORT_EVENT) {
    return;
  }
  const uint8_t next =
      (queueHead + 1U) % cellq::config::DISPLAY_EVENT_QUEUE_SIZE;
  if (next == queueTail) {
    DBG_EVENT_DROP();
    return;
  }
  eventQueue[queueHead] = {
      frame.reportObject.object, frame.reportObject.index, value};
  queueHead = next;
}
}

namespace cellq {
namespace display {
void begin() {
  queueHead = 0;
  queueTail = 0;
  Serial1.begin(config::DISPLAY_BAUD);
  genie.Begin(Serial1);
  genie.AttachEventHandler(onGenieEvent);
  pinMode(hw::DISPLAY_RESET_PIN, OUTPUT);
  digitalWrite(hw::DISPLAY_RESET_PIN, HIGH);
  delay(100);
  digitalWrite(hw::DISPLAY_RESET_PIN, LOW);
  delay(config::DISPLAY_STARTUP_MS);
  for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
    setManualSwitch(channel, false);
  }
}

void service() {
  genie.DoEvents();
}

bool nextEvent(Event &event) {
  if (queueTail == queueHead) {
    return false;
  }
  event = eventQueue[queueTail];
  queueTail = (queueTail + 1U) % config::DISPLAY_EVENT_QUEUE_SIZE;
  return true;
}

void setManualSwitch(uint8_t channel, bool on) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  genie.WriteObject(gui::ISWITCH_OBJECT,
                    gui::MANUAL_VALVE_SWITCH_FIRST + channel, on ? 1 : 0);
}

void setManualPressureLabel(uint8_t regulator, int16_t tenths) {
  if (regulator >= hw::REGULATOR_COUNT) {
    return;
  }
  char text[8];
  formatSignedTenths(text, tenths);
  genie.WriteInhLabel(gui::MANUAL_PRESSURE_LABEL[regulator], text);
}

void setManualTimeLabel(uint8_t channel, uint16_t seconds) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  char text[6];
  uint8_t pos = appendUnsigned(text, seconds);
  text[pos] = '\0';
  genie.WriteInhLabel(gui::MANUAL_TIME_LABEL_FIRST + channel, text);
}

void setMonitorChannelStatus(uint8_t channel, uint8_t image) {
  if (channel >= hw::CHANNEL_COUNT || image > 2U) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT,
                    gui::MONITOR_CHANNEL_IMAGE_FIRST + channel, image);
}

void setMonitorPageStatus(uint8_t image) {
  if (image > 2U) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT,
                    gui::MONITOR_PAGE_STATUS_IMAGE, image);
}

void setMonitorPressureLabel(uint8_t channel, int16_t tenths) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  char text[8];
  formatSignedTenths(text, tenths);
  genie.WriteInhLabel(gui::MONITOR_PRESSURE_LABEL_FIRST + channel, text);
}

void addMonitorScopeSample(uint8_t channel, uint16_t value) {
  if (channel >= hw::CHANNEL_COUNT ||
      value > config::MONITOR_SCOPE_VALUE_MAX) {
    return;
  }
  const uint8_t group = channel / gui::MONITOR_SCOPE_TRACES;
  genie.WriteObject(gui::SCOPE_OBJECT, gui::MONITOR_GROUP_SCOPE[group], value);
}

void setSquareLabel(uint8_t index, const char *text) {
  const bool runLabel = index >= gui::SQUARE_NAME_LABEL &&
                        index <= gui::SQUARE_GROUP_2_DELAY_LABEL;
  const bool totalLabel = index == gui::SQUARE_TOTAL_CYCLES_LABEL[0] ||
                          index == gui::SQUARE_TOTAL_CYCLES_LABEL[1];
  if (text == nullptr || (!runLabel && !totalLabel)) {
    return;
  }
  genie.WriteInhLabel(index, text);
}

void setSquareChannelEnabled(uint8_t channel, bool enabled) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT,
                    gui::SQUARE_CHANNEL_IMAGE_FIRST + channel,
                    enabled ? 1 : 0);
}

void setSquareStatus(uint8_t image) {
  if (image > 4U) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT, gui::SQUARE_STATUS_IMAGE, image);
}

void setSquarePresetReady(bool ready) {
  genie.WriteObject(gui::USERIMAGES_OBJECT,
                    gui::SQUARE_PRESET_READY_IMAGE, ready ? 1 : 0);
}

void setSquareStartButton(bool stopImage) {
  genie.WriteObject(gui::USERBUTTON_OBJECT, gui::SQUARE_START_BUTTON,
                    stopImage ? 1 : 0);
}

void setSquarePauseButton(bool continueImage) {
  genie.WriteObject(gui::USERBUTTON_OBJECT, gui::SQUARE_PAUSE_BUTTON,
                    continueImage ? 1 : 0);
}

void setSquarePauseAtCycleSwitch(bool enabled) {
  genie.WriteObject(gui::ISWITCH_OBJECT,
                    gui::SQUARE_PAUSE_AT_CYCLE_SWITCH,
                    enabled ? 1 : 0);
}

void setSquareDataLogSwitch(bool enabled) {
  genie.WriteObject(gui::ISWITCH_OBJECT, gui::SQUARE_DATA_LOG_SWITCH,
                    enabled ? 1 : 0);
}

void setPresetSelectName(uint8_t slot, const char *text) {
  if (slot >= config::SQUARE_PRESET_SLOT_COUNT || text == nullptr) {
    return;
  }
  genie.WriteInhLabel(gui::PRESET_SELECT_NAME_LABEL_FIRST + slot, text);
}

void setSquareSetupLabel(uint8_t index, const char *text) {
  if (text == nullptr) {
    return;
  }
  bool valid = index == gui::SETUP_NAME_LABEL;
  for (uint8_t group = 0; group < hw::REGULATOR_COUNT; ++group) {
    for (uint8_t parameter = 0;
         parameter < gui::SETUP_PARAMETER_COUNT; ++parameter) {
      valid = valid || index == gui::SETUP_PARAMETER_LABEL[group][parameter];
    }
  }
  if (valid) {
    genie.WriteInhLabel(index, text);
  }
}

void setSquareSetupCycleModeSwitch(bool cycleMode) {
  genie.WriteObject(gui::ISWITCH_OBJECT,
                    gui::SETUP_CYCLE_MODE_SWITCH, cycleMode ? 1U : 0U);
}

void setSquareSetupModeImage(uint8_t group, bool timeMode) {
  if (group >= hw::REGULATOR_COUNT) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT,
                    gui::SETUP_MODE_IMAGE[group], timeMode ? 1U : 0U);
}

void setSquareSetupChannel(uint8_t channel, bool enabled) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  genie.WriteObject(gui::USERBUTTON_OBJECT,
                    gui::SETUP_CHANNEL_BUTTON_FIRST + channel,
                    enabled ? 0U : 1U);
}

void setSquareSetupSaveButton(bool savedImage) {
  genie.WriteObject(gui::USERBUTTON_OBJECT, gui::SETUP_SAVE_BUTTON,
                    savedImage ? 1U : 0U);
}

void setPresetNameEntry(const char *text) {
  if (text == nullptr) {
    return;
  }
  genie.WriteInhLabel(gui::PRESET_NAME_LABEL, text);
}

void activateForm(uint8_t form) {
  genie.WriteObject(gui::FORM_OBJECT, form, 0);
}
}
}
