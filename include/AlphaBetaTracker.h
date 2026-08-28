#pragma once
#include <Arduino.h>
#include <math.h>
#include "MeasurementTypes.h"
#include "TrackerTypes_v2.h"    // Proposal 1: EstimatorDecision

// =====================================================================
// AlphaBetaTracker
// ---------------------------------------------------------------------
// Bo loc alpha-beta cho mo hinh constant-velocity 1D (range, range-rate).
//
//   Predict:    x_pred = x + v * dt
//               v_pred = v
//   Innovation: r      = z - x_pred
//   Gate:       |r| <= gateThreshold  -> accept; nguoc lai -> reject
//   Update:     x = x_pred + alpha * r
//               v = v_pred + (beta / dt) * r          (chi khi beta > 0)
//
// Hanh vi dac biet:
//  - Khi gate reject: van predict-only x = x_pred, v = v_pred, KHONG drop
//    state. Dong nhat voi phien ban Python de tuning offline co gia tri.
//  - Sau maxReject lan reject lien tiep + reinitOnSwitch: coi la doi muc
//    tieu, reinit x = z, v = 0.
//  - Khi notifyInvalid(): nhan v voi invalidVelocityDecay; sau
//    maxPredictHoldSamples lan lien tiep invalid -> v = 0, freeze x.
// =====================================================================

struct AlphaBetaConfig{
    // ---- tham so chinh ----
    float alpha = 0.30f;
    float beta  = 0.0529f;        // ~ Benedict-Bordner cho alpha=0.30
    float gateThresholdM = 10.0f;
    uint8_t maxReject = 5;

    bool reinitOnSwitch = true;

    // ---- xu ly dt ----
    float defaultDtS = 0.40f;
    float minDtS = 0.10f;   // Truoc: 0.001f. Sua de tranh velocity spike khi
                            // 2 frame UART den qua gan nhau (drain artifact).
                            // TC22 max ~10fps -> dt >= 0.10s thuc te.
    float maxDtS = 2.0f;

    // ---- khoi tao ----
    float initRateMps = 0.0f;

    // ---- xu ly mau invalid (B4) ----
    float   invalidVelocityDecay  = 0.85f;
    uint8_t maxPredictHoldSamples = 8;
};

struct AlphaBetaOutput{
    bool hasEstimate = false;

    float estimateM   = NAN;
    float rateMps     = NAN;
    float predictedM  = NAN;
    float residualM   = NAN;
    bool  rejectedByGate = false;

    uint8_t rejectCount = 0;
    uint8_t predictHoldCount = 0;
    uint32_t sampleTimeMs = 0;

    // Proposal 1: quyet dinh cua tracker cho mau nay
    EstimatorDecision decision = EST_DECISION_NONE;
};

class AlphaBetaTracker{
public:
    explicit AlphaBetaTracker(const AlphaBetaConfig& cfg = AlphaBetaConfig{});

    void configure(const AlphaBetaConfig& cfg);
    const AlphaBetaConfig& config() const { return cfg_; }

    void reset();

    AlphaBetaOutput update(const Measurement& m, float fps = 0.0f);
    AlphaBetaOutput updateValidMeasurement(float zM, uint32_t sampleTimeMs, float fps = 0.0f);
    AlphaBetaOutput notifyInvalid(uint32_t sampleTimeMs, float fps = 0.0f);

    const AlphaBetaOutput& output() const{ return out_; }

    bool  hasEstimate() const{ return hasState_; }
    float value()       const{ return hasState_ ? x_ : NAN; }
    float rate()        const{ return hasState_ ? v_ : NAN; }

private:
    float resolveDtS(uint32_t sampleTimeMs, float fps) const;
    // Review#8: caller chi dinh INITIALIZED (first lock) hoac REINITIALIZED (target-switch)
    void  initializeState(float zM, uint32_t sampleTimeMs,
                          EstimatorDecision decision = EST_DECISION_INITIALIZED);
    // Proposal 1: them decision vao fillOutput
    void  fillOutput(float predictedM, float residualM, bool rejected,
                     uint32_t sampleTimeMs, EstimatorDecision decision);

private:
    AlphaBetaConfig cfg_{};
    AlphaBetaOutput out_{};

    bool     hasState_ = false;
    float    x_ = NAN;
    float    v_ = 0.0f;
    uint32_t lastSampleMs_   = 0;
    uint8_t  rejectCount_    = 0;
    uint8_t  predictHoldCnt_ = 0;
};
