#pragma once
#include <Arduino.h>
#include <genieArduino.h>

namespace cellq {
namespace gui {
constexpr uint8_t USERBUTTON_OBJECT = GENIE_OBJ_USERBUTTON;
constexpr uint8_t KEYBOARD_OBJECT = GENIE_OBJ_KEYBOARD;
constexpr uint8_t USERIMAGES_OBJECT = GENIE_OBJ_USERIMAGES;
constexpr uint8_t SCOPE_OBJECT = GENIE_OBJ_SCOPE;
constexpr uint8_t ISWITCH_OBJECT = GENIE_OBJ_ISWITCH;
constexpr uint8_t FORM_OBJECT = GENIE_OBJ_FORM;
constexpr uint8_t ILABELB_OBJECT = GENIE_OBJ_ILABELB;
constexpr uint8_t HOME_FORM = 0;
constexpr uint8_t HOME_VERSION_LABEL = 69;
constexpr uint8_t MANUAL_FORM = 1;
constexpr uint8_t MANUAL_PRESSURE_MINUS[2] = {5, 6};
constexpr uint8_t MANUAL_PRESSURE_PLUS[2] = {4, 7};
constexpr uint8_t MANUAL_PRESSURE_LABEL[2] = {0, 1};
constexpr uint8_t MANUAL_VALVE_SWITCH_FIRST = 0;
constexpr uint8_t MANUAL_VALVE_SWITCH_LAST = 5;
constexpr uint8_t MANUAL_GROUP_LINK_SWITCH[2] = {9, 10};
constexpr uint8_t MANUAL_TIME_MINUS_FIRST = 9;
constexpr uint8_t MANUAL_TIME_MINUS_LAST = 14;
constexpr uint8_t MANUAL_TIME_PLUS_FIRST = 15;
constexpr uint8_t MANUAL_TIME_PLUS_LAST = 20;
constexpr uint8_t MANUAL_TIME_SELECTOR_FIRST = 109;
constexpr uint8_t MANUAL_TIME_SELECTOR_LAST = 114;
constexpr uint8_t MANUAL_TIME_UNIT_IMAGE_FIRST = 17;
constexpr uint8_t MANUAL_TIME_UNIT_IMAGE_LAST = 22;
constexpr uint8_t MANUAL_TIME_LABEL[3][6] = {
    {2, 3, 4, 5, 6, 7},
    {70, 71, 72, 73, 74, 75},
    {76, 77, 78, 79, 80, 81}};
constexpr uint8_t SQUARE_FORM = 2;
constexpr uint8_t MONITOR_FORM = 3;
constexpr uint8_t SQUARE_SETUP_FORM = 4;
constexpr uint8_t PRESET_NAME_FORM = 6;
constexpr uint8_t MONITOR_CHANNEL_IMAGE_FIRST = 0;
constexpr uint8_t MONITOR_CHANNEL_IMAGE_LAST = 5;
constexpr uint8_t MONITOR_PRESSURE_LABEL_FIRST = 8;
constexpr uint8_t MONITOR_PRESSURE_LABEL_LAST = 13;
constexpr uint8_t MONITOR_GROUP_SCOPE[2] = {1, 2};
constexpr uint8_t MONITOR_SCOPE_TRACES = 3;
constexpr uint8_t MONITOR_PAGE_STATUS_IMAGE = 14;
constexpr uint8_t SQUARE_NAME_LABEL = 14;
constexpr uint8_t SQUARE_GROUP_1_PRESSURE_LABEL = 15;
constexpr uint8_t SQUARE_GROUP_1_FREQUENCY_LABEL = 16;
constexpr uint8_t SQUARE_GROUP_1_DUTY_LABEL = 17;
constexpr uint8_t SQUARE_GROUP_1_DELAY_LABEL = 18;
constexpr uint8_t SQUARE_REMAINING_TIME_LABEL[2] = {19, 20};
constexpr uint8_t SQUARE_REMAINING_CYCLES_LABEL[2] = {21, 22};
constexpr uint8_t SQUARE_PROGRESS_LABEL = 24;
constexpr uint8_t SQUARE_GROUP_2_PRESSURE_LABEL = 25;
constexpr uint8_t SQUARE_GROUP_2_FREQUENCY_LABEL = 26;
constexpr uint8_t SQUARE_GROUP_2_DUTY_LABEL = 27;
constexpr uint8_t SQUARE_GROUP_2_DELAY_LABEL = 28;
constexpr uint8_t SQUARE_TOTAL_CYCLES_LABEL[2] = {65, 66};
constexpr uint8_t SQUARE_CHANNEL_IMAGE_FIRST = 6;
constexpr uint8_t SQUARE_CHANNEL_IMAGE_LAST = 11;
constexpr uint8_t SQUARE_STATUS_IMAGE = 12;
constexpr uint8_t SQUARE_PRESET_READY_IMAGE = 13;
constexpr uint8_t SQUARE_START_BUTTON = 27;
constexpr uint8_t SQUARE_PAUSE_BUTTON = 28;
constexpr uint8_t SQUARE_PRESET_SELECT_BUTTON = 30;
constexpr uint8_t SQUARE_SETUP_BUTTON = 24;
constexpr uint8_t MONITOR_SETUP_BUTTON = 26;
constexpr uint8_t SQUARE_PAUSE_AT_CYCLE_SWITCH = 7;
constexpr uint8_t SQUARE_DATA_LOG_SWITCH = 6;
constexpr uint8_t PRESET_SELECT_FORM = 7;
constexpr uint8_t PRESET_SELECT_BACK_BUTTON = 87;
constexpr uint8_t PRESET_SELECT_NAME_LABEL_FIRST = 46;
constexpr uint8_t PRESET_SELECT_NAME_LABEL_LAST = 63;
constexpr uint8_t PRESET_SELECT_BUTTON_FIRST = 88;
constexpr uint8_t PRESET_SELECT_BUTTON_LAST = 105;
constexpr uint8_t SETUP_NAME_LABEL = 36;
constexpr uint8_t SETUP_CYCLE_MODE_SWITCH = 8;
constexpr uint8_t SETUP_MODE_IMAGE[2] = {15, 16};
constexpr uint8_t SETUP_PARAMETER_COUNT = 5;
constexpr uint8_t SETUP_PARAMETER_LABEL[2][SETUP_PARAMETER_COUNT] = {
    {23, 29, 30, 31, 67}, {32, 33, 34, 35, 68}};
constexpr uint8_t SETUP_INCREASE_BUTTON[2][SETUP_PARAMETER_COUNT] = {
    {44, 45, 46, 47, 106}, {48, 49, 50, 51, 108}};
constexpr uint8_t SETUP_DECREASE_BUTTON[2][SETUP_PARAMETER_COUNT] = {
    {36, 37, 38, 39, 29}, {43, 42, 41, 40, 107}};
constexpr uint8_t SETUP_NEW_BUTTON = 54;
constexpr uint8_t SETUP_SAVE_BUTTON = 55;
constexpr uint8_t SETUP_DELETE_BUTTON = 56;
constexpr uint8_t SETUP_PRESET_SELECT_BUTTON = 53;
constexpr uint8_t SETUP_NAME_BUTTON = 52;
constexpr uint8_t PRESET_NAME_SET_BUTTON = 32;
constexpr uint8_t PRESET_NAME_BACK_BUTTON = 33;
constexpr uint8_t PRESET_NAME_LABEL = 64;
constexpr uint8_t PRESET_NAME_KEYBOARD = 0;
constexpr uint8_t SETUP_CHANNEL_BUTTON_FIRST = 57;
constexpr uint8_t SETUP_CHANNEL_BUTTON_LAST = 62;
}
}
