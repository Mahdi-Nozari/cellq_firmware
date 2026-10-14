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
#include "WaveformTimer.h"

namespace {
constexpr uint8_t PARAMETER_COUNT = cellq::gui::SETUP_PARAMETER_COUNT;
constexpr uint8_t SYNC_STEP_COUNT =
    3U + cellq::hw::REGULATOR_COUNT +
    cellq::hw::REGULATOR_COUNT * PARAMETER_COUNT +
    cellq::hw::CHANNEL_COUNT + cellq::hw::REGULATOR_COUNT;
constexpr uint8_t MODE_SYNC_STEP_COUNT =
    cellq::hw::REGULATOR_COUNT * 3U;
constexpr uint8_t UNKNOWN_FORM = 0xFFU;
constexpr uint8_t TIME_FIELD_MINUTES = 0U;
constexpr uint8_t TIME_FIELD_HOURS = 1U;
constexpr uint8_t TIME_FIELD_HIDDEN = 2U;
constexpr uint32_t MAXIMUM_TIME_MINUTES =
    static_cast<uint32_t>(cellq::config::SETUP_TIME_MAX_HOURS) * 60UL + 59UL;
static_assert(cellq::config::SETUP_TIME_STEP_MINUTES > 0U &&
                  cellq::config::SETUP_TIME_STEP_MINUTES < 60U,
              "Time minute step must be between 1 and 59");
static_assert(cellq::config::SETUP_TIME_STEP_HOURS > 0U &&
                  cellq::config::SETUP_TIME_STEP_HOURS <=
                      cellq::config::SETUP_TIME_MAX_HOURS,
              "Time hour step must fit the hour range");

cellq::SquareWavePreset draft_ = {};
bool draftValid_ = false;
bool formVisible_ = false;
bool nameEntryVisible_ = false;
bool selectorDraftPending_ = false;
bool saveFeedbackActive_ = false;
uint32_t saveFeedbackStartedMs_ = 0;
bool syncPending_ = false;
bool modeSyncOnly_ = false;
uint8_t syncStep_ = 0;
uint8_t currentForm_ = UNKNOWN_FORM;
uint32_t lastSyncMs_ = 0;
char nameEntry_[21] = {};
uint8_t nameLength_ = 0;
bool cycleMode_ = false;
uint32_t enteredTimeMinutes_[cellq::hw::REGULATOR_COUNT] = {};
uint8_t selectedTimeField_[cellq::hw::REGULATOR_COUNT] = {};
bool timeDerivedFromCycles_[cellq::hw::REGULATOR_COUNT] = {};

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

void formatHoursMinutes(char *text, uint32_t totalMinutes) {
  const uint32_t hours = totalMinutes / 60UL;
  const uint8_t minutes = totalMinutes % 60UL;
  uint8_t position = 0;
  if (hours < 10UL) {
    text[position++] = '0';
  }
  position += appendUnsigned(text + position, hours);
  text[position++] = ':';
  text[position++] = static_cast<char>('0' + minutes / 10U);
  text[position++] = static_cast<char>('0' + minutes % 10U);
  text[position] = '\0';
}

void resetMode() {
  cycleMode_ = false;
  for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT; ++group) {
    enteredTimeMinutes_[group] = 0U;
    selectedTimeField_[group] = TIME_FIELD_MINUTES;
    timeDerivedFromCycles_[group] = false;
    draft_.totalCycles[group] = cellq::config::SETUP_DEFAULT_TOTAL_CYCLES;
  }
}

void writeTimeFieldImage(uint8_t group) {
  cellq::display::setSquareSetupTimeFieldImage(
      group, cycleMode_ || draft_.frequencyTenthsHz[group] == 0U
                 ? TIME_FIELD_HIDDEN
                 : selectedTimeField_[group]);
}

void updateCyclesFromTime(uint8_t group) {
  if (draft_.frequencyTenthsHz[group] == 0U) {
    return;
  }
  draft_.totalCycles[group] = cellq::waveform::cyclesForDurationMinutes(
      enteredTimeMinutes_[group], draft_.frequencyTenthsHz[group],
      draft_.delayMs[group]);
}

uint32_t displayedMinutesForCycles(uint8_t group) {
  const uint32_t minutes =
      cellq::waveform::durationMinutesForCyclesRoundedUp(
          draft_.totalCycles[group], draft_.frequencyTenthsHz[group],
          draft_.delayMs[group]);
  return minutes > MAXIMUM_TIME_MINUTES ? MAXIMUM_TIME_MINUTES : minutes;
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
  } else if (parameter == 3U) {
    formatHundredths(text, (draft_.delayMs[group] + 5UL) / 10UL);
  } else if (draft_.frequencyTenthsHz[group] == 0U) {
    strcpy(text, cycleMode_ ? "---" : "--:--");
  } else if (!cycleMode_) {
    formatHoursMinutes(text, enteredTimeMinutes_[group]);
  } else {
    const uint8_t position = appendUnsigned(text, draft_.totalCycles[group]);
    text[position] = '\0';
  }
  cellq::display::setSquareSetupLabel(
      cellq::gui::SETUP_PARAMETER_LABEL[group][parameter], text);
}

void beginSync(uint32_t now) {
  syncPending_ = true;
  modeSyncOnly_ = false;
  syncStep_ = 0;
  lastSyncMs_ = now - cellq::config::SETUP_DISPLAY_WRITE_INTERVAL_MS;
}

void beginModeSync(uint32_t now) {
  if (syncPending_ && !modeSyncOnly_) {
    beginSync(now);
    return;
  }
  syncPending_ = true;
  modeSyncOnly_ = true;
  syncStep_ = 0;
  lastSyncMs_ = now - cellq::config::SETUP_DISPLAY_WRITE_INTERVAL_MS;
}

void writeSyncStep() {
  if (modeSyncOnly_) {
    if (syncStep_ < cellq::hw::REGULATOR_COUNT) {
      cellq::display::setSquareSetupModeImage(syncStep_, !cycleMode_);
    } else if (syncStep_ < cellq::hw::REGULATOR_COUNT * 2U) {
      writeParameter(syncStep_ - cellq::hw::REGULATOR_COUNT,
                     PARAMETER_COUNT - 1U);
    } else {
      writeTimeFieldImage(syncStep_ -
                          cellq::hw::REGULATOR_COUNT * 2U);
    }
    return;
  }
  if (syncStep_ == 0U) {
    cellq::display::setSquareSetupSaveButton(saveFeedbackActive_);
    return;
  }
  if (syncStep_ == 1U) {
    cellq::display::setSquareSetupLabel(cellq::gui::SETUP_NAME_LABEL,
                                        draft_.name);
    return;
  }
  if (syncStep_ == 2U) {
    cellq::display::setSquareSetupCycleModeSwitch(cycleMode_);
    return;
  }
  if (syncStep_ < 3U + cellq::hw::REGULATOR_COUNT) {
    cellq::display::setSquareSetupModeImage(syncStep_ - 3U,
                                            !cycleMode_);
    return;
  }
  const uint8_t parameterSteps =
      cellq::hw::REGULATOR_COUNT * PARAMETER_COUNT;
  const uint8_t parameterStart = 3U + cellq::hw::REGULATOR_COUNT;
  if (syncStep_ < parameterStart + parameterSteps) {
    const uint8_t offset = syncStep_ - parameterStart;
    writeParameter(offset / PARAMETER_COUNT, offset % PARAMETER_COUNT);
    return;
  }
  const uint8_t channelStart = parameterStart + parameterSteps;
  if (syncStep_ < channelStart + cellq::hw::CHANNEL_COUNT) {
    const uint8_t channel = syncStep_ - channelStart;
    cellq::display::setSquareSetupChannel(
        channel, draft_.channelEnabled[channel] != 0U);
  } else {
    writeTimeFieldImage(syncStep_ -
                        channelStart - cellq::hw::CHANNEL_COUNT);
  }
}

bool loadDraft(uint32_t now, bool &fromActive) {
  cellq::SquareWavePreset candidate = {};
  fromActive = cellq::square::copyActivePreset(candidate);
  if (!fromActive &&
      !cellq::square::copyPresetForEditing(1U, candidate)) {
    return false;
  }
  draft_ = candidate;
  draftValid_ = true;
  resetMode();
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
  saveFeedbackActive_ = false;
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

void cancelNameEntry(uint32_t now, bool activateForm) {
  nameEntryVisible_ = false;
  formVisible_ = true;
  currentForm_ = cellq::gui::SQUARE_SETUP_FORM;
  strcpy(nameEntry_, draft_.name);
  nameLength_ = strlen(nameEntry_);
  if (activateForm) {
    cellq::display::activateForm(cellq::gui::SQUARE_SETUP_FORM);
  }
  beginSync(now);
  DBG_SETUP("NAME,CANCEL");
}

void restoreDraftAfterSelector(uint32_t now) {
  if (!selectorDraftPending_ || !draftValid_) {
    return;
  }
  selectorDraftPending_ = false;
  formVisible_ = true;
  nameEntryVisible_ = false;
  currentForm_ = cellq::gui::SQUARE_SETUP_FORM;
  beginSync(now);
  DBG_SETUP("SELECT,BACK");
}

void enterSetup(uint32_t now, bool activateForm) {
  if (cellq::square::isProcedureActive()) {
    DBG_SETUP("LOCKED");
    return;
  }
  formVisible_ = true;
  nameEntryVisible_ = false;
  selectorDraftPending_ = false;
  saveFeedbackActive_ = false;
  currentForm_ = cellq::gui::SQUARE_SETUP_FORM;
  if (activateForm) {
    cellq::display::activateForm(cellq::gui::SQUARE_SETUP_FORM);
    cellq::display::service();
  }
  bool fromActive = false;
  if (!loadDraft(now, fromActive)) {
    draftValid_ = false;
    syncPending_ = false;
    DBG_SETUP("LOAD,FAIL");
    return;
  }
  if (fromActive) {
    DBG_SETUP("LOAD,ACTIVE");
  } else {
    DBG_SETUP("LOAD,1");
  }
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
  resetMode();
  beginSync(now);
  DBG_SETUP("NEW");
}

void setCycleMode(bool cycleMode, uint32_t now) {
  if (cycleMode_ == cycleMode) {
    return;
  }
  if (!cycleMode) {
    for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT;
         ++group) {
      if (draft_.frequencyTenthsHz[group] != 0U) {
        enteredTimeMinutes_[group] = displayedMinutesForCycles(group);
      }
      selectedTimeField_[group] = TIME_FIELD_MINUTES;
      timeDerivedFromCycles_[group] = true;
    }
  } else {
    for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT;
         ++group) {
      timeDerivedFromCycles_[group] = false;
    }
  }
  cycleMode_ = cycleMode;
  beginModeSync(now);
  if (cycleMode_) {
    DBG_SETUP("MODE,CYCLES");
  } else {
    DBG_SETUP("MODE,TIME");
  }
}

void selectTimeField(uint8_t group) {
  if (cycleMode_ || draft_.frequencyTenthsHz[group] == 0U) {
    return;
  }
  selectedTimeField_[group] =
      selectedTimeField_[group] == TIME_FIELD_MINUTES
          ? TIME_FIELD_HOURS
          : TIME_FIELD_MINUTES;
  writeTimeFieldImage(group);
  if (group == 0U) {
    if (selectedTimeField_[group] == TIME_FIELD_HOURS) {
      DBG_SETUP("TIME,G1,HOUR");
    } else {
      DBG_SETUP("TIME,G1,MIN");
    }
  } else if (selectedTimeField_[group] == TIME_FIELD_HOURS) {
    DBG_SETUP("TIME,G2,HOUR");
  } else {
    DBG_SETUP("TIME,G2,MIN");
  }
}

void adjustEnteredTime(uint8_t group, bool increase) {
  uint16_t hours =
      static_cast<uint16_t>(enteredTimeMinutes_[group] / 60UL);
  uint8_t minutes =
      static_cast<uint8_t>(enteredTimeMinutes_[group] % 60UL);
  if (selectedTimeField_[group] == TIME_FIELD_HOURS) {
    const uint8_t step = cellq::config::SETUP_TIME_STEP_HOURS;
    const uint8_t maximum = cellq::config::SETUP_TIME_MAX_HOURS;
    if (increase) {
      hours = hours <= maximum - step ? hours + step : maximum;
    } else {
      hours = hours >= step ? hours - step : 0U;
    }
  } else {
    const uint8_t step = cellq::config::SETUP_TIME_STEP_MINUTES;
    minutes = increase
                  ? static_cast<uint8_t>((minutes + step) % 60U)
                  : static_cast<uint8_t>((minutes + 60U - step) % 60U);
  }
  enteredTimeMinutes_[group] =
      static_cast<uint32_t>(hours) * 60UL + minutes;
  timeDerivedFromCycles_[group] = false;
  updateCyclesFromTime(group);
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
    const bool wasContinuousOff =
        draft_.frequencyTenthsHz[group] == 0U;
    uint16_t value = draft_.frequencyTenthsHz[group];
    if (increase) {
      if (value < cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ) {
        value += cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ;
      }
    } else if (value >= cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ) {
      value -= cellq::config::SETUP_FREQUENCY_STEP_TENTHS_HZ;
    }
    draft_.frequencyTenthsHz[group] = value;
    if (!cycleMode_ && value != 0U) {
      if (wasContinuousOff && timeDerivedFromCycles_[group]) {
        enteredTimeMinutes_[group] = displayedMinutesForCycles(group);
      } else {
        updateCyclesFromTime(group);
        timeDerivedFromCycles_[group] = false;
      }
    }
    if (wasContinuousOff != (value == 0U)) {
      writeParameter(group, PARAMETER_COUNT - 1U);
      writeTimeFieldImage(group);
    }
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
  } else if (parameter == 3U) {
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
    if (!cycleMode_ && draft_.frequencyTenthsHz[group] != 0U) {
      updateCyclesFromTime(group);
      timeDerivedFromCycles_[group] = false;
    }
  } else {
    if (draft_.frequencyTenthsHz[group] == 0U) {
      return;
    }
    if (cycleMode_) {
      uint32_t value = draft_.totalCycles[group];
      const uint32_t step = cellq::config::SETUP_CYCLE_STEP;
      if (increase) {
        value = value <= UINT32_MAX - step ? value + step : UINT32_MAX;
      } else {
        value = value >= step ? value - step : 0U;
      }
      draft_.totalCycles[group] = value;
    } else {
      adjustEnteredTime(group, increase);
    }
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
    cellq::display::setSquareSetupSaveButton(false);
    DBG_SETUP("SAVE,READONLY");
    return;
  }
  char timestamp[16];
  cellq::clock::timestamp(timestamp);
  if (!cellq::preset::saveUserPreset(draft_, timestamp)) {
    cellq::display::setSquareSetupSaveButton(false);
    DBG_SETUP("SAVE,FAIL");
    return;
  }
  cellq::square::invalidatePresetList();
  cellq::square::setActivePreset(draft_);
  saveFeedbackActive_ = true;
  cellq::display::setSquareSetupSaveButton(true);
  saveFeedbackStartedMs_ = millis();
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
  cellq::square::setActivePreset(next);
  draft_ = next;
  draftValid_ = true;
  resetMode();
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
  selectorDraftPending_ = false;
  saveFeedbackActive_ = false;
  syncPending_ = false;
  modeSyncOnly_ = false;
  currentForm_ = UNKNOWN_FORM;
  resetMode();
}

void update(uint32_t now) {
  if (saveFeedbackActive_ &&
      static_cast<uint32_t>(now - saveFeedbackStartedMs_) >=
          config::SETUP_SAVE_FEEDBACK_MS) {
    saveFeedbackActive_ = false;
    if (formVisible_) {
      display::setSquareSetupSaveButton(false);
      return;
    }
  }
  if (!formVisible_ || !draftValid_ || !syncPending_ ||
      static_cast<uint32_t>(now - lastSyncMs_) <
          config::SETUP_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  lastSyncMs_ = now;
  writeSyncStep();
  ++syncStep_;
  if (syncStep_ >=
      (modeSyncOnly_ ? MODE_SYNC_STEP_COUNT : SYNC_STEP_COUNT)) {
    syncPending_ = false;
    if (!modeSyncOnly_) {
      DBG_SETUP("READY");
    }
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
        cancelNameEntry(now, false);
      } else if (selectorDraftPending_ && draftValid_) {
        restoreDraftAfterSelector(now);
      } else if (!formVisible_) {
        enterSetup(now, false);
      }
    } else if (event.index == gui::PRESET_NAME_FORM) {
      formVisible_ = false;
      saveFeedbackActive_ = false;
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
    } else if (event.index == gui::PRESET_SELECT_FORM &&
               selectorDraftPending_) {
      formVisible_ = false;
      nameEntryVisible_ = false;
      saveFeedbackActive_ = false;
      syncPending_ = false;
    } else {
      formVisible_ = false;
      nameEntryVisible_ = false;
      selectorDraftPending_ = false;
      saveFeedbackActive_ = false;
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
    } else if (event.object == gui::USERBUTTON_OBJECT &&
               event.index == gui::PRESET_NAME_BACK_BUTTON &&
               event.value <= 1U) {
      cancelNameEntry(now, true);
    }
    return;
  }
  if (event.object == gui::ISWITCH_OBJECT &&
      event.index == gui::SETUP_CYCLE_MODE_SWITCH) {
    if (formVisible_ && draftValid_ &&
        currentForm_ == gui::SQUARE_SETUP_FORM && event.value <= 1U) {
      setCycleMode(event.value != 0U, now);
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
    if (currentForm_ == gui::SQUARE_FORM &&
        !square::isProcedureActive()) {
      square::leaveRunPage();
    }
    enterSetup(now, true);
    return;
  }
  if (!formVisible_ || !draftValid_) {
    return;
  }
  for (uint8_t group = 0; group < hw::REGULATOR_COUNT; ++group) {
    if (event.index == gui::SETUP_TIME_FIELD_BUTTON[group]) {
      if (event.value == 0U &&
          currentForm_ == gui::SQUARE_SETUP_FORM) {
        selectTimeField(group);
      }
      return;
    }
  }
  if (event.index == gui::SETUP_NAME_BUTTON) {
    openNameEntry();
    return;
  }
  if (event.index == gui::SETUP_PRESET_SELECT_BUTTON) {
    if (square::openPresetSelectorForSetup(now)) {
      formVisible_ = false;
      saveFeedbackActive_ = false;
      syncPending_ = false;
      selectorDraftPending_ = true;
      currentForm_ = gui::PRESET_SELECT_FORM;
    }
    return;
  }
  if (event.index == gui::SETUP_NEW_BUTTON) {
    makeNewDraft(now);
    return;
  }
  if (event.index == gui::SETUP_SAVE_BUTTON) {
    if (event.value == 1U && !saveFeedbackActive_) {
      saveDraft();
    }
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
  selectorDraftPending_ = false;
  saveFeedbackActive_ = false;
  currentForm_ = gui::SQUARE_SETUP_FORM;
  resetMode();
  beginSync(now);
  DBG_SETUP("SELECT");
}

void returnFromPresetSelector(uint32_t now) {
  restoreDraftAfterSelector(now);
}
}
}
