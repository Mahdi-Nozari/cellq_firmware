#pragma once
#include <Arduino.h>
#include <genieArduino.h>

namespace cellq {
namespace gui {
constexpr uint8_t USERBUTTON_OBJECT=GENIE_OBJ_USERBUTTON;
constexpr uint8_t ISWITCH_OBJECT=GENIE_OBJ_ISWITCH;
constexpr uint8_t FORM_OBJECT=GENIE_OBJ_FORM;
constexpr uint8_t ILABELB_OBJECT=GENIE_OBJ_ILABELB;
constexpr uint8_t MANUAL_FORM=1;
constexpr uint8_t MANUAL_PRESSURE_MINUS[2]={5,6};
constexpr uint8_t MANUAL_PRESSURE_PLUS[2]={4,7};
constexpr uint8_t MANUAL_PRESSURE_LABEL[2]={0,1};
constexpr uint8_t MANUAL_VALVE_SWITCH_FIRST=0;
constexpr uint8_t MANUAL_VALVE_SWITCH_LAST=5;
constexpr uint8_t MANUAL_TIME_MINUS_FIRST=9;
constexpr uint8_t MANUAL_TIME_MINUS_LAST=14;
constexpr uint8_t MANUAL_TIME_PLUS_FIRST=15;
constexpr uint8_t MANUAL_TIME_PLUS_LAST=20;
constexpr uint8_t MANUAL_TIME_LABEL_FIRST=2;
constexpr uint8_t MANUAL_TIME_LABEL_LAST=7;
}
}
