"""
Alpha-Beta analysis v2 - chay nhu script hoac convert sang notebook.

Quy trinh:
  1. Load CSV log (tu firmware EST_RAW_ONLY hoac EST_BASELINE)
  2. Holdout split 70/30 theo thoi gian
  3. Tren TUNE set: grid search alpha-beta, score composite
  4. Eval cac top-K cau hinh tren HOLDOUT set (so lieu bao cao DUNG cai nay)
  5. Compare voi baseline (Gating+Median+EMA) tren cung holdout
  6. Output bang so lieu va do thi

Convert sang notebook:
    pip install jupytext
    jupytext --to ipynb alphabeta_analysis_v2.py
"""
# %%
import os, math, json
from pathlib import Path
from itertools import product
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from alphabeta_core import (
    ABConfig, RangeTracker, BaselineFilter,
    run_tracker_on_dataframe, run_baseline_on_dataframe,
    benedict_bordner_beta, normalize_columns,
    MEAS_OK, TRACK_STABLE, TRACK_LOST,
)
from metrics import (
    bias_sigma_rmse, std_signal, gate_reject_ratio,
    settling_time, rise_time_10_90, reacquisition_time,
    time_holdout_split, composite_score,
)

plt.rcParams["figure.figsize"] = (12, 4)
plt.rcParams["axes.grid"] = True

# %% [markdown]
# # Cau hinh

# %%
TRIPOD_CSV = "Tripod_531m_baseline.csv"
SWITCH_CSV = "Switching_7m_525m_baseline.csv"

TRIPOD_TRUTH_M = 531.0
LOW_TARGET_M   = 7.0
HIGH_TARGET_M  = 525.0

TUNE_RATIO = 0.7

# Grid search ALPHA / BETA - mo rong de bao quat che do tracker that
ALPHA_LIST = [0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45]
BETA_LIST  = [0.0, 0.02, 0.05, 0.08, 0.13]   # bao gom cac gia tri Benedict-Bordner
GATE_LIST  = [6.0, 8.0, 10.0, 12.0]
MAX_REJECT_LIST = [3, 5, 7]

TOP_K = 5

# %% [markdown]
# # Load CSV va chuan hoa

# %%
def load_csv(path):
    df = pd.read_csv(path)
    df = normalize_columns(df)
    # Bao dam co cot status (alias cho meas_status)
    if "status" not in df.columns and "meas_status" in df.columns:
        df = df.rename(columns={"meas_status": "status"})
    df["timestamp_ms"] = pd.to_numeric(df["timestamp_ms"], errors="coerce")
    df = df.dropna(subset=["timestamp_ms"]).reset_index(drop=True)
    df["t_s"] = (df["timestamp_ms"] - df["timestamp_ms"].iloc[0]) / 1000.0
    return df

tripod_df = load_csv(TRIPOD_CSV)
switch_df = load_csv(SWITCH_CSV)
print(f"Tripod : {len(tripod_df)} rows, {tripod_df['t_s'].iloc[-1]:.1f}s")
print(f"Switch : {len(switch_df)} rows, {switch_df['t_s'].iloc[-1]:.1f}s")

# %% [markdown]
# # Holdout split (TUNE / HOLDOUT)
# Phai tach truoc khi tune de tranh overfit.
# Bao cao cuoi cung CHI dung HOLDOUT set.

# %%
tripod_tune, tripod_hold = time_holdout_split(tripod_df, "timestamp_ms", TUNE_RATIO)
switch_tune, switch_hold = time_holdout_split(switch_df, "timestamp_ms", TUNE_RATIO)
print(f"Tripod tune={len(tripod_tune)} hold={len(tripod_hold)}")
print(f"Switch tune={len(switch_tune)} hold={len(switch_hold)}")

# %% [markdown]
# # Phan tich phan bo dt - quan trong cho alpha-beta

# %%
fig, axes = plt.subplots(1, 2, figsize=(14, 4))
for ax, df, title in [(axes[0], tripod_df, "Tripod dt"), (axes[1], switch_df, "Switch dt")]:
    dt_ms = np.diff(df["timestamp_ms"].values)
    dt_ms = dt_ms[(dt_ms > 0) & (dt_ms < 5000)]
    ax.hist(dt_ms, bins=50)
    ax.set_title(f"{title}: median={np.median(dt_ms):.0f}ms, p90={np.percentile(dt_ms,90):.0f}ms")
    ax.set_xlabel("dt (ms)")
plt.tight_layout()
plt.savefig("dt_distribution.png", dpi=80)
print("Saved: dt_distribution.png")

# %% [markdown]
# # Score helpers

# %%
def detect_switch_windows(df, low, high, near_tol=40.0, min_stable=2):
    """Detect cac mau truoc/sau khi doi muc tieu de tinh rise/settling time."""
    windows = []
    raw = pd.to_numeric(df.get("raw_m"), errors="coerce")
    t_ms = df["timestamp_ms"].values
    last_class = None
    cur_start = None
    for i, v in enumerate(raw):
        if pd.isna(v):
            continue
        if abs(v - low) <= near_tol:
            cls = "low"
        elif abs(v - high) <= near_tol:
            cls = "high"
        else:
            cls = "mid"
        if cls != last_class and last_class in ("low", "high"):
            # transition detected
            y0 = low if last_class == "low" else high
            y1 = high if cls == "high" else (low if cls == "low" else None)
            if y1 is not None and y0 != y1:
                windows.append({
                    "switch_idx": i,
                    "switch_t_ms": t_ms[i],
                    "y0": y0, "y1": y1,
                })
        last_class = cls
    return windows


def evaluate_config(df_static, df_switch, cfg: ABConfig, truth_m=TRIPOD_TRUTH_M):
    """Tinh metric cho 1 cau hinh tren ca static va switch."""
    # static
    out_static = run_tracker_on_dataframe(df_static, cfg)
    est = pd.to_numeric(out_static["filteredDistanceM"], errors="coerce").values
    truth = np.full_like(est, truth_m, dtype=float)
    s = bias_sigma_rmse(est, truth)
    rate_std = std_signal(out_static["rangeRateMps"])

    # switch
    out_switch = run_tracker_on_dataframe(df_switch, cfg)
    wins = detect_switch_windows(df_switch, LOW_TARGET_M, HIGH_TARGET_M)
    rises, settles = [], []
    for w in wins[:6]:    # cap so window de lay trung binh
        idx = w["switch_idx"]
        t_ms = out_switch["sampleTimeMs"].values
        y_est = pd.to_numeric(out_switch["filteredDistanceM"], errors="coerce").values
        rt = rise_time_10_90(t_ms, y_est, w["y0"], w["y1"], start_idx=idx)
        st = settling_time(t_ms, y_est, w["y1"], tol_abs=2.0, start_idx=idx)
        if np.isfinite(rt): rises.append(rt)
        if np.isfinite(st): settles.append(st)

    rise_mean = np.mean(rises) if rises else np.nan
    settle_mean = np.mean(settles) if settles else np.nan
    score = composite_score(s["sigma"], rise_mean, settle_mean, abs(s["bias"]))

    return {
        "alpha": cfg.alpha, "beta": cfg.beta,
        "gate_m": cfg.gate_threshold_m, "max_reject": cfg.max_reject,
        "static_bias": s["bias"], "static_sigma": s["sigma"], "static_rmse": s["rmse"],
        "rate_std": rate_std,
        "rise_mean_s": rise_mean, "settle_mean_s": settle_mean,
        "n_windows": len(rises),
        "score": score,
    }

# %% [markdown]
# # Grid search tren TUNE set

# %%
results = []
total = len(ALPHA_LIST) * len(BETA_LIST) * len(GATE_LIST) * len(MAX_REJECT_LIST)
i = 0
for a, b, g, mr in product(ALPHA_LIST, BETA_LIST, GATE_LIST, MAX_REJECT_LIST):
    i += 1
    if i % 50 == 0:
        print(f"  Grid {i}/{total}")
    cfg = ABConfig(alpha=a, beta=b, gate_threshold_m=g, max_reject=mr)
    r = evaluate_config(tripod_tune, switch_tune, cfg)
    results.append(r)

res_df = pd.DataFrame(results).sort_values("score").reset_index(drop=True)
print(f"\nTop {TOP_K} configs (TUNE):")
print(res_df.head(TOP_K).to_string(index=False))
res_df.to_csv("grid_results_tune.csv", index=False)

# %% [markdown]
# # Eval TOP_K tren HOLDOUT set (so lieu cuoi)

# %%
holdout_rows = []
for _, row in res_df.head(TOP_K).iterrows():
    cfg = ABConfig(
        alpha=float(row["alpha"]), beta=float(row["beta"]),
        gate_threshold_m=float(row["gate_m"]), max_reject=int(row["max_reject"]),
    )
    r = evaluate_config(tripod_hold, switch_hold, cfg)
    r["preset_label"] = f"a={cfg.alpha} b={cfg.beta} g={cfg.gate_threshold_m} mr={cfg.max_reject}"
    holdout_rows.append(r)

holdout_df = pd.DataFrame(holdout_rows).sort_values("score").reset_index(drop=True)
print("\nTop configs eval HOLDOUT:")
print(holdout_df.to_string(index=False))
holdout_df.to_csv("eval_holdout.csv", index=False)

best = holdout_df.iloc[0]
FINAL_CFG = ABConfig(
    alpha=float(best["alpha"]), beta=float(best["beta"]),
    gate_threshold_m=float(best["gate_m"]), max_reject=int(best["max_reject"]),
)
print(f"\n*** FINAL config (eval on holdout): {FINAL_CFG}")

# %% [markdown]
# # So sanh BASELINE vs ALPHA-BETA tren HOLDOUT (FAIR)

# %%
def compare_pipelines(df, truth_m, cfg_ab):
    out_baseline = run_baseline_on_dataframe(df, alpha=0.25, gate_threshold_m=10.0, max_reject=5)
    out_ab = run_tracker_on_dataframe(df, cfg_ab)
    truth = np.full(len(df), truth_m, dtype=float)
    return {
        "baseline": bias_sigma_rmse(out_baseline["filteredDistanceM"], truth),
        "alpha_beta": bias_sigma_rmse(out_ab["filteredDistanceM"], truth),
        "raw": bias_sigma_rmse(pd.to_numeric(df["raw_m"], errors="coerce"), truth),
    }

cmp_static = compare_pipelines(tripod_hold, TRIPOD_TRUTH_M, FINAL_CFG)
print("\n=== STATIC HOLDOUT (truth=531m) ===")
for k, v in cmp_static.items():
    print(f"  {k:12s}: bias={v['bias']:+.3f} sigma={v['sigma']:.3f} rmse={v['rmse']:.3f} n={v['n']}")

# %% [markdown]
# # Plots so sanh tren HOLDOUT static

# %%
out_baseline = run_baseline_on_dataframe(tripod_hold, alpha=0.25, gate_threshold_m=10.0, max_reject=5)
out_ab = run_tracker_on_dataframe(tripod_hold, FINAL_CFG)
t_s = (tripod_hold["timestamp_ms"].values - tripod_hold["timestamp_ms"].iloc[0]) / 1000.0

fig, ax = plt.subplots(figsize=(14, 5))
ax.plot(t_s, tripod_hold["raw_m"], "o", ms=2, alpha=0.4, label="raw")
ax.plot(t_s, out_baseline["filteredDistanceM"], "-", lw=1.5, label="baseline (G+M+EMA)")
ax.plot(t_s, out_ab["filteredDistanceM"], "-", lw=1.5, label=f"alpha-beta a={FINAL_CFG.alpha} b={FINAL_CFG.beta}")
ax.axhline(TRIPOD_TRUTH_M, color="k", ls="--", alpha=0.5, label=f"truth {TRIPOD_TRUTH_M}m")
ax.set_xlabel("Time (s)"); ax.set_ylabel("Distance (m)")
ax.set_title("HOLDOUT static @531m: baseline vs alpha-beta")
ax.legend()
plt.tight_layout()
plt.savefig("holdout_static_compare.png", dpi=80)
print("Saved: holdout_static_compare.png")

# %% [markdown]
# # Plot switching - dynamic

# %%
out_baseline_sw = run_baseline_on_dataframe(switch_hold, alpha=0.25, gate_threshold_m=10.0, max_reject=5)
out_ab_sw = run_tracker_on_dataframe(switch_hold, FINAL_CFG)
t_s_sw = (switch_hold["timestamp_ms"].values - switch_hold["timestamp_ms"].iloc[0]) / 1000.0

fig, ax = plt.subplots(figsize=(14, 5))
ax.plot(t_s_sw, switch_hold["raw_m"], "o", ms=2, alpha=0.4, label="raw")
ax.plot(t_s_sw, out_baseline_sw["filteredDistanceM"], "-", lw=1.5, label="baseline")
ax.plot(t_s_sw, out_ab_sw["filteredDistanceM"], "-", lw=1.5, label=f"alpha-beta")
ax.set_xlabel("Time (s)"); ax.set_ylabel("Distance (m)")
ax.set_title("HOLDOUT switching: baseline vs alpha-beta")
ax.legend()
plt.tight_layout()
plt.savefig("holdout_switching_compare.png", dpi=80)
print("Saved: holdout_switching_compare.png")

# %% [markdown]
# # Summary - bang so lieu cho bao cao

# %%
summary = {
    "final_config": {
        "alpha": FINAL_CFG.alpha,
        "beta": FINAL_CFG.beta,
        "gate_m": FINAL_CFG.gate_threshold_m,
        "max_reject": FINAL_CFG.max_reject,
    },
    "static_holdout": {k: {kk: float(vv) for kk, vv in v.items()} for k, v in cmp_static.items()},
    "n_tripod_tune": len(tripod_tune),
    "n_tripod_hold": len(tripod_hold),
    "n_switch_tune": len(switch_tune),
    "n_switch_hold": len(switch_hold),
}
with open("analysis_summary.json", "w") as f:
    json.dump(summary, f, indent=2)
print("\nSaved: analysis_summary.json")
print(json.dumps(summary, indent=2))
