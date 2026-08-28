#include "TrackerCore_v2.h"
#include <math.h>

TrackerCore::TrackerCore(const TrackerConfig& cfg)
    : cfg_(cfg),
      // Review#3.2: bootstrap voi BaselineConfig{} default. Se bi
      // setBaselineConfig() overwrite lan dau apply preset.
      baselineFilter_(BaselineConfig{}.emaLambda,
                      BaselineConfig{}.gateThresholdM,
                      BaselineConfig{}.maxReject),
      abCfg_{},
      abTracker_(abCfg_){
    reset();
}

void TrackerCore::configure(const TrackerConfig& cfg){
    // Review#3.2: TrackerConfig chi con candidateHits/lostAfterInvalid.
    // KHONG cham baselineFilter_ / abTracker_ o day - dung API rieng.
    cfg_ = cfg;
}

void TrackerCore::setEstimatorMode(EstimatorMode mode){
    mode_ = mode;
    out_.estimatorMode = mode_;
    // Doi mode -> reset state ca hai estimator de A/B benchmark sach
    baselineFilter_.reset();
    abTracker_.reset();
    out_.hasEstimate = false;
    out_.filteredDistanceM = NAN;
    // Fix #4: reset state counter va track state khi doi mode.
    // Tranh TFT hien STABLE ngay sau khi cycle preset.
    out_.consecutiveValids   = 0;
    out_.consecutiveInvalids = 0;
    out_.trackState          = TRACK_SEARCHING;
    out_.estimatorDecision   = EST_DECISION_NONE;   // Proposal 1
    clearAlphaBetaFields();
}

void TrackerCore::setAlphaBetaConfig(const AlphaBetaConfig& cfg){
    abCfg_ = cfg;
    abTracker_.configure(abCfg_);
}

// Proposal 3: Config rieng cho baseline (Gating + Median + EMA)
void TrackerCore::setBaselineConfig(const BaselineConfig& cfg){
    baselineCfg_ = cfg;
    baselineFilter_.configure(cfg.emaLambda, cfg.gateThresholdM, cfg.maxReject);
    baselineFilter_.reset();
}

void TrackerCore::reset(){
    baselineFilter_.reset();
    abTracker_.reset();
    out_ = TrackerOutput{};
    out_.measStatus    = MEAS_TIMEOUT;
    out_.trackState    = TRACK_SEARCHING;
    out_.estimatorMode = mode_;
}

void TrackerCore::clearAlphaBetaFields(){
    out_.rangeRateMps       = NAN;
    out_.predictedDistanceM = NAN;
    out_.residualM          = NAN;
    out_.rejectedByGate     = false;
    out_.predictHoldCount   = 0;   // Chi co y nghia trong ALPHABETA mode
}

void TrackerCore::updateTrackStateOnValid(){
    if (out_.consecutiveValids >= cfg_.candidateHits){
        out_.trackState = TRACK_STABLE;
    } else if (out_.consecutiveValids > 0) {
        out_.trackState = TRACK_CANDIDATE;
    } else {
        out_.trackState = TRACK_SEARCHING;
    }
}

void TrackerCore::updateTrackStateOnInvalid(){
    if (out_.consecutiveInvalids >= cfg_.lostAfterInvalid){
        out_.trackState = TRACK_LOST;
    }
}

// Proposal 1: FSM su dung decision cua estimator, khong dua vao meas.status.
// Ban chat: mot mau MEAS_OK bi gate reject KHONG duoc coi la "hit hop le"
// cua tracker (tranh tang STABLE gia trong khi gate lien tuc chan).
void TrackerCore::applyDecisionToFsm(EstimatorDecision decision){
    switch (decision){
        case EST_DECISION_ACCEPTED:
        case EST_DECISION_INITIALIZED:     // Review#8: first-lock
        case EST_DECISION_REINITIALIZED:   // target-switch
            // Review#7: saturating increment (uint16_t), khong roll-over
            if (out_.consecutiveValids < UINT16_MAX) out_.consecutiveValids++;
            out_.consecutiveInvalids = 0;
            // Review#TC1: chi update lastGoodTimeMs khi estimator accept.
            // Truoc day update tren moi MEAS_OK -> gate reject van coi la "good".
            out_.lastGoodTimeMs = out_.sampleTimeMs;
            updateTrackStateOnValid();
            break;

        case EST_DECISION_REJECTED:
        case EST_DECISION_PREDICT_ONLY:
            if (out_.consecutiveInvalids < UINT16_MAX) out_.consecutiveInvalids++;
            out_.consecutiveValids = 0;
            updateTrackStateOnInvalid();
            break;

        case EST_DECISION_NONE:
        default:
            // Khong co estimate va cung khong coasting -> chi tang invalid
            // neu sensor cung khong OK (giu tuong thich voi hanh vi cu).
            if (out_.measStatus != MEAS_OK){
                // Review#7: saturating increment
                if (out_.consecutiveInvalids < UINT16_MAX) out_.consecutiveInvalids++;
                out_.consecutiveValids = 0;
                updateTrackStateOnInvalid();
            }
            break;
    }
}

void TrackerCore::updateMeasurement(const Measurement& m, float fps){
    out_.fps           = fps;
    out_.measStatus    = m.status;
    out_.sampleTimeMs  = m.t_ms;
    out_.estimatorMode = mode_;

    // Proposal 1: tach tinh hop le cam bien khoi quyet dinh estimator.
    // Buoc 1 - chay estimator, lay decision.
    EstimatorDecision decision = EST_DECISION_NONE;

    if (m.status == MEAS_OK){
        out_.rawDistanceM   = m.dist_m;
        // Review#TC1: KHONG update lastGoodTimeMs o day - se update SAU KHI
        // biet estimator decision. Firmware truoc day update ngay tren moi
        // MEAS_OK, ke ca khi α-β/baseline gate reject -> lech voi Python
        // (Python chi update khi accept).

        switch (mode_){
            case EST_BASELINE: {
                // Review#8: kiem tra state truoc khi update() de phan biet
                // first-lock (INITIALIZED) vs target-switch (REINITIALIZED)
                const bool wasCold = !baselineFilter_.hasValue();
                baselineFilter_.update(m.dist_m, true);
                const float est = baselineFilter_.value();
                out_.filteredDistanceM = est;
                out_.hasEstimate       = isfinite(est);
                clearAlphaBetaFields();
                // Review#1: SET SAU clearAlphaBetaFields() vi ham nay reset rejectedByGate.
                out_.rejectedByGate = baselineFilter_.lastGateRejected();
                if (!isfinite(est)) {
                    decision = EST_DECISION_NONE;
                } else if (wasCold) {
                    decision = EST_DECISION_INITIALIZED;   // Review#8
                } else if (baselineFilter_.lastReinit()) {
                    decision = EST_DECISION_REINITIALIZED;
                } else if (baselineFilter_.lastGateRejected()) {
                    // Predict-only: giu EMA cu, khong update
                    decision = EST_DECISION_REJECTED;
                } else {
                    decision = EST_DECISION_ACCEPTED;
                }
                break;
            }
            case EST_ALPHABETA: {
                AlphaBetaOutput ab = abTracker_.update(m, fps);
                out_.filteredDistanceM   = ab.estimateM;
                out_.rangeRateMps        = ab.rateMps;
                out_.predictedDistanceM  = ab.predictedM;
                out_.residualM           = ab.residualM;
                out_.rejectedByGate      = ab.rejectedByGate;
                out_.hasEstimate         = ab.hasEstimate;
                out_.predictHoldCount    = ab.predictHoldCount;
                decision                 = ab.decision;
                break;
            }
            case EST_RAW_ONLY:
            default: {
                out_.filteredDistanceM = m.dist_m;
                out_.hasEstimate       = isfinite(m.dist_m);
                clearAlphaBetaFields();
                decision = isfinite(m.dist_m) ? EST_DECISION_ACCEPTED
                                              : EST_DECISION_NONE;
                break;
            }
        }
    }else{
        out_.rawDistanceM = NAN;

        switch (mode_){
            case EST_BASELINE: {
                out_.filteredDistanceM = baselineFilter_.value();
                out_.hasEstimate       = isfinite(out_.filteredDistanceM);
                clearAlphaBetaFields();
                decision = out_.hasEstimate ? EST_DECISION_PREDICT_ONLY
                                            : EST_DECISION_NONE;
                break;
            }
            case EST_ALPHABETA: {
                AlphaBetaOutput ab = abTracker_.notifyInvalid(m.t_ms, fps);
                out_.filteredDistanceM  = ab.estimateM;
                out_.rangeRateMps       = ab.rateMps;
                out_.predictedDistanceM = ab.predictedM;
                out_.residualM          = ab.residualM;
                out_.rejectedByGate     = ab.rejectedByGate;
                out_.hasEstimate        = ab.hasEstimate;
                out_.predictHoldCount   = ab.predictHoldCount;
                decision                = ab.decision;
                break;
            }
            case EST_RAW_ONLY:
            default: {
                out_.filteredDistanceM = NAN;
                out_.hasEstimate       = false;
                clearAlphaBetaFields();
                decision = EST_DECISION_NONE;
                break;
            }
        }
    }

    out_.estimatorDecision = decision;
    applyDecisionToFsm(decision);
}

void TrackerCore::notifyNoFrame(uint32_t nowMs){
    out_.measStatus      = MEAS_TIMEOUT;
    out_.sampleTimeMs    = nowMs;
    out_.rawDistanceM    = NAN;
    out_.estimatorMode   = mode_;

    EstimatorDecision decision = EST_DECISION_NONE;

    switch (mode_){
        case EST_BASELINE: {
            out_.filteredDistanceM = baselineFilter_.value();
            out_.hasEstimate       = isfinite(out_.filteredDistanceM);
            clearAlphaBetaFields();
            decision = out_.hasEstimate ? EST_DECISION_PREDICT_ONLY
                                        : EST_DECISION_NONE;
            break;
        }
        case EST_ALPHABETA: {
            AlphaBetaOutput ab = abTracker_.notifyInvalid(nowMs, out_.fps);
            out_.filteredDistanceM  = ab.estimateM;
            out_.rangeRateMps       = ab.rateMps;
            out_.predictedDistanceM = ab.predictedM;
            out_.residualM          = ab.residualM;
            out_.rejectedByGate     = ab.rejectedByGate;
            out_.hasEstimate        = ab.hasEstimate;
            out_.predictHoldCount   = ab.predictHoldCount;
            decision                = ab.decision;
            break;
        }
        case EST_RAW_ONLY:
        default: {
            out_.filteredDistanceM = NAN;
            out_.hasEstimate       = false;
            clearAlphaBetaFields();
            decision = EST_DECISION_NONE;
            break;
        }
    }
    out_.estimatorDecision = decision;
    applyDecisionToFsm(decision);
}
