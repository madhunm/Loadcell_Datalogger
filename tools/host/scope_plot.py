#!/usr/bin/env python3
"""Live scope plot: send 'scope <hz>' to device, parse CSV, write file and plot."""
import argparse
import csv
import time
from collections import deque

import matplotlib.pyplot as plt
import numpy as np
import serial
from serial.tools import list_ports


def choose_port():
    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found.")
    for p in ports:
        d = (p.description or "").upper()
        if "USB" in d or "CP210" in d or "CH340" in d or "JTAG" in d:
            return p.device
    return ports[0].device


def main():
    parser = argparse.ArgumentParser(description="Scope plot from PDL device")
    parser.add_argument("--port", type=str, default=None, help="Serial port (auto if omitted)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--hz", type=int, default=25, help="Scope rate in Hz")
    parser.add_argument("--window", type=float, default=30, help="Plot window in seconds")
    parser.add_argument("--outfile", type=str, default="scope_capture.csv", help="Output CSV file")
    args = parser.parse_args()

    port = args.port or choose_port()
    outfile = args.outfile

    print(f"Opening {port} @ {args.baud}")
    ser = serial.Serial(port, args.baud, timeout=1)

    ser.write(f"scope {args.hz}\n".encode("ascii"))

    f = open(outfile, "w", newline="")
    w = csv.writer(f)
    w.writerow(["ms", "force_mean_N", "force_peak_N", "accel_mag_g", "flags"])

    maxlen = int(args.window * args.hz * 2)
    t = deque(maxlen=maxlen)
    mean_n = deque(maxlen=maxlen)
    peak_n = deque(maxlen=maxlen)
    amag = deque(maxlen=maxlen)

    plt.ion()
    fig, ax = plt.subplots()
    (l1,) = ax.plot([], [], label="force_mean (N)")
    (l2,) = ax.plot([], [], label="force_peak (N)")
    (l3,) = ax.plot([], [], label="accel_mag (g)")
    ax.set_xlabel("t (s)")
    ax.legend()

    last_plot = time.time()

    try:
        while True:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if not line:
                continue
            if line.startswith("#"):
                print(line)
                continue

            parts = line.split(",")
            if len(parts) < 5:
                continue

            try:
                ms = int(parts[0])
                fm = float(parts[1])
                fp = float(parts[2])
                ag = float(parts[3])
                flags = parts[4]
            except ValueError:
                continue

            w.writerow([ms, fm, fp, ag, flags])
            f.flush()

            ts = ms / 1000.0
            t.append(ts)
            mean_n.append(fm)
            peak_n.append(fp)
            amag.append(ag)

            now = time.time()
            if now - last_plot >= 0.1:
                last_plot = now
                tt = np.array(t)
                if tt.size >= 2:
                    t0 = tt[-1] - args.window
                    m = tt >= t0
                    x = tt[m] - tt[m][0]
                    l1.set_data(x, np.array(mean_n)[m])
                    l2.set_data(x, np.array(peak_n)[m])
                    l3.set_data(x, np.array(amag)[m])
                    ax.relim()
                    ax.autoscale_view()
                    fig.canvas.draw()
                    fig.canvas.flush_events()

    except KeyboardInterrupt:
        print("Stopping...")

    finally:
        try:
            ser.write(b"scope 0\n")
        except Exception:
            pass
        ser.close()
        f.close()
        print("Saved:", outfile)


if __name__ == "__main__":
    main()
