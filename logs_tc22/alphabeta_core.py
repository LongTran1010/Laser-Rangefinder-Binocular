"""
Alpha-Beta tracker - Python mirror cua firmware AlphaBetaTracker.cpp.

Moi thay doi trong firmware phai duoc dong bo o day de tuning offline co gia tri.
Sau khi sua, chay `python3 test_alphabeta_consistency.py` de verify.

Anh xa voi firmware:
  ABConfig            <-> AlphaBetaConfig (include/AlphaBetaTracker.h)
  RangeTracker.step() <-> AlphaBetaTracker::update()
  BaselineFilter      <-> lib/DistanceFilter (Gating + Median + EMA)
"""
from dataclasses import dataclass, asdict
from typing import Optional, List, Dict, Any
import math
import pandas as pd

# ---------------------------
# Match firmware enums
# ---------------------------

MEAS_OK         = 0
MEAS_TIMEOUT    = 1
MEAS_BAD_CRC    = 2
MEAS_BAD_FRAME  = 3
MEAS_NO_SIGNAL  = 4

TRACK_SEARCHING = 0
TRACK_CANDIDATE = 1
TRACK_STABLE    = 2
TRACK_LOCKED    = TRACK_STABLE     # alias cho code cu
TRACK_LOST      = 3

EST_BASELINE    = 0
EST_ALPHABETA   = 1
EST_RAW_ONLY    = 2


# ---------------------------
# Config / Output
# ---------------------------

@dataclass
class ABConfig:
    """Default phai khop PRESET_D_AB_BALANCED trong AlphaBetapresets.h."""
    alpha: float = 0.30
    beta:  float = 0.0529
    gate_threshold_m: float = 10.0
    max_reject: int = 5

    candidate_hits: int = 2
    lost_after_invalid: int = 5

    default_dt_s: float = 0.40
    min_dt_s: float = 0.10
    max_dt_s: float = 2.0

    reinit_on_switch: bool = True
    init_rate_mps: float = 0.0

    invalid_velocity_decay: float = 0.85
    max_predict_hold_samples: int = 8


def benedict_bordner_beta(alpha: float) -> float:
    """beta = alpha^2 / (2 - alpha) - critically damped."""
    return (alpha * alpha) / (2.0 - alpha)


def assert_matches_firmware_defaults(cfg: ABConfig) -> None:
    fw = ABConfig()
    diffs = []
    for f in fw.__dataclass_fields__:
        a, b = getattr(cfg, f), getattr(fw, f)
        if isinstance(a, float):
            if not math.isclose(a, b, rel_tol=1e-4, abs_tol=1e-6):
                diffs.append((f, a, b))
        elif a != b:
            diffs.append((f, a, b))
    if diffs:
        raise AssertionError(f"ABConfig khac firmware default: {diffs}")


@dataclass
class ABOutput:
    hasEstimate: bool = False
    rawDistanceM: float       = math.nan
    filteredDistanceM: float  = math.nan
    rangeRateMps: float       = math.nan
    fps: float                = 0.0
    measStatus: int = MEAS_TIMEOUT
    trackState: int = TRACK_SEARCHING
    sampleTimeMs: int   = 0
    lastGoodTimeMs: int = 0
    consecutiveValids: int   = 0
    consecutiveInvalids: int = 0
    residualM: float          = math.nan
    predictedDistanceM: float = math.nan
    rejectedByGate: bool      = False
    predictHoldCount: int = 0
    rejectCount: int      = 0


# ---------------------------
# Alpha-Beta Tracker
# ---------------------------

class RangeTracker:
    def __init__(self, cfg: ABConfig):
        self.cfg = cfg
        self.reset()

    def reset(self):
        self.initialized = False
        self.x: float = math.nan
        self.v: float = self.cfg.init_rate_mps
        self.last_ts_ms: Optional[int] = None
        self.reject_count = 0
        self.predict_hold_count = 0
        self.consecutive_valids = 0
        self.consecutive_invalids = 0
        self.last_good_ts_ms = 0
        self.track_state = TRACK_SEARCHING

    def _resolve_dt(self, ts_ms: int, fps: float) -> float:
        dt = self.cfg.default_dt_s
        if self.last_ts_ms is not None and ts_ms > self.last_ts_ms:
            dt = (ts_ms - self.last_ts_ms) / 1000.0
        elif fps > 0.01:
            dt = 1.0 / fps
        if math.isnan(dt) or dt < self.cfg.min_dt_s:
            dt = self.cfg.min_dt_s
        if dt > self.cfg.max_dt_s:
            dt = self.cfg.max_dt_s
        return dt

    def _initialize(self, ts_ms: int, z_m: float):
        self.initialized = True
        self.x = z_m
        self.v = self.cfg.init_rate_mps
        self.last_ts_ms = ts_ms
        self.reject_count = 0
        self.predict_hold_count = 0
        self.consecutive_valids = 1
        self.consecutive_invalids = 0
        self.last_good_ts_ms = ts_ms
        self.track_state = TRACK_CANDIDATE

    def _make_output(self, ts_ms, raw_m, fps, status,
                     residual_m=math.nan, predicted_m=math.nan, rejected_by_gate=False):
        return ABOutput(
            hasEstimate=self.initialized and math.isfinite(self.x),
            rawDistanceM=raw_m,
            filteredDistanceM=self.x if self.initialized else math.nan,
            rangeRateMps=self.v if self.initialized else math.nan,
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
            predictHoldCount=self.predict_hold_count,
            rejectCount=self.reject_count,
        )

    def step(self, timestamp_ms: int, z_m: float, status: int, fps: float = 0.0) -> ABOutput:
        # ---- INVALID measurement ----
        if status != MEAS_OK or not math.isfinite(z_m):
            if not self.initialized:
                self.track_state = TRACK_SEARCHING
                return self._make_output(timestamp_ms, z_m, fps, status)

            dt = self._resolve_dt(timestamp_ms, fps)
            # B4: decay velocity
            self.v *= self.cfg.invalid_velocity_decay
            self.predict_hold_count += 1
            if self.predict_hold_count >= self.cfg.max_predict_hold_samples:
                self.v = 0.0
            else:
                self.x = self.x + self.v * dt

            self.last_ts_ms = timestamp_ms
            self.consecutive_valids = 0
            self.consecutive_invalids += 1
            if self.consecutive_invalids >= self.cfg.lost_after_invalid:
                self.track_state = TRACK_LOST

            return self._make_output(
                timestamp_ms, z_m, fps, status,
                residual_m=math.nan, predicted_m=self.x, rejected_by_gate=False,
            )

        # ---- First valid -> initialize ----
        if not self.initialized:
            self._initialize(timestamp_ms, z_m)
            return self._make_output(
                timestamp_ms, z_m, fps, status,
                residual_m=0.0, predicted_m=z_m, rejected_by_gate=False,
            )

        # ---- Predict ----
        dt = self._resolve_dt(timestamp_ms, fps)
        x_pred = self.x + self.v * dt
        v_pred = self.v
        residual = z_m - x_pred

        # ---- Gate ----
        if abs(residual) > self.cfg.gate_threshold_m:
            self.reject_count += 1
            if self.cfg.reinit_on_switch and self.reject_count >= self.cfg.max_reject:
                self._initialize(timestamp_ms, z_m)
                out = self._make_output(
                    timestamp_ms, z_m, fps, status,
                    residual_m=residual, predicted_m=x_pred, rejected_by_gate=True,
                )
                return out
            # B2: predict-only
            self.x = x_pred
            self.v = v_pred
            self.last_ts_ms = timestamp_ms
            self.predict_hold_count = 0
            return self._make_output(
                timestamp_ms, z_m, fps, status,
                residual_m=residual, predicted_m=x_pred, rejected_by_gate=True,
            )

        # ---- Accept ----
        self.reject_count = 0
        self.predict_hold_count = 0
        self.consecutive_invalids = 0
        self.consecutive_valids += 1
        self.last_good_ts_ms = timestamp_ms

        self.x = x_pred + self.cfg.alpha * residual
        if self.cfg.beta > 0.0 and dt > self.cfg.min_dt_s:
            self.v = v_pred + (self.cfg.beta / dt) * residual
        else:
            self.v = v_pred
        self.last_ts_ms = timestamp_ms

        if self.consecutive_valids < self.cfg.candidate_hits:
            self.track_state = TRACK_CANDIDATE
        else:
            self.track_state = TRACK_STABLE

        return self._make_output(
            timestamp_ms, z_m, fps, status,
            residual_m=residual, predicted_m=x_pred, rejected_by_gate=False,
        )


# ---------------------------
# Baseline (Gating + Median(5) + EMA) - mirror lib/DistanceFilter
# ---------------------------

class BaselineFilter:
    def __init__(self, alpha: float = 0.25, gate_threshold_m: float = 10.0, max_reject: int = 5):
        self.alpha = alpha
        self.gate_threshold_m = gate_threshold_m
        self.max_reject = max_reject
        self.reset()

    def reset(self):
        self.dist_ema = math.nan
        self.buf = [0.0] * 5
        self.length = 0
        self.idx = 0
        self.reject_count = 0

    def _median5(self) -> float:
        return sorted(self.buf)[2]

    def update(self, m: float, valid: bool) -> float:
        if not valid:
            return self.dist_ema
        # Gating
        if math.isfinite(self.dist_ema):
            delta = abs(m - self.dist_ema)
            if delta > self.gate_threshold_m:
                self.reject_count += 1
                if self.reject_count < self.max_reject:
                    return self.dist_ema
                # target switch
                self.dist_ema = m
                self.reject_count = 0
                self.buf = [m] * 5
                self.length = 5
            else:
                self.reject_count = 0
        # Median(5)
        if self.length < 5:
            self.length += 1
        self.buf[self.idx] = m
        self.idx = (self.idx + 1) % 5
        med = m if self.length < 5 else self._median5()
        # EMA
        if math.isnan(self.dist_ema):
            self.dist_ema = med
        else:
            self.dist_ema = self.alpha * med + (1.0 - self.alpha) * self.dist_ema
        return self.dist_ema


# ---------------------------
# CSV column normalization
# ---------------------------

_COL_ALIASES = {
    "timestamp_ms": ["timestamp_ms", "dev_ts_ms", "t_ms"],
    "raw_m":        ["raw_m", "rawDistanceM", "distance_raw_m"],
    "status":       ["status", "meas_status", "measStatus"],
    "fps":          ["fps"],
    "est_m":        ["est_m", "filteredDistanceM"],
    "reject_reason":["reject_reason"],
}


def normalize_columns(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()
    for canonical, aliases in _COL_ALIASES.items():
        if canonical in out.columns:
            continue
        for alt in aliases:
            if alt in out.columns:
                out = out.rename(columns={alt: canonical})
                break
    return out


def run_tracker_on_dataframe(df: pd.DataFrame, cfg: ABConfig) -> pd.DataFrame:
    df = normalize_columns(df)
    tracker = RangeTracker(cfg)
    rows: List[Dict[str, Any]] = []
    for _, row in df.iterrows():
        z = float(row["raw_m"]) if pd.notna(row.get("raw_m")) else math.nan
        st = int(row["status"]) if pd.notna(row.get("status")) else MEAS_TIMEOUT
        fps = float(row["fps"]) if "fps" in row and pd.notna(row["fps"]) else 0.0
        out = tracker.step(int(row["timestamp_ms"]), z, st, fps)
        rows.append(asdict(out))
    return pd.DataFrame(rows)


def run_baseline_on_dataframe(df: pd.DataFrame,
                              alpha: float = 0.25,
                              gate_threshold_m: float = 10.0,
                              max_reject: int = 5) -> pd.DataFrame:
    """Chay baseline (Gating+Median+EMA) tren cung raw data nhu alpha-beta.
    Output co cung schema co ban voi run_tracker_on_dataframe.
    """
    df = normalize_columns(df)
    f = BaselineFilter(alpha=alpha, gate_threshold_m=gate_threshold_m, max_reject=max_reject)
    rows: List[Dict[str, Any]] = []
    for _, row in df.iterrows():
        z = float(row["raw_m"]) if pd.notna(row.get("raw_m")) else math.nan
        st = int(row["status"]) if pd.notna(row.get("status")) else MEAS_TIMEOUT
        valid = (st == MEAS_OK) and math.isfinite(z)
        est = f.update(z, valid)
        rows.append({
            "sampleTimeMs": int(row["timestamp_ms"]),
            "rawDistanceM": z,
            "filteredDistanceM": est,
            "measStatus": st,
            "hasEstimate": math.isfinite(est),
        })
    return pd.DataFrame(rows)
