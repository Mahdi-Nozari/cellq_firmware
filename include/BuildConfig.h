#pragma once
#define CELLQ_DEBUG 1

#include <stdint.h>

#if CELLQ_DEBUG != 0 && CELLQ_DEBUG != 1
#error CELLQ_DEBUG must be 0 or 1
#endif

namespace cellq {
namespace config {
constexpr uint32_t DEBUG_BAUD = 250000UL;
constexpr uint32_t DISPLAY_BAUD = 200000UL;
constexpr uint16_t DISPLAY_STARTUP_MS = 3500U;
constexpr uint8_t DISPLAY_EVENT_QUEUE_SIZE = 8U;
constexpr int16_t MANUAL_PRESSURE_MIN_TENTHS = -800;
constexpr int16_t MANUAL_PRESSURE_MAX_TENTHS = 0;
constexpr int16_t MANUAL_PRESSURE_DEFAULT_TENTHS = -800;
constexpr int16_t MANUAL_PRESSURE_STEP_TENTHS = 10;
constexpr uint16_t REGULATOR_FULL_SCALE_TENTHS = 820U;
constexpr uint16_t REGULATOR_DAC_MAX_CODE = 4095U;
constexpr uint16_t REGULATOR_SAFE_DAC_CODE = 0U;
constexpr uint32_t I2C_TIMEOUT_US = 25000UL;
constexpr uint16_t MANUAL_TIME_DEFAULT_SECONDS = 0U;
constexpr uint16_t SQUARE_DISPLAY_WRITE_INTERVAL_MS = 10U;
constexpr uint16_t SQUARE_RUNTIME_DISPLAY_INTERVAL_MS = 1000U;
constexpr uint16_t SQUARE_SCOPE_SAMPLE_COUNT = 100U;
constexpr uint16_t SQUARE_SCOPE_PREVIEW_MS = 2000U;
constexpr uint16_t SQUARE_SCOPE_LOW_VALUE = 0U;
constexpr uint16_t SQUARE_SCOPE_HIGH_VALUE = 100U;
}
}
