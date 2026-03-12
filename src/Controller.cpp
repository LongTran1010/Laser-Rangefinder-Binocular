#include "Controller.h"

Controller::Controller(TC22Driver& sensor, TFTDistance& display, ButtonEdgeFn buttonEdge)
    : sensor_(sensor), display_(display), buttonEdge_(buttonEdge), tracker_(TrackerConfig{}) {}

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
  restartCount_ = 0;

  tracker_.reset();
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

      if (singleShot_ && !targetLocked_) {
        singleShot_ = false;
        targetLocked_ = true;
        lockedUntilMs_ = now + LOCK_HOLD_MS;
        lockedValueM_ = tracker_.output().filteredDistanceM;
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
      sensor_.startContinuous();
    }
  }

  if (!targetLocked_) {
    handleRestartPolicy(now);
  }
}

void Controller::renderTracker() {
  const TrackerOutput& out = tracker_.output();
  display_.render(out);

  Serial.printf("%lu,%.3f,%.3f,%u,%.1f\n",
                (unsigned long)out.sampleTimeMs,
                out.rawDistanceM,
                out.filteredDistanceM,
                (unsigned)out.measStatus,
                out.fps);
}

void Controller::renderLocked() {
  TrackerOutput out = tracker_.output();
  out.hasEstimate = true;
  out.rawDistanceM = lockedValueM_;
  out.filteredDistanceM = lockedValueM_;
  out.fps = 0.0f;
  out.measStatus = MEAS_OK;
  out.trackState = TRACK_LOCKED;

  display_.render(out);
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
    display_.setSystemError(1);

    TrackerOutput out = tracker_.output();
    out.measStatus = MEAS_TIMEOUT;
    out.trackState = TRACK_LOST;
    out.fps = 0.0f;
    display_.render(out);
  } else {
    display_.setSystemError(0);
  }
}