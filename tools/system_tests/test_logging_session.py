"""
Logging Session Tests

End-to-end tests for complete logging sessions:
- Start/stop logging
- File creation
- Binary file validation
- CSV conversion
"""

import pytest
import time
import struct
import sys
import os

# Add parent directory to path for loadcell_parser import
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from loadcell_parser import parser


class TestLoggingBasic:
    """Basic logging start/stop tests."""
    
    def test_start_logging(self, device_api, ensure_not_logging):
        """Should be able to start logging."""
        result = device_api.start_logging()
        assert result.get("success") == True or result.get("status") == "ok"
        
        # Give it time to start
        time.sleep(0.5)
        
        status = device_api.get_status()
        assert status["state"] in ["Logging", "PreLog"]
        
        # Stop logging
        device_api.stop_logging()
    
    def test_stop_logging(self, device_api, ensure_not_logging):
        """Should be able to stop logging."""
        # Start first
        device_api.start_logging()
        time.sleep(0.5)
        
        # Stop
        result = device_api.stop_logging()
        assert result.get("success") == True or result.get("status") == "ok"
        
        time.sleep(0.5)
        status = device_api.get_status()
        assert status["state"] not in ["Logging"]
    
    def test_double_start_handled(self, device_api, ensure_not_logging):
        """Double start should be handled gracefully."""
        device_api.start_logging()
        time.sleep(0.2)
        
        # Second start should either succeed (no-op) or return appropriate error
        result = device_api.start_logging()
        # Should not crash - any response is acceptable
        assert result is not None
        
        device_api.stop_logging()
    
    def test_stop_when_not_logging(self, device_api, ensure_not_logging):
        """Stop when not logging should be handled gracefully."""
        # Ensure stopped
        device_api.stop_logging()
        time.sleep(0.2)
        
        # Stop again
        result = device_api.stop_logging()
        # Should not crash
        assert result is not None


class TestLoggingSession:
    """Complete logging session tests."""
    
    def test_short_session_creates_file(self, device_api, ensure_not_logging):
        """A short logging session should create a file."""
        # Get initial file count
        initial_files = device_api.get_files()["files"]
        initial_count = len(initial_files)
        
        # Run a 5-second session
        device_api.start_logging()
        time.sleep(5)
        device_api.stop_logging()
        time.sleep(1)  # Wait for file to be finalized
        
        # Check new file was created
        final_files = device_api.get_files()["files"]
        assert len(final_files) > initial_count, "No new file created"
    
    def test_session_file_has_data(self, device_api, ensure_not_logging, output_dir):
        """Session file should contain data."""
        # Run a session
        device_api.start_logging()
        time.sleep(3)
        device_api.stop_logging()
        time.sleep(1)
        
        # Get latest file
        files = device_api.get_files()["files"]
        assert len(files) > 0, "No files found"
        
        latest = files[-1]
        assert latest["size"] > 64, "File too small (only header?)"
    
    def test_session_file_valid_header(self, device_api, ensure_not_logging, output_dir):
        """Session file should have valid binary header."""
        # Run a session
        device_api.start_logging()
        time.sleep(3)
        device_api.stop_logging()
        time.sleep(1)
        
        # Download file
        files = device_api.get_files()["files"]
        latest = files[-1]["name"]
        
        response = device_api.download_file(latest)
        assert response.status_code == 200
        
        content = response.content
        
        # Check magic number (first 4 bytes)
        # "LCLG" = 0x4C 0x43 0x4C 0x47 little-endian = 0x474C434C
        magic = struct.unpack("<I", content[:4])[0]
        assert magic == 0x474C434C, f"Invalid magic: {hex(magic)}"
        
        # Check version (bytes 4-5)
        version = struct.unpack("<H", content[4:6])[0]
        assert version == 1, f"Unexpected version: {version}"
        
        # Check header size (bytes 6-7)
        header_size = struct.unpack("<H", content[6:8])[0]
        assert header_size == 64, f"Unexpected header size: {header_size}"
    
    @pytest.mark.slow
    def test_longer_session_no_overflow(self, device_api, ensure_not_logging):
        """A longer session should complete without overflow."""
        # Run a 30-second session
        device_api.start_logging()
        
        # Monitor status during logging
        overflows_detected = False
        for _ in range(30):
            time.sleep(1)
            status = device_api.get_status()
            if status.get("overflowCount", 0) > 0:
                overflows_detected = True
                break
        
        device_api.stop_logging()
        
        assert not overflows_detected, "Overflow detected during logging"


class TestFileDownload:
    """File download tests."""
    
    def test_download_existing_file(self, device_api, ensure_not_logging, output_dir):
        """Should be able to download an existing log file."""
        # Create a file
        device_api.start_logging()
        time.sleep(2)
        device_api.stop_logging()
        time.sleep(1)
        
        # Download it
        files = device_api.get_files()["files"]
        assert len(files) > 0
        
        filename = files[-1]["name"]
        response = device_api.download_file(filename)
        
        assert response.status_code == 200
        assert len(response.content) > 0
    
    def test_download_nonexistent_file(self, device_api):
        """Downloading non-existent file should return 404."""
        response = device_api.download_file("nonexistent_file_xyz.bin")
        assert response.status_code == 404


class TestFileDelete:
    """File deletion tests."""
    
    def test_delete_file(self, device_api, ensure_not_logging):
        """Should be able to delete a log file."""
        # Create a file
        device_api.start_logging()
        time.sleep(2)
        device_api.stop_logging()
        time.sleep(1)
        
        # Get file list
        files = device_api.get_files()["files"]
        assert len(files) > 0
        
        filename = files[-1]["name"]
        
        # Delete it
        result = device_api.delete_file(filename)
        assert result.get("success") == True or result.get("status") == "ok"
        
        # Verify it's gone
        files_after = device_api.get_files()["files"]
        filenames = [f["name"] for f in files_after]
        assert filename not in filenames


class TestBinaryValidation:
    """Binary file format validation tests."""
    
    def test_parse_with_loadcell_parser(self, device_api, ensure_not_logging, output_dir):
        """Log file should be parseable with loadcell_parser."""
        # Create a file
        device_api.start_logging()
        time.sleep(5)
        device_api.stop_logging()
        time.sleep(1)
        
        # Download it
        files = device_api.get_files()["files"]
        filename = files[-1]["name"]
        
        response = device_api.download_file(filename)
        
        # Save to temp file
        local_path = output_dir / filename
        with open(local_path, "wb") as f:
            f.write(response.content)
        
        # Parse it
        try:
            log = parser.LogFile(str(local_path))
            
            # Should have valid header
            assert log.header is not None
            assert log.header.magic == 0x474C434C
            
            # Should have some ADC records
            adc_count = 0
            for record in log.records():
                if isinstance(record, parser.ADCRecord):
                    adc_count += 1
                if adc_count > 10:
                    break  # Don't need to count all
            
            assert adc_count > 0, "No ADC records found"
            
        except Exception as e:
            pytest.fail(f"Failed to parse log file: {e}")
    
    def test_records_have_increasing_timestamps(self, device_api, ensure_not_logging, output_dir):
        """ADC records should have monotonically increasing timestamps."""
        # Create and download file
        device_api.start_logging()
        time.sleep(3)
        device_api.stop_logging()
        time.sleep(1)
        
        files = device_api.get_files()["files"]
        filename = files[-1]["name"]
        response = device_api.download_file(filename)
        
        local_path = output_dir / filename
        with open(local_path, "wb") as f:
            f.write(response.content)
        
        # Parse and check timestamps
        log = parser.LogFile(str(local_path))
        
        last_timestamp = 0
        for record in log.records():
            if isinstance(record, parser.ADCRecord):
                assert record.timestamp_offset_us >= last_timestamp, \
                    f"Timestamp went backwards: {record.timestamp_offset_us} < {last_timestamp}"
                last_timestamp = record.timestamp_offset_us




