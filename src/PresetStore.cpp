#include "PresetStore.h"
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "HardwareConfig.h"

namespace {
enum class ParseError : uint8_t {
  None,
  SdUnavailable,
  FileOpen,
  FileTooLong,
  InvalidField,
  BadSignature,
  UnsupportedVersion,
  OutOfRange,
  WrongChannelCount,
  CrcMismatch
};

enum class LineResult : uint8_t { Line, End, TooLong, InvalidCharacter };

constexpr char FIELD_HEADER[] =
    "FIELDS,CH,ENABLED,FREQ_X10_HZ,DUTY_PCT,DELAY_MS,TOTAL_CYCLES";
bool sdAvailable_ = false;

void buildFactoryPath(uint16_t id, char *path) {
  strcpy(path, "/PRESETS/FCTRY0.CSV");
  path[14] = static_cast<char>('0' + id);
}

void buildUserPath(uint16_t id, char slot, char *path) {
  strcpy(path, "/PRESETS/SW000A.CSV");
  path[11] = static_cast<char>('0' + id / 100U);
  path[12] = static_cast<char>('0' + id / 10U % 10U);
  path[13] = static_cast<char>('0' + id % 10U);
  path[14] = slot;
}

uint32_t updateCrc(uint32_t crc, const char *text) {
  while (*text != '\0') {
    crc ^= static_cast<uint8_t>(*text++);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320UL
                            : crc >> 1U;
    }
  }
  crc ^= static_cast<uint8_t>('\n');
  for (uint8_t bit = 0; bit < 8U; ++bit) {
    crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320UL
                          : crc >> 1U;
  }
  return crc;
}

LineResult readLine(File &file, char *line, size_t capacity) {
  size_t length = 0;
  while (file.available()) {
    const int next = file.read();
    if (next == '\n') {
      line[length] = '\0';
      return LineResult::Line;
    }
    if (next == '\r') {
      if (file.peek() == '\n') {
        file.read();
      }
      line[length] = '\0';
      return LineResult::Line;
    }
    if (next < 32 || next > 126) {
      return LineResult::InvalidCharacter;
    }
    if (length + 1U >= capacity) {
      while (file.available()) {
        const int discard = file.read();
        if (discard == '\n') {
          break;
        }
      }
      return LineResult::TooLong;
    }
    line[length++] = static_cast<char>(next);
  }
  if (length == 0) {
    return LineResult::End;
  }
  line[length] = '\0';
  return LineResult::Line;
}

bool splitExact(char *line, char **tokens, uint8_t expected) {
  uint8_t count = 1;
  tokens[0] = line;
  if (*line == '\0') {
    return false;
  }
  for (char *cursor = line; *cursor != '\0'; ++cursor) {
    if (*cursor != ',') {
      continue;
    }
    *cursor = '\0';
    if (*(cursor + 1) == '\0' || count >= expected) {
      return false;
    }
    tokens[count++] = cursor + 1;
  }
  if (count != expected) {
    return false;
  }
  for (uint8_t token = 0; token < count; ++token) {
    if (*tokens[token] == '\0') {
      return false;
    }
  }
  return true;
}

bool parseUnsigned(const char *text, uint32_t maximum, uint32_t &value) {
  if (*text == '\0') {
    return false;
  }
  uint32_t parsed = 0;
  while (*text != '\0') {
    if (*text < '0' || *text > '9') {
      return false;
    }
    const uint8_t digit = static_cast<uint8_t>(*text++ - '0');
    if (parsed > maximum / 10UL ||
        (parsed == maximum / 10UL && digit > maximum % 10UL)) {
      return false;
    }
    parsed = parsed * 10UL + digit;
  }
  value = parsed;
  return true;
}

bool parseSigned(const char *text, int32_t minimum, int32_t maximum,
                 int32_t &value) {
  bool negative = false;
  if (*text == '-') {
    negative = true;
    ++text;
  }
  uint32_t magnitude;
  if (!parseUnsigned(text, static_cast<uint32_t>(-minimum), magnitude)) {
    return false;
  }
  const int32_t parsed =
      negative ? -static_cast<int32_t>(magnitude)
               : static_cast<int32_t>(magnitude);
  if (parsed < minimum || parsed > maximum) {
    return false;
  }
  value = parsed;
  return true;
}

bool validName(const char *name) {
  const size_t length = strlen(name);
  if (length == 0 || length > 20U || name[0] == ' ' ||
      name[length - 1U] == ' ') {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const char value = name[index];
    const bool alphaNumeric =
        (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
    if (!alphaNumeric && value != ' ' && value != '-' && value != '_') {
      return false;
    }
  }
  return true;
}

bool validTimestamp(const char *timestamp) {
  if (timestamp == nullptr || strlen(timestamp) != 15U ||
      timestamp[8] != 'T') {
    return false;
  }
  for (uint8_t index = 0; index < 15U; ++index) {
    if (index == 8U) {
      continue;
    }
    if (timestamp[index] < '0' || timestamp[index] > '9') {
      return false;
    }
  }
  return true;
}

bool validPresetData(const cellq::SquareWavePreset &preset) {
  if (preset.id < 3U ||
      preset.id > cellq::config::SQUARE_PRESET_SLOT_COUNT ||
      !validName(preset.name)) {
    return false;
  }
  for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT; ++group) {
    const uint16_t frequency = preset.frequencyTenthsHz[group];
    if (preset.pressureTenths[group] <
            cellq::config::SQUARE_PRESSURE_MIN_TENTHS ||
        preset.pressureTenths[group] >
            cellq::config::SQUARE_PRESSURE_MAX_TENTHS ||
        frequency > cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ ||
        (frequency != 0U &&
         frequency < cellq::config::SQUARE_FREQUENCY_MIN_TENTHS_HZ) ||
        preset.dutyPercent[group] > 100U ||
        preset.delayMs[group] > cellq::config::SQUARE_DELAY_MAX_MS) {
      return false;
    }
  }
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    if (preset.channelEnabled[channel] > 1U) {
      return false;
    }
  }
  return true;
}

bool parseHex32(const char *text, uint32_t &value) {
  if (strlen(text) != 8U) {
    return false;
  }
  uint32_t parsed = 0;
  for (uint8_t index = 0; index < 8U; ++index) {
    const char character = text[index];
    uint8_t digit;
    if (character >= '0' && character <= '9') {
      digit = static_cast<uint8_t>(character - '0');
    } else if (character >= 'A' && character <= 'F') {
      digit = static_cast<uint8_t>(character - 'A' + 10);
    } else {
      return false;
    }
    parsed = (parsed << 4U) | digit;
  }
  value = parsed;
  return true;
}

ParseError parsePreset(File &file, uint16_t expectedId,
                       cellq::SquareWavePreset &preset) {
  char line[cellq::config::PRESET_LINE_BUFFER_SIZE];
  char *tokens[7];
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t value;
  int32_t signedValue;
  uint16_t groupFrequency[cellq::hw::REGULATOR_COUNT] = {};
  uint8_t groupDuty[cellq::hw::REGULATOR_COUNT] = {};
  uint16_t groupDelay[cellq::hw::REGULATOR_COUNT] = {};
  uint32_t groupCycles[cellq::hw::REGULATOR_COUNT] = {};
  bool groupParametersSet[cellq::hw::REGULATOR_COUNT] = {};

  for (uint8_t record = 0; record < 17U; ++record) {
    const LineResult lineResult = readLine(file, line, sizeof(line));
    if (lineResult == LineResult::TooLong) {
      return ParseError::FileTooLong;
    }
    if (lineResult != LineResult::Line || line[0] == '\0') {
      return ParseError::InvalidField;
    }
    if (record < 16U) {
      crc = updateCrc(crc, line);
    }

    if (record == 0U) {
      if (!splitExact(line, tokens, 2U) ||
          strcmp(tokens[0], "CELLQ_SW") != 0) {
        return ParseError::BadSignature;
      }
      if (!parseUnsigned(tokens[1], UINT8_MAX, value) || value != 1U) {
        return ParseError::UnsupportedVersion;
      }
      continue;
    }
    if (record == 1U) {
      if (!splitExact(line, tokens, 2U) || strcmp(tokens[0], "ID") != 0 ||
          !parseUnsigned(tokens[1], UINT16_MAX, value) ||
          value != expectedId) {
        return ParseError::InvalidField;
      }
      preset.id = static_cast<uint16_t>(value);
      continue;
    }
    if (record == 2U) {
      if (!splitExact(line, tokens, 2U) || strcmp(tokens[0], "REV") != 0 ||
          !parseUnsigned(tokens[1], UINT32_MAX, value)) {
        return ParseError::InvalidField;
      }
      preset.revision = value;
      continue;
    }
    if (record == 3U) {
      if (!splitExact(line, tokens, 2U) || strcmp(tokens[0], "NAME") != 0 ||
          !validName(tokens[1])) {
        return ParseError::InvalidField;
      }
      strcpy(preset.name, tokens[1]);
      continue;
    }
    if (record == 4U || record == 5U) {
      const char *expected = record == 4U ? "CREATED" : "MODIFIED";
      if (!splitExact(line, tokens, 2U) ||
          strcmp(tokens[0], expected) != 0 || !validTimestamp(tokens[1])) {
        return ParseError::InvalidField;
      }
      strcpy(record == 4U ? preset.created : preset.modified, tokens[1]);
      continue;
    }
    if (record == 6U) {
      if (!splitExact(line, tokens, 3U) ||
          strcmp(tokens[0], "PRESSURE_X10_KPA") != 0) {
        return ParseError::InvalidField;
      }
      for (uint8_t regulator = 0; regulator < cellq::hw::REGULATOR_COUNT;
           ++regulator) {
        if (!parseSigned(tokens[regulator + 1U],
                         cellq::config::SQUARE_PRESSURE_MIN_TENTHS,
                         cellq::config::SQUARE_PRESSURE_MAX_TENTHS,
                         signedValue)) {
          return ParseError::OutOfRange;
        }
        preset.pressureTenths[regulator] =
            static_cast<int16_t>(signedValue);
      }
      continue;
    }
    if (record == 7U) {
      if (!splitExact(line, tokens, 2U) ||
          strcmp(tokens[0], "CHANNELS") != 0 ||
          !parseUnsigned(tokens[1], UINT8_MAX, value) ||
          value != cellq::hw::CHANNEL_COUNT) {
        return ParseError::WrongChannelCount;
      }
      continue;
    }
    if (record == 8U) {
      if (strcmp(line, FIELD_HEADER) != 0) {
        return ParseError::InvalidField;
      }
      continue;
    }
    if (record >= 9U && record <= 14U) {
      if (!splitExact(line, tokens, 7U) || strcmp(tokens[0], "CH") != 0) {
        return ParseError::InvalidField;
      }
      const uint8_t channel = record - 9U;
      if (!parseUnsigned(tokens[1], cellq::hw::CHANNEL_COUNT, value) ||
          value != channel + 1U) {
        return ParseError::WrongChannelCount;
      }
      if (!parseUnsigned(tokens[2], 1U, value)) {
        return ParseError::OutOfRange;
      }
      preset.channelEnabled[channel] = static_cast<uint8_t>(value);

      uint32_t frequency;
      uint32_t duty;
      uint32_t delay;
      uint32_t cycles;
      if (!parseUnsigned(tokens[3],
                         cellq::config::SQUARE_FREQUENCY_MAX_TENTHS_HZ,
                         frequency) ||
          !parseUnsigned(tokens[4], 100U, duty) ||
          !parseUnsigned(tokens[5], cellq::config::SQUARE_DELAY_MAX_MS,
                         delay) ||
          !parseUnsigned(tokens[6], UINT32_MAX, cycles)) {
        return ParseError::OutOfRange;
      }
      if (frequency != 0 &&
          frequency < cellq::config::SQUARE_FREQUENCY_MIN_TENTHS_HZ) {
        return ParseError::OutOfRange;
      }
      const uint8_t group = cellq::hw::CHANNEL_TO_REGULATOR[channel];
      if (group >= cellq::hw::REGULATOR_COUNT) {
        return ParseError::WrongChannelCount;
      }
      if (!groupParametersSet[group]) {
        groupFrequency[group] = static_cast<uint16_t>(frequency);
        groupDuty[group] = static_cast<uint8_t>(duty);
        groupDelay[group] = static_cast<uint16_t>(delay);
        groupCycles[group] = cycles;
        groupParametersSet[group] = true;
      } else if (frequency != groupFrequency[group] ||
                 duty != groupDuty[group] || delay != groupDelay[group] ||
                 cycles != groupCycles[group]) {
        return ParseError::InvalidField;
      }
      continue;
    }
    if (record == 15U) {
      if (!splitExact(line, tokens, 2U) || strcmp(tokens[0], "END") != 0 ||
          strcmp(tokens[1], "1") != 0) {
        return ParseError::InvalidField;
      }
      continue;
    }
    if (!splitExact(line, tokens, 2U) || strcmp(tokens[0], "CRC32") != 0 ||
        !parseHex32(tokens[1], value)) {
      return ParseError::InvalidField;
    }
    if ((crc ^ 0xFFFFFFFFUL) != value) {
      return ParseError::CrcMismatch;
    }
  }

  while (true) {
    const LineResult lineResult = readLine(file, line, sizeof(line));
    if (lineResult == LineResult::End) {
      break;
    }
    if (lineResult == LineResult::TooLong) {
      return ParseError::FileTooLong;
    }
    if (lineResult != LineResult::Line || line[0] != '\0') {
      return ParseError::InvalidField;
    }
  }

  for (uint8_t group = 0; group < cellq::hw::REGULATOR_COUNT; ++group) {
    if (!groupParametersSet[group]) {
      return ParseError::InvalidField;
    }
    preset.frequencyTenthsHz[group] = groupFrequency[group];
    preset.dutyPercent[group] = groupDuty[group];
    preset.delayMs[group] = groupDelay[group];
    preset.totalCycles[group] = groupCycles[group];
  }
  return ParseError::None;
}

ParseError parsePath(const char *path, uint16_t id,
                     cellq::SquareWavePreset &preset) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return ParseError::FileOpen;
  }
  cellq::SquareWavePreset candidate = {};
  const ParseError error = parsePreset(file, id, candidate);
  file.close();
  if (error == ParseError::None) {
    preset = candidate;
  }
  return error;
}

ParseError findPresetInternal(uint16_t id,
                              cellq::SquareWavePreset &preset) {
  if (!sdAvailable_) {
    return ParseError::SdUnavailable;
  }
  if (id == 0U || id > cellq::config::SQUARE_PRESET_SLOT_COUNT) {
    return ParseError::InvalidField;
  }

  char path[20];
  if (id <= 2U) {
    buildFactoryPath(id, path);
    return parsePath(path, id, preset);
  }

  cellq::SquareWavePreset candidateA = {};
  cellq::SquareWavePreset candidateB = {};
  buildUserPath(id, 'A', path);
  const ParseError errorA = parsePath(path, id, candidateA);
  buildUserPath(id, 'B', path);
  const ParseError errorB = parsePath(path, id, candidateB);
  const bool validA = errorA == ParseError::None;
  const bool validB = errorB == ParseError::None;
  if (validA && validB) {
    preset = candidateB.revision > candidateA.revision
                 ? candidateB
                 : candidateA;
    return ParseError::None;
  }
  if (validA) {
    preset = candidateA;
    return ParseError::None;
  }
  if (validB) {
    preset = candidateB;
    return ParseError::None;
  }
  return errorA != ParseError::FileOpen ? errorA : errorB;
}

void reportError(uint16_t id, ParseError error) {
  (void)id;
  switch (error) {
    case ParseError::SdUnavailable:
      DBG_PRESET_ERROR(id, "SD");
      break;
    case ParseError::FileOpen:
      DBG_PRESET_ERROR(id, "OPEN");
      break;
    case ParseError::FileTooLong:
      DBG_PRESET_ERROR(id, "LONG");
      break;
    case ParseError::BadSignature:
      DBG_PRESET_ERROR(id, "SIG");
      break;
    case ParseError::UnsupportedVersion:
      DBG_PRESET_ERROR(id, "VER");
      break;
    case ParseError::OutOfRange:
      DBG_PRESET_ERROR(id, "RANGE");
      break;
    case ParseError::WrongChannelCount:
      DBG_PRESET_ERROR(id, "CHANNEL");
      break;
    case ParseError::CrcMismatch:
      DBG_PRESET_ERROR(id, "CRC");
      break;
    default:
      DBG_PRESET_ERROR(id, "FIELD");
      break;
  }
}

bool writeCoveredLine(File &file, uint32_t &crc, const char *line) {
  crc = updateCrc(crc, line);
  return file.println(line) > 0U;
}

bool writePresetFile(const char *path,
                     const cellq::SquareWavePreset &preset) {
  if (SD.exists(path) && !SD.remove(path)) {
    return false;
  }
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  char line[cellq::config::PRESET_LINE_BUFFER_SIZE];
  uint32_t crc = 0xFFFFFFFFUL;
  bool ok = writeCoveredLine(file, crc, "CELLQ_SW,1");
  snprintf(line, sizeof(line), "ID,%u", preset.id);
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "REV,%lu",
           static_cast<unsigned long>(preset.revision));
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "NAME,%s", preset.name);
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "CREATED,%s", preset.created);
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "MODIFIED,%s", preset.modified);
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "PRESSURE_X10_KPA,%d,%d",
           preset.pressureTenths[0], preset.pressureTenths[1]);
  ok = writeCoveredLine(file, crc, line) && ok;
  snprintf(line, sizeof(line), "CHANNELS,%u",
           static_cast<unsigned int>(cellq::hw::CHANNEL_COUNT));
  ok = writeCoveredLine(file, crc, line) && ok;
  ok = writeCoveredLine(file, crc, FIELD_HEADER) && ok;
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    const uint8_t group = cellq::hw::CHANNEL_TO_REGULATOR[channel];
    snprintf(line, sizeof(line), "CH,%u,%u,%u,%u,%u,%lu",
             static_cast<unsigned int>(channel + 1U),
             static_cast<unsigned int>(preset.channelEnabled[channel]),
             static_cast<unsigned int>(preset.frequencyTenthsHz[group]),
             static_cast<unsigned int>(preset.dutyPercent[group]),
             static_cast<unsigned int>(preset.delayMs[group]),
             static_cast<unsigned long>(preset.totalCycles[group]));
    ok = writeCoveredLine(file, crc, line) && ok;
  }
  ok = writeCoveredLine(file, crc, "END,1") && ok;
  snprintf(line, sizeof(line), "CRC32,%08lX",
           static_cast<unsigned long>(crc ^ 0xFFFFFFFFUL));
  ok = file.println(line) > 0U && ok;
  file.flush();
  ok = file.getWriteError() == 0 && ok;
  file.close();
  return ok;
}

bool prepareUserSave(cellq::SquareWavePreset &preset, const char *timestamp,
                     char *path) {
  if (!validPresetData(preset)) {
    return false;
  }
  const char *saveTime = validTimestamp(timestamp)
                             ? timestamp
                             : "00000000T000000";
  char pathA[20];
  char pathB[20];
  buildUserPath(preset.id, 'A', pathA);
  buildUserPath(preset.id, 'B', pathB);
  cellq::SquareWavePreset candidateA = {};
  cellq::SquareWavePreset candidateB = {};
  const bool validA =
      parsePath(pathA, preset.id, candidateA) == ParseError::None;
  const bool validB =
      parsePath(pathB, preset.id, candidateB) == ParseError::None;

  const cellq::SquareWavePreset *newest = nullptr;
  char targetSlot = 'A';
  if (validA && validB) {
    newest = candidateB.revision > candidateA.revision ? &candidateB
                                                        : &candidateA;
    targetSlot = newest == &candidateA ? 'B' : 'A';
  } else if (validA) {
    newest = &candidateA;
    targetSlot = 'B';
  } else if (validB) {
    newest = &candidateB;
    targetSlot = 'A';
  }
  if (newest != nullptr && newest->revision == UINT32_MAX) {
    return false;
  }
  preset.revision = newest == nullptr ? 1UL : newest->revision + 1UL;
  if (newest != nullptr) {
    strcpy(preset.created, newest->created);
  } else if (!validTimestamp(preset.created)) {
    strcpy(preset.created, saveTime);
  }
  strcpy(preset.modified, saveTime);
  buildUserPath(preset.id, targetSlot, path);
  return true;
}
}

namespace cellq {
namespace preset {
void begin() {
  pinMode(hw::SD_CS_PIN, OUTPUT);
  digitalWrite(hw::SD_CS_PIN, HIGH);
  sdAvailable_ = SD.begin(hw::SD_CS_PIN);
  if (sdAvailable_) {
    DBG_SD_OK();
  } else {
    DBG_SD_FAIL();
  }
}

bool isSdAvailable() {
  return sdAvailable_;
}

bool findPreset(uint16_t id, SquareWavePreset &preset) {
  return findPresetInternal(id, preset) == ParseError::None;
}

bool loadPreset(uint16_t id, SquareWavePreset &preset) {
  SquareWavePreset candidate = {};
  const ParseError error = findPresetInternal(id, candidate);
  if (error != ParseError::None) {
    reportError(id, error);
    return false;
  }
  preset = candidate;
  DBG_PRESET_LOAD(preset.id, preset.revision);
  return true;
}

bool loadFactoryPreset1(SquareWavePreset &preset) {
  return loadPreset(1U, preset);
}

bool saveUserPreset(SquareWavePreset &preset, const char *timestamp) {
  if (!sdAvailable_) {
    DBG_PRESET_ERROR(preset.id, "SD");
    return false;
  }
  if (!SD.exists("/PRESETS") && !SD.mkdir("/PRESETS")) {
    DBG_PRESET_ERROR(preset.id, "DIR");
    return false;
  }
  SquareWavePreset candidate = preset;
  char path[20];
  if (!prepareUserSave(candidate, timestamp, path)) {
    DBG_PRESET_ERROR(preset.id, "SAVE");
    return false;
  }
  if (!writePresetFile(path, candidate)) {
    SD.remove(path);
    DBG_PRESET_ERROR(preset.id, "SAVE");
    return false;
  }
  SquareWavePreset verified = {};
  if (parsePath(path, candidate.id, verified) != ParseError::None) {
    SD.remove(path);
    DBG_PRESET_ERROR(preset.id, "VERIFY");
    return false;
  }
  preset = verified;
  DBG_PRESET_SAVE(preset.id, preset.revision);
  return true;
}

bool deleteUserPreset(uint16_t id) {
  if (!sdAvailable_ || id < 3U ||
      id > config::SQUARE_PRESET_SLOT_COUNT) {
    return false;
  }
  char pathA[20];
  char pathB[20];
  buildUserPath(id, 'A', pathA);
  buildUserPath(id, 'B', pathB);
  const bool existed = SD.exists(pathA) || SD.exists(pathB);
  if (SD.exists(pathA) && !SD.remove(pathA)) {
    DBG_PRESET_ERROR(id, "DELETE");
    return false;
  }
  if (SD.exists(pathB) && !SD.remove(pathB)) {
    DBG_PRESET_ERROR(id, "DELETE");
    return false;
  }
  if (!existed) {
    return false;
  }
  DBG_PRESET_DELETE(id);
  return true;
}

bool firstVacantUserId(uint16_t &id) {
  for (uint16_t candidate = 3U;
       candidate <= config::SQUARE_PRESET_SLOT_COUNT; ++candidate) {
    SquareWavePreset ignored = {};
    if (findPresetInternal(candidate, ignored) != ParseError::None) {
      id = candidate;
      return true;
    }
  }
  return false;
}
}
}
