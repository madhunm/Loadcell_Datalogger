"""
Binary Log File Parser

Parses binary log files from the ESP32-S3 Loadcell Datalogger.
Supports lazy loading for memory-efficient processing of large files.
"""

import struct
from dataclasses import dataclass
from typing import Iterator, Optional, BinaryIO, List, Tuple
from pathlib import Path
from datetime import datetime


# Constants from binary_format.h
FILE_MAGIC = 0x474C434C  # "LCLG"
FOOTER_MAGIC = 0xF007F007
FORMAT_VERSION = 1
HEADER_SIZE = 64
ADC_RECORD_SIZE = 12
IMU_RECORD_SIZE = 16


@dataclass
class FileHeader:
    """Log file header (64 bytes)"""
    magic: int
    version: int
    header_size: int
    adc_sample_rate_hz: int
    imu_sample_rate_hz: int
    start_timestamp_us: int
    loadcell_id: str
    flags: int
    adc_gain: int
    adc_bits: int
    imu_accel_scale: int
    imu_gyro_scale: int
    
    @property
    def start_datetime(self) -> datetime:
        """Convert start timestamp to datetime"""
        return datetime.fromtimestamp(self.start_timestamp_us / 1_000_000)
    
    @property
    def is_valid(self) -> bool:
        """Check if header is valid"""
        return (self.magic == FILE_MAGIC and 
                self.version == FORMAT_VERSION and
                self.header_size == HEADER_SIZE)
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'FileHeader':
        """Parse header from bytes"""
        if len(data) < HEADER_SIZE:
            raise ValueError(f"Header too short: {len(data)} < {HEADER_SIZE}")
        
        magic, version, header_size = struct.unpack_from('<IHH', data, 0)
        adc_rate, imu_rate = struct.unpack_from('<II', data, 8)
        start_ts = struct.unpack_from('<Q', data, 16)[0]
        
        # Extract null-terminated string
        loadcell_bytes = data[24:56]
        null_pos = loadcell_bytes.find(b'\x00')
        if null_pos >= 0:
            loadcell_id = loadcell_bytes[:null_pos].decode('utf-8', errors='replace')
        else:
            loadcell_id = loadcell_bytes.decode('utf-8', errors='replace')
        
        flags, adc_gain, adc_bits = struct.unpack_from('<BBB', data, 56)
        imu_accel_scale, imu_gyro_scale = struct.unpack_from('<BB', data, 59)
        
        return cls(
            magic=magic,
            version=version,
            header_size=header_size,
            adc_sample_rate_hz=adc_rate,
            imu_sample_rate_hz=imu_rate,
            start_timestamp_us=start_ts,
            loadcell_id=loadcell_id,
            flags=flags,
            adc_gain=adc_gain,
            adc_bits=adc_bits,
            imu_accel_scale=imu_accel_scale,
            imu_gyro_scale=imu_gyro_scale
        )


@dataclass
class ADCRecord:
    """ADC sample record (12 bytes)"""
    timestamp_offset_us: int
    raw_adc: int
    sequence_num: int
    
    @property
    def timestamp_s(self) -> float:
        """Timestamp in seconds from start"""
        return self.timestamp_offset_us / 1_000_000
    
    def to_microvolts(self, vref_mv: float = 2500, bits: int = 24, gain: int = 1) -> float:
        """Convert raw ADC to microvolts"""
        full_scale = 2 ** (bits - 1)
        return self.raw_adc * (vref_mv * 1000.0 / full_scale) / gain
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'ADCRecord':
        """Parse record from bytes"""
        if len(data) < ADC_RECORD_SIZE:
            raise ValueError(f"ADC record too short: {len(data)} < {ADC_RECORD_SIZE}")
        
        ts, raw, seq = struct.unpack_from('<Iii', data, 0)
        return cls(timestamp_offset_us=ts, raw_adc=raw, sequence_num=seq)


@dataclass  
class IMURecord:
    """IMU sample record (16 bytes)"""
    timestamp_offset_us: int
    accel_x: int
    accel_y: int
    accel_z: int
    gyro_x: int
    gyro_y: int
    gyro_z: int
    
    @property
    def timestamp_s(self) -> float:
        """Timestamp in seconds from start"""
        return self.timestamp_offset_us / 1_000_000
    
    def accel_g(self, scale: int = 0) -> Tuple[float, float, float]:
        """Convert raw accel to g (scale: 0=±2g, 1=±4g, 2=±8g, 3=±16g)"""
        # LSM6DSV16X sensitivity (mg/LSB)
        sensitivity = [0.061, 0.122, 0.244, 0.488][scale]
        factor = sensitivity / 1000.0
        return (self.accel_x * factor, self.accel_y * factor, self.accel_z * factor)
    
    def gyro_dps(self, scale: int = 0) -> Tuple[float, float, float]:
        """Convert raw gyro to degrees/sec"""
        # LSM6DSV16X sensitivity (mdps/LSB)
        sensitivity = [4.375, 8.75, 17.5, 35.0, 70.0][scale]
        factor = sensitivity / 1000.0
        return (self.gyro_x * factor, self.gyro_y * factor, self.gyro_z * factor)
    
    def accel_magnitude_g(self, scale: int = 0) -> float:
        """Calculate acceleration magnitude in g"""
        ax, ay, az = self.accel_g(scale)
        return (ax**2 + ay**2 + az**2) ** 0.5
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'IMURecord':
        """Parse record from bytes"""
        if len(data) < IMU_RECORD_SIZE:
            raise ValueError(f"IMU record too short: {len(data)} < {IMU_RECORD_SIZE}")
        
        ts = struct.unpack_from('<I', data, 0)[0]
        ax, ay, az = struct.unpack_from('<hhh', data, 4)
        gx, gy, gz = struct.unpack_from('<hhh', data, 10)
        return cls(
            timestamp_offset_us=ts,
            accel_x=ax, accel_y=ay, accel_z=az,
            gyro_x=gx, gyro_y=gy, gyro_z=gz
        )


@dataclass
class EventRecord:
    """Event marker record (8+ bytes)"""
    timestamp_offset_us: int
    event_code: int
    data: bytes
    
    @property
    def timestamp_s(self) -> float:
        """Timestamp in seconds from start"""
        return self.timestamp_offset_us / 1_000_000
    
    @property
    def event_name(self) -> str:
        """Human-readable event name"""
        names = {
            0x0001: 'SessionStart',
            0x0002: 'SessionEnd',
            0x0010: 'ButtonPress',
            0x0020: 'Overflow',
            0x0030: 'SyncLost',
            0x0031: 'SyncRestored',
            0x0100: 'CalibrationPoint',
            0x00F0: 'Checkpoint',
            0x00F1: 'FileRotation',
            0x00F2: 'LowBattery',
            0x00F3: 'Saturation',
            0x00F4: 'WriteLatency',
            0x00F5: 'Recovery',
        }
        return names.get(self.event_code, f'Unknown(0x{self.event_code:04X})')


@dataclass
class FileFooter:
    """File footer for integrity verification (32 bytes)"""
    magic: int
    total_adc_samples: int
    total_imu_samples: int
    dropped_samples: int
    end_timestamp_us: int
    crc32: int
    
    @property
    def is_valid(self) -> bool:
        """Check if footer magic is valid"""
        return self.magic == FOOTER_MAGIC
    
    @property
    def duration_s(self) -> float:
        """Duration in seconds"""
        return self.end_timestamp_us / 1_000_000
    
    @classmethod
    def from_bytes(cls, data: bytes) -> Optional['FileFooter']:
        """Parse footer from bytes, returns None if invalid"""
        if len(data) < 32:
            return None
        
        magic = struct.unpack_from('<I', data, 0)[0]
        if magic != FOOTER_MAGIC:
            return None
        
        total_adc = struct.unpack_from('<Q', data, 4)[0]
        total_imu = struct.unpack_from('<Q', data, 12)[0]
        dropped = struct.unpack_from('<I', data, 20)[0]
        end_ts = struct.unpack_from('<I', data, 24)[0]
        crc = struct.unpack_from('<I', data, 28)[0]
        
        return cls(
            magic=magic,
            total_adc_samples=total_adc,
            total_imu_samples=total_imu,
            dropped_samples=dropped,
            end_timestamp_us=end_ts,
            crc32=crc
        )


class LogFile:
    """
    Loadcell datalogger binary file parser.
    
    Supports lazy loading for memory-efficient processing of large files.
    
    Example:
        log = LogFile('data.bin')
        for adc in log.iter_adc():
            print(f"{adc.timestamp_s:.6f}: {adc.raw_adc}")
    """
    
    def __init__(self, filepath: str):
        self.filepath = Path(filepath)
        self._header: Optional[FileHeader] = None
        self._footer: Optional[FileFooter] = None
        self._adc_records: Optional[List[ADCRecord]] = None
        self._imu_records: Optional[List[IMURecord]] = None
        self._events: Optional[List[EventRecord]] = None
        
        # Parse header on init
        with open(self.filepath, 'rb') as f:
            header_data = f.read(HEADER_SIZE)
            self._header = FileHeader.from_bytes(header_data)
            
            # Try to read footer from end
            f.seek(-32, 2)  # Seek 32 bytes from end
            footer_data = f.read(32)
            self._footer = FileFooter.from_bytes(footer_data)
    
    @property
    def header(self) -> FileHeader:
        """File header"""
        return self._header
    
    @property
    def footer(self) -> Optional[FileFooter]:
        """File footer (None if file ended uncleanly)"""
        return self._footer
    
    @property
    def is_valid(self) -> bool:
        """Check if file has valid header"""
        return self._header.is_valid
    
    @property
    def is_complete(self) -> bool:
        """Check if file ended cleanly (has valid footer)"""
        return self._footer is not None and self._footer.is_valid
    
    @property
    def adc_count(self) -> int:
        """Number of ADC samples (from footer or counted)"""
        if self._footer:
            return self._footer.total_adc_samples
        return len(self.adc_records)
    
    @property
    def imu_count(self) -> int:
        """Number of IMU samples (from footer or counted)"""
        if self._footer:
            return self._footer.total_imu_samples
        return len(self.imu_records)
    
    @property
    def duration_seconds(self) -> float:
        """Recording duration in seconds"""
        if self._footer:
            return self._footer.duration_s
        # Estimate from last record
        adc = self.adc_records
        if adc:
            return adc[-1].timestamp_s
        return 0.0
    
    @property
    def dropped_count(self) -> int:
        """Number of dropped samples"""
        if self._footer:
            return self._footer.dropped_samples
        return 0
    
    @property
    def adc_records(self) -> List[ADCRecord]:
        """All ADC records (loads into memory)"""
        if self._adc_records is None:
            self._adc_records = list(self.iter_adc())
        return self._adc_records
    
    @property
    def imu_records(self) -> List[IMURecord]:
        """All IMU records (loads into memory)"""
        if self._imu_records is None:
            self._imu_records = list(self.iter_imu())
        return self._imu_records
    
    def iter_adc(self) -> Iterator[ADCRecord]:
        """Iterate ADC records without loading all into memory"""
        with open(self.filepath, 'rb') as f:
            f.seek(HEADER_SIZE)
            
            # Calculate expected IMU decimation
            imu_decimation = (self._header.adc_sample_rate_hz // 
                            self._header.imu_sample_rate_hz) if self._header.imu_sample_rate_hz > 0 else 0
            
            sample_count = 0
            while True:
                data = f.read(ADC_RECORD_SIZE)
                if len(data) < ADC_RECORD_SIZE:
                    break
                
                # Check for end record (first byte 0xFF)
                if data[0] == 0xFF:
                    break
                
                yield ADCRecord.from_bytes(data)
                sample_count += 1
                
                # Skip IMU record if interleaved
                if imu_decimation > 0 and sample_count % imu_decimation == 0:
                    imu_data = f.read(IMU_RECORD_SIZE)
                    if len(imu_data) < IMU_RECORD_SIZE:
                        break
    
    def iter_imu(self) -> Iterator[IMURecord]:
        """Iterate IMU records without loading all into memory"""
        with open(self.filepath, 'rb') as f:
            f.seek(HEADER_SIZE)
            
            imu_decimation = (self._header.adc_sample_rate_hz // 
                            self._header.imu_sample_rate_hz) if self._header.imu_sample_rate_hz > 0 else 0
            
            if imu_decimation == 0:
                return
            
            sample_count = 0
            while True:
                # Read ADC record
                adc_data = f.read(ADC_RECORD_SIZE)
                if len(adc_data) < ADC_RECORD_SIZE:
                    break
                
                if adc_data[0] == 0xFF:
                    break
                
                sample_count += 1
                
                # Read IMU record at decimation points
                if sample_count % imu_decimation == 0:
                    imu_data = f.read(IMU_RECORD_SIZE)
                    if len(imu_data) < IMU_RECORD_SIZE:
                        break
                    yield IMURecord.from_bytes(imu_data)
    
    def __repr__(self) -> str:
        return (f"LogFile('{self.filepath.name}', "
                f"adc={self.adc_count}, imu={self.imu_count}, "
                f"duration={self.duration_seconds:.2f}s)")

