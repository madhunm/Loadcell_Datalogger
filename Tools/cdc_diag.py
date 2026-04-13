#!/usr/bin/env python3
"""Quick CDC diagnostic: read raw bytes from COM port, print hex + ASCII."""
import sys
import serial
import serial.tools.list_ports

port = sys.argv[1] if len(sys.argv) > 1 else "COM12"
print(f"Opening {port} ...")

# List all ports for reference
for p in serial.tools.list_ports.comports():
    vid = f"VID={p.vid:04X}" if p.vid else "VID=None"
    pid = f"PID={p.pid:04X}" if p.pid else "PID=None"
    print(f"  {p.device:8s}  {vid} {pid}  {p.description}  hwid={p.hwid}")

try:
    with serial.Serial(port, 115200, timeout=2.0) as ser:
        print(f"\nConnected to {port}. Waiting for data (5 reads, 2s timeout each)...\n")
        for i in range(5):
            raw = ser.read(256)
            if not raw:
                print(f"  read {i}: (empty / timeout)")
                continue
            hex_str = raw.hex(' ')
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in raw)
            print(f"  read {i}: {len(raw)} bytes")
            print(f"    HEX:   {hex_str}")
            print(f"    ASCII: {ascii_str}")
            print()
except serial.SerialException as e:
    print(f"Error: {e}")
