#include "TrackerCore.h"

void TrackerCore::configure(const TrackerConfig& cfg){
    cfg_ = cfg;
    filter_.configure(cfg.alpha, cfg.gateThresholdM, cfg.maxReject);
    reset();
}

void TrackerCore::reset(){
    filter_.reset();
    out_ = TrackerOutput{};
    out_.measStatus = MEAS_TIMEOUT;
    out_.trackState = TRACK_SEARCHING;
}

void TrackerCore::updateMeasurement(const Measurement& m, float fps){
    out_.rawDistanceM = m.dist_m;
    out_.fps = fps;
    out_.measStatus = m.status;
    out_.sampleTimeMs = m.t_ms;

    if(m.status == MEAS_OK){
        out_.lastGoodTimeMs = m.t_ms;
        out_.consecutiveInvalids = 0;
        out_.consecutiveValids++;

        out_.filteredDistanceM = filter_.update(m.dist_m, true);
        out_.hasEstimate = filter_.hasValue();

        updateTrackStateFromValid();
        return;
    }
    out_.consecutiveValids = 0;
    out_.consecutiveInvalids++;
    out_.filteredDistanceM = filter_.value();
    out_.hasEstimate = filter_.hasValue();

    updateTrackStateFromInvalid();   
}

void TrackerCore::notifyNoFrame(uint32_t nowMs){
    out_.measStatus = MEAS_TIMEOUT;
    out_.sampleTimeMs = nowMs;
    out_.fps = 0.0f;
    out_.rawDistanceM = NAN;

    out_.consecutiveValids = 0;
    out_.consecutiveInvalids++;

    out_.filteredDistanceM = filter_.value();
    out_.hasEstimate = filter_.hasValue();

    updateTrackStateFromInvalid();
}

void TrackerCore::updateTrackStateFromValid(){
    if(!out_.hasEstimate){
        out_.trackState = TRACK_SEARCHING;
        return;
    }

    if(out_.consecutiveValids < cfg_.candidateHits){
        out_.trackState = TRACK_CANDIDATE;
    }else{
        out_.trackState = TRACK_LOCKED;
  }
}

void TrackerCore::updateTrackStateFromInvalid(){
    if(!out_.hasEstimate){
        out_.trackState = TRACK_SEARCHING;
        return;
    }
    if(out_.consecutiveInvalids >= cfg_.lostAfterInvalid){
        out_.trackState = TRACK_LOST;
    }
}

