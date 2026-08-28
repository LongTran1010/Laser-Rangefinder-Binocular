# tc22_log_csv.py
import serial, struct, csv, time, sys, os
from pathlib import Path

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = 115200
START_CONT = bytes.fromhex("FA 01 FF 04 01 00 00 00 FF")
STOP       = bytes.fromhex("FA 01 FF 04 00 00 00 00 FE")

def unique_csv_path(out_arg: str | None) -> Path:
    """Nếu out_arg là file .csv -> dùng trực tiếp.
       Nếu out_arg là thư mục -> tạo tên file mới trong thư mục đó.
       Nếu None -> tạo tên file mới trong thư mục làm việc hiện tại.
       Tên file: tc22_YYYYmmdd_HHMMSS.csv; nếu trùng, tự thêm _01, _02...
    """
    if out_arg:
        p = Path(out_arg)
        if p.suffix.lower() == ".csv":  #đưa sẵn tên file
            p.parent.mkdir(parents=True, exist_ok=True)
            if p.exists():
                # tránh ghi đè: thêm số tăng dần
                stem, ext = p.stem, p.suffix
                i = 1
                while p.exists():
                    p = p.with_name(f"{stem}_{i:02d}{ext}")
                    i += 1
            return p
        else:
            out_dir = p
    else:
        out_dir = Path.cwd()

    out_dir.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    base = out_dir / f"tc22_{ts}.csv"
    if not base.exists():
        return base
    # hiếm khi trùng giây -> thêm hậu tố
    i = 1
    p = base
    while p.exists():
        p = out_dir / f"tc22_{ts}_{i:02d}.csv"
        i += 1
    return p

def read_frame(ser):
    b = ser.read(1)
    if not b or b != b'\xFB': return None
    pkt = bytearray(b) + ser.read(8)
    if len(pkt) != 9: return None
    crc_ok = (sum(pkt[:8]) & 0xFF) == pkt[8]
    board  = pkt[2]
    valid  = struct.unpack("<H", pkt[4:6])[0]
    dist_dm= struct.unpack("<H", pkt[6:8])[0]
    return crc_ok, board, valid, dist_dm, pkt

# Tham số thứ 2 (tùy chọn): đường dẫn file CSV hoặc thư mục đích
OUT_PATH = unique_csv_path(sys.argv[2] if len(sys.argv) > 2 else None)

with serial.Serial(PORT, BAUD, timeout=1) as s, open(OUT_PATH, "w", newline="", encoding="utf-8") as f:
    print("Logging →", OUT_PATH.resolve())
    w = csv.writer(f)
    w.writerow(["ts_iso","t_rel_s","board","valid","dist_m","raw_hex","dt_s","fps"])
    s.reset_input_buffer()
    s.write(START_CONT)
    print(f"Logging on {PORT}  (Ctrl+C to stop)")

    first_t = None   # mốc t=0
    last_t  = None

    try:
        while True:
            fr = read_frame(s)
            if not fr:
                continue

            crc_ok, board, valid, dist_dm, pkt = fr
            now = time.time()
            if first_t is None:
                first_t = now
            t_rel = now - first_t

            dt  = (now - last_t) if last_t else None
            fps = (1.0/dt) if dt and dt > 0 else None
            last_t = now

            if crc_ok:
                if valid == 1:
                    print(f"t={t_rel:7.3f}s  Board={board}  Dist={dist_dm/10:.1f} m" + (f"  fps={fps:.2f}" if fps else ""))
                else:
                    print(f"t={t_rel:7.3f}s  (invalid) Board={board}")
            else:
                print(f"t={t_rel:7.3f}s  CRC fail: {pkt.hex(' ')}")

            w.writerow([
                time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now)),
                f"{t_rel:.4f}",
                board, valid, f"{dist_dm/10:.3f}", pkt.hex(" "),
                f"{dt:.4f}" if dt else "", f"{fps:.3f}" if fps else ""
            ])
            f.flush()

    except KeyboardInterrupt:
        s.write(STOP)
        print("\nStopped.")
