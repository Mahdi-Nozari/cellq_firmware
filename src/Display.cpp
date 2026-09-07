#include "Display.h"
#include <genieArduino.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"

namespace {
Genie genie;
cellq::display::Event eventQueue[cellq::config::DISPLAY_EVENT_QUEUE_SIZE];
uint8_t queueHead=0;
uint8_t queueTail=0;

void onGenieEvent(){
  genieFrame frame;
  if(!genie.DequeueEvent(&frame))return;
  const uint16_t value=genie.GetEventData(&frame);
  DBG_EVENT(frame.reportObject.cmd,frame.reportObject.object,frame.reportObject.index,value);
  if(frame.reportObject.cmd!=GENIE_REPORT_EVENT)return;
  const uint8_t next=(queueHead+1U)%cellq::config::DISPLAY_EVENT_QUEUE_SIZE;
  if(next==queueTail){DBG_EVENT_DROP();return;}
  eventQueue[queueHead]={frame.reportObject.object,frame.reportObject.index,value};
  queueHead=next;
}
}

namespace cellq {
namespace display {
void begin(){
  queueHead=0;
  queueTail=0;
  Serial1.begin(config::DISPLAY_BAUD);
  genie.Begin(Serial1);
  genie.AttachEventHandler(onGenieEvent);
  pinMode(hw::DISPLAY_RESET_PIN,OUTPUT);
  digitalWrite(hw::DISPLAY_RESET_PIN,HIGH);
  delay(100);
  digitalWrite(hw::DISPLAY_RESET_PIN,LOW);
  delay(config::DISPLAY_STARTUP_MS);
  for(uint8_t channel=0;channel<hw::CHANNEL_COUNT;++channel)setManualSwitch(channel,false);
}

void service(){
  genie.DoEvents();
}

bool nextEvent(Event &event){
  if(queueTail==queueHead)return false;
  event=eventQueue[queueTail];
  queueTail=(queueTail+1U)%config::DISPLAY_EVENT_QUEUE_SIZE;
  return true;
}

void setManualSwitch(uint8_t channel,bool on){
  if(channel>=hw::CHANNEL_COUNT)return;
  genie.WriteObject(gui::ISWITCH_OBJECT,gui::MANUAL_VALVE_SWITCH_FIRST+channel,on?1:0);
}
}
}
