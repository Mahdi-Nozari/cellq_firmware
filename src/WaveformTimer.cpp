#include "WaveformTimer.h"
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "BuildConfig.h"
#include "HardwareConfig.h"

namespace {
enum class Phase : uint8_t { Delay, On, Off };

constexpr uint8_t GROUP_COUNT = cellq::hw::REGULATOR_COUNT;
constexpr uint32_t TIMER_PRESCALER = 64UL;
constexpr uint32_t TIMER_TICKS_PER_SECOND = F_CPU / TIMER_PRESCALER;
constexpr uint32_t TIMER_MAX_SEGMENT_TICKS = 65536UL;
constexpr uint8_t TIMER_CLOCK_BITS = _BV(CS51) | _BV(CS50);
static_assert(GROUP_COUNT == 2U, "Square-wave scheduler requires two groups");
static_assert(F_CPU % TIMER_PRESCALER == 0,
              "Timer5 prescaler must divide the CPU clock");
static_assert(TIMER_TICKS_PER_SECOND % 1000UL == 0,
              "Timer5 must convert milliseconds to whole ticks");

uint32_t periodTicksForFrequency(uint16_t frequencyTenthsHz) {
  return (TIMER_TICKS_PER_SECOND * 10UL +
          frequencyTenthsHz / 2U) / frequencyTenthsHz;
}

volatile uint8_t *valvePorts_[cellq::hw::CHANNEL_COUNT] = {};
uint8_t valveMasks_[cellq::hw::CHANNEL_COUNT] = {};
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
volatile uint8_t *ledPort_ = nullptr;
uint8_t ledMask_ = 0;
#endif

volatile bool active_ = false;
volatile bool paused_ = false;
volatile bool boundaryPauseRequested_ = false;
volatile bool boundaryPaused_ = false;
volatile bool boundaryPausedPending_ = false;
volatile bool finishedPending_ = false;
volatile uint32_t scheduledTicks_ = 0;
volatile uint8_t channelMask_[GROUP_COUNT] = {};
volatile uint8_t dutyPercent_[GROUP_COUNT] = {};
volatile uint32_t totalCycles_[GROUP_COUNT] = {};
volatile uint32_t completedCycles_[GROUP_COUNT] = {};
volatile uint32_t periodTicks_[GROUP_COUNT] = {};
volatile uint32_t delayTicks_[GROUP_COUNT] = {};
volatile uint32_t onTicks_[GROUP_COUNT] = {};
volatile uint32_t offTicks_[GROUP_COUNT] = {};
volatile uint32_t ticksToEvent_[GROUP_COUNT] = {};
volatile bool continuousOff_[GROUP_COUNT] = {};
volatile bool groupFinished_[GROUP_COUNT] = {};
volatile bool groupFinishedPending_[GROUP_COUNT] = {};
volatile bool waitingAtBoundary_[GROUP_COUNT] = {};
volatile Phase phase_[GROUP_COUNT] = {};

void writeGroupOutputsUnsafe(uint8_t group, bool on) {
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    if (cellq::hw::CHANNEL_TO_REGULATOR[channel] != group) {
      continue;
    }
    const bool channelOn =
        on && (channelMask_[group] & static_cast<uint8_t>(1U << channel)) != 0;
    if (channelOn) {
      *valvePorts_[channel] |= valveMasks_[channel];
    } else {
      *valvePorts_[channel] &= static_cast<uint8_t>(~valveMasks_[channel]);
    }
  }
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
  if (group == 0U) {
    const bool channel1On =
        on && (channelMask_[0] & static_cast<uint8_t>(1U)) != 0;
    if (channel1On) {
      *ledPort_ |= ledMask_;
    } else {
      *ledPort_ &= static_cast<uint8_t>(~ledMask_);
    }
  }
#endif
}

void writeAllOutputsOffUnsafe() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    writeGroupOutputsUnsafe(group, false);
  }
}

void stopClockUnsafe() {
  TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
  TCCR5B = _BV(WGM52);
  TIFR5 = _BV(OCF5A);
  scheduledTicks_ = 0;
}

bool groupHasTimerUnsafe(uint8_t group) {
  return !groupFinished_[group] && !continuousOff_[group] &&
         !waitingAtBoundary_[group];
}

bool allGroupsFinishedUnsafe() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (!groupFinished_[group]) {
      return false;
    }
  }
  return true;
}

bool allGroupsAtBoundaryUnsafe() {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (!groupFinished_[group] && !continuousOff_[group] &&
        !waitingAtBoundary_[group]) {
      return false;
    }
  }
  return true;
}

void finishProcedureUnsafe() {
  stopClockUnsafe();
  writeAllOutputsOffUnsafe();
  active_ = false;
  paused_ = false;
  boundaryPauseRequested_ = false;
  boundaryPaused_ = false;
  finishedPending_ = true;
}

void finishGroupUnsafe(uint8_t group) {
  writeGroupOutputsUnsafe(group, false);
  groupFinished_[group] = true;
  groupFinishedPending_[group] = true;
  waitingAtBoundary_[group] = false;
  ticksToEvent_[group] = 0;
}

void beginCycleUnsafe(uint8_t group) {
  waitingAtBoundary_[group] = false;
  if (dutyPercent_[group] == 0U) {
    phase_[group] = Phase::Off;
    writeGroupOutputsUnsafe(group, false);
    ticksToEvent_[group] = periodTicks_[group];
    return;
  }
  phase_[group] = Phase::On;
  writeGroupOutputsUnsafe(group, true);
  ticksToEvent_[group] =
      dutyPercent_[group] == 100U ? periodTicks_[group] : onTicks_[group];
}

void beginDelayUnsafe(uint8_t group) {
  waitingAtBoundary_[group] = false;
  writeGroupOutputsUnsafe(group, false);
  if (delayTicks_[group] == 0U) {
    beginCycleUnsafe(group);
    return;
  }
  phase_[group] = Phase::Delay;
  ticksToEvent_[group] = delayTicks_[group];
}

void completeCycleUnsafe(uint8_t group) {
  if (completedCycles_[group] < UINT32_MAX) {
    ++completedCycles_[group];
  }
  if (totalCycles_[group] != 0U &&
      completedCycles_[group] >= totalCycles_[group]) {
    finishGroupUnsafe(group);
    return;
  }
  if (boundaryPauseRequested_) {
    writeGroupOutputsUnsafe(group, false);
    waitingAtBoundary_[group] = true;
    ticksToEvent_[group] = 0;
    return;
  }
  beginDelayUnsafe(group);
}

void processGroupEventUnsafe(uint8_t group) {
  if (phase_[group] == Phase::Delay) {
    if (boundaryPauseRequested_) {
      waitingAtBoundary_[group] = true;
      ticksToEvent_[group] = 0;
    } else {
      beginCycleUnsafe(group);
    }
    return;
  }
  if (phase_[group] == Phase::On && dutyPercent_[group] != 100U) {
    phase_[group] = Phase::Off;
    writeGroupOutputsUnsafe(group, false);
    ticksToEvent_[group] = offTicks_[group];
    return;
  }
  completeCycleUnsafe(group);
}

void enterBoundaryPauseUnsafe() {
  stopClockUnsafe();
  writeAllOutputsOffUnsafe();
  paused_ = true;
  boundaryPaused_ = true;
  boundaryPauseRequested_ = false;
  boundaryPausedPending_ = true;
}

void scheduleNextUnsafe() {
  uint32_t nextTicks = UINT32_MAX;
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (groupHasTimerUnsafe(group) && ticksToEvent_[group] < nextTicks) {
      nextTicks = ticksToEvent_[group];
    }
  }
  if (nextTicks == UINT32_MAX) {
    stopClockUnsafe();
    return;
  }
  if (nextTicks == 0U) {
    nextTicks = 1U;
  }
  if (nextTicks > TIMER_MAX_SEGMENT_TICKS) {
    nextTicks = TIMER_MAX_SEGMENT_TICKS;
  }
  scheduledTicks_ = nextTicks;
  TCNT5 = 0;
  OCR5A = static_cast<uint16_t>(nextTicks - 1UL);
  TIFR5 = _BV(OCF5A);
  TIMSK5 |= _BV(OCIE5A);
  TCCR5B = _BV(WGM52) | TIMER_CLOCK_BITS;
}

void advanceGroupsUnsafe(uint32_t elapsedTicks) {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (!groupHasTimerUnsafe(group)) {
      continue;
    }
    if (ticksToEvent_[group] > elapsedTicks) {
      ticksToEvent_[group] -= elapsedTicks;
    } else {
      ticksToEvent_[group] = 0;
    }
  }
}

void captureTimerProgressUnsafe() {
  if (scheduledTicks_ == 0U) {
    return;
  }
  TCCR5B = _BV(WGM52);
  TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
  uint32_t elapsedTicks = TCNT5;
  if ((TIFR5 & _BV(OCF5A)) != 0U || elapsedTicks > scheduledTicks_) {
    elapsedTicks = scheduledTicks_;
  }
  advanceGroupsUnsafe(elapsedTicks);
  TIFR5 = _BV(OCF5A);
  scheduledTicks_ = 0;
}

void handleCompareUnsafe() {
  if (!active_ || paused_) {
    stopClockUnsafe();
    return;
  }
  const uint32_t elapsedTicks = scheduledTicks_;
  scheduledTicks_ = 0;
  advanceGroupsUnsafe(elapsedTicks);
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    if (groupHasTimerUnsafe(group) && ticksToEvent_[group] == 0U) {
      processGroupEventUnsafe(group);
    }
  }
  if (allGroupsFinishedUnsafe()) {
    finishProcedureUnsafe();
    return;
  }
  if (boundaryPauseRequested_ && allGroupsAtBoundaryUnsafe()) {
    enterBoundaryPauseUnsafe();
    return;
  }
  scheduleNextUnsafe();
}
}

ISR(TIMER5_COMPA_vect) {
  handleCompareUnsafe();
}

namespace cellq {
namespace waveform {
uint32_t cyclesForDurationMinutes(uint32_t minutes,
                                  uint16_t frequencyTenthsHz,
                                  uint16_t delayMs) {
  if (minutes == 0U || frequencyTenthsHz == 0U) {
    return 0U;
  }
  const uint32_t cycleTicks =
      periodTicksForFrequency(frequencyTenthsHz) +
      static_cast<uint32_t>(delayMs) *
          (TIMER_TICKS_PER_SECOND / 1000UL);
  const uint64_t requestedTicks =
      static_cast<uint64_t>(minutes) * 60ULL *
      TIMER_TICKS_PER_SECOND;
  const uint64_t cycles = requestedTicks / cycleTicks;
  return cycles > UINT32_MAX ? UINT32_MAX
                             : static_cast<uint32_t>(cycles);
}

void begin() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR5A = 0;
    TCCR5B = _BV(WGM52);
    TIMSK5 &= static_cast<uint8_t>(~_BV(OCIE5A));
    TIFR5 = _BV(OCF5A);
    active_ = false;
    paused_ = false;
    boundaryPauseRequested_ = false;
    boundaryPaused_ = false;
    boundaryPausedPending_ = false;
    finishedPending_ = false;
    for (uint8_t channel = 0; channel < hw::CHANNEL_COUNT; ++channel) {
      valvePorts_[channel] =
          portOutputRegister(digitalPinToPort(hw::VALVE_PINS[channel]));
      valveMasks_[channel] = digitalPinToBitMask(hw::VALVE_PINS[channel]);
    }
#if CELLQ_MIRROR_CHANNEL_1_TO_LED
    ledPort_ = portOutputRegister(digitalPinToPort(hw::BUILTIN_LED_PIN));
    ledMask_ = digitalPinToBitMask(hw::BUILTIN_LED_PIN);
#endif
    for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
      channelMask_[group] = 0;
      groupFinished_[group] = true;
      groupFinishedPending_[group] = false;
      waitingAtBoundary_[group] = false;
    }
    writeAllOutputsOffUnsafe();
  }
}

bool start(const Config &waveformConfig) {
  for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
    const GroupConfig &config = waveformConfig.group[group];
    if (config.frequencyTenthsHz >
            cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ ||
        (config.frequencyTenthsHz != 0U &&
         config.frequencyTenthsHz <
             cellq::config::SQUARE_FREQUENCY_MIN_TENTHS_HZ) ||
        config.dutyPercent > 100U ||
        config.delayMs > cellq::config::SQUARE_DELAY_MAX_MS) {
      return false;
    }
  }

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    stopClockUnsafe();
    active_ = true;
    paused_ = false;
    boundaryPauseRequested_ = false;
    boundaryPaused_ = false;
    boundaryPausedPending_ = false;
    finishedPending_ = false;
    for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
      const GroupConfig &config = waveformConfig.group[group];
      channelMask_[group] = config.channelMask;
      dutyPercent_[group] = config.dutyPercent;
      totalCycles_[group] = config.totalCycles;
      completedCycles_[group] = 0;
      continuousOff_[group] = config.frequencyTenthsHz == 0U;
      groupFinished_[group] = false;
      groupFinishedPending_[group] = false;
      waitingAtBoundary_[group] = false;
      writeGroupOutputsUnsafe(group, false);

      if (continuousOff_[group]) {
        phase_[group] = Phase::Off;
        ticksToEvent_[group] = 0;
        continue;
      }
      periodTicks_[group] =
          periodTicksForFrequency(config.frequencyTenthsHz);
      onTicks_[group] = static_cast<uint32_t>(
          (static_cast<uint64_t>(periodTicks_[group]) *
               config.dutyPercent +
           50ULL) /
          100ULL);
      if (config.dutyPercent != 0U && onTicks_[group] == 0U) {
        onTicks_[group] = 1U;
      }
      if (config.dutyPercent != 100U &&
          onTicks_[group] >= periodTicks_[group]) {
        onTicks_[group] = periodTicks_[group] - 1UL;
      }
      offTicks_[group] = periodTicks_[group] - onTicks_[group];
      delayTicks_[group] =
          static_cast<uint32_t>(config.delayMs) *
          (TIMER_TICKS_PER_SECOND / 1000UL);
      beginDelayUnsafe(group);
    }
    scheduleNextUnsafe();
  }
  return true;
}

void stop() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    stopClockUnsafe();
    active_ = false;
    paused_ = false;
    boundaryPauseRequested_ = false;
    boundaryPaused_ = false;
    boundaryPausedPending_ = false;
    finishedPending_ = false;
    for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
      groupFinishedPending_[group] = false;
      waitingAtBoundary_[group] = false;
    }
    writeAllOutputsOffUnsafe();
  }
}

void pause() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!active_ || paused_) {
      return;
    }
    captureTimerProgressUnsafe();
    paused_ = true;
    boundaryPaused_ = false;
    boundaryPauseRequested_ = false;
  }
}

void pauseAtCycleBoundary() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!active_ || paused_ || boundaryPauseRequested_) {
      return;
    }
    captureTimerProgressUnsafe();
    boundaryPauseRequested_ = true;
    for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
      if (continuousOff_[group] ||
          (!groupFinished_[group] && phase_[group] == Phase::Delay)) {
        writeGroupOutputsUnsafe(group, false);
        waitingAtBoundary_[group] = true;
        ticksToEvent_[group] = 0;
      }
    }
    for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
      if (groupHasTimerUnsafe(group) && ticksToEvent_[group] == 0U) {
        processGroupEventUnsafe(group);
      }
    }
    if (allGroupsAtBoundaryUnsafe()) {
      enterBoundaryPauseUnsafe();
    } else {
      scheduleNextUnsafe();
    }
  }
}

void resume() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!active_ || !paused_) {
      return;
    }
    paused_ = false;
    if (boundaryPaused_) {
      boundaryPaused_ = false;
      for (uint8_t group = 0; group < GROUP_COUNT; ++group) {
        if (!groupFinished_[group] && !continuousOff_[group]) {
          beginDelayUnsafe(group);
        }
      }
    }
    scheduleNextUnsafe();
  }
}

uint32_t completedCycles(uint8_t group) {
  if (group >= GROUP_COUNT) {
    return 0;
  }
  uint32_t cycles;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    cycles = completedCycles_[group];
  }
  return cycles;
}

uint64_t remainingMilliseconds(uint8_t group) {
  if (group >= GROUP_COUNT) {
    return 0;
  }
  uint32_t totalCycles;
  uint32_t completedCycles;
  uint32_t periodTicks;
  uint32_t delayTicks;
  uint32_t offTicks;
  uint32_t ticksToEvent;
  uint8_t dutyPercent;
  bool continuousOff;
  bool finished;
  bool waiting;
  Phase phase;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    totalCycles = totalCycles_[group];
    completedCycles = completedCycles_[group];
    periodTicks = periodTicks_[group];
    delayTicks = delayTicks_[group];
    offTicks = offTicks_[group];
    ticksToEvent = ticksToEvent_[group];
    dutyPercent = dutyPercent_[group];
    continuousOff = continuousOff_[group];
    finished = groupFinished_[group];
    waiting = waitingAtBoundary_[group];
    phase = phase_[group];
  }
  if (finished) {
    return 0;
  }
  if (totalCycles == 0U || continuousOff) {
    return UINT64_MAX;
  }
  const uint32_t cyclesRemaining = totalCycles > completedCycles
                                       ? totalCycles - completedCycles
                                       : 0U;
  uint64_t ticks = 0;
  if (waiting) {
    ticks = static_cast<uint64_t>(cyclesRemaining) *
            (periodTicks + delayTicks);
  } else if (phase == Phase::Delay) {
    ticks = ticksToEvent +
            static_cast<uint64_t>(cyclesRemaining) * periodTicks;
    if (cyclesRemaining > 0U) {
      ticks += static_cast<uint64_t>(cyclesRemaining - 1U) * delayTicks;
    }
  } else {
    ticks = ticksToEvent;
    if (phase == Phase::On && dutyPercent != 100U) {
      ticks += offTicks;
    }
    if (cyclesRemaining > 0U) {
      ticks += static_cast<uint64_t>(cyclesRemaining - 1U) *
               (periodTicks + delayTicks);
    }
  }
  return ticks / (TIMER_TICKS_PER_SECOND / 1000UL);
}

bool takeGroupFinished(uint8_t group) {
  if (group >= GROUP_COUNT) {
    return false;
  }
  bool finished;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    finished = groupFinishedPending_[group];
    groupFinishedPending_[group] = false;
  }
  return finished;
}

bool takeBoundaryPaused() {
  bool paused;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    paused = boundaryPausedPending_;
    boundaryPausedPending_ = false;
  }
  return paused;
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
