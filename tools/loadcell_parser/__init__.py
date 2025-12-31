"""
Loadcell Datalogger Binary File Parser

A Python library for parsing, validating, and analyzing binary log files
from the ESP32-S3 Loadcell Datalogger.

Example usage:
    from loadcell_parser import LogFile
    
    log = LogFile('data.bin')
    print(f"Duration: {log.duration_seconds:.2f}s")
    print(f"ADC samples: {log.adc_count}")
    
    # Validate file integrity
    report = log.validate()
    if report.is_valid:
        print("File integrity verified!")
    
    # Export to CSV
    log.to_csv('output.csv')
    
    # Find peaks
    peaks = log.find_peaks(threshold_n=100)
    print(f"Peak load: {peaks.max_load_n:.2f} N at {peaks.max_load_time_s:.3f}s")
"""

from .parser import LogFile, FileHeader, ADCRecord, IMURecord, EventRecord, FileFooter
from .validator import ValidationReport, validate_file
from .analysis import find_peaks, PeakInfo
from .export import to_csv, to_dataframe

__version__ = '1.0.0'
__all__ = [
    'LogFile',
    'FileHeader',
    'ADCRecord',
    'IMURecord',
    'EventRecord',
    'FileFooter',
    'ValidationReport',
    'validate_file',
    'find_peaks',
    'PeakInfo',
    'to_csv',
    'to_dataframe',
]


