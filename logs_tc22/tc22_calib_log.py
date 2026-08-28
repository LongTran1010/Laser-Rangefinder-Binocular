#!/usr/bin/env python3
"""
TC22 Bore-sight Calibration Logger
===================================
Ban song song voi tc22_mqtt_log.py, dung rieng cho bai test hieu chuan co khi (bore-sight).
KHONG can sua firmware — subscribe cung MQTT topic, chi khac cach xu ly va luu.

Workflow:
  1. Prompt metadata session (target, distance, weather, screw pitch...)
  2. Do R0 (median cua 100 samples) tai bia
  3. Chay N scan (default 3), moi scan gom M vi tri vit (default 9)
     - Moi vi tri: prompt vat vit -> ENTER -> tu dong log K samples (default 100)
     - Tinh hit-rate on-the-fly: HIT <=> valid AND |raw - R0| < tol
  4. Xuat 3 loai file cho moi session:
     - <session>_scan<N>.csv     : raw samples tu firmware (30 cot)
     - <session>_scan<N>_pos.csv : summary theo vi tri vit (hit-rate)
     - <session>_meta.json       : context (target, weather, mechanical, firmware...)

Usage:
  python tc22_calib_log.py --ip 192.168.88.56 --distance 100
  python tc22_calib_log.py --session my_calib_100m --n-scans 3 --n-positions 9

Author: Tran Thang Long - Dang Quoc Phu - cờ lốti
"""
import argparse
import csv
import json
import statistics
import sys
import time
from pathlib import Path
from datetime import datetime
from threading import Lock
from typing import List, Optional

import paho.mqtt.client as mqtt

# ---------------------------------------------------------------------
# Defaults (giu giong tc22_mqtt_log.py de tuong thich firmware hien tai)
# ---------------------------------------------------------------------
DEFAULT_HOST = "192.168.88.56"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "thesis/tc22/log"
DEFAULT_TOPIC_STATUS = "thesis/tc22/status"
DEFAULT_OUT_DIR = "logs_tc22/calibration"

# CSV_COLUMNS = giong tc22_mqtt_log.py + 2 cot calibration-specific o cuoi
CSV_COLUMNS = [
    "boot_id", "msg_seq", "dev_ts_ms", "host_ts",
    "mode", "raw_m", "est_m", "fps",
    "meas_status", "track_state", "has_estimate",
    "consecutive_valids", "consecutive_invalids",
    "last_good_ts_ms", "restart_count", "system_error",
    "rate_mps", "predicted_m", "residual_m",
    "rejected_by_gate", "reject_reason", "estimator_mode",
    "test_id",
    "cfg_alpha", "cfg_beta", "cfg_gate_m",
    "cfg_min_dt_s", "cfg_max_reject", "cfg_preset_id",
    "predict_hold_count",
    # Schema v4 (Proposal 1): tach sensor validity va estimator decision
    "estimator_decision",
    "fw_version", "schema_version",
    # ---- Calibration-specific (do Python them, khong tu firmware) ----
    "calib_scan_id", "calib_screw_pos_turns",
]

# CSV cho scan position summary
POS_CSV_COLUMNS = [
    "scan_id", "direction", "screw_pos_turns", "screw_pos_mrad",
    "start_time_iso", "end_time_iso",
    "samples_total", "hits", "hit_rate_pct",
    "raw_range_mean_m", "raw_range_std_m",
    "note",
]


# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------
def safe_name(text):
    return "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in str(text))


def prompt_default(msg, default=None, type_fn=str):
    """Prompt user, cho phep ENTER de dung default."""
    default_str = f" [{default}]" if default is not None else ""
    while True:
        try:
            raw = input(f"  {msg}{default_str}: ").strip()
        except EOFError:
            raw = ""
        if raw == "" and default is not None:
            return default
        try:
            return type_fn(raw)
        except (ValueError, TypeError):
            print(f"    Invalid input, expected {type_fn.__name__}, try again.")


def normalize_payload(data: dict) -> dict:
    """Giong ham normalize_payload trong tc22_mqtt_log.py.
    Copy toan bo 30 cot tu payload MQTT, khong loc."""
    row = {k: "" for k in CSV_COLUMNS}
    row["boot_id"] = data.get("boot_id", "boot_unknown")
    row["msg_seq"] = data.get("msg_seq", "")
    row["dev_ts_ms"] = data.get("dev_ts_ms", data.get("timestamp_ms", ""))
    row["host_ts"] = datetime.now().isoformat(timespec="milliseconds")
    row["mode"] = data.get("mode", "")
    row["raw_m"] = data.get("raw_m", data.get("distance_raw_m", ""))
    est = data.get("est_m", None)
    if est is None:
        est = data.get("distance_gated_m", data.get("distance_medema_m", ""))
    row["est_m"] = est
    row["fps"] = data.get("fps", "")
    row["meas_status"] = data.get("meas_status", data.get("status", ""))
    row["track_state"] = data.get("track_state", "")
    row["has_estimate"] = data.get("has_estimate", "")
    row["consecutive_valids"] = data.get("consecutive_valids", "")
    row["consecutive_invalids"] = data.get("consecutive_invalids", "")
    row["last_good_ts_ms"] = data.get("last_good_ts_ms", "")
    row["restart_count"] = data.get("restart_count", "")
    row["system_error"] = data.get("system_error", "")
    row["rate_mps"] = data.get("rate_mps", "")
    row["predicted_m"] = data.get("predicted_m", "")
    row["residual_m"] = data.get("residual_m", "")
    row["rejected_by_gate"] = data.get("rejected_by_gate", "")
    row["reject_reason"] = data.get("reject_reason", "")
    row["estimator_mode"] = data.get("estimator_mode", "")
    row["test_id"] = data.get("test_id", "")
    row["cfg_alpha"]      = data.get("cfg_alpha", "")
    row["cfg_beta"]       = data.get("cfg_beta", "")
    row["cfg_gate_m"]     = data.get("cfg_gate_m", "")
    row["cfg_min_dt_s"]   = data.get("cfg_min_dt_s", "")
    row["cfg_max_reject"] = data.get("cfg_max_reject", "")
    row["cfg_preset_id"]  = data.get("cfg_preset_id", "")
    row["predict_hold_count"] = data.get("predict_hold_count", "")
    # Schema v4: estimator_decision (Proposal 1)
    row["estimator_decision"] = data.get("estimator_decision", "")
    row["fw_version"]     = data.get("fw_version", "")
    row["schema_version"] = data.get("schema_version", "")
    return row


def is_hit(sample: dict, r0_m: float, tol_m: float) -> bool:
    """Rule: HIT khi valid=1 AND |raw - R0| < tol."""
    try:
        raw = float(sample.get("raw_m", ""))
    except (ValueError, TypeError):
        return False
    # Ap dung nhieu cach detect valid (tuy theo firmware version):
    #   - has_estimate = 1
    #   - meas_status = "VALID" hoac "OK"
    #   - raw_m > 0 (fallback)
    he = sample.get("has_estimate", "")
    ms = str(sample.get("meas_status", "")).upper()
    valid = False
    if str(he) in ("1", "True", "true"):
        valid = True
    elif ms in ("VALID", "OK", "1"):
        valid = True
    elif raw > 0.5:
        valid = True
    return valid and abs(raw - r0_m) < tol_m


# ---------------------------------------------------------------------
# Recorder: thread-safe buffer for MQTT samples
# ---------------------------------------------------------------------
class CalibRecorder:
    """Nhan message tu MQTT thread, chi giu khi dang recording."""
    def __init__(self, r0_nominal_m, hit_tol_m=2.0):
        self.r0_nominal = r0_nominal_m
        self.r0_measured: Optional[float] = None
        self.hit_tol_m = hit_tol_m
        self.lock = Lock()
        self.buffer: List[dict] = []
        self.recording = False
        self.target_samples = 0
        self.current_scan_id = 0
        self.current_screw_pos = 0

    def start(self, scan_id, screw_pos, n_samples):
        with self.lock:
            self.buffer.clear()
            self.recording = True
            self.target_samples = n_samples
            self.current_scan_id = scan_id
            self.current_screw_pos = screw_pos

    def stop(self):
        with self.lock:
            self.recording = False

    def push(self, row: dict):
        """Called from MQTT thread when new message arrives."""
        with self.lock:
            if self.recording and len(self.buffer) < self.target_samples:
                row["calib_scan_id"] = self.current_scan_id
                row["calib_screw_pos_turns"] = self.current_screw_pos
                self.buffer.append(row)

    def count(self):
        with self.lock:
            return len(self.buffer)

    def drain(self) -> List[dict]:
        """Move samples out of buffer, return them."""
        with self.lock:
            out = list(self.buffer)
            self.buffer.clear()
            self.recording = False
            return out

    def compute_stats(self, samples: List[dict]) -> dict:
        """Tinh hit-rate + range stats cho 1 lan record."""
        r0 = self.r0_measured if self.r0_measured is not None else self.r0_nominal
        hits = 0
        hit_ranges = []
        for s in samples:
            if is_hit(s, r0, self.hit_tol_m):
                hits += 1
                try:
                    hit_ranges.append(float(s.get("raw_m", "")))
                except (ValueError, TypeError):
                    pass
        total = len(samples)
        return {
            "samples_total": total,
            "hits": hits,
            "hit_rate_pct": 100.0 * hits / total if total > 0 else 0.0,
            "raw_range_mean_m": statistics.mean(hit_ranges) if hit_ranges else 0.0,
            "raw_range_std_m": statistics.stdev(hit_ranges) if len(hit_ranges) > 1 else 0.0,
        }


# ---------------------------------------------------------------------
# Metadata gathering
# ---------------------------------------------------------------------
def gather_metadata(args) -> dict:
    print("\n" + "=" * 60)
    print("SESSION METADATA (ENTER de dung default)")
    print("=" * 60)
    r0_nom = prompt_default("R0 nominal distance (m)", args.distance, float)
    target_size = prompt_default("Target size (cm, canh vuong)", 60, float)
    target_material = prompt_default("Target material",
                                     "bia cung trang matte tren khung go")
    target_shape = prompt_default("Target shape / marking",
                                  "vuong 60x60cm, chu thap den 5x60cm o giua")
    screw_pitch = prompt_default("Screw pitch (mm/turn)", 0.5, float)
    lever_arm = prompt_default("Lever arm L (mm)", 50, float)
    delta_h = prompt_default("Delta h laser-optic offset (mm)", 40, float)
    location = prompt_default("Location", "San DHBK TP.HCM")
    weather = prompt_default("Weather", "troi nang nhe, gio nhe")
    temp_c = prompt_default("Ambient temperature (C)", 28, float)
    humidity = prompt_default("Humidity (%)", 75, float)
    operator = prompt_default("Operator", "Tran Thang Long")
    tripod = prompt_default("Tripod / mount", "chan may 3 chan, cao 1.5m")
    notes = prompt_default("Extra notes", "")

    mrad_per_full = 1000.0 * screw_pitch / lever_arm
    mrad_per_eighth = mrad_per_full / 8.0

    print(f"\n  => mrad_per_full_turn   = {mrad_per_full:.4f}")
    print(f"  => mrad_per_eighth_turn = {mrad_per_eighth:.4f}")
    print(f"  => expected alpha at R0 = {(delta_h / r0_nom):.4f} mrad "
          f"(= {(delta_h / r0_nom / mrad_per_eighth):+.2f} eighth-turns)")

    return {
        "session_id": args.session,
        "date": datetime.now().strftime("%Y-%m-%d"),
        "start_time": datetime.now().isoformat(timespec="seconds"),
        "operator": operator,
        "location": location,
        "weather": weather,
        "ambient_temp_C": temp_c,
        "humidity_pct": humidity,
        "target": {
            "material": target_material,
            "size_cm": [target_size, target_size],
            "shape": target_shape,
            "mount": tripod,
        },
        "range": {
            "nominal_m": r0_nom,
            "measured_m": None,
            "uncertainty_m": None,
            "measurement_method": "TC22 median of pre-scan samples",
        },
        "mechanical": {
            "screw_pitch_mm_per_turn": screw_pitch,
            "lever_arm_L_mm": lever_arm,
            "delta_h_mm": delta_h,
            "mrad_per_full_turn": round(mrad_per_full, 4),
            "mrad_per_eighth_turn": round(mrad_per_eighth, 4),
            "expected_alpha_at_R0_mrad": round(delta_h / r0_nom, 4),
        },
        "firmware": {
            "env": "tc22_raw (expected)",
            "version": None,
            "schema_version": None,
            "first_boot_id": None,
        },
        "calibration_config": {
            "n_scans": args.n_scans,
            "n_positions_per_scan": args.n_positions,
            "samples_per_position": args.samples_per_pos,
            "hit_tolerance_m": args.hit_tol_m,
            "scan_step_eighth_turns": args.scan_step,
        },
        "scans": [],
        "notes": notes,
    }


# ---------------------------------------------------------------------
# Measurement routines
# ---------------------------------------------------------------------
def wait_recording(recorder: CalibRecorder, n_samples: int, timeout_s: float = 30.0):
    """Poll cho den khi du n_samples hoac timeout."""
    start = time.time()
    last_count = -1
    while recorder.count() < n_samples:
        if time.time() - start > timeout_s:
            print(f"\n    TIMEOUT: chi nhan duoc {recorder.count()}/{n_samples} samples")
            print(f"    Kiem tra firmware, WiFi, MQTT broker.")
            break
        c = recorder.count()
        if c != last_count:
            bar = "#" * (c * 40 // n_samples) + "-" * (40 - c * 40 // n_samples)
            print(f"\r    [{bar}] {c}/{n_samples}", end="", flush=True)
            last_count = c
        time.sleep(0.1)
    print()


def measure_r0(recorder: CalibRecorder, n_samples: int = 100) -> tuple:
    """Do R0 bang median cua N samples."""
    print("\n" + "=" * 60)
    print("BUOC 1: DO R0 (BASELINE)")
    print("=" * 60)
    print(f"  1. Nap firmware env 'tc22_raw' (KHONG bench!)")
    print(f"  2. Aim ong nhom vao TAM bia, khoa tripod")
    print(f"  3. Van vit calibration ve vi tri 0 (baseline)")
    print(f"  Se do {n_samples} samples de tinh R0 measured.")
    input("  ENTER khi san sang...")

    recorder.start(scan_id=0, screw_pos=0, n_samples=n_samples)
    wait_recording(recorder, n_samples, timeout_s=60.0)
    samples = recorder.drain()

    if len(samples) < 10:
        print(f"  CANH BAO: chi co {len(samples)} samples. Bo qua R0, dung nominal.")
        return None, None

    ranges = []
    for s in samples:
        try:
            r = float(s.get("raw_m", ""))
            if 0.5 < r < 1000:
                ranges.append(r)
        except (ValueError, TypeError):
            pass

    if len(ranges) < 10:
        print(f"  CANH BAO: chi co {len(ranges)}/{len(samples)} valid ranges.")
        return None, None

    r0 = statistics.median(ranges)
    r0_std = statistics.stdev(ranges) if len(ranges) > 1 else 0.0
    print(f"\n  R0_measured = {r0:.3f} m (sigma = {r0_std:.3f}, n_valid = {len(ranges)}/{len(samples)})")
    return r0, r0_std


def get_scan_positions(direction: str, n_positions: int, step: int) -> List[int]:
    """Tra ve list vi tri vit (1/8 turn units).
    Direction 'bottom-up': tu -N ... 0 ... +N (van vit tu dat len).
    Direction 'top-down' : nguoc lai.
    """
    half = n_positions // 2
    positions = list(range(-half * step, (half + 1) * step, step))
    if direction == "top-down":
        positions.reverse()
    return positions


def run_scan(recorder: CalibRecorder, scan_id: int, direction: str,
             positions: List[int], samples_per_pos: int,
             mrad_per_eighth: float,
             out_dir: Path, session: str) -> List[dict]:
    """Chay 1 scan, luu raw CSV + pos CSV. Tra ve list stats theo vi tri."""
    print("\n" + "=" * 60)
    print(f"SCAN {scan_id} — direction: {direction}")
    print(f"Positions (1/8 turns): {positions}")
    print(f"Samples/position: {samples_per_pos}")
    print("=" * 60)

    csv_path = out_dir / f"{session}_scan{scan_id}.csv"
    pos_path = out_dir / f"{session}_scan{scan_id}_pos.csv"

    csv_f = csv_path.open("w", newline="", encoding="utf-8")
    csv_w = csv.DictWriter(csv_f, fieldnames=CSV_COLUMNS)
    csv_w.writeheader()

    pos_f = pos_path.open("w", newline="", encoding="utf-8")
    pos_w = csv.DictWriter(pos_f, fieldnames=POS_CSV_COLUMNS)
    pos_w.writeheader()

    stats_list = []
    try:
        for idx, pos in enumerate(positions):
            pos_mrad = pos * mrad_per_eighth
            print(f"\n[{idx+1}/{len(positions)}] Van vit sang vi tri "
                  f"{pos:+d} (x 1/8 turn) = {pos_mrad:+.3f} mrad")
            note = input("    Ghi chu (ENTER bo qua): ").strip()
            input("    ENTER sau khi van xong vit...")

            start_ts = datetime.now()
            recorder.start(scan_id=scan_id, screw_pos=pos,
                          n_samples=samples_per_pos)
            wait_recording(recorder, samples_per_pos, timeout_s=30.0)
            samples = recorder.drain()
            end_ts = datetime.now()

            # Write raw samples
            for s in samples:
                csv_w.writerow(s)
            csv_f.flush()

            stats = recorder.compute_stats(samples)
            stats_list.append({"pos_turns": pos, "pos_mrad": pos_mrad, **stats})

            pos_row = {
                "scan_id": scan_id,
                "direction": direction,
                "screw_pos_turns": pos,
                "screw_pos_mrad": round(pos_mrad, 4),
                "start_time_iso": start_ts.isoformat(timespec="seconds"),
                "end_time_iso": end_ts.isoformat(timespec="seconds"),
                "samples_total": stats["samples_total"],
                "hits": stats["hits"],
                "hit_rate_pct": round(stats["hit_rate_pct"], 2),
                "raw_range_mean_m": round(stats["raw_range_mean_m"], 3),
                "raw_range_std_m": round(stats["raw_range_std_m"], 3),
                "note": note,
            }
            pos_w.writerow(pos_row)
            pos_f.flush()

            print(f"    -> HIT: {stats['hits']}/{stats['samples_total']} "
                  f"({stats['hit_rate_pct']:.1f}%)  "
                  f"range mean={stats['raw_range_mean_m']:.2f}m "
                  f"sigma={stats['raw_range_std_m']:.3f}m")
    finally:
        csv_f.close()
        pos_f.close()

    print(f"\nSaved: {csv_path.name}, {pos_path.name}")
    return stats_list


def estimate_optimal_position_knife_edge(stats_list: List[dict]) -> Optional[float]:
    """Uoc luong optimal position bang knife-edge:
    Tim 2 vi tri hit-rate ~50% (mot ben trai, mot ben phai peak) -> trung diem.
    Fallback: weighted centroid (neu khong tim duoc 50% crossing).
    """
    if len(stats_list) < 3:
        return None
    positions = [s["pos_mrad"] for s in stats_list]
    rates = [s["hit_rate_pct"] for s in stats_list]

    peak_rate = max(rates)
    if peak_rate < 20.0:
        return None  # curve khong ro, khong estimate duoc

    threshold = peak_rate * 0.5

    # Sap xep theo position tang dan
    combined = sorted(zip(positions, rates), key=lambda x: x[0])
    positions_sorted = [x[0] for x in combined]
    rates_sorted = [x[1] for x in combined]

    # Tim tat ca cap (i, i+1) ma rate cat threshold (giao voi 50%)
    crossings = []
    for i in range(len(rates_sorted) - 1):
        r1, r2 = rates_sorted[i], rates_sorted[i + 1]
        if (r1 - threshold) * (r2 - threshold) < 0:  # cross
            p1, p2 = positions_sorted[i], positions_sorted[i + 1]
            # Linear interpolate
            p_cross = p1 + (threshold - r1) * (p2 - p1) / (r2 - r1)
            crossings.append(p_cross)

    if len(crossings) >= 2:
        return (crossings[0] + crossings[-1]) / 2.0  # midpoint of first + last crossing

    # Fallback: weighted centroid
    weights = [max(r - threshold * 0.5, 0) for r in rates]
    tot = sum(weights)
    if tot == 0:
        return None
    return sum(p * w for p, w in zip(positions, weights)) / tot


# ---------------------------------------------------------------------
# MQTT setup
# ---------------------------------------------------------------------
class MqttContext:
    def __init__(self, args, recorder: CalibRecorder, meta: dict, out_dir: Path):
        self.args = args
        self.recorder = recorder
        self.meta = meta
        self.out_dir = out_dir
        self.first_msg_warned = False

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"[MQTT] Connected {self.args.ip}:{self.args.port}")
            client.subscribe(self.args.topic)
            client.subscribe(self.args.status_topic)
            print(f"[MQTT] Subscribed: {self.args.topic}, {self.args.status_topic}")
        else:
            print(f"[MQTT] Connection failed rc={rc}")

    def on_message(self, client, userdata, msg):
        # Status topic: ghi log rieng, khong parse JSON
        if msg.topic == self.args.status_topic:
            try:
                text = msg.payload.decode("utf-8", errors="replace")
            except Exception:
                text = repr(msg.payload)
            ts = datetime.now().isoformat(timespec="seconds")
            print(f"\n[STATUS {ts}] {text}")
            status_file = self.out_dir / "status.log"
            try:
                with status_file.open("a", encoding="utf-8") as f:
                    f.write(f"{ts}\t{text}\n")
            except Exception as e:
                print(f"[MQTT] Status write error: {e}")
            return

        # Log topic
        try:
            data = json.loads(msg.payload.decode("utf-8"))
            row = normalize_payload(data)
            # Cap nhat metadata tu message dau tien
            if self.meta["firmware"]["version"] is None:
                self.meta["firmware"]["version"] = row.get("fw_version", "")
                self.meta["firmware"]["schema_version"] = row.get("schema_version", "")
                self.meta["firmware"]["first_boot_id"] = row.get("boot_id", "")
            # Canh bao firmware sai mode
            if not self.first_msg_warned:
                mode = str(row.get("mode", "")).upper()
                if mode and "RAW" not in mode:
                    print(f"\n[CANH BAO] Firmware mode = '{mode}', mong doi 'RAW'.")
                    print(f"           Hit-rate co the sai do filter alpha-beta can thiep!")
                    print(f"           Nap lai voi: pio run -e tc22_raw -t upload")
                self.first_msg_warned = True

            self.recorder.push(row)
        except Exception as e:
            print(f"\n[MQTT] Parse error: {e}")


# ---------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------
def build_parser():
    p = argparse.ArgumentParser(
        description="TC22 Bore-sight Calibration Logger",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--ip", default=DEFAULT_HOST, help="MQTT broker IP")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--topic", default=DEFAULT_TOPIC)
    p.add_argument("--status-topic", default=DEFAULT_TOPIC_STATUS)
    p.add_argument("--out-dir", default=DEFAULT_OUT_DIR,
                   help="Output directory (session subfolder created inside)")
    p.add_argument("--session",
                   default=datetime.now().strftime("%Y%m%d_calib_%H%M"),
                   help="Session ID (used in filenames)")
    p.add_argument("--distance", type=float, default=100.0,
                   help="Nominal R0 (m)")
    p.add_argument("--samples-per-pos", type=int, default=100,
                   help="Samples per screw position")
    p.add_argument("--n-positions", type=int, default=9,
                   help="Positions per scan (odd number recommended)")
    p.add_argument("--n-scans", type=int, default=3,
                   help="Number of scans (>=2 for repeatability)")
    p.add_argument("--scan-step", type=int, default=1,
                   help="Step between positions (in 1/8 turn units)")
    p.add_argument("--hit-tol-m", type=float, default=2.0,
                   help="Tolerance for HIT: |raw - R0| < tol")
    p.add_argument("--skip-r0", action="store_true",
                   help="Skip R0 measurement, use nominal")
    return p


def main():
    args = build_parser().parse_args()

    # Setup output folder <out_dir>/<session>/
    out_dir = Path(args.out_dir) / args.session
    out_dir.mkdir(parents=True, exist_ok=True)

    print("\n" + "=" * 60)
    print("TC22 BORE-SIGHT CALIBRATION LOGGER")
    print("=" * 60)
    print(f"Session:  {args.session}")
    print(f"Output:   {out_dir}")
    print(f"MQTT:     {args.ip}:{args.port} @ {args.topic}")
    print(f"Config:   {args.n_scans} scans x {args.n_positions} positions "
          f"x {args.samples_per_pos} samples")

    # Gather metadata
    meta = gather_metadata(args)

    # Recorder + MQTT
    recorder = CalibRecorder(r0_nominal_m=meta["range"]["nominal_m"],
                             hit_tol_m=args.hit_tol_m)
    ctx = MqttContext(args, recorder, meta, out_dir)

    client = mqtt.Client(client_id="tc22-calib-logger")
    client.on_connect = ctx.on_connect
    client.on_message = ctx.on_message

    print(f"\n[MQTT] Connecting to {args.ip}:{args.port}...")
    try:
        client.connect(args.ip, args.port, keepalive=60)
    except Exception as e:
        print(f"[MQTT] Connect failed: {e}")
        print("       Kiem tra IP broker + firewall.")
        sys.exit(1)

    client.loop_start()  # Non-blocking MQTT loop
    time.sleep(1.5)      # let subscription settle

    try:
        # Step 1: Measure R0
        if args.skip_r0:
            print("\n[SKIP] Bo qua do R0, dung nominal.")
        else:
            r0, r0_std = measure_r0(recorder, n_samples=100)
            if r0 is not None:
                recorder.r0_measured = r0
                meta["range"]["measured_m"] = round(r0, 3)
                meta["range"]["uncertainty_m"] = round(r0_std, 3)

        # Step 2: Run scans
        directions_cycle = ["bottom-up", "top-down", "bottom-up",
                           "top-down", "bottom-up"]
        for i in range(args.n_scans):
            scan_id = i + 1
            direction = directions_cycle[i % len(directions_cycle)]
            positions = get_scan_positions(direction, args.n_positions,
                                          args.scan_step)

            stats_list = run_scan(
                recorder, scan_id, direction, positions,
                args.samples_per_pos,
                meta["mechanical"]["mrad_per_eighth_turn"],
                out_dir, args.session
            )

            optimal_mrad = estimate_optimal_position_knife_edge(stats_list)
            if optimal_mrad is not None:
                print(f"\n[SCAN {scan_id}] Estimated optimal position: "
                      f"{optimal_mrad:+.3f} mrad")
            else:
                print(f"\n[SCAN {scan_id}] Khong estimate duoc optimal "
                      f"(curve khong ro).")

            meta["scans"].append({
                "scan_id": scan_id,
                "direction": direction,
                "csv": f"{args.session}_scan{scan_id}.csv",
                "pos_csv": f"{args.session}_scan{scan_id}_pos.csv",
                "positions_eighth_turns": positions,
                "optimal_mrad": (round(optimal_mrad, 4)
                                if optimal_mrad is not None else None),
                "hit_rates_pct": [round(s["hit_rate_pct"], 1) for s in stats_list],
            })

            if i < args.n_scans - 1:
                ans = input(f"\n>>> SCAN {scan_id} xong. "
                           f"ENTER de tiep scan {scan_id+1}, 'q' de dung: ")
                if ans.strip().lower() == "q":
                    print("Dung boi user.")
                    break

        # Step 3: Summary
        print("\n" + "=" * 60)
        print("SUMMARY")
        print("=" * 60)
        optimals = [s["optimal_mrad"] for s in meta["scans"]
                   if s["optimal_mrad"] is not None]
        if len(optimals) >= 2:
            opt_mean = statistics.mean(optimals)
            opt_std = statistics.stdev(optimals) if len(optimals) > 1 else 0.0
            print(f"Optimal (mrad) qua {len(optimals)} scans: {optimals}")
            print(f"  Mean   = {opt_mean:+.3f} mrad")
            print(f"  Sigma  = {opt_std:.3f} mrad")
            passed = opt_std < 0.5
            print(f"  Repeatability: {'PASS' if passed else 'FAIL'} "
                  f"(threshold 0.5 mrad)")
            meta["summary"] = {
                "optimals_mrad": optimals,
                "mean_optimal_mrad": round(opt_mean, 4),
                "sigma_optimal_mrad": round(opt_std, 4),
                "repeatability_threshold_mrad": 0.5,
                "repeatability_pass": passed,
            }
        else:
            print("Khong du data de tinh repeatability (can >= 2 scans).")

        meta["end_time"] = datetime.now().isoformat(timespec="seconds")

    except KeyboardInterrupt:
        print("\n\n[INTERRUPTED] Luu partial meta...")
        meta["end_time"] = datetime.now().isoformat(timespec="seconds")
        meta["interrupted"] = True
    finally:
        # Always save meta.json (ke ca khi interrupt)
        meta_path = out_dir / f"{args.session}_meta.json"
        try:
            with meta_path.open("w", encoding="utf-8") as f:
                json.dump(meta, f, indent=2, ensure_ascii=False)
            print(f"\nMeta saved: {meta_path}")
        except Exception as e:
            print(f"[ERROR] Save meta failed: {e}")

        client.loop_stop()
        client.disconnect()
        print("[LOGGER] Disconnected.")


if __name__ == "__main__":
    main()
