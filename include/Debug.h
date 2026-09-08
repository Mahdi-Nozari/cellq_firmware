#pragma once
#include <Arduino.h>
#include "BuildConfig.h"

#if CELLQ_DEBUG
#define DBG_BEGIN() do{Serial.begin(cellq::config::DEBUG_BAUD);}while(0)
#define DBG_BOOT() do{if(Serial.availableForWrite()>=11)Serial.println(F("D,BOOT,OK"));}while(0)
#define DBG_EVENT(c,o,i,v) do{if(Serial.availableForWrite()>=32){Serial.print(F("D,E,"));Serial.print(c);Serial.print(',');Serial.print(o);Serial.print(',');Serial.print(i);Serial.print(',');Serial.println(v);}}while(0)
#define DBG_EVENT_DROP() do{if(Serial.availableForWrite()>=10)Serial.println(F("D,E,DROP"));}while(0)
#define DBG_PRESSURE(r,v) do{if(Serial.availableForWrite()>=24){Serial.print(F("D,P,"));Serial.print(r);Serial.print(',');Serial.println(v);}}while(0)
#define DBG_TIME(c,v) do{if(Serial.availableForWrite()>=24){Serial.print(F("D,T,"));Serial.print(c);Serial.print(',');Serial.println(v);}}while(0)
#define DBG_VALVE(c,v) do{if(Serial.availableForWrite()>=18){Serial.print(F("D,V,"));Serial.print(c);Serial.print(',');Serial.println(v);}}while(0)
#define DBG_TIMEOUT(c) do{if(Serial.availableForWrite()>=18){Serial.print(F("D,TO,"));Serial.println(c);}}while(0)
#define DBG_MANUAL_SYNC() do{if(Serial.availableForWrite()>=10)Serial.println(F("D,M,SYNC"));}while(0)
#else
#define DBG_BEGIN() do{}while(0)
#define DBG_BOOT() do{}while(0)
#define DBG_EVENT(c,o,i,v) do{}while(0)
#define DBG_EVENT_DROP() do{}while(0)
#define DBG_PRESSURE(r,v) do{}while(0)
#define DBG_TIME(c,v) do{}while(0)
#define DBG_VALVE(c,v) do{}while(0)
#define DBG_TIMEOUT(c) do{}while(0)
#define DBG_MANUAL_SYNC() do{}while(0)
#endif
