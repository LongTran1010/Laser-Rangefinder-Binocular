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

    // Proposal 3: Config rieng cho baseline (khong dung TrackerConfig chung)
    void setBaselineConfig(const BaselineConfig& cfg);
    const BaselineConfig& baselineConfig() const { return baselineCfg_; }

    void updateMeasurement(const Measurement& m, float fps);
    void notifyNoFrame(uint32_t nowMs);

    const TrackerOutput& output() const{ return out_; }

private:
    void updateTrackStateOnValid();
    void updateTrackStateOnInvalid();
    void clearAlphaBetaFields();
    // Proposal 1: FSM chay theo decision cua estimator (khong dua vao meas.status)
    void applyDecisionToFsm(EstimatorDecision decision);

private:
    TrackerConfig cfg_{};
    TrackerOutput out_{};

    // baseline
    DistanceFilter baselineFilter_;
    BaselineConfig baselineCfg_{};   // Proposal 3: config rieng cho baseline

    // alpha-beta
    AlphaBetaConfig abCfg_{};
    AlphaBetaTracker abTracker_;

    EstimatorMode mode_ = EST_BASELINE;
};