#include "MonitorMode.h"
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "SquareWaveMode.h"

namespace {
constexpr uint8_t STATUS_NOT_RUNNING_IMAGE = 0;
constexpr uint8_t STATUS_ACTIVE_IMAGE = 1;
constexpr uint8_t STATUS_SUPPRESSED_IMAGE = 2;
constexpr uint16_t SCOPE_CHANNEL_INTERVAL_MS =
    cellq::config::MONITOR_SCOPE_UPDATE_INTERVAL_MS /
    cellq::hw::CHANNEL_COUNT;
constexpr uint16_t LABEL_CHANNEL_INTERVAL_MS =
    cellq::config::MONITOR_LABEL_UPDATE_INTERVAL_MS /
    cellq::hw::CHANNEL_COUNT;

static_assert(SCOPE_CHANNEL_INTERVAL_MS > 0,
              "Monitor scope interval is too short");
static_assert(LABEL_CHANNEL_INTERVAL_MS > 0,
              "Monitor label interval is too short");

int16_t pressureTenths_[cellq::hw::CHANNEL_COUNT] = {};
bool pressureValid_[cellq::hw::CHANNEL_COUNT] = {};
bool formVisible_ = false;
bool streamActive_ = false;
uint8_t displayedTestState_ = UINT8_MAX;
uint8_t statusSyncChannel_ = cellq::hw::CHANNEL_COUNT;
uint8_t scopeChannel_ = 0;
uint8_t labelChannel_ = 0;
uint32_t lastDisplayWriteMs_ = 0;
uint32_t lastScopeWriteMs_ = 0;
uint32_t lastLabelWriteMs_ = 0;

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

int16_t readPressureTenths(uint8_t channel) {
#if CELLQ_VIRTUAL_PRESSURE_SENSORS
  if (!cellq::square::isProcedureActive() ||
      !cellq::square::isChannelEnabled(channel) ||
      digitalRead(cellq::hw::VALVE_PINS[channel]) != cellq::hw::VALVE_ON) {
    return 0;
  }
  return cellq::square::channelPressureTenths(channel);
#else
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

uint16_t pressureToScopeValue(int16_t pressureTenths) {
  if (pressureTenths < cellq::config::SQUARE_PRESSURE_MIN_TENTHS) {
    pressureTenths = cellq::config::SQUARE_PRESSURE_MIN_TENTHS;
  }
  if (pressureTenths > cellq::config::SQUARE_PRESSURE_MAX_TENTHS) {
    pressureTenths = cellq::config::SQUARE_PRESSURE_MAX_TENTHS;
  }
  const int16_t range = cellq::config::SQUARE_PRESSURE_MAX_TENTHS -
                        cellq::config::SQUARE_PRESSURE_MIN_TENTHS;
  return static_cast<uint16_t>(
      static_cast<int32_t>(cellq::config::SQUARE_PRESSURE_MAX_TENTHS -
                           pressureTenths) *
      100L / range);
}

void sampleChannel(uint8_t channel) {
  pressureTenths_[channel] = readPressureTenths(channel);
  pressureValid_[channel] = true;
}

void stopStream() {
  streamActive_ = false;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    pressureValid_[channel] = false;
  }
}

void startStream(uint32_t now) {
  streamActive_ = true;
  scopeChannel_ = 0;
  labelChannel_ = 0;
  lastScopeWriteMs_ = now - SCOPE_CHANNEL_INTERVAL_MS;
  lastLabelWriteMs_ = now - LABEL_CHANNEL_INTERVAL_MS;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    pressureValid_[channel] = false;
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
}

void activate(uint32_t now) {
  formVisible_ = true;
  streamActive_ = false;
  displayedTestState_ = UINT8_MAX;
  statusSyncChannel_ = 0;
  lastDisplayWriteMs_ =
      now - cellq::config::MONITOR_DISPLAY_WRITE_INTERVAL_MS;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    pressureValid_[channel] = false;
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

void writeNextScope(uint32_t now) {
  sampleChannel(scopeChannel_);
  cellq::display::addMonitorScopeSample(
      scopeChannel_, pressureToScopeValue(pressureTenths_[scopeChannel_]));
  scopeChannel_ = (scopeChannel_ + 1U) % cellq::hw::CHANNEL_COUNT;
  lastScopeWriteMs_ = now;
}

void writeNextLabel(uint32_t now) {
  if (!pressureValid_[labelChannel_]) {
    sampleChannel(labelChannel_);
  }
  cellq::display::setMonitorPressureLabel(labelChannel_,
                                          pressureTenths_[labelChannel_]);
  DBG_MONITOR_PRESSURE(labelChannel_ + 1U,
                       pressureTenths_[labelChannel_]);
  labelChannel_ = (labelChannel_ + 1U) % cellq::hw::CHANNEL_COUNT;
  lastLabelWriteMs_ = now;
}
}

namespace cellq {
namespace monitor {
void begin() {
  formVisible_ = false;
  streamActive_ = false;
  displayedTestState_ = UINT8_MAX;
  statusSyncChannel_ = hw::CHANNEL_COUNT;
#if !CELLQ_VIRTUAL_PRESSURE_SENSORS
  calibratePressureZeros();
#endif
}

void update(uint32_t now) {
  if (!formVisible_) {
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
  if (statusSyncChannel_ < hw::CHANNEL_COUNT) {
    writeNextStatus();
  } else if (!streamActive_) {
    return;
  } else if (static_cast<uint32_t>(now - lastScopeWriteMs_) >=
             SCOPE_CHANNEL_INTERVAL_MS) {
    writeNextScope(now);
  } else if (static_cast<uint32_t>(now - lastLabelWriteMs_) >=
             LABEL_CHANNEL_INTERVAL_MS) {
    writeNextLabel(now);
  } else {
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

int16_t sampleChannelPressureTenths(uint8_t channel) {
  return channel < hw::CHANNEL_COUNT ? readPressureTenths(channel) : 0;
}
}
}
