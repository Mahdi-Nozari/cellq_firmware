#pragma once
#define CELLQ_DEBUG 1

#include <stdint.h>

#if CELLQ_DEBUG!=0 && CELLQ_DEBUG!=1
#error CELLQ_DEBUG must be 0 or 1
#endif

namespace cellq {
namespace config {
constexpr uint32_t DEBUG_BAUD=250000UL;
constexpr uint32_t DISPLAY_BAUD=200000UL;
constexpr uint16_t DISPLAY_STARTUP_MS=3500U;
constexpr uint8_t DISPLAY_EVENT_QUEUE_SIZE=8U;
constexpr int16_t MANUAL_PRESSURE_MIN_TENTHS=0;
constexpr int16_t MANUAL_PRESSURE_MAX_TENTHS=50;
constexpr int16_t MANUAL_PRESSURE_DEFAULT_TENTHS=10;
constexpr uint16_t MANUAL_TIME_DEFAULT_SECONDS=0U;
}
}