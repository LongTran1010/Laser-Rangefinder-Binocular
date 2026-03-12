#pragma once
#include <Arduino.h>

enum MeasStatus : uint8_t {
  MEAS_OK = 0,
  MEAS_TIMEOUT,
  MEAS_BAD_CRC,
  MEAS_BAD_FRAME,
  MEAS_NO_SIGNAL
};

struct Measurement {
  uint32_t t_ms;       // timestamp (ms)
  float    dist_m;     // khoảng cách (m)
  uint16_t strength;   // "Strength" từ TF02 (nếu không có thì 0)
  float    temp_C;     // nhiệt độ (nếu không có thì NAN)
  MeasStatus status;   // trạng thái mẫu
  float slantRange;    // Khoảng cách chéo (m)
  float horzDist;      // Khoảng cách ngang (m)
  float vertDist;      // Độ cao/sâu (m)
  float speedKmh;      // Vận tốc mục tiêu (km/h)
  float pitchAngle;    // Góc nghiêng (độ)
  int   confidence;    // Độ tin cậy (0-100%)
};
constexpr float TC22_BLIND_M = 3.0f;