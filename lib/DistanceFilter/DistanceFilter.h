// DistanceFilter.h
#pragma once
#include <Arduino.h>

/**
 * DistanceFilter
 *  - Lọc median cửa sổ 5 mẫu
 *  - Làm mượt bằng EMA (alpha)
 *  - Gating filter: chặn các mẫu nhảy vọt bất thường,
 *    chỉ chấp nhận là "mục tiêu mới" khi lệch liên tiếp maxReject mẫu
 */

class DistanceFilter {
public:
    DistanceFilter() = default;

    DistanceFilter(float alpha, float gateThreshold, uint8_t maxReject) {
        configure(alpha, gateThreshold, maxReject);
    }

    // Cấu hình nhanh
    void configure(float alpha, float gateThreshold, uint8_t maxReject) {
        setAlpha(alpha);
        gateThreshold_ = gateThreshold;
        maxReject_     = maxReject;
    }

    void setAlpha(float a) {
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        alpha_ = a;
    }

    void setGateThreshold(float g) { gateThreshold_ = g; }
    void setMaxReject(uint8_t r)   { maxReject_ = r; }

    // Xoá trạng thái bộ lọc (ví dụ khi đổi mode / reset hệ thống)
    void reset() {
        distEMA_m_        = NAN;
        length_           = 0;
        idx_              = 0;
        rejectCount_      = 0;
        lastGateRejected_ = false;
        lastReinit_       = false;
    }

    // Cập nhật với 1 mẫu mới.
    // - m: khoảng cách đo (m)
    // - valid: mẫu hợp lệ hay không
    // Trả về giá trị sau lọc (m). Nếu chưa có giá trị hợp lệ thì trả về NAN.
    float update(float m, bool valid);

    // Giá trị đã lọc hiện tại
    float value() const { return distEMA_m_; }

    // Đã có giá trị hợp lệ chưa?
    bool hasValue() const { return !isnan(distEMA_m_); }

    // Review#1: FSM ngoai can biet mau vua update co bi gate reject
    // (dropped/held) hay khong. Reset sau moi update() ke tiep.
    bool lastGateRejected() const { return lastGateRejected_; }
    // True khi update() reinit (target-switch sau maxReject lien tiep).
    bool lastReinit() const { return lastReinit_; }

private:
    // Tham số
    float   alpha_         = 0.25f;   // hệ số EMA
    float   gateThreshold_ = 10.0f;   // ngưỡng gating (m)
    uint8_t maxReject_     = 5;       // số mẫu bị chặn trước khi chấp nhận mục tiêu mới

    // Trạng thái nội bộ
    float   distEMA_m_ = NAN;         // giá trị sau EMA
    float   buff_[5];                 // buffer cho median
    uint8_t length_      = 0;         // số phần tử hợp lệ hiện có (<=5)
    uint8_t idx_         = 0;         // index vòng tròn
    uint8_t rejectCount_ = 0;         // bộ đếm gating

    // Review#1: expose FSM signal cho TrackerCore
    bool    lastGateRejected_ = false; // update() gan nhat da bi gate chan?
    bool    lastReinit_       = false; // update() gan nhat da reinit target-switch?

    float median5_() const;
};
