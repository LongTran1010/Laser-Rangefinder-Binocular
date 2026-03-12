#pragma once
#include <Arduino.h>
#include "DistanceFilter.h"
#include "TrackerTypes.h"

class TrackerCore{
public:
    explicit TrackerCore(const TrackerConfig& cfg = {})
    : cfg_(cfg), filter_(cfg.alpha, cfg.gateThresholdM, cfg.maxReject) {}

    void configure(const TrackerConfig& cfg);
    void reset();

    void updateMeasurement(const Measurement& m, float fps);
    void notifyNoFrame(uint32_t nowMs);

    const TrackerOutput& output() const { return out_; }

private:
    void updateTrackStateFromValid();
    void updateTrackStateFromInvalid();

    TrackerConfig cfg_;
    DistanceFilter filter_;
    TrackerOutput out_;
};