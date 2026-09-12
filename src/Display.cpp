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

void setSquareLabel(uint8_t index, const char *text) {
  if (index < gui::SQUARE_NAME_LABEL ||
      index > gui::SQUARE_ACTIVE_NAME_LABEL || text == nullptr) {
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
  if (image > 3U) {
    return;
  }
  genie.WriteObject(gui::USERIMAGES_OBJECT, gui::SQUARE_STATUS_IMAGE, image);
}

void setSquareStartButton(bool stopImage) {
  genie.WriteObject(gui::USERBUTTON_OBJECT, gui::SQUARE_START_BUTTON,
                    stopImage ? 1 : 0);
}

void setSquarePauseButton(bool continueImage) {
  genie.WriteObject(gui::USERBUTTON_OBJECT, gui::SQUARE_PAUSE_BUTTON,
                    continueImage ? 1 : 0);
}

void addSquareScopeSample(uint16_t value) {
  genie.WriteObject(gui::SCOPE_OBJECT, gui::SQUARE_SCOPE, value);
}
}
}
