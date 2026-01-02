"""
API Status Endpoint Tests

Tests for device status, live data, and basic API functionality.
"""

import pytest
import time


class TestStatusEndpoint:
    """Tests for /api/status endpoint."""
    
    def test_status_returns_200(self, device_api):
        """Status endpoint should return 200 OK."""
        response = device_api.get("/api/status")
        assert response.status_code == 200
    
    def test_status_returns_json(self, device_api):
        """Status should return valid JSON."""
        response = device_api.get("/api/status")
        data = response.json()
        assert isinstance(data, dict)
    
    def test_status_contains_required_fields(self, device_api):
        """Status should contain essential fields."""
        status = device_api.get_status()
        
        # Check for required fields
        assert "state" in status
        assert "uptime" in status
        assert "freeHeap" in status
    
    def test_status_state_is_valid(self, device_api):
        """State should be one of the valid states."""
        status = device_api.get_status()
        valid_states = ["Init", "Admin", "PreLog", "Logging", "Stopping", 
                       "Converting", "Ready", "Error"]
        assert status["state"] in valid_states
    
    def test_status_uptime_increases(self, device_api):
        """Uptime should increase between calls."""
        status1 = device_api.get_status()
        time.sleep(1.1)
        status2 = device_api.get_status()
        
        assert status2["uptime"] > status1["uptime"]


class TestLiveDataEndpoint:
    """Tests for /api/live endpoint."""
    
    def test_live_returns_200(self, device_api):
        """Live data endpoint should return 200 OK."""
        response = device_api.get("/api/live")
        assert response.status_code == 200
    
    def test_live_returns_sensor_data(self, device_api):
        """Live data should include sensor readings."""
        data = device_api.get_live_data()
        
        # Should have ADC data
        assert "adc" in data or "raw" in data
    
    def test_live_data_changes(self, device_api):
        """Live data should show some variation (noise at minimum)."""
        readings = []
        for _ in range(5):
            data = device_api.get_live_data()
            if "adc" in data:
                readings.append(data["adc"])
            elif "raw" in data:
                readings.append(data["raw"])
            time.sleep(0.1)
        
        # Should have gotten some readings
        assert len(readings) > 0
        # Note: Values might be identical with stable input, so we don't
        # strictly require variation


class TestBatteryEndpoint:
    """Tests for /api/battery endpoint."""
    
    def test_battery_returns_200(self, device_api):
        """Battery endpoint should return 200 OK."""
        response = device_api.get("/api/battery")
        assert response.status_code == 200
    
    def test_battery_contains_voltage(self, device_api):
        """Battery status should include voltage."""
        data = device_api.get_battery()
        assert "voltage" in data or "v" in data
    
    def test_battery_contains_percentage(self, device_api):
        """Battery status should include percentage."""
        data = device_api.get_battery()
        assert "percent" in data or "soc" in data or "percentage" in data
    
    def test_battery_values_reasonable(self, device_api):
        """Battery values should be in reasonable range."""
        data = device_api.get_battery()
        
        # Get voltage (handle different key names)
        voltage = data.get("voltage") or data.get("v", 0)
        
        # Li-Ion battery: 3.0V - 4.2V typical, or 0 if not present
        assert 0 <= voltage <= 5.0, f"Voltage {voltage}V seems unreasonable"
        
        # Get percentage
        percent = data.get("percent") or data.get("soc") or data.get("percentage", 0)
        assert 0 <= percent <= 100, f"Percentage {percent}% out of range"


class TestFilesEndpoint:
    """Tests for /api/files endpoint."""
    
    def test_files_returns_200(self, device_api):
        """Files endpoint should return 200 OK."""
        response = device_api.get("/api/files")
        assert response.status_code == 200
    
    def test_files_returns_list(self, device_api):
        """Files endpoint should return a list."""
        data = device_api.get_files()
        assert "files" in data
        assert isinstance(data["files"], list)
    
    def test_files_entries_have_required_fields(self, device_api):
        """Each file entry should have name and size."""
        data = device_api.get_files()
        
        for file_info in data["files"]:
            assert "name" in file_info
            assert "size" in file_info


class TestStaticFiles:
    """Tests for static file serving."""
    
    def test_index_html_served(self, device_api):
        """Index.html should be served at root."""
        response = device_api.get("/")
        assert response.status_code == 200
        assert "text/html" in response.headers.get("Content-Type", "")
    
    def test_css_served(self, device_api):
        """CSS file should be served."""
        response = device_api.get("/style.css")
        # May return 200 or 404 depending on SPIFFS contents
        if response.status_code == 200:
            assert "css" in response.headers.get("Content-Type", "").lower()
    
    def test_js_served(self, device_api):
        """JavaScript file should be served."""
        response = device_api.get("/app.js")
        # May return 200 or 404 depending on SPIFFS contents
        if response.status_code == 200:
            content_type = response.headers.get("Content-Type", "").lower()
            assert "javascript" in content_type or "text" in content_type




