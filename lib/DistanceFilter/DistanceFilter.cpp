// DistanceFilter.cpp
#include "DistanceFilter.h"

/*---------Median----------*/
float DistanceFilter::median5_() const {
    float a[5];
    for (int i = 0; i < 5; ++i) {
        a[i] = buff_[i];
    }

    // insertion sort cho 5 phần tử
    for (int i = 1; i < 5; ++i) {
        float k = a[i];
        int   j = i - 1;
        while (j >= 0 && a[j] > k) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = k;
    }
    return a[2];
}

float DistanceFilter::update(float m, bool valid) {
    // Review#1: reset FSM signal moi lan update()
    lastGateRejected_ = false;
    lastReinit_       = false;

    //Nếu mẫu không hợp lệ: không thay đổi trạng thái, trả về giá trị hiện tại
    if (!valid) {
        return distEMA_m_;
    }
    // --------- Gating filter ----------
    if (!isnan(distEMA_m_)) {
        float delta = fabsf(m - distEMA_m_);

        if (delta > gateThreshold_) {
            //mẫu nhảy vọt
            ++rejectCount_;

            if (rejectCount_ < maxReject_) {
                //coi là nhiễu: bỏ qua, giữ giá trị cũ
                lastGateRejected_ = true;   // Review#1
                return distEMA_m_;
            } else {
                //lệch liên tiếp đủ maxReject mẫu -> coi là mục tiêu mới
                distEMA_m_    = m;
                rejectCount_  = 0;
                lastReinit_   = true;       // Review#1: target-switch
                // Review#RG1: chot semantic - rejectedByGate = "raw sample vuot
                // nguong gate" (khong phai "bi loai bo"). Sample reinit VAN vuot
                // gate nen flag = true. Field estimatorDecision (REINITIALIZED)
                // moi la field ket luan "co dung sample hay khong".
                lastGateRejected_ = true;
                //reset median buffer quanh giá trị mới
                for (int i = 0; i < 5; ++i) {
                    ((float*)buff_)[i] = m;
                }
                length_ = 5;
            }
        } else {
            //chênh lệch nhỏ -> reset bộ đếm lỗi
            rejectCount_ = 0;
        }
    }

    // --------- Median(5) ----------
    if (length_ < 5) {
        length_++;
    }
    buff_[idx_] = m;
    idx_        = (idx_ + 1) % 5;

    float med = (length_ < 5) ? m : median5_();

    /*--------- EMA ----------*/
    if (isnan(distEMA_m_)) {
        distEMA_m_ = med;
    } else {
        distEMA_m_ = alpha_ * med + (1.0f - alpha_) * distEMA_m_;
    }

    return distEMA_m_;
}
