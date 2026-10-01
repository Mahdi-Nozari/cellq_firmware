#include "Display.h"
#include <genieArduino.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"

namespace {
constexpr uint8_t MAX_LABEL_LENGTH = 20U;
constexpr uint8_t FRAME_LENGTH = 6U;
constexpr uint8_t NO_EXPECTED_FORM = 0xFFU;

struct TxCommand {
  uint8_t command;
  uint8_t object;
  uint8_t index;
  uint8_t retries;
  uint16_t value;
  char text[MAX_LABEL_LENGTH + 1U];
};

cellq::display::Event eventQueue[cellq::config::DISPLAY_EVENT_QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
TxCommand txQueue[cellq::config::DISPLAY_TX_QUEUE_SIZE] = {};
TxCommand inFlight = {};
uint8_t txCount = 0;
uint8_t priorityTail = 0;
uint8_t rxFrame[FRAME_LENGTH] = {};
uint8_t rxFrameLength = 0;
uint8_t expectedForm = NO_EXPECTED_FORM;
uint32_t commandSentMs = 0;
uint32_t lastFrameByteMs = 0;
bool waitingForAck = false;
bool eventPriority = false;

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

void acceptEvent(uint8_t object, uint8_t index, uint16_t value) {
  DBG_EVENT(GENIE_REPORT_EVENT, object, index, value);
  if (object == cellq::gui::FORM_OBJECT) {
    if (index != expectedForm) {
      txCount = 0;
      priorityTail = 0;
    }
    expectedForm = NO_EXPECTED_FORM;
  }
  const uint8_t next =
      (queueHead + 1U) % cellq::config::DISPLAY_EVENT_QUEUE_SIZE;
  if (next == queueTail) {
    DBG_EVENT_DROP();
    return;
  }
  eventQueue[queueHead] = {object, index, value};
  queueHead = next;
}

void retryInFlight() {
  if (inFlight.retries != 0U ||
      txCount >= cellq::config::DISPLAY_TX_QUEUE_SIZE ||
      (eventPriority && txCount != 0U) ||
      (txCount != 0U && txQueue[0].object == cellq::gui::FORM_OBJECT)) {
    DBG_LCD_DROP(inFlight.command, inFlight.index);
    return;
  }
  DBG_LCD_RETRY(inFlight.command, inFlight.index);
  ++inFlight.retries;
  for (uint8_t index = txCount; index > 0U; --index) {
    txQueue[index] = txQueue[index - 1U];
  }
  txQueue[0] = inFlight;
  ++txCount;
}

void receiveByte(uint8_t byte) {
  const uint32_t now = millis();
  if (rxFrameLength != 0U &&
      static_cast<uint32_t>(now - lastFrameByteMs) > 50U) {
    rxFrameLength = 0;
  }
  if (rxFrameLength != 0U) {
    lastFrameByteMs = now;
    rxFrame[rxFrameLength++] = byte;
    if (rxFrameLength == FRAME_LENGTH) {
      uint8_t checksum = 0;
      for (uint8_t index = 0; index < FRAME_LENGTH; ++index) {
        checksum ^= rxFrame[index];
      }
      if (checksum == 0U) {
        acceptEvent(rxFrame[1], rxFrame[2],
                    static_cast<uint16_t>(rxFrame[3]) << 8U |
                        rxFrame[4]);
      }
      rxFrameLength = 0;
    }
    return;
  }
  if (byte == GENIE_REPORT_EVENT) {
    rxFrame[0] = byte;
    rxFrameLength = 1;
    lastFrameByteMs = now;
  } else if (byte == GENIE_ACK) {
    waitingForAck = false;
  } else if (byte == GENIE_NAK) {
    if (waitingForAck) {
      waitingForAck = false;
      retryInFlight();
    }
  }
}

uint8_t packetLength(const TxCommand &item) {
  if (item.command == GENIE_WRITE_OBJ) {
    return FRAME_LENGTH;
  }
  return static_cast<uint8_t>(4U + strlen(item.text));
}

void sendPacket(const TxCommand &item) {
  uint8_t checksum = item.command;
  Serial1.write(item.command);
  if (item.command == GENIE_WRITE_OBJ) {
    const uint8_t bytes[] = {
        item.object, item.index, static_cast<uint8_t>(item.value >> 8U),
        static_cast<uint8_t>(item.value)};
    for (uint8_t byte : bytes) {
      Serial1.write(byte);
      checksum ^= byte;
    }
  } else {
    const uint8_t length = strlen(item.text);
    Serial1.write(item.index);
    Serial1.write(length);
    checksum ^= item.index ^ length;
    for (uint8_t index = 0; index < length; ++index) {
      const uint8_t byte = item.text[index];
      Serial1.write(byte);
      checksum ^= byte;
    }
  }
  Serial1.write(checksum);
  commandSentMs = millis();
  waitingForAck = true;
}

void sendNext() {
  if (waitingForAck || txCount == 0U ||
      Serial1.availableForWrite() < packetLength(txQueue[0])) {
    return;
  }
  inFlight = txQueue[0];
  for (uint8_t index = 1; index < txCount; ++index) {
    txQueue[index - 1U] = txQueue[index];
  }
  --txCount;
  if (priorityTail != 0U) {
    --priorityTail;
  }
  sendPacket(inFlight);
}

void queueCommand(const TxCommand &item) {
  const bool pageStatusImage =
      item.object == cellq::gui::USERIMAGES_OBJECT &&
      (item.index == cellq::gui::SQUARE_PRESET_READY_IMAGE ||
       item.index == cellq::gui::MONITOR_PAGE_STATUS_IMAGE);
  if (item.object != cellq::gui::SCOPE_OBJECT && !pageStatusImage) {
    for (uint8_t index = 0; index < txCount; ++index) {
      if (txQueue[index].command == item.command &&
          txQueue[index].object == item.object &&
          txQueue[index].index == item.index) {
        if (!eventPriority) {
          txQueue[index] = item;
          return;
        }
        for (uint8_t next = index + 1U; next < txCount; ++next) {
          txQueue[next - 1U] = txQueue[next];
        }
        --txCount;
        if (index < priorityTail) {
          --priorityTail;
        }
        break;
      }
    }
  }
  if (txCount >= cellq::config::DISPLAY_TX_QUEUE_SIZE) {
    if (!eventPriority) {
      DBG_LCD_DROP(item.command, item.index);
      return;
    }
    DBG_LCD_DROP(txQueue[txCount - 1U].command,
                 txQueue[txCount - 1U].index);
    --txCount;
    if (priorityTail > txCount) {
      priorityTail = txCount;
    }
  }
  if (eventPriority) {
    for (uint8_t index = txCount; index > priorityTail; --index) {
      txQueue[index] = txQueue[index - 1U];
    }
    txQueue[priorityTail++] = item;
    ++txCount;
  } else {
    txQueue[txCount++] = item;
  }
  sendNext();
}

void queueObject(uint8_t object, uint8_t index, uint16_t value) {
  TxCommand item = {};
  item.command = GENIE_WRITE_OBJ;
  item.object = object;
  item.index = index;
  item.value = value;
  queueCommand(item);
}

void queueLabel(uint8_t index, const char *text) {
  if (text == nullptr) {
    return;
  }
  const size_t length = strlen(text);
  if (length > MAX_LABEL_LENGTH) {
    DBG_LCD_DROP(GENIE_WRITE_INH_LABEL, index);
    return;
  }
  TxCommand item = {};
  item.command = GENIE_WRITE_INH_LABEL;
  item.object = cellq::gui::ILABELB_OBJECT;
  item.index = index;
  memcpy(item.text, text, length + 1U);
  queueCommand(item);
}
}

namespace cellq {
namespace display {
void begin() {
  queueHead = 0;
  queueTail = 0;
  txCount = 0;
  priorityTail = 0;
  rxFrameLength = 0;
  expectedForm = NO_EXPECTED_FORM;
  waitingForAck = false;
  Serial1.begin(config::DISPLAY_BAUD);
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
  uint8_t processed = 0;
  while (Serial1.available() > 0 && processed < 64U) {
    const uint8_t nextEventSlot =
        (queueHead + 1U) % config::DISPLAY_EVENT_QUEUE_SIZE;
    if (nextEventSlot == queueTail && rxFrameLength == 0U) {
      break;
    }
    receiveByte(static_cast<uint8_t>(Serial1.read()));
    ++processed;
  }
  if (waitingForAck &&
      static_cast<uint32_t>(millis() - commandSentMs) >=
          config::DISPLAY_ACK_TIMEOUT_MS) {
    waitingForAck = false;
    retryInFlight();
  }
  sendNext();
}

void beginEventUpdate() {
  eventPriority = true;
  priorityTail = 0;
}

void endEventUpdate() {
  eventPriority = false;
  priorityTail = 0;
}

bool nextEvent(Event &event) {
  if (queueTail == queueHead) {
    return false;
  }
  event = eventQueue[queueTail];
  queueTail = (queueTail + 1U) % config::DISPLAY_EVENT_QUEUE_SIZE;
  return true;
}

void setHomeVersionLabel() {
  queueLabel(gui::HOME_VERSION_LABEL, config::FIRMWARE_VERSION_TEXT);
}

void setManualSwitch(uint8_t channel, bool on) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  queueObject(gui::ISWITCH_OBJECT,
              gui::MANUAL_VALVE_SWITCH_FIRST + channel, on ? 1 : 0);
}

void setManualGroupLinkSwitch(uint8_t group, bool linked) {
  if (group >= hw::REGULATOR_COUNT) {
    return;
  }
  queueObject(gui::ISWITCH_OBJECT,
              gui::MANUAL_GROUP_LINK_SWITCH[group], linked ? 1U : 0U);
}

void setManualPressureLabel(uint8_t regulator, int16_t tenths) {
  if (regulator >= hw::REGULATOR_COUNT) {
    return;
  }
  char text[8];
  formatSignedTenths(text, tenths);
  queueLabel(gui::MANUAL_PRESSURE_LABEL[regulator], text);
}

void setManualTimeLabel(uint8_t channel, uint8_t unit, uint8_t value) {
  if (channel >= hw::CHANNEL_COUNT || unit >= 3U) {
    return;
  }
  char text[4];
  uint8_t pos = 0;
  if (value < 10U) {
    text[pos++] = '0';
  }
  pos += appendUnsigned(text + pos, value);
  text[pos] = '\0';
  queueLabel(gui::MANUAL_TIME_LABEL[unit][channel], text);
}

void setManualTimeSelector(uint8_t channel, uint8_t unit) {
  if (channel >= hw::CHANNEL_COUNT || unit >= 3U) {
    return;
  }
  queueObject(gui::USERBUTTON_OBJECT,
              gui::MANUAL_TIME_SELECTOR_FIRST + channel, unit);
}

void setMonitorChannelStatus(uint8_t channel, uint8_t image) {
  if (channel >= hw::CHANNEL_COUNT || image > 2U) {
    return;
  }
  queueObject(gui::USERIMAGES_OBJECT,
              gui::MONITOR_CHANNEL_IMAGE_FIRST + channel, image);
}

void setMonitorPageStatus(uint8_t image) {
  if (image > 2U) {
    return;
  }
  queueObject(gui::USERIMAGES_OBJECT,
              gui::MONITOR_PAGE_STATUS_IMAGE, image);
}

void setMonitorPressureLabel(uint8_t channel, int16_t tenths) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  char text[8];
  formatSignedTenths(text, tenths);
  queueLabel(gui::MONITOR_PRESSURE_LABEL_FIRST + channel, text);
}

void addMonitorScopeSample(uint8_t channel, uint16_t value) {
  if (channel >= hw::CHANNEL_COUNT ||
      value > config::MONITOR_SCOPE_VALUE_MAX) {
    return;
  }
  const uint8_t group = channel / gui::MONITOR_SCOPE_TRACES;
  queueObject(gui::SCOPE_OBJECT, gui::MONITOR_GROUP_SCOPE[group], value);
}

void setSquareLabel(uint8_t index, const char *text) {
  const bool runLabel = index >= gui::SQUARE_NAME_LABEL &&
                        index <= gui::SQUARE_GROUP_2_DELAY_LABEL;
  const bool totalLabel = index == gui::SQUARE_TOTAL_CYCLES_LABEL[0] ||
                          index == gui::SQUARE_TOTAL_CYCLES_LABEL[1];
  if (text == nullptr || (!runLabel && !totalLabel)) {
    return;
  }
  queueLabel(index, text);
}

void setSquareChannelEnabled(uint8_t channel, bool enabled) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  queueObject(gui::USERIMAGES_OBJECT,
              gui::SQUARE_CHANNEL_IMAGE_FIRST + channel,
              enabled ? 1 : 0);
}

void setSquareStatus(uint8_t image) {
  if (image > 4U) {
    return;
  }
  queueObject(gui::USERIMAGES_OBJECT, gui::SQUARE_STATUS_IMAGE, image);
}

void setSquarePresetReady(bool ready) {
  queueObject(gui::USERIMAGES_OBJECT,
              gui::SQUARE_PRESET_READY_IMAGE, ready ? 1 : 0);
}

void setSquareStartButton(bool stopImage) {
  queueObject(gui::USERBUTTON_OBJECT, gui::SQUARE_START_BUTTON,
              stopImage ? 1 : 0);
}

void setSquarePauseButton(bool continueImage) {
  queueObject(gui::USERBUTTON_OBJECT, gui::SQUARE_PAUSE_BUTTON,
              continueImage ? 1 : 0);
}

void setSquarePauseAtCycleSwitch(bool enabled) {
  queueObject(gui::ISWITCH_OBJECT,
              gui::SQUARE_PAUSE_AT_CYCLE_SWITCH,
              enabled ? 1 : 0);
}

void setSquareDataLogSwitch(bool enabled) {
  queueObject(gui::ISWITCH_OBJECT, gui::SQUARE_DATA_LOG_SWITCH,
              enabled ? 1 : 0);
}

void setPresetSelectName(uint8_t slot, const char *text) {
  if (slot >= config::SQUARE_PRESET_SLOT_COUNT || text == nullptr) {
    return;
  }
  queueLabel(gui::PRESET_SELECT_NAME_LABEL_FIRST + slot, text);
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
    queueLabel(index, text);
  }
}

void setSquareSetupCycleModeSwitch(bool cycleMode) {
  queueObject(gui::ISWITCH_OBJECT,
              gui::SETUP_CYCLE_MODE_SWITCH, cycleMode ? 1U : 0U);
}

void setSquareSetupModeImage(uint8_t group, bool timeMode) {
  if (group >= hw::REGULATOR_COUNT) {
    return;
  }
  queueObject(gui::USERIMAGES_OBJECT,
              gui::SETUP_MODE_IMAGE[group], timeMode ? 1U : 0U);
}

void setSquareSetupChannel(uint8_t channel, bool enabled) {
  if (channel >= hw::CHANNEL_COUNT) {
    return;
  }
  queueObject(gui::USERBUTTON_OBJECT,
              gui::SETUP_CHANNEL_BUTTON_FIRST + channel,
              enabled ? 0U : 1U);
}

void setSquareSetupSaveButton(bool savedImage) {
  queueObject(gui::USERBUTTON_OBJECT, gui::SETUP_SAVE_BUTTON,
              savedImage ? 1U : 0U);
}

void setPresetNameEntry(const char *text) {
  if (text == nullptr) {
    return;
  }
  queueLabel(gui::PRESET_NAME_LABEL, text);
}

void activateForm(uint8_t form) {
  txCount = 0;
  priorityTail = 0;
  expectedForm = form;
  queueObject(gui::FORM_OBJECT, form, 0);
}
}
}
