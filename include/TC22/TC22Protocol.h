#pragma once
#include <Arduino.h>
#include <math.h>
#include "MeasurementTypes.h"

// ===== Reject reason =====
// Mã lỗi từ chối đo của TC22, dùng để phân loại lý do tại sao một phép đo bị từ chối sau khi đã parse khung TC22
enum Tc22RejectReason : uint8_t{
    TC22_REJECT_NONE = 0,
    TC22_REJECT_BAD_FRAME,
    TC22_REJECT_BAD_CRC,
    TC22_REJECT_DATA_INVALID,
    TC22_REJECT_BLIND_AREA,
    TC22_REJECT_OUT_OF_RANGE
};
// ===== Raw parsed frame =====
// Khung giao thức TC22
struct Tc22Frame{
    bool frameOk = false;
    bool crcOk = false;

    uint8_t msgType = 0;   // expect 0xFB
    uint8_t msgCode = 0;   // expect 0x03
    uint8_t boardId = 0;
    uint8_t payloadLen = 0;

    bool dataValid = false;
    uint16_t distanceDm = 0;

    uint32_t t_ms = 0;
};

// ===== Measurement sau gate =====
// Kết quả phép đo sau khi đã qua các bước kiểm tra và phân loại lỗi, sẵn sàng để sử dụng trong ứng dụng
struct Tc22Measurement{
    bool accepted = false;
    float rawDistanceM = NAN; //đã chuyển dm sang m
    MeasStatus status = MEAS_BAD_FRAME;
    Tc22RejectReason rejectReason = TC22_REJECT_BAD_FRAME;
    uint32_t t_ms = 0;
};