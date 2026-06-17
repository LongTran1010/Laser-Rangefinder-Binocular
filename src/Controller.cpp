#include "Controller.h"
#include <esp_system.h>

Controller::Controller(TC22Driver& sensor,
                       TFTDistance& display,
                       ButtonEdgeFn buttonEdge,
                       PublishLogFn publishLog)
    : sensor_(sensor),
      display_(display),
      buttonEdge_(buttonEdge),
      logger_(publishLog),
      tracker_(TrackerConfig{}) {}

uint32_t Controller::makeBootId() const {
#if defined(ESP32)
  return esp_random();
#else
  return static_cast<uint32_t>(micros() ^ millis());
#endif
}

void Controller::begin() {
  display_.begin();
  display_.setMaxRangeMeters(800.0f);
  display_.setStaleTimeoutMs(1000);

  sensor_.begin(115200);
  delay(500);
  sensor_.setLittleEndianPayload(true);
  sensor_.startContinuous();

  uint32_t now = millis();
  lastFrameMs_         = now;
  lastOkMs_            = now;
  lastRestartMs_       = now;
  lastOkSampleMs_      = 0;
  lastNoFrameNotifyMs_ = 0;
  restartCount_        = 0;
  fpsEst_              = 0.0f;

  singleShot_          = false;
  targetLocked_        = false;
  lockedUntilMs_       = 0;
  lockedValueM_        = NAN;
  lastLockedPublishMs_ = 0;

  systemErrorCode_     = 0;
  bootId_              = makeBootId();
  msgSeq_              = 0;

  tracker_.reset();
  display_.setSystemError(0);

  Serial.printf("[Controller] boot_id=%lu test_id=%u\n",
                static_cast<unsigned long>(bootId_),
                static_cast<unsigned>(testId_));
}

float Controller::computeFPSOnOk(uint32_t nowMs) {
  if (lastOkSampleMs_ != 0) {
    float dt = (nowMs - lastOkSampleMs_) / 1000.0f;
    if (dt > 0.001f && dt < 2.0f) {
      float instFps = 1.0f / dt;
      fpsEst_ = (fpsEst_ <= 0.0f) ? instFps : (0.4f * instFps + 0.6f * fpsEst_);
    }
  }
  lastOkSampleMs_ = nowMs;
  return fpsEst_;
}

LogContext Controller::makeLogContext(uint8_t mode) const {
  LogContext ctx;
  ctx.bootId       = bootId_;
  ctx.msgSeq       = msgSeq_;
  ctx.testId       = testId_;
  ctx.mode         = mode;
  ctx.systemError  = systemErrorCode_;
  ctx.restartCount = restartCount_;
  ctx.rejectReason = sensor_.lastRejectReason();
  // Config snapshot — moi message tu mo ta cau hinh dang chay
  ctx.cfgAlpha     = cfgAlphaCached_;
  ctx.cfgBeta      = cfgBetaCached_;
  ctx.cfgGateM     = cfgGateCached_;
  ctx.cfgMinDtS    = cfgMinDtSCached_;
  ctx.cfgMaxReject = cfgMaxRejectCached_;
  ctx.cfgPresetId  = cfgPresetIdCached_;
  return ctx;
}

void Controller::publishSnapshot(const TrackerOutput& out, uint8_t mode) {
  LogContext ctx = makeLogContext(mode);
  if (logger_.publishSnapshot(out, ctx)) {
    msgSeq_++;
  }
}

void Controller::tick() {
  uint32_t now = millis();

  if (!targetLocked_ && !singleShot_ && buttonEdge_ && buttonEdge_()) {
    singleShot_ = true;
    sensor_.startSingle();
  }

  // ===== Drain TAT CA frame dang co trong buffer UART =====
  // Neu TC22 gui nhanh hon toc do render, frame don lai -> hien thi tre tich luy
  // (ro nhat khi do cu ly gan, TC22 fps cao). Giai phap: xu ly HET frame moi
  // tick, log TUNG frame (toan ven benchmark), nhung render TFT 1 lan duy nhat.
  Measurement m{};
  bool gotFrame  = false;
  bool lockedNow = false;
  while (sensor_.poll(m)) {
    gotFrame             = true;
    lastFrameMs_         = now;
    lastNoFrameNotifyMs_ = 0;

    float fps = (m.status == MEAS_OK) ? computeFPSOnOk(now) : fpsEst_;
    tracker_.updateMeasurement(m, fps);

    if (m.status == MEAS_OK) {
      lastOkMs_     = now;
      restartCount_ = 0;
      if (systemErrorCode_ != 0) {       // chi clear khi co transition
        systemErrorCode_ = 0;
        display_.setSystemError(0);
      }

      if (singleShot_ && !targetLocked_) {
        singleShot_          = false;
        targetLocked_        = true;
        lockedUntilMs_       = now + LOCK_HOLD_MS;
        lockedValueM_        = tracker_.output().filteredDistanceM;
        lastLockedPublishMs_ = 0;
        lockedNow            = true;
      }
    }

    // Log MOI frame -> dam bao toan ven du lieu cho benchmark
    if (!targetLocked_) {
      publishSnapshot(tracker_.output(),
                      singleShot_ ? MODE_SINGLESHOT : MODE_CONTINUOUS);
    }
    if (lockedNow) break;                // ngung drain ngay khi vua lock
  }

  // Render TFT 1 lan sau khi drain xong (TFT khong con la bottleneck)
  if (gotFrame && !targetLocked_) {
    display_.render(tracker_.output());
  }

  // ===== No-frame timeout (rate-limited) =====
  if (!gotFrame && !targetLocked_
      && (now - lastFrameMs_ > FRAME_TIMEOUT_MS)
      && (now - lastNoFrameNotifyMs_ > NOFRAME_NOTIFY_PERIOD_MS)) {
    lastNoFrameNotifyMs_ = now;
    tracker_.notifyNoFrame(now);
    publishSnapshot(tracker_.output(), MODE_CONTINUOUS);
    display_.render(tracker_.output());
  }

  if (targetLocked_) {
    renderLocked();
    if (now > lockedUntilMs_ || (buttonEdge_ && buttonEdge_())) {
      targetLocked_ = false;
      singleShot_   = false;
      sensor_.startContinuous();
    }
  }

  if (!targetLocked_) {
    handleRestartPolicy(now);
  }
}

void Controller::renderLocked() {
  TrackerOutput out = tracker_.output();
  out.hasEstimate       = true;
  out.rawDistanceM      = NAN;
  out.filteredDistanceM = lockedValueM_;
  out.fps               = 0.0f;
  out.measStatus        = MEAS_OK;
  out.trackState        = TRACK_STABLE;
  out.sampleTimeMs      = millis();

  display_.render(out);
  uint32_t now = out.sampleTimeMs;
  if (lastLockedPublishMs_ == 0 || (now - lastLockedPublishMs_ >= LOCK_PUBLISH_INTERVAL_MS)) {
    lastLockedPublishMs_ = now;
    publishSnapshot(out, MODE_LOCKED);
  }
}

void Controller::publishLostSnapshot(uint32_t nowMs) {
  TrackerOutput out = tracker_.output();
  out.measStatus   = MEAS_TIMEOUT;
  out.trackState   = TRACK_LOST;
  out.fps          = 0.0f;
  out.sampleTimeMs = nowMs;
  publishSnapshot(out, MODE_CONTINUOUS);
}

void Controller::handleRestartPolicy(uint32_t nowMs) {
  bool tooLongNoOK    = (nowMs - lastOkMs_      > OK_TIMEOUT_MS);
  bool canRestart     = (nowMs - lastRestartMs_ > RESTART_INTERVAL_MS);
  bool haveRetryLeft  = (restartCount_ < MAX_RESTARTS);

  if (tooLongNoOK && canRestart && haveRetryLeft) {
    lastRestartMs_ = nowMs;
    restartCount_++;
    Serial.printf("[Failsafe] Restart sensor (attempt %u/%u)\n",
                  restartCount_, MAX_RESTARTS);
    sensor_.stop();
    delay(500);
    sensor_.startContinuous();
    return;
  }

  if (restartCount_ >= MAX_RESTARTS && tooLongNoOK) {
    singleShot_ = false;
    if (systemErrorCode_ != 1) {
      systemErrorCode_ = 1;
      display_.setSystemError(systemErrorCode_);
      publishLostSnapshot(nowMs);
    }
    TrackerOutput out = tracker_.output();
    out.measStatus   = MEAS_TIMEOUT;
    out.trackState   = TRACK_LOST;
    out.fps          = 0.0f;
    out.sampleTimeMs = nowMs;
    display_.render(out);
  } else {
    if (systemErrorCode_ == 1) {
      systemErrorCode_ = 0;
      display_.setSystemError(0);
    }
  }
}
