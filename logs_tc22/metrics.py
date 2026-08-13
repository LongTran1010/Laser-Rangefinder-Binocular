"""Metric functions cho danh gia tracking distance.

Chia 3 nhom:
  1. Static metrics: bias, sigma, RMSE, availability (do tinh)
  2. Dynamic / step-response: rise time, settling time, reacquisition time
  3. Quality: gate reject ratio, false accept/reject (can ground truth)
Kem ham holdout split de evaluate fair.
"""
from typing import Optional, Tuple, Dict, List
import numpy as np
import pandas as pd


def valid_pair(est, truth):
    est = np.asarray(est, dtype=float)
    truth = np.asarray(truth, dtype=float)
    mask = np.isfinite(est) & np.isfinite(truth)
    return est[mask], truth[mask]


# ---------------------------
# 1. Static metrics
# ---------------------------

def bias_sigma_rmse(est, truth) -> Dict[str, float]:
    est, truth = valid_pair(est, truth)
    if len(est) == 0:
        return {"bias": np.nan, "sigma": np.nan, "rmse": np.nan, "n": 0}
    err = est - truth
    return {
        "bias":  float(np.mean(err)),
        "sigma": float(np.std(err, ddof=1)) if len(err) > 1 else 0.0,
        "rmse":  float(np.sqrt(np.mean(err**2))),
        "n":     int(len(err)),
    }


def availability(est) -> float:
    est = np.asarray(est, dtype=float)
    if len(est) == 0:
        return np.nan
    return float(np.mean(np.isfinite(est)))


def std_signal(x) -> float:
    x = np.asarray(x, dtype=float)
    x = x[np.isfinite(x)]
    if len(x) <= 1:
        return np.nan
    return float(np.std(x, ddof=1))


def gate_reject_ratio(rejected_by_gate) -> float:
    s = pd.Series(rejected_by_gate).astype(float).dropna()
    if len(s) == 0:
        return np.nan
    return float(s.mean())


# ---------------------------
# 2. Dynamic / step-response
# ---------------------------

def settling_time(t_ms, y, final_value, tol_abs=2.0, start_idx=0) -> float:
    """Thoi gian (s) tu start_idx den khi |y - final_value| <= tol_abs on dinh.

    Bo qua cac mau NaN khi kiem tra on dinh (chi xet mau huu han).
    Truoc day, neu co bat ky NaN nao trong y[i:] thi np.abs(NaN) <= tol tra False
    khien ham khong bao gio return -> tra NaN sai o kich ban DT3/DT4 co mat tin hieu.
    """
    t_ms = np.asarray(t_ms)
    y = np.asarray(y, dtype=float)
    for i in range(start_idx, len(y)):
        if not np.isfinite(y[i]):
            continue
        tail = y[i:]
        finite_mask = np.isfinite(tail)
        if finite_mask.sum() == 0:
            continue
        if np.all(np.abs(tail[finite_mask] - final_value) <= tol_abs):
            return float((t_ms[i] - t_ms[start_idx]) / 1000.0)
    return np.nan


def rise_time_10_90(t_ms, y, y0, y1, start_idx=0) -> float:
    """Thoi gian tu 10% -> 90% bien do giua y0 va y1."""
    t_ms = np.asarray(t_ms)
    y = np.asarray(y, dtype=float)
    lo = y0 + 0.1 * (y1 - y0)
    hi = y0 + 0.9 * (y1 - y0)
    t_lo = None
    t_hi = None
    for i in range(start_idx, len(y)):
        if not np.isfinite(y[i]):
            continue
        if t_lo is None and ((y0 < y1 and y[i] >= lo) or (y0 > y1 and y[i] <= lo)):
            t_lo = t_ms[i]
        if t_lo is not None and ((y0 < y1 and y[i] >= hi) or (y0 > y1 and y[i] <= hi)):
            t_hi = t_ms[i]
            break
    if t_lo is None or t_hi is None:
        return np.nan
    return float((t_hi - t_lo) / 1000.0)


def reacquisition_time(t_ms, track_state, start_idx=0,
                       lost_state: int = 3, locked_state: int = 2) -> float:
    """Thoi gian tu LOST -> STABLE/LOCKED tinh tu start_idx (chi tim 1 event)."""
    t_ms = np.asarray(t_ms)
    state = np.asarray(track_state, dtype=int)
    saw_lost = False
    t_lost = None
    for i in range(start_idx, len(state)):
        if not saw_lost and state[i] == lost_state:
            saw_lost = True
            t_lost = t_ms[i]
        elif saw_lost and state[i] == locked_state:
            return float((t_ms[i] - t_lost) / 1000.0)
    return np.nan


def all_reacquisition_events(t_ms, track_state,
                              lost_state: int = 3,
                              locked_state: int = 2) -> List[float]:
    """Tim TAT CA events LOST -> STABLE trong chuoi track_state.
    Dung cho DT5 (5 lan lap che TC22 lien tiep).

    Returns:
        List[float]: danh sach thoi gian reacquisition (giay) cua tung event.
                     Tra list rong neu khong co event nao.
    """
    t_ms = np.asarray(t_ms)
    state = np.asarray(track_state, dtype=int)
    events: List[float] = []
    i = 0
    n = len(state)
    while i < n:
        # Tim diem LOST bat dau
        while i < n and state[i] != lost_state:
            i += 1
        if i >= n:
            break
        t_lost = t_ms[i]
        # Tim diem STABLE ke tiep
        while i < n and state[i] != locked_state:
            i += 1
        if i >= n:
            break
        t_stable = t_ms[i]
        events.append(float((t_stable - t_lost) / 1000.0))
        # Skip qua vung STABLE (khong ghi nhan lai cung 1 event)
        while i < n and state[i] == locked_state:
            i += 1
    return events


# ---------------------------
# 3. False accept / reject
# ---------------------------

def false_accept_rate(residual_m, gate_threshold_m, ground_truth_outlier_mask) -> float:
    residual = np.asarray(residual_m, dtype=float)
    mask_outlier = np.asarray(ground_truth_outlier_mask, dtype=bool)
    accepted_outliers = mask_outlier & (np.abs(residual) <= gate_threshold_m)
    if mask_outlier.sum() == 0:
        return np.nan
    return float(accepted_outliers.sum() / mask_outlier.sum())


def false_reject_rate(rejected_by_gate, ground_truth_inlier_mask) -> float:
    rejected = np.asarray(rejected_by_gate, dtype=bool)
    mask_inlier = np.asarray(ground_truth_inlier_mask, dtype=bool)
    bad_rejects = rejected & mask_inlier
    if mask_inlier.sum() == 0:
        return np.nan
    return float(bad_rejects.sum() / mask_inlier.sum())


# ---------------------------
# 4. Holdout split (time-series, KHONG random)
# ---------------------------

def time_holdout_split(df: pd.DataFrame,
                       time_col: str = "timestamp_ms",
                       tune_ratio: float = 0.7) -> Tuple[pd.DataFrame, pd.DataFrame]:
    df = df.sort_values(time_col).reset_index(drop=True)
    cut = int(len(df) * tune_ratio)
    return df.iloc[:cut].copy(), df.iloc[cut:].copy()


def kfold_block_split(df: pd.DataFrame,
                      time_col: str = "timestamp_ms",
                      k: int = 5) -> List[Tuple[pd.DataFrame, pd.DataFrame]]:
    df = df.sort_values(time_col).reset_index(drop=True)
    n = len(df)
    block = n // k
    splits = []
    for i in range(k):
        start = i * block
        end = (i + 1) * block if i < k - 1 else n
        val = df.iloc[start:end].copy()
        train = pd.concat([df.iloc[:start], df.iloc[end:]]).copy()
        splits.append((train, val))
    return splits


# ---------------------------
# 5. Composite scoring (cang NHO cang tot)
# ---------------------------

def composite_score(static_sigma: float,
                    rise_time_s: float,
                    settling_time_s: float,
                    bias_abs: float,
                    weights: Optional[Dict[str, float]] = None) -> float:
    w = weights or {"sigma": 1.0, "rise": 0.5, "settle": 0.5, "bias": 1.0}
    def safe(v, default=10.0):
        return v if (v is not None and np.isfinite(v)) else default
    return (w["sigma"]  * safe(static_sigma, 50.0)
          + w["rise"]   * safe(rise_time_s, 5.0)
          + w["settle"] * safe(settling_time_s, 10.0)
          + w["bias"]   * abs(safe(bias_abs, 50.0)))
