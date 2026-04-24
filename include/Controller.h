#pragma once
#include <Arduino.h>
#include "TC22.h"
#include "TFT.h"
#include "TrackerCore_v2.h"
#include <esp_system.h>

using ButtonEdgeFn = bool (*)();
using PublishLogFn = void (*)(const char* payload);

class Controller {
public:
  Controller(TC22Driver& sensor, TFTDistance& display, ButtonEdgeFn buttonEdge, PublishLogFn publishLog = nullptr);

  void begin();
  void tick();
  void setTestID(uint16_t id) { testId_ = id; }
  void setPublishLogFn(PublishLogFn fn) { publishLog_ = fn; }

  void setEstimatorMode(EstimatorMode mode) { tracker_.setEstimatorMode(mode); }
  void setAlphaBetaConfig(const AlphaBetaConfig& cfg) { tracker_.setAlphaBetaConfig(cfg); }

  uint32_t bootId() const { return bootId_; }

private:
  enum LogMode : uint8_t{
    MODE_CONTINUOUS = 0,
    MODE_SINGLESHOT = 1,
    MODE_LOCKED = 2
  };

  static constexpr uint32_t FRAME_TIMEOUT_MS    = 1000;
  static constexpr uint32_t OK_TIMEOUT_MS       = 3000;
  static constexpr uint32_t RESTART_INTERVAL_MS = 5000;
  static constexpr uint8_t  MAX_RESTARTS        = 3;
  static constexpr uint32_t LOCK_HOLD_MS        = 4000;
  static constexpr uint32_t LOCK_PUBLISH_INTERVAL_MS = 250;

  float computeFPSOnOk(uint32_t nowMs);

  void renderTracker();
  void renderLocked();
  void handleRestartPolicy(uint32_t nowMs);
  void publishTrackerSnapshot(const TrackerOutput& out, uint8_t mode, uint8_t systemErrorCode);
  uint32_t makeBootId() const;

  TC22Driver& sensor_;
  TFTDistance& display_;
  ButtonEdgeFn buttonEdge_;
  PublishLogFn publishLog_;
  TrackerCore tracker_;

  bool singleShot_ = false;
  bool targetLocked_ = false;
  uint32_t lockedUntilMs_ = 0;
  float lockedValueM_ = NAN;

  uint32_t lastFrameMs_ = 0;
  uint32_t lastOkMs_ = 0;
  uint32_t lastRestartMs_ = 0;
  uint8_t restartCount_ = 0;

  uint32_t lastOkSampleMs_ = 0;
  uint16_t testId_ = 0;

  uint32_t bootId_ = 0;
  uint32_t msgSeq_ = 0;
  uint32_t lastLockedPublishMs_ = 0;
  uint8_t systemErrorCode_ = 0;  
};