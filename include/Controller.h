#pragma once
#include <Arduino.h>
#include "TC22/TC22.h"
#include "TFT.h"
#include "TrackerCore_v2.h"
#include "Logger.h"
#include <esp_system.h>

using ButtonEdgeFn = bool (*)();

// =====================================================================
// Controller
// ---------------------------------------------------------------------
// Trach nhiem:
//  - Doc TC22 qua TC22Driver (Parser + ValidityGate)
//  - Cap nhat TrackerCore (chon mode BASELINE / ALPHABETA / RAW_ONLY)
//  - Hien thi TFT
//  - Failsafe: rate-limit no-frame notify, restart sensor, watchdog state
//  - Publish snapshot qua Logger
// =====================================================================

class Controller {
public:
  Controller(TC22Driver& sensor,
             TFTDistance& display,
             ButtonEdgeFn buttonEdge,
             PublishLogFn publishLog = nullptr);

  void begin();
  void tick();

  void setTestID(uint16_t id) { testId_ = id; }
  void setPublishLogFn(PublishLogFn fn) { logger_.setPublishFn(fn); }

  void setEstimatorMode(EstimatorMode mode) { tracker_.setEstimatorMode(mode); }
  void setAlphaBetaConfig(const AlphaBetaConfig& cfg) {
    tracker_.setAlphaBetaConfig(cfg);
    // Cache vao Controller de Logger co the publish kem moi message
    cfgAlphaCached_     = cfg.alpha;
    cfgBetaCached_      = cfg.beta;
    cfgGateCached_      = cfg.gateThresholdM;
    cfgMinDtSCached_    = cfg.minDtS;
    cfgMaxRejectCached_ = cfg.maxReject;
  }
  // Set preset ID (0xFF = N/A, dung 0 = A_EMA_LIKE ... 5 = F_HANDHELD)
  void setPresetId(uint8_t id) { cfgPresetIdCached_ = id; }

  uint32_t bootId() const { return bootId_; }

private:
  enum LogMode : uint8_t{
    MODE_CONTINUOUS = 0,
    MODE_SINGLESHOT = 1,
    MODE_LOCKED     = 2
  };

  static constexpr uint32_t FRAME_TIMEOUT_MS         = 1000;
  static constexpr uint32_t OK_TIMEOUT_MS            = 3000;
  static constexpr uint32_t RESTART_INTERVAL_MS      = 5000;
  static constexpr uint8_t  MAX_RESTARTS             = 3;
  static constexpr uint32_t LOCK_HOLD_MS             = 4000;
  static constexpr uint32_t LOCK_PUBLISH_INTERVAL_MS = 250;
  static constexpr uint32_t NOFRAME_NOTIFY_PERIOD_MS = 250;

  float computeFPSOnOk(uint32_t nowMs);
  uint32_t makeBootId() const;
  LogContext makeLogContext(uint8_t mode) const;

  void renderLocked();
  void publishSnapshot(const TrackerOutput& out, uint8_t mode);

  void handleRestartPolicy(uint32_t nowMs);
  void publishLostSnapshot(uint32_t nowMs);

  TC22Driver&  sensor_;
  TFTDistance& display_;
  ButtonEdgeFn buttonEdge_;
  Logger       logger_;
  TrackerCore  tracker_;

  bool     singleShot_     = false;
  bool     targetLocked_   = false;
  uint32_t lockedUntilMs_  = 0;
  float    lockedValueM_   = NAN;
  uint32_t lastLockedPublishMs_ = 0;

  uint32_t lastFrameMs_         = 0;
  uint32_t lastOkMs_            = 0;
  uint32_t lastRestartMs_       = 0;
  uint8_t  restartCount_        = 0;
  uint32_t lastNoFrameNotifyMs_ = 0;

  uint32_t lastOkSampleMs_ = 0;
  float    fpsEst_         = 0.0f;

  uint16_t testId_         = 0;
  uint32_t bootId_         = 0;
  uint32_t msgSeq_         = 0;
  uint8_t  systemErrorCode_ = 0;

  // ---- Config cache cho LogContext ----
  float    cfgAlphaCached_     = 0.0f;
  float    cfgBetaCached_      = 0.0f;
  float    cfgGateCached_      = 0.0f;
  float    cfgMinDtSCached_    = 0.0f;
  uint8_t  cfgMaxRejectCached_ = 0;
  uint8_t  cfgPresetIdCached_  = 0xFF;
};
