#include "Controller.h"
#include <esp_system.h>

Controller::Controller(TC22Driver& sensor, TFTDistance& display, ButtonEdgeFn buttonEdge, PublishLogFn publishLog)
    : sensor_(sensor), display_(display), buttonEdge_(buttonEdge), publishLog_(publishLog), tracker_(TrackerConfig{}) {}

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
  lastFrameMs_ = now;
  lastOkMs_ = now;
  lastRestartMs_ = now;
  lastOkSampleMs_ = 0;
  restartCount_ = 0;

  singleShot_ = false;
  targetLocked_ = false;
  lockedUntilMs_ = 0;
  lockedValueM_ = NAN;

  systemErrorCode_ = 0;
  lastLockedPublishMs_ = 0;
  bootId_ = makeBootId();
  msgSeq_ = 0;
  tracker_.reset();
  display_.setSystemError(0);
  
}

float Controller::computeFPSOnOk(uint32_t nowMs) {
  static float fps = 0.0f;

  if (lastOkSampleMs_ != 0) {
    float dt = (nowMs - lastOkSampleMs_) / 1000.0f;
    if (dt > 0.001f && dt < 2.0f) {
      fps = 1.0f / dt;
    }
  }

  lastOkSampleMs_ = nowMs;
  return fps;
}

void Controller::tick() {
  uint32_t now = millis();
  // Nếu chưa lock, chưa ở single-shot wait, và có cạnh nhấn nút
  // => gửi lệnh single-shot cho sensor
  if (!targetLocked_ && !singleShot_ && buttonEdge_ && buttonEdge_()) {
    singleShot_ = true;
    sensor_.startSingle();
  }

  Measurement m{};
  bool haveFrame = sensor_.poll(m);

  if (haveFrame) {
    lastFrameMs_ = now;

    float fps = (m.status == MEAS_OK) ? computeFPSOnOk(now) : 0.0f;
    tracker_.updateMeasurement(m, fps);

    if (m.status == MEAS_OK) {
      lastOkMs_ = now;
      restartCount_ = 0;
      systemErrorCode_ = 0;
      display_.setSystemError(0);

      // Nếu đang chờ single-shot và đã có mẫu OK
      // => chốt giá trị hiện tại rồi lock 4 giây
      if (singleShot_ && !targetLocked_) {
        singleShot_ = false;
        targetLocked_ = true;
        lockedUntilMs_ = now + LOCK_HOLD_MS;
        lockedValueM_ = tracker_.output().filteredDistanceM;
        lastLockedPublishMs_ = 0; // cho phép publish ngay khi vào locked
      }
    }

    if (!targetLocked_) {
      renderTracker();
    }
  } else {
    if (!targetLocked_ && (now - lastFrameMs_ > FRAME_TIMEOUT_MS)) {
      tracker_.notifyNoFrame(now);
      renderTracker();
    }
  }

  if (targetLocked_) {
    renderLocked();

    if (now > lockedUntilMs_ || (buttonEdge_ && buttonEdge_())) {
      targetLocked_ = false;
      singleShot_ = false;
      sensor_.startContinuous();
    }
  }

  if (!targetLocked_) {
    handleRestartPolicy(now);
  }
}

void Controller::publishTrackerSnapshot(const TrackerOutput& out,
                                        uint8_t mode,
                                        uint8_t systemErrorCode) {
  const uint8_t rejectReason = static_cast<uint8_t>(sensor_.lastRejectReason());
  if (!publishLog_) return;

  char rawBuf[24];
  char estBuf[24];
  char fpsBuf[16];
  char rateBuf[24];
  char predBuf[24];
  char residBuf[24];

  if (isfinite(out.rawDistanceM)) snprintf(rawBuf, sizeof(rawBuf), "%.3f", out.rawDistanceM);
  else snprintf(rawBuf, sizeof(rawBuf), "null");

  if (isfinite(out.filteredDistanceM)) snprintf(estBuf, sizeof(estBuf), "%.3f", out.filteredDistanceM);
  else snprintf(estBuf, sizeof(estBuf), "null");

  if (isfinite(out.fps)) snprintf(fpsBuf, sizeof(fpsBuf), "%.2f", out.fps);
  else snprintf(fpsBuf, sizeof(fpsBuf), "0.00");

  if (isfinite(out.rangeRateMps)) snprintf(rateBuf, sizeof(rateBuf), "%.3f", out.rangeRateMps);
  else snprintf(rateBuf, sizeof(rateBuf), "null");

  if (isfinite(out.predictedDistanceM)) snprintf(predBuf, sizeof(predBuf), "%.3f", out.predictedDistanceM);
  else snprintf(predBuf, sizeof(predBuf), "null");

  if (isfinite(out.residualM)) snprintf(residBuf, sizeof(residBuf), "%.3f", out.residualM);
  else snprintf(residBuf, sizeof(residBuf), "null");

  char payload[640];
  snprintf(
      payload,
      sizeof(payload),
      "{"
      "\"boot_id\":%lu,"
      "\"msg_seq\":%lu,"
      "\"dev_ts_ms\":%lu,"
      "\"mode\":%u,"
      "\"raw_m\":%s,"
      "\"est_m\":%s,"
      "\"fps\":%s,"
      "\"meas_status\":%u,"
      "\"track_state\":%u,"
      "\"has_estimate\":%u,"
      "\"consecutive_valids\":%u,"
      "\"consecutive_invalids\":%u,"
      "\"last_good_ts_ms\":%lu,"
      "\"restart_count\":%u,"
      "\"system_error\":%u,"
      "\"rate_mps\":%s,"
      "\"predicted_m\":%s,"
      "\"residual_m\":%s,"
      "\"rejected_by_gate\":%u,"
      "\"reject_reason\":%u,"
      "\"estimator_mode\":%u,"
      "\"test_id\":%u"
      "}",
      static_cast<unsigned long>(bootId_),
      static_cast<unsigned long>(msgSeq_++),
      static_cast<unsigned long>(out.sampleTimeMs),
      static_cast<unsigned>(mode),
      rawBuf,
      estBuf,
      fpsBuf,
      static_cast<unsigned>(out.measStatus),
      static_cast<unsigned>(out.trackState),
      static_cast<unsigned>(out.hasEstimate),
      static_cast<unsigned>(out.consecutiveValids),
      static_cast<unsigned>(out.consecutiveInvalids),
      static_cast<unsigned long>(out.lastGoodTimeMs),
      static_cast<unsigned>(restartCount_),
      static_cast<unsigned>(systemErrorCode),
      rateBuf,
      predBuf,
      residBuf,
      static_cast<unsigned>(out.rejectedByGate),
      static_cast<unsigned>(rejectReason),
      static_cast<unsigned>(out.estimatorMode),
      static_cast<unsigned>(testId_));

  publishLog_(payload);
}

void Controller::renderTracker() {
  const TrackerOutput& out = tracker_.output();
  display_.render(out);

  const uint8_t mode = (singleShot_ ? MODE_SINGLESHOT : MODE_CONTINUOUS);
  publishTrackerSnapshot(out, mode, systemErrorCode_);
}

void Controller::renderLocked() {
  TrackerOutput out = tracker_.output();
  out.hasEstimate = true;
  out.rawDistanceM = lockedValueM_;
  out.filteredDistanceM = lockedValueM_;
  out.fps = 0.0f;
  out.measStatus = MEAS_OK;
  out.trackState = TRACK_STABLE;
  out.sampleTimeMs = millis();

  display_.render(out);
  uint32_t now = out.sampleTimeMs;
  if (lastLockedPublishMs_ == 0 || (now - lastLockedPublishMs_ >= LOCK_PUBLISH_INTERVAL_MS)) {
    lastLockedPublishMs_ = now;
    publishTrackerSnapshot(out, MODE_LOCKED, systemErrorCode_);
  } 
}

void Controller::handleRestartPolicy(uint32_t nowMs) {
  bool tooLongNoOK = (nowMs - lastOkMs_ > OK_TIMEOUT_MS);
  bool canRestart = (nowMs - lastRestartMs_ > RESTART_INTERVAL_MS);
  bool haveRetryLeft = (restartCount_ < MAX_RESTARTS);

  if (tooLongNoOK && canRestart && haveRetryLeft) {
    lastRestartMs_ = nowMs;
    restartCount_++;

    sensor_.stop();
    delay(500);
    sensor_.startContinuous();
  }

  if (restartCount_ >= MAX_RESTARTS && tooLongNoOK) {
    singleShot_ = false;
    if (systemErrorCode_ != 1) {
      systemErrorCode_ = 1;
      display_.setSystemError(systemErrorCode_);

      TrackerOutput out = tracker_.output();
      out.measStatus = MEAS_TIMEOUT;
      out.trackState = TRACK_LOST;
      out.fps = 0.0f;
      out.sampleTimeMs = nowMs;
      publishTrackerSnapshot(out, MODE_CONTINUOUS, systemErrorCode_);
    }

    TrackerOutput out = tracker_.output();
    out.measStatus = MEAS_TIMEOUT;
    out.trackState = TRACK_LOST;
    out.fps = 0.0f;
    out.sampleTimeMs = nowMs;
    display_.render(out);
  } else {
    systemErrorCode_ = 0;
    display_.setSystemError(0);
  }
}