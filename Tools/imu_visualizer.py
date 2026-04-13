#!/usr/bin/env python3
"""
ValueXT Loadcell Datalogger — IMU 3D Orientation Visualizer
============================================================
Reads the $IMU CSV protocol from the STM32 CDC-ACM port
(firmware must be built with VIZ_STREAM defined) and renders
a live 3D board orientation using the SFLP quaternion.

Protocol (20 Hz):
  $IMU,<ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,
       <qw>,<qx>,<qy>,<qz>,<roll>,<pitch>,<yaw>,
       <dx>,<dy>,<dz>,<temp>,<grav>

Requirements:
    pip install pyserial matplotlib numpy

Usage:
    python imu_visualizer.py             # auto-detect STM32 CDC port
    python imu_visualizer.py COM7        # explicit port
    python imu_visualizer.py /dev/ttyACM0
"""

import sys
import re
import threading
import time
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import serial
import serial.tools.list_ports

BAUD_RATE = 115200
UPDATE_MS = 50
ST_VID    = 0x0483

_IMU_RE = re.compile(
    r'\$IMU,(\d+),'                          # timestamp ms
    r'([^,]+),([^,]+),([^,]+),'              # ax, ay, az
    r'([^,]+),([^,]+),([^,]+),'              # gx, gy, gz
    r'([^,]+),([^,]+),([^,]+),([^,]+),'      # qw, qx, qy, qz
    r'([^,]+),([^,]+),([^,]+),'              # roll, pitch, yaw
    r'([^,]+),([^,]+),([^,]+),'              # drift_x, drift_y, drift_z
    r'([^,]+),'                              # temp
    r'(\S+)'                                 # grav axis
)

_DEFAULTS = dict(
    ts=0, ax=0.0, ay=0.0, az=0.0, gx=0.0, gy=0.0, gz=0.0,
    qw=1.0, qx=0.0, qy=0.0, qz=0.0,
    roll=0.0, pitch=0.0, yaw=0.0,
    dx=0.0, dy=0.0, dz=0.0,
    temp=0.0, grav="??", connected=False
)

_state = dict(_DEFAULTS)
_lock  = threading.Lock()


def _find_stm32_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == ST_VID:
            return p.device
        desc = (p.description or "").lower()
        if "stm32" in desc or "stlink" in desc:
            return p.device
    return None


def serial_thread(port):
    global _state
    while True:
        try:
            with serial.Serial(port, BAUD_RATE, timeout=1.0) as ser:
                with _lock:
                    _state["connected"] = True
                print(f"[serial] opened {port} @ {BAUD_RATE}")
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    m = _IMU_RE.search(line)
                    if not m:
                        continue
                    g = m.groups()
                    try:
                        parsed = dict(
                            ts=int(g[0]),
                            ax=float(g[1]),  ay=float(g[2]),  az=float(g[3]),
                            gx=float(g[4]),  gy=float(g[5]),  gz=float(g[6]),
                            qw=float(g[7]),  qx=float(g[8]),
                            qy=float(g[9]),  qz=float(g[10]),
                            roll=float(g[11]), pitch=float(g[12]),
                            yaw=float(g[13]),
                            dx=float(g[14]), dy=float(g[15]),
                            dz=float(g[16]),
                            temp=float(g[17]), grav=g[18],
                        )
                    except (ValueError, IndexError):
                        continue
                    with _lock:
                        _state.update(parsed)
        except serial.SerialException as exc:
            with _lock:
                _state["connected"] = False
            print(f"[serial] {exc}  — retrying in 2 s")
            time.sleep(2)


def quat_to_rotmat(w, x, y, z):
    return np.array([
        [1-2*(y*y+z*z),  2*(x*y-w*z),    2*(x*z+w*y)],
        [2*(x*y+w*z),    1-2*(x*x+z*z),  2*(y*z-w*x)],
        [2*(x*z-w*y),    2*(y*z+w*x),    1-2*(x*x+y*y)]
    ])


_BOX_VERTS = np.array([
    [-1.0, -0.6, -0.075],
    [ 1.0, -0.6, -0.075],
    [ 1.0,  0.6, -0.075],
    [-1.0,  0.6, -0.075],
    [-1.0, -0.6,  0.075],
    [ 1.0, -0.6,  0.075],
    [ 1.0,  0.6,  0.075],
    [-1.0,  0.6,  0.075],
], dtype=float)

_BOX_FACES = [
    [3, 2, 1, 0],   # bottom
    [4, 5, 6, 7],   # top (component side)
    [0, 1, 5, 4],   # front
    [2, 3, 7, 6],   # back
    [0, 3, 7, 4],   # left
    [1, 2, 6, 5],   # right
]
_FACE_COLORS = ["#1a3a6e", "#3a8fd4", "#c0392b", "#7f2a1c", "#27ae60", "#1a6e3a"]
_FACE_ALPHA  = 0.90


def _box_polys(R):
    v = _BOX_VERTS @ R.T
    return [v[idx] for idx in _BOX_FACES]


def _draw_axes(ax, R, length=1.4):
    origin = np.zeros(3)
    for i, (col, lbl) in enumerate(zip(
            ["#e74c3c", "#2ecc71", "#3498db"], ["X", "Y", "Z"])):
        tip = R[:, i] * length
        ax.quiver(*origin, *tip, color=col, linewidth=2.5,
                  arrow_length_ratio=0.18, normalize=False)
        ax.text(*(origin + tip * 1.12), lbl, color=col, fontsize=10,
                fontweight="bold", ha="center", va="center")


matplotlib.rcParams["toolbar"] = "None"
fig = plt.figure(figsize=(11, 8), facecolor="#0d0d1a")
fig.canvas.manager.set_window_title("ValueXT IMU Visualizer — $IMU Protocol")

ax3d = fig.add_axes([0.02, 0.22, 0.70, 0.75], projection="3d")
ax3d.set_facecolor("#12122a")

ax_yaw   = fig.add_axes([0.05, 0.06, 0.27, 0.06])
ax_pitch = fig.add_axes([0.37, 0.06, 0.27, 0.06])
ax_roll  = fig.add_axes([0.69, 0.06, 0.27, 0.06])

for _a, _lbl in [(ax_yaw, "Yaw (deg)"), (ax_pitch, "Pitch (deg)"),
                  (ax_roll, "Roll (deg)")]:
    _a.set_facecolor("#1a1a33")
    _a.set_xlim(-180, 180)
    _a.set_ylim(0, 1)
    _a.set_yticks([])
    _a.tick_params(colors="white", labelsize=7)
    for sp in _a.spines.values():
        sp.set_color("#444466")
    _a.set_title(_lbl, color="#aaaacc", fontsize=8, pad=2)

_yaw_bar,   = ax_yaw.barh(0.5, 0, height=0.6, color="#3498db", left=0)
_pitch_bar, = ax_pitch.barh(0.5, 0, height=0.6, color="#e74c3c", left=0)
_roll_bar,  = ax_roll.barh(0.5, 0, height=0.6, color="#2ecc71", left=0)


def _setup_3d():
    ax3d.set_xlim(-1.8, 1.8)
    ax3d.set_ylim(-1.8, 1.8)
    ax3d.set_zlim(-1.8, 1.8)
    ax3d.set_xlabel("X", color="#aaaacc", labelpad=2)
    ax3d.set_ylabel("Y", color="#aaaacc", labelpad=2)
    ax3d.set_zlabel("Z", color="#aaaacc", labelpad=2)
    ax3d.tick_params(colors="#aaaacc", labelsize=7)
    for pane in (ax3d.xaxis.pane, ax3d.yaxis.pane, ax3d.zaxis.pane):
        pane.fill = False
        pane.set_edgecolor("#1e1e3a")
    ax3d.grid(True, color="#1e1e3a", linewidth=0.5)


def update(_frame):
    with _lock:
        s = dict(_state)

    ax3d.cla()
    _setup_3d()

    R = quat_to_rotmat(s["qw"], s["qx"], s["qy"], s["qz"])
    polys = _box_polys(R)
    pc = Poly3DCollection(polys, zsort="average", alpha=_FACE_ALPHA)
    pc.set_facecolor(_FACE_COLORS)
    pc.set_edgecolor("#ffffff")
    pc.set_linewidth(0.6)
    ax3d.add_collection3d(pc)
    _draw_axes(ax3d, R)

    for v in np.arange(-1.5, 1.6, 0.5):
        ax3d.plot([-1.5, 1.5], [v, v], [-1.5, -1.5],
                  color="#1e1e3a", lw=0.4)
        ax3d.plot([v, v], [-1.5, 1.5], [-1.5, -1.5],
                  color="#1e1e3a", lw=0.4)

    amag = np.sqrt(s["ax"]**2 + s["ay"]**2 + s["az"]**2)
    title = (
        f"R: {s['roll']:+7.2f}  P: {s['pitch']:+7.2f}  "
        f"Y: {s['yaw']:+7.2f} deg\n"
        f"|a|: {amag:.3f} m/s²   T: {s['temp']:.1f}°C   "
        f"Grav: {s['grav']}"
    )
    ax3d.set_title(title, color="white", fontsize=10, pad=6)

    conn_str = "LIVE" if s["connected"] else "NO SIGNAL"
    conn_col = "#27ae60" if s["connected"] else "#e74c3c"

    # Remove old text annotations
    for txt in list(fig.texts):
        txt.remove()

    fig.text(0.97, 0.97, conn_str, color=conn_col, fontsize=9,
             ha="right", va="top", fontweight="bold")

    # Right-side info panel
    rx = 0.74
    ry = 0.92
    ls = 0.028
    info = [
        ("QUATERNION", "#aaaacc", True),
        (f"  W: {s['qw']:+.5f}", "#ffffff", False),
        (f"  X: {s['qx']:+.5f}", "#ffffff", False),
        (f"  Y: {s['qy']:+.5f}", "#ffffff", False),
        (f"  Z: {s['qz']:+.5f}", "#ffffff", False),
        ("", "#ffffff", False),
        ("ACCEL (m/s²)", "#aaaacc", True),
        (f"  X: {s['ax']:+7.3f}", "#ffffff", False),
        (f"  Y: {s['ay']:+7.3f}", "#ffffff", False),
        (f"  Z: {s['az']:+7.3f}", "#ffffff", False),
        ("", "#ffffff", False),
        ("GYRO (dps)", "#aaaacc", True),
        (f"  X: {s['gx']:+7.1f}", "#ffffff", False),
        (f"  Y: {s['gy']:+7.1f}", "#ffffff", False),
        (f"  Z: {s['gz']:+7.1f}", "#ffffff", False),
        ("", "#ffffff", False),
        ("DRIFT (deg)", "#aaaacc", True),
        (f"  X: {s['dx']:+7.3f}", "#ffffff", False),
        (f"  Y: {s['dy']:+7.3f}", "#ffffff", False),
        (f"  Z: {s['dz']:+7.3f}", "#ffffff", False),
        ("", "#ffffff", False),
        (f"TEMP: {s['temp']:+.1f} °C", "#f39c12", True),
        (f"GRAV: {s['grav']}", "#3498db", True),
        (f"  t: {s['ts']} ms", "#666688", False),
    ]
    for i, (text, col, bold) in enumerate(info):
        fig.text(rx, ry - i * ls, text, color=col, fontsize=8,
                 fontfamily="monospace",
                 fontweight="bold" if bold else "normal")

    for bar, val in [(_yaw_bar, s["yaw"]),
                     (_pitch_bar, s["pitch"]),
                     (_roll_bar, s["roll"])]:
        bar.set_width(val)
        bar.set_x(min(val, 0))

    return []


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else None
    if port is None:
        port = _find_stm32_port()
    if port is None:
        available = [p.device for p in serial.tools.list_ports.comports()]
        print("Available ports:", available or "(none found)")
        port = input("Enter COM port (e.g. COM7 or /dev/ttyACM0): ").strip()

    reader = threading.Thread(target=serial_thread, args=(port,), daemon=True)
    reader.start()

    _setup_3d()
    ani = animation.FuncAnimation(
        fig, update, interval=UPDATE_MS,
        blit=False, cache_frame_data=False
    )
    plt.show()
