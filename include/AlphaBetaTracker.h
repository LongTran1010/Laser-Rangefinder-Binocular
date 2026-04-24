#pragma once
#include <Arduino.h>
#include <math.h>
#include "MeasurementTypes.h"

struct AlphaBetaConfig{
    float alpha = 0.35f;
    float beta = 0.00f;
    float gateThresholdM = 8.0f;
    uint8_t maxReject = 5;

    bool reinitOnSwitch = true;

    // dùng khi không suy ra dt hợp lệ từ timestamp/fps
    float defaultDtS = 0.40f;

    // chống dt bất thường
    float minDtS = 0.001f;
    float maxDtS = 2.0f;

    // rate ban đầu khi khởi tạo / reinit
    float initRateMps = 0.0f;
};

struct AlphaBetaOutput{
    bool hasEstimate = false;

    float estimateM = NAN;     // x sau update / hoặc state hiện tại
    float rateMps = NAN;       // v hiện tại
    float predictedM = NAN;    // x_pred
    float residualM = NAN;     // z - x_pred
    bool rejectedByGate = false;

    uint8_t rejectCount = 0;
    uint32_t sampleTimeMs = 0;
};

class AlphaBetaTracker{
public:
    explicit AlphaBetaTracker(const AlphaBetaConfig& cfg = AlphaBetaConfig{});

    void configure(const AlphaBetaConfig& cfg);
    const AlphaBetaConfig& config() const { return cfg_; }

    void reset();

    // wrapper tiện dùng trực tiếp với Measurement
    AlphaBetaOutput update(const Measurement& m, float fps = 0.0f);

    // dùng khi bạn đã tách measurement ra ngoài
    AlphaBetaOutput updateValidMeasurement(float zM, uint32_t sampleTimeMs, float fps = 0.0f);

    // gọi khi có frame lỗi / timeout / no-signal
    AlphaBetaOutput notifyInvalid(uint32_t sampleTimeMs, float fps = 0.0f);

    const AlphaBetaOutput& output() const{ return out_; }

    bool hasEstimate() const{ return hasState_; }
    float value() const{ return hasState_ ? x_ : NAN; }
    float rate() const{ return hasState_ ? v_ : NAN; }

private:
    float resolveDtS(uint32_t sampleTimeMs, float fps) const;
    void initializeState(float zM, uint32_t sampleTimeMs);
    void fillOutput(float predictedM, float residualM, bool rejected, uint32_t sampleTimeMs);

private:
    AlphaBetaConfig cfg_{};
    AlphaBetaOutput out_{};

    bool hasState_ = false;
    float x_ = NAN;
    float v_ = 0.0f;
    uint32_t lastSampleMs_ = 0;
    uint8_t rejectCount_ = 0;
};