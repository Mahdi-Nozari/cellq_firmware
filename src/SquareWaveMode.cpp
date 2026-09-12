#include "SquareWaveMode.h"
#include <avr/pgmspace.h>
#include <string.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "Regulator.h"

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
constexpr uint8_t DISPLAY_SYNC_STEP_COUNT = 19;

SquareWavePreset activePreset_ = {};
ProcedureState state_ = ProcedureState::Idle;
bool formActive_ = false;
bool presetLoaded_ = false;
bool waveOutputOn_ = false;
uint64_t activeElapsedMs_ = 0;
uint32_t completedCycles_ = 0;
uint32_t lastTickMs_ = 0;
uint32_t lastRuntimeDisplayMs_ = 0;
uint32_t displayedMinute_ = UINT32_MAX;
uint32_t displayedCycles_ = UINT32_MAX;
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

void applyWaveOutput(bool on) {
  if (waveOutputOn_ == on) {
    return;
  }
  waveOutputOn_ = on;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    const bool channelOn = on && activePreset_.channelEnabled[channel] != 0;
    digitalWrite(cellq::hw::VALVE_PINS[channel],
                 channelOn ? cellq::hw::VALVE_ON : cellq::hw::VALVE_OFF);
  }
}

void idleOutputs() {
  waveOutputOn_ = false;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    digitalWrite(cellq::hw::VALVE_PINS[channel], cellq::hw::VALVE_OFF);
  }
  cellq::regulator::idle();
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

void writeProcedureState() {
  cellq::display::setSquareStatus(statusImage());
  cellq::display::setSquareStartButton(procedureActive());
  cellq::display::setSquarePauseButton(state_ == ProcedureState::Paused);
  cellq::display::setSquareLabel(
      cellq::gui::SQUARE_ACTIVE_NAME_LABEL,
      procedureActive() ? activePreset_.name : "---");
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
  writeElapsedTime();
  writeElapsedCycles();
  writeProcedureState();
  DBG_SQUARE("DONE");
}

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

void updateWaveform() {
  if (activePreset_.frequencyTenthsHz == 0) {
    completedCycles_ = 0;
    applyWaveOutput(false);
    return;
  }
  if (activeElapsedMs_ < activePreset_.delayMs) {
    completedCycles_ = 0;
    applyWaveOutput(false);
    return;
  }

  const uint64_t waveformElapsed = activeElapsedMs_ - activePreset_.delayMs;
  const uint64_t waveformPosition =
      waveformElapsed * activePreset_.frequencyTenthsHz;
  const uint64_t cycles = waveformPosition / 10000ULL;
  if (activePreset_.totalCycles != 0 &&
      cycles >= activePreset_.totalCycles) {
    completedCycles_ = activePreset_.totalCycles;
    finishProcedure();
    return;
  }

  completedCycles_ =
      cycles > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(cycles);
  const uint16_t phase = static_cast<uint16_t>(waveformPosition % 10000ULL);
  const bool on = activePreset_.dutyPercent == 100U ||
                  (activePreset_.dutyPercent != 0 &&
                   phase < static_cast<uint16_t>(
                               activePreset_.dutyPercent * 100U));
  applyWaveOutput(on);
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
  }
}

void startProcedure(uint32_t now) {
  if (!presetLoaded_) {
    return;
  }
  const bool regulator1Ok =
      cellq::regulator::setPressureTenths(0, activePreset_.pressureTenths[0]);
  const bool regulator2Ok =
      cellq::regulator::setPressureTenths(1, activePreset_.pressureTenths[1]);
  if (!regulator1Ok || !regulator2Ok) {
    failStart();
    return;
  }

  state_ = ProcedureState::Running;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  lastTickMs_ = now;
  lastRuntimeDisplayMs_ = now;
  applyWaveOutput(false);
  updateWaveform();
  writeElapsedTime();
  writeElapsedCycles();
  writeProcedureState();
  DBG_SQUARE("START");
}

void pauseProcedure() {
  state_ = ProcedureState::Paused;
  cellq::display::setSquarePauseButton(true);
  DBG_SQUARE("PAUSE");
}

void resumeProcedure(uint32_t now) {
  state_ = ProcedureState::Running;
  lastTickMs_ = now;
  lastRuntimeDisplayMs_ = now;
  cellq::display::setSquarePauseButton(false);
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
  }
}

void loadFactoryPreset1(uint32_t now) {
  memcpy_P(&activePreset_, &FACTORY_PRESET_1, sizeof(activePreset_));
  presetLoaded_ = true;
  state_ = ProcedureState::Idle;
  activeElapsedMs_ = 0;
  completedCycles_ = 0;
  waveOutputOn_ = false;
  displayedMinute_ = UINT32_MAX;
  displayedCycles_ = UINT32_MAX;
  startDisplaySync(now);
  DBG_SQUARE("LOAD,1");
}
}

namespace cellq {
namespace square {
void begin() {
  state_ = ProcedureState::Idle;
  formActive_ = false;
  presetLoaded_ = false;
  displaySyncPending_ = false;
  scopePreviewPending_ = false;
}

void update(uint32_t now) {
  if (!formActive_) {
    return;
  }
  if (state_ == ProcedureState::Running) {
    activeElapsedMs_ += static_cast<uint32_t>(now - lastTickMs_);
    lastTickMs_ = now;
    updateWaveform();
    if (state_ == ProcedureState::Running) {
      updateRuntimeDisplay(now);
    }
  }
  updateDisplaySync(now);
  updateScopePreview(now);
}

void handleDisplayEvent(const display::Event &event, uint32_t now) {
  if (event.object == gui::FORM_OBJECT) {
    if (event.index == gui::SQUARE_FORM) {
      if (!formActive_) {
        formActive_ = true;
        loadFactoryPreset1(now);
      }
    } else if (formActive_) {
      stopProcedure(false);
      formActive_ = false;
      displaySyncPending_ = false;
      scopePreviewPending_ = false;
    }
    return;
  }
  if (!formActive_ || event.object != gui::USERBUTTON_OBJECT ||
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
}
}
