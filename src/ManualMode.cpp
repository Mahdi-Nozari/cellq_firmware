#include "ManualMode.h"
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"

namespace {
int16_t pressureTenths_[cellq::hw::REGULATOR_COUNT]={
  cellq::config::MANUAL_PRESSURE_DEFAULT_TENTHS,
  cellq::config::MANUAL_PRESSURE_DEFAULT_TENTHS
};
uint16_t onTimeSeconds_[cellq::hw::CHANNEL_COUNT]={};
uint16_t activeTimeSeconds_[cellq::hw::CHANNEL_COUNT]={};
uint32_t valveStartedMs_[cellq::hw::CHANNEL_COUNT]={};
bool valveOn_[cellq::hw::CHANNEL_COUNT]={};

void setValve(uint8_t channel,bool on,uint32_t now){
  if(channel>=cellq::hw::CHANNEL_COUNT)return;
  digitalWrite(cellq::hw::VALVE_PINS[channel],on?cellq::hw::VALVE_ON:cellq::hw::VALVE_OFF);
  valveOn_[channel]=on;
  if(on){
    valveStartedMs_[channel]=now;
    activeTimeSeconds_[channel]=onTimeSeconds_[channel];
  }else{
    activeTimeSeconds_[channel]=0;
  }
  DBG_VALVE(channel+1U,on?1:0);
}

void adjustPressure(uint8_t regulator,int8_t direction){
  if(regulator>=cellq::hw::REGULATOR_COUNT)return;
  int16_t next=pressureTenths_[regulator]+direction;
  if(next<cellq::config::MANUAL_PRESSURE_MIN_TENTHS)next=cellq::config::MANUAL_PRESSURE_MIN_TENTHS;
  if(next>cellq::config::MANUAL_PRESSURE_MAX_TENTHS)next=cellq::config::MANUAL_PRESSURE_MAX_TENTHS;
  pressureTenths_[regulator]=next;
  DBG_PRESSURE(regulator+1U,next);
}

void adjustTime(uint8_t channel,int8_t direction){
  if(channel>=cellq::hw::CHANNEL_COUNT)return;
  if(direction>0&&onTimeSeconds_[channel]<UINT16_MAX)++onTimeSeconds_[channel];
  if(direction<0&&onTimeSeconds_[channel]>0)--onTimeSeconds_[channel];
  DBG_TIME(channel+1U,onTimeSeconds_[channel]);
}
}

namespace cellq {
namespace manual {
void begin(){
  for(uint8_t channel=0;channel<hw::CHANNEL_COUNT;++channel){
    digitalWrite(hw::VALVE_PINS[channel],hw::VALVE_OFF);
    pinMode(hw::VALVE_PINS[channel],OUTPUT);
    digitalWrite(hw::VALVE_PINS[channel],hw::VALVE_OFF);
    onTimeSeconds_[channel]=0;
    activeTimeSeconds_[channel]=0;
    valveStartedMs_[channel]=0;
    valveOn_[channel]=false;
  }
}

void update(uint32_t now){
  for(uint8_t channel=0;channel<hw::CHANNEL_COUNT;++channel){
    if(!valveOn_[channel]||activeTimeSeconds_[channel]==0)continue;
    const uint32_t durationMs=static_cast<uint32_t>(activeTimeSeconds_[channel])*1000UL;
    if(static_cast<uint32_t>(now-valveStartedMs_[channel])<durationMs)continue;
    setValve(channel,false,now);
    display::setManualSwitch(channel,false);
    DBG_TIMEOUT(channel+1U);
  }
}

void handleDisplayEvent(const display::Event &event,uint32_t now){
  if(event.object==gui::ISWITCH_OBJECT&&event.index>=gui::MANUAL_VALVE_SWITCH_FIRST&&event.index<=gui::MANUAL_VALVE_SWITCH_LAST){
    setValve(event.index-gui::MANUAL_VALVE_SWITCH_FIRST,event.value!=0,now);
    return;
  }
  if(event.object!=gui::USERBUTTON_OBJECT)return;
  for(uint8_t regulator=0;regulator<hw::REGULATOR_COUNT;++regulator){
    if(event.index==gui::MANUAL_PRESSURE_MINUS[regulator]){adjustPressure(regulator,-1);return;}
    if(event.index==gui::MANUAL_PRESSURE_PLUS[regulator]){adjustPressure(regulator,1);return;}
  }
  if(event.index>=gui::MANUAL_TIME_MINUS_FIRST&&event.index<=gui::MANUAL_TIME_MINUS_LAST){
    adjustTime(event.index-gui::MANUAL_TIME_MINUS_FIRST,-1);
    return;
  }
  if(event.index>=gui::MANUAL_TIME_PLUS_FIRST&&event.index<=gui::MANUAL_TIME_PLUS_LAST){
    adjustTime(event.index-gui::MANUAL_TIME_PLUS_FIRST,1);
  }
}

int16_t pressureTenths(uint8_t regulator){
  return regulator<hw::REGULATOR_COUNT?pressureTenths_[regulator]:0;
}

uint16_t onTimeSeconds(uint8_t channel){
  return channel<hw::CHANNEL_COUNT?onTimeSeconds_[channel]:0;
}

bool valveIsOn(uint8_t channel){
  return channel<hw::CHANNEL_COUNT&&valveOn_[channel];
}
}
}
