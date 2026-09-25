#include "MAX30102.h"

static const uint8_t MAX_30102_ID = 0x15;

MAX30102::MAX30102() {
  // Constructor
}

boolean MAX30102::begin(TwoWire &wirePort, uint8_t i2caddr) {
  _i2cPort = &wirePort;
  _i2caddr = i2caddr;

  _i2cPort->begin();
  delay(10);

  if (readRegister8(REG_PART_ID) != MAX_30102_ID) return false;

  // Reset internal sense buffer indexes
  sense.head = 0;
  sense.tail = 0;

  return true;
}

void MAX30102::setup() {
  writeRegister8(REG_MODE_CONFIG, 0x40); // Reset
  delay(500);

  writeRegister8(REG_FIFO_WR_PTR, 0x00);
  writeRegister8(REG_OVF_COUNTER, 0x00);
  writeRegister8(REG_FIFO_RD_PTR, 0x00);
  writeRegister8(REG_FIFO_CONFIG, 0x4F); // sample avg = 4, fifo rollover=false, fifo almost full=17
  writeRegister8(REG_MODE_CONFIG, 0x03); // SpO2 mode
  writeRegister8(REG_SPO2_CONFIG, 0x27); // ADC range, sample rate, pulse width
  writeRegister8(REG_LED1_PA, 0x17);     // LED1 current ~6mA (IR)
  writeRegister8(REG_LED2_PA, 0x17);     // LED2 current ~6mA (Red)
  writeRegister8(REG_PILOT_PA, 0x1F);    // Pilot LED current ~6mA
}

uint8_t MAX30102::available(void) {
  int8_t numberOfSamples = sense.head - sense.tail;
  if (numberOfSamples < 0) numberOfSamples += STORAGE_SIZE;
  return numberOfSamples;
}

uint32_t MAX30102::getRed(void) {
  return sense.red[sense.tail];
}

uint32_t MAX30102::getIR(void) {
  return sense.IR[sense.tail];
}

void MAX30102::nextSample(void) {
  if (available()) {
    sense.tail++;
    sense.tail %= STORAGE_SIZE;
  }
}

uint16_t MAX30102::check(void) {
  uint8_t readPointer = readRegister8(REG_FIFO_RD_PTR);
  uint8_t writePointer = readRegister8(REG_FIFO_WR_PTR);
  int numberOfSamples = 0;

  if (readPointer != writePointer) {
    numberOfSamples = writePointer - readPointer;
    if (numberOfSamples < 0) numberOfSamples += 32;

    int bytesLeftToRead = numberOfSamples * 6;
    bytesLeftToRead = bytesLeftToRead <= 32 ? bytesLeftToRead : 32;

    _i2cPort->beginTransmission(_i2caddr);
    _i2cPort->write(REG_FIFO_DATA);
    _i2cPort->endTransmission();

    _i2cPort->requestFrom(_i2caddr, (uint8_t)bytesLeftToRead);

    while (bytesLeftToRead > 0) {
      sense.head++;
      sense.head %= STORAGE_SIZE;
      sense.IR[sense.head] = readFIFOSample();

      sense.red[sense.head] = readFIFOSample();

      bytesLeftToRead -= 6;
    }
  }

  return numberOfSamples;
}

uint8_t MAX30102::readRegister8(uint8_t reg) {
  _i2cPort->beginTransmission(_i2caddr);
  _i2cPort->write(reg);
  _i2cPort->endTransmission(false);

  _i2cPort->requestFrom(_i2caddr, (uint8_t)1);

  uint8_t value = 0;
  if (_i2cPort->available()) {
    value = _i2cPort->read();
  }
  return value;
}

void MAX30102::writeRegister8(uint8_t reg, uint8_t value) {
  _i2cPort->beginTransmission(_i2caddr);
  _i2cPort->write(reg);
  _i2cPort->write(value);
  _i2cPort->endTransmission();
}

uint32_t MAX30102::readFIFOSample() {
  uint8_t temp[4] = {0};
  uint32_t temp32 = 0;

  temp[2] = _i2cPort->read();
  temp[1] = _i2cPort->read();
  temp[0] = _i2cPort->read();

  memcpy(&temp32, temp, 4);
  return temp32 & 0x3FFFF;
}
