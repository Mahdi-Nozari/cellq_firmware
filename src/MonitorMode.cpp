#include "MonitorMode.h"
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "SquareWaveMode.h"

#include <WaveformTimer.h>

namespace {
constexpr uint8_t STATUS_NOT_RUNNING_IMAGE = 0;
constexpr uint8_t STATUS_ACTIVE_IMAGE = 1;
constexpr uint8_t STATUS_SUPPRESSED_IMAGE = 2;
constexpr uint8_t PAGE_LOADING_IMAGE = 0;
constexpr uint8_t PAGE_READY_IMAGE = 1;
constexpr uint8_t PAGE_RUNNING_IMAGE = 2;
constexpr uint16_t SCOPE_CHANNEL_INTERVAL_MS =
    (cellq::config::MONITOR_SCOPE_UPDATE_INTERVAL_MS +
     cellq::hw::CHANNEL_COUNT - 1U) /
    cellq::hw::CHANNEL_COUNT;
constexpr uint16_t LABEL_CHANNEL_INTERVAL_MS =
    (cellq::config::MONITOR_LABEL_UPDATE_INTERVAL_MS +
     cellq::hw::CHANNEL_COUNT - 1U) /
    cellq::hw::CHANNEL_COUNT;

static_assert(SCOPE_CHANNEL_INTERVAL_MS > 0,
              "Monitor scope interval is too short");
static_assert(cellq::config::PRESSURE_ACQUISITION_INTERVAL_MS > 0,
              "Pressure acquisition interval must be nonzero");
static_assert(cellq::config::MONITOR_LEAK_CHECK_INTERVAL_MS >=
                  cellq::config::PRESSURE_ACQUISITION_INTERVAL_MS,
              "Leak check interval must allow pressure samples");
static_assert(cellq::config::MONITOR_LEAK_THRESHOLD_PERCENT <= 100U,
              "Leak threshold percentage must not exceed 100");
static_assert(cellq::config::MONITOR_LEAK_THRESHOLD_PERCENT > 0U &&
                  cellq::config::MONITOR_LEAK_MIN_ON_SAMPLES > 0U,
              "Leak detection requires a threshold and samples");
static_assert(LABEL_CHANNEL_INTERVAL_MS > 0,
              "Monitor label interval is too short");
static_assert(cellq::config::MONITOR_LABEL_MIN_ON_SAMPLES > 0U,
              "Monitor label averaging requires samples");
static_assert(cellq::gui::MONITOR_SCOPE_TRACES *
                      cellq::hw::REGULATOR_COUNT ==
                  cellq::hw::CHANNEL_COUNT,
              "Monitor scope mapping must cover all channels");
static_assert(cellq::gui::MONITOR_LEAK_IMAGE_LAST -
                      cellq::gui::MONITOR_LEAK_IMAGE_FIRST +
                  1U == cellq::hw::CHANNEL_COUNT,
              "Monitor leak images must cover all channels");

cellq::monitor::PressureSample latest_ = {};
bool sampleValid_ = false;
bool acquisitionActive_ = false;
uint32_t lastAcquisitionMs_ = 0;
uint16_t seenRevision_[cellq::hw::REGULATOR_COUNT] = {};
uint32_t phaseStartMs_[cellq::hw::REGULATOR_COUNT] = {};
int32_t labelPressureSum_[cellq::hw::CHANNEL_COUNT] = {};
uint16_t labelSampleCount_[cellq::hw::CHANNEL_COUNT] = {};
uint32_t labelWindowStartMs_[cellq::hw::CHANNEL_COUNT] = {};
bool displayedPressureValid_[cellq::hw::CHANNEL_COUNT] = {};
bool displayedUnavailable_[cellq::hw::CHANNEL_COUNT] = {};
int32_t leakPressureSum_[cellq::hw::CHANNEL_COUNT] = {};
uint16_t leakSampleCount_[cellq::hw::CHANNEL_COUNT] = {};
bool leakDetected_[cellq::hw::CHANNEL_COUNT] = {};
uint8_t displayedLeak_[cellq::hw::CHANNEL_COUNT] = {};
uint8_t leakSyncChannel_ = cellq::hw::CHANNEL_COUNT;
uint32_t leakWindowStartMs_ = 0;
bool leakWindowActive_ = false;
bool formVisible_ = false;
bool streamActive_ = false;
uint8_t displayedTestState_ = UINT8_MAX;
uint8_t displayedPageStatus_ = UINT8_MAX;
bool loadingStatusPending_ = false;
uint8_t statusSyncChannel_ = cellq::hw::CHANNEL_COUNT;
// Preserve trace order across pauses; restart at channel 1 on form entry.
uint8_t scopeChannel_ = 0;
uint8_t labelChannel_ = 0;
uint32_t lastDisplayWriteMs_ = 0;
uint32_t lastScopeWriteMs_ = 0;
uint32_t nextLabelWriteMs_ = 0;
bool scopeResyncPending_ = false;
bool scopeReloadQueued_ = false;
bool scopeRecoveryAttempted_ = false;

#if !CELLQ_VIRTUAL_PRESSURE_SENSORS
static_assert(cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT > 0U,
              "Pressure zero sample count must be positive");
static_assert(cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT <= 64U,
              "Pressure zero sample count exceeds fixed-point range");

uint32_t zeroAdcSum_[cellq::hw::CHANNEL_COUNT] = {};

int32_t divideRounded(int32_t value, int32_t divisor) {
  if (value >= 0) {
    return (value + divisor / 2L) / divisor;
  }
  return (value - divisor / 2L) / divisor;
}

uint16_t readPressureAdc(uint8_t channel) {
  analogRead(cellq::hw::PRESSURE_PINS[channel]);
  return analogRead(cellq::hw::PRESSURE_PINS[channel]);
}

void calibratePressureZeros() {
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    analogRead(cellq::hw::PRESSURE_PINS[channel]);
    uint32_t sum = 0;
    for (uint8_t sample = 0;
         sample < cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT;
         ++sample) {
      sum += analogRead(cellq::hw::PRESSURE_PINS[channel]);
    }
    zeroAdcSum_[channel] = sum;
    const uint16_t average = static_cast<uint16_t>(
        (sum + cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT / 2U) /
        cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT);
    DBG_MONITOR_ZERO(channel + 1U, average);
  }
}
#endif

int16_t readPressureTenths(uint8_t channel, bool valveOn) {
#if CELLQ_VIRTUAL_PRESSURE_SENSORS
  if (!cellq::square::isProcedureActive() ||
      !cellq::square::isChannelEnabled(channel) ||
      !valveOn) {
    return 0;
  }
  return cellq::square::channelPressureTenths(channel);
#else
  (void)valveOn;
  const uint16_t raw = readPressureAdc(channel);

  // Honeywell 015PDAA5 slope with the per-channel boot zero removed.
  const int32_t centered =
      static_cast<int32_t>(raw) *
          cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT -
      static_cast<int32_t>(zeroAdcSum_[channel]);
  const int32_t scaled =
      centered * cellq::config::PRESSURE_SENSOR_GAIN_X10000[channel];
  const int32_t tenths =
      divideRounded(
          scaled, cellq::config::PRESSURE_SENSOR_CALIBRATION_SCALE *
                      cellq::config::PRESSURE_SENSOR_ZERO_SAMPLE_COUNT);
  if (tenths < INT16_MIN) {
    return INT16_MIN;
  }
  if (tenths > INT16_MAX) {
    return INT16_MAX;
  }
  return static_cast<int16_t>(tenths);
#endif
}

uint16_t pressureToScopeValue(uint8_t channel, int16_t pressureTenths) {
  if (pressureTenths >= 0) {
    return 0;
  }
  const int16_t targetTenths =
      cellq::square::channelPressureTenths(channel);
  const uint16_t scaleTenths = targetTenths < 0
      ? static_cast<uint16_t>(-static_cast<int32_t>(targetTenths))
      : static_cast<uint16_t>(-cellq::config::SQUARE_PRESSURE_MIN_TENTHS);
  const uint32_t magnitude =
      static_cast<uint32_t>(-static_cast<int32_t>(pressureTenths));
  const uint32_t scaled =
      (magnitude * cellq::config::MONITOR_SCOPE_VALUE_MAX +
       scaleTenths / 2U) / scaleTenths;
  return static_cast<uint16_t>(
      scaled > cellq::config::MONITOR_SCOPE_VALUE_MAX
          ? cellq::config::MONITOR_SCOPE_VALUE_MAX
          : scaled);
}

void resetLeakDetection() {
  leakWindowActive_ = false;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT;
       ++channel) {
    leakPressureSum_[channel] = 0;
    leakSampleCount_[channel] = 0;
    leakDetected_[channel] = false;
  }
}

void evaluateLeakDetection(uint32_t now) {
  if (!leakWindowActive_ ||
      static_cast<uint32_t>(now - leakWindowStartMs_) <
          cellq::config::MONITOR_LEAK_CHECK_INTERVAL_MS) {
    return;
  }
  leakWindowStartMs_ = now;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT;
       ++channel) {
    const int16_t target =
        cellq::square::channelPressureTenths(channel);
    bool detected = false;
    if (cellq::square::isProcedureRunning() &&
        cellq::square::isChannelEnabled(channel) && target < 0 &&
        leakSampleCount_[channel] >=
            cellq::config::MONITOR_LEAK_MIN_ON_SAMPLES) {
      detected =
          static_cast<int64_t>(leakPressureSum_[channel]) * 100L >
          static_cast<int64_t>(leakSampleCount_[channel]) * target *
              cellq::config::MONITOR_LEAK_THRESHOLD_PERCENT;
    }
    leakDetected_[channel] = detected;
    leakPressureSum_[channel] = 0;
    leakSampleCount_[channel] = 0;
  }
}

void stopStream() {
  streamActive_ = false;
}

void startStream(uint32_t now) {
  streamActive_ = true;
  labelChannel_ = 0;
  lastScopeWriteMs_ = now - SCOPE_CHANNEL_INTERVAL_MS;
  nextLabelWriteMs_ =
      now + cellq::config::MONITOR_LABEL_UPDATE_INTERVAL_MS;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT;
       ++channel) {
    labelPressureSum_[channel] = 0;
    labelSampleCount_[channel] = 0;
    labelWindowStartMs_[channel] =
        now + static_cast<uint32_t>(channel) *
                  LABEL_CHANNEL_INTERVAL_MS;
  }
#if CELLQ_VIRTUAL_PRESSURE_SENSORS
  DBG_MONITOR_SOURCE("VIRTUAL");
#else
  DBG_MONITOR_SOURCE("ANALOG");
#endif
}

void deactivate() {
  formVisible_ = false;
  stopStream();
  scopeResyncPending_ = false;
  scopeReloadQueued_ = false;
  scopeRecoveryAttempted_ = false;
}

void activate(uint32_t now) {
  scopeRecoveryAttempted_ = scopeReloadQueued_;
  formVisible_ = true;
  streamActive_ = false;
  scopeChannel_ = 0;
  scopeResyncPending_ = false;
  scopeReloadQueued_ = false;
  cellq::display::takeMonitorScopeFault();
  displayedTestState_ = UINT8_MAX;
  displayedPageStatus_ = UINT8_MAX;
  loadingStatusPending_ = true;
  statusSyncChannel_ = 0;
  leakSyncChannel_ = 0;
  lastDisplayWriteMs_ =
      now - cellq::config::MONITOR_DISPLAY_WRITE_INTERVAL_MS;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    displayedPressureValid_[channel] = false;
    displayedUnavailable_[channel] = false;
    displayedLeak_[channel] = UINT8_MAX;
  }
}

void refreshStatusState() {
  const uint8_t testState = cellq::square::isProcedureActive() ? 1U : 0U;
  if (testState == displayedTestState_) {
    return;
  }
  displayedTestState_ = testState;
  statusSyncChannel_ = 0;
}

void writeNextStatus() {
  uint8_t image = STATUS_NOT_RUNNING_IMAGE;
  if (displayedTestState_ != 0) {
    image = cellq::square::isChannelEnabled(statusSyncChannel_)
                ? STATUS_ACTIVE_IMAGE
                : STATUS_SUPPRESSED_IMAGE;
  }
  cellq::display::setMonitorChannelStatus(statusSyncChannel_, image);
  ++statusSyncChannel_;
}

void writeNextLeakStatus() {
  const uint8_t channel = leakSyncChannel_;
  cellq::display::setMonitorLeakStatus(channel,
                                       leakDetected_[channel]);
  displayedLeak_[channel] = leakDetected_[channel] ? 1U : 0U;
  ++leakSyncChannel_;
}

void writeNextScope(uint32_t now) {
  cellq::display::addMonitorScopeSample(
      scopeChannel_,
      pressureToScopeValue(scopeChannel_,
                           latest_.pressureTenths[scopeChannel_]));
  scopeChannel_ = (scopeChannel_ + 1U) % cellq::hw::CHANNEL_COUNT;
  lastScopeWriteMs_ = now;
}

bool writeNextLabel(uint32_t now) {
  const uint8_t channel = labelChannel_;
  labelChannel_ = (labelChannel_ + 1U) % cellq::hw::CHANNEL_COUNT;
  nextLabelWriteMs_ = now + LABEL_CHANNEL_INTERVAL_MS;
  const uint16_t count = labelSampleCount_[channel];
  const int32_t sum = labelPressureSum_[channel];
  labelPressureSum_[channel] = 0;
  labelSampleCount_[channel] = 0;
  labelWindowStartMs_[channel] = now;
  if (!cellq::square::isChannelEnabled(channel)) {
    return false;
  }
  if (count < cellq::config::MONITOR_LABEL_MIN_ON_SAMPLES) {
    if (!displayedPressureValid_[channel] &&
        !displayedUnavailable_[channel] &&
        cellq::display::monitorQueueHasRoom()) {
      cellq::display::setMonitorPressureUnavailable(channel);
      displayedUnavailable_[channel] = true;
      return true;
    }
    return false;
  }
  if (!cellq::display::monitorQueueHasRoom()) {
    return false;
  }
  const int32_t halfCount = count / 2U;
  const int16_t pressure = static_cast<int16_t>(
      (sum >= 0 ? sum + halfCount : sum - halfCount) / count);
  cellq::display::setMonitorPressureLabel(channel, pressure);
  DBG_MONITOR_PRESSURE(channel + 1U, pressure);
  displayedPressureValid_[channel] = true;
  displayedUnavailable_[channel] = false;
  return true;
}
}

namespace cellq {
namespace monitor {
void begin() {
  formVisible_ = false;
  streamActive_ = false;
  displayedTestState_ = UINT8_MAX;
  displayedPageStatus_ = UINT8_MAX;
  loadingStatusPending_ = false;
  statusSyncChannel_ = hw::CHANNEL_COUNT;
  leakSyncChannel_ = hw::CHANNEL_COUNT;
  scopeChannel_ = 0;
  scopeResyncPending_ = false;
  scopeReloadQueued_ = false;
  scopeRecoveryAttempted_ = false;
  sampleValid_ = false;
  acquisitionActive_ = false;
  resetLeakDetection();
#if !CELLQ_VIRTUAL_PRESSURE_SENSORS
  calibratePressureZeros();
#endif
}

void acquire(uint32_t now) {
  if (!square::isProcedureActive()) {
    acquisitionActive_ = false;
    sampleValid_ = false;
    if (leakWindowActive_) {
      resetLeakDetection();
    }
    return;
  }
  if (!leakWindowActive_) {
    leakWindowStartMs_ = now;
    leakWindowActive_ = true;
  }
  if (!acquisitionActive_) {
    acquisitionActive_ = true;
    lastAcquisitionMs_ =
        now - config::PRESSURE_ACQUISITION_INTERVAL_MS;
  }
  const uint32_t elapsed = now - lastAcquisitionMs_;
  if (elapsed < config::PRESSURE_ACQUISITION_INTERVAL_MS) {
    return;
  }
  lastAcquisitionMs_ +=
      (elapsed / config::PRESSURE_ACQUISITION_INTERVAL_MS) *
      config::PRESSURE_ACQUISITION_INTERVAL_MS;
  uint16_t before[hw::REGULATOR_COUNT];
  for (uint8_t group = 0; group < hw::REGULATOR_COUNT; ++group) {
    before[group] = waveform::outputRevision(group);
    if (!sampleValid_ || before[group] != seenRevision_[group]) {
      seenRevision_[group] = before[group];
      phaseStartMs_[group] = now;
    }
  }
  latest_.timeMs = now;
  for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
    const bool valveOn =
        digitalRead(hw::VALVE_PINS[channel]) == hw::VALVE_ON;
    latest_.valveOn[channel] = valveOn ? 1U : 0U;
    latest_.pressureTenths[channel] =
        readPressureTenths(channel, valveOn);
  }
  for (uint8_t group = 0; group < hw::REGULATOR_COUNT; ++group) {
    const uint16_t after = waveform::outputRevision(group);
    if (after != before[group]) {
      seenRevision_[group] = after;
      phaseStartMs_[group] = now;
      continue;
    }
    if (static_cast<uint32_t>(now - phaseStartMs_[group]) <
        config::MONITOR_LABEL_SETTLE_MS) {
      continue;
    }
    const bool labelSampleEligible =
        formVisible_ && streamActive_ && square::isProcedureRunning() &&
        waveform::onPhaseHasRemainingMargin(
            group, config::MONITOR_LABEL_PRE_CLOSE_MS);
    for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
      if (hw::CHANNEL_TO_REGULATOR[channel] != group) {
        continue;
      }
      const uint8_t phase = latest_.valveOn[channel];
      if (labelSampleEligible && phase != 0U &&
          square::isChannelEnabled(channel)) {
        const int32_t windowAge =
            static_cast<int32_t>(now - labelWindowStartMs_[channel]);
        if (windowAge >= 0 &&
            static_cast<uint32_t>(windowAge) <
                config::MONITOR_LABEL_UPDATE_INTERVAL_MS &&
            labelSampleCount_[channel] < UINT16_MAX) {
          labelPressureSum_[channel] += latest_.pressureTenths[channel];
          ++labelSampleCount_[channel];
        }
      }
      if (square::isProcedureRunning() && phase != 0U &&
          square::isChannelEnabled(channel) &&
          square::channelPressureTenths(channel) < 0 &&
          leakSampleCount_[channel] < UINT16_MAX) {
        leakPressureSum_[channel] += latest_.pressureTenths[channel];
        ++leakSampleCount_[channel];
      }
    }
  }
  sampleValid_ = true;
}

void update(uint32_t now) {
  if (!square::isProcedureActive()) {
    if (leakWindowActive_) {
      resetLeakDetection();
    }
  } else {
    evaluateLeakDetection(now);
  }
  if (!formVisible_) {
    return;
  }
  if (display::takeMonitorScopeFault()) {
    stopStream();
    scopeResyncPending_ = true;
  }
  if (scopeResyncPending_) {
    if (!scopeRecoveryAttempted_ && !scopeReloadQueued_) {
      scopeReloadQueued_ = true;
      display::activateForm(gui::MONITOR_FORM);
    }
    return;
  }
  refreshStatusState();
  if (!square::isProcedureRunning()) {
    if (streamActive_) {
      stopStream();
    }
  } else if (!streamActive_) {
    startStream(now);
  }
  if (static_cast<uint32_t>(now - lastDisplayWriteMs_) <
      config::MONITOR_DISPLAY_WRITE_INTERVAL_MS) {
    return;
  }
  const uint8_t pageStatus = square::isProcedureActive()
                                 ? PAGE_RUNNING_IMAGE
                                 : PAGE_READY_IMAGE;
  if (loadingStatusPending_) {
    display::setMonitorPageStatus(PAGE_LOADING_IMAGE);
    displayedPageStatus_ = PAGE_LOADING_IMAGE;
    loadingStatusPending_ = false;
    lastDisplayWriteMs_ = now;
    return;
  } else if (statusSyncChannel_ < hw::CHANNEL_COUNT) {
    writeNextStatus();
    lastDisplayWriteMs_ = now;
    return;
  } else if (leakSyncChannel_ < hw::CHANNEL_COUNT) {
    writeNextLeakStatus();
    lastDisplayWriteMs_ = now;
    return;
  } else if (pageStatus != displayedPageStatus_) {
    display::setMonitorPageStatus(pageStatus);
    displayedPageStatus_ = pageStatus;
    lastDisplayWriteMs_ = now;
    return;
  }
  for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
    const uint8_t image = leakDetected_[channel] ? 1U : 0U;
    if (displayedLeak_[channel] != image) {
      if (!display::monitorQueueHasRoom()) {
        return;
      }
      display::setMonitorLeakStatus(channel, leakDetected_[channel]);
      displayedLeak_[channel] = image;
      lastDisplayWriteMs_ = now;
      return;
    }
  }
  if (!streamActive_) {
    return;
  }
  bool wrote = false;
  if (static_cast<int32_t>(now - nextLabelWriteMs_) >= 0) {
    wrote = writeNextLabel(now);
  }
  if (!wrote && sampleValid_ &&
      static_cast<uint32_t>(now - lastScopeWriteMs_) >=
          SCOPE_CHANNEL_INTERVAL_MS &&
      display::monitorQueueHasRoom()) {
    writeNextScope(now);
    wrote = true;
  }
  if (!wrote) {
    return;
  }
  lastDisplayWriteMs_ = now;
}

void handleDisplayEvent(const display::Event &event, uint32_t now) {
  if (event.object == gui::FORM_OBJECT) {
    if (event.index == gui::MONITOR_FORM) {
      activate(now);
    } else {
      deactivate();
    }
    return;
  }
  if (formVisible_ && event.object == gui::USERBUTTON_OBJECT &&
      event.index == gui::MONITOR_SETUP_BUTTON &&
      !square::isProcedureActive()) {
    deactivate();
  }
}

bool latestSample(PressureSample &sample) {
  if (!sampleValid_) {
    return false;
  }
  sample = latest_;
  return true;
}
}
}
