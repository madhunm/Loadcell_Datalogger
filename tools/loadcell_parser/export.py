"""
Data Export Module

Export log data to various formats: CSV, JSON, and pandas DataFrame.
"""

import csv
import json
from pathlib import Path
from typing import Optional, Callable, List, Dict, Any
from .parser import LogFile


def to_csv(log: LogFile, output_path: str,
           include_imu: bool = True,
           calibration: Optional[Callable[[int], float]] = None,
           accel_scale: int = 0,
           progress_callback: Optional[Callable[[int, int], None]] = None) -> int:
    """
    Export log data to CSV format.
    
    Args:
        log: LogFile instance
        output_path: Output CSV file path
        include_imu: Whether to include IMU data columns
        calibration: Optional function to convert raw ADC to kg
        accel_scale: IMU accelerometer scale setting
        progress_callback: Optional callback(current, total) for progress
    
    Returns:
        Number of rows written
    """
    path = Path(output_path)
    
    # Determine columns
    adc_columns = ['timestamp_s', 'raw_adc', 'sequence_num']
    if calibration:
        adc_columns.extend(['load_kg', 'load_n'])
    
    imu_columns = []
    if include_imu:
        imu_columns = ['accel_x_g', 'accel_y_g', 'accel_z_g',
                       'gyro_x_dps', 'gyro_y_dps', 'gyro_z_dps']
    
    columns = adc_columns + imu_columns
    
    # Get total count for progress
    total = log.adc_count
    
    rows_written = 0
    
    with open(path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(columns)
        
        # Create iterators
        adc_iter = log.iter_adc()
        imu_iter = iter(log.iter_imu()) if include_imu else iter([])
        
        current_imu = None
        if include_imu:
            try:
                current_imu = next(imu_iter)
            except StopIteration:
                current_imu = None
        
        for i, adc in enumerate(adc_iter):
            row = [
                f"{adc.timestamp_s:.6f}",
                adc.raw_adc,
                adc.sequence_num
            ]
            
            if calibration:
                kg = calibration(adc.raw_adc)
                row.extend([f"{kg:.6f}", f"{kg * 9.81:.6f}"])
            
            # Add IMU data (interpolated to closest timestamp)
            if include_imu:
                if current_imu and current_imu.timestamp_s <= adc.timestamp_s:
                    ax, ay, az = current_imu.accel_g(accel_scale)
                    gx, gy, gz = current_imu.gyro_dps(accel_scale)
                    row.extend([
                        f"{ax:.4f}", f"{ay:.4f}", f"{az:.4f}",
                        f"{gx:.2f}", f"{gy:.2f}", f"{gz:.2f}"
                    ])
                    try:
                        current_imu = next(imu_iter)
                    except StopIteration:
                        current_imu = None
                else:
                    row.extend(['', '', '', '', '', ''])
            
            writer.writerow(row)
            rows_written += 1
            
            if progress_callback and i % 10000 == 0:
                progress_callback(i, total)
    
    return rows_written


def to_json(log: LogFile, output_path: str,
            include_data: bool = False,
            calibration: Optional[Callable[[int], float]] = None) -> None:
    """
    Export log metadata and optionally data to JSON format.
    
    Args:
        log: LogFile instance
        output_path: Output JSON file path
        include_data: Whether to include all sample data (can be large!)
        calibration: Optional function to convert raw ADC to kg
    """
    from .analysis import find_peaks, find_accel_peaks, get_load_statistics
    
    result = {
        'file': {
            'path': str(log.filepath),
            'format_version': log.header.version,
            'is_complete': log.is_complete,
        },
        'header': {
            'loadcell_id': log.header.loadcell_id,
            'adc_sample_rate_hz': log.header.adc_sample_rate_hz,
            'imu_sample_rate_hz': log.header.imu_sample_rate_hz,
            'start_timestamp_us': log.header.start_timestamp_us,
            'adc_gain': log.header.adc_gain,
            'adc_bits': log.header.adc_bits,
        },
        'summary': {
            'duration_s': log.duration_seconds,
            'adc_samples': log.adc_count,
            'imu_samples': log.imu_count,
            'dropped_samples': log.dropped_count,
        }
    }
    
    # Add footer info if present
    if log.footer:
        result['footer'] = {
            'total_adc_samples': log.footer.total_adc_samples,
            'total_imu_samples': log.footer.total_imu_samples,
            'dropped_samples': log.footer.dropped_samples,
            'crc32': f"0x{log.footer.crc32:08X}",
        }
    
    # Add peak analysis
    load_peaks = find_peaks(log, calibration=calibration)
    accel_peaks = find_accel_peaks(log)
    
    result['peaks'] = {
        'max_load_n': load_peaks.max_load_n,
        'max_load_time_s': load_peaks.max_load_time_s,
        'min_load_n': load_peaks.min_load_n,
        'min_load_time_s': load_peaks.min_load_time_s,
        'max_accel_g': accel_peaks.max_g,
        'max_accel_time_s': accel_peaks.max_g_time_s,
    }
    
    # Add statistics
    stats = get_load_statistics(log, calibration)
    result['statistics'] = {
        'load_mean_n': stats.mean,
        'load_std_n': stats.std,
        'load_rms_n': stats.rms,
    }
    
    # Optionally include all data
    if include_data:
        result['adc_data'] = [
            {
                'timestamp_s': adc.timestamp_s,
                'raw': adc.raw_adc,
                'seq': adc.sequence_num
            }
            for adc in log.iter_adc()
        ]
        
        result['imu_data'] = [
            {
                'timestamp_s': imu.timestamp_s,
                'accel': list(imu.accel_g()),
                'gyro': list(imu.gyro_dps())
            }
            for imu in log.iter_imu()
        ]
    
    with open(output_path, 'w') as f:
        json.dump(result, f, indent=2)


def to_dataframe(log: LogFile,
                 data_type: str = 'adc',
                 calibration: Optional[Callable[[int], float]] = None,
                 accel_scale: int = 0):
    """
    Convert log data to pandas DataFrame.
    
    Args:
        log: LogFile instance
        data_type: 'adc', 'imu', or 'merged'
        calibration: Optional function to convert raw ADC to kg
        accel_scale: IMU accelerometer scale setting
    
    Returns:
        pandas DataFrame
    
    Raises:
        ImportError if pandas is not installed
    """
    try:
        import pandas as pd
    except ImportError:
        raise ImportError("pandas is required for DataFrame export. "
                        "Install with: pip install pandas")
    
    if data_type == 'adc':
        data = []
        for adc in log.iter_adc():
            row = {
                'timestamp_s': adc.timestamp_s,
                'raw_adc': adc.raw_adc,
                'sequence_num': adc.sequence_num,
            }
            if calibration:
                kg = calibration(adc.raw_adc)
                row['load_kg'] = kg
                row['load_n'] = kg * 9.81
            data.append(row)
        return pd.DataFrame(data)
    
    elif data_type == 'imu':
        data = []
        for imu in log.iter_imu():
            ax, ay, az = imu.accel_g(accel_scale)
            gx, gy, gz = imu.gyro_dps(accel_scale)
            data.append({
                'timestamp_s': imu.timestamp_s,
                'accel_x_g': ax,
                'accel_y_g': ay,
                'accel_z_g': az,
                'gyro_x_dps': gx,
                'gyro_y_dps': gy,
                'gyro_z_dps': gz,
            })
        return pd.DataFrame(data)
    
    elif data_type == 'merged':
        # Create both dataframes and merge on timestamp
        adc_df = to_dataframe(log, 'adc', calibration)
        imu_df = to_dataframe(log, 'imu', accel_scale=accel_scale)
        
        # Merge with nearest timestamp (forward fill IMU to ADC rate)
        return pd.merge_asof(
            adc_df.sort_values('timestamp_s'),
            imu_df.sort_values('timestamp_s'),
            on='timestamp_s',
            direction='nearest'
        )
    
    else:
        raise ValueError(f"Unknown data_type: {data_type}")


def to_numpy(log: LogFile,
             data_type: str = 'adc',
             calibration: Optional[Callable[[int], float]] = None):
    """
    Convert log data to numpy arrays.
    
    Args:
        log: LogFile instance
        data_type: 'adc' or 'imu'
        calibration: Optional function to convert raw ADC to kg
    
    Returns:
        Dictionary of numpy arrays
    
    Raises:
        ImportError if numpy is not installed
    """
    try:
        import numpy as np
    except ImportError:
        raise ImportError("numpy is required for numpy export. "
                        "Install with: pip install numpy")
    
    if data_type == 'adc':
        records = list(log.iter_adc())
        result = {
            'timestamp_s': np.array([r.timestamp_s for r in records]),
            'raw_adc': np.array([r.raw_adc for r in records], dtype=np.int32),
            'sequence_num': np.array([r.sequence_num for r in records], dtype=np.uint32),
        }
        if calibration:
            result['load_kg'] = np.array([calibration(r.raw_adc) for r in records])
            result['load_n'] = result['load_kg'] * 9.81
        return result
    
    elif data_type == 'imu':
        records = list(log.iter_imu())
        return {
            'timestamp_s': np.array([r.timestamp_s for r in records]),
            'accel_x': np.array([r.accel_x for r in records], dtype=np.int16),
            'accel_y': np.array([r.accel_y for r in records], dtype=np.int16),
            'accel_z': np.array([r.accel_z for r in records], dtype=np.int16),
            'gyro_x': np.array([r.gyro_x for r in records], dtype=np.int16),
            'gyro_y': np.array([r.gyro_y for r in records], dtype=np.int16),
            'gyro_z': np.array([r.gyro_z for r in records], dtype=np.int16),
        }
    
    else:
        raise ValueError(f"Unknown data_type: {data_type}")

