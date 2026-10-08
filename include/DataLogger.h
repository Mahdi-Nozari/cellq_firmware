#pragma once
#include <Arduino.h>
#include "SquareWavePreset.h"

namespace cellq {
namespace logger {
enum class State : uint8_t { Running, PausePending, Paused };
enum class EndReason : uint8_t { Done, Stopped, Error };

void begin();
bool enabled();
bool setEnabled(bool enabled, uint32_t now);
bool startTest(const SquareWavePreset &preset, uint32_t now);
bool update(uint32_t now, State state,
            const uint32_t completedCycles[2]);
bool finish(uint32_t now, EndReason reason,
            const uint32_t completedCycles[2]);
void abort();
void disableAfterFailure();
}
}
