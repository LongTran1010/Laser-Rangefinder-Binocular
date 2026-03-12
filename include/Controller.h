#pragma once
#include <Arduino.h>
#include "TC22.h"
#include "TFT.h"
#include "TrackerCore.h"

using ButtonEdgeFn = bool (*)();

class Controller {
public:
  Controller(TC22Driver& sensor, TFTDistance& display, ButtonEdgeFn buttonEdge);

  void begin();
  void tick();

private:
  static constexpr uint32_t FRAME_TIMEOUT_MS    = 1000;
  static constexpr uint32_t OK_TIMEOUT_MS       = 3000;
  static constexpr uint32_t RESTART_INTERVAL_MS = 5000;
  static constexpr uint8_t  MAX_RESTARTS        = 3;
  static constexpr uint32_t LOCK_HOLD_MS        = 4000;

  float computeFPSOnOk(uint32_t nowMs);
  void renderTracker();
  void renderLocked();
  void handleRestartPolicy(uint32_t nowMs);

  TC22Driver& sensor_;
  TFTDistance& display_;
  ButtonEdgeFn buttonEdge_;
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
};