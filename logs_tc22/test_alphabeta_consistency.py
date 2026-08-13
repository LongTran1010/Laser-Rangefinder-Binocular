"""Unit tests cho RangeTracker / BaselineFilter Python.

Mục tiêu: bảo vệ chống regression khi sửa firmware -> Python lệch.
Khi build firmware, có thể chạy unit test C++ tương ứng và so sánh
golden output (CSV) — xem test/ trong Module/.

Chạy: python -m pytest test_alphabeta_consistency.py -v
"""
import math
import pytest
from alphabeta_core import (
    ABConfig, RangeTracker, BaselineFilter,
    MEAS_OK, MEAS_NO_SIGNAL, MEAS_TIMEOUT,
    TRACK_SEARCHING, TRACK_CANDIDATE, TRACK_STABLE, TRACK_LOST,
    benedict_bordner_beta, assert_matches_firmware_defaults,
)


# ---------------------------
# Defaults consistency
# ---------------------------

def test_default_matches_firmware():
    """Default ABConfig phải khớp PRESET_D_AB_BALANCED."""
    cfg = ABConfig()
    assert math.isclose(cfg.alpha, 0.30)
    assert math.isclose(cfg.beta, benedict_bordner_beta(0.30), rel_tol=1e-3)
    assert cfg.gate_threshold_m == 10.0
    assert cfg.max_reject == 5
    assert_matches_firmware_defaults(cfg)


def test_assert_catches_drift():
    cfg = ABConfig(alpha=0.55)
    with pytest.raises(AssertionError):
        assert_matches_firmware_defaults(cfg)


def test_benedict_bordner():
    # Sanity check công thức
    assert math.isclose(benedict_bordner_beta(0.30), 0.30**2 / (2-0.30), rel_tol=1e-9)
    assert math.isclose(benedict_bordner_beta(0.20), 0.0222, abs_tol=1e-4)


# ---------------------------
# Tracker behavior
# ---------------------------

def test_init_on_first_valid():
    t = RangeTracker(ABConfig())
    out = t.step(timestamp_ms=1000, z_m=100.0, status=MEAS_OK, fps=2.5)
    assert out.hasEstimate
    assert math.isclose(out.filteredDistanceM, 100.0)
    assert out.trackState == TRACK_CANDIDATE


def test_static_target_converges():
    """Mục tiêu tĩnh ở 100m + nhiễu nhỏ -> filtered xấp xỉ 100m."""
    t = RangeTracker(ABConfig())
    import random
    random.seed(42)
    last_est = None
    ts = 0
    for _ in range(200):
        ts += 400
        z = 100.0 + random.gauss(0, 0.5)
        out = t.step(ts, z, MEAS_OK, 2.5)
        last_est = out.filteredDistanceM
    assert abs(last_est - 100.0) < 1.0, f"Final estimate {last_est} far from 100m"


def test_step_change_triggers_reinit():
    """Sau MAX_REJECT mẫu liên tiếp ngoài gate -> reinit."""
    cfg = ABConfig(max_reject=3, gate_threshold_m=10.0)
    t = RangeTracker(cfg)
    # init ở 100m
    for _ in range(5):
        t.step(t.last_ts_ms + 400 if t.last_ts_ms else 0, 100.0, MEAS_OK, 2.5)
    pre_x = t.x
    # spike 200m liên tục -> sau max_reject lần phải reinit về 200
    for _ in range(cfg.max_reject):
        t.step(t.last_ts_ms + 400, 200.0, MEAS_OK, 2.5)
    assert abs(t.x - 200.0) < 5.0, f"Expected reinit to ~200m, got {t.x}"


def test_predict_only_on_reject():
    """B2: state vẫn predict tiếp khi reject (không freeze)."""
    cfg = ABConfig(beta=0.05, gate_threshold_m=5.0, max_reject=10)
    t = RangeTracker(cfg)
    # init với velocity từ 2 mẫu khác nhau cách nhau 1m / 0.4s = 2.5 m/s
    t.step(0,    100.0, MEAS_OK, 2.5)
    t.step(400,  101.0, MEAS_OK, 2.5)
    t.step(800,  102.0, MEAS_OK, 2.5)
    # spike rất xa -> reject; expect x đẩy theo predict
    x_before = t.x
    v_before = t.v
    t.step(1200, 999.0, MEAS_OK, 2.5)
    # state KHÔNG được giữ nguyên (B2): phải = x_before + v_before * dt
    assert not math.isclose(t.x, x_before), \
        "x phải predict tiếp khi reject, không được freeze"


def test_velocity_decay_on_invalid():
    """B4: velocity decay khi nhận nhiều invalid liên tiếp."""
    cfg = ABConfig(beta=0.10, invalid_velocity_decay=0.5,
                   max_predict_hold_samples=4)
    t = RangeTracker(cfg)
    t.step(0,    100.0, MEAS_OK, 2.5)
    t.step(400,  102.0, MEAS_OK, 2.5)
    t.step(800,  104.0, MEAS_OK, 2.5)
    v_init = abs(t.v)
    # 3 invalid liên tiếp -> v giảm theo (0.5)^3
    t.step(1200, math.nan, MEAS_NO_SIGNAL, 2.5)
    t.step(1600, math.nan, MEAS_NO_SIGNAL, 2.5)
    t.step(2000, math.nan, MEAS_NO_SIGNAL, 2.5)
    assert abs(t.v) < v_init * 0.2 + 1e-3, f"v không decay đủ: {t.v} vs init {v_init}"
    # sau hold limit -> v=0
    t.step(2400, math.nan, MEAS_NO_SIGNAL, 2.5)
    assert t.v == 0.0


def test_invalid_then_valid_recovers():
    cfg = ABConfig()
    t = RangeTracker(cfg)
    t.step(0, 100.0, MEAS_OK, 2.5)
    # vài invalid
    for k in range(3):
        t.step(400 * (k+1), math.nan, MEAS_TIMEOUT, 2.5)
    # valid mới ở 100.5m -> phải accept
    out = t.step(2000, 100.5, MEAS_OK, 2.5)
    assert out.hasEstimate
    assert not out.rejectedByGate


# ---------------------------
# Baseline filter
# ---------------------------

def test_baseline_smoothing():
    f = BaselineFilter(alpha=0.25, gate_threshold_m=10.0, max_reject=5)
    # warmup
    for _ in range(10):
        f.update(100.0, True)
    assert abs(f.dist_ema - 100.0) < 0.5


def test_baseline_rejects_spike():
    f = BaselineFilter(alpha=0.25, gate_threshold_m=10.0, max_reject=5)
    for _ in range(10):
        f.update(100.0, True)
    pre = f.dist_ema
    out = f.update(500.0, True)   # spike
    # Phải bị reject -> giữ giá trị cũ
    assert math.isclose(out, pre, abs_tol=1e-3)


def test_baseline_accepts_target_switch():
    f = BaselineFilter(alpha=0.25, gate_threshold_m=10.0, max_reject=3)
    for _ in range(10):
        f.update(100.0, True)
    # 3 spike liên tiếp ở 200m -> coi là switch
    for _ in range(3):
        f.update(200.0, True)
    assert abs(f.dist_ema - 200.0) < 1.0


# ---------------------------
# Dt guard regression tests (fix minDtS = 0.10)
# ---------------------------

def test_default_min_dt_s_is_0_10():
    """minDtS phải = 0.10. Nếu ai sửa về 0.001, test fail ngay."""
    cfg = ABConfig()
    assert math.isclose(cfg.min_dt_s, 0.10, abs_tol=1e-9), (
        f"min_dt_s = {cfg.min_dt_s}, expected 0.10. "
        "Sửa về 0.001 sẽ tái hiện bug velocity spike ở bài 345m."
    )


def test_dt_guard_prevents_velocity_spike():
    """Kịch bản tái hiện bug 345m: 2 frame UART đến cách 3ms.
    Sau fix minDtS=0.10, velocity không được spike vì:
      (a) dt bị clamp về 0.10s (không phải 0.003s)
      (b) khi clamp xảy ra, velocity update bị skip (dtS > minDtS = False)
    """
    cfg = ABConfig()  # default min_dt_s = 0.10
    t = RangeTracker(cfg)
    # Mục tiêu tĩnh 345m, đo bình thường 400ms/frame
    t.step(0, 345.0, MEAS_OK, fps=2.5)
    t.step(400, 345.05, MEAS_OK, fps=2.5)
    t.step(800, 344.95, MEAS_OK, fps=2.5)
    # Frame bất thường đến chỉ sau 3ms (UART drain)
    out = t.step(803, 345.1, MEAS_OK, fps=2.5)
    # Trước fix: |rate| có thể > 9 m/s (beta/dt * residual)
    # Sau fix: |rate| phải < 0.5 m/s vì velocity update bị skip
    assert abs(out.rangeRateMps) < 0.5, (
        f"rate_mps = {out.rangeRateMps:.3f} m/s — kỳ vọng < 0.5. "
        "Nếu fail, kiểm tra điều kiện 'dtS > minDtS' trong AlphaBetaTracker.cpp."
    )


def test_dt_guard_does_not_freeze_normal_operation():
    """Đảm bảo guard không phá tracker khi dt bình thường (>= 100ms)."""
    cfg = ABConfig()
    t = RangeTracker(cfg)
    # Mục tiêu đi đều +1 m/s, dt = 400ms (bình thường)
    t.step(0, 100.0, MEAS_OK, fps=2.5)
    for i in range(1, 10):
        ts = i * 400
        z = 100.0 + 0.4 * i   # +0.4 m mỗi 400ms = +1 m/s
        out = t.step(ts, z, MEAS_OK, fps=2.5)
    # Velocity phải bám gần +1 m/s (alpha-beta cần vài frame hội tụ)
    assert 0.5 < out.rangeRateMps < 1.5, (
        f"rate_mps = {out.rangeRateMps:.3f} m/s — kỳ vọng quanh +1.0 m/s. "
        "Guard có thể đã làm freeze velocity sai."
    )


def test_dt_anomaly_does_not_destabilize_position():
    """Position estimate vẫn phải hợp lý ngay cả khi có dt anomaly liên tiếp."""
    cfg = ABConfig()
    t = RangeTracker(cfg)
    t.step(0, 345.0, MEAS_OK, fps=2.5)
    t.step(400, 345.0, MEAS_OK, fps=2.5)
    # 3 frame anomaly liên tiếp
    t.step(403, 345.05, MEAS_OK, fps=2.5)
    t.step(406, 344.95, MEAS_OK, fps=2.5)
    out = t.step(409, 345.0, MEAS_OK, fps=2.5)
    assert abs(out.filteredDistanceM - 345.0) < 1.0, (
        f"x = {out.filteredDistanceM:.3f} drift quá xa truth 345.0 sau dt anomaly."
    )


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
