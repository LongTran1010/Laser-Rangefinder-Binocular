#pragma once
#include <Arduino.h>
#include "TrackerTypes_v2.h"
#include "DistanceFilter.h"
#include "AlphaBetaTracker.h"

class TrackerCore{
public:
    explicit TrackerCore(const TrackerConfig& cfg = TrackerConfig{});

    void configure(const TrackerConfig& cfg);
    const TrackerConfig& config() const{ return cfg_; }

    void reset();

    void setEstimatorMode(EstimatorMode mode);
    EstimatorMode estimatorMode() const{ return mode_; }

    void setAlphaBetaConfig(const AlphaBetaConfig& cfg);
    const AlphaBetaConfig& alphaBetaConfig() const{ return abCfg_; }

    void updateMeasurement(const Measurement& m, float fps);
    void notifyNoFrame(uint32_t nowMs);

    const TrackerOutput& output() const{ return out_; }

private:
    void updateTrackStateOnValid();
    void updateTrackStateOnInvalid();
    void clearAlphaBetaFields();

private:
    TrackerConfig cfg_{};
    TrackerOutput out_{};

    // baseline
    DistanceFilter baselineFilter_;

    // alpha-beta
    AlphaBetaConfig abCfg_{};
    AlphaBetaTracker abTracker_;

    EstimatorMode mode_ = EST_BASELINE;
};