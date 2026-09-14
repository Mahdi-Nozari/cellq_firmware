#include "WaveformTimer.h"
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "BuildConfig.h"
#include "HardwareConfig.h"

namespace {
enum class Phase : uint8_t { Delay, On, Off };

constexpr uint32_t TIMER_PRESCALER = 64UL;
constexpr uint32_t TIMER_TICKS_PER_SECOND = F_CPU / TIMER_PRESCALER;
constexpr uint32_t TIMER_MAX_SEGMENT_TICKS = 65536UL;
constexpr uint8_t TIMER_CLOCK_BITS = _BV(CS51) | _BV(CS50);
static_assert(F_CPU % TIMER_PRESCALER == 0,
              "Timer5 prescaler must divide the CPU clock");
static_assert(TIMER_TICKS_PER_SECOND % 1000UL == 0,
              "Timer5 must convert milliseconds to whole ticks");

volatile uint8_t *valvePorts_[cellq::hw::CHANNEL_COUNT] = {};
uint8_t valveMasks_[cellq::hw::CHANNEL_COUNT] = {};
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
volatile uint8_t *ledPort_ = nullptr;
uint8_t ledMask_ = 0;
#endif

volatile bool active_ = false;
volatile bool paused_ = false;
volatile bool continuousOff_ = false;
volatile bool finishedPending_ = false;
volatile uint8_t channelMask_ = 0;
volatile uint8_t dutyPercent_ = 0;
volatile uint32_t totalCycles_ = 0;
volatile uint32_t completedCycles_ = 0;
volatile uint32_t periodTicks_ = 0;
volatile uint32_t onTicks_ = 0;
volatile uint32_t offTicks_ = 0;
volatile uint32_t remainingTicks_ = 0;
volatile Phase phase_ = Phase::Off;

void writeOutputsUnsafe(bool on) {
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    const bool channelOn =
        on && (channelMask_ & static_cast<uint8_t>(1U << channel)) != 0;
    if (channelOn) {
      *valvePorts_[channel] |= valveMasks_[channel];
    } else {
      *valvePorts_[channel] &= static_cast<uint8_t>(~valveMasks_[channel]);
    }
  }
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
  const bool channel1On =
      on && (channelMask_ & static_cast<uint8_t>(1U)) != 0;
  if (channel1On) {
    *ledPort_ |= ledMask_;
  } else {
    *ledPort_ &= static_cast<uint8_t>(~ledMask_);
  }
#endif
}

void stopClockUnsafe() {
  TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
  TCCR5B = _BV(WGM52);
  TIFR5 = _BV(OCF5A);
}

void loadNextSegmentUnsafe() {
  uint32_t segmentTicks = remainingTicks_;
  if (segmentTicks > TIMER_MAX_SEGMENT_TICKS) {
    segmentTicks = TIMER_MAX_SEGMENT_TICKS;
  }
  remainingTicks_ -= segmentTicks;
  TCNT5 = 0;
  OCR5A = static_cast<uint16_t>(segmentTicks - 1UL);
  TIFR5 = _BV(OCF5A);
  TIMSK5 |= _BV(OCIE5A);
  TCCR5B = _BV(WGM52) | TIMER_CLOCK_BITS;
}

void scheduleUnsafe(uint32_t ticks) {
  remainingTicks_ = ticks == 0 ? 1UL : ticks;
  loadNextSegmentUnsafe();
}

void finishUnsafe() {
  stopClockUnsafe();
  writeOutputsUnsafe(false);
  active_ = false;
  paused_ = false;
  finishedPending_ = true;
}

bool completeCycleUnsafe() {
  if (completedCycles_ < UINT32_MAX) {
    ++completedCycles_;
  }
  if (totalCycles_ != 0 && completedCycles_ >= totalCycles_) {
    finishUnsafe();
    return true;
  }
  return false;
}

void beginCycleUnsafe() {
  if (dutyPercent_ == 0) {
    phase_ = Phase::Off;
    writeOutputsUnsafe(false);
    scheduleUnsafe(periodTicks_);
    return;
  }
  phase_ = Phase::On;
  writeOutputsUnsafe(true);
  scheduleUnsafe(dutyPercent_ == 100 ? periodTicks_ : onTicks_);
}

void handleCompareUnsafe() {
  if (!active_ || paused_) {
    stopClockUnsafe();
    return;
  }
  if (remainingTicks_ != 0) {
    loadNextSegmentUnsafe();
    return;
  }
  if (phase_ == Phase::Delay) {
    beginCycleUnsafe();
    return;
  }
  if (phase_ == Phase::On && dutyPercent_ != 100) {
    phase_ = Phase::Off;
    writeOutputsUnsafe(false);
    scheduleUnsafe(offTicks_);
    return;
  }
  if (!completeCycleUnsafe()) {
    beginCycleUnsafe();
  }
}
}

ISR(TIMER5_COMPA_vect) {
  handleCompareUnsafe();
}

namespace cellq {
namespace waveform {
void begin() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR5A = 0;
    TCCR5B = _BV(WGM52);
    TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
    TIFR5 = _BV(OCF5A);
    active_ = false;
    paused_ = false;
    finishedPending_ = false;
    channelMask_ = 0;
    for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
      valvePorts_[channel] =
          portOutputRegister(digitalPinToPort(hw::VALVE_PINS[channel]));
      valveMasks_[channel] = digitalPinToBitMask(hw::VALVE_PINS[channel]);
    }
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
    ledPort_ = portOutputRegister(digitalPinToPort(hw::BUILTIN_LED_PIN));
    ledMask_ = digitalPinToBitMask(hw::BUILTIN_LED_PIN);
#endif
    writeOutputsUnsafe(false);
  }
}

bool start(const Config &waveformConfig) {
  if (waveformConfig.frequencyTenthsHz >
          config::SQUARE_FREQUENCY_MAX_TENTHS_HZ ||
      (waveformConfig.frequencyTenthsHz != 0 &&
       waveformConfig.frequencyTenthsHz <
           config::SQUARE_FREQUENCY_MIN_TENTHS_HZ) ||
      waveformConfig.dutyPercent > 100U) {
    return false;
  }

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    stopClockUnsafe();
    channelMask_ = waveformConfig.channelMask;
    dutyPercent_ = waveformConfig.dutyPercent;
    totalCycles_ = waveformConfig.totalCycles;
    completedCycles_ = 0;
    finishedPending_ = false;
    paused_ = false;
    active_ = true;
    continuousOff_ = waveformConfig.frequencyTenthsHz == 0;
    writeOutputsUnsafe(false);

    if (!continuousOff_) {
      periodTicks_ =
          (TIMER_TICKS_PER_SECOND * 10UL +
           waveformConfig.frequencyTenthsHz / 2U) /
          waveformConfig.frequencyTenthsHz;
      onTicks_ = static_cast<uint32_t>(
          (static_cast<uint64_t>(periodTicks_) *
               waveformConfig.dutyPercent +
           50ULL) /
          100ULL);
      if (waveformConfig.dutyPercent != 0 && onTicks_ == 0) {
        onTicks_ = 1;
      }
      if (waveformConfig.dutyPercent != 100 &&
          onTicks_ >= periodTicks_) {
        onTicks_ = periodTicks_ - 1UL;
      }
      offTicks_ = periodTicks_ - onTicks_;
      const uint32_t delayTicks =
          static_cast<uint32_t>(waveformConfig.delayMs) *
          (TIMER_TICKS_PER_SECOND / 1000UL);
      if (delayTicks == 0) {
        beginCycleUnsafe();
      } else {
        phase_ = Phase::Delay;
        scheduleUnsafe(delayTicks);
      }
    }
  }
  return true;
}

void stop() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    stopClockUnsafe();
    active_ = false;
    paused_ = false;
    continuousOff_ = false;
    finishedPending_ = false;
    writeOutputsUnsafe(false);
  }
}

void pause() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (active_ && !paused_) {
      if (!continuousOff_) {
        TCCR5B = _BV(WGM52);
        TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
        if ((TIFR5 & _BV(OCF5A)) == 0) {
          const uint32_t segmentTicks =
              static_cast<uint32_t>(OCR5A) + 1UL;
          const uint16_t elapsedTicks = TCNT5;
          if (elapsedTicks < segmentTicks) {
            remainingTicks_ += segmentTicks - elapsedTicks;
          }
        }
        if (remainingTicks_ == 0) {
          remainingTicks_ = 1;
        }
        TIFR5 = _BV(OCF5A);
      }
      paused_ = true;
    }
  }
}

void resume() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (active_ && paused_) {
      paused_ = false;
      if (!continuousOff_) {
        loadNextSegmentUnsafe();
      }
    }
  }
}

uint32_t completedCycles() {
  uint32_t cycles;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    cycles = completedCycles_;
  }
  return cycles;
}

bool takeFinished() {
  bool finished;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    finished = finishedPending_;
    finishedPending_ = false;
  }
  return finished;
}
}
}
