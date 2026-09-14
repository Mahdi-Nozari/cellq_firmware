#include "SquareWaveMode.h"
#include <avr/pgmspace.h>
#include <string.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "Regulator.h"
#include "WaveformTimer.h"

namespace {
enum class ProcedureState : uint8_t { Idle, Running, Paused, Done, Error };

struct SquareWavePreset {
  uint16_t id;
  char name[21];
  int16_t pressureTenths[2];
  uint8_t channelEnabled[cellq::hw::CHANNEL_COUNT];
  uint16_t frequencyTenthsHz;
  uint8_t dutyPercent;
  uint16_t delayMs;
  uint32_t totalCycles;
};

const SquareWavePreset FACTORY_PRESET_1 PROGMEM = {
    1,
    "default preset 1",
    {-20, -20},
    {1, 1, 0, 1, 1, 0},
    10,
    50,
    0,
    30};

constexpr uint8_t STATUS_IDLE_IMAGE = 0;
constexpr uint8_t STATUS_RUNNING_IMAGE = 1;
constexpr uint8_t STATUS_DONE_IMAGE = 2;
constexpr uint8_t STATUS_ERROR_IMAGE = 3;
constexpr uint8_t DISPLAY_SYNC_STEP_COUNT = 20;
constexpr int16_t PROGRESS_NOT_SENT = -2;
constexpr int16_t PROGRESS_BLANK = -1;

SquareWavePreset activePreset_ = {};
ProcedureState state_ = ProcedureState::Idle;
bool form2Visible_ = false;
bool presetLoaded_ = false;
uint64_t activeElapsedMs_ = 0;
uint32_t completedCycles_ = 0;
uint32_t lastTickMs_ = 0;
uint32_t lastRuntimeDisplayMs_ = 0;
uint32_t displayedMinute_ = UINT32_MAX;
uint32_t displayedCycles_ = UINT32_MAX;
int16_t displayedProgress_ = PROGRESS_NOT_SENT;
bool displaySyncPending_ = false;
uint8_t displaySyncStep_ = 0;
uint32_t displaySyncLastMs_ = 0;
bool scopePreviewPending_ = false;
uint16_t scopeSample_ = 0;
uint32_t scopeLastWriteMs_ = 0;

uint8_t appendUnsigned(char *text, uint32_t value) {
  char reverse[10];
  uint8_t count = 0;
  do {
    reverse[count++] = static_cast<char>('0' + value % 10UL);
    value /= 10UL;
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

void formatUnsignedTenths(char *text, uint16_t tenths) {
  uint8_t pos = appendUnsigned(text, tenths / 10U);
  text[pos++] = '.';
  text[pos++] = static_cast<char>('0' + tenths % 10U);
  text[pos] = '\0';
}

void formatHundredths(char *text, uint32_t hundredths) {
  uint8_t pos = appendUnsigned(text, hundredths / 100UL);
  const uint8_t fraction = hundredths % 100UL;
  if (fraction == 0) {
    text[pos] = '\0';
    return;
  }
  text[pos++] = '.';
  text[pos++] = static_cast<char>('0' + fraction / 10U);
  if (fraction % 10U != 0) {
    text[pos++] = static_cast<char>('0' + fraction % 10U);
  }
  text[pos] = '\0';
}

void formatHoursMinutes(char *text, uint64_t milliseconds) {
  const uint32_t totalMinutes =
      static_cast<uint32_t>(milliseconds / 60000ULL);
  const uint32_t hours = totalMinutes / 60UL;
  const uint8_t minutes = totalMinutes % 60UL;
  uint8_t pos = 0;
  if (hours < 10UL) {
    text[pos++] = '0';
  }
  pos += appendUnsigned(text + pos, hours);
  text[pos++] = ':';
  text[pos++] = static_cast<char>('0' + minutes / 10U);
  text[pos++] = static_cast<char>('0' + minutes % 10U);
  text[pos] = '\0';
}

uint64_t totalTimeMs() {
  if (activePreset_.frequencyTenthsHz == 0 ||
      activePreset_.totalCycles == 0) {
    return 0;
  }
  return activePreset_.delayMs +
         static_cast<uint64_t>(activePreset_.totalCycles) * 10000ULL /
             activePreset_.frequencyTenthsHz;
}

uint8_t statusImage() {
  switch (state_) {
    case ProcedureState::Running:
    case ProcedureState::Paused:
      return STATUS_RUNNING_IMAGE;
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
         state_ == ProcedureState::Paused;
}

void idleOutputs() {
  cellq::waveform::stop();
  cellq::regulator::idle();
}

bool presetValid() {
  for (uint8_t regulator = 0; regulator < cellq::hw::REGULATOR_COUNT;
       ++regulator) {
    if (activePreset_.pressureTenths[regulator] <
            cellq::config::SQUARE_PRESSURE_MIN_TENTHS ||
        activePreset_.pressureTenths[regulator] >
            cellq::config::SQUARE_PRESSURE_MAX_TENTHS) {
      return false;
    }
  }
  if (activePreset_.frequencyTenthsHz >
          cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ ||
      (activePreset_.frequencyTenthsHz != 0 &&
       activePreset_.frequencyTenthsHz <
           cellq::config::SQUARE_FREQUENCY_MIN_TENTHS_HZ)) {
    return false;
  }
  return activePreset_.dutyPercent <= 100U;
}

uint8_t enabledChannelMask() {
  uint8_t mask = 0;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    if (activePreset_.channelEnabled[channel] != 0) {
      mask |= static_cast<uint8_t>(1U << channel);
    }
  }
  return mask;
}

void writeElapsedTime() {
  char text[16];
  formatHoursMinutes(text, activeElapsedMs_);
  cellq::display::setSquareLabel(cellq::gui::SQUARE_ELAPSED_TIME_LABEL,
                                 text);
  displayedMinute_ = static_cast<uint32_t>(activeElapsedMs_ / 60000ULL);
}

void writeElapsedCycles() {
  char text[11];
  const uint8_t length = appendUnsigned(text, completedCycles_);
  text[length] = '\0';
  cellq::display::setSquareLabel(cellq::gui::SQUARE_ELAPSED_CYCLES_LABEL,
                                 text);
  displayedCycles_ = completedCycles_;
}

void writeProgress() {
  int16_t progress = PROGRESS_BLANK;
  if (state_ == ProcedureState::Running &&
      activePreset_.totalCycles != 0) {
    const uint64_t scaled =
        static_cast<uint64_t>(completedCycles_) * 100ULL;
    progress = static_cast<int16_t>(scaled / activePreset_.totalCycles);
  }
  if (progress == displayedProgress_) {
    return;
  }

  char text[5];
  if (progress == PROGRESS_BLANK) {
    text[0] = '\0';
  } else {
    uint8_t pos = appendUnsigned(text, static_cast<uint16_t>(progress));
    text[pos++] = '%';
    text[pos] = '\0';
  }
  cellq::display::setSquareLabel(cellq::gui::SQUARE_PROGRESS_LABEL, text);
  displayedProgress_ = progress;
}

void writeProcedureState() {
  cellq::display::setSquareStatus(statusImage());
  cellq::display::setSquareStartButton(procedureActive());
  cellq::display::setSquarePauseButton(state_ == ProcedureState::Paused);
  cellq::display::setSquareLabel(
      cellq::gui::SQUARE_ACTIVE_NAME_LABEL,
      procedureActive() ? activePreset_.name : "---");
  writeProgress();
}

void stopProcedure(bool updateDisplay) {
  idleOutputs();
  state_ = ProcedureState::Idle;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  if (updateDisplay) {
    writeElapsedTime();
    writeElapsedCycles();
    writeProcedureState();
  }
  DBG_SQUARE("STOP");
}

void finishProcedure() {
  idleOutputs();
  state_ = ProcedureState::Done;
  if (form2Visible_) {
    writeElapsedTime();
    writeElapsedCycles();
    writeProcedureState();
  }
  DBG_SQUARE("DONE");
}

#if CELLQ_REQUIRE_DAC_ACK
void failStart() {
  idleOutputs();
  state_ = ProcedureState::Error;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  writeElapsedTime();
  writeElapsedCycles();
  writeProcedureState();
  DBG_SQUARE("ERR,DAC");
}
#endif

void failPreset() {
  idleOutputs();
  state_ = ProcedureState::Error;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  writeElapsedTime();
  writeElapsedCycles();
  writeProcedureState();
  DBG_SQUARE("ERR,PRESET");
}

void updateRuntimeDisplay(uint32_t now) {
  if (static_cast<uint32_t>(now - lastRuntimeDisplayMs_) <
      cellq::config::SQUARE_RUNTIME_DISPLAY_INTERVAL_MS) {
    return;
  }
  lastRuntimeDisplayMs_ = now;
  const uint32_t minute =
      static_cast<uint32_t>(activeElapsedMs_ / 60000ULL);
  if (minute != displayedMinute_) {
    writeElapsedTime();
  }
  if (completedCycles_ != displayedCycles_) {
    writeElapsedCycles();
    writeProgress();
  }
}

void startProcedure(uint32_t now) {
  if (!presetLoaded_ || !presetValid()) {
    failPreset();
    return;
  }
  const bool regulator1Ok =
      cellq::regulator::setPressureTenths(0, activePreset_.pressureTenths[0]);
  const bool regulator2Ok =
      cellq::regulator::setPressureTenths(1, activePreset_.pressureTenths[1]);
  if (!regulator1Ok || !regulator2Ok) {
#if CELLQ_REQUIRE_DAC_ACK
    failStart();
    return;
#else
    DBG_SQUARE("WARN,DAC,IGNORED");
#endif
  }

  state_ = ProcedureState::Running;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  lastTickMs_ = now;
  lastRuntimeDisplayMs_ = now;
  const cellq::waveform::Config waveformConfig = {
      enabledChannelMask(), activePreset_.frequencyTenthsHz,
      activePreset_.dutyPercent, activePreset_.delayMs,
      activePreset_.totalCycles};
  if (!cellq::waveform::start(waveformConfig)) {
    failPreset();
    return;
  }
  writeElapsedTime();
  writeElapsedCycles();
  writeProcedureState();
  DBG_SQUARE("START");
}

void pauseProcedure() {
  cellq::waveform::pause();
  state_ = ProcedureState::Paused;
  cellq::display::setSquarePauseButton(true);
  writeProgress();
  DBG_SQUARE("PAUSE");
}

void resumeProcedure(uint32_t now) {
  cellq::waveform::resume();
  state_ = ProcedureState::Running;
  lastTickMs_ = now;
  lastRuntimeDisplayMs_ = now;
  cellq::display::setSquarePauseButton(false);
  writeProgress();
  DBG_SQUARE("RESUME");
}

void startDisplaySync(uint32_t now) {
  displaySyncPending_ = true;
  displaySyncStep_ = 0;
  displaySyncLastMs_ =
      now - cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS;
  scopePreviewPending_ = false;
  scopeSample_ = 0;
}

void writeDisplaySyncStep() {
  char text[24];
  switch (displaySyncStep_) {
    case 0:
      cellq::display::setSquareLabel(cellq::gui::SQUARE_NAME_LABEL,
                                     activePreset_.name);
      break;
    case 1:
      formatSignedTenths(text, activePreset_.pressureTenths[0]);
      cellq::display::setSquareLabel(cellq::gui::SQUARE_PRESSURE_1_LABEL,
                                     text);
      break;
    case 2:
      formatSignedTenths(text, activePreset_.pressureTenths[1]);
      cellq::display::setSquareLabel(cellq::gui::SQUARE_PRESSURE_2_LABEL,
                                     text);
      break;
    case 3: {
      uint32_t onTimeHundredths = 0;
      if (activePreset_.frequencyTenthsHz != 0) {
        onTimeHundredths =
            (static_cast<uint32_t>(activePreset_.dutyPercent) * 10UL +
             activePreset_.frequencyTenthsHz / 2U) /
            activePreset_.frequencyTenthsHz;
      }
      formatHundredths(text, onTimeHundredths);
      cellq::display::setSquareLabel(cellq::gui::SQUARE_ON_TIME_LABEL, text);
      break;
    }
    case 4:
      formatUnsignedTenths(text, activePreset_.frequencyTenthsHz);
      cellq::display::setSquareLabel(cellq::gui::SQUARE_FREQUENCY_LABEL,
                                     text);
      break;
    case 5:
      formatHoursMinutes(text, activeElapsedMs_);
      cellq::display::setSquareLabel(
          cellq::gui::SQUARE_ELAPSED_TIME_LABEL, text);
      displayedMinute_ =
          static_cast<uint32_t>(activeElapsedMs_ / 60000ULL);
      break;
    case 6:
      if (activePreset_.totalCycles == 0 ||
          activePreset_.frequencyTenthsHz == 0) {
        strcpy(text, "--:--");
      } else {
        formatHoursMinutes(text, totalTimeMs());
      }
      cellq::display::setSquareLabel(cellq::gui::SQUARE_TOTAL_TIME_LABEL,
                                     text);
      break;
    case 7: {
      const uint8_t length = appendUnsigned(text, completedCycles_);
      text[length] = '\0';
      cellq::display::setSquareLabel(
          cellq::gui::SQUARE_ELAPSED_CYCLES_LABEL, text);
      displayedCycles_ = completedCycles_;
      break;
    }
    case 8:
      if (activePreset_.totalCycles == 0) {
        strcpy(text, "---");
      } else {
        const uint8_t length = appendUnsigned(text, activePreset_.totalCycles);
        text[length] = '\0';
      }
      cellq::display::setSquareLabel(
          cellq::gui::SQUARE_TOTAL_CYCLES_LABEL, text);
      break;
    case 15:
      cellq::display::setSquareStatus(statusImage());
      break;
    case 16:
      cellq::display::setSquareStartButton(procedureActive());
      break;
    case 17:
      cellq::display::setSquarePauseButton(
          state_ == ProcedureState::Paused);
      break;
    case 18:
      cellq::display::setSquareLabel(
          cellq::gui::SQUARE_ACTIVE_NAME_LABEL,
          procedureActive() ? activePreset_.name : "---");
      break;
    case 19:
      writeProgress();
      break;
    default: {
      const uint8_t channel = displaySyncStep_ - 9U;
      cellq::display::setSquareChannelEnabled(
          channel, activePreset_.channelEnabled[channel] != 0);
      break;
    }
  }
}

void updateDisplaySync(uint32_t now) {
  if (!displaySyncPending_ ||
      static_cast<uint32_t>(now - displaySyncLastMs_) <
          cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  displaySyncLastMs_ = now;
  writeDisplaySyncStep();
  ++displaySyncStep_;
  if (displaySyncStep_ >= DISPLAY_SYNC_STEP_COUNT) {
    displaySyncPending_ = false;
    scopePreviewPending_ = true;
    scopeSample_ = 0;
    scopeLastWriteMs_ = now;
  }
}

void updateScopePreview(uint32_t now) {
  if (!scopePreviewPending_ ||
      static_cast<uint32_t>(now - scopeLastWriteMs_) <
          cellq::config::SQUARE_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  scopeLastWriteMs_ = now;
  uint16_t value = cellq::config::SQUARE_SCOPE_LOW_VALUE;
  if (activePreset_.frequencyTenthsHz != 0 &&
      activePreset_.dutyPercent != 0) {
    const uint32_t sampleTimeMs =
        static_cast<uint32_t>(scopeSample_) *
        cellq::config::SQUARE_SCOPE_PREVIEW_MS /
        cellq::config::SQUARE_SCOPE_SAMPLE_COUNT;
    const uint32_t phase =
        sampleTimeMs * activePreset_.frequencyTenthsHz % 10000UL;
    if (activePreset_.dutyPercent == 100U ||
        phase < static_cast<uint32_t>(activePreset_.dutyPercent) * 100UL) {
      value = cellq::config::SQUARE_SCOPE_HIGH_VALUE;
    }
  }
  cellq::display::addSquareScopeSample(value);
  ++scopeSample_;
  if (scopeSample_ >= cellq::config::SQUARE_SCOPE_SAMPLE_COUNT) {
    scopePreviewPending_ = false;
    if (state_ == ProcedureState::Running) {
      const uint32_t minute =
          static_cast<uint32_t>(activeElapsedMs_ / 60000ULL);
      if (minute != displayedMinute_) {
        writeElapsedTime();
      }
      if (completedCycles_ != displayedCycles_) {
        writeElapsedCycles();
        writeProgress();
      }
    }
    cellq::display::setSquarePresetReady(true);
    DBG_SQUARE("READY");
  }
}

void beginPageSync(uint32_t now) {
  cellq::display::setSquarePresetReady(false);
  displayedMinute_ = UINT32_MAX;
  displayedCycles_ = UINT32_MAX;
  displayedProgress_ = PROGRESS_NOT_SENT;
  lastRuntimeDisplayMs_ = now;
  startDisplaySync(now);
}

void loadFactoryPreset1(uint32_t now) {
  memcpy_P(&activePreset_, &FACTORY_PRESET_1, sizeof(activePreset_));
  presetLoaded_ = true;
  state_ = ProcedureState::Idle;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  beginPageSync(now);
  DBG_SQUARE("LOAD,1");
}
}

namespace cellq {
namespace square {
void begin() {
  waveform::begin();
  state_ = ProcedureState::Idle;
  form2Visible_ = false;
  presetLoaded_ = false;
  displayedProgress_ = PROGRESS_NOT_SENT;
  displaySyncPending_ = false;
  scopePreviewPending_ = false;
}

void update(uint32_t now) {
  if (state_ == ProcedureState::Running) {
    activeElapsedMs_ += static_cast<uint32_t>(now - lastTickMs_);
    lastTickMs_ = now;
    completedCycles_ = waveform::completedCycles();
    if (waveform::takeFinished()) {
      completedCycles_ = waveform::completedCycles();
      activeElapsedMs_ = totalTimeMs();
      finishProcedure();
    } else if (form2Visible_ &&
        !displaySyncPending_ && !scopePreviewPending_) {
      updateRuntimeDisplay(now);
    }
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
      form2Visible_ = true;
      if (procedureActive() || state_ == ProcedureState::Done ||
          state_ == ProcedureState::Error) {
        beginPageSync(now);
      } else {
        loadFactoryPreset1(now);
      }
    } else {
      form2Visible_ = false;
      displaySyncPending_ = false;
      scopePreviewPending_ = false;
    }
    return;
  }
  if (!form2Visible_ || event.object != gui::USERBUTTON_OBJECT ||
      event.value > 1U) {
    return;
  }
  if (event.index == gui::SQUARE_START_BUTTON) {
    if (procedureActive()) {
      stopProcedure(true);
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
         activePreset_.channelEnabled[channel] != 0;
}

int16_t channelPressureTenths(uint8_t channel) {
  if (!presetLoaded_ || channel >= hw::CHANNEL_COUNT) {
    return 0;
  }
  const uint8_t regulator = hw::CHANNEL_TO_REGULATOR[channel];
  return regulator < hw::REGULATOR_COUNT
             ? activePreset_.pressureTenths[regulator]
             : 0;
}
}
}
