#pragma once
#include <Arduino.h>

namespace cellq {
namespace regulator {
void begin();
void idle();
bool setPressureTenths(uint8_t regulator, int16_t tenths);
uint16_t pressureToDacCode(int16_t tenths);
}
}
