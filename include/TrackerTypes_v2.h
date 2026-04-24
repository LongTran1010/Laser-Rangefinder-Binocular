#pragma once
#include <Arduino.h>
#include "MeasurementTypes.h"

enum TrackState : uint8_t{
    TRACK_SEARCHING = 0,
    TRACK_CANDIDATE,
    TRACK_STABLE,
    TRACK_LOST
};

enum EstimatorMode : uint8_t{
    EST_BASELINE = 0,
    EST_ALPHABETA = 1
};

struct TrackerConfig{
    // baseline cũ
    float alpha = 0.25f;
    float gateThresholdM = 10.0f;
    uint8_t maxReject = 5;

    // state machine chung
    uint8_t candidateHits = 2;
    uint8_t lostAfterInvalid = 5;
};

struct TrackerOutput{
    bool hasEstimate = false;

    float rawDistanceM = NAN;
    float filteredDistanceM = NAN;

    // field thật cho alpha-beta
    float rangeRateMps = NAN;
    float predictedDistanceM = NAN;
    float residualM = NAN;
    bool rejectedByGate = false;

    float fps = 0.0f;
    MeasStatus measStatus = MEAS_TIMEOUT;
    TrackState trackState = TRACK_SEARCHING;
    EstimatorMode estimatorMode = EST_BASELINE;

    uint32_t sampleTimeMs = 0;
    uint32_t lastGoodTimeMs = 0;

    uint16_t consecutiveValids = 0;
    uint16_t consecutiveInvalids = 0;

    uint8_t restartCount = 0;
    uint8_t systemErrorCode = 0;
    uint8_t mode = 0;
    uint16_t testId = 0;
};