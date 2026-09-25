#ifndef MAX30102_H
#define MAX30102_H

#include <Arduino.h>
#include <Wire.h>

#define MAX30105_ADDRESS 0x57

// Registers (add others as needed)
#define REG_PART_ID         0xFF
#define REG_FIFO_WR_PTR     0x04
#define REG_OVF_COUNTER     0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D
#define REG_PILOT_PA        0x10
#define REG_FIFO_DATA       0x07

#define STORAGE_SIZE 32

class MAX30102 {
public:
  MAX30102();

  // Begin with I2C port & address
  boolean begin(TwoWire &wirePort = Wire, uint8_t i2caddr = MAX30105_ADDRESS);

  void setup();

  uint8_t available(void);
  uint32_t getRed(void);
  uint32_t getIR(void);
  void nextSample(void);

  uint16_t check(void);

private:
  TwoWire *_i2cPort;
  uint8_t _i2caddr;

  struct {
    uint8_t head = 0;
    uint8_t tail = 0;
    uint32_t IR[STORAGE_SIZE];
    uint32_t red[STORAGE_SIZE];
  } sense;

  uint8_t readRegister8(uint8_t reg);
  void writeRegister8(uint8_t reg, uint8_t value);
  uint32_t readFIFOSample();
};

#endif
