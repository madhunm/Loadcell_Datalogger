"""
Visualization Module

Generate plots for loadcell and IMU data using matplotlib.
"""

from typing import Optional, Callable, Tuple, List
from .parser import LogFile
from .analysis import find_peaks, find_accel_peaks


def _check_matplotlib():
    """Check if matplotlib is available"""
    try:
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        raise ImportError("matplotlib is required for plotting. "
                        "Install with: pip install matplotlib")


def plot_load(log: LogFile,
              output_path: Optional[str] = None,
              calibration: Optional[Callable[[int], float]] = None,
              title: str = "Load vs Time",
              figsize: Tuple[int, int] = (12, 6),
              mark_peaks: bool = True,
              peak_threshold_n: float = 0.0) -> None:
    """
    Plot load over time.
    
    Args:
        log: LogFile instance
        output_path: Optional path to save figure (shows if None)
        calibration: Function to convert raw ADC to kg
        title: Plot title
        figsize: Figure size (width, height)
        mark_peaks: Whether to mark peak values
        peak_threshold_n: Threshold for marking peaks
    """
    plt = _check_matplotlib()
    
    # Collect data (sample for large files)
    timestamps = []
    loads = []
    
    max_points = 100000  # Limit for reasonable plot performance
    step = max(1, log.adc_count // max_points)
    
    for i, adc in enumerate(log.iter_adc()):
        if i % step == 0:
            timestamps.append(adc.timestamp_s)
            if calibration:
                kg = calibration(adc.raw_adc)
                loads.append(kg * 9.81)
            else:
                # Show raw values if no calibration
                loads.append(adc.raw_adc / 1000.0)  # Scale for visibility
    
    fig, ax = plt.subplots(figsize=figsize)
    ax.plot(timestamps, loads, linewidth=0.5, color='#2196F3')
    
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Load (N)' if calibration else 'Raw ADC (scaled)')
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    
    # Mark peaks
    if mark_peaks:
        peaks = find_peaks(log, threshold_n=peak_threshold_n, calibration=calibration)
        if peaks.max_load_n > 0:
            ax.axhline(y=peaks.max_load_n, color='r', linestyle='--', alpha=0.5, 
                      label=f'Peak: {peaks.max_load_n:.2f} N')
            ax.scatter([peaks.max_load_time_s], [peaks.max_load_n], 
                      color='r', s=100, zorder=5, marker='v')
            ax.legend()
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        plt.close()
    else:
        plt.show()


def plot_accel(log: LogFile,
               output_path: Optional[str] = None,
               accel_scale: int = 0,
               title: str = "Acceleration vs Time",
               figsize: Tuple[int, int] = (12, 8),
               show_magnitude: bool = True,
               show_components: bool = True) -> None:
    """
    Plot acceleration over time.
    
    Args:
        log: LogFile instance
        output_path: Optional path to save figure
        accel_scale: IMU accelerometer scale setting
        title: Plot title
        figsize: Figure size
        show_magnitude: Whether to plot magnitude
        show_components: Whether to plot individual axes
    """
    plt = _check_matplotlib()
    
    timestamps = []
    ax_list = []
    ay_list = []
    az_list = []
    mag_list = []
    
    for imu in log.iter_imu():
        timestamps.append(imu.timestamp_s)
        ax, ay, az = imu.accel_g(accel_scale)
        ax_list.append(ax)
        ay_list.append(ay)
        az_list.append(az)
        mag_list.append(imu.accel_magnitude_g(accel_scale))
    
    if show_components and show_magnitude:
        fig, axes = plt.subplots(2, 1, figsize=figsize, sharex=True)
        ax_comp, ax_mag = axes
    elif show_components:
        fig, ax_comp = plt.subplots(figsize=figsize)
        ax_mag = None
    else:
        fig, ax_mag = plt.subplots(figsize=figsize)
        ax_comp = None
    
    if ax_comp is not None:
        ax_comp.plot(timestamps, ax_list, label='X', linewidth=0.5, alpha=0.8)
        ax_comp.plot(timestamps, ay_list, label='Y', linewidth=0.5, alpha=0.8)
        ax_comp.plot(timestamps, az_list, label='Z', linewidth=0.5, alpha=0.8)
        ax_comp.set_ylabel('Acceleration (g)')
        ax_comp.legend(loc='upper right')
        ax_comp.grid(True, alpha=0.3)
        ax_comp.set_title(title if ax_mag is None else f"{title} - Components")
    
    if ax_mag is not None:
        ax_mag.plot(timestamps, mag_list, linewidth=0.5, color='#E91E63')
        ax_mag.set_xlabel('Time (s)')
        ax_mag.set_ylabel('Magnitude (g)')
        ax_mag.grid(True, alpha=0.3)
        ax_mag.set_title("Acceleration Magnitude" if ax_comp else title)
        
        # Mark peak
        peaks = find_accel_peaks(log, accel_scale=accel_scale)
        if peaks.max_g > 0:
            ax_mag.axhline(y=peaks.max_g, color='r', linestyle='--', alpha=0.5)
            ax_mag.scatter([peaks.max_g_time_s], [peaks.max_g], 
                          color='r', s=100, zorder=5, marker='v',
                          label=f'Peak: {peaks.max_g:.2f} g')
            ax_mag.legend()
    
    if ax_comp is not None and ax_mag is None:
        ax_comp.set_xlabel('Time (s)')
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        plt.close()
    else:
        plt.show()


def plot_gyro(log: LogFile,
              output_path: Optional[str] = None,
              gyro_scale: int = 0,
              title: str = "Angular Rate vs Time",
              figsize: Tuple[int, int] = (12, 6)) -> None:
    """
    Plot gyroscope data over time.
    
    Args:
        log: LogFile instance
        output_path: Optional path to save figure
        gyro_scale: IMU gyroscope scale setting
        title: Plot title
        figsize: Figure size
    """
    plt = _check_matplotlib()
    
    timestamps = []
    gx_list = []
    gy_list = []
    gz_list = []
    
    for imu in log.iter_imu():
        timestamps.append(imu.timestamp_s)
        gx, gy, gz = imu.gyro_dps(gyro_scale)
        gx_list.append(gx)
        gy_list.append(gy)
        gz_list.append(gz)
    
    fig, ax = plt.subplots(figsize=figsize)
    ax.plot(timestamps, gx_list, label='X', linewidth=0.5, alpha=0.8)
    ax.plot(timestamps, gy_list, label='Y', linewidth=0.5, alpha=0.8)
    ax.plot(timestamps, gz_list, label='Z', linewidth=0.5, alpha=0.8)
    
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Angular Rate (°/s)')
    ax.set_title(title)
    ax.legend(loc='upper right')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        plt.close()
    else:
        plt.show()


def plot_summary(log: LogFile,
                 output_path: Optional[str] = None,
                 calibration: Optional[Callable[[int], float]] = None,
                 accel_scale: int = 0,
                 figsize: Tuple[int, int] = (14, 10)) -> None:
    """
    Generate summary plot with load, acceleration, and statistics.
    
    Args:
        log: LogFile instance
        output_path: Optional path to save figure
        calibration: Function to convert raw ADC to kg
        accel_scale: IMU accelerometer scale setting
        figsize: Figure size
    """
    plt = _check_matplotlib()
    
    fig = plt.figure(figsize=figsize)
    
    # Create grid
    gs = fig.add_gridspec(3, 2, height_ratios=[2, 2, 1], hspace=0.3, wspace=0.3)
    
    # Load plot (top left)
    ax_load = fig.add_subplot(gs[0, 0])
    timestamps = []
    loads = []
    step = max(1, log.adc_count // 50000)
    for i, adc in enumerate(log.iter_adc()):
        if i % step == 0:
            timestamps.append(adc.timestamp_s)
            if calibration:
                loads.append(calibration(adc.raw_adc) * 9.81)
            else:
                loads.append(adc.raw_adc / 1000.0)
    
    ax_load.plot(timestamps, loads, linewidth=0.5, color='#2196F3')
    ax_load.set_ylabel('Load (N)' if calibration else 'Raw (scaled)')
    ax_load.set_title('Load')
    ax_load.grid(True, alpha=0.3)
    
    # Acceleration magnitude plot (top right)
    ax_accel = fig.add_subplot(gs[0, 1])
    timestamps = []
    mags = []
    for imu in log.iter_imu():
        timestamps.append(imu.timestamp_s)
        mags.append(imu.accel_magnitude_g(accel_scale))
    
    ax_accel.plot(timestamps, mags, linewidth=0.5, color='#E91E63')
    ax_accel.set_ylabel('Acceleration (g)')
    ax_accel.set_title('Acceleration Magnitude')
    ax_accel.grid(True, alpha=0.3)
    
    # Acceleration components (middle left)
    ax_comp = fig.add_subplot(gs[1, 0])
    timestamps = []
    ax_list, ay_list, az_list = [], [], []
    for imu in log.iter_imu():
        timestamps.append(imu.timestamp_s)
        ax, ay, az = imu.accel_g(accel_scale)
        ax_list.append(ax)
        ay_list.append(ay)
        az_list.append(az)
    
    ax_comp.plot(timestamps, ax_list, label='X', linewidth=0.5, alpha=0.8)
    ax_comp.plot(timestamps, ay_list, label='Y', linewidth=0.5, alpha=0.8)
    ax_comp.plot(timestamps, az_list, label='Z', linewidth=0.5, alpha=0.8)
    ax_comp.set_xlabel('Time (s)')
    ax_comp.set_ylabel('Acceleration (g)')
    ax_comp.set_title('Acceleration Components')
    ax_comp.legend(loc='upper right')
    ax_comp.grid(True, alpha=0.3)
    
    # Gyro (middle right)
    ax_gyro = fig.add_subplot(gs[1, 1])
    timestamps = []
    gx_list, gy_list, gz_list = [], [], []
    for imu in log.iter_imu():
        timestamps.append(imu.timestamp_s)
        gx, gy, gz = imu.gyro_dps()
        gx_list.append(gx)
        gy_list.append(gy)
        gz_list.append(gz)
    
    ax_gyro.plot(timestamps, gx_list, label='X', linewidth=0.5, alpha=0.8)
    ax_gyro.plot(timestamps, gy_list, label='Y', linewidth=0.5, alpha=0.8)
    ax_gyro.plot(timestamps, gz_list, label='Z', linewidth=0.5, alpha=0.8)
    ax_gyro.set_xlabel('Time (s)')
    ax_gyro.set_ylabel('Angular Rate (°/s)')
    ax_gyro.set_title('Gyroscope')
    ax_gyro.legend(loc='upper right')
    ax_gyro.grid(True, alpha=0.3)
    
    # Statistics text (bottom)
    ax_stats = fig.add_subplot(gs[2, :])
    ax_stats.axis('off')
    
    load_peaks = find_peaks(log, calibration=calibration)
    accel_peaks = find_accel_peaks(log, accel_scale=accel_scale)
    
    stats_text = (
        f"File: {log.filepath.name}\n"
        f"Duration: {log.duration_seconds:.2f}s | "
        f"ADC Samples: {log.adc_count:,} | "
        f"IMU Samples: {log.imu_count:,} | "
        f"Dropped: {log.dropped_count}\n\n"
        f"Peak Load: {load_peaks.max_load_n:.2f} N at {load_peaks.max_load_time_s:.3f}s\n"
        f"Peak Accel: {accel_peaks.max_g:.2f} g at {accel_peaks.max_g_time_s:.3f}s"
    )
    
    ax_stats.text(0.5, 0.5, stats_text, transform=ax_stats.transAxes,
                  fontsize=11, verticalalignment='center', horizontalalignment='center',
                  fontfamily='monospace',
                  bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    plt.suptitle(f"Log Summary: {log.header.loadcell_id or 'Unknown'}", fontsize=14)
    
    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
    else:
        plt.show()

