#!/usr/bin/env python3
"""
Local Development Server for Loadcell Datalogger WebUI

Serves the WebUI files from data/ directory and provides stub API endpoints
for local development and preview without ESP32 hardware.

Usage:
    cd Loadcell_Datalogger_V1
    python tools/dev_server.py

Opens browser at http://localhost:8080
"""

import http.server
import socketserver
import json
import os
import sys
import webbrowser
from urllib.parse import urlparse, parse_qs
from pathlib import Path

PORT = 8080
DATA_DIR = Path(__file__).parent.parent / "data"

# LED Test States (same as ESP32 implementation)
LED_TEST_STATES = [
    {"color": "off", "pattern": "off", "blink_count": 0, "name": "Off"},
    {"color": "red", "pattern": "steady", "blink_count": 0, "name": "Red Solid"},
    {"color": "green", "pattern": "steady", "blink_count": 0, "name": "Green Solid"},
    {"color": "blue", "pattern": "steady", "blink_count": 0, "name": "Blue Solid"},
    {"color": "cyan", "pattern": "steady", "blink_count": 0, "name": "Cyan Solid"},
    {"color": "orange", "pattern": "steady", "blink_count": 0, "name": "Orange Solid"},
    {"color": "magenta", "pattern": "steady", "blink_count": 0, "name": "Magenta Solid"},
    {"color": "red", "pattern": "pulse", "blink_count": 0, "name": "Red Pulse"},
    {"color": "green", "pattern": "pulse", "blink_count": 0, "name": "Green Pulse"},
    {"color": "blue", "pattern": "pulse", "blink_count": 0, "name": "Blue Pulse"},
    {"color": "cyan", "pattern": "pulse", "blink_count": 0, "name": "Cyan Pulse"},
    {"color": "orange", "pattern": "pulse", "blink_count": 0, "name": "Orange Pulse"},
    {"color": "magenta", "pattern": "pulse", "blink_count": 0, "name": "Magenta Pulse"},
    {"color": "red", "pattern": "fast_blink", "blink_count": 0, "name": "Red Fast Blink"},
    {"color": "green", "pattern": "fast_blink", "blink_count": 0, "name": "Green Fast Blink"},
    {"color": "blue", "pattern": "fast_blink", "blink_count": 0, "name": "Blue Fast Blink"},
    {"color": "orange", "pattern": "fast_blink", "blink_count": 0, "name": "Orange Fast Blink"},
    {"color": "red", "pattern": "very_fast_blink", "blink_count": 0, "name": "Red Very Fast (Critical)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 1, "name": "Error Code 1 (SD Missing)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 2, "name": "Error Code 2 (SD Full)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 3, "name": "Error Code 3 (SD Write)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 4, "name": "Error Code 4 (ADC)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 5, "name": "Error Code 5 (IMU)"},
    {"color": "red", "pattern": "blink_code", "blink_count": 6, "name": "Error Code 6 (RTC)"},
]

# Mock data for API responses
MOCK_DATA = {
    "mode": "user",
    "status": {
        "mode": "user",
        "wifi": True,
        "sd_present": True,
        "logging": False,
        "uptime_ms": 123456,
        "free_heap": 245000
    },
    "config": {
        "loadcell_id": "TC023L0-000025",
        "loadcell_model": "TC023L0",
        "loadcell_serial": "000025",
        "capacity_kg": 2000.0,
        "excitation_V": 10.0,
        "adc_pga_gain": 128,
        "imu_g_range": 16,
        "imu_gyro_dps": 2000,
        "calibration_points": [
            {"load_kg": 0, "output_uV": 0},
            {"load_kg": 500, "output_uV": 2500},
            {"load_kg": 1000, "output_uV": 5000},
            {"load_kg": 1500, "output_uV": 7500},
            {"load_kg": 2000, "output_uV": 10000}
        ]
    },
    "sdcard": {
        "present": True,
        "total_mb": 32768,
        "used_mb": 1234,
        "free_mb": 31534,
        "files": [
            {"name": "LOG_20241230_143022.bin", "size_kb": 45678, "date": "2024-12-30"},
            {"name": "LOG_20241229_091500.bin", "size_kb": 32100, "date": "2024-12-29"},
            {"name": "LOG_20241228_161245.bin", "size_kb": 28500, "date": "2024-12-28"}
        ]
    },
    "battery": {
        "voltage_mV": 3850,
        "percent": 75,
        "charging": False
    },
    "test_results": {
        "adc": None,
        "imu": None,
        "rtc": None,
        "sd": None,
        "neopixel": None
    },
    "led": {
        "state_index": 0,
        "cycling": False,
        "color": "off",
        "pattern": "off",
        "blink_count": 0
    }
}

# Passwords for mode switching (same as ESP32)
PASSWORDS = {
    "factory": "factory123",
    "admin": "admin123"
}


class WebUIHandler(http.server.SimpleHTTPRequestHandler):
    """Custom HTTP handler for WebUI development server"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DATA_DIR), **kwargs)
    
    def end_headers(self):
        # Add CORS headers for local development
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()
    
    def do_OPTIONS(self):
        """Handle CORS preflight requests"""
        self.send_response(200)
        self.end_headers()
    
    def do_GET(self):
        """Handle GET requests"""
        parsed = urlparse(self.path)
        path = parsed.path
        
        # API endpoints
        if path.startswith('/api/'):
            self.handle_api_get(path)
            return
        
        # Serve index.html for root
        if path == '/':
            self.path = '/index.html'
        
        # Serve static files
        super().do_GET()
    
    def do_POST(self):
        """Handle POST requests"""
        parsed = urlparse(self.path)
        path = parsed.path
        
        if path.startswith('/api/'):
            self.handle_api_post(path)
            return
        
        self.send_error(404, "Not Found")
    
    def handle_api_get(self, path):
        """Handle API GET requests with mock data"""
        response = None
        
        if path == '/api/mode':
            response = {"mode": MOCK_DATA["mode"]}
        
        elif path == '/api/status':
            response = MOCK_DATA["status"].copy()
            response["demo_mode"] = True  # Indicate running locally
        
        elif path == '/api/config':
            response = MOCK_DATA["config"]
        
        elif path == '/api/sdcard':
            response = MOCK_DATA["sdcard"]
        
        elif path == '/api/battery':
            response = MOCK_DATA["battery"]
        
        elif path == '/api/test/results':
            response = MOCK_DATA["test_results"]
        
        elif path == '/api/live':
            # Simulated live data point
            import random
            response = {
                "timestamp_ms": MOCK_DATA["status"]["uptime_ms"],
                "load_kg": 500 + random.uniform(-10, 10),
                "raw_adc": 5000000 + random.randint(-10000, 10000),
                "accel_x": random.uniform(-0.1, 0.1),
                "accel_y": random.uniform(-0.1, 0.1),
                "accel_z": 1.0 + random.uniform(-0.05, 0.05)
            }
        
        elif path == '/api/led':
            # Get current LED state
            idx = MOCK_DATA["led"]["state_index"]
            state = LED_TEST_STATES[idx] if idx < len(LED_TEST_STATES) else LED_TEST_STATES[0]
            response = {
                "state_index": idx,
                "state_count": len(LED_TEST_STATES),
                "state_name": state["name"],
                "cycling": MOCK_DATA["led"]["cycling"]
            }
        
        else:
            self.send_error(404, f"API endpoint not found: {path}")
            return
        
        self.send_json_response(response)
    
    def handle_api_post(self, path):
        """Handle API POST requests"""
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length > 0 else '{}'
        
        try:
            data = json.loads(body) if body else {}
        except json.JSONDecodeError:
            self.send_error(400, "Invalid JSON")
            return
        
        response = None
        
        if path == '/api/mode':
            # Mode switching with password validation
            new_mode = data.get('mode', '').lower()
            password = data.get('password', '')
            
            if new_mode == 'user':
                MOCK_DATA["mode"] = "user"
                MOCK_DATA["status"]["mode"] = "user"
                response = {"success": True, "mode": "user"}
            
            elif new_mode in ['factory', 'admin']:
                if password == PASSWORDS.get(new_mode):
                    MOCK_DATA["mode"] = new_mode
                    MOCK_DATA["status"]["mode"] = new_mode
                    response = {"success": True, "mode": new_mode}
                else:
                    response = {"success": False, "error": "Invalid password"}
            else:
                response = {"success": False, "error": "Invalid mode"}
        
        elif path == '/api/config':
            # Save configuration (just update mock data)
            for key in data:
                if key in MOCK_DATA["config"]:
                    MOCK_DATA["config"][key] = data[key]
            response = {"success": True}
        
        elif path.startswith('/api/test/'):
            # Sensor tests
            sensor = path.split('/')[-1]
            if sensor in ['adc', 'imu', 'rtc', 'sd', 'neopixel']:
                # Simulate test result (random pass/fail for demo)
                import random
                passed = random.random() > 0.1  # 90% pass rate
                result = {
                    "sensor": sensor,
                    "passed": passed,
                    "message": f"{sensor.upper()} test {'passed' if passed else 'failed'}",
                    "details": {}
                }
                if sensor == 'adc':
                    result["details"] = {"raw_value": 8388608, "noise_uV": 0.5}
                elif sensor == 'imu':
                    result["details"] = {"accel_z": 1.0, "gyro_bias": [0.1, -0.2, 0.05]}
                elif sensor == 'rtc':
                    result["details"] = {"time": "2024-12-31T12:00:00", "valid": True}
                elif sensor == 'sd':
                    result["details"] = {"type": "SDHC", "size_gb": 32}
                elif sensor == 'neopixel':
                    result["details"] = {"colors_tested": 6}
                
                MOCK_DATA["test_results"][sensor] = result
                response = result
            else:
                response = {"success": False, "error": f"Unknown sensor: {sensor}"}
        
        elif path == '/api/logging/start':
            MOCK_DATA["status"]["logging"] = True
            response = {"success": True, "message": "Logging started (demo)"}
        
        elif path == '/api/logging/stop':
            MOCK_DATA["status"]["logging"] = False
            response = {"success": True, "message": "Logging stopped (demo)"}
        
        elif path == '/api/led':
            # Set LED color/pattern
            if MOCK_DATA["mode"] != "factory":
                response = {"success": False, "error": "LED test only available in Factory mode"}
            else:
                color = data.get("color", "off")
                pattern = data.get("pattern", "steady")
                blink_count = data.get("blink_count", 1)
                
                MOCK_DATA["led"]["color"] = color
                MOCK_DATA["led"]["pattern"] = pattern
                MOCK_DATA["led"]["blink_count"] = blink_count
                MOCK_DATA["led"]["cycling"] = False
                
                # Find matching state name
                state_name = f"{color.title()} {pattern.replace('_', ' ').title()}"
                if pattern == "blink_code":
                    state_name = f"Error Code {blink_count}"
                
                response = {
                    "success": True,
                    "color": color,
                    "pattern": pattern,
                    "blink_count": blink_count
                }
        
        elif path == '/api/led/next':
            # Advance to next LED test state
            if MOCK_DATA["mode"] != "factory":
                response = {"success": False, "error": "LED test only available in Factory mode"}
            else:
                idx = MOCK_DATA["led"]["state_index"]
                idx = (idx + 1) % len(LED_TEST_STATES)
                MOCK_DATA["led"]["state_index"] = idx
                
                state = LED_TEST_STATES[idx]
                response = {
                    "success": True,
                    "state_index": idx,
                    "state_count": len(LED_TEST_STATES),
                    "state_name": state["name"]
                }
        
        elif path == '/api/led/cycle/start':
            # Start auto-cycling
            if MOCK_DATA["mode"] != "factory":
                response = {"success": False, "error": "LED test only available in Factory mode"}
            else:
                interval = data.get("interval_ms", 1500)
                MOCK_DATA["led"]["cycling"] = True
                response = {
                    "success": True,
                    "cycling": True,
                    "interval_ms": interval
                }
        
        elif path == '/api/led/cycle/stop':
            # Stop auto-cycling
            MOCK_DATA["led"]["cycling"] = False
            response = {
                "success": True,
                "cycling": False
            }
        
        else:
            self.send_error(404, f"API endpoint not found: {path}")
            return
        
        self.send_json_response(response)
    
    def send_json_response(self, data, status=200):
        """Send JSON response"""
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode('utf-8'))
    
    def log_message(self, format, *args):
        """Custom log format"""
        print(f"[DevServer] {args[0]}")


def main():
    # Check if data directory exists
    if not DATA_DIR.exists():
        print(f"Error: data/ directory not found at {DATA_DIR}")
        print("Please run this script from the project root directory.")
        sys.exit(1)
    
    # Check if index.html exists
    if not (DATA_DIR / "index.html").exists():
        print(f"Warning: index.html not found in {DATA_DIR}")
        print("Creating placeholder...")
        (DATA_DIR / "index.html").write_text("""<!DOCTYPE html>
<html>
<head>
    <title>Loadcell Logger - Development</title>
    <meta charset="UTF-8">
    <style>
        body { font-family: sans-serif; background: #1a1a2e; color: #eee; 
               display: flex; justify-content: center; align-items: center; 
               min-height: 100vh; margin: 0; }
        .container { text-align: center; }
        h1 { color: #00d9ff; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Loadcell Datalogger WebUI</h1>
        <p>Development server is running!</p>
        <p>Create your WebUI files in the <code>data/</code> directory.</p>
    </div>
</body>
</html>
""")
    
    print("=" * 50)
    print("  Loadcell Datalogger - WebUI Development Server")
    print("=" * 50)
    print(f"  Serving files from: {DATA_DIR}")
    print(f"  Server URL: http://localhost:{PORT}")
    print()
    print("  API endpoints available:")
    print("    GET  /api/mode          - Current mode")
    print("    POST /api/mode          - Switch mode")
    print("    GET  /api/status        - System status")
    print("    GET  /api/config        - Configuration")
    print("    POST /api/config        - Save configuration")
    print("    GET  /api/sdcard        - SD card stats")
    print("    GET  /api/battery       - Battery level")
    print("    POST /api/test/{s}      - Run sensor test")
    print("    GET  /api/live          - Live data point")
    print("    GET  /api/led           - LED test state")
    print("    POST /api/led           - Set LED color/pattern")
    print("    POST /api/led/next      - Next LED test state")
    print("    POST /api/led/cycle/*   - Start/stop auto-cycle")
    print()
    print("  Press Ctrl+C to stop")
    print("=" * 50)
    
    # Create server
    with socketserver.TCPServer(("", PORT), WebUIHandler) as httpd:
        # Open browser
        webbrowser.open(f"http://localhost:{PORT}")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[DevServer] Shutting down...")


if __name__ == "__main__":
    main()

