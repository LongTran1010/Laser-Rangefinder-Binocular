#include "TC22/TC22ValidityGate.h"

//======= Tầng kiểm tra tính hợp lệ của phép đo TC22 sau khi đã parse khung =======
Tc22Measurement Tc22ValidityGate::evaluate(const Tc22Frame& f){
    Tc22Measurement m{};
    m.t_ms = f.t_ms;
    // ===== Frame lỗi =====
    if (!f.frameOk){
        m.accepted = false;
        m.status = MEAS_BAD_FRAME;
        m.rejectReason = TC22_REJECT_BAD_FRAME;
        return m;
    }
    // ===== CRC lỗi =====
    if (!f.crcOk){
        m.accepted = false;
        m.status = MEAS_BAD_CRC;
        m.rejectReason = TC22_REJECT_BAD_CRC;
        return m;
    }
    // ===== Data invalid =====
    if (!f.dataValid){
        m.accepted = false;
        m.status = MEAS_NO_SIGNAL;
        m.rejectReason = TC22_REJECT_DATA_INVALID;
        return m;
    }
    // ===== Convert dm → m =====
    const float distM = static_cast<float>(f.distanceDm) * 0.1f;
    m.rawDistanceM = distM;
    // ===== Blind area =====
    if (distM < cfg_.blindAreaM){
        m.accepted = false;
        m.status = MEAS_NO_SIGNAL;
        m.rejectReason = TC22_REJECT_BLIND_AREA;
        return m;
    }
    // ===== Max range =====
    if (distM > cfg_.maxRangeM){
        m.accepted = false;
        m.status = MEAS_NO_SIGNAL;
        m.rejectReason = TC22_REJECT_OUT_OF_RANGE;
        return m;
    }
    // ===== OK =====
    m.accepted = true;
    m.status = MEAS_OK;
    m.rejectReason = TC22_REJECT_NONE;

    return m;
}