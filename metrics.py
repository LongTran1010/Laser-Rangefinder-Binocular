import numpy as np
import pandas as pd

def valid_pair(est, truth):
    mask = np.isfinite(est) & np.isfinite(truth)
    return est[mask], truth[mask]

def bias_sigma_rmse(est, truth):
    est = np.asarray(est, dtype=float)
    truth = np.asarray(truth, dtype=float)
    est, truth = valid_pair(est, truth)

    if len(est) == 0:
        return {"bias": np.nan, "sigma": np.nan, "rmse": np.nan}

    err = est - truth
    return {
        "bias": float(np.mean(err)),
        "sigma": float(np.std(err, ddof=1)) if len(err) > 1 else 0.0,
        "rmse": float(np.sqrt(np.mean(err**2))),
    }

def availability(est):
    est = np.asarray(est, dtype=float)
    if len(est) == 0:
        return np.nan
    return float(np.mean(np.isfinite(est)))

def std_signal(x):
    x = np.asarray(x, dtype=float)
    x = x[np.isfinite(x)]
    if len(x) <= 1:
        return np.nan
    return float(np.std(x, ddof=1))


def settling_time(t_ms, y, final_value, tol_abs=2.0, start_idx=0) -> float:
    """Thoi gian (s) tu start_idx den khi |y - final_value| <= tol_abs on dinh.

    Bo qua cac mau NaN trong khi kiem tra on dinh (chi xet cac mau huu han).
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

def rise_time_10_90(
    t_ms,
    y,
    y0,
    y1,
    start_idx=0,
):
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

def all_reacquisition_events(t_ms, track_state,
                              lost_state: int = 3,
                              locked_state: int = 2) -> List[float]:
    """Tim tat ca events LOST -> STABLE, tra list thoi gian tuong ung (s).
    Dung cho DT5 co nhieu lan che TC22 lien tiep.
    """
    times = []
    i = 0
    while i < len(track_state):
        t = reacquisition_time(t_ms, track_state, start_idx=i,
                                lost_state=lost_state,
                                locked_state=locked_state)
        if np.isnan(t):
            break
        times.append(t)
        # Advance past this event: tim lai vi tri STABLE tiep theo
        state = np.asarray(track_state, dtype=int)
        while i < len(state) and state[i] != locked_state:
            i += 1
        # skip qua vung STABLE cho toi khi thay LOST khac
        while i < len(state) and state[i] == locked_state:
            i += 1
    return times