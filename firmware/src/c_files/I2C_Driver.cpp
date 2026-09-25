#include "I2C_Driver.h"

void I2C_Init(void) {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_MASTER_FREQ_HZ); // Force the bus to run at 400kHz for the AMOLED       
}

int I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr); 
  if (Wire.endTransmission(true) != 0){
    printf("The I2C transmission fails. - I2C Read\r\n");
    return -1; // Return -1 for error
  }
  
  Wire.requestFrom((uint16_t)Driver_addr, (size_t)Length);
  for (uint32_t i = 0; i < Length; i++) {
    if (Wire.available()) {
        *Reg_data++ = Wire.read();
    }
  }
  return 0; // Return 0 for ESP_OK (Success)
}

int I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);       
  for (uint32_t i = 0; i < Length; i++) {
    Wire.write(*Reg_data++);
  }
  if (Wire.endTransmission(true) != 0)
  {
    printf("The I2C transmission fails. - I2C Write\r\n");
    return -1; // Return -1 for error
  }
  return 0; // Return 0 for ESP_OK (Success)
}