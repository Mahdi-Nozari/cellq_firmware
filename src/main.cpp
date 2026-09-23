#include <Arduino.h>
#include "Debug.h"
#include "Display.h"
#include "HardwareConfig.h"
#include "ManualMode.h"
#include "MonitorMode.h"
#include "PresetStore.h"
#include "RtcClock.h"
#include "SquareWaveMode.h"
#include "SquareWaveSetup.h"

namespace {
bool channel1LedOn_ = false;

void beginChannel1Led() {
  digitalWrite(cellq::hw::BUILTIN_LED_PIN, cellq::hw::BUILTIN_LED_OFF);
  pinMode(cellq::hw::BUILTIN_LED_PIN, OUTPUT);
  digitalWrite(cellq::hw::BUILTIN_LED_PIN, cellq::hw::BUILTIN_LED_OFF);
  channel1LedOn_ = false;
}

void updateChannel1Led() {
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
  if (cellq::square::isProcedureActive()) {
    return;
  }
  const bool channel1On =
      digitalRead(cellq::hw::VALVE_PINS[0]) == cellq::hw::VALVE_ON;
  if (channel1On == channel1LedOn_) {
    return;
  }
  digitalWrite(cellq::hw::BUILTIN_LED_PIN,
               channel1On ? cellq::hw::BUILTIN_LED_ON
                          : cellq::hw::BUILTIN_LED_OFF);
  channel1LedOn_ = channel1On;
#endif
}
}

void setup() {
  DBG_BEGIN();
  beginChannel1Led();
  cellq::manual::begin();
  cellq::clock::begin();
  cellq::square::begin();
  cellq::setup::begin();
  cellq::monitor::begin();
  cellq::display::begin();
  cellq::preset::begin();
  DBG_BOOT();
}

void loop() {
  const uint32_t now = millis();
  cellq::manual::update(now);
  cellq::square::update(now);
  cellq::setup::update(now);
  cellq::monitor::update(now);
  cellq::display::service();
  cellq::display::Event event;
  while (cellq::display::nextEvent(event)) {
    const uint32_t eventTime = millis();
    cellq::square::handleDisplayEvent(event, eventTime);
    cellq::setup::handleDisplayEvent(event, eventTime);
    cellq::monitor::handleDisplayEvent(event, eventTime);
    if (!cellq::square::isProcedureActive()) {
      cellq::manual::handleDisplayEvent(event, eventTime);
    }
  }
  updateChannel1Led();
}
