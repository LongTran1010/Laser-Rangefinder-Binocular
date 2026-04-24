#include "TrackerCore_v2.h"
#include <math.h>

TrackerCore::TrackerCore(const TrackerConfig& cfg)
    : cfg_(cfg),
      baselineFilter_(cfg.alpha, cfg.gateThresholdM, cfg.maxReject),
      abCfg_{},
      abTracker_(abCfg_){
    reset();
}

void TrackerCore::configure(const TrackerConfig& cfg){
    cfg_ = cfg;

    // cập nhật baseline filter
    baselineFilter_ = DistanceFilter(cfg.alpha, cfg.gateThresholdM, cfg.maxReject);

    // đồng bộ gate cho alpha-beta nếu muốn dùng chung threshold
    abCfg_.gateThresholdM = cfg.gateThresholdM;
    abTracker_.configure(abCfg_);
}

void TrackerCore::setEstimatorMode(EstimatorMode mode){
    mode_ = mode;
    out_.estimatorMode = mode_;
}

void TrackerCore::setAlphaBetaConfig(const AlphaBetaConfig& cfg){
    abCfg_ = cfg;
    abTracker_.configure(abCfg_);
}

void TrackerCore::reset(){
    baselineFilter_.reset();
    abTracker_.reset();

    out_ = TrackerOutput{};
    out_.measStatus = MEAS_TIMEOUT;
    out_.trackState = TRACK_SEARCHING;
    out_.estimatorMode = mode_;
}

void TrackerCore::clearAlphaBetaFields(){
    out_.rangeRateMps = NAN;
    out_.predictedDistanceM = NAN;
    out_.residualM = NAN;
    out_.rejectedByGate = false;
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

void TrackerCore::updateMeasurement(const Measurement& m, float fps){
    out_.fps = fps;
    out_.measStatus = m.status;
    out_.sampleTimeMs = m.t_ms;
    out_.estimatorMode = mode_;

    if (m.status == MEAS_OK){
        out_.rawDistanceM = m.dist_m;
        out_.lastGoodTimeMs = m.t_ms;
        out_.consecutiveValids++;
        out_.consecutiveInvalids = 0;

        if (mode_ == EST_BASELINE){
            baselineFilter_.update(m.dist_m, true);
            const float est = baselineFilter_.value();
            out_.filteredDistanceM = est;
            out_.hasEstimate = isfinite(est);
            clearAlphaBetaFields();
        }else{
            AlphaBetaOutput ab = abTracker_.update(m, fps);
            out_.filteredDistanceM = ab.estimateM;
            out_.rangeRateMps = ab.rateMps;
            out_.predictedDistanceM = ab.predictedM;
            out_.residualM = ab.residualM;
            out_.rejectedByGate = ab.rejectedByGate;
            out_.hasEstimate = ab.hasEstimate;
        }

        updateTrackStateOnValid();
    }else{
        out_.rawDistanceM = NAN;
        out_.consecutiveInvalids++;
        out_.consecutiveValids = 0;

        if (mode_ == EST_BASELINE){
            out_.filteredDistanceM = baselineFilter_.value();
            out_.hasEstimate = isfinite(out_.filteredDistanceM);
            clearAlphaBetaFields();
        }else{
            AlphaBetaOutput ab = abTracker_.notifyInvalid(m.t_ms, fps);
            out_.filteredDistanceM = ab.estimateM;
            out_.rangeRateMps = ab.rateMps;
            out_.predictedDistanceM = ab.predictedM;
            out_.residualM = ab.residualM;
            out_.rejectedByGate = ab.rejectedByGate;
            out_.hasEstimate = ab.hasEstimate;
        }

        updateTrackStateOnInvalid();
    }
}

void TrackerCore::notifyNoFrame(uint32_t nowMs){
    out_.measStatus = MEAS_TIMEOUT;
    out_.sampleTimeMs = nowMs;
    out_.rawDistanceM = NAN;
    out_.consecutiveInvalids++;
    out_.consecutiveValids = 0;
    out_.estimatorMode = mode_;
    
    if (mode_ == EST_BASELINE){
        out_.filteredDistanceM = baselineFilter_.value();
        out_.hasEstimate = isfinite(out_.filteredDistanceM);
        clearAlphaBetaFields();
    } else {
        AlphaBetaOutput ab = abTracker_.notifyInvalid(nowMs, out_.fps);
        out_.filteredDistanceM = ab.estimateM;
        out_.rangeRateMps = ab.rateMps;
        out_.predictedDistanceM = ab.predictedM;
        out_.residualM = ab.residualM;
        out_.rejectedByGate = ab.rejectedByGate;
        out_.hasEstimate = ab.hasEstimate;
    }

    updateTrackStateOnInvalid();
}