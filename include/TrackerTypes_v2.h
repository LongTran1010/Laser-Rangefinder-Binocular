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
    EST_BASELINE  = 0,   // Gating + Median(5) + EMA (firmware giai doan 1)
    EST_ALPHABETA = 1,   // Alpha-beta tracker (giai doan 2)
    EST_RAW_ONLY  = 2    // Khong loc - chi pass-through raw, dung cho fair benchmark
};

// Proposal 1: Tach hop le sensor (MEAS_OK) vs quyet dinh estimator (Decision).
// FSM dung EstimatorDecision de update consecutiveValids/Invalids, khong
// dung MEAS_OK dan thuan (co the bi α-β gate reject).
enum EstimatorDecision : uint8_t {
    EST_DECISION_NONE           = 0,   // Sensor invalid, khong co quyet dinh
    EST_DECISION_ACCEPTED       = 1,   // Filter accept -> hit++, miss=0
    EST_DECISION_REJECTED       = 2,   // Gate reject -> miss++, khong tang hit
    EST_DECISION_REINITIALIZED  = 3,   // Reinit (target-switch) -> hit=1, miss=0
    EST_DECISION_PREDICT_ONLY   = 4,   // Predict-only (invalid sensor) -> miss++
    // Review#8: tach mau dau tien (lock target) khoi target-switch.
    // INITIALIZED = lan dau co state (cold start hoac sau reset).
    // REINITIALIZED = da co state, gap target-switch -> reinit.
    EST_DECISION_INITIALIZED    = 5    // First lock -> hit=1, miss=0
};

// Proposal 3: Config rieng cho BaselineFilter (Gating + Median + EMA).
struct BaselineConfig {
    float   emaLambda      = 0.25f;    // Smoothing factor EMA
    float   gateThresholdM = 10.0f;    // Gate reject threshold (met)
    uint8_t maxReject      = 5;        // So mau reject lien tiep truoc khi reinit
};

struct TrackerConfig{
    // Review#3.2: DA XOA baseline params cu (alpha/gateThresholdM/maxReject).
    // Preset la SINGLE SOURCE - BaselineConfig cho baseline, AlphaBetaConfig
    // cho alpha-beta. TrackerCore bootstrap voi default cua BaselineConfig{}.
    // state machine chung
    uint8_t candidateHits = 2;
    uint8_t lostAfterInvalid = 5;
};

struct TrackerOutput{
    bool hasEstimate = false;

    float rawDistanceM = NAN;
    float filteredDistanceM = NAN;

    // field that cho alpha-beta
    float rangeRateMps = NAN;
    float predictedDistanceM = NAN;
    float residualM = NAN;
    // Review#RG1: SEMANTIC CHOT - rejectedByGate = "raw sample vuot nguong gate"
    // (KHONG phai "sample bi loai bo khoi filter"). Vi vay case REINITIALIZED
    // van co flag = true (sample da vuot gate, chi khac la duoc dung de reinit).
    // De biet sample co duoc dung khong, dung estimatorDecision:
    //   ACCEPTED    : sample duoc dung update
    //   REJECTED    : sample bi bo, predict-only
    //   REINITIALIZED: sample duoc dung REINIT state (dong thoi vuot gate)
    //   INITIALIZED : first lock, khong nghia gate
    //   PREDICT_ONLY: sensor invalid, coasting
    //   NONE        : sensor invalid, chua co state
    bool rejectedByGate = false;

    float fps = 0.0f;
    MeasStatus measStatus = MEAS_TIMEOUT;
    TrackState trackState = TRACK_SEARCHING;
    EstimatorMode estimatorMode = EST_BASELINE;

    // Proposal 1: quyet dinh cua estimator cho mau nay
    // Cho phep log/FSM phan biet sensor validity vs tracker locking state.
    EstimatorDecision estimatorDecision = EST_DECISION_NONE;

    uint32_t sampleTimeMs = 0;
    uint32_t lastGoodTimeMs = 0;

    uint16_t consecutiveValids = 0;
    uint16_t consecutiveInvalids = 0;

    // Chi co y nghia trong mode ALPHABETA: dem so mau invalid lien tiep
    // duoc predict-only (B4). Reset ve 0 khi co mau OK. Dung cho DT5
    // verify velocity decay behavior. Cac mode khac giu = 0.
    uint8_t predictHoldCount = 0;

    uint8_t restartCount = 0;
    uint8_t systemErrorCode = 0;
    uint8_t mode = 0;
    uint16_t testId = 0;
};
