"""
Binary Log File Validator

Validates log file integrity through:
- Header/footer magic number verification
- CRC32 checksum validation
- Sequence number gap detection
- Sample count verification
"""

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from pathlib import Path

from .parser import (
    LogFile, FileHeader, ADCRecord, FileFooter,
    FILE_MAGIC, FOOTER_MAGIC, HEADER_SIZE, ADC_RECORD_SIZE
)


def crc32_ieee(data: bytes, crc: int = 0) -> int:
    """
    Calculate CRC32 using IEEE 802.3 polynomial (0x04C11DB7).
    Compatible with ESP32's esp_crc32_le() function.
    """
    import binascii
    # Python's binascii.crc32 uses same polynomial as ESP32
    return binascii.crc32(data, crc) & 0xFFFFFFFF


@dataclass
class Gap:
    """Represents a gap in sequence numbers"""
    position: int           # Record index where gap was detected
    expected_seq: int       # Expected sequence number
    actual_seq: int         # Actual sequence number found
    missing_count: int      # Number of missing samples
    timestamp_us: int       # Timestamp where gap occurred


@dataclass
class ValidationReport:
    """Results of file validation"""
    filepath: str
    is_valid: bool = True
    header_valid: bool = True
    footer_valid: bool = True
    footer_present: bool = True
    crc_valid: bool = True
    crc_expected: int = 0
    crc_computed: int = 0
    
    adc_count: int = 0
    imu_count: int = 0
    expected_adc_count: int = 0
    expected_imu_count: int = 0
    
    gaps: List[Gap] = field(default_factory=list)
    total_missing: int = 0
    
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    
    def add_error(self, msg: str):
        """Add an error and mark as invalid"""
        self.errors.append(msg)
        self.is_valid = False
    
    def add_warning(self, msg: str):
        """Add a warning (doesn't affect validity)"""
        self.warnings.append(msg)
    
    def summary(self) -> str:
        """Generate human-readable summary"""
        lines = [
            f"Validation Report: {self.filepath}",
            "=" * 50,
            f"Status: {'VALID' if self.is_valid else 'INVALID'}",
            f"",
            f"Header:  {'OK' if self.header_valid else 'INVALID'}",
            f"Footer:  {'OK' if self.footer_valid else 'MISSING/INVALID'}",
            f"CRC32:   {'OK' if self.crc_valid else 'MISMATCH'}",
        ]
        
        if not self.crc_valid:
            lines.append(f"  Expected: 0x{self.crc_expected:08X}")
            lines.append(f"  Computed: 0x{self.crc_computed:08X}")
        
        lines.extend([
            f"",
            f"ADC Samples:  {self.adc_count:,} (expected: {self.expected_adc_count:,})",
            f"IMU Samples:  {self.imu_count:,} (expected: {self.expected_imu_count:,})",
        ])
        
        if self.gaps:
            lines.extend([
                f"",
                f"Sequence Gaps: {len(self.gaps)}",
                f"Total Missing: {self.total_missing:,} samples",
            ])
            
            # Show first few gaps
            for gap in self.gaps[:5]:
                lines.append(f"  - Gap at record {gap.position}: "
                           f"expected seq {gap.expected_seq}, got {gap.actual_seq} "
                           f"({gap.missing_count} missing)")
            if len(self.gaps) > 5:
                lines.append(f"  ... and {len(self.gaps) - 5} more gaps")
        
        if self.errors:
            lines.extend(["", "Errors:"])
            for err in self.errors:
                lines.append(f"  - {err}")
        
        if self.warnings:
            lines.extend(["", "Warnings:"])
            for warn in self.warnings:
                lines.append(f"  - {warn}")
        
        return "\n".join(lines)


def validate_file(filepath: str, check_crc: bool = True, 
                  check_gaps: bool = True) -> ValidationReport:
    """
    Validate a binary log file.
    
    Args:
        filepath: Path to the log file
        check_crc: Whether to verify CRC32 checksum
        check_gaps: Whether to check for sequence number gaps
    
    Returns:
        ValidationReport with detailed results
    """
    report = ValidationReport(filepath=filepath)
    path = Path(filepath)
    
    if not path.exists():
        report.add_error(f"File not found: {filepath}")
        return report
    
    file_size = path.stat().st_size
    if file_size < HEADER_SIZE:
        report.add_error(f"File too small: {file_size} bytes")
        return report
    
    try:
        log = LogFile(filepath)
    except Exception as e:
        report.add_error(f"Failed to parse file: {e}")
        return report
    
    # Validate header
    if not log.header.is_valid:
        report.header_valid = False
        report.add_error(f"Invalid header magic: 0x{log.header.magic:08X}")
    
    # Check footer
    if log.footer is None:
        report.footer_present = False
        report.footer_valid = False
        report.add_warning("Footer missing - file may have ended uncleanly")
    elif not log.footer.is_valid:
        report.footer_valid = False
        report.add_error(f"Invalid footer magic: 0x{log.footer.magic:08X}")
    else:
        report.expected_adc_count = log.footer.total_adc_samples
        report.expected_imu_count = log.footer.total_imu_samples
    
    # Count records and check gaps
    adc_count = 0
    imu_count = 0
    expected_seq = 0
    
    with open(filepath, 'rb') as f:
        # Skip header
        f.seek(HEADER_SIZE)
        
        # Calculate CRC if requested
        if check_crc and log.footer and log.footer.crc32 != 0:
            f.seek(0)
            # Read all data except footer
            data_size = file_size - 32  # Exclude footer
            data = f.read(data_size)
            report.crc_computed = crc32_ieee(data)
            report.crc_expected = log.footer.crc32
            
            if report.crc_computed != report.crc_expected:
                report.crc_valid = False
                report.add_error("CRC32 mismatch - file may be corrupted")
            
            # Reset to after header
            f.seek(HEADER_SIZE)
        
        # Parse records
        imu_decimation = (log.header.adc_sample_rate_hz // 
                        log.header.imu_sample_rate_hz) if log.header.imu_sample_rate_hz > 0 else 0
        
        record_index = 0
        while True:
            data = f.read(ADC_RECORD_SIZE)
            if len(data) < ADC_RECORD_SIZE:
                break
            
            # Check for end record
            if data[0] == 0xFF:
                break
            
            adc = ADCRecord.from_bytes(data)
            adc_count += 1
            
            # Check sequence gaps
            if check_gaps:
                if adc.sequence_num != expected_seq:
                    gap = Gap(
                        position=record_index,
                        expected_seq=expected_seq,
                        actual_seq=adc.sequence_num,
                        missing_count=adc.sequence_num - expected_seq,
                        timestamp_us=adc.timestamp_offset_us
                    )
                    report.gaps.append(gap)
                    report.total_missing += gap.missing_count
                
                expected_seq = adc.sequence_num + 1
            
            record_index += 1
            
            # Skip IMU record if interleaved
            if imu_decimation > 0 and adc_count % imu_decimation == 0:
                imu_data = f.read(16)
                if len(imu_data) >= 16:
                    imu_count += 1
    
    report.adc_count = adc_count
    report.imu_count = imu_count
    
    # Verify counts match footer
    if report.footer_valid and report.footer_present:
        if adc_count != report.expected_adc_count:
            report.add_warning(f"ADC count mismatch: found {adc_count}, "
                             f"footer says {report.expected_adc_count}")
        if imu_count != report.expected_imu_count:
            report.add_warning(f"IMU count mismatch: found {imu_count}, "
                             f"footer says {report.expected_imu_count}")
    
    # Gap summary
    if report.gaps:
        report.add_warning(f"Found {len(report.gaps)} sequence gaps "
                         f"({report.total_missing} missing samples)")
    
    return report


def find_gaps(filepath: str) -> List[Gap]:
    """
    Find all sequence number gaps in a log file.
    
    Returns list of Gap objects describing each discontinuity.
    """
    report = validate_file(filepath, check_crc=False, check_gaps=True)
    return report.gaps


def verify_crc32(filepath: str) -> Tuple[bool, int, int]:
    """
    Verify file CRC32.
    
    Returns:
        (is_valid, expected_crc, computed_crc)
    """
    report = validate_file(filepath, check_crc=True, check_gaps=False)
    return (report.crc_valid, report.crc_expected, report.crc_computed)


