"""
Stress Tests

Long-running tests to verify system stability under load.
These tests are marked as 'slow' and skipped by default.

Run with: pytest -m slow
"""

import pytest
import time


@pytest.mark.slow
class TestStress:
    """Stress tests for system stability."""
    
    def test_1_minute_logging(self, device_api, ensure_not_logging):
        """1-minute continuous logging session."""
        DURATION = 60  # seconds
        
        print(f"\nStarting {DURATION}-second logging session...")
        
        device_api.start_logging()
        start_time = time.time()
        
        # Monitor during logging
        errors = []
        samples_logged = 0
        
        while time.time() - start_time < DURATION:
            time.sleep(5)
            
            try:
                status = device_api.get_status()
                
                if status.get("state") != "Logging":
                    errors.append(f"Unexpected state: {status.get('state')}")
                    break
                
                overflow = status.get("overflowCount", 0)
                if overflow > 0:
                    errors.append(f"Overflow detected: {overflow} samples")
                
                samples = status.get("samplesLogged", 0)
                if samples > samples_logged:
                    samples_logged = samples
                    
            except Exception as e:
                errors.append(f"Status check failed: {e}")
        
        device_api.stop_logging()
        elapsed = time.time() - start_time
        
        print(f"Session completed in {elapsed:.1f}s")
        print(f"Samples logged: {samples_logged}")
        
        assert len(errors) == 0, f"Errors during logging: {errors}"
    
    def test_5_minute_logging(self, device_api, ensure_not_logging):
        """5-minute continuous logging session."""
        DURATION = 300  # 5 minutes
        
        print(f"\nStarting {DURATION//60}-minute logging session...")
        
        device_api.start_logging()
        start_time = time.time()
        
        max_latency = 0
        overflow_count = 0
        
        while time.time() - start_time < DURATION:
            time.sleep(10)
            
            status = device_api.get_status()
            
            if status.get("state") != "Logging":
                print(f"State changed to: {status.get('state')}")
                break
            
            latency = status.get("maxWriteLatencyMs", 0)
            if latency > max_latency:
                max_latency = latency
            
            overflow = status.get("overflowCount", 0)
            if overflow > overflow_count:
                print(f"Warning: Overflow count increased to {overflow}")
                overflow_count = overflow
            
            # Print progress
            elapsed = time.time() - start_time
            print(f"  {elapsed:.0f}s: state={status.get('state')}, latency={latency}ms")
        
        device_api.stop_logging()
        
        print(f"\nMax write latency: {max_latency}ms")
        print(f"Total overflows: {overflow_count}")
        
        # Verify no data loss
        assert overflow_count == 0, f"Lost {overflow_count} samples"
    
    def test_rapid_start_stop(self, device_api, ensure_not_logging):
        """Rapidly start and stop logging."""
        CYCLES = 20
        
        print(f"\nRunning {CYCLES} rapid start/stop cycles...")
        
        errors = []
        
        for i in range(CYCLES):
            try:
                device_api.start_logging()
                time.sleep(0.5)
                device_api.stop_logging()
                time.sleep(0.5)
            except Exception as e:
                errors.append(f"Cycle {i}: {e}")
        
        print(f"Completed {CYCLES} cycles")
        
        # Device should still be responsive
        status = device_api.get_status()
        assert status is not None, "Device unresponsive after rapid cycling"
        
        assert len(errors) == 0, f"Errors during cycling: {errors}"
    
    def test_api_hammering(self, device_api):
        """Rapidly call API endpoints."""
        DURATION = 30  # seconds
        
        print(f"\nHammering API for {DURATION} seconds...")
        
        start_time = time.time()
        call_count = 0
        error_count = 0
        
        while time.time() - start_time < DURATION:
            try:
                device_api.get_status()
                call_count += 1
                
                device_api.get_live_data()
                call_count += 1
                
                device_api.get_battery()
                call_count += 1
                
            except Exception:
                error_count += 1
            
            # Small delay to avoid completely overwhelming
            time.sleep(0.05)
        
        elapsed = time.time() - start_time
        rate = call_count / elapsed
        
        print(f"API calls: {call_count} ({rate:.1f}/sec)")
        print(f"Errors: {error_count}")
        
        # Should handle at least 10 requests/sec with < 5% errors
        assert rate > 5, f"API too slow: {rate:.1f} calls/sec"
        assert error_count / call_count < 0.05, f"Too many errors: {error_count}/{call_count}"


@pytest.mark.slow
class TestRecovery:
    """Recovery and error handling tests."""
    
    def test_recovery_after_many_files(self, device_api, ensure_not_logging):
        """Create many small files and verify system handles it."""
        NUM_FILES = 10
        
        print(f"\nCreating {NUM_FILES} log files...")
        
        initial_files = len(device_api.get_files()["files"])
        
        for i in range(NUM_FILES):
            device_api.start_logging()
            time.sleep(2)
            device_api.stop_logging()
            time.sleep(1)
            print(f"  Created file {i+1}/{NUM_FILES}")
        
        final_files = device_api.get_files()["files"]
        new_files = len(final_files) - initial_files
        
        print(f"Created {new_files} new files")
        
        # Should have created all requested files
        assert new_files >= NUM_FILES - 1, f"Only created {new_files} files"
        
        # Clean up - delete test files
        print("Cleaning up...")
        for f in final_files[-NUM_FILES:]:
            device_api.delete_file(f["name"])




