#include "SquareWaveMode.h"
#include <avr/pgmspace.h>
#include <string.h>
#include "BuildConfig.h"
#include "DataLogger.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "PresetStore.h"
#include "Regulator.h"
#include "SquareWavePreset.h"
#include "SquareWaveSetup.h"
#include "WaveformTimer.h"

namespace {
enum class ProcedureState : uint8_t {
  Idle,
  Running,
  PausePending,
  Paused,
  Done,
  Error
};

enum class PresetSelectDestination : uint8_t { Run, Setup };
enum class FailureCause : uint8_t { Preset, Dac, Logging };

constexpr uint8_t GROUP_COUNT = cellq::hw::REGULATOR_COUNT;
const cellq::SquareWavePreset FACTORY_PRESET_1 PROGMEM = {
    1,
    1,
    "default preset 1",
    {-20, -20},
    {1, 1, 0, 1, 1, 0},
    {10, 10},
    {50, 50},
    {0, 0},
    {30, 30},
    {},
    {}};
const cellq::SquareWavePreset FACTORY_PRESET_2 PROGMEM = {
    2,
    1,
    "default preset 2",
    {-25, -30},
    {1, 1, 0, 1, 1, 0},
    {10, 8},
    {40, 60},
    {500, 1000},
    {25, 20},
    {},
    {}};

constexpr uint8_t STATUS_IDLE_IMAGE = 0;
constexpr uint8_t STATUS_RUNNING_IMAGE = 1;
constexpr uint8_t STATUS_DONE_IMAGE = 2;
constexpr uint8_t STATUS_ERROR_IMAGE = 3;
constexpr uint8_t STATUS_PAUSED_IMAGE = 4;
constexpr uint8_t FULL_DISPLAY_SYNC_STEP_COUNT = 25;
constexpr uint8_t RUNTIME_DISPLAY_SYNC_STEP_COUNT = 10;
constexpr int16_t PROGRESS_NOT_SENT = -2;
constexpr int16_t PROGRESS_BLANK = -1;
constexpr char UNAVAILABLE_PRESET_NAME[] = "---";
static_assert(cellq::gui::PRESET_SELECT_BUTTON_LAST -
                      cellq::gui::PRESET_SELECT_BUTTON_FIRST +
                  1U ==
              cellq::config::SQUARE_PRESET_SLOT_COUNT,
              "Preset selector button count must match preset slots");

cellq::SquareWavePreset activePreset_ = {};
ProcedureState state_ = ProcedureState::Idle;
bool form2Visible_ = false;
bool presetSelectVisible_ = false;
PresetSelectDestination presetSelectDestination_ =
    PresetSelectDestination::Run;
bool presetLoaded_ = false;
bool presetPageSynchronized_ = false;
bool pauseAtCycleBoundary_ = false;
uint8_t progressGroup_ = 0;
uint32_t completedCycles_[GROUP_COUNT] = {};
uint64_t remainingTimeMs_[GROUP_COUNT] = {};
uint32_t displayedMinute_[GROUP_COUNT] = {UINT32_MAX, UINT32_MAX};
uint32_t displayedRemainingCycles_[GROUP_COUNT] = {UINT32_MAX, UINT32_MAX};
int16_t displayedProgress_ = PROGRESS_NOT_SENT;
uint32_t lastRuntimeDisplayMs_ = 0;
bool displaySyncPending_ = false;
bool fullDisplaySync_ = false;
uint8_t displaySyncStep_ = 0;
uint32_t displaySyncLastMs_ = 0;
bool scopePreviewPending_ = false;
uint8_t scopePreviewGroup_ = 0;
uint16_t scopeSample_ = 0;
uint32_t scopeLastWriteMs_ = 0;
char presetNames_[cellq::config::SQUARE_PRESET_SLOT_COUNT][21] = {};
bool presetAvailable_[cellq::config::SQUARE_PRESET_SLOT_COUNT] = {};
bool presetListCached_ = false;
bool presetListScanPending_ = false;
bool presetListWritePending_ = false;
uint8_t presetListSlot_ = 0;
uint32_t presetListLastWriteMs_ = 0;

uint8_t appendUnsigned(char *text, uint32_t value) {
  char reverse[10];
  uint8_t count = 0;
  do {
    reverse[count++] = static_cast<char>('0' + value % 10UL);
    value /= 10UL;
  } while (value > 0);
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

void formatHoursMinutes(char *text, uint64_t milliseconds) {
  const uint32_t totalMinutes =
      static_cast<uint32_t>(milliseconds / 60000ULL);
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

uint64_t configuredTimeMs(uint8_t group) {
  if (activePreset_.frequencyTenthsHz[group] == 0U ||
      activePreset_.totalCycles[group] == 0U) {
    return UINT64_MAX;
  }
  return activePreset_.delayMs[group] +
         static_cast<uint64_t>(activePreset_.totalCycles[group]) *
             10000ULL / activePreset_.frequencyTenthsHz[group];
}

uint32_t remainingCycles(uint8_t group) {
  if (activePreset_.totalCycles[group] == 0U) {
    return UINT32_MAX;
  }
  return completedCycles_[group] < activePreset_.totalCycles[group]
             ? activePreset_.totalCycles[group] - completedCycles_[group]
             : 0U;
}

void resetCountdowns() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    completedCycles_[group] = 0;
    remainingTimeMs_[group] = configuredTimeMs(group);
  }
  const uint64_t group1Time = configuredTimeMs(0);
  const uint64_t group2Time = configuredTimeMs(1);
  progressGroup_ = group2Time > group1Time ? 1U : 0U;
}

void copyFactoryPreset(uint16_t id, cellq::SquareWavePreset &preset) {
  const cellq::SquareWavePreset *source =
      id == 2U ? &FACTORY_PRESET_2 : &FACTORY_PRESET_1;
  memcpy_P(&preset, source, sizeof(preset));
}

bool findPresetForList(uint16_t id, cellq::SquareWavePreset &preset) {
  if (id <= 2U) {
#if CELLQ_LOAD_FACTORY_PRESET_FROM_SD
    if (cellq::preset::findPreset(id, preset)) {
      return true;
    }
#endif
    copyFactoryPreset(id, preset);
    return true;
  }
  return cellq::preset::findPreset(id, preset);
}

bool loadSelectedPreset(uint16_t id, cellq::SquareWavePreset &preset) {
  if (id <= 2U) {
#if CELLQ_LOAD_FACTORY_PRESET_FROM_SD
    if (cellq::preset::loadPreset(id, preset)) {
      return true;
    }
#endif
    copyFactoryPreset(id, preset);
    DBG_PRESET_FLASH(id);
    return true;
  }
  return cellq::preset::loadPreset(id, preset);
}

void beginPresetList(uint32_t now) {
  presetListScanPending_ = !presetListCached_;
  presetListWritePending_ = presetListCached_;
  presetListSlot_ = 0;
  presetListLastWriteMs_ =
      now - cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS;
}

void openPresetSelector(uint32_t now,
                        PresetSelectDestination destination) {
  form2Visible_ = false;
  displaySyncPending_ = false;
  scopePreviewPending_ = false;
  presetSelectDestination_ = destination;
  presetSelectVisible_ = true;
  beginPresetList(now);
  cellq::display::activateForm(cellq::gui::PRESET_SELECT_FORM);
}

void scanNextPresetSlot(uint32_t now) {
  if (!presetListScanPending_) {
    return;
  }
  const uint16_t id = static_cast<uint16_t>(presetListSlot_) + 1U;
  cellq::SquareWavePreset candidate = {};
  presetAvailable_[presetListSlot_] = findPresetForList(id, candidate);
  strcpy(presetNames_[presetListSlot_],
         presetAvailable_[presetListSlot_] ? candidate.name
                                          : UNAVAILABLE_PRESET_NAME);
  ++presetListSlot_;
  if (presetListSlot_ < cellq::config::SQUARE_PRESET_SLOT_COUNT) {
    return;
  }
  presetListCached_ = true;
  presetListScanPending_ = false;
  presetListWritePending_ = true;
  presetListSlot_ = 0;
  presetListLastWriteMs_ =
      now - cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS;
}

void updatePresetListDisplay(uint32_t now) {
  if (!presetListWritePending_ ||
      static_cast<uint32_t>(now - presetListLastWriteMs_) <
          cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  presetListLastWriteMs_ = now;
  cellq::display::setPresetSelectName(
      presetListSlot_, presetNames_[presetListSlot_]);
  ++presetListSlot_;
  if (presetListSlot_ >= cellq::config::SQUARE_PRESET_SLOT_COUNT) {
    presetListWritePending_ = false;
    DBG_SQUARE("LIST,READY");
  }
}

void refreshRuntimeSnapshot() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    completedCycles_[group] = cellq::waveform::completedCycles(group);
    remainingTimeMs_[group] =
        cellq::waveform::remainingMilliseconds(group);
  }
}

uint8_t statusImage() {
  switch (state_) {
    case ProcedureState::Running:
    case ProcedureState::PausePending:
      return STATUS_RUNNING_IMAGE;
    case ProcedureState::Paused:
      return STATUS_PAUSED_IMAGE;
    case ProcedureState::Done:
      return STATUS_DONE_IMAGE;
    case ProcedureState::Error:
      return STATUS_ERROR_IMAGE;
    default:
      return STATUS_IDLE_IMAGE;
  }
}

bool procedureActive() {
  return state_ == ProcedureState::Running ||
         state_ == ProcedureState::PausePending ||
         state_ == ProcedureState::Paused;
}

bool procedureAdvancing() {
  return state_ == ProcedureState::Running ||
         state_ == ProcedureState::PausePending;
}

cellq::logger::State loggerState() {
  if (state_ == ProcedureState::PausePending) {
    return cellq::logger::State::PausePending;
  }
  if (state_ == ProcedureState::Paused) {
    return cellq::logger::State::Paused;
  }
  return cellq::logger::State::Running;
}

void idleOutputs() {
  cellq::waveform::stop();
  cellq::regulator::idle();
}

bool presetValid(const cellq::SquareWavePreset &preset) {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (preset.pressureTenths[group] <
            cellq::config::SQUARE_PRESSURE_MIN_TENTHS ||
        preset.pressureTenths[group] >
            cellq::config::SQUARE_PRESSURE_MAX_TENTHS ||
        preset.frequencyTenthsHz[group] >
            cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ ||
        (preset.frequencyTenthsHz[group] != 0U &&
         preset.frequencyTenthsHz[group] <
             cellq::config::SQUARE_FREQUENCY_MIN_TENTHS_HZ) ||
        preset.dutyPercent[group] > 100U ||
        preset.delayMs[group] >
            cellq::config::SQUARE_DELAY_MAX_MS) {
      return false;
    }
  }
  return true;
}

uint8_t enabledChannelMask(uint8_t group) {
  uint8_t mask = 0;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    if (cellq::hw::CHANNEL_TO_REGULATOR[channel] == group &&
        activePreset_.channelEnabled[channel] != 0U) {
      mask |= static_cast<uint8_t>(1U << channel);
    }
  }
  return mask;
}

void writeRemainingTime(uint8_t group) {
  char text[16];
  if (remainingTimeMs_[group] == UINT64_MAX) {
    strcpy(text, "--:--");
    displayedMinute_[group] = UINT32_MAX - 1UL;
  } else {
    formatHoursMinutes(text, remainingTimeMs_[group]);
    displayedMinute_[group] =
        static_cast<uint32_t>(remainingTimeMs_[group] / 60000ULL);
  }
  cellq::display::setSquareLabel(
      cellq::gui::SQUARE_REMAINING_TIME_LABEL[group], text);
}

void writeRemainingCycles(uint8_t group) {
  char text[11];
  const uint32_t cycles = remainingCycles(group);
  if (cycles == UINT32_MAX) {
    strcpy(text, "---");
  } else {
    const uint8_t length = appendUnsigned(text, cycles);
    text[length] = '\0';
  }
  cellq::display::setSquareLabel(
      cellq::gui::SQUARE_REMAINING_CYCLES_LABEL[group], text);
  displayedRemainingCycles_[group] = cycles;
}

void writeProgress() {
  int16_t progress = PROGRESS_BLANK;
  const uint32_t totalCycles = activePreset_.totalCycles[progressGroup_];
  if (procedureAdvancing() && totalCycles != 0U &&
      activePreset_.frequencyTenthsHz[progressGroup_] != 0U) {
    const uint64_t scaled =
        static_cast<uint64_t>(completedCycles_[progressGroup_]) * 100ULL;
    progress = static_cast<int16_t>(scaled / totalCycles);
    if (progress > 100) {
      progress = 100;
    }
  }
  if (progress == displayedProgress_) {
    return;
  }
  char text[5];
  if (progress == PROGRESS_BLANK) {
    text[0] = '\0';
  } else {
    uint8_t position =
        appendUnsigned(text, static_cast<uint16_t>(progress));
    text[position++] = '%';
    text[position] = '\0';
  }
  cellq::display::setSquareLabel(cellq::gui::SQUARE_PROGRESS_LABEL, text);
  displayedProgress_ = progress;
}

void writeProcedureState() {
  cellq::display::setSquareStatus(statusImage());
  cellq::display::setSquareStartButton(procedureActive());
  cellq::display::setSquarePauseButton(state_ == ProcedureState::Paused);
  writeProgress();
}

void writeAllCountdowns() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    writeRemainingTime(group);
    writeRemainingCycles(group);
  }
}

void failProcedure(FailureCause cause, uint32_t now);

void stopProcedure(bool updateDisplay, uint32_t now) {
  idleOutputs();
  if (!cellq::logger::finish(now, cellq::logger::EndReason::Stopped,
                             completedCycles_)) {
    failProcedure(FailureCause::Logging, now);
    return;
  }
  state_ = ProcedureState::Idle;
  resetCountdowns();
  if (updateDisplay) {
    writeAllCountdowns();
    writeProcedureState();
  }
  DBG_SQUARE("STOP");
}

void finishProcedure(uint32_t now) {
  cellq::regulator::idle();
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    completedCycles_[group] = activePreset_.totalCycles[group];
    remainingTimeMs_[group] = 0;
  }
  if (!cellq::logger::finish(now, cellq::logger::EndReason::Done,
                             completedCycles_)) {
    failProcedure(FailureCause::Logging, now);
    return;
  }
  state_ = ProcedureState::Done;
  if (form2Visible_) {
    writeAllCountdowns();
    writeProcedureState();
  }
  DBG_SQUARE("DONE");
}

void failProcedure(FailureCause cause, uint32_t now) {
  idleOutputs();
  if (cause == FailureCause::Logging) {
    cellq::logger::abort();
  } else if (!cellq::logger::finish(
                 now, cellq::logger::EndReason::Error,
                 completedCycles_)) {
    cellq::logger::abort();
    cause = FailureCause::Logging;
  }
  state_ = ProcedureState::Error;
  resetCountdowns();
  if (form2Visible_) {
    if (cause == FailureCause::Logging) {
      cellq::display::setSquareDataLogSwitch(false);
    }
    writeAllCountdowns();
    writeProcedureState();
  }
  if (cause == FailureCause::Dac) {
    DBG_SQUARE("ERR,DAC");
  } else if (cause == FailureCause::Logging) {
    DBG_SQUARE("ERR,LOG");
  } else {
    DBG_SQUARE("ERR,PRESET");
  }
}

void updateRuntimeDisplay(uint32_t now) {
  if (static_cast<uint32_t>(now - lastRuntimeDisplayMs_) <
      cellq::config::SQUARE_RUNTIME_DISPLAY_INTERVAL_MS) {
    return;
  }
  lastRuntimeDisplayMs_ = now;
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    const uint32_t minute =
        remainingTimeMs_[group] == UINT64_MAX
            ? UINT32_MAX - 1UL
            : static_cast<uint32_t>(remainingTimeMs_[group] / 60000ULL);
    if (minute != displayedMinute_[group]) {
      writeRemainingTime(group);
    }
    const uint32_t cycles = remainingCycles(group);
    if (cycles != displayedRemainingCycles_[group]) {
      writeRemainingCycles(group);
    }
  }
  writeProgress();
}

void startProcedure(uint32_t now) {
  if (!presetLoaded_ || !presetValid(activePreset_)) {
    failProcedure(FailureCause::Preset, now);
    return;
  }
  if (!cellq::logger::startTest(activePreset_, now)) {
    failProcedure(FailureCause::Logging, now);
    return;
  }
  bool regulatorsOk = true;
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    regulatorsOk = cellq::regulator::setPressureTenths(
                       group, activePreset_.pressureTenths[group]) &&
                   regulatorsOk;
  }
  if (!regulatorsOk) {
#if CELLQ_REQUIRE_DAC_ACK
    failProcedure(FailureCause::Dac, now);
    return;
#else
    DBG_SQUARE("WARN,DAC,IGNORED");
#endif
  }

  cellq::waveform::Config waveformConfig = {};
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    waveformConfig.group[group] = {
        enabledChannelMask(group), activePreset_.frequencyTenthsHz[group],
        activePreset_.dutyPercent[group], activePreset_.delayMs[group],
        activePreset_.totalCycles[group]};
  }
  if (!cellq::waveform::start(waveformConfig)) {
    failProcedure(FailureCause::Preset, now);
    return;
  }
  state_ = ProcedureState::Running;
  resetCountdowns();
  lastRuntimeDisplayMs_ = now;
  writeAllCountdowns();
  writeProcedureState();
  DBG_SQUARE("START");
}

void pauseProcedure() {
  if (pauseAtCycleBoundary_) {
    cellq::waveform::pauseAtCycleBoundary();
    state_ = ProcedureState::PausePending;
    DBG_SQUARE("PAUSE,WAIT");
    return;
  }
  cellq::waveform::pause();
  refreshRuntimeSnapshot();
  state_ = ProcedureState::Paused;
  cellq::display::setSquareStatus(statusImage());
  cellq::display::setSquarePauseButton(true);
  writeProgress();
  DBG_SQUARE("PAUSE");
}

void completeBoundaryPause() {
  refreshRuntimeSnapshot();
  state_ = ProcedureState::Paused;
  if (form2Visible_) {
    writeAllCountdowns();
    cellq::display::setSquareStatus(statusImage());
    cellq::display::setSquarePauseButton(true);
    writeProgress();
  }
  DBG_SQUARE("PAUSE");
}

void resumeProcedure(uint32_t now) {
  cellq::waveform::resume();
  state_ = ProcedureState::Running;
  lastRuntimeDisplayMs_ = now;
  cellq::display::setSquareStatus(statusImage());
  cellq::display::setSquarePauseButton(false);
  writeProgress();
  DBG_SQUARE("RESUME");
}

void startDisplaySync(uint32_t now, bool fullSync) {
  displaySyncPending_ = true;
  fullDisplaySync_ = fullSync;
  displaySyncStep_ = 0;
  displaySyncLastMs_ =
      now - cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS;
  scopePreviewPending_ = false;
  scopePreviewGroup_ = 0;
  scopeSample_ = 0;
}

void writeGroupParameter(uint8_t group, uint8_t parameter) {
  const uint8_t labels[GROUP_COUNT][4] = {
      {cellq::gui::SQUARE_GROUP_1_PRESSURE_LABEL,
       cellq::gui::SQUARE_GROUP_1_FREQUENCY_LABEL,
       cellq::gui::SQUARE_GROUP_1_ON_TIME_LABEL,
       cellq::gui::SQUARE_GROUP_1_DELAY_LABEL},
      {cellq::gui::SQUARE_GROUP_2_PRESSURE_LABEL,
       cellq::gui::SQUARE_GROUP_2_FREQUENCY_LABEL,
       cellq::gui::SQUARE_GROUP_2_ON_TIME_LABEL,
       cellq::gui::SQUARE_GROUP_2_DELAY_LABEL}};
  char text[16];
  if (parameter == 0U) {
    formatSignedTenths(text, activePreset_.pressureTenths[group]);
  } else if (parameter == 1U) {
    formatUnsignedTenths(text, activePreset_.frequencyTenthsHz[group]);
  } else if (parameter == 2U) {
    uint32_t onTimeHundredths = 0;
    if (activePreset_.frequencyTenthsHz[group] != 0U) {
      onTimeHundredths =
          (static_cast<uint32_t>(activePreset_.dutyPercent[group]) * 10UL +
           activePreset_.frequencyTenthsHz[group] / 2U) /
          activePreset_.frequencyTenthsHz[group];
    }
    formatHundredths(text, onTimeHundredths);
  } else {
    formatHundredths(text, (activePreset_.delayMs[group] + 5UL) / 10UL);
  }
  cellq::display::setSquareLabel(labels[group][parameter], text);
}

void writeFullDisplaySyncStep() {
  if (displaySyncStep_ == 0U) {
    cellq::display::setSquareLabel(cellq::gui::SQUARE_NAME_LABEL,
                                   activePreset_.name);
    return;
  }
  if (displaySyncStep_ <= 8U) {
    const uint8_t offset = displaySyncStep_ - 1U;
    writeGroupParameter(offset / 4U, offset % 4U);
    return;
  }
  if (displaySyncStep_ <= 10U) {
    writeRemainingTime(displaySyncStep_ - 9U);
    return;
  }
  if (displaySyncStep_ <= 12U) {
    writeRemainingCycles(displaySyncStep_ - 11U);
    return;
  }
  if (displaySyncStep_ <= 18U) {
    const uint8_t channel = displaySyncStep_ - 13U;
    cellq::display::setSquareChannelEnabled(
        channel, activePreset_.channelEnabled[channel] != 0U);
    return;
  }
  switch (displaySyncStep_) {
    case 19:
      cellq::display::setSquareStatus(statusImage());
      break;
    case 20:
      cellq::display::setSquareStartButton(procedureActive());
      break;
    case 21:
      cellq::display::setSquarePauseButton(
          state_ == ProcedureState::Paused);
      break;
    case 22:
      cellq::display::setSquarePauseAtCycleSwitch(
          pauseAtCycleBoundary_);
      break;
    case 23:
      cellq::display::setSquareDataLogSwitch(cellq::logger::enabled());
      break;
    default:
      writeProgress();
      break;
  }
}

void writeRuntimeDisplaySyncStep() {
  if (displaySyncStep_ <= 1U) {
    writeRemainingTime(displaySyncStep_);
    return;
  }
  if (displaySyncStep_ <= 3U) {
    writeRemainingCycles(displaySyncStep_ - 2U);
    return;
  }
  switch (displaySyncStep_) {
    case 4:
      cellq::display::setSquareStatus(statusImage());
      break;
    case 5:
      cellq::display::setSquareStartButton(procedureActive());
      break;
    case 6:
      cellq::display::setSquarePauseButton(
          state_ == ProcedureState::Paused);
      break;
    case 7:
      cellq::display::setSquarePauseAtCycleSwitch(
          pauseAtCycleBoundary_);
      break;
    case 8:
      cellq::display::setSquareDataLogSwitch(cellq::logger::enabled());
      break;
    default:
      writeProgress();
      break;
  }
}

void updateDisplaySync(uint32_t now) {
  if (!displaySyncPending_ ||
      static_cast<uint32_t>(now - displaySyncLastMs_) <
          cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  displaySyncLastMs_ = now;
  if (fullDisplaySync_) {
    writeFullDisplaySyncStep();
  } else {
    writeRuntimeDisplaySyncStep();
  }
  ++displaySyncStep_;
  const uint8_t stepCount =
      fullDisplaySync_ ? FULL_DISPLAY_SYNC_STEP_COUNT
                       : RUNTIME_DISPLAY_SYNC_STEP_COUNT;
  if (displaySyncStep_ < stepCount) {
    return;
  }
  displaySyncPending_ = false;
  if (fullDisplaySync_) {
    scopePreviewPending_ = true;
    scopePreviewGroup_ = 0;
    scopeSample_ = 0;
    scopeLastWriteMs_ = now;
  } else {
    cellq::display::setSquarePresetReady(true);
    DBG_SQUARE("READY");
  }
}

uint16_t previewValue(uint8_t group, uint16_t sample) {
  if (activePreset_.frequencyTenthsHz[group] == 0U ||
      activePreset_.dutyPercent[group] == 0U) {
    return cellq::config::SQUARE_SCOPE_LOW_VALUE;
  }
  const uint32_t sampleTimeMs =
      static_cast<uint32_t>(sample) *
      cellq::config::SQUARE_SCOPE_PREVIEW_MS /
      cellq::config::SQUARE_SCOPE_SAMPLE_COUNT;
  const uint32_t phase =
      sampleTimeMs * activePreset_.frequencyTenthsHz[group] % 10000UL;
  return activePreset_.dutyPercent[group] == 100U ||
                 phase <
                     static_cast<uint32_t>(activePreset_.dutyPercent[group]) *
                         100UL
             ? cellq::config::SQUARE_SCOPE_HIGH_VALUE
             : cellq::config::SQUARE_SCOPE_LOW_VALUE;
}

void finishPageSync() {
  scopePreviewPending_ = false;
  presetPageSynchronized_ = true;
  if (procedureAdvancing()) {
    refreshRuntimeSnapshot();
    updateRuntimeDisplay(lastRuntimeDisplayMs_ +
                         cellq::config::SQUARE_RUNTIME_DISPLAY_INTERVAL_MS);
  }
  cellq::display::setSquarePresetReady(true);
  DBG_SQUARE("READY");
}

void updateScopePreview(uint32_t now) {
  if (!scopePreviewPending_ ||
      static_cast<uint32_t>(now - scopeLastWriteMs_) <
          cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  scopeLastWriteMs_ = now;
  cellq::display::addSquareScopeSample(
      scopePreviewGroup_, previewValue(scopePreviewGroup_, scopeSample_));
  ++scopeSample_;
  if (scopeSample_ < cellq::config::SQUARE_SCOPE_SAMPLE_COUNT) {
    return;
  }
  scopeSample_ = 0;
  ++scopePreviewGroup_;
  if (scopePreviewGroup_ >= GROUP_COUNT) {
    finishPageSync();
  }
}

void beginPageSync(uint32_t now, bool fullSync) {
  cellq::display::setSquarePresetReady(false);
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    displayedMinute_[group] = UINT32_MAX;
    displayedRemainingCycles_[group] = UINT32_MAX;
  }
  displayedProgress_ = PROGRESS_NOT_SENT;
  lastRuntimeDisplayMs_ = now;
  startDisplaySync(now, fullSync);
}

void loadPreset1(uint32_t now) {
  bool loadedFromSd = false;
#if CELLQ_LOAD_FACTORY_PRESET_FROM_SD
  loadedFromSd = cellq::preset::loadFactoryPreset1(activePreset_);
#endif
  if (!loadedFromSd) {
    copyFactoryPreset(1U, activePreset_);
  }
  presetLoaded_ = true;
  presetPageSynchronized_ = false;
  state_ = ProcedureState::Idle;
  resetCountdowns();
  beginPageSync(now, true);
  if (loadedFromSd) {
    DBG_SQUARE("LOAD,1,SD");
  } else {
    DBG_PRESET_FLASH(1U);
  }
}

void openSquarePage(uint32_t now) {
  form2Visible_ = true;
  presetSelectVisible_ = false;
  presetListScanPending_ = false;
  presetListWritePending_ = false;
  if (displaySyncPending_ || scopePreviewPending_) {
    return;
  }
  if (!presetLoaded_) {
    loadPreset1(now);
  } else if (!presetPageSynchronized_) {
    beginPageSync(now, true);
    DBG_PRESET_CACHE(activePreset_.id);
  } else if (procedureActive() || state_ == ProcedureState::Done ||
             state_ == ProcedureState::Error) {
    if (procedureActive()) {
      refreshRuntimeSnapshot();
    }
    beginPageSync(now, false);
    DBG_PRESET_CACHE(activePreset_.id);
  } else {
    cellq::display::setSquarePresetReady(true);
    DBG_PRESET_CACHE(activePreset_.id);
  }
}

void selectPreset(uint8_t slot, uint32_t now) {
  if (procedureActive() || !presetListCached_ ||
      slot >= cellq::config::SQUARE_PRESET_SLOT_COUNT ||
      !presetAvailable_[slot]) {
    return;
  }
  const uint16_t id = static_cast<uint16_t>(slot) + 1U;
  cellq::SquareWavePreset candidate = {};
  if (!loadSelectedPreset(id, candidate)) {
    presetAvailable_[slot] = false;
    strcpy(presetNames_[slot], UNAVAILABLE_PRESET_NAME);
    cellq::display::setPresetSelectName(slot, UNAVAILABLE_PRESET_NAME);
    return;
  }

  presetSelectVisible_ = false;
  presetListScanPending_ = false;
  presetListWritePending_ = false;
  if (presetSelectDestination_ == PresetSelectDestination::Setup) {
    cellq::display::activateForm(cellq::gui::SQUARE_SETUP_FORM);
    cellq::setup::showSelectedPreset(candidate, now);
    DBG_SQUARE("SELECT,SETUP");
    return;
  }

  activePreset_ = candidate;
  presetLoaded_ = true;
  presetPageSynchronized_ = false;
  state_ = ProcedureState::Idle;
  resetCountdowns();
  form2Visible_ = true;
  cellq::display::activateForm(cellq::gui::SQUARE_FORM);
  beginPageSync(now, true);
}
}

namespace cellq {
namespace square {
void begin() {
  waveform::begin();
  logger::begin();
  state_ = ProcedureState::Idle;
  form2Visible_ = false;
  presetSelectVisible_ = false;
  presetSelectDestination_ = PresetSelectDestination::Run;
  presetLoaded_ = false;
  presetPageSynchronized_ = false;
  pauseAtCycleBoundary_ = false;
  displayedProgress_ = PROGRESS_NOT_SENT;
  displaySyncPending_ = false;
  scopePreviewPending_ = false;
  presetListCached_ = false;
  presetListScanPending_ = false;
  presetListWritePending_ = false;
}

void update(uint32_t now) {
  if (procedureAdvancing()) {
    refreshRuntimeSnapshot();
    if (waveform::takeFinished()) {
      for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
        waveform::takeGroupFinished(group);
      }
      finishProcedure(now);
    } else {
      for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
        if (waveform::takeGroupFinished(group)) {
          regulator::idle(group);
        }
      }
      if (waveform::takeBoundaryPaused()) {
        completeBoundaryPause();
      } else if (form2Visible_ && !displaySyncPending_ &&
                 !scopePreviewPending_) {
        updateRuntimeDisplay(now);
      }
    }
  }
  if (procedureActive() &&
      !logger::update(now, loggerState(), completedCycles_)) {
    failProcedure(FailureCause::Logging, now);
  }
  if (presetSelectVisible_) {
    scanNextPresetSlot(now);
    updatePresetListDisplay(now);
  }
  if (!form2Visible_) {
    return;
  }
  updateDisplaySync(now);
  updateScopePreview(now);
}

void handleDisplayEvent(const display::Event &event, uint32_t now) {
  if (event.object == gui::FORM_OBJECT) {
    if (event.index == gui::SQUARE_FORM) {
      openSquarePage(now);
    } else if (event.index == gui::PRESET_SELECT_FORM) {
      form2Visible_ = false;
      displaySyncPending_ = false;
      scopePreviewPending_ = false;
      if (procedureActive()) {
        presetSelectVisible_ = false;
        cellq::display::activateForm(gui::SQUARE_FORM);
        openSquarePage(now);
        DBG_SQUARE("SELECT,LOCKED");
      } else {
        if (!presetSelectVisible_) {
          presetSelectDestination_ = PresetSelectDestination::Run;
          presetSelectVisible_ = true;
          beginPresetList(now);
        }
      }
    } else {
      form2Visible_ = false;
      presetSelectVisible_ = false;
      displaySyncPending_ = false;
      scopePreviewPending_ = false;
      presetListScanPending_ = false;
      presetListWritePending_ = false;
      presetSelectDestination_ = PresetSelectDestination::Run;
    }
    return;
  }
  if (presetSelectVisible_ && event.object == gui::USERBUTTON_OBJECT &&
      event.index >= gui::PRESET_SELECT_BUTTON_FIRST &&
      event.index <= gui::PRESET_SELECT_BUTTON_LAST && event.value <= 1U) {
    selectPreset(event.index - gui::PRESET_SELECT_BUTTON_FIRST, now);
    return;
  }
  if (!form2Visible_ || event.value > 1U) {
    return;
  }
  if (event.object == gui::ISWITCH_OBJECT &&
      event.index == gui::SQUARE_PAUSE_AT_CYCLE_SWITCH) {
    pauseAtCycleBoundary_ = event.value != 0U;
    return;
  }
  if (event.object == gui::ISWITCH_OBJECT &&
      event.index == gui::SQUARE_DATA_LOG_SWITCH) {
    if (!logger::setEnabled(event.value != 0U, now)) {
      failProcedure(FailureCause::Logging, now);
    }
    return;
  }
  if (event.object != gui::USERBUTTON_OBJECT) {
    return;
  }
  if (event.index == gui::SQUARE_PRESET_SELECT_BUTTON) {
    if (procedureActive()) {
      DBG_SQUARE("SELECT,LOCKED");
    } else {
      openPresetSelector(now, PresetSelectDestination::Run);
    }
    return;
  }
  if (event.index == gui::SQUARE_START_BUTTON) {
    if (procedureActive()) {
      stopProcedure(true, now);
    } else {
      startProcedure(now);
    }
    return;
  }
  if (event.index == gui::SQUARE_PAUSE_BUTTON) {
    if (state_ == ProcedureState::Running) {
      pauseProcedure();
    } else if (state_ == ProcedureState::Paused) {
      resumeProcedure(now);
    }
  }
}

bool isProcedureActive() {
  return procedureActive();
}

bool isChannelEnabled(uint8_t channel) {
  return presetLoaded_ && channel < hw::CHANNEL_COUNT &&
         activePreset_.channelEnabled[channel] != 0U;
}

int16_t channelPressureTenths(uint8_t channel) {
  if (!presetLoaded_ || channel >= hw::CHANNEL_COUNT) {
    return 0;
  }
  const uint8_t group = hw::CHANNEL_TO_REGULATOR[channel];
  return group < GROUP_COUNT ? activePreset_.pressureTenths[group] : 0;
}

bool copyPresetForEditing(uint16_t id, SquareWavePreset &preset) {
  return findPresetForList(id, preset);
}

bool setActivePreset(const SquareWavePreset &preset) {
  if (procedureActive() || !presetValid(preset)) {
    return false;
  }
  activePreset_ = preset;
  presetLoaded_ = true;
  presetPageSynchronized_ = false;
  state_ = ProcedureState::Idle;
  resetCountdowns();
  displaySyncPending_ = false;
  scopePreviewPending_ = false;
  return true;
}

bool openPresetSelectorForSetup(uint32_t now) {
  if (procedureActive()) {
    DBG_SQUARE("SELECT,LOCKED");
    return false;
  }
  openPresetSelector(now, PresetSelectDestination::Setup);
  return true;
}

void invalidatePresetList() {
  presetListCached_ = false;
  presetListScanPending_ = false;
  presetListWritePending_ = false;
}
}
}
