//sensor/driver/parser
#pragma once
#include <Arduino.h>
#include "MeasurementTypes.h"


// TC22 UART driver
//  - UART 115200 8N1, TTL 3.3V.
//  - Start/Stop command: MsgType=0xFA, MsgCode=0x01, PayLoadLen=0x04,
//      MeaType (1=start, 0=stop), MeaTimes (0=infinite, 1=single).
//  - Measurement report: [FB 03 BrdId 04 DataValid_L DataValid_H Dist_L Dist_H CRC]
//      Distance unit is dm (decimetre). Convert to metres: m = dist_dm / 10.0f
//  - CRC = (sum of first N-1 bytes) & 0xFF


class TC22Driver {
public:
  TC22Driver(HardwareSerial& port = Serial2, int rxPin = 16, int txPin = 17)
  : port_(port), rx_(rxPin), tx_(txPin) {}

  bool begin(uint32_t baud = 115200) {
    port_.begin(baud, SERIAL_8N1, rx_, tx_);
    resetParser();
    return true;
  }

  void setTimeoutMs(uint32_t ms) { timeoutMs_ = ms; }
  void setBoardId(uint8_t id)    { brdId_ = id; }   // mặc định 0xFF
  void setLittleEndianPayload(bool le) { littleEndian_ = le; }

  // ---- Control helpers (non-blocking send) ----
  void startContinuous() { sendStartStop(/*start=*/true, /*times=*/0); }
  void startSingle()     { sendStartStop(/*start=*/true, /*times=*/1); }
  void stop()            { sendStartStop(/*start=*/false,/*times=*/0); }




  // Non-blocking; call often in loop().
  // Returns true when a measurement frame (valid or invalid) was parsed.
  bool poll(Measurement& out) {
    while (port_.available()) {
      uint8_t b = port_.read();
      switch (st_) {
        case 0: // chờ 0xFB
          if (b == 0xFB) { buf_[0] = b; st_ = 1; t0_ = millis(); /*Serial.printf("Start RX,%lu\n", (unsigned long)t0_);*/}
          break;
        case 1: // chờ 0x03
          if (b == 0x03) { buf_[1] = b; idx_ = 2; st_ = 2; }
          else { 
            st_ = 0; 
            out.t_ms     = millis();
            out.dist_m   = NAN;
            out.strength = 0;
            //out.temp_C   = NAN;
            out.status   = MEAS_BAD_FRAME; //báo sai khung
            return true; // phát lỗi
          }
          break;
        case 2: // nhận 7 byte còn lại
          buf_[idx_++] = b;
          if (idx_ >= 9) {
            st_ = 0; idx_ = 0;
            // CRC check
            uint8_t crc = sum8(buf_, 8);
            if (crc != buf_[8]) {
            //return false; // BAD_CRC (đổi thành trả mẫu lỗi) 
              out.t_ms     = millis();
              out.dist_m   = NAN;
              out.strength = 0;
              out.temp_C   = NAN;
              out.status   = MEAS_BAD_CRC;
              return true;
            }
            const uint8_t brdId = buf_[2];                (void)brdId;
            const uint8_t payLen= buf_[3];                (void)payLen; // nên là 0x04
            //uint16_t valid =  uint16_t(buf_[4]) | (uint16_t(buf_[5]) << 8);
            //uint16_t dist_dm = uint16_t(buf_[6]) | (uint16_t(buf_[7]) << 8);
            uint16_t valid, dist_dm;
            if (littleEndian_) {
              valid =  uint16_t(buf_[4]) | (uint16_t(buf_[5]) << 8);
              dist_dm = uint16_t(buf_[6]) | (uint16_t(buf_[7]) << 8);
            } else {
              valid =  (uint16_t(buf_[5]) << 0) | (uint16_t(buf_[4]) << 8);
              dist_dm =  (uint16_t(buf_[7]) << 0) | (uint16_t(buf_[6]) << 8);
            }
            out.t_ms     = millis();
            const float m   = dist_dm / 10.0f; // dm -> m
            out.dist_m   = m;
            out.strength = 0;               // TC22 k trả strength
            out.temp_C   = NAN;             // TC22 k trả temperature

            out.status = (valid == 1 && m >= TC22_BLIND_M) ? MEAS_OK : MEAS_NO_SIGNAL;
            return true;
            
          }
          break;
      }
      // timeout đang nhận dở
      if (st_ > 0 && (millis() - t0_ > timeoutMs_)) {
        st_ = 0; idx_ = 0;// bỏ khung dở
        // không phát "mẫu timeout" ở đây để tránh spam; UI dùng cơ chế stale
        return false;
      }
    }
    return false;
  }

  void resetParser() { st_ = 0; idx_ = 0; t0_ = millis(); }

private:
  bool littleEndian_ = true;
  // send start/stop command
  void sendStartStop(bool start, uint16_t times) {
    uint8_t pkt[9];
    pkt[0] = 0xFA; // MsgType
    pkt[1] = 0x01; // MsgCode: Start/Stop measure
    pkt[2] = brdId_;     // BrdId (0xFF broadcast)
    pkt[3] = 0x04;       // PayLoadLen
    // MeaType (LE): 1=start, 0=stop
    pkt[4] = (start ? 1 : 0) & 0xFF;
    pkt[5] = 0x00;
    // MeaTimes (LE): 0=infinite, 1=single, or N times
    pkt[6] = (uint8_t)(times & 0xFF);
    pkt[7] = (uint8_t)((times >> 8) & 0xFF);
    pkt[8] = sum8(pkt, 8); // CRC = lower 8 bits of sum
    port_.write(pkt, sizeof(pkt));
  }

  static uint8_t sum8(const uint8_t* b, size_t n) {
    uint32_t s = 0; for (size_t i = 0; i < n; ++i) s += b[i];
    return (uint8_t)(s & 0xFF);
  }

  HardwareSerial& port_;
  int rx_, tx_;
  uint8_t  brdId_     = 0xFF;   // broadcast by default
  uint32_t timeoutMs_ = 120;    // like TF02 driver

  uint8_t  buf_[9];
  uint8_t  st_ = 0;
  uint8_t  idx_ = 0;
  uint32_t t0_ = 0;
};

/* --- Minimal usage example (ESP32) ---
#include "TC22Driver.h"
HardwareSerial& LRF = Serial2; // optional alias
TC22Driver tc(LRF, 16, 17);    // RX=16, TX=17 (DOIT DevKit V1 defaults)

void setup(){
  Serial.begin(115200);
  tc.begin();
  delay(50);
  tc.startContinuous(); // or: tc.startSingle();
}

void loop(){
  Measurement m; // from MeasurementTypes.h
  if (tc.poll(m)) {
    if (m.status == MEAS_OK) Serial.printf("%.1f m\n", m.dist_m);
    else Serial.println("NO_SIGNAL");
  }
}
*/
