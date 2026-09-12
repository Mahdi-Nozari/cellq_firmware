#pragma once
#include <Arduino.h>
#include <genieArduino.h>

namespace cellq {
namespace gui {
constexpr uint8_t USERBUTTON_OBJECT = GENIE_OBJ_USERBUTTON;
constexpr uint8_t USERIMAGES_OBJECT = GENIE_OBJ_USERIMAGES;
constexpr uint8_t SCOPE_OBJECT = GENIE_OBJ_SCOPE;
constexpr uint8_t ISWITCH_OBJECT = GENIE_OBJ_ISWITCH;
constexpr uint8_t FORM_OBJECT = GENIE_OBJ_FORM;
constexpr uint8_t ILABELB_OBJECT = GENIE_OBJ_ILABELB;
constexpr uint8_t MANUAL_FORM = 1;
constexpr uint8_t MANUAL_PRESSURE_MINUS[2] = {5, 6};
constexpr uint8_t MANUAL_PRESSURE_PLUS[2] = {4, 7};
constexpr uint8_t MANUAL_PRESSURE_LABEL[2] = {0, 1};
constexpr uint8_t MANUAL_VALVE_SWITCH_FIRST = 0;
constexpr uint8_t MANUAL_VALVE_SWITCH_LAST = 5;
constexpr uint8_t MANUAL_TIME_MINUS_FIRST = 9;
constexpr uint8_t MANUAL_TIME_MINUS_LAST = 14;
constexpr uint8_t MANUAL_TIME_PLUS_FIRST = 15;
constexpr uint8_t MANUAL_TIME_PLUS_LAST = 20;
constexpr uint8_t MANUAL_TIME_LABEL_FIRST = 2;
constexpr uint8_t MANUAL_TIME_LABEL_LAST = 7;
constexpr uint8_t SQUARE_FORM = 2;
constexpr uint8_t SQUARE_NAME_LABEL = 14;
constexpr uint8_t SQUARE_PRESSURE_1_LABEL = 15;
constexpr uint8_t SQUARE_PRESSURE_2_LABEL = 16;
constexpr uint8_t SQUARE_ON_TIME_LABEL = 17;
constexpr uint8_t SQUARE_FREQUENCY_LABEL = 18;
constexpr uint8_t SQUARE_ELAPSED_TIME_LABEL = 19;
constexpr uint8_t SQUARE_TOTAL_TIME_LABEL = 20;
constexpr uint8_t SQUARE_ELAPSED_CYCLES_LABEL = 21;
constexpr uint8_t SQUARE_TOTAL_CYCLES_LABEL = 22;
constexpr uint8_t SQUARE_ACTIVE_NAME_LABEL = 23;
constexpr uint8_t SQUARE_CHANNEL_IMAGE_FIRST = 6;
constexpr uint8_t SQUARE_CHANNEL_IMAGE_LAST = 11;
constexpr uint8_t SQUARE_STATUS_IMAGE = 12;
constexpr uint8_t SQUARE_START_BUTTON = 27;
constexpr uint8_t SQUARE_PAUSE_BUTTON = 28;
constexpr uint8_t SQUARE_SCOPE = 0;
}
}
