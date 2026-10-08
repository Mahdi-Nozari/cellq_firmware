#include "DataLogger.h"
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "HardwareConfig.h"
#include "MonitorMode.h"
#include "PresetStore.h"
#include "RtcClock.h"

namespace {
static_assert(cellq::hw::CHANNEL_COUNT == 6U,
              "Data log schema requires six channels");
static_assert(cellq::hw::REGULATOR_COUNT == 2U,
              "Data log schema requires two groups");
static_assert(cellq::config::DATA_LOG_INTERVAL_MS > 0U,
              "Data log interval must be nonzero");

File logFile_;
cellq::SquareWavePreset preset_ = {};
bool requested_ = false;
bool testActive_ = false;
bool loggingActive_ = false;
bool fileCreated_ = false;
bool testStartValid_ = false;
uint32_t testStartMs_ = 0;
uint32_t lastSampleMs_ = 0;
uint32_t lastLoggedAcquisitionMs_ = UINT32_MAX;
uint32_t lastFlushMs_ = 0;
uint32_t sampleCount_ = 0;
uint32_t droppedSamples_ = 0;
char testStart_[16] = {};
char logPath_[cellq::config::DATA_LOG_PATH_BUFFER_SIZE] = {};

bool writeLine(const char *line) {
  return logFile_ && logFile_.println(line) > 0U &&
         logFile_.getWriteError() == 0;
}

bool flushFile() {
  if (!logFile_) {
    return false;
  }
  logFile_.flush();
  return logFile_.getWriteError() == 0;
}

void closeFile() {
  if (logFile_) {
    logFile_.close();
  }
  loggingActive_ = false;
}

void clearTestState() {
  closeFile();
  testActive_ = false;
  fileCreated_ = false;
  testStartValid_ = false;
  logPath_[0] = '\0';
}

bool ensureDirectory(const char *path) {
  return SD.exists(path) || SD.mkdir(path);
}

bool createLogPath() {
  if (!cellq::preset::isSdAvailable() || !ensureDirectory("/DATA")) {
    return false;
  }
  char directory[16];
  if (testStartValid_) {
    strcpy(directory, "/DATA/00000000");
    memcpy(directory + 6, testStart_, 8U);
    if (!ensureDirectory(directory)) {
      return false;
    }
    for (uint8_t suffix = 0; suffix < 100U; ++suffix) {
      snprintf(logPath_, sizeof(logPath_), "%s/%.6s%02u.CSV", directory,
               testStart_ + 9, static_cast<unsigned int>(suffix));
      if (!SD.exists(logPath_)) {
        return true;
      }
    }
    return false;
  }

  strcpy(directory, "/DATA/NO_RTC");
  if (!ensureDirectory(directory)) {
    return false;
  }
  for (uint32_t sequence = 1UL; sequence <= 99999UL; ++sequence) {
    snprintf(logPath_, sizeof(logPath_), "%s/RUN%05lu.CSV", directory,
             static_cast<unsigned long>(sequence));
    if (!SD.exists(logPath_)) {
      return true;
    }
  }
  return false;
}

bool writeHeader(uint32_t now) {
  char line[cellq::config::DATA_LOG_LINE_BUFFER_SIZE];
  char created[16];
  cellq::clock::timestamp(created);
  bool ok = writeLine("CELLQ_DATA,1");
  snprintf(line, sizeof(line), "TEST_START,%s", testStart_);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "LOG_CREATED,%s", created);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "FIRST_LOG_ELAPSED_MS,%lu",
           static_cast<unsigned long>(now - testStartMs_));
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "PRESET_ID,%u", preset_.id);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "PRESET_REV,%lu",
           static_cast<unsigned long>(preset_.revision));
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "PRESET_NAME,%s", preset_.name);
  ok = writeLine(line) && ok;
#if CELLQ_VIRTUAL_PRESSURE_SENSORS
  ok = writeLine("SOURCE,VIRTUAL") && ok;
#else
  ok = writeLine("SOURCE,ANALOG") && ok;
#endif
  snprintf(line, sizeof(line), "SAMPLE_INTERVAL_MS,%u",
           cellq::config::DATA_LOG_INTERVAL_MS);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "PRESSURE_X10_KPA,%d,%d",
           preset_.pressureTenths[0], preset_.pressureTenths[1]);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "FREQ_X10_HZ,%u,%u",
           preset_.frequencyTenthsHz[0], preset_.frequencyTenthsHz[1]);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "DUTY_PCT,%u,%u",
           static_cast<unsigned int>(preset_.dutyPercent[0]),
           static_cast<unsigned int>(preset_.dutyPercent[1]));
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "DELAY_MS,%u,%u", preset_.delayMs[0],
           preset_.delayMs[1]);
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "TOTAL_CYCLES,%lu,%lu",
           static_cast<unsigned long>(preset_.totalCycles[0]),
           static_cast<unsigned long>(preset_.totalCycles[1]));
  ok = writeLine(line) && ok;
  snprintf(line, sizeof(line), "CHANNEL_ENABLED,%u,%u,%u,%u,%u,%u",
           static_cast<unsigned int>(preset_.channelEnabled[0]),
           static_cast<unsigned int>(preset_.channelEnabled[1]),
           static_cast<unsigned int>(preset_.channelEnabled[2]),
           static_cast<unsigned int>(preset_.channelEnabled[3]),
           static_cast<unsigned int>(preset_.channelEnabled[4]),
           static_cast<unsigned int>(preset_.channelEnabled[5]));
  ok = writeLine(line) && ok;
  ok = writeLine(
           "COLUMNS,TYPE,ELAPSED_MS,STATE,G1_CYCLE,G2_CYCLE,V1,V2,V3,"
           "V4,V5,V6,P1_X10_KPA,P2_X10_KPA,P3_X10_KPA,P4_X10_KPA,"
           "P5_X10_KPA,P6_X10_KPA") &&
       ok;
  return writeLine("DATA") && ok;
}

bool writeEvent(uint32_t now, const char *event) {
  char line[48];
  snprintf(line, sizeof(line), "EVENT,%lu,%s",
           static_cast<unsigned long>(now - testStartMs_), event);
  return writeLine(line);
}

const char *stateText(cellq::logger::State state) {
  switch (state) {
    case cellq::logger::State::PausePending:
      return "PAUSE_PENDING";
    case cellq::logger::State::Paused:
      return "PAUSED";
    default:
      return "RUNNING";
  }
}

const char *reasonText(cellq::logger::EndReason reason) {
  switch (reason) {
    case cellq::logger::EndReason::Stopped:
      return "STOPPED";
    case cellq::logger::EndReason::Error:
      return "ERROR";
    default:
      return "DONE";
  }
}

bool openLogging(uint32_t now) {
  const bool creatingFile = !fileCreated_;
  if (!fileCreated_) {
    if (!createLogPath()) {
      DBG_LOG("ERR,PATH");
      return false;
    }
    logFile_ = SD.open(logPath_, FILE_WRITE);
    if (!logFile_ || !writeHeader(now)) {
      closeFile();
      SD.remove(logPath_);
      logPath_[0] = '\0';
      DBG_LOG("ERR,HEADER");
      return false;
    }
    fileCreated_ = true;
    DBG_LOG("OPEN");
  } else {
    logFile_ = SD.open(logPath_, FILE_WRITE);
    if (!logFile_) {
      DBG_LOG("ERR,REOPEN");
      return false;
    }
  }
  if (!writeEvent(now, "LOGGING_ON") || !flushFile()) {
    closeFile();
    if (creatingFile) {
      SD.remove(logPath_);
      fileCreated_ = false;
      logPath_[0] = '\0';
    }
    DBG_LOG("ERR,EVENT");
    return false;
  }
  loggingActive_ = true;
  lastSampleMs_ = now - cellq::config::DATA_LOG_INTERVAL_MS;
  lastLoggedAcquisitionMs_ = UINT32_MAX;
  lastFlushMs_ = now;
  DBG_LOG("ON");
  return true;
}

bool writeSample(const cellq::monitor::PressureSample &sample,
                 cellq::logger::State state,
                 const uint32_t completedCycles[2]) {
  char line[cellq::config::DATA_LOG_LINE_BUFFER_SIZE];
  snprintf(
      line, sizeof(line),
      "SAMPLE,%lu,%s,%lu,%lu,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d",
      static_cast<unsigned long>(sample.timeMs - testStartMs_),
      stateText(state),
      static_cast<unsigned long>(completedCycles[0]),
      static_cast<unsigned long>(completedCycles[1]),
      static_cast<unsigned int>(sample.valveOn[0]),
      static_cast<unsigned int>(sample.valveOn[1]),
      static_cast<unsigned int>(sample.valveOn[2]),
      static_cast<unsigned int>(sample.valveOn[3]),
      static_cast<unsigned int>(sample.valveOn[4]),
      static_cast<unsigned int>(sample.valveOn[5]),
      sample.pressureTenths[0], sample.pressureTenths[1],
      sample.pressureTenths[2], sample.pressureTenths[3],
      sample.pressureTenths[4], sample.pressureTenths[5]);
  if (!writeLine(line)) {
    DBG_LOG("ERR,WRITE");
    return false;
  }
  ++sampleCount_;
  return true;
}
}

namespace cellq {
namespace logger {
void begin() {
  requested_ = false;
  testActive_ = false;
  loggingActive_ = false;
  fileCreated_ = false;
  logPath_[0] = '\0';
}

bool enabled() {
  return requested_;
}

bool setEnabled(bool enabled, uint32_t now) {
  requested_ = enabled;
  if (!testActive_) {
    if (enabled) {
      DBG_LOG("ARM,1");
    } else {
      DBG_LOG("ARM,0");
    }
    return true;
  }
  if (enabled) {
    if (loggingActive_) {
      return true;
    }
    if (!openLogging(now)) {
      return false;
    }
    return true;
  }
  if (!loggingActive_) {
    return true;
  }
  const bool ok = writeEvent(now, "LOGGING_OFF") && flushFile();
  closeFile();
  if (!ok) {
    DBG_LOG("ERR,CLOSE");
    return false;
  }
  DBG_LOG("OFF");
  return true;
}

bool startTest(const SquareWavePreset &preset, uint32_t now) {
  if (testActive_) {
    return false;
  }
  preset_ = preset;
  testStartMs_ = now;
  testStartValid_ = clock::timestamp(testStart_);
  sampleCount_ = 0;
  droppedSamples_ = 0;
  lastLoggedAcquisitionMs_ = UINT32_MAX;
  fileCreated_ = false;
  loggingActive_ = false;
  logPath_[0] = '\0';
  testActive_ = true;
  if (!requested_) {
    return true;
  }
  return openLogging(now);
}

bool update(uint32_t now, State state,
            const uint32_t completedCycles[2]) {
  if (!testActive_ || !loggingActive_) {
    return true;
  }
  const uint32_t elapsed = now - lastSampleMs_;
  if (elapsed < config::DATA_LOG_INTERVAL_MS) {
    return true;
  }
  const uint32_t periods = elapsed / config::DATA_LOG_INTERVAL_MS;
  if (periods > 1UL) {
    droppedSamples_ += periods - 1UL;
  }
  lastSampleMs_ += periods * config::DATA_LOG_INTERVAL_MS;
  monitor::PressureSample sample;
  if (!monitor::latestSample(sample) ||
      static_cast<uint32_t>(now - sample.timeMs) >=
          config::DATA_LOG_INTERVAL_MS ||
      sample.timeMs == lastLoggedAcquisitionMs_) {
    ++droppedSamples_;
  } else {
    const uint32_t writeStartedMs = millis();
    if (!writeSample(sample, state, completedCycles)) {
      return false;
    }
    lastLoggedAcquisitionMs_ = sample.timeMs;
    const uint32_t writeDurationMs = millis() - writeStartedMs;
    if (writeDurationMs >= config::DATA_LOG_INTERVAL_MS) {
      droppedSamples_ += writeDurationMs / config::DATA_LOG_INTERVAL_MS;
      lastSampleMs_ = millis();
    }
  }
  if (static_cast<uint32_t>(now - lastFlushMs_) >=
      config::DATA_LOG_FLUSH_INTERVAL_MS) {
    lastFlushMs_ = now;
    const uint32_t flushStartedMs = millis();
    if (!flushFile()) {
      DBG_LOG("ERR,FLUSH");
      return false;
    }
    const uint32_t flushDurationMs = millis() - flushStartedMs;
    if (flushDurationMs >= config::DATA_LOG_INTERVAL_MS) {
      droppedSamples_ += flushDurationMs / config::DATA_LOG_INTERVAL_MS;
      lastSampleMs_ = millis();
    }
  }
  return true;
}

bool finish(uint32_t now, EndReason reason,
            const uint32_t completedCycles[2]) {
  (void)completedCycles;
  if (!testActive_) {
    return true;
  }
  if (!fileCreated_) {
    clearTestState();
    return true;
  }
  if (!logFile_) {
    logFile_ = SD.open(logPath_, FILE_WRITE);
    if (!logFile_) {
      DBG_LOG("ERR,FINALOPEN");
      return false;
    }
  }
  char line[72];
  snprintf(line, sizeof(line), "END,%lu,%s,%lu,%lu",
           static_cast<unsigned long>(now - testStartMs_),
           reasonText(reason), static_cast<unsigned long>(sampleCount_),
           static_cast<unsigned long>(droppedSamples_));
  const bool ok = writeLine(line) && flushFile();
  closeFile();
  if (!ok) {
    DBG_LOG("ERR,FINAL");
    return false;
  }
  DBG_LOG("END");
  clearTestState();
  return true;
}

void abort() {
  closeFile();
  requested_ = false;
  testActive_ = false;
  fileCreated_ = false;
  logPath_[0] = '\0';
  DBG_LOG("ABORT");
}

void disableAfterFailure() {
  closeFile();
  requested_ = false;
  fileCreated_ = false;
  logPath_[0] = '\0';
  DBG_LOG("DISABLED");
}
}
}
