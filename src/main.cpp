#include <Arduino.h>
#include "Debug.h"
#include "Display.h"
#include "ManualMode.h"

void setup(){
  DBG_BEGIN();
  cellq::manual::begin();
  cellq::display::begin();
  DBG_BOOT();
}

void loop(){
  cellq::manual::update(millis());
  cellq::display::service();
  cellq::display::Event event;
  while(cellq::display::nextEvent(event))cellq::manual::handleDisplayEvent(event,millis());
}
