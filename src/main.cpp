#include <Arduino.h>
#include "Debug.h"
#include "Display.h"
#include "ManualMode.h"
#include "SquareWaveMode.h"

void setup() {
  DBG_BEGIN();
  cellq::manual::begin();
  cellq::square::begin();
  cellq::display::begin();
  DBG_BOOT();
}

void loop() {
  const uint32_t now = millis();
  cellq::manual::update(now);
  cellq::square::update(now);
  cellq::display::service();
  cellq::display::Event event;
  while (cellq::display::nextEvent(event)) {
    const uint32_t eventTime = millis();
    cellq::square::handleDisplayEvent(event, eventTime);
    cellq::manual::handleDisplayEvent(event, eventTime);
  }
}
