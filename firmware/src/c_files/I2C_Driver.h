#pragma once
#include <Arduino.h>
#include <Wire.h> 

#define I2C_MASTER_FREQ_HZ  (400000)  /*!< I2C master clock frequency */
#define I2C_SCL_PIN         10        // Correct WaveShare SCL Pin
#define I2C_SDA_PIN         11        // Correct WaveShare SDA Pin

void I2C_Init(void);

// Changed to 'int' so it perfectly matches the 'esp_err_t' the Gyro expects
int I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
int I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);