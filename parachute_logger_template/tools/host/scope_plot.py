import time, csv
from collections import deque

import numpy as np
import serial
from serial.tools import list_ports
import matplotlib.pyplot as plt

BAUD = 115200
SCOPE_HZ = 25
WINDOW_S = 30
OUTFILE = f"scope_{int(time.time())}.csv"

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
    port = choose_port()
    print(f"Opening {port} @ {BAUD}")
    ser = serial.Serial(port, BAUD, timeout=1)

    ser.write(f"scope {SCOPE_HZ}\n".encode("ascii"))

    f = open(OUTFILE, "w", newline="")
    w = csv.writer(f)
    w.writerow(["ms","force_mean_N","force_peak_N","accel_mag_g","flags"])

    maxlen = int(WINDOW_S * SCOPE_HZ * 2)
    t = deque(maxlen=maxlen)
    meanN = deque(maxlen=maxlen)
    peakN = deque(maxlen=maxlen)
    amag = deque(maxlen=maxlen)

    plt.ion()
    fig, ax = plt.subplots()
    l1, = ax.plot([], [], label="force_mean (N)")
    l2, = ax.plot([], [], label="force_peak (N)")
    l3, = ax.plot([], [], label="accel_mag (g)")
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
            t.append(ts); meanN.append(fm); peakN.append(fp); amag.append(ag)

            now = time.time()
            if now - last_plot >= 0.1:
                last_plot = now
                tt = np.array(t)
                if tt.size < 2:
                    continue
                t0 = tt[-1] - WINDOW_S
                m = tt >= t0
                x = tt[m] - tt[m][0]
                l1.set_data(x, np.array(meanN)[m])
                l2.set_data(x, np.array(peakN)[m])
                l3.set_data(x, np.array(amag)[m])
                ax.relim(); ax.autoscale_view()
                fig.canvas.draw(); fig.canvas.flush_events()

    except KeyboardInterrupt:
        print("Stopping...")

    finally:
        try:
            ser.write(b"scope 0\n")
        except Exception:
            pass
        ser.close()
        f.close()
        print("Saved:", OUTFILE)

if __name__ == "__main__":
    main()
