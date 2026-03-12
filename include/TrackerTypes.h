#pragma once
#include <Arduino.h>
#include "MeasurementTypes.h"

enum TrackState : uint8_t{
  TRACK_SEARCHING = 0,
  TRACK_CANDIDATE,
  TRACK_LOCKED,
  TRACK_LOST
};

struct TrackerConfig{
    float alpha = 0.25f;
    float gateThresholdM = 10.0f; // m
    uint8_t maxReject = 5;        // số mẫu nhảy vọt liên tiếp để coi là mục tiêu mới

    uint8_t candidateHits = 2;    // số mẫu OK liên tiếp để chuyển từ CANDIDATE sang LOCKED
    uint8_t lostAfterInvalid = 5; // số mâu không OK liên tiếp để báo LOST
};

struct TrackerOutput{
    bool hasEstimate = false; // có mẫu mới (có thể là lỗi)

    float rawDistanceM = NAN;
    float filteredDistanceM = NAN;
    float fps = 0.0f;
    
    MeasStatus measStatus = MEAS_TIMEOUT;
    TrackState trackState = TRACK_SEARCHING;

    uint32_t sampleTimeMs = 0;
    uint32_t lastGoodTimeMs = 0;

    uint16_t consecutiveValids = 0;
    uint16_t consecutiveInvalids = 0;
};