#include "PresetStore.h"
#include <SD.h>
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

constexpr char FACTORY_PRESET_1_PATH[] = "/PRESETS/FCTRY1.CSV";
constexpr char FIELD_HEADER[] =
    "FIELDS,CH,ENABLED,FREQ_X10_HZ,DUTY_PCT,DELAY_MS,TOTAL_CYCLES";
bool sdAvailable_ = false;

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
  if (strlen(timestamp) != 15U || timestamp[8] != 'T') {
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
  uint16_t sharedFrequency = 0;
  uint8_t sharedDuty = 0;
  uint16_t sharedDelay = 0;
  uint32_t sharedCycles = 0;

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
      if (channel == 0U) {
        sharedFrequency = static_cast<uint16_t>(frequency);
        sharedDuty = static_cast<uint8_t>(duty);
        sharedDelay = static_cast<uint16_t>(delay);
        sharedCycles = cycles;
      } else if (frequency != sharedFrequency || duty != sharedDuty ||
                 delay != sharedDelay || cycles != sharedCycles) {
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

  preset.frequencyTenthsHz = sharedFrequency;
  preset.dutyPercent = sharedDuty;
  preset.delayMs = sharedDelay;
  preset.totalCycles = sharedCycles;
  return ParseError::None;
}

void reportError(ParseError error) {
  switch (error) {
    case ParseError::SdUnavailable:
      DBG_PRESET_ERROR("SD");
      break;
    case ParseError::FileOpen:
      DBG_PRESET_ERROR("OPEN");
      break;
    case ParseError::FileTooLong:
      DBG_PRESET_ERROR("LONG");
      break;
    case ParseError::BadSignature:
      DBG_PRESET_ERROR("SIG");
      break;
    case ParseError::UnsupportedVersion:
      DBG_PRESET_ERROR("VER");
      break;
    case ParseError::OutOfRange:
      DBG_PRESET_ERROR("RANGE");
      break;
    case ParseError::WrongChannelCount:
      DBG_PRESET_ERROR("CHANNEL");
      break;
    case ParseError::CrcMismatch:
      DBG_PRESET_ERROR("CRC");
      break;
    default:
      DBG_PRESET_ERROR("FIELD");
      break;
  }
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

bool loadFactoryPreset1(SquareWavePreset &preset) {
  if (!sdAvailable_) {
    reportError(ParseError::SdUnavailable);
    return false;
  }
  File file = SD.open(FACTORY_PRESET_1_PATH, FILE_READ);
  if (!file) {
    reportError(ParseError::FileOpen);
    return false;
  }

  SquareWavePreset candidate = {};
  const ParseError error = parsePreset(file, 1U, candidate);
  file.close();
  if (error != ParseError::None) {
    reportError(error);
    return false;
  }
  preset = candidate;
  DBG_PRESET_LOAD(preset.id, preset.revision);
  return true;
}
}
}
