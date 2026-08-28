# tc22_quick.py
import serial, struct, sys, signal

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = 115200

START_CONT = bytes.fromhex("FA 01 FF 04 01 00 00 00 FF")
STOP       = bytes.fromhex("FA 01 FF 04 00 00 00 00 FE")

ser = serial.Serial(PORT, BAUD, bytesize=8, parity="N", stopbits=1, timeout=1)
ser.reset_input_buffer()

def stop_and_exit(*_):
    try:
        ser.write(STOP)
    finally:
        ser.close()
    print("\nStopped.")
    sys.exit(0)
1

signal.signal(signal.SIGINT, stop_and_exit)
print(f"Open {PORT} @ {BAUD} ... start measuring")
ser.write(START_CONT)

pkt = bytearray()
while True:
    b = ser.read(1)
    if not b:
        continue
    if not pkt:
        if b != b'\xFB':   # sync to header
            continue
    pkt += b
    if len(pkt) == 9:
        crc_ok = (sum(pkt[:8]) & 0xFF) == pkt[8]
        board  = pkt[2]
        dtype  = pkt[3]     # 0x04
        valid  = struct.unpack("<H", pkt[4:6])[0]
        dist_dm= struct.unpack("<H", pkt[6:8])[0]
        if crc_ok and valid == 1:
            print(f"Board={board} Dist={dist_dm/10:.1f} m  (raw={dist_dm} dm)")
        else:
            print(f"Frame CRC/valid fail: {pkt.hex(' ')}")
        pkt.clear()
