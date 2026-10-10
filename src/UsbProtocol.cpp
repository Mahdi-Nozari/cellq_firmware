#include "UsbProtocol.h"
#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include "BuildConfig.h"
#include "DataLogger.h"
#include "Display.h"
#include "DisplayObjectMap.h"
#include "HardwareConfig.h"
#include "ManualMode.h"
#include "MonitorMode.h"
#include "PresetStore.h"
#include "SquareWaveMode.h"
#include "SquareWaveSetup.h"

namespace {
enum class Work : uint8_t { None, List, Get, Hash, Delete };
enum class PendingAck : uint8_t { None, Enter, Firmware };
constexpr uint8_t TX_CAPACITY = 64U;
constexpr uint8_t DATA_HEADER_SIZE = 8U;
constexpr uint8_t DATA_TRAILER_SIZE = 2U;
static_assert(cellq::config::USB_DATA_CHUNK_SIZE + DATA_HEADER_SIZE +
                  DATA_TRAILER_SIZE <= TX_CAPACITY,
              "USB data frame exceeds the AVR serial buffer");

char rxLine_[cellq::config::USB_RX_LINE_SIZE] = {};
uint8_t rxLength_ = 0U;
bool rxOverflow_ = false;
uint8_t txBuffer_[TX_CAPACITY] = {};
uint8_t txLength_ = 0U;
File file_;
Work work_ = Work::None;
bool maintenance_ = false;
bool firmwarePrepared_ = false;
PendingAck pendingAck_ = PendingAck::None;
bool listEmpty_ = false;
const char *statusText_ = "Ready";
char path_[cellq::config::DATA_LOG_PATH_BUFFER_SIZE] = {};
uint32_t fileSize_ = 0UL;
uint32_t crc32_ = 0xFFFFFFFFUL;
uint32_t expectedSize_ = 0UL;
uint32_t expectedCrc_ = 0UL;

void setStatus(const char *text) {
  statusText_ = text;
  if (maintenance_) {
    cellq::display::setMaintenanceStatus(text);
  }
}

bool queueText(const char *line) {
  if (txLength_ != 0U) {
    return false;
  }
  const size_t length = strlen(line);
  if (length + 1U > TX_CAPACITY) {
    return false;
  }
  memcpy(txBuffer_, line, length);
  txBuffer_[length] = '\n';
  txLength_ = static_cast<uint8_t>(length + 1U);
  return true;
}

void sendError(const char *error) {
  char line[TX_CAPACITY];
  snprintf(line, sizeof(line), "CQ1,ERR,%s", error);
  queueText(line);
}

void flushTx() {
  if (txLength_ != 0U && Serial.availableForWrite() >= txLength_) {
    Serial.write(txBuffer_, txLength_);
    txLength_ = 0U;
  }
}

void closeWork() {
  if (file_) {
    file_.close();
  }
  work_ = Work::None;
  listEmpty_ = false;
  path_[0] = '\0';
}

bool parseUnsigned(const char *text, uint32_t &value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  value = 0UL;
  for (; *text != '\0'; ++text) {
    if (*text < '0' || *text > '9') {
      return false;
    }
    const uint8_t digit = static_cast<uint8_t>(*text - '0');
    if (value > (UINT32_MAX - digit) / 10UL) {
      return false;
    }
    value = value * 10UL + digit;
  }
  return true;
}

bool parseHex32(const char *text, uint32_t &value) {
  if (text == nullptr || strlen(text) != 8U) {
    return false;
  }
  value = 0UL;
  for (uint8_t index = 0; index < 8U; ++index) {
    const char digit = text[index];
    uint8_t nibble;
    if (digit >= '0' && digit <= '9') {
      nibble = digit - '0';
    } else if (digit >= 'A' && digit <= 'F') {
      nibble = digit - 'A' + 10U;
    } else {
      return false;
    }
    value = (value << 4U) | nibble;
  }
  return true;
}

bool digits(const char *text, uint8_t length) {
  for (uint8_t index = 0; index < length; ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return false;
    }
  }
  return true;
}

bool validDirectory(const char *path) {
  if (strcmp(path, "/DATA") == 0 || strcmp(path, "/DATA/NO_RTC") == 0) {
    return true;
  }
  return strlen(path) == 14U && strncmp(path, "/DATA/", 6U) == 0 &&
         digits(path + 6U, 8U);
}

bool validLogPath(const char *path) {
  const size_t length = strlen(path);
  if (length == 25U && strncmp(path, "/DATA/NO_RTC/RUN", 16U) == 0 &&
      digits(path + 16U, 5U) && strcmp(path + 21U, ".CSV") == 0) {
    return true;
  }
  return length == 27U && strncmp(path, "/DATA/", 6U) == 0 &&
         digits(path + 6U, 8U) && path[14] == '/' &&
         digits(path + 15U, 8U) && strcmp(path + 23U, ".CSV") == 0;
}

bool manualValveActive() {
  for (uint8_t channel = 0; channel < cellq::hw::CHANNEL_COUNT; ++channel) {
    if (cellq::manual::valveIsOn(channel)) {
      return true;
    }
  }
  return false;
}

const char *deviceState() {
  if (maintenance_) {
    return "MAINT";
  }
  if (cellq::square::isProcedureActive()) {
    return "TEST";
  }
  if (manualValveActive()) {
    return "MANUAL";
  }
  return "IDLE";
}

uint32_t updateCrc32(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (uint8_t bit = 0; bit < 8U; ++bit) {
    crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : crc >> 1U;
  }
  return crc;
}

uint16_t frameCrc16(const uint8_t *data, uint8_t length) {
  uint16_t crc = 0xFFFFU;
  for (uint8_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8U;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U ? (crc << 1U) ^ 0x1021U : crc << 1U;
    }
  }
  return crc;
}

void enterMaintenance() {
  if (cellq::square::isProcedureActive()) {
    sendError("TEST_ACTIVE");
    return;
  }
  if (manualValveActive()) {
    sendError("VALVE_ACTIVE");
    return;
  }
  cellq::manual::enterMaintenance();
  cellq::logger::setEnabled(false, millis());
  const cellq::display::Event hidden = {cellq::gui::FORM_OBJECT,
                                        cellq::gui::MAINTENANCE_FORM, 0U};
  cellq::square::handleDisplayEvent(hidden, millis());
  cellq::setup::handleDisplayEvent(hidden, millis());
  cellq::monitor::handleDisplayEvent(hidden, millis());
  maintenance_ = true;
  firmwarePrepared_ = false;
  cellq::display::activateForm(cellq::gui::MAINTENANCE_FORM);
  setStatus("Ready");
  pendingAck_ = PendingAck::Enter;
}

void exitMaintenance() {
  closeWork();
  pendingAck_ = PendingAck::None;
  firmwarePrepared_ = false;
  maintenance_ = false;
  cellq::display::activateForm(cellq::gui::HOME_FORM);
  cellq::display::setHomeVersionLabel();
  queueText("CQ1,OK,EXIT");
}

bool openLog(const char *path) {
  if (!cellq::preset::isSdAvailable() || !validLogPath(path)) {
    return false;
  }
  file_ = SD.open(path, FILE_READ);
  if (!file_ || file_.isDirectory()) {
    closeWork();
    return false;
  }
  strncpy(path_, path, sizeof(path_) - 1U);
  path_[sizeof(path_) - 1U] = '\0';
  fileSize_ = file_.size();
  return true;
}

void sendNext() {
  if (file_.position() >= fileSize_) {
    char line[TX_CAPACITY];
    snprintf(line, sizeof(line), "CQ1,EOF,%lu,%08lX",
             static_cast<unsigned long>(fileSize_),
             static_cast<unsigned long>(crc32_ ^ 0xFFFFFFFFUL));
    closeWork();
    setStatus("Transfer complete");
    queueText(line);
    return;
  }
  const uint32_t offset = file_.position();
  const int count = file_.read(txBuffer_ + DATA_HEADER_SIZE,
                               cellq::config::USB_DATA_CHUNK_SIZE);
  if (count <= 0) {
    closeWork();
    setStatus("Transfer error");
    sendError("READ");
    return;
  }
  txBuffer_[0] = 0xA5U;
  txBuffer_[1] = 0x5AU;
  txBuffer_[2] = 0x01U;
  txBuffer_[3] = static_cast<uint8_t>(count);
  for (uint8_t index = 0; index < 4U; ++index) {
    txBuffer_[4U + index] = static_cast<uint8_t>(offset >> (8U * index));
  }
  for (uint8_t index = 0; index < static_cast<uint8_t>(count); ++index) {
    crc32_ = updateCrc32(crc32_, txBuffer_[DATA_HEADER_SIZE + index]);
  }
  const uint8_t payloadEnd = DATA_HEADER_SIZE + count;
  const uint16_t crc = frameCrc16(txBuffer_, payloadEnd);
  txBuffer_[payloadEnd] = static_cast<uint8_t>(crc);
  txBuffer_[payloadEnd + 1U] = static_cast<uint8_t>(crc >> 8U);
  txLength_ = payloadEnd + DATA_TRAILER_SIZE;
}

void startScan(const char *path, Work mode, uint32_t expectedSize,
               uint32_t expectedCrc) {
  if (!openLog(path)) {
    sendError("FILE");
    return;
  }
  work_ = mode;
  crc32_ = 0xFFFFFFFFUL;
  expectedSize_ = expectedSize;
  expectedCrc_ = expectedCrc;
  setStatus("Checking log");
  queueText(mode == Work::Delete ? "CQ1,OK,CHECKING" : "CQ1,OK,HASHING");
}

void serviceScan() {
  uint8_t bytes = 0U;
  while (bytes < 96U && file_.available()) {
    const int next = file_.read();
    if (next < 0) {
      closeWork();
      setStatus("Transfer error");
      sendError("READ");
      return;
    }
    crc32_ = updateCrc32(crc32_, static_cast<uint8_t>(next));
    ++bytes;
  }
  if (file_.available()) {
    return;
  }
  const Work completed = work_;
  const uint32_t size = fileSize_;
  const uint32_t crc = crc32_ ^ 0xFFFFFFFFUL;
  char savedPath[sizeof(path_)];
  strncpy(savedPath, path_, sizeof(savedPath));
  savedPath[sizeof(savedPath) - 1U] = '\0';
  closeWork();
  if (completed == Work::Delete) {
    if (size != expectedSize_ || crc != expectedCrc_) {
      sendError("MISMATCH");
    } else if (!SD.remove(savedPath)) {
      sendError("DELETE");
    } else {
      queueText("CQ1,OK,DELETE");
    }
  } else {
    char line[TX_CAPACITY];
    snprintf(line, sizeof(line), "CQ1,HASH,%lu,%08lX",
             static_cast<unsigned long>(size), static_cast<unsigned long>(crc));
    queueText(line);
  }
  setStatus("Ready");
}

void serviceList() {
  if (listEmpty_) {
    closeWork();
    setStatus("Ready");
    queueText("CQ1,LIST,END");
    return;
  }
  File item = file_.openNextFile();
  if (!item) {
    closeWork();
    setStatus("Ready");
    queueText("CQ1,LIST,END");
    return;
  }
  char line[TX_CAPACITY];
  snprintf(line, sizeof(line), "CQ1,ITEM,%s,%c,%lu", item.name(),
           item.isDirectory() ? 'D' : 'F',
           static_cast<unsigned long>(item.size()));
  item.close();
  queueText(line);
}

void handleCommand(char *line) {
  char *fields[5] = {};
  uint8_t count = 0U;
  fields[count++] = line;
  for (char *cursor = line; *cursor != '\0'; ++cursor) {
    if (*cursor == ',') {
      if (count >= 5U) {
        sendError("SYNTAX");
        return;
      }
      *cursor = '\0';
      fields[count++] = cursor + 1U;
    }
  }
  if (strcmp(fields[0], "CQ1") != 0 || count < 2U) {
    sendError("SYNTAX");
    return;
  }
  const char *command = fields[1];
  if (strcmp(command, "HELLO") == 0 && count == 2U) {
    char reply[TX_CAPACITY];
    snprintf(reply, sizeof(reply), "CQ1,HELLO,%s,%s",
             cellq::config::FIRMWARE_VERSION_TEXT, deviceState());
    queueText(reply);
    return;
  }
  if (strcmp(command, "ENTER") == 0 && count == 2U) {
    if (maintenance_) {
      sendError("ALREADY_IN");
    } else {
      enterMaintenance();
    }
    return;
  }
  if (!maintenance_) {
    sendError("NOT_IN_MAINT");
    return;
  }
  if (strcmp(command, "EXIT") == 0 && count == 2U) {
    exitMaintenance();
    return;
  }
  if (strcmp(command, "CANCEL") == 0 && count == 2U) {
    closeWork();
    setStatus("Ready");
    queueText("CQ1,OK,CANCEL");
    return;
  }
  if (work_ != Work::None) {
    if (work_ == Work::Get && strcmp(command, "NEXT") == 0 &&
        count == 2U) {
      sendNext();
    } else {
      sendError("BUSY");
    }
    return;
  }
  if (firmwarePrepared_) {
    sendError("UPDATE_PREPARED");
    return;
  }
  if (strcmp(command, "LIST") == 0 && count == 3U) {
    if (!validDirectory(fields[2])) {
      sendError("PATH");
    } else if (!cellq::preset::isSdAvailable()) {
      sendError("SD");
    } else if (strcmp(fields[2], "/DATA") == 0 &&
               !SD.exists("/DATA")) {
      listEmpty_ = true;
      work_ = Work::List;
      setStatus("Listing logs");
      queueText("CQ1,LIST,BEGIN");
    } else {
      file_ = SD.open(fields[2], FILE_READ);
      if (!file_ || !file_.isDirectory()) {
        closeWork();
        sendError("DIRECTORY");
      } else {
        work_ = Work::List;
        setStatus("Listing logs");
        queueText("CQ1,LIST,BEGIN");
      }
    }
    return;
  }
  if (strcmp(command, "GET") == 0 && count == 4U) {
    uint32_t offset;
    if (!parseUnsigned(fields[3], offset) || !validLogPath(fields[2])) {
      sendError("SYNTAX");
    } else if (!openLog(fields[2])) {
      sendError("FILE");
    } else if (offset > fileSize_ || !file_.seek(offset)) {
      closeWork();
      sendError("OFFSET");
    } else {
      work_ = Work::Get;
      crc32_ = 0xFFFFFFFFUL;
      setStatus("Transferring logs");
      char reply[TX_CAPACITY];
      snprintf(reply, sizeof(reply), "CQ1,GET,%lu,%lu",
               static_cast<unsigned long>(fileSize_),
               static_cast<unsigned long>(offset));
      queueText(reply);
    }
    return;
  }
  if (strcmp(command, "HASH") == 0 && count == 3U) {
    startScan(fields[2], Work::Hash, 0UL, 0UL);
    return;
  }
  if (strcmp(command, "DELETE") == 0 && count == 5U) {
    uint32_t size;
    uint32_t crc;
    if (!parseUnsigned(fields[3], size) || !parseHex32(fields[4], crc)) {
      sendError("SYNTAX");
    } else {
      startScan(fields[2], Work::Delete, size, crc);
    }
    return;
  }
  if (strcmp(command, "FWPREP") == 0 && count == 2U) {
    firmwarePrepared_ = true;
    setStatus("Updating firmware");
    pendingAck_ = PendingAck::Firmware;
    return;
  }
  sendError("UNKNOWN");
}

void serviceInput() {
  uint8_t processed = 0U;
  while (Serial.available() > 0 && processed < 64U && txLength_ == 0U) {
    const char next = static_cast<char>(Serial.read());
    ++processed;
    if (next == '\n') {
      if (rxOverflow_) {
        sendError("LONG");
      } else if (rxLength_ != 0U) {
        if (rxLine_[rxLength_ - 1U] == '\r') {
          --rxLength_;
        }
        rxLine_[rxLength_] = '\0';
        handleCommand(rxLine_);
      }
      rxLength_ = 0U;
      rxOverflow_ = false;
      break;
    }
    if (rxLength_ + 1U >= sizeof(rxLine_)) {
      rxOverflow_ = true;
    } else if (!rxOverflow_) {
      rxLine_[rxLength_++] = next;
    }
  }
}
}

namespace cellq {
namespace usb {
void begin() {
  Serial.begin(config::DEBUG_BAUD);
  rxLength_ = 0U;
  rxOverflow_ = false;
  txLength_ = 0U;
  maintenance_ = false;
  firmwarePrepared_ = false;
  pendingAck_ = PendingAck::None;
  work_ = Work::None;
}

void service() {
  flushTx();
  if (txLength_ != 0U) {
    return;
  }
  if (pendingAck_ != PendingAck::None) {
    if (display::idle()) {
      queueText(pendingAck_ == PendingAck::Enter ? "CQ1,OK,ENTER"
                                                 : "CQ1,OK,FWPREP");
      pendingAck_ = PendingAck::None;
      flushTx();
    }
    return;
  }
  serviceInput();
  if (txLength_ == 0U && work_ == Work::List) {
    serviceList();
  } else if (txLength_ == 0U &&
             (work_ == Work::Hash || work_ == Work::Delete)) {
    serviceScan();
  }
  flushTx();
}

bool maintenanceActive() {
  return maintenance_;
}

void restoreMaintenanceForm() {
  if (maintenance_) {
    display::activateForm(gui::MAINTENANCE_FORM);
    display::setMaintenanceStatus(statusText_);
  }
}
}
}
