#ifndef QMI8658_H
#define QMI8658_H

#include <Wire.h>

class QMI8658 {
public:
  QMI8658() {}

  bool begin(TwoWire *wire, uint32_t speed = 100000) {
    (void)wire;
    (void)speed;
    return false;
  }

  void enableLowPowerMode(bool enabled) {
    (void)enabled;
  }

  void readSensor() {}

  float getAccelX_mss() { return 0.0f; }
  float getAccelY_mss() { return 0.0f; }
  float getAccelZ_mss() { return 0.0f; }
  float getGyroX_rads() { return 0.0f; }
  float getGyroY_rads() { return 0.0f; }
  float getGyroZ_rads() { return 0.0f; }
};

#endif // QMI8658_H
