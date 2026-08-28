#!/usr/bin/env python3
"""
TC22 Dynamic Target Logger (DT1/DT2)
=====================================
Ban song song voi tc22_mqtt_log.py va tc22_calib_log.py, dung rieng cho bai test
muc tieu di chuyen deu (DT1 approaching/receding, DT2 diagonal walk).

KHONG can sua firmware — subscribe cung MQTT topic, chi khac cach xu ly va luu.

Workflow:
  1. Prompt metadata session (test type, velocity, marks, weather, ...)
  2. Chay lien tuc N walks (default 3), moi walk:
     - ENTER de bat dau -> stream logger tu dong ghi CSV
     - Trong luc di, GO event tren console:
         * m<dist>  = MARK khi qua vach (VD: m190 = qua vach 190m)
         * n <text> = NOTE (VD: "n windy at this point")
         * s        = STOP walk hien tai
         * q        = QUIT ca session
     - Sau STOP: save 2 file (<walk>.csv + <walk>_events.csv)
  3. Cuoi session: save meta.json va print summary

Kien truc thread:
  - MQTT thread (background)       : nhan message tu firmware, append vao stream buffer
  - Main thread                    : quan ly walks, drain stream buffer -> CSV
  - Event input thread (per walk)  : blocking input() -> event queue

Usage:
  python tc22_dynamic_log.py --ip 192.168.88.56 --test-type approach
  python tc22_dynamic_log.py --session DT1a_approach_v1 --n-walks 3

Author: Tran Thang Long - Dang Quoc Phu
"""
import argparse
import csv
import json
import queue
import statistics
import sys
import threading
import time
from pathlib import Path
from datetime import datetime
from typing import List, Optional

import paho.mqtt.client as mqtt

# ---------------------------------------------------------------------
# Defaults (giu giong tc22_mqtt_log.py + tc22_calib_log.py)
# ---------------------------------------------------------------------
DEFAULT_HOST = "192.168.88.56"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "thesis/tc22/log"
DEFAULT_TOPIC_STATUS = "thesis/tc22/status"
DEFAULT_OUT_DIR = "logs_tc22/dynamic"

# CSV_COLUMNS = giong tc22_mqtt_log.py (30 cot chuan) + 1 cot dynamic-specific
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
    # ---- Dynamic-specific (Python them) ----
    "walk_id",
]

# CSV events (per walk)
EVENTS_CSV_COLUMNS = [
    "walk_id", "event_type", "timestamp_iso", "host_ts_ms_since_start",
    "mark_m", "note",
]


# =====================================================================
# HELPERS
# =====================================================================
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
    Copy toan bo 30 cot tu payload MQTT."""
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


# =====================================================================
# STREAM BUFFER: thread-safe, MQTT thread push -> main thread drain
# =====================================================================
class StreamBuffer:
    """Buffer chua sensor samples. MQTT thread append, main thread drain
    va flush ra CSV."""
    def __init__(self):
        self.lock = threading.Lock()
        self.buffer: List[dict] = []
        self.recording = False
        self.current_walk_id = 0

    def start(self, walk_id: int):
        with self.lock:
            self.buffer.clear()
            self.recording = True
            self.current_walk_id = walk_id

    def stop(self):
        with self.lock:
            self.recording = False

    def push(self, row: dict):
        with self.lock:
            if self.recording:
                row["walk_id"] = self.current_walk_id
                self.buffer.append(row)

    def drain(self) -> List[dict]:
        with self.lock:
            out = list(self.buffer)
            self.buffer.clear()
            return out

    def count(self):
        with self.lock:
            return len(self.buffer)


# =====================================================================
# EVENT QUEUE: input thread -> main thread
# =====================================================================
class EventInputThread(threading.Thread):
    """Chay blocking input() trong thread rieng, push event vao queue."""
    def __init__(self, event_queue: queue.Queue, walk_id: int):
        super().__init__(daemon=True)
        self.event_queue = event_queue
        self.walk_id = walk_id
        self.stop_flag = threading.Event()

    def run(self):
        print("\n  === EVENT INPUT ACTIVE ===")
        print("  Commands: m<dist> (mark)  |  n <text> (note)  |  s (stop walk)  |  q (quit session)")
        print("  Vi du: 'm190' khi qua vach 190m")
        print()
        while not self.stop_flag.is_set():
            try:
                cmd = input(">>> ").strip()
            except EOFError:
                break
            if not cmd:
                continue
            self.event_queue.put((cmd, datetime.now()))
            if cmd.lower() in ("s", "q"):
                break

    def stop(self):
        self.stop_flag.set()


def parse_event(cmd: str) -> Optional[dict]:
    """Parse user command string thanh event dict.
    Returns None neu invalid."""
    cmd = cmd.strip()
    if not cmd:
        return None
    c0 = cmd[0].lower()
    if c0 == "m":
        # m<dist>  hoac  m <dist>
        rest = cmd[1:].strip()
        try:
            dist = float(rest)
            return {"event_type": "MARK", "mark_m": dist, "note": ""}
        except ValueError:
            print(f"    [WARN] Cannot parse distance: '{rest}'")
            return None
    elif c0 == "n":
        # n <text>
        rest = cmd[1:].strip()
        return {"event_type": "NOTE", "mark_m": "", "note": rest}
    elif c0 == "s":
        return {"event_type": "STOP", "mark_m": "", "note": ""}
    elif c0 == "q":
        return {"event_type": "QUIT", "mark_m": "", "note": ""}
    else:
        print(f"    [WARN] Unknown command: '{cmd}' (use m/n/s/q)")
        return None


# =====================================================================
# METADATA
# =====================================================================
TEST_TYPES = {
    "approach":  "DT1a_approach — target di lai gan (velocity negative)",
    "recede":    "DT1b_recede   — target di ra xa (velocity positive)",
    "diagonal":  "DT2_diagonal  — target di cheo (velocity radial nho)",
    "handheld":  "DT_handheld   — cam tay, muc tieu tinh",
}

def gather_metadata(args) -> dict:
    print("\n" + "=" * 60)
    print("DYNAMIC SESSION METADATA (ENTER de dung default)")
    print("=" * 60)

    print(f"  Test type: {args.test_type} -> {TEST_TYPES.get(args.test_type, 'unknown')}")

    v_nominal = prompt_default("Nominal velocity (m/s, +ve = recede, -ve = approach)",
                               -1.0 if args.test_type == "approach" else 1.0, float)
    start_m = prompt_default("Start position (m)",
                             200.0 if args.test_type == "approach" else 100.0, float)
    end_m = prompt_default("End position (m)",
                           100.0 if args.test_type == "approach" else 200.0, float)
    marks_str = prompt_default("Marks list (comma-separated, m)",
                               "200,190,180,170,160,150,140,130,120,110,100")
    try:
        marks = [float(x.strip()) for x in marks_str.split(",") if x.strip()]
    except ValueError:
        print("    [WARN] Cannot parse marks, using default 100-200 step 10")
        marks = list(range(100, 210, 10))

    target_material = prompt_default("Target material", "bia 30x30cm bia cung trang matte")
    target_carrier = prompt_default("Target carrier", "nguoi cam tay")
    location = prompt_default("Location", "San DHBK TP.HCM")
    weather = prompt_default("Weather", "troi nang nhe, gio nhe")
    temp_c = prompt_default("Ambient temp (C)", 28.0, float)
    operator = prompt_default("Operator", "Tran Thang Long")
    preset_id = prompt_default("Firmware preset (0=RAW, 1=BASE, 3=AB-B default, 5=HAND)", 3, int)
    notes = prompt_default("Extra notes", "walk 1 m/s con metronome 60 BPM")

    return {
        "session_id": args.session,
        "test_type": args.test_type,
        "test_type_desc": TEST_TYPES.get(args.test_type, "unknown"),
        "date": datetime.now().strftime("%Y-%m-%d"),
        "start_time": datetime.now().isoformat(timespec="seconds"),
        "operator": operator,
        "location": location,
        "weather": weather,
        "ambient_temp_C": temp_c,
        "target": {
            "material": target_material,
            "carrier": target_carrier,
        },
        "motion": {
            "type": args.test_type,
            "nominal_velocity_mps": v_nominal,
            "start_m": start_m,
            "end_m": end_m,
            "expected_duration_s": abs((end_m - start_m) / v_nominal) if v_nominal != 0 else None,
            "marks_m": marks,
        },
        "device": {
            "firmware_env_expected": "tc22_bench",
            "preset_id_requested": preset_id,
            "firmware_version": None,     # filled from first sample
            "schema_version": None,
            "first_boot_id": None,
        },
        "session_config": {
            "n_walks_planned": args.n_walks,
        },
        "walks": [],  # filled during measurement
        "notes": notes,
    }


# =====================================================================
# RUN 1 WALK
# =====================================================================
def run_walk(stream: StreamBuffer, walk_id: int, meta_walks: List,
             out_dir: Path, session: str) -> str:
    """Chay 1 walk. Tra ve 'continue', 'quit', hoac 'stop'."""
    print("\n" + "=" * 60)
    print(f"WALK {walk_id}")
    print("=" * 60)
    print("Chuan bi:")
    print("  1. Person o vi tri start, cam bia")
    print("  2. Metronome 60 BPM bat trong dien thoai")
    print("  3. ENTER de bat dau logging (person bat dau di)")
    input("  ENTER khi san sang...")

    walk_csv_path = out_dir / f"{session}_walk{walk_id}.csv"
    events_csv_path = out_dir / f"{session}_walk{walk_id}_events.csv"

    # Open CSV files
    csv_f = walk_csv_path.open("w", newline="", encoding="utf-8")
    csv_w = csv.DictWriter(csv_f, fieldnames=CSV_COLUMNS)
    csv_w.writeheader()

    events_f = events_csv_path.open("w", newline="", encoding="utf-8")
    events_w = csv.DictWriter(events_f, fieldnames=EVENTS_CSV_COLUMNS)
    events_w.writeheader()

    # Start recording
    walk_start_time = datetime.now()
    walk_start_ms = int(walk_start_time.timestamp() * 1000)
    stream.start(walk_id)

    # Write START event
    events_w.writerow({
        "walk_id": walk_id,
        "event_type": "START",
        "timestamp_iso": walk_start_time.isoformat(timespec="milliseconds"),
        "host_ts_ms_since_start": 0,
        "mark_m": "",
        "note": "walk started",
    })
    events_f.flush()

    # Start event input thread
    event_q = queue.Queue()
    input_thread = EventInputThread(event_q, walk_id)
    input_thread.start()

    walk_result = "stop"
    total_samples = 0
    mark_count = 0

    try:
        while True:
            # Drain stream buffer -> CSV
            samples = stream.drain()
            if samples:
                for s in samples:
                    csv_w.writerow(s)
                csv_f.flush()
                total_samples += len(samples)

            # Check event queue
            try:
                cmd, ts = event_q.get(timeout=0.2)
            except queue.Empty:
                continue

            ev = parse_event(cmd)
            if ev is None:
                continue

            ts_ms_since = int(ts.timestamp() * 1000) - walk_start_ms
            events_w.writerow({
                "walk_id": walk_id,
                "event_type": ev["event_type"],
                "timestamp_iso": ts.isoformat(timespec="milliseconds"),
                "host_ts_ms_since_start": ts_ms_since,
                "mark_m": ev["mark_m"],
                "note": ev["note"],
            })
            events_f.flush()

            if ev["event_type"] == "MARK":
                mark_count += 1
                print(f"    [MARK #{mark_count}] {ev['mark_m']:.1f}m at t+{ts_ms_since/1000:.2f}s")
            elif ev["event_type"] == "NOTE":
                print(f"    [NOTE] {ev['note']}")
            elif ev["event_type"] == "STOP":
                print(f"    [STOP] walk {walk_id} kết thúc")
                walk_result = "stop"
                break
            elif ev["event_type"] == "QUIT":
                print(f"    [QUIT] dừng ca session")
                walk_result = "quit"
                break

    except KeyboardInterrupt:
        print("\n  [Ctrl+C] Walk interrupted")
        walk_result = "quit"

    finally:
        # Stop recording + drain remaining
        stream.stop()
        input_thread.stop()
        time.sleep(0.3)  # let final samples arrive
        samples = stream.drain()
        for s in samples:
            csv_w.writerow(s)
        total_samples += len(samples)

        # STOP event
        end_time = datetime.now()
        events_w.writerow({
            "walk_id": walk_id,
            "event_type": "END",
            "timestamp_iso": end_time.isoformat(timespec="milliseconds"),
            "host_ts_ms_since_start": int(end_time.timestamp() * 1000) - walk_start_ms,
            "mark_m": "",
            "note": f"walk ended, total {total_samples} samples, {mark_count} marks",
        })

        csv_f.close()
        events_f.close()

    duration_s = (end_time - walk_start_time).total_seconds()
    print(f"\n  Walk {walk_id} summary:")
    print(f"    Duration:      {duration_s:.1f} s")
    print(f"    Sensor samples: {total_samples}")
    print(f"    Marks:         {mark_count}")
    print(f"    Saved:         {walk_csv_path.name}, {events_csv_path.name}")

    meta_walks.append({
        "walk_id": walk_id,
        "csv": walk_csv_path.name,
        "events_csv": events_csv_path.name,
        "start_time_iso": walk_start_time.isoformat(timespec="seconds"),
        "end_time_iso": end_time.isoformat(timespec="seconds"),
        "duration_s": round(duration_s, 2),
        "sensor_samples": total_samples,
        "marks_recorded": mark_count,
        "result": walk_result,
    })

    return walk_result


# =====================================================================
# MQTT SETUP
# =====================================================================
class MqttContext:
    def __init__(self, args, stream: StreamBuffer, meta: dict, out_dir: Path):
        self.args = args
        self.stream = stream
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
        # Status topic
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
            # Populate metadata tu message dau tien
            if self.meta["device"]["firmware_version"] is None:
                self.meta["device"]["firmware_version"] = row.get("fw_version", "")
                self.meta["device"]["schema_version"] = row.get("schema_version", "")
                self.meta["device"]["first_boot_id"] = row.get("boot_id", "")
            # Canh bao neu preset khac requested
            if not self.first_msg_warned:
                mode = str(row.get("mode", "")).upper()
                preset = str(row.get("cfg_preset_id", ""))
                req = str(self.meta["device"]["preset_id_requested"])
                if preset and preset != req:
                    print(f"\n[WARN] Firmware preset={preset} khac yeu cau {req}")
                print(f"[INFO] Firmware: mode={mode}, preset_id={preset}")
                self.first_msg_warned = True

            self.stream.push(row)
        except Exception as e:
            print(f"\n[MQTT] Parse error: {e}")


# =====================================================================
# MAIN
# =====================================================================
def build_parser():
    p = argparse.ArgumentParser(
        description="TC22 Dynamic Target Logger (DT1/DT2)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--ip", default=DEFAULT_HOST, help="MQTT broker IP")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--topic", default=DEFAULT_TOPIC)
    p.add_argument("--status-topic", default=DEFAULT_TOPIC_STATUS)
    p.add_argument("--out-dir", default=DEFAULT_OUT_DIR,
                   help="Output dir; session subfolder created inside")
    p.add_argument("--session",
                   default=datetime.now().strftime("DT_%Y%m%d_%H%M"),
                   help="Session ID (used in filenames)")
    p.add_argument("--test-type", default="approach",
                   choices=list(TEST_TYPES.keys()),
                   help="Loai test: approach/recede/diagonal/handheld")
    p.add_argument("--n-walks", type=int, default=3,
                   help="So walks planned trong session")
    return p


def main():
    args = build_parser().parse_args()

    out_dir = Path(args.out_dir) / args.session
    out_dir.mkdir(parents=True, exist_ok=True)

    print("\n" + "=" * 60)
    print("TC22 DYNAMIC TARGET LOGGER (DT1/DT2)")
    print("=" * 60)
    print(f"Session:   {args.session}")
    print(f"Test type: {args.test_type}")
    print(f"Output:    {out_dir}")
    print(f"MQTT:      {args.ip}:{args.port} @ {args.topic}")
    print(f"Planned:   {args.n_walks} walks")

    # Gather metadata
    meta = gather_metadata(args)

    # Setup stream buffer + MQTT
    stream = StreamBuffer()
    ctx = MqttContext(args, stream, meta, out_dir)

    client = mqtt.Client(client_id="tc22-dynamic-logger")
    client.on_connect = ctx.on_connect
    client.on_message = ctx.on_message

    print(f"\n[MQTT] Connecting to {args.ip}:{args.port}...")
    try:
        client.connect(args.ip, args.port, keepalive=60)
    except Exception as e:
        print(f"[MQTT] Connect failed: {e}")
        print("       Kiem tra IP broker + firewall.")
        sys.exit(1)

    client.loop_start()
    time.sleep(1.5)  # let subscription settle

    try:
        for walk_idx in range(args.n_walks):
            walk_id = walk_idx + 1
            result = run_walk(stream, walk_id, meta["walks"], out_dir, args.session)

            if result == "quit":
                print(f"\n[SESSION] Dung boi user tai walk {walk_id}")
                break

            if walk_idx < args.n_walks - 1:
                ans = input(f"\n>>> Walk {walk_id} xong. ENTER de walk tiep, "
                           f"'q' de dung session: ").strip().lower()
                if ans == "q":
                    print("Dung boi user.")
                    break

        # Summary
        print("\n" + "=" * 60)
        print("SESSION SUMMARY")
        print("=" * 60)
        completed = [w for w in meta["walks"] if w["result"] != "quit"]
        print(f"Walks completed: {len(completed)}/{args.n_walks}")
        total_samples = sum(w["sensor_samples"] for w in meta["walks"])
        total_marks = sum(w["marks_recorded"] for w in meta["walks"])
        print(f"Total sensor samples: {total_samples}")
        print(f"Total marks recorded: {total_marks}")

        meta["summary"] = {
            "walks_completed": len(completed),
            "total_sensor_samples": total_samples,
            "total_marks_recorded": total_marks,
        }
        meta["end_time"] = datetime.now().isoformat(timespec="seconds")

    except KeyboardInterrupt:
        print("\n\n[INTERRUPTED] Save partial meta...")
        meta["end_time"] = datetime.now().isoformat(timespec="seconds")
        meta["interrupted"] = True
    finally:
        # Save meta.json
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
