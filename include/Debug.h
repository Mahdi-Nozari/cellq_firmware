#pragma once
#include <Arduino.h>
#include "BuildConfig.h"

#if CELLQ_DEBUG
#define DBG_BEGIN()                                                           \
  do {                                                                        \
    Serial.begin(cellq::config::DEBUG_BAUD);                                  \
  } while (0)

#define DBG_BOOT()                                                            \
  do {                                                                        \
    if (Serial.availableForWrite() >= 11) {                                   \
      Serial.println(F("D,BOOT,OK"));                                         \
    }                                                                         \
  } while (0)

#define DBG_EVENT(c, o, i, v)                                                 \
  do {                                                                        \
    if (Serial.availableForWrite() >= 32) {                                   \
      Serial.print(F("D,E,"));                                                \
      Serial.print(c);                                                        \
      Serial.print(',');                                                      \
      Serial.print(o);                                                        \
      Serial.print(',');                                                      \
      Serial.print(i);                                                        \
      Serial.print(',');                                                      \
      Serial.println(v);                                                      \
    }                                                                         \
  } while (0)

#define DBG_EVENT_DROP()                                                      \
  do {                                                                        \
    if (Serial.availableForWrite() >= 10) {                                   \
      Serial.println(F("D,E,DROP"));                                         \
    }                                                                         \
  } while (0)

#define DBG_PRESSURE(r, v)                                                    \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,P,"));                                                \
      Serial.print(r);                                                        \
      Serial.print(',');                                                      \
      Serial.println(v);                                                      \
    }                                                                         \
  } while (0)

#define DBG_DAC(r, c, o)                                                      \
  do {                                                                        \
    if (Serial.availableForWrite() >= 32) {                                   \
      Serial.print(F("D,DAC,"));                                              \
      Serial.print(r);                                                        \
      Serial.print(',');                                                      \
      Serial.print(c);                                                        \
      Serial.print(',');                                                      \
      Serial.println(o);                                                      \
    }                                                                         \
  } while (0)

#define DBG_TIME(c, v)                                                        \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,T,"));                                                \
      Serial.print(c);                                                        \
      Serial.print(',');                                                      \
      Serial.println(v);                                                      \
    }                                                                         \
  } while (0)

#define DBG_VALVE(c, v)                                                       \
  do {                                                                        \
    if (Serial.availableForWrite() >= 18) {                                   \
      Serial.print(F("D,V,"));                                                \
      Serial.print(c);                                                        \
      Serial.print(',');                                                      \
      Serial.println(v);                                                      \
    }                                                                         \
  } while (0)

#define DBG_TIMEOUT(c)                                                        \
  do {                                                                        \
    if (Serial.availableForWrite() >= 18) {                                   \
      Serial.print(F("D,TO,"));                                               \
      Serial.println(c);                                                      \
    }                                                                         \
  } while (0)

#define DBG_MANUAL_SYNC()                                                     \
  do {                                                                        \
    if (Serial.availableForWrite() >= 10) {                                   \
      Serial.println(F("D,M,SYNC"));                                         \
    }                                                                         \
  } while (0)

#define DBG_MONITOR_PRESSURE(c, v)                                            \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,MON,P,"));                                           \
      Serial.print(c);                                                        \
      Serial.print(',');                                                      \
      Serial.println(v);                                                      \
    }                                                                         \
  } while (0)

#define DBG_MONITOR_SOURCE(message)                                           \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,MON,SRC,"));                                         \
      Serial.println(F(message));                                             \
    }                                                                         \
  } while (0)

#define DBG_SD_OK()                                                           \
  do {                                                                        \
    if (Serial.availableForWrite() >= 10) {                                   \
      Serial.println(F("D,SD,OK"));                                          \
    }                                                                         \
  } while (0)

#define DBG_SD_FAIL()                                                         \
  do {                                                                        \
    if (Serial.availableForWrite() >= 12) {                                   \
      Serial.println(F("D,SD,FAIL"));                                        \
    }                                                                         \
  } while (0)

#define DBG_PRESET_LOAD(i, r)                                                 \
  do {                                                                        \
    if (Serial.availableForWrite() >= 30) {                                   \
      Serial.print(F("D,PR,LOAD,"));                                         \
      Serial.print(i);                                                        \
      Serial.print(',');                                                      \
      Serial.println(r);                                                      \
    }                                                                         \
  } while (0)

#define DBG_PRESET_ERROR(i, code)                                             \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,PR,ERR,"));                                          \
      Serial.print(i);                                                        \
      Serial.print(',');                                                      \
      Serial.println(F(code));                                                \
    }                                                                         \
  } while (0)

#define DBG_PRESET_CACHE(i)                                                   \
  do {                                                                        \
    if (Serial.availableForWrite() >= 18) {                                   \
      Serial.print(F("D,SW,CACHE,"));                                        \
      Serial.println(i);                                                      \
    }                                                                         \
  } while (0)

#define DBG_PRESET_FLASH(i)                                                   \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,SW,LOAD,"));                                         \
      Serial.print(i);                                                        \
      Serial.println(F(",FLASH"));                                           \
    }                                                                         \
  } while (0)

#define DBG_SQUARE(message)                                                   \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,SW,"));                                               \
      Serial.println(F(message));                                             \
    }                                                                         \
  } while (0)

#define DBG_SETUP(message)                                                    \
  do {                                                                        \
    if (Serial.availableForWrite() >= 24) {                                   \
      Serial.print(F("D,SET,"));                                             \
      Serial.println(F(message));                                             \
    }                                                                         \
  } while (0)

#define DBG_PRESET_SAVE(i, r)                                                 \
  do {                                                                        \
    if (Serial.availableForWrite() >= 30) {                                   \
      Serial.print(F("D,PR,SAVE,"));                                         \
      Serial.print(i);                                                        \
      Serial.print(',');                                                      \
      Serial.println(r);                                                      \
    }                                                                         \
  } while (0)

#define DBG_PRESET_DELETE(i)                                                  \
  do {                                                                        \
    if (Serial.availableForWrite() >= 20) {                                   \
      Serial.print(F("D,PR,DELETE,"));                                       \
      Serial.println(i);                                                      \
    }                                                                         \
  } while (0)

#define DBG_RTC(message)                                                      \
  do {                                                                        \
    if (Serial.availableForWrite() >= 18) {                                   \
      Serial.print(F("D,RTC,"));                                             \
      Serial.println(F(message));                                             \
    }                                                                         \
  } while (0)
#else
#define DBG_BEGIN() do { } while (0)
#define DBG_BOOT() do { } while (0)
#define DBG_EVENT(c, o, i, v) do { } while (0)
#define DBG_EVENT_DROP() do { } while (0)
#define DBG_PRESSURE(r, v) do { } while (0)
#define DBG_DAC(r, c, o) do { } while (0)
#define DBG_TIME(c, v) do { } while (0)
#define DBG_VALVE(c, v) do { } while (0)
#define DBG_TIMEOUT(c) do { } while (0)
#define DBG_MANUAL_SYNC() do { } while (0)
#define DBG_MONITOR_PRESSURE(c, v) do { } while (0)
#define DBG_MONITOR_SOURCE(message) do { } while (0)
#define DBG_SD_OK() do { } while (0)
#define DBG_SD_FAIL() do { } while (0)
#define DBG_PRESET_LOAD(i, r) do { } while (0)
#define DBG_PRESET_ERROR(i, code) do { } while (0)
#define DBG_PRESET_CACHE(i) do { } while (0)
#define DBG_PRESET_FLASH(i) do { } while (0)
#define DBG_SQUARE(message) do { } while (0)
#define DBG_SETUP(message) do { } while (0)
#define DBG_PRESET_SAVE(i, r) do { } while (0)
#define DBG_PRESET_DELETE(i) do { } while (0)
#define DBG_RTC(message) do { } while (0)
#endif
