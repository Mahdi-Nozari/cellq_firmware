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
constexpr uint8_t HOME_FORM = 0;
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
constexpr uint8_t MONITOR_FORM = 3;
constexpr uint8_t MONITOR_CHANNEL_IMAGE_FIRST = 0;
constexpr uint8_t MONITOR_CHANNEL_IMAGE_LAST = 5;
constexpr uint8_t MONITOR_PRESSURE_LABEL_FIRST = 8;
constexpr uint8_t MONITOR_PRESSURE_LABEL_LAST = 13;
constexpr uint8_t MONITOR_SCOPE_FIRST = 1;
constexpr uint8_t MONITOR_SCOPE_LAST = 6;
constexpr uint8_t SQUARE_NAME_LABEL = 14;
constexpr uint8_t SQUARE_GROUP_2_PRESSURE_LABEL = 15;
constexpr uint8_t SQUARE_GROUP_2_FREQUENCY_LABEL = 16;
constexpr uint8_t SQUARE_GROUP_2_ON_TIME_LABEL = 17;
constexpr uint8_t SQUARE_GROUP_2_DELAY_LABEL = 18;
constexpr uint8_t SQUARE_REMAINING_TIME_LABEL[2] = {19, 20};
constexpr uint8_t SQUARE_REMAINING_CYCLES_LABEL[2] = {21, 22};
constexpr uint8_t SQUARE_PROGRESS_LABEL = 24;
constexpr uint8_t SQUARE_GROUP_1_PRESSURE_LABEL = 25;
constexpr uint8_t SQUARE_GROUP_1_FREQUENCY_LABEL = 26;
constexpr uint8_t SQUARE_GROUP_1_ON_TIME_LABEL = 27;
constexpr uint8_t SQUARE_GROUP_1_DELAY_LABEL = 28;
constexpr uint8_t SQUARE_CHANNEL_IMAGE_FIRST = 6;
constexpr uint8_t SQUARE_CHANNEL_IMAGE_LAST = 11;
constexpr uint8_t SQUARE_STATUS_IMAGE = 12;
constexpr uint8_t SQUARE_PRESET_READY_IMAGE = 13;
constexpr uint8_t SQUARE_START_BUTTON = 27;
constexpr uint8_t SQUARE_PAUSE_BUTTON = 28;
constexpr uint8_t SQUARE_PAUSE_AT_CYCLE_SWITCH = 7;
constexpr uint8_t SQUARE_GROUP_SCOPE[2] = {0, 7};
constexpr uint8_t PRESET_SELECT_FORM = 7;
constexpr uint8_t PRESET_SELECT_NAME_LABEL_FIRST = 46;
constexpr uint8_t PRESET_SELECT_NAME_LABEL_LAST = 63;
constexpr uint8_t PRESET_SELECT_BUTTON_FIRST = 88;
constexpr uint8_t PRESET_SELECT_BUTTON_LAST = 105;
}
}
