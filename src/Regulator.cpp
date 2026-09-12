#include "Regulator.h"
#include <Wire.h>
#include "BuildConfig.h"
#include "Debug.h"
#include "HardwareConfig.h"

namespace {
constexpr uint8_t MCP4725_WRITE_DAC = 0x40U;

bool writeCode(uint8_t regulator, uint16_t code) {
  if (regulator >= cellq::hw::REGULATOR_COUNT) {
    return false;
  }
  if (code > cellq::config::REGULATOR_DAC_MAX_CODE) {
    code = cellq::config::REGULATOR_DAC_MAX_CODE;
  }
  Wire.beginTransmission(cellq::hw::DAC_ADDRESSES[regulator]);
  Wire.write(MCP4725_WRITE_DAC);
  Wire.write(static_cast<uint8_t>(code >> 4U));
  Wire.write(static_cast<uint8_t>((code & 0x0FU) << 4U));
  const bool ok = Wire.endTransmission() == 0;
  DBG_DAC(regulator + 1U, code, ok ? 1 : 0);
  return ok;
}
}

namespace cellq {
namespace regulator {
void begin() {
  Wire.begin();
  Wire.setWireTimeout(config::I2C_TIMEOUT_US, true);
  idle();
}

void idle() {
  for (uint8_t regulator = 0; regulator < hw::REGULATOR_COUNT;
       ++regulator) {
    writeCode(regulator, config::REGULATOR_SAFE_DAC_CODE);
  }
}

uint16_t pressureToDacCode(int16_t tenths) {
  int32_t magnitude = tenths < 0 ? -static_cast<int32_t>(tenths) : 0;
  if (magnitude > config::REGULATOR_FULL_SCALE_TENTHS) {
    magnitude = config::REGULATOR_FULL_SCALE_TENTHS;
  }
  return static_cast<uint16_t>(
      static_cast<uint32_t>(magnitude) * config::REGULATOR_DAC_MAX_CODE /
      config::REGULATOR_FULL_SCALE_TENTHS);
}

bool setPressureTenths(uint8_t regulator, int16_t tenths) {
  return writeCode(regulator, pressureToDacCode(tenths));
}
}
}
