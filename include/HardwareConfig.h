#pragma once
#include <Arduino.h>

namespace cellq {
namespace hw {
constexpr uint8_t CHANNEL_COUNT=6;
constexpr uint8_t REGULATOR_COUNT=2;
constexpr uint8_t DISPLAY_RESET_PIN=4;
constexpr uint8_t SD_CS_PIN=10;
constexpr uint8_t VALVE_ON=HIGH;
constexpr uint8_t VALVE_OFF=LOW;
constexpr uint8_t VALVE_PINS[CHANNEL_COUNT]={5,6,7,9,11,12};
constexpr uint8_t PRESSURE_PINS[CHANNEL_COUNT]={A8,A9,A10,A11,A12,A13};
constexpr uint8_t REGULATOR_FEEDBACK_PINS[REGULATOR_COUNT]={A1,A2};
constexpr uint8_t DAC_ADDRESSES[REGULATOR_COUNT]={0x63,0x62};
constexpr uint8_t CHANNEL_TO_REGULATOR[CHANNEL_COUNT]={0,0,0,1,1,1};
}
}
