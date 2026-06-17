#pragma once
#include <Arduino.h>
#include "TC22/Tc22Protocol.h"
//Máy trạng thái byte-by-byte để parse khung TC22 từ UART
class Tc22Parser{
public:
    static const size_t FRAME_LEN = 9;

    Tc22Parser();
    // feed từng byte từ UART
    bool feedByte(uint8_t b);
    // có frame hoàn chỉnh chưa
    bool hasFrame() const;
    // lấy frame ra (reset flag)
    Tc22Frame popFrame();
    void reset();
    // giải mã payload theo little-endian
    void setLittleEndianPayload(bool le);
private:
    uint8_t buf_[FRAME_LEN]{};
    size_t idx_ = 0;
    bool frameReady_ = false;
    Tc22Frame lastFrame_{};
    bool littleEndian_ = true;

    uint8_t calcCrc(const uint8_t* data, size_t n) const;
    bool tryParseCurrentBuffer();
    void shiftWindow();
};