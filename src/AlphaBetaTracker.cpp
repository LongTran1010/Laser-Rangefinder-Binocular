#include "AlphaBetaTracker.h"

AlphaBetaTracker::AlphaBetaTracker(const AlphaBetaConfig& cfg)
    : cfg_(cfg){
    reset();
}

void AlphaBetaTracker::configure(const AlphaBetaConfig& cfg){
    cfg_ = cfg;
    reset();
}

void AlphaBetaTracker::reset(){
    hasState_ = false;
    x_ = NAN;
    v_ = cfg_.initRateMps;
    lastSampleMs_ = 0;
    rejectCount_ = 0;
    out_ = AlphaBetaOutput{};
}

float AlphaBetaTracker::resolveDtS(uint32_t sampleTimeMs, float fps) const{
    float dtS = cfg_.defaultDtS;

    if(lastSampleMs_ != 0 && sampleTimeMs > lastSampleMs_){
        dtS = (sampleTimeMs - lastSampleMs_) / 1000.0f;
    } else if (fps > 0.01f) {
        dtS = 1.0f / fps;
    }

    if(!isfinite(dtS) || dtS < cfg_.minDtS || dtS > cfg_.maxDtS){
        dtS = cfg_.defaultDtS;
    }

    return dtS;
}

void AlphaBetaTracker::initializeState(float zM, uint32_t sampleTimeMs){
    hasState_ = true;
    x_ = zM;
    v_ = cfg_.initRateMps;
    lastSampleMs_ = sampleTimeMs;
    rejectCount_ = 0;

    out_.hasEstimate = true;
    out_.estimateM = x_;
    out_.rateMps = v_;
    out_.predictedM = zM;
    out_.residualM = 0.0f;
    out_.rejectedByGate = false;
    out_.rejectCount = rejectCount_;
    out_.sampleTimeMs = sampleTimeMs;
}

void AlphaBetaTracker::fillOutput(float predictedM, float residualM, bool rejected, uint32_t sampleTimeMs){
    out_.hasEstimate = hasState_;
    out_.estimateM = hasState_ ? x_ : NAN;
    out_.rateMps = hasState_ ? v_ : NAN;
    out_.predictedM = predictedM;
    out_.residualM = residualM;
    out_.rejectedByGate = rejected;
    out_.rejectCount = rejectCount_;
    out_.sampleTimeMs = sampleTimeMs;
}

AlphaBetaOutput AlphaBetaTracker::update(const Measurement& m, float fps){
    if (m.status == MEAS_OK){
        return updateValidMeasurement(m.dist_m, m.t_ms, fps);
    }
    return notifyInvalid(m.t_ms, fps);
}

AlphaBetaOutput AlphaBetaTracker::updateValidMeasurement(float zM, uint32_t sampleTimeMs, float fps){
    if (!isfinite(zM)){
        return notifyInvalid(sampleTimeMs, fps);
    }

    // lần đầu có mẫu hợp lệ -> khởi tạo state
    if (!hasState_){
        initializeState(zM, sampleTimeMs);
        return out_;
    }

    const float dtS = resolveDtS(sampleTimeMs, fps);

    // predict
    const float xPred = x_ + v_ * dtS;
    const float vPred = v_;
    const float residual = zM - xPred;

    // gate
    if (fabsf(residual) > cfg_.gateThresholdM){
        rejectCount_++;

        // nếu cho phép reinit khi coi như đã đổi mục tiêu
        if (cfg_.reinitOnSwitch && rejectCount_ >= cfg_.maxReject) {
            x_ = zM;
            v_ = cfg_.initRateMps;
            lastSampleMs_ = sampleTimeMs;
            rejectCount_ = 0;

            fillOutput(zM, 0.0f, false, sampleTimeMs);
            return out_;
        }

        // reject measurement: giữ state cũ, nhưng vẫn đẩy predicted/residual ra ngoài
        // chủ động KHÔNG update x_, v_
        lastSampleMs_ = sampleTimeMs;
        fillOutput(xPred, residual, true, sampleTimeMs);
        return out_;
    }

    // accepted update
    rejectCount_ = 0;

    x_ = xPred + cfg_.alpha * residual;

    // nếu beta = 0 thì tracker trở thành vị trí-thuần
    if(cfg_.beta > 0.0f && dtS > cfg_.minDtS){
        v_ = vPred + (cfg_.beta / dtS) * residual;
    }else{
        v_ = vPred;
    }

    lastSampleMs_ = sampleTimeMs;

    fillOutput(xPred, residual, false, sampleTimeMs);
    return out_;
}

AlphaBetaOutput AlphaBetaTracker::notifyInvalid(uint32_t sampleTimeMs, float fps){
    if (!hasState_) {
        out_ = AlphaBetaOutput{};
        out_.sampleTimeMs = sampleTimeMs;
        return out_;
    }

    const float dtS = resolveDtS(sampleTimeMs, fps);

    // với invalid frame, chỉ predict tiếp state hiện tại
    const float xPred = x_ + v_ * dtS;
    const float vPred = v_;

    x_ = xPred;
    v_ = vPred;
    lastSampleMs_ = sampleTimeMs;

    fillOutput(xPred, NAN, false, sampleTimeMs);
    return out_;
}