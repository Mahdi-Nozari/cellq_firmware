#include "SquareWaveSetup.h"
#include <stdio.h>
#include <string.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "PresetStore.h"
#include "RtcClock.h"
#include "SquareWaveMode.h"
#include "SquareWavePreset.h"

namespace {
constexpr uint8_t PARAMETER_COUNT = 4U;
constexpr uint8_t SYNC_STEP_COUNT =
    1U + cellq::hw::REGULATOR_COUNT * PARAMETER_COUNT +
    cellq::hw::CHANNEL_COUNT;
constexpr uint8_t UNKNOWN_FORM = 0xFFU;

cellq::SquareWavePreset draft_ = {};
bool draftValid_ = false;
bool formVisible_ = false;
bool nameEntryVisible_ = false;
bool syncPending_ = false;
uint8_t syncStep_ = 0;
uint8_t currentForm_ = UNKNOWN_FORM;
uint32_t lastSyncMs_ = 0;
char nameEntry_[21] = {};
uint8_t nameLength_ = 0;

uint8_t appendUnsigned(char *text, uint32_t value) {
  char reverse[10];
  uint8_t count = 0;
  do {
    reverse[count++] = static_cast<char>('0' + value % 10UL);
    value /= 10UL;
  } while (value > 0U);
  for (uint8_t index = 0; index < count; ++index) {
    text[index] = reverse[count - 1U - index];
  }
  return count;
}

void formatSignedTenths(char *text, int16_t tenths) {
  uint8_t position = 0;
  if (tenths < 0) {
    text[position++] = '-';
  }
  const uint16_t magnitude =
      tenths < 0
          ? static_cast<uint16_t>(-static_cast<int32_t>(tenths))
          : static_cast<uint16_t>(tenths);
  position += appendUnsigned(text + position, magnitude / 10U);
  text[position++] = '.';
  text[position++] = static_cast<char>('0' + magnitude % 10U);
  text[position] = '\0';
}

void formatUnsignedTenths(char *text, uint16_t tenths) {
  uint8_t position = appendUnsigned(text, tenths / 10U);
  text[position++] = '.';
  text[position++] = static_cast<char>('0' + tenths % 10U);
  text[position] = '\0';
}

void formatHundredths(char *text, uint32_t hundredths) {
  uint8_t position = appendUnsigned(text, hundredths / 100UL);
  const uint8_t fraction = hundredths % 100UL;
  if (fraction == 0U) {
    text[position] = '\0';
    return;
  }
  text[position++] = '.';
  text[position++] = static_cast<char>('0' + fraction / 10U);
  if (fraction % 10U != 0U) {
    text[position++] = static_cast<char>('0' + fraction % 10U);
  }
  text[position] = '\0';
}

void writeParameter(uint8_t group, uint8_t parameter) {
  char text[16];
  if (parameter == 0U) {
    formatSignedTenths(text, draft_.pressureTenths[group]);
  } else if (parameter == 1U) {
    formatUnsignedTenths(text, draft_.frequencyTenthsHz[group]);
  } else if (parameter == 2U) {
    const uint8_t position = appendUnsigned(text, draft_.dutyPercent[group]);
    text[position] = '\0';
  } else {
    formatHundredths(text, (draft_.delayMs[group] + 5UL) / 10UL);
  }
  cellq::display::setSquareSetupLabel(
      cellq::gui::SETUP_PARAMETER_LABEL[group][parameter], text);
}

void beginSync(uint32_t now) {
  syncPending_ = true;
  syncStep_ = 0;
  lastSyncMs_ = now - cellq::config::SETUP_DISPLAY_WRITE_INTERVAL_MS;
}

void writeSyncStep() {
  if (syncStep_ == 0U) {
    cellq::display::setSquareSetupLabel(cellq::gui::SETUP_NAME_LABEL,
                                        draft_.name);
    return;
  }
  const uint8_t parameterSteps =
      cellq::hw::REGULATOR_COUNT * PARAMETER_COUNT;
  if (syncStep_ <= parameterSteps) {
    const uint8_t offset = syncStep_ - 1U;
    writeParameter(offset / PARAMETER_COUNT, offset % PARAMETER_COUNT);
    return;
  }
  const uint8_t channel = syncStep_ - parameterSteps - 1U;
  cellq::display::setSquareSetupChannel(
      channel, draft_.channelEnabled[channel] != 0U);
}

bool loadDraft(uint16_t id, uint32_t now) {
  cellq::SquareWavePreset candidate = {};
  if (!cellq::square::copyPresetForEditing(id, candidate)) {
    return false;
  }
  draft_ = candidate;
  draftValid_ = true;
  beginSync(now);
  return true;
}

bool validNameCharacter(uint8_t value) {
  const bool alphaNumeric =
      (value >= 'a' && value <= 'z') ||
      (value >= 'A' && value <= 'Z') ||
      (value >= '0' && value <= '9');
  return alphaNumeric || value == ' ' || value == '-' || value == '_';
}

bool validNameEntry() {
  return nameLength_ > 0U && nameEntry_[0] != ' ' &&
         nameEntry_[nameLength_ - 1U] != ' ';
}

void openNameEntry() {
  if (!draftValid_ || draft_.id <= 2U) {
    DBG_SETUP("NAME,READONLY");
    return;
  }
  strcpy(nameEntry_, draft_.name);
  nameLength_ = strlen(nameEntry_);
  formVisible_ = false;
  nameEntryVisible_ = true;
  syncPending_ = false;
  currentForm_ = cellq::gui::PRESET_NAME_FORM;
  cellq::display::activateForm(cellq::gui::PRESET_NAME_FORM);
  cellq::display::setPresetNameEntry(nameEntry_);
  DBG_SETUP("NAME,OPEN");
}

void handleNameKey(uint16_t value) {
  if (value == 8U) {
    if (nameLength_ == 0U) {
      return;
    }
    nameEntry_[--nameLength_] = '\0';
    cellq::display::setPresetNameEntry(nameEntry_);
    return;
  }
  if (value > UINT8_MAX || !validNameCharacter(value) ||
      nameLength_ >= sizeof(nameEntry_) - 1U) {
    return;
  }
  nameEntry_[nameLength_++] = static_cast<char>(value);
  nameEntry_[nameLength_] = '\0';
  cellq::display::setPresetNameEntry(nameEntry_);
}

void commitNameEntry(uint32_t now) {
  if (!validNameEntry()) {
    DBG_SETUP("NAME,INVALID");
    return;
  }
  strcpy(draft_.name, nameEntry_);
  nameEntryVisible_ = false;
  formVisible_ = true;
  currentForm_ = cellq::gui::SQUARE_SETUP_FORM;
  cellq::display::activateForm(cellq::gui::SQUARE_SETUP_FORM);
  beginSync(now);
  DBG_SETUP("NAME,SET");
}

void enterSetup(uint32_t now, bool activateForm) {
  if (cellq::square::isProcedureActive()) {
    DBG_SETUP("LOCKED");
    return;
  }
  formVisible_ = true;
  nameEntryVisible_ = false;
  currentForm_ = cellq::gui::SQUARE_SETUP_FORM;
  if (activateForm) {
    cellq::display::activateForm(cellq::gui::SQUARE_SETUP_FORM);
  }
  if (!loadDraft(1U, now)) {
    draftValid_ = false;
    syncPending_ = false;
    DBG_SETUP("LOAD,FAIL");
    return;
  }
  DBG_SETUP("LOAD,1");
}

void makeNewDraft(uint32_t now) {
  uint16_t id;
  if (!cellq::preset::firstVacantUserId(id)) {
    DBG_SETUP("NEW,FULL");
    return;
  }
  memset(&draft_, 0, sizeof(draft_));
  draft_.id = id;
  snprintf(draft_.name, sizeof(draft_.name), "preset %u", id);
  for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT; ++group) {
    draft_.pressureTenths[group] = 0;
    draft_.frequencyTenthsHz[group] = 10U;
    draft_.dutyPercent[group] = 50U;
    draft_.delayMs[group] = 0U;
    draft_.totalCycles[group] =
        cellq::config::SETUP_DEFAULT_TOTAL_CYCLES;
  }
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    draft_.channelEnabled[channel] = 1U;
  }
  draftValid_ = true;
  beginSync(now);
  DBG_SETUP("NEW");
}

void adjustParameter(uint8_t group, uint8_t parameter, bool increase) {
  if (!draftValid_ || draft_.id <= 2U) {
    DBG_SETUP("EDIT,READONLY");
    return;
  }
  if (parameter == 0U) {
    const int16_t change = cellq::config::SETUP_PRESSURE_STEP_TENTHS;
    int16_t value = draft_.pressureTenths[group];
    value = increase ? value + change : value - change;
    if (value < cellq::config::SQUARE_PRESSURE_MIN_TENTHS) {
      value = cellq::config::SQUARE_PRESSURE_MIN_TENTHS;
    }
    if (value > cellq::config::SQUARE_PRESSURE_MAX_TENTHS) {
      value = cellq::config::SQUARE_PRESSURE_MAX_TENTHS;
    }
    draft_.pressureTenths[group] = value;
  } else if (parameter == 1U) {
    uint16_t value = draft_.frequencyTenthsHz[group];
    if (increase) {
      if (value < cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ) {
        value += cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ;
      }
    } else if (value >= cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ) {
      value -= cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ;
    }
    draft_.frequencyTenthsHz[group] = value;
  } else if (parameter == 2U) {
    uint8_t value = draft_.dutyPercent[group];
    if (increase) {
      value = value <= 100U - cellq::config::SETUP_DUTY_STEP_PERCENT
                  ? value + cellq::config::SETUP_DUTY_STEP_PERCENT
                  : 100U;
    } else {
      value = value >= cellq::config::SETUP_DUTY_STEP_PERCENT
                  ? value - cellq::config::SETUP_DUTY_STEP_PERCENT
                  : 0U;
    }
    draft_.dutyPercent[group] = value;
  } else {
    uint16_t value = draft_.delayMs[group];
    if (increase) {
      value = value <= cellq::config::SQUARE_DELAY_MAX_MS -
                            cellq::config::SETUP_DELAY_STEP_MS
                  ? value + cellq::config::SETUP_DELAY_STEP_MS
                  : cellq::config::SQUARE_DELAY_MAX_MS;
    } else {
      value = value >= cellq::config::SETUP_DELAY_STEP_MS
                  ? value - cellq::config::SETUP_DELAY_STEP_MS
                  : 0U;
    }
    draft_.delayMs[group] = value;
  }
  writeParameter(group, parameter);
}

void toggleChannel(uint8_t channel) {
  if (!draftValid_ || draft_.id <= 2U) {
    DBG_SETUP("EDIT,READONLY");
    return;
  }
  draft_.channelEnabled[channel] =
      draft_.channelEnabled[channel] == 0U ? 1U : 0U;
  cellq::display::setSquareSetupChannel(
      channel, draft_.channelEnabled[channel] != 0U);
}

void saveDraft() {
  if (!draftValid_ || draft_.id <= 2U) {
    DBG_SETUP("SAVE,READONLY");
    return;
  }
  char timestamp[16];
  cellq::clock::timestamp(timestamp);
  if (!cellq::preset::saveUserPreset(draft_, timestamp)) {
    DBG_SETUP("SAVE,FAIL");
    return;
  }
  cellq::square::invalidatePresetList();
  cellq::square::setActivePreset(draft_);
  DBG_SETUP("SAVE,OK");
}

bool findNextPreset(uint16_t deletedId, cellq::SquareWavePreset &preset) {
  for (uint16_t id = deletedId + 1U;
       id <= cellq::config::SQUARE_PRESET_SLOT_COUNT; ++id) {
    if (cellq::square::copyPresetForEditing(id, preset)) {
      return true;
    }
  }
  for (uint16_t id = deletedId - 1U; id >= 1U; --id) {
    if (cellq::square::copyPresetForEditing(id, preset)) {
      return true;
    }
  }
  return false;
}

void deleteDraft(uint32_t now) {
  if (!draftValid_ || draft_.id <= 2U) {
    DBG_SETUP("DELETE,READONLY");
    return;
  }
  const uint16_t deletedId = draft_.id;
  if (!cellq::preset::deleteUserPreset(deletedId)) {
    DBG_SETUP("DELETE,FAIL");
    return;
  }
  cellq::square::invalidatePresetList();
  cellq::SquareWavePreset next = {};
  if (!findNextPreset(deletedId, next)) {
    draftValid_ = false;
    syncPending_ = false;
    return;
  }
  draft_ = next;
  draftValid_ = true;
  cellq::square::setActivePreset(draft_);
  beginSync(now);
  DBG_SETUP("DELETE,OK");
}

bool findParameterButton(uint8_t button, uint8_t &group,
                         uint8_t &parameter, bool &increase) {
  for (group = 0; group < cellq::hw::REGULATOR_COUNT; ++group) {
    for (parameter = 0; parameter < PARAMETER_COUNT; ++parameter) {
      if (button == cellq::gui::SETUP_INCREASE_BUTTON[group][parameter]) {
        increase = true;
        return true;
      }
      if (button == cellq::gui::SETUP_DECREASE_BUTTON[group][parameter]) {
        increase = false;
        return true;
      }
    }
  }
  return false;
}
}

namespace cellq {
namespace setup {
void begin() {
  draftValid_ = false;
  formVisible_ = false;
  nameEntryVisible_ = false;
  syncPending_ = false;
  currentForm_ = UNKNOWN_FORM;
}

void update(uint32_t now) {
  if (!formVisible_ || !draftValid_ || !syncPending_ ||
      static_cast<uint32_t>(now - lastSyncMs_) <
          config::SETUP_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  lastSyncMs_ = now;
  writeSyncStep();
  ++syncStep_;
  if (syncStep_ >= SYNC_STEP_COUNT) {
    syncPending_ = false;
    DBG_SETUP("READY");
  }
}

void handleDisplayEvent(const display::Event &event, uint32_t now) {
  if (event.object == gui::FORM_OBJECT) {
    currentForm_ = event.index;
    if (event.index == gui::SQUARE_SETUP_FORM) {
      if (square::isProcedureActive()) {
        formVisible_ = false;
        display::activateForm(gui::SQUARE_FORM);
        currentForm_ = gui::SQUARE_FORM;
        DBG_SETUP("LOCKED");
      } else if (nameEntryVisible_ && draftValid_) {
        nameEntryVisible_ = false;
        formVisible_ = true;
        beginSync(now);
        DBG_SETUP("NAME,CANCEL");
      } else if (!formVisible_) {
        enterSetup(now, false);
      }
    } else if (event.index == gui::PRESET_NAME_FORM) {
      formVisible_ = false;
      syncPending_ = false;
      if (square::isProcedureActive()) {
        nameEntryVisible_ = false;
        display::activateForm(gui::SQUARE_FORM);
        currentForm_ = gui::SQUARE_FORM;
        DBG_SETUP("LOCKED");
      } else if (nameEntryVisible_ && draftValid_) {
        display::setPresetNameEntry(nameEntry_);
      } else {
        display::activateForm(gui::SQUARE_SETUP_FORM);
        enterSetup(now, false);
      }
    } else {
      formVisible_ = false;
      nameEntryVisible_ = false;
      syncPending_ = false;
      draftValid_ = false;
    }
    return;
  }
  if (nameEntryVisible_ && currentForm_ == gui::PRESET_NAME_FORM) {
    if (event.object == gui::KEYBOARD_OBJECT &&
        event.index == gui::PRESET_NAME_KEYBOARD) {
      handleNameKey(event.value);
    } else if (event.object == gui::USERBUTTON_OBJECT &&
               event.index == gui::PRESET_NAME_SET_BUTTON &&
               event.value <= 1U) {
      commitNameEntry(now);
    }
    return;
  }
  if (event.object != gui::USERBUTTON_OBJECT || event.value > 1U) {
    return;
  }
  const bool setupRequest =
      (currentForm_ == gui::SQUARE_FORM &&
       event.index == gui::SQUARE_SETUP_BUTTON) ||
      (currentForm_ == gui::MONITOR_FORM &&
       event.index == gui::MONITOR_SETUP_BUTTON);
  if (setupRequest) {
    enterSetup(now, true);
    return;
  }
  if (!formVisible_ || !draftValid_) {
    return;
  }
  if (event.index == gui::SETUP_NAME_BUTTON) {
    openNameEntry();
    return;
  }
  if (event.index == gui::SETUP_PRESET_SELECT_BUTTON) {
    if (square::openPresetSelectorForSetup(now)) {
      formVisible_ = false;
      syncPending_ = false;
      draftValid_ = false;
      currentForm_ = gui::PRESET_SELECT_FORM;
    }
    return;
  }
  if (event.index == gui::SETUP_NEW_BUTTON) {
    makeNewDraft(now);
    return;
  }
  if (event.index == gui::SETUP_SAVE_BUTTON) {
    saveDraft();
    return;
  }
  if (event.index == gui::SETUP_DELETE_BUTTON) {
    deleteDraft(now);
    return;
  }
  if (event.index >= gui::SETUP_CHANNEL_BUTTON_FIRST &&
      event.index <= gui::SETUP_CHANNEL_BUTTON_LAST) {
    toggleChannel(event.index - gui::SETUP_CHANNEL_BUTTON_FIRST);
    return;
  }
  uint8_t group;
  uint8_t parameter;
  bool increase;
  if (findParameterButton(event.index, group, parameter, increase)) {
    adjustParameter(group, parameter, increase);
  }
}

void showSelectedPreset(const SquareWavePreset &preset, uint32_t now) {
  draft_ = preset;
  draftValid_ = true;
  formVisible_ = true;
  nameEntryVisible_ = false;
  currentForm_ = gui::SQUARE_SETUP_FORM;
  beginSync(now);
  DBG_SETUP("SELECT");
}
}
}
