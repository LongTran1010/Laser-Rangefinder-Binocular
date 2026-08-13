from dataclasses import dataclass, asdict
from typing import Optional, List, Dict, Any
import math
import pandas as pd

# ---------------------------
# Match firmware enums
# ---------------------------

MEAS_OK = 0
MEAS_TIMEOUT = 1
MEAS_BAD_CRC = 2
MEAS_BAD_FRAME = 3
MEAS_NO_SIGNAL = 4

TRACK_SEARCHING = 0
TRACK_CANDIDATE = 1
TRACK_LOCKED = 2
TRACK_LOST = 3


# ---------------------------
# Config / Output
# ---------------------------

@dataclass
class ABConfig:
    alpha: float = 0.55
    beta: float = 0.14
    gate_threshold_m: float = 10.0
    max_reject: int = 3
    candidate_hits: int = 2
    lost_after_invalid: int = 5
    min_dt_s: float = 1e-3
    max_dt_s: float = 2.0
    reinit_on_switch: bool = True


@dataclass
class ABOutput:
    hasEstimate: bool = False

    rawDistanceM: float = math.nan
    filteredDistanceM: float = math.nan
    rangeRateMps: float = 0.0
    fps: float = 0.0

    measStatus: int = MEAS_TIMEOUT
    trackState: int = TRACK_SEARCHING

    sampleTimeMs: int = 0
    lastGoodTimeMs: int = 0

    consecutiveValids: int = 0
    consecutiveInvalids: int = 0

    residualM: float = math.nan
    predictedDistanceM: float = math.nan
    rejectedByGate: bool = False


# ---------------------------
# Alpha-Beta Tracker
# ---------------------------

class RangeTracker:
    def __init__(self, cfg: ABConfig):
        self.cfg = cfg
        self.reset()

    def reset(self):
        self.initialized = False
        self.x = math.nan
        self.v = 0.0
        self.last_ts_ms: Optional[int] = None

        self.reject_count = 0
        self.consecutive_valids = 0
        self.consecutive_invalids = 0
        self.last_good_ts_ms = 0
        self.track_state = TRACK_SEARCHING

    def _clamp_dt(self, dt_s: float) -> float:
        if math.isnan(dt_s) or dt_s <= 0:
            return self.cfg.min_dt_s
        if dt_s < self.cfg.min_dt_s:
            return self.cfg.min_dt_s
        if dt_s > self.cfg.max_dt_s:
            return self.cfg.max_dt_s
        return dt_s

    def _compute_dt(self, ts_ms: int) -> float:
        if self.last_ts_ms is None:
            return self.cfg.min_dt_s
        return self._clamp_dt((ts_ms - self.last_ts_ms) / 1000.0)

    def _initialize(self, ts_ms: int, z_m: float):
        self.initialized = True
        self.x = z_m
        self.v = 0.0
        self.last_ts_ms = ts_ms

        self.reject_count = 0
        self.consecutive_valids = 1
        self.consecutive_invalids = 0
        self.last_good_ts_ms = ts_ms
        self.track_state = TRACK_CANDIDATE

    def _predict(self, dt_s: float):
        x_pred = self.x + self.v * dt_s
        v_pred = self.v
        return x_pred, v_pred

    def _make_output(
        self,
        ts_ms: int,
        raw_m: float,
        fps: float,
        status: int,
        residual_m: float = math.nan,
        predicted_m: float = math.nan,
        rejected_by_gate: bool = False,
    ) -> ABOutput:
        return ABOutput(
            hasEstimate=self.initialized and math.isfinite(self.x),
            rawDistanceM=raw_m,
            filteredDistanceM=self.x if self.initialized else math.nan,
            rangeRateMps=self.v if self.initialized else 0.0,
            fps=fps,
            measStatus=status,
            trackState=self.track_state,
            sampleTimeMs=ts_ms,
            lastGoodTimeMs=self.last_good_ts_ms,
            consecutiveValids=self.consecutive_valids,
            consecutiveInvalids=self.consecutive_invalids,
            residualM=residual_m,
            predictedDistanceM=predicted_m,
            rejectedByGate=rejected_by_gate,
        )

    def step(self, timestamp_ms: int, z_m: float, status: int, fps: float = 0.0) -> ABOutput:
        # No valid measurement
        if status != MEAS_OK or not math.isfinite(z_m):
            if self.initialized:
                dt_s = self._compute_dt(timestamp_ms)
                x_pred, v_pred = self._predict(dt_s)
                self.x, self.v = x_pred, v_pred
                self.last_ts_ms = timestamp_ms

                self.consecutive_valids = 0
                self.consecutive_invalids += 1

                if self.consecutive_invalids >= self.cfg.lost_after_invalid:
                    self.track_state = TRACK_LOST
            else:
                self.track_state = TRACK_SEARCHING

            return self._make_output(
                ts_ms=timestamp_ms,
                raw_m=z_m,
                fps=fps,
                status=status,
                residual_m=math.nan,
                predicted_m=self.x if self.initialized else math.nan,
                rejected_by_gate=False,
            )

        # First valid sample -> initialize
        if not self.initialized:
            self._initialize(timestamp_ms, z_m)
            return self._make_output(
                ts_ms=timestamp_ms,
                raw_m=z_m,
                fps=fps,
                status=status,
                residual_m=0.0,
                predicted_m=z_m,
                rejected_by_gate=False,
            )

        # Normal predict-update
        dt_s = self._compute_dt(timestamp_ms)
        x_pred, v_pred = self._predict(dt_s)
        residual_m = z_m - x_pred

        # Innovation gate
        if abs(residual_m) > self.cfg.gate_threshold_m:
            self.reject_count += 1
            self.consecutive_valids = 0
            self.consecutive_invalids += 1

            # keep predict-only state
            self.x, self.v = x_pred, v_pred
            self.last_ts_ms = timestamp_ms

            # repeated jump -> treat as target switch
            if self.reject_count >= self.cfg.max_reject and self.cfg.reinit_on_switch:
                self._initialize(timestamp_ms, z_m)
                return self._make_output(
                    ts_ms=timestamp_ms,
                    raw_m=z_m,
                    fps=fps,
                    status=status,
                    residual_m=residual_m,
                    predicted_m=x_pred,
                    rejected_by_gate=True,
                )

            if self.consecutive_invalids >= self.cfg.lost_after_invalid:
                self.track_state = TRACK_LOST

            return self._make_output(
                ts_ms=timestamp_ms,
                raw_m=z_m,
                fps=fps,
                status=status,
                residual_m=residual_m,
                predicted_m=x_pred,
                rejected_by_gate=True,
            )

        # Accept measurement
        self.reject_count = 0
        self.consecutive_invalids = 0
        self.consecutive_valids += 1
        self.last_good_ts_ms = timestamp_ms

        self.x = x_pred + self.cfg.alpha * residual_m
        self.v = v_pred + (self.cfg.beta / max(dt_s, self.cfg.min_dt_s)) * residual_m
        self.last_ts_ms = timestamp_ms

        if self.consecutive_valids < self.cfg.candidate_hits:
            self.track_state = TRACK_CANDIDATE
        else:
            self.track_state = TRACK_LOCKED

        return self._make_output(
            ts_ms=timestamp_ms,
            raw_m=z_m,
            fps=fps,
            status=status,
            residual_m=residual_m,
            predicted_m=x_pred,
            rejected_by_gate=False,
        )


def run_tracker_on_dataframe(df: pd.DataFrame, cfg: ABConfig) -> pd.DataFrame:
    tracker = RangeTracker(cfg)
    rows: List[Dict[str, Any]] = []

    for _, row in df.iterrows():
        out = tracker.step(
            timestamp_ms=int(row["timestamp_ms"]),
            z_m=float(row["raw_m"]) if pd.notna(row["raw_m"]) else math.nan,
            status=int(row["status"]),
            fps=float(row["fps"]) if "fps" in row and pd.notna(row["fps"]) else 0.0,
        )
        rows.append(asdict(out))

    return pd.DataFrame(rows)