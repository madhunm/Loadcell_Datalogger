"""
Data Analysis Module

Provides peak detection, statistics, and signal analysis for loadcell data.
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple, Callable
from .parser import LogFile, ADCRecord, IMURecord


@dataclass
class PeakInfo:
    """Information about a detected peak"""
    value: float            # Peak value
    timestamp_s: float      # Timestamp in seconds
    record_index: int       # Index in record list
    raw_value: int          # Original raw value


@dataclass
class LoadPeaks:
    """Load peak detection results"""
    max_load_n: float = 0.0
    max_load_time_s: float = 0.0
    min_load_n: float = 0.0
    min_load_time_s: float = 0.0
    peaks_above_threshold: List[PeakInfo] = None
    
    def __post_init__(self):
        if self.peaks_above_threshold is None:
            self.peaks_above_threshold = []


@dataclass  
class AccelPeaks:
    """Acceleration peak detection results"""
    max_g: float = 0.0
    max_g_time_s: float = 0.0
    max_component: Tuple[float, float, float] = (0.0, 0.0, 0.0)  # (ax, ay, az) at peak
    peaks_above_threshold: List[PeakInfo] = None
    
    def __post_init__(self):
        if self.peaks_above_threshold is None:
            self.peaks_above_threshold = []


@dataclass
class Statistics:
    """Statistical summary of data"""
    count: int = 0
    mean: float = 0.0
    std: float = 0.0
    min_val: float = 0.0
    max_val: float = 0.0
    rms: float = 0.0


def raw_to_newtons(raw: int, calibration: Optional[Callable[[int], float]] = None) -> float:
    """
    Convert raw ADC value to Newtons.
    
    Args:
        raw: Raw 24-bit ADC value
        calibration: Optional calibration function (raw -> kg)
                    If None, assumes 1:1 mapping (raw = microvolts = kg)
    
    Returns:
        Load in Newtons
    """
    if calibration:
        kg = calibration(raw)
    else:
        # Default: assume raw is approximately proportional to kg
        # This is a placeholder - real calibration should be provided
        kg = raw / 1000000.0  # Very rough approximation
    
    return kg * 9.81


def find_peaks(log: LogFile, 
               threshold_n: float = 0.0,
               calibration: Optional[Callable[[int], float]] = None) -> LoadPeaks:
    """
    Find peak load values in a log file.
    
    Args:
        log: LogFile instance
        threshold_n: Minimum load (N) to consider as a peak
        calibration: Function to convert raw ADC to kg
    
    Returns:
        LoadPeaks with max/min values and peaks above threshold
    """
    result = LoadPeaks()
    
    max_load = float('-inf')
    min_load = float('inf')
    max_time = 0.0
    min_time = 0.0
    max_raw = 0
    min_raw = 0
    
    peaks = []
    
    for i, adc in enumerate(log.iter_adc()):
        load_n = raw_to_newtons(adc.raw_adc, calibration)
        
        if load_n > max_load:
            max_load = load_n
            max_time = adc.timestamp_s
            max_raw = adc.raw_adc
        
        if load_n < min_load:
            min_load = load_n
            min_time = adc.timestamp_s
            min_raw = adc.raw_adc
        
        # Collect peaks above threshold
        if abs(load_n) >= threshold_n:
            peaks.append(PeakInfo(
                value=load_n,
                timestamp_s=adc.timestamp_s,
                record_index=i,
                raw_value=adc.raw_adc
            ))
    
    result.max_load_n = max_load if max_load != float('-inf') else 0.0
    result.max_load_time_s = max_time
    result.min_load_n = min_load if min_load != float('inf') else 0.0
    result.min_load_time_s = min_time
    result.peaks_above_threshold = peaks
    
    return result


def find_accel_peaks(log: LogFile, 
                     threshold_g: float = 1.0,
                     accel_scale: int = 0) -> AccelPeaks:
    """
    Find peak acceleration (deceleration) values.
    
    Args:
        log: LogFile instance
        threshold_g: Minimum acceleration magnitude to record as peak
        accel_scale: IMU accelerometer scale setting
    
    Returns:
        AccelPeaks with max values and peaks above threshold
    """
    result = AccelPeaks()
    
    max_g = 0.0
    max_time = 0.0
    max_components = (0.0, 0.0, 0.0)
    
    peaks = []
    
    for i, imu in enumerate(log.iter_imu()):
        ax, ay, az = imu.accel_g(accel_scale)
        mag = imu.accel_magnitude_g(accel_scale)
        
        if mag > max_g:
            max_g = mag
            max_time = imu.timestamp_s
            max_components = (ax, ay, az)
        
        if mag >= threshold_g:
            peaks.append(PeakInfo(
                value=mag,
                timestamp_s=imu.timestamp_s,
                record_index=i,
                raw_value=0  # Not applicable for IMU
            ))
    
    result.max_g = max_g
    result.max_g_time_s = max_time
    result.max_component = max_components
    result.peaks_above_threshold = peaks
    
    return result


def compute_statistics(values: List[float]) -> Statistics:
    """
    Compute statistical summary of values.
    
    Args:
        values: List of numeric values
    
    Returns:
        Statistics dataclass
    """
    if not values:
        return Statistics()
    
    n = len(values)
    mean = sum(values) / n
    
    # Standard deviation
    variance = sum((x - mean) ** 2 for x in values) / n
    std = variance ** 0.5
    
    # RMS
    rms = (sum(x ** 2 for x in values) / n) ** 0.5
    
    return Statistics(
        count=n,
        mean=mean,
        std=std,
        min_val=min(values),
        max_val=max(values),
        rms=rms
    )


def get_load_statistics(log: LogFile,
                        calibration: Optional[Callable[[int], float]] = None) -> Statistics:
    """
    Compute statistics for load data.
    
    Args:
        log: LogFile instance
        calibration: Function to convert raw ADC to kg
    
    Returns:
        Statistics in Newtons
    """
    loads = [raw_to_newtons(adc.raw_adc, calibration) for adc in log.iter_adc()]
    return compute_statistics(loads)


def get_accel_statistics(log: LogFile, accel_scale: int = 0) -> Statistics:
    """
    Compute statistics for acceleration magnitude.
    
    Args:
        log: LogFile instance
        accel_scale: IMU accelerometer scale setting
    
    Returns:
        Statistics in g
    """
    accels = [imu.accel_magnitude_g(accel_scale) for imu in log.iter_imu()]
    return compute_statistics(accels)


def detect_events(log: LogFile,
                  load_threshold_n: float = 100.0,
                  accel_threshold_g: float = 2.0,
                  min_duration_s: float = 0.01,
                  calibration: Optional[Callable[[int], float]] = None) -> List[dict]:
    """
    Detect significant events (impacts, drops, etc.) in the data.
    
    An event is defined as a period where load or acceleration exceeds
    the threshold for at least min_duration.
    
    Args:
        log: LogFile instance
        load_threshold_n: Load threshold in Newtons
        accel_threshold_g: Acceleration threshold in g
        min_duration_s: Minimum event duration in seconds
        calibration: Function to convert raw ADC to kg
    
    Returns:
        List of event dictionaries with type, start_time, end_time, peak_value
    """
    events = []
    
    # Detect load events
    in_event = False
    event_start = 0.0
    peak_load = 0.0
    
    for adc in log.iter_adc():
        load = raw_to_newtons(adc.raw_adc, calibration)
        
        if abs(load) >= load_threshold_n:
            if not in_event:
                in_event = True
                event_start = adc.timestamp_s
                peak_load = load
            else:
                if abs(load) > abs(peak_load):
                    peak_load = load
        else:
            if in_event:
                duration = adc.timestamp_s - event_start
                if duration >= min_duration_s:
                    events.append({
                        'type': 'load',
                        'start_time_s': event_start,
                        'end_time_s': adc.timestamp_s,
                        'duration_s': duration,
                        'peak_value': peak_load,
                        'unit': 'N'
                    })
                in_event = False
    
    # Detect acceleration events
    in_event = False
    event_start = 0.0
    peak_accel = 0.0
    
    for imu in log.iter_imu():
        accel = imu.accel_magnitude_g()
        
        if accel >= accel_threshold_g:
            if not in_event:
                in_event = True
                event_start = imu.timestamp_s
                peak_accel = accel
            else:
                if accel > peak_accel:
                    peak_accel = accel
        else:
            if in_event:
                duration = imu.timestamp_s - event_start
                if duration >= min_duration_s:
                    events.append({
                        'type': 'acceleration',
                        'start_time_s': event_start,
                        'end_time_s': imu.timestamp_s,
                        'duration_s': duration,
                        'peak_value': peak_accel,
                        'unit': 'g'
                    })
                in_event = False
    
    # Sort events by start time
    events.sort(key=lambda e: e['start_time_s'])
    
    return events

