#include "TC22/TC22Parser.h"

Tc22Parser::Tc22Parser() = default;

void Tc22Parser::reset(){
  idx_ = 0;
  frameReady_ = false;
  lastFrame_ = Tc22Frame{};
}

void Tc22Parser::setLittleEndianPayload(bool le){
  littleEndian_ = le;
}

bool Tc22Parser::feedByte(uint8_t b){
    if(idx_ < FRAME_LEN){
        buf_[idx_++] = b;
    }else{
        shiftWindow();
        buf_[FRAME_LEN - 1] = b;
    }

    if(idx_ < FRAME_LEN){
        return false;
    }

    if(tryParseCurrentBuffer()){
        return true;
    }

    shiftWindow();
    idx_ = FRAME_LEN - 1;
    return false;
}

bool Tc22Parser::hasFrame() const{
    return frameReady_;
}

Tc22Frame Tc22Parser::popFrame(){
    frameReady_ = false;
    return lastFrame_;
}

uint8_t Tc22Parser::calcCrc(const uint8_t* data, size_t n) const{
    uint32_t sum = 0;
    for(size_t i = 0; i < n; ++i){
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFFu);
}

void Tc22Parser::shiftWindow(){
  for(size_t i = 1; i < FRAME_LEN; ++i){
    buf_[i - 1] = buf_[i];
  }
}


bool Tc22Parser::tryParseCurrentBuffer(){
  // Khung đo chuẩn của TC22:
  // [0] MsgType   = 0xFB
  // [1] MsgCode   = 0x03
  // [2] BrdId
  // [3] PayLoadLen= 0x04
  // [4..5] DataValidInd (LE)
  // [6..7] Distance dm   (LE)
  // [8] CRC
    if(buf_[0] != 0xFB || buf_[1] != 0x03){
    return false;
    }
    Tc22Frame f{};
    //===== Parse theo TC22 =====
    f.frameOk = true;
    f.msgType = buf_[0];
    f.msgCode = buf_[1];
    f.boardId = buf_[2];
    f.payloadLen = buf_[3];
    f.t_ms = millis();
    //payloadLen hợp lệ của measurement report là 0x04
    if(f.payloadLen != 0x04){
        f.crcOk = false;
        lastFrame_ = f;
        frameReady_ = true;
        return true;
    }
    // CRC
    const uint8_t crcRx = buf_[8];
    const uint8_t crcCalc = calcCrc(buf_, 8);
    f.crcOk = (crcRx == crcCalc);

    uint16_t validWord = 0;
    uint16_t distDm = 0;

    if(littleEndian_){
        validWord = static_cast<uint16_t>(buf_[4]) |
                    (static_cast<uint16_t>(buf_[5]) << 8);
        distDm = static_cast<uint16_t>(buf_[6]) |
                (static_cast<uint16_t>(buf_[7]) << 8);
    }else{
        validWord = static_cast<uint16_t>(buf_[5]) |
                    (static_cast<uint16_t>(buf_[4]) << 8);
        distDm = static_cast<uint16_t>(buf_[7]) |
                (static_cast<uint16_t>(buf_[6]) << 8);
    }

    f.dataValid = (validWord == 1u);
    f.distanceDm = distDm;

    lastFrame_ = f;
    frameReady_ = true;
    return true;
}