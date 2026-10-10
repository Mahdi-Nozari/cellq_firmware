#include "ManualMode.h"
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "Regulator.h"

namespace {
enum TimeUnit : uint8_t { Seconds = 0, Minutes = 1, Hours = 2 };
constexpr uint8_t GROUP_SIZE = 3U;
static_assert(GROUP_SIZE * cellq::hw::REGULATOR_COUNT ==
                  cellq::hw::CHANNEL_COUNT,
              "Manual groups must cover all channels");
int16_t pressureTenths_[cellq::hw::REGULATOR_COUNT] = {
    cellq::config::MANUAL_PRESSURE_DEFAULT_TENTHS,
    cellq::config::MANUAL_PRESSURE_DEFAULT_TENTHS};
uint8_t timeFields_[cellq::hw::CHANNEL_COUNT][3] = {};
uint8_t selectedTimeUnit_[cellq::hw::CHANNEL_COUNT] = {};
uint32_t activeTimeSeconds_[cellq::hw::CHANNEL_COUNT] = {};
uint32_t valveStartedMs_[cellq::hw::CHANNEL_COUNT] = {};
bool valveOn_[cellq::hw::CHANNEL_COUNT] = {};
bool groupLinked_[cellq::hw::REGULATOR_COUNT] = {};
bool active_ = false;
bool formVisible_ = false;

uint8_t groupFor(uint8_t channel) { return channel / GROUP_SIZE; }

uint8_t masterFor(uint8_t group) { return group * GROUP_SIZE; }

bool linkedFollower(uint8_t channel) {
  return groupLinked_[groupFor(channel)] &&
         channel != masterFor(groupFor(channel));
}

uint32_t configuredTimeSeconds(uint8_t channel) {
  return static_cast<uint32_t>(timeFields_[channel][Hours]) * 3600UL +
         static_cast<uint32_t>(timeFields_[channel][Minutes]) * 60UL +
         timeFields_[channel][Seconds];
}

void refreshLabels() {
  for (uint8_t regulator = 0; regulator < cellq::hw::REGULATOR_COUNT;
       ++regulator) {
    cellq::display::setManualPressureLabel(regulator,
                                           pressureTenths_[regulator]);
    cellq::display::setManualGroupLinkSwitch(regulator,
                                             groupLinked_[regulator]);
  }
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    for (uint8_t unit = Seconds; unit <= Hours; ++unit) {
      cellq::display::setManualTimeLabel(channel, unit,
                                        timeFields_[channel][unit]);
    }
    cellq::display::setManualTimeUnitImage(channel,
                                           selectedTimeUnit_[channel]);
    cellq::display::setManualSwitch(channel, valveOn_[channel]);
  }
  DBG_MANUAL_SYNC();
}

void idleOutputs() {
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    digitalWrite(cellq::hw::VALVE_PINS[channel], cellq::hw::VALVE_OFF);
    activeTimeSeconds_[channel] = 0;
    valveOn_[channel] = false;
  }
  cellq::regulator::idle();
}

void activate() {
  const bool wasActive = active_;
  active_ = true;
  formVisible_ = true;
  if (!wasActive) {
    for (uint8_t output = 0; output < cellq::hw::REGULATOR_COUNT; ++output) {
      cellq::regulator::setPressureTenths(output, pressureTenths_[output]);
    }
  }
  refreshLabels();
}

void deactivate() {
  formVisible_ = false;
  if (!active_) {
    return;
  }
  active_ = false;
  idleOutputs();
}

void setValve(uint8_t channel, bool on, uint32_t now) {
  if (channel >= cellq::hw::CHANNEL_COUNT) {
    return;
  }
  digitalWrite(cellq::hw::VALVE_PINS[channel],
               on ? cellq::hw::VALVE_ON : cellq::hw::VALVE_OFF);
  valveOn_[channel] = on;
  if (on) {
    valveStartedMs_[channel] = now;
    activeTimeSeconds_[channel] = configuredTimeSeconds(channel);
  } else {
    activeTimeSeconds_[channel] = 0;
  }
  DBG_VALVE(channel + 1U, on ? 1 : 0);
}

void setLinkedValves(uint8_t group, bool on, uint32_t now,
                     bool syncMasterSwitch) {
  const uint8_t master = masterFor(group);
  for (uint8_t channel = master; channel < master + GROUP_SIZE; ++channel) {
    setValve(channel, on, now);
  }
  if (!formVisible_) {
    return;
  }
  for (uint8_t channel = master; channel < master + GROUP_SIZE; ++channel) {
    if (channel != master || syncMasterSwitch) {
      cellq::display::setManualSwitch(channel, on);
    }
  }
}

void copyMasterTime(uint8_t group) {
  const uint8_t master = masterFor(group);
  for (uint8_t channel = master + 1U; channel < master + GROUP_SIZE;
       ++channel) {
    bool changed = false;
    for (uint8_t unit = Seconds; unit <= Hours; ++unit) {
      if (timeFields_[channel][unit] == timeFields_[master][unit]) {
        continue;
      }
      timeFields_[channel][unit] = timeFields_[master][unit];
      changed = true;
      if (formVisible_) {
        cellq::display::setManualTimeLabel(channel, unit,
                                            timeFields_[channel][unit]);
      }
    }
    if (selectedTimeUnit_[channel] != selectedTimeUnit_[master]) {
      selectedTimeUnit_[channel] = selectedTimeUnit_[master];
      if (formVisible_) {
        cellq::display::setManualTimeUnitImage(channel,
                                               selectedTimeUnit_[channel]);
      }
    }
    if (changed) {
      DBG_TIME(channel + 1U, configuredTimeSeconds(channel));
    }
  }
}

void adjustPressure(uint8_t regulator, int8_t direction) {
  if (regulator >= cellq::hw::REGULATOR_COUNT) {
    return;
  }
  int16_t next = pressureTenths_[regulator] +
                 direction * cellq::config::MANUAL_PRESSURE_STEP_TENTHS;
  if (next < cellq::config::MANUAL_PRESSURE_MIN_TENTHS) {
    next = cellq::config::MANUAL_PRESSURE_MIN_TENTHS;
  }
  if (next > cellq::config::MANUAL_PRESSURE_MAX_TENTHS) {
    next = cellq::config::MANUAL_PRESSURE_MAX_TENTHS;
  }
  pressureTenths_[regulator] = next;
  cellq::regulator::setPressureTenths(regulator, next);
  cellq::display::setManualPressureLabel(regulator, next);
  DBG_PRESSURE(regulator + 1U, next);
}

void adjustTime(uint8_t channel, int8_t direction) {
  if (channel >= cellq::hw::CHANNEL_COUNT || linkedFollower(channel)) {
    return;
  }
  const uint8_t unit = selectedTimeUnit_[channel];
  uint8_t &value = timeFields_[channel][unit];
  const uint8_t maximum =
      unit == Hours
          ? cellq::config::MANUAL_TIME_MAX_HOURS
          : (unit == Minutes ? cellq::config::MANUAL_TIME_MAX_MINUTES
                             : cellq::config::MANUAL_TIME_MAX_SECONDS);
  if (unit == Hours) {
    if (direction > 0 && value < maximum) {
      ++value;
    } else if (direction < 0 && value > 0U) {
      --value;
    } else {
      return;
    }
  } else if (direction > 0) {
    value = value == maximum ? 0U : value + 1U;
  } else if (direction < 0) {
    value = value == 0U ? maximum : value - 1U;
  } else {
    return;
  }
  cellq::display::setManualTimeLabel(channel, unit, value);
  DBG_TIME(channel + 1U, configuredTimeSeconds(channel));
  const uint8_t group = groupFor(channel);
  if (groupLinked_[group]) {
    copyMasterTime(group);
  }
}
}

namespace cellq {
namespace manual {
void begin() {
  active_ = false;
  formVisible_ = false;
  for (uint8_t regulator = 0; regulator < hw::REGULATOR_COUNT;
       ++regulator) {
    pressureTenths_[regulator] = config::MANUAL_PRESSURE_DEFAULT_TENTHS;
    groupLinked_[regulator] = false;
  }
  for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
    digitalWrite(hw::VALVE_PINS[channel], hw::VALVE_OFF);
    pinMode(hw::VALVE_PINS[channel], OUTPUT);
    digitalWrite(hw::VALVE_PINS[channel], hw::VALVE_OFF);
    timeFields_[channel][Seconds] = config::MANUAL_TIME_DEFAULT_SECONDS;
    timeFields_[channel][Minutes] = config::MANUAL_TIME_DEFAULT_MINUTES;
    timeFields_[channel][Hours] = config::MANUAL_TIME_DEFAULT_HOURS;
    selectedTimeUnit_[channel] = Seconds;
    activeTimeSeconds_[channel] = 0;
    valveStartedMs_[channel] = 0;
    valveOn_[channel] = false;
  }
  regulator::begin();
}

void update(uint32_t now) {
  if (!active_) {
    return;
  }
  for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
    if (!valveOn_[channel] || activeTimeSeconds_[channel] == 0) {
      continue;
    }
    const uint32_t durationMs =
        static_cast<uint32_t>(activeTimeSeconds_[channel]) * 1000UL;
    if (static_cast<uint32_t>(now - valveStartedMs_[channel]) < durationMs) {
      continue;
    }
    const uint8_t group = groupFor(channel);
    if (groupLinked_[group] && channel == masterFor(group)) {
      setLinkedValves(group, false, now, true);
    } else {
      setValve(channel, false, now);
      if (formVisible_) {
        display::setManualSwitch(channel, false);
      }
    }
    DBG_TIMEOUT(channel + 1U);
  }
}

void handleDisplayEvent(const display::Event &event, uint32_t now) {
  if (event.object == gui::FORM_OBJECT) {
    if (event.index == gui::MANUAL_FORM) {
      activate();
    } else if (event.index == gui::HOME_FORM) {
      formVisible_ = false;
    } else {
      deactivate();
    }
    return;
  }
  if (!active_ || !formVisible_) {
    return;
  }
  if (event.object == gui::ISWITCH_OBJECT) {
    if (event.value > 1U) {
      return;
    }
    for (uint8_t group = 0; group < hw::REGULATOR_COUNT; ++group) {
      if (event.index == gui::MANUAL_GROUP_LINK_SWITCH[group]) {
        if (groupLinked_[group] != (event.value != 0U)) {
          groupLinked_[group] = event.value != 0U;
          if (groupLinked_[group]) {
            copyMasterTime(group);
          }
        }
        return;
      }
    }
    if (event.index <= gui::MANUAL_VALVE_SWITCH_LAST) {
      const uint8_t channel =
          event.index - gui::MANUAL_VALVE_SWITCH_FIRST;
      const bool on = event.value != 0U;
      const uint8_t group = groupFor(channel);
      if (groupLinked_[group] && channel == masterFor(group)) {
        setLinkedValves(group, on, now, false);
      } else if (valveOn_[channel] != on) {
        setValve(channel, on, now);
      }
    }
    return;
  }
  if (event.object != gui::USERBUTTON_OBJECT) {
    return;
  }
  if (event.index >= gui::MANUAL_TIME_SELECTOR_FIRST &&
      event.index <= gui::MANUAL_TIME_SELECTOR_LAST) {
    if (event.value != 0U) {
      return;
    }
    const uint8_t channel = event.index - gui::MANUAL_TIME_SELECTOR_FIRST;
    if (linkedFollower(channel)) {
      return;
    }
    if (selectedTimeUnit_[channel] == Hours) {
      selectedTimeUnit_[channel] = Seconds;
    } else {
      ++selectedTimeUnit_[channel];
    }
    display::setManualTimeUnitImage(channel, selectedTimeUnit_[channel]);
    DBG_MANUAL_UNIT(channel + 1U, selectedTimeUnit_[channel]);
    const uint8_t group = groupFor(channel);
    if (groupLinked_[group]) {
      copyMasterTime(group);
    }
    return;
  }
  for (uint8_t regulator = 0; regulator < hw::REGULATOR_COUNT;
       ++regulator) {
    if (event.index == gui::MANUAL_PRESSURE_MINUS[regulator]) {
      adjustPressure(regulator, -1);
      return;
    }
    if (event.index == gui::MANUAL_PRESSURE_PLUS[regulator]) {
      adjustPressure(regulator, 1);
      return;
    }
  }
  if (event.index >= gui::MANUAL_TIME_MINUS_FIRST &&
      event.index <= gui::MANUAL_TIME_MINUS_LAST) {
    adjustTime(event.index - gui::MANUAL_TIME_MINUS_FIRST, -1);
    return;
  }
  if (event.index >= gui::MANUAL_TIME_PLUS_FIRST &&
      event.index <= gui::MANUAL_TIME_PLUS_LAST) {
    adjustTime(event.index - gui::MANUAL_TIME_PLUS_FIRST, 1);
  }
}

int16_t pressureTenths(uint8_t regulator) {
  return regulator < hw::REGULATOR_COUNT ? pressureTenths_[regulator] : 0;
}

uint32_t onTimeSeconds(uint8_t channel) {
  return channel < hw::CHANNEL_COUNT ? configuredTimeSeconds(channel) : 0;
}

bool valveIsOn(uint8_t channel) {
  return channel < hw::CHANNEL_COUNT && valveOn_[channel];
}

void enterMaintenance() {
  deactivate();
}
}
}
