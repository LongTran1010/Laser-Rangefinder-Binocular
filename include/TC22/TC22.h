//sensor/driver/parser
#pragma once
#include <Arduino.h>
#include "MeasurementTypes.h"
#include "TC22/Tc22Protocol.h"
#include "TC22/Tc22Parser.h"
#include "TC22/Tc22ValidityGate.h"


// TC22 UART driver
//  - UART 115200 8N1, TTL 3.3V.
//  - Start/Stop command: MsgType=0xFA, MsgCode=0x01, PayLoadLen=0x04,
//      MeaType (1=start, 0=stop), MeaTimes (0=infinite, 1=single).
//  - Measurement report: [FB 03 BrdId 04 DataValid_L DataValid_H Dist_L Dist_H CRC]
//      Distance unit is dm (decimetre). Convert to metres: m = dist_dm / 10.0f
//  - CRC = (sum of first N-1 bytes) & 0xFF


class TC22Driver{
public:
  TC22Driver(HardwareSerial& port = Serial2, int rxPin = 17, int txPin = 16)
  : port_(port), rx_(rxPin), tx_(txPin){}

  bool begin(uint32_t baud = 115200){
    port_.begin(baud, SERIAL_8N1, rx_, tx_);
    parser_.reset();
    parser_.setLittleEndianPayload(littleEndian_);
    Tc22ValidityGate::Config cfg;
    cfg.blindAreaM = blindAreaM_;
    cfg.maxRangeM = maxRangeM_;
    gate_.configure(cfg);
    return true;
  }

  void setTimeoutMs(uint32_t ms) { timeoutMs_ = ms; }

  void setBoardId(uint8_t id) { brdId_ = id; }

  void setLittleEndianPayload(bool le){
    littleEndian_ = le;
    parser_.setLittleEndianPayload(le);
  }

  void setMaxRangeM(float m){
    maxRangeM_ = m;
    Tc22ValidityGate::Config cfg;
    cfg.blindAreaM = blindAreaM_;
    cfg.maxRangeM = maxRangeM_;
    gate_.configure(cfg);
  }

  Tc22RejectReason lastRejectReason() const { return lastRejectReason_; }

  void startContinuous() { sendStartStop(true, 0); }
  void startSingle()     { sendStartStop(true, 1); }
  void stop()            { sendStartStop(false, 0); }

  // Non-blocking; call often in loop().
  // Returns true khi vừa parse được 1 frame đo (dù hợp lệ hay không hợp lệ).
  bool poll(Measurement& out){
    uint32_t now = millis();

    while(port_.available()){
      uint8_t b = static_cast<uint8_t>(port_.read());
      lastRxByteMs_ = now;

      if(parser_.feedByte(b) && parser_.hasFrame()){
        Tc22Frame f = parser_.popFrame();
        Tc22Measurement tm = gate_.evaluate(f);

        fillMeasurementFromGate(tm, out);
        lastRejectReason_ = tm.rejectReason;
        return true;
      }
    }

    // Nếu đang nhận dở quá lâu thì reset parser.
    // Không phát timeout sample ở đây để tránh spam.
    if((millis() - lastRxByteMs_) > timeoutMs_){
      parser_.reset();
    }

    return false;
  }

private:
  void fillMeasurementFromGate(const Tc22Measurement& tm, Measurement& out){
    out = Measurement{};  // clear toàn bộ field cũ
    out.t_ms = tm.t_ms;
    out.dist_m = tm.rawDistanceM;
    out.status = tm.status;

    // các field cũ không có ở TC22
    out.strength = 0;
    out.temp_C = NAN;
    out.slantRange = NAN;
    out.horzDist = NAN;
    out.vertDist = NAN;
    out.speedKmh = NAN;
    out.pitchAngle = NAN;

    // tạm thời dùng confidence kiểu đơn giản
    out.confidence = tm.accepted ? 100 : 0;
  }

  void sendStartStop(bool start, uint16_t times){
    uint8_t pkt[9];
    pkt[0] = 0xFA;    // MsgType
    pkt[1] = 0x01;    // MsgCode: Start/Stop measure
    pkt[2] = brdId_;  // BrdId
    pkt[3] = 0x04;    // PayLoadLen

    // MeaType (LE)
    pkt[4] = static_cast<uint8_t>((start ? 1 : 0) & 0xFF);
    pkt[5] = 0x00;

    // MeaTimes (LE)
    pkt[6] = static_cast<uint8_t>(times & 0xFF);
    pkt[7] = static_cast<uint8_t>((times >> 8) & 0xFF);

    pkt[8] = sum8(pkt, 8);
    port_.write(pkt, sizeof(pkt));
  }

  static uint8_t sum8(const uint8_t* b, size_t n){
    uint32_t s = 0;
    for(size_t i = 0; i < n; ++i) s += b[i];
    return static_cast<uint8_t>(s & 0xFF);
  }

private:
  HardwareSerial& port_;
  int rx_, tx_;

  uint8_t brdId_ = 0xFF;
  uint32_t timeoutMs_ = 120;
  uint32_t lastRxByteMs_ = 0;

  bool littleEndian_ = true;
  float maxRangeM_ = 1000.0f;
  float blindAreaM_ = 3.0f;

  Tc22Parser parser_;
  Tc22ValidityGate gate_;
  Tc22RejectReason lastRejectReason_ = TC22_REJECT_NONE;
};
