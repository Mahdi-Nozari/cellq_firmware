#include "RtcClock.h"
#include <RTClib.h>
#include <string.h>
#include "Debug.h"

namespace {
RTC_PCF8523 rtc;
bool rtcReady_ = false;

void writeTwoDigits(char *output, uint8_t value) {
  output[0] = static_cast<char>('0' + value / 10U);
  output[1] = static_cast<char>('0' + value % 10U);
}

void writeFourDigits(char *output, uint16_t value) {
  output[0] = static_cast<char>('0' + value / 1000U % 10U);
  output[1] = static_cast<char>('0' + value / 100U % 10U);
  output[2] = static_cast<char>('0' + value / 10U % 10U);
  output[3] = static_cast<char>('0' + value % 10U);
}
}

namespace cellq {
namespace clock {
void begin() {
  rtcReady_ = rtc.begin();
  if (!rtcReady_) {
    DBG_RTC("FAIL");
    return;
  }
  rtc.start();
  rtcReady_ = rtc.initialized() && !rtc.lostPower();
  if (rtcReady_) {
    DBG_RTC("OK");
  } else {
    DBG_RTC("INVALID");
  }
}

bool timestamp(char output[16]) {
  if (!rtcReady_) {
    strcpy(output, "00000000T000000");
    return false;
  }
  const DateTime now = rtc.now();
  if (!now.isValid() || now.year() > 9999U) {
    strcpy(output, "00000000T000000");
    DBG_RTC("INVALID");
    return false;
  }
  writeFourDigits(output, now.year());
  writeTwoDigits(output + 4, now.month());
  writeTwoDigits(output + 6, now.day());
  output[8] = 'T';
  writeTwoDigits(output + 9, now.hour());
  writeTwoDigits(output + 11, now.minute());
  writeTwoDigits(output + 13, now.second());
  output[15] = '\0';
  return true;
}
}
}
