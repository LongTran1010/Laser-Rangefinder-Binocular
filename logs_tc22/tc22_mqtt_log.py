#!/usr/bin/env python3
# Usage: python tc22_mqtt_log.py --ip 192.168.90.52
import argparse
import csv
import json
from pathlib import Path
from datetime import datetime
from typing import Optional

import paho.mqtt.client as mqtt

DEFAULT_HOST = "192.168.88.56"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "thesis/tc22/log"
DEFAULT_TOPIC_STATUS = "thesis/tc22/status"
DEFAULT_OUT_DIR = "logs_tc22"

CSV_COLUMNS = [
    "boot_id",
    "msg_seq",
    "dev_ts_ms",
    "host_ts",
    "mode",
    "raw_m",
    "est_m",
    "fps",
    "meas_status",
    "track_state",
    "has_estimate",
    "consecutive_valids",
    "consecutive_invalids",
    "last_good_ts_ms",
    "restart_count",
    "system_error",
    "rate_mps",
    "predicted_m",
    "residual_m",
    "rejected_by_gate",
    "reject_reason",
    "estimator_mode",
    "test_id",
    # ---- Schema v2 (firmware 0.3.0+): config snapshot per-row ----
    "cfg_alpha",
    "cfg_beta",
    "cfg_gate_m",
    "cfg_min_dt_s",
    "cfg_max_reject",
    "cfg_preset_id",
    # ---- Schema v3 (firmware 0.3.1+): predict-hold diagnostic ----
    "predict_hold_count",
    "fw_version",
    "schema_version",
]


def safe_name(text: str) -> str:
    text = str(text)
    return "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in text)


class CsvSessionWriter:
    def __init__(self, out_dir: Path):
        self.out_dir = out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.current_boot_id: Optional[str] = None
        self.csv_file = None
        self.writer = None
        self.csv_path: Optional[Path] = None

    def _open_new_file(self, boot_id: str):
        self.close()
        ts_str = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"tc22_{safe_name(boot_id)}_{ts_str}.csv"
        self.csv_path = self.out_dir / filename
        self.csv_file = self.csv_path.open("w", newline="", encoding="utf-8")
        self.writer = csv.DictWriter(self.csv_file, fieldnames=CSV_COLUMNS)
        self.writer.writeheader()
        self.csv_file.flush()
        self.current_boot_id = boot_id
        print(f"[LOGGER] New session file: {self.csv_path}")

    def write_row(self, row: dict):
        boot_id = str(row.get("boot_id") or "boot_unknown")
        if self.writer is None or self.current_boot_id != boot_id:
          self._open_new_file(boot_id)

        row_out = {k: row.get(k, "") for k in CSV_COLUMNS}
        self.writer.writerow(row_out)
        self.csv_file.flush()

    def close(self):
        if self.csv_file is not None:
            self.csv_file.close()
        self.csv_file = None
        self.writer = None
        self.csv_path = None


def normalize_payload(data: dict) -> dict:
    """
    Hỗ trợ:
    - schema mới của Controller MQTT
    - fallback một phần cho schema cũ nếu cần
    """
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
    # Schema v2 config snapshot (firmware 0.3.0+)
    row["cfg_alpha"]      = data.get("cfg_alpha", "")
    row["cfg_beta"]       = data.get("cfg_beta", "")
    row["cfg_gate_m"]     = data.get("cfg_gate_m", "")
    row["cfg_min_dt_s"]   = data.get("cfg_min_dt_s", "")
    row["cfg_max_reject"] = data.get("cfg_max_reject", "")
    row["cfg_preset_id"]  = data.get("cfg_preset_id", "")
    # Schema v3: predict_hold_count (dung cho DT5 verify B4 velocity decay)
    row["predict_hold_count"] = data.get("predict_hold_count", "")
    row["fw_version"]     = data.get("fw_version", "")
    row["schema_version"] = data.get("schema_version", "")
    return row


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="TC22 MQTT Logger")
    parser.add_argument("--ip", type=str, default=DEFAULT_HOST, help="MQTT broker IP")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="MQTT broker port")
    parser.add_argument("--topic", type=str, default=DEFAULT_TOPIC, help="MQTT log topic")
    parser.add_argument("--status-topic", type=str, default=DEFAULT_TOPIC_STATUS,
                        help="MQTT status topic (boot / restart events)")
    parser.add_argument("--out-dir", type=str, default=DEFAULT_OUT_DIR, help="Output directory")
    return parser


def main():
    args = build_parser().parse_args()
    topic = args.topic
    status_topic = args.status_topic
    out_dir = Path(args.out_dir)
    session_writer = CsvSessionWriter(out_dir)

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"[LOGGER] Connected to MQTT {args.ip}:{args.port}, rc=0")
            client.subscribe(topic)
            print(f"[LOGGER] Subscribed to LOG topic: {topic}")
            client.subscribe(status_topic)
            print(f"[LOGGER] Subscribed to STATUS topic: {status_topic}")
        else:
            print(f"[LOGGER] Failed to connect, rc={rc}")

    def on_message(client, userdata, msg):
        # Status topic: khong parse JSON, chi in ra + luu vao file rieng
        if msg.topic == status_topic:
            try:
                text = msg.payload.decode("utf-8", errors="replace")
            except Exception:
                text = repr(msg.payload)
            ts = datetime.now().isoformat(timespec="seconds")
            print(f"[STATUS {ts}] {text}")
            # Ghi vao file status.log de theo doi boot/restart events
            status_file = out_dir / "status.log"
            try:
                with status_file.open("a", encoding="utf-8") as f:
                    f.write(f"{ts}\t{text}\n")
            except Exception as e:
                print(f"[LOGGER] Status file write error: {e}")
            return

        # Log topic: parse JSON va ghi CSV
        try:
            payload = msg.payload.decode("utf-8")
            data = json.loads(payload)
            row = normalize_payload(data)
            session_writer.write_row(row)
        except Exception as e:
            print("[LOGGER] Parse error:", e)
            print("[LOGGER] Raw payload:", msg.payload)

    client = mqtt.Client(client_id="tc22-logger-pc")
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[LOGGER] Connecting to MQTT {args.ip}:{args.port} ...")
    client.connect(args.ip, args.port, keepalive=60)

    try:
        print("[LOGGER] Logging... Press Ctrl+C to stop.")
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[LOGGER] Stopping...")
    finally:
        session_writer.close()
        client.disconnect()


if __name__ == "__main__":
    main()