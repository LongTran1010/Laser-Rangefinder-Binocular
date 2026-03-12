#pragma once
#include <Arduino.h>
#include "MeasurementTypes.h"

// Driver khung 9 byte mặc định (đơn vị cm) của TF02-Pro
class TF02ProDriver {
public:
  TF02ProDriver(HardwareSerial& port = Serial2, int rxPin = 16, int txPin = 17)
  : port_(port), rx_(rxPin), tx_(txPin) {}

  bool begin(uint32_t baud = 115200) {
    port_.begin(baud, SERIAL_8N1, rx_, tx_);
    resetParser();
    return true;
  }

  void setTimeoutMs(uint32_t ms)      { timeoutMs_ = ms; }
  void setStrengthMin(uint16_t s)     { strengthMin_ = s; }

  // Non-blocking; gọi thường xuyên trong loop.
  // Trả về true nếu có measurement mới (OK hoặc NO_SIGNAL).
  bool poll(Measurement& out) {
    while (port_.available()) {
      uint8_t b = port_.read();
      switch (st_) {
        case 0: // chờ 0x59
          if (b == 0x59) { buf_[0] = b; st_ = 1; t0_ = millis(); }
          break;
        case 1: // chờ byte header thứ 2
          if (b == 0x59) { buf_[1] = b; idx_ = 2; st_ = 2; }
          else { st_ = 0; }
          break;
        case 2: // nhận 7 byte còn lại
          buf_[idx_++] = b;
          if (idx_ >= 9) {
            st_ = 0; idx_ = 0;
            // checksum
            uint8_t sum = 0; for (int i = 0; i < 8; ++i) sum += buf_[i];
            if (sum != buf_[8]) {
              // khách quan: BAD_CRC - không trả mẫu ra, cứ bỏ qua khung
              return false;
            }
            uint16_t dist_cm  = (uint16_t)buf_[2] | ((uint16_t)buf_[3] << 8);
            uint16_t strength = (uint16_t)buf_[4] | ((uint16_t)buf_[5] << 8);
            int16_t  tempRaw  = (int16_t)((uint16_t)buf_[6] | ((uint16_t)buf_[7] << 8));
            float    tempC    = tempRaw / 8.0f - 256.0f;

            out.t_ms     = millis();
            out.dist_m   = dist_cm / 100.0f;  // cm -> m
            out.strength = strength;
            out.temp_C   = tempC;

            // lọc giá trị đặc biệt/không tin cậy
            if (dist_cm == 4500 || strength == 65535 || strength < strengthMin_ || dist_cm == 0) {
              out.status = MEAS_NO_SIGNAL;
            } else {
              out.status = MEAS_OK;
            }
            return true;
          }
          break;
      }
      // timeout đang nhận dở
      if (st_ > 0 && (millis() - t0_ > timeoutMs_)) {
        st_ = 0; idx_ = 0; // bỏ khung dở
        // không phát "mẫu timeout" ở đây để tránh spam; UI dùng cơ chế stale
        return false;
      }
    }
    return false;
  }

  void resetParser() { st_ = 0; idx_ = 0; t0_ = millis(); }

private:
  HardwareSerial& port_;
  int rx_, tx_;
  uint32_t timeoutMs_  = 120;     // ms
  uint16_t strengthMin_ = 30;

  uint8_t  buf_[9];
  uint8_t  st_ = 0;
  uint8_t  idx_ = 0;
  uint32_t t0_ = 0;
};
