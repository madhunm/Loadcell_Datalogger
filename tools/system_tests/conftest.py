"""
pytest configuration and fixtures for system tests.

Configure device connection settings via environment variables or command line:
    DEVICE_IP=192.168.4.1 pytest
    pytest --device-ip=192.168.4.1 --serial-port=COM3
"""

import os
import pytest
import requests
import serial
import time

# Default configuration
DEFAULT_DEVICE_IP = "192.168.4.1"
DEFAULT_SERIAL_PORT = None  # Auto-detect or skip serial tests
DEFAULT_TIMEOUT = 10


def pytest_addoption(parser):
    """Add custom command line options."""
    parser.addoption(
        "--device-ip",
        action="store",
        default=os.environ.get("DEVICE_IP", DEFAULT_DEVICE_IP),
        help="IP address of the device (default: 192.168.4.1)"
    )
    parser.addoption(
        "--serial-port",
        action="store",
        default=os.environ.get("SERIAL_PORT", DEFAULT_SERIAL_PORT),
        help="Serial port for device (e.g., COM3, /dev/ttyUSB0)"
    )
    parser.addoption(
        "--timeout",
        action="store",
        default=int(os.environ.get("DEVICE_TIMEOUT", DEFAULT_TIMEOUT)),
        type=int,
        help="HTTP request timeout in seconds"
    )


@pytest.fixture(scope="session")
def device_ip(request):
    """Get device IP address."""
    return request.config.getoption("--device-ip")


@pytest.fixture(scope="session")
def serial_port(request):
    """Get serial port."""
    return request.config.getoption("--serial-port")


@pytest.fixture(scope="session")
def timeout(request):
    """Get request timeout."""
    return request.config.getoption("--timeout")


@pytest.fixture(scope="session")
def base_url(device_ip):
    """Get base URL for API requests."""
    return f"http://{device_ip}"


@pytest.fixture(scope="session")
def api_session(timeout):
    """Create a requests session with configured timeout."""
    session = requests.Session()
    session.timeout = timeout
    return session


@pytest.fixture(scope="module")
def device_connected(base_url, api_session):
    """
    Check if device is reachable before running tests.
    Skips tests if device is not available.
    """
    try:
        response = api_session.get(f"{base_url}/api/status", timeout=5)
        return response.status_code == 200
    except requests.exceptions.RequestException:
        pytest.skip("Device not reachable - skipping integration tests")
        return False


@pytest.fixture(scope="function")
def ensure_not_logging(base_url, api_session, device_connected):
    """Ensure logging is stopped before and after test."""
    # Stop any active logging before test
    try:
        api_session.post(f"{base_url}/api/logging/stop", timeout=5)
        time.sleep(0.5)
    except:
        pass
    
    yield
    
    # Stop logging after test
    try:
        api_session.post(f"{base_url}/api/logging/stop", timeout=5)
    except:
        pass


@pytest.fixture(scope="session")
def serial_connection(serial_port):
    """
    Create serial connection to device.
    Returns None if serial port not configured.
    """
    if not serial_port:
        return None
    
    try:
        conn = serial.Serial(serial_port, 115200, timeout=2)
        time.sleep(0.5)  # Wait for connection
        conn.reset_input_buffer()
        return conn
    except serial.SerialException as e:
        pytest.skip(f"Serial port not available: {e}")
        return None


@pytest.fixture(scope="session")
def output_dir(tmp_path_factory):
    """Create output directory for test artifacts."""
    return tmp_path_factory.mktemp("test_output")


# Helper class for API interactions
class DeviceAPI:
    """Helper class for device API interactions."""
    
    def __init__(self, base_url, session):
        self.base_url = base_url
        self.session = session
    
    def get(self, endpoint, **kwargs):
        """Make GET request to device."""
        url = f"{self.base_url}{endpoint}"
        return self.session.get(url, **kwargs)
    
    def post(self, endpoint, **kwargs):
        """Make POST request to device."""
        url = f"{self.base_url}{endpoint}"
        return self.session.post(url, **kwargs)
    
    def get_status(self):
        """Get device status."""
        return self.get("/api/status").json()
    
    def start_logging(self):
        """Start data logging."""
        return self.post("/api/logging/start").json()
    
    def stop_logging(self):
        """Stop data logging."""
        return self.post("/api/logging/stop").json()
    
    def get_files(self):
        """Get list of log files."""
        return self.get("/api/files").json()
    
    def download_file(self, filename):
        """Download a file from the device."""
        return self.get(f"/api/download?file={filename}")
    
    def delete_file(self, filename):
        """Delete a file from the device."""
        return self.post(f"/api/delete?file={filename}")
    
    def get_live_data(self):
        """Get live sensor data."""
        return self.get("/api/live").json()
    
    def get_battery(self):
        """Get battery status."""
        return self.get("/api/battery").json()


@pytest.fixture(scope="module")
def device_api(base_url, api_session, device_connected):
    """Create DeviceAPI helper instance."""
    return DeviceAPI(base_url, api_session)




