#include "webconfig.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "logger.h"
#include "adc.h"
#include "imu.h"
#include "sdcard.h"
#include "FS.h"
#include "loadcell_cal.h"

// Web server instance
static WebServer server(80);

// Current logger configuration (can be modified via web interface)
static LoggerConfig currentConfig = {
    .adcSampleRate = 64000,
    .adcPgaGain = ADC_PGA_GAIN_4,
    .imuAccelRange = 16,
    .imuGyroRange = 2000,
    .imuOdr = 960
};

static bool webConfigActive = false;
static String cachedSSID = "";

// Calibration values (loadcell scale and offset)
static float loadcellScale = LOADCELL_SCALING_FACTOR_N_PER_ADC;
static uint32_t loadcellOffset = LOADCELL_ADC_BASELINE;

// Get or generate SSID (stored in NVS, generated once per device)
static String getOrGenerateSSID()
{
    // Return cached SSID if already retrieved
    if (cachedSSID.length() > 0)
    {
        return cachedSSID;
    }
    
    Preferences preferences;
    preferences.begin("webconfig", true); // Read-only mode first
    
    // Try to read existing SSID from NVS
    String ssid = preferences.getString("ssid", "");
    
    if (ssid.length() == 0)
    {
        // No SSID stored - generate new one
        preferences.end(); // Close read-only
        preferences.begin("webconfig", false); // Open read-write
        
        // Generate random number (using ESP32 hardware RNG)
        uint32_t randomNum = (uint32_t)random(1000, 9999);
        ssid = "Loadcell_Datalogger_" + String(randomNum);
        
        // Store in NVS
        preferences.putString("ssid", ssid);
        Serial.print("[WEBCONFIG] Generated new SSID: ");
        Serial.println(ssid);
    }
    else
    {
        Serial.print("[WEBCONFIG] Using stored SSID: ");
        Serial.println(ssid);
    }
    
    preferences.end();
    
    // Cache for future use
    cachedSSID = ssid;
    return ssid;
}

// HTML page for configuration
// Using custom delimiter to avoid issues with parentheses in JavaScript
static const char* htmlPage = R"HTML_PAGE(
<!DOCTYPE html>
<html>
<head>
    <title>Loadcell Datalogger Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { box-sizing: border-box; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            margin: 0; 
            padding: 20px; 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
        }
        .container { 
            max-width: 1000px; 
            margin: 0 auto; 
            background: white; 
            padding: 30px; 
            border-radius: 12px; 
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
        }
        h1 { 
            color: #2d3748; 
            border-bottom: 3px solid #667eea; 
            padding-bottom: 15px; 
            margin-top: 0;
            font-size: 28px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .section { 
            margin: 25px 0; 
            padding: 20px; 
            background: #f7fafc; 
            border-radius: 8px; 
            border-left: 4px solid #667eea;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .section:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(0,0,0,0.1);
        }
        .section h2 { 
            margin-top: 0; 
            color: #2d3748; 
            font-size: 20px;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label { 
            display: block; 
            margin: 0 0 8px 0; 
            font-weight: 600; 
            color: #4a5568; 
            font-size: 14px;
        }
        input, select { 
            width: 100%; 
            padding: 12px; 
            margin-bottom: 0; 
            border: 2px solid #e2e8f0; 
            border-radius: 6px; 
            font-size: 14px;
            transition: border-color 0.2s, box-shadow 0.2s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        button { 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white; 
            padding: 12px 24px; 
            border: none; 
            border-radius: 6px; 
            cursor: pointer; 
            font-size: 15px; 
            font-weight: 600;
            margin: 5px 5px 0 0; 
            transition: transform 0.2s, box-shadow 0.2s;
            box-shadow: 0 2px 8px rgba(102, 126, 234, 0.3);
        }
        button:hover { 
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
        }
        button:active {
            transform: translateY(0);
        }
        button.secondary {
            background: #e2e8f0;
            color: #4a5568;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        button.secondary:hover {
            background: #cbd5e0;
            box-shadow: 0 4px 8px rgba(0,0,0,0.15);
        }
        .status { 
            padding: 12px 16px; 
            margin: 15px 0; 
            border-radius: 6px; 
            font-weight: 500;
            animation: slideIn 0.3s ease-out;
        }
        @keyframes slideIn {
            from { opacity: 0; transform: translateY(-10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        .success { background: #c6f6d5; color: #22543d; border-left: 4px solid #48bb78; }
        .error { background: #fed7d7; color: #742a2a; border-left: 4px solid #f56565; }
        .info { background: #bee3f8; color: #2c5282; border-left: 4px solid #4299e1; }
        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        .status-card {
            background: white;
            padding: 15px;
            border-radius: 8px;
            border: 2px solid #e2e8f0;
            transition: all 0.2s;
        }
        .status-card:hover {
            border-color: #667eea;
            box-shadow: 0 4px 12px rgba(0,0,0,0.1);
        }
        .status-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        .status-label { 
            font-weight: 600; 
            color: #4a5568; 
            font-size: 13px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .status-value { 
            color: #2d3748; 
            font-size: 16px;
            font-weight: 600;
        }
        .status-value.status-ok { color: #48bb78; }
        .status-value.status-warning { color: #ed8936; }
        .status-value.status-error { color: #f56565; }
        .progress-bar {
            width: 100%;
            height: 8px;
            background: #e2e8f0;
            border-radius: 4px;
            overflow: hidden;
            margin-top: 8px;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #48bb78, #38a169);
            border-radius: 4px;
            transition: width 0.3s ease;
        }
        .progress-fill.warning {
            background: linear-gradient(90deg, #ed8936, #dd6b20);
        }
        .progress-fill.error {
            background: linear-gradient(90deg, #f56565, #e53e3e);
        }
        .icon {
            display: inline-block;
            width: 20px;
            height: 20px;
            vertical-align: middle;
        }
        .refresh-indicator {
            display: inline-block;
            width: 16px;
            height: 16px;
            border: 2px solid #667eea;
            border-top-color: transparent;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin-left: 8px;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        .button-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 15px;
        }
        @media (max-width: 768px) {
            .container { padding: 20px; }
            .status-grid { grid-template-columns: 1fr; }
            h1 { font-size: 24px; }
        }
        .badge {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 12px;
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .badge-ok { background: #c6f6d5; color: #22543d; }
        .badge-warning { background: #feebc8; color: #7c2d12; }
        .badge-error { background: #fed7d7; color: #742a2a; }
        .chart-container {
            position: relative;
            height: 400px;
            margin-top: 15px;
        }
        .chart-controls {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-bottom: 15px;
            align-items: center;
        }
        .chart-controls label {
            margin: 0;
            font-weight: 500;
        }
        .chart-controls input[type="number"],
        .chart-controls select {
            width: auto;
            min-width: 100px;
            margin: 0;
        }
        .file-input-wrapper {
            position: relative;
            display: inline-block;
        }
        .file-input-wrapper input[type="file"] {
            position: absolute;
            opacity: 0;
            width: 0;
            height: 0;
        }
        .file-input-label {
            display: inline-block;
            padding: 10px 20px;
            background: #4299e1;
            color: white;
            border-radius: 6px;
            cursor: pointer;
            font-weight: 600;
            transition: background 0.2s;
        }
        .file-input-label:hover {
            background: #3182ce;
        }
        .chart-legend {
            display: flex;
            flex-wrap: wrap;
            gap: 20px;
            margin-top: 10px;
            font-size: 12px;
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 8px;
            cursor: pointer;
        }
        .legend-color {
            width: 16px;
            height: 3px;
            border-radius: 2px;
        }
        .csv-file-list {
            margin-top: 15px;
            max-height: 200px;
            overflow-y: auto;
            border: 2px solid #e2e8f0;
            border-radius: 6px;
            padding: 10px;
            background: white;
        }
        .csv-file-item {
            padding: 8px;
            margin: 5px 0;
            border-radius: 4px;
            cursor: pointer;
            transition: background 0.2s;
        }
        .csv-file-item:hover {
            background: #f7fafc;
        }
        .csv-file-item.selected {
            background: #e6f2ff;
            border-left: 3px solid #4299e1;
        }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
</head>
<body>
    <div class="container">
        <h1>
            <span>⚙️</span>
            Loadcell Datalogger Configuration
        </h1>
        
        <div class="section">
            <h2>📊 ADC Settings</h2>
            <form id="adcForm">
                <div class="form-group">
                    <label for="adcSampleRate">ADC Sample Rate (Hz)</label>
                    <input type="number" id="adcSampleRate" name="adcSampleRate" value="64000" min="1000" max="64000" step="1000">
                    <small style="color: #718096; font-size: 12px;">Range: 1,000 - 64,000 Hz</small>
                </div>
                
                <div class="form-group">
                    <label for="adcPgaGain">PGA Gain</label>
                    <select id="adcPgaGain" name="adcPgaGain">
                        <option value="0">x1</option>
                        <option value="1">x2</option>
                        <option value="2" selected>x4</option>
                        <option value="3">x8</option>
                        <option value="4">x16</option>
                        <option value="5">x32</option>
                        <option value="6">x64</option>
                        <option value="7">x128</option>
                    </select>
                </div>
            </form>
        </div>

        <div class="section">
            <h2>🔄 IMU Settings</h2>
            <form id="imuForm">
                <div class="form-group">
                    <label for="imuOdr">IMU Sample Rate (Hz)</label>
                    <input type="number" id="imuOdr" name="imuOdr" value="960" min="15" max="960" step="15">
                    <small style="color: #718096; font-size: 12px;">Range: 15 - 960 Hz</small>
                </div>
                
                <div class="form-group">
                    <label for="imuAccelRange">Accelerometer Range</label>
                    <select id="imuAccelRange" name="imuAccelRange">
                        <option value="2">±2g</option>
                        <option value="4">±4g</option>
                        <option value="8">±8g</option>
                        <option value="16" selected>±16g</option>
                    </select>
                </div>
                
                <div class="form-group">
                    <label for="imuGyroRange">Gyroscope Range</label>
                    <select id="imuGyroRange" name="imuGyroRange">
                        <option value="125">±125 dps</option>
                        <option value="250">±250 dps</option>
                        <option value="500">±500 dps</option>
                        <option value="1000">±1000 dps</option>
                        <option value="2000" selected>±2000 dps</option>
                    </select>
                </div>
            </form>
        </div>

        <div class="section">
            <div class="button-group">
                <button onclick="saveConfig()">💾 Save Configuration</button>
                <button class="secondary" onclick="loadConfig()">📥 Load Current Config</button>
            </div>
        </div>

        <div class="section">
            <h2>
                📈 System Status
                <span id="refreshIndicator" class="refresh-indicator" style="display: none;"></span>
            </h2>
            <div class="status-grid" id="statusIndicators">
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">💾 SD Card</span>
                        <span id="sdStatus" class="status-value">Checking...</span>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">💿 Free Space</span>
                        <span id="sdSpace" class="status-value">-</span>
                    </div>
                    <div class="progress-bar">
                        <div id="sdSpaceBar" class="progress-fill" style="width: 0%;"></div>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">📊 ADC Buffer</span>
                        <span id="adcBuffer" class="status-value">-</span>
                    </div>
                    <div class="progress-bar">
                        <div id="adcBufferBar" class="progress-fill" style="width: 0%;"></div>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">🔄 IMU Buffer</span>
                        <span id="imuBuffer" class="status-value">-</span>
                    </div>
                    <div class="progress-bar">
                        <div id="imuBufferBar" class="progress-fill" style="width: 0%;"></div>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">⚠️ Write Failures</span>
                        <span id="writeFailures" class="status-value">-</span>
                    </div>
                </div>
                <div class="status-card">
                    <div class="status-header">
                        <span class="status-label">🔌 Logger State</span>
                        <span id="loggerState" class="status-value">-</span>
                    </div>
                </div>
            </div>
        </div>

        <div class="section">
            <h2 id="chartTitle">📊 Data Visualization</h2>
            <div class="chart-controls">
                <label>Moving Average Window:</label>
                <select id="avgWindow" onchange="updateChart()">
                    <option value="1" selected>No Filtering</option>
                    <option value="10">10 samples</option>
                    <option value="50">50 samples</option>
                    <option value="100">100 samples</option>
                    <option value="500">500 samples</option>
                    <option value="1000">1000 samples</option>
                </select>
                <label>Max Points:</label>
                <input type="number" id="maxPoints" value="5000" min="100" max="50000" step="100" onchange="updateChart()">
                <button class="secondary" onclick="reloadLatestCsv()">🔄 Reload Latest</button>
            </div>
            <div class="chart-container">
                <canvas id="dataChart"></canvas>
            </div>
            <div class="chart-legend" id="chartLegend"></div>
            <div class="chart-stats" id="chartStats" style="margin-top: 15px; padding: 15px; background: #f7fafc; border-radius: 8px; font-size: 14px; line-height: 1.8;">
                <div><strong>Max Force:</strong> <span id="maxForce">-</span></div>
                <div><strong>Max Deceleration:</strong> <span id="maxDecel">-</span></div>
            </div>
        </div>

        <div id="status"></div>

    </div>

    <script>
        function showStatus(message, type) {
            const statusDiv = document.getElementById('status');
            statusDiv.className = 'status ' + type;
            statusDiv.textContent = message;
            setTimeout(() => statusDiv.textContent = '', 4000);
        }

        async function saveConfig() {
            // Validate inputs
            const adcRate = parseInt(document.getElementById('adcSampleRate').value);
            const adcGain = parseInt(document.getElementById('adcPgaGain').value);
            const imuOdr = parseInt(document.getElementById('imuOdr').value);
            const imuAccel = parseInt(document.getElementById('imuAccelRange').value);
            const imuGyro = parseInt(document.getElementById('imuGyroRange').value);

            if (adcRate < 1000 || adcRate > 64000) {
                showStatus('ADC Sample Rate must be between 1,000 and 64,000 Hz', 'error');
                return;
            }
            if (imuOdr < 15 || imuOdr > 960) {
                showStatus('IMU Sample Rate must be between 15 and 960 Hz', 'error');
                return;
            }

            const params = new URLSearchParams({
                adcSampleRate: adcRate,
                adcPgaGain: adcGain,
                imuOdr: imuOdr,
                imuAccelRange: imuAccel,
                imuGyroRange: imuGyro
            });

            try {
                showStatus('Saving configuration...', 'info');
                const response = await fetch('/config?' + params.toString(), { method: 'POST' });
                const text = await response.text();
                if (response.ok) {
                    showStatus('✅ Configuration saved successfully!', 'success');
                } else {
                    showStatus('❌ Error saving configuration: ' + text, 'error');
                }
            } catch (error) {
                showStatus('❌ Error saving configuration: ' + error, 'error');
            }
        }

        async function loadConfig() {
            try {
                const response = await fetch('/config');
                const config = await response.json();
                
                document.getElementById('adcSampleRate').value = config.adcSampleRate || 64000;
                document.getElementById('adcPgaGain').value = config.adcPgaGain || 2;
                document.getElementById('imuOdr').value = config.imuOdr || 960;
                document.getElementById('imuAccelRange').value = config.imuAccelRange || 16;
                document.getElementById('imuGyroRange').value = config.imuGyroRange || 2000;
                
                showStatus('✅ Configuration loaded successfully!', 'success');
            } catch (error) {
                showStatus('Error loading configuration: ' + error, 'error');
            }
        }

        // Update system status indicators
        async function updateStatusIndicators() {
            const refreshIndicator = document.getElementById('refreshIndicator');
            refreshIndicator.style.display = 'inline-block';
            
            try {
                const response = await fetch('/status');
                const data = await response.json();
                
                // SD Card status
                const sdStatusEl = document.getElementById('sdStatus');
                if (data.sd && data.sd.mounted && data.sd.present) {
                    sdStatusEl.textContent = '✓ OK';
                    sdStatusEl.className = 'status-value status-ok';
                } else {
                    sdStatusEl.textContent = '✗ ERROR';
                    sdStatusEl.className = 'status-value status-error';
                }
                
                // SD Card free space
                const sdSpaceEl = document.getElementById('sdSpace');
                const sdSpaceBar = document.getElementById('sdSpaceBar');
                if (data.sd && data.sd.totalSpace > 0) {
                    const freeMB = (data.sd.freeSpace / (1024 * 1024)).toFixed(1);
                    const totalMB = (data.sd.totalSpace / (1024 * 1024)).toFixed(1);
                    const percent = data.sd.freePercent;
                    sdSpaceEl.textContent = freeMB + ' MB / ' + totalMB + ' MB';
                    sdSpaceBar.style.width = percent + '%';
                    if (percent < 10) {
                        sdSpaceEl.className = 'status-value status-warning';
                        sdSpaceBar.className = 'progress-fill error';
                    } else if (percent < 25) {
                        sdSpaceEl.className = 'status-value status-warning';
                        sdSpaceBar.className = 'progress-fill warning';
                    } else {
                        sdSpaceEl.className = 'status-value status-ok';
                        sdSpaceBar.className = 'progress-fill';
                    }
                } else {
                    sdSpaceEl.textContent = 'N/A';
                    sdSpaceEl.className = 'status-value';
                    sdSpaceBar.style.width = '0%';
                }
                
                // ADC Buffer
                const adcBufferEl = document.getElementById('adcBuffer');
                const adcBufferBar = document.getElementById('adcBufferBar');
                if (data.adc) {
                    const fillPercent = (data.adc.buffered / 2048 * 100);
                    adcBufferEl.textContent = data.adc.buffered + ' / 2048 (' + fillPercent.toFixed(1) + '%)';
                    adcBufferBar.style.width = fillPercent + '%';
                    if (fillPercent > 90) {
                        adcBufferEl.className = 'status-value status-error';
                        adcBufferBar.className = 'progress-fill error';
                    } else if (fillPercent > 75) {
                        adcBufferEl.className = 'status-value status-warning';
                        adcBufferBar.className = 'progress-fill warning';
                    } else {
                        adcBufferEl.className = 'status-value status-ok';
                        adcBufferBar.className = 'progress-fill';
                    }
                }
                
                // IMU Buffer
                const imuBufferEl = document.getElementById('imuBuffer');
                const imuBufferBar = document.getElementById('imuBufferBar');
                if (data.imu) {
                    const fillPercent = (data.imu.buffered / 1024 * 100);
                    imuBufferEl.textContent = data.imu.buffered + ' / 1024 (' + fillPercent.toFixed(1) + '%)';
                    imuBufferBar.style.width = fillPercent + '%';
                    if (fillPercent > 90) {
                        imuBufferEl.className = 'status-value status-error';
                        imuBufferBar.className = 'progress-fill error';
                    } else if (fillPercent > 75) {
                        imuBufferEl.className = 'status-value status-warning';
                        imuBufferBar.className = 'progress-fill warning';
                    } else {
                        imuBufferEl.className = 'status-value status-ok';
                        imuBufferBar.className = 'progress-fill';
                    }
                }
                
                // Write Failures
                const writeFailuresEl = document.getElementById('writeFailures');
                if (data.writes) {
                    const totalFailures = data.writes.adcFailures + data.writes.imuFailures;
                    const consecutive = Math.max(data.writes.adcConsecutiveFailures, data.writes.imuConsecutiveFailures);
                    if (totalFailures === 0 && consecutive === 0) {
                        writeFailuresEl.textContent = '✓ None';
                        writeFailuresEl.className = 'status-value status-ok';
                    } else {
                        writeFailuresEl.textContent = 'Total: ' + totalFailures + ' | Consecutive: ' + consecutive;
                        if (consecutive >= 5) {
                            writeFailuresEl.className = 'status-value status-error';
                        } else if (consecutive > 0) {
                            writeFailuresEl.className = 'status-value status-warning';
                        } else {
                            writeFailuresEl.className = 'status-value status-ok';
                        }
                    }
                } else {
                    writeFailuresEl.textContent = '-';
                    writeFailuresEl.className = 'status-value';
                }
                
                // Logger State
                const loggerStateEl = document.getElementById('loggerState');
                const states = ['IDLE', 'SESSION_OPEN', 'CONVERTING'];
                const stateText = states[data.logger.state] || 'UNKNOWN';
                loggerStateEl.textContent = stateText;
                if (data.logger.state === 1) { // SESSION_OPEN
                    loggerStateEl.className = 'status-value status-ok';
                } else if (data.logger.state === 2) { // CONVERTING
                    loggerStateEl.className = 'status-value status-warning';
                } else {
                    loggerStateEl.className = 'status-value';
                }
            } catch (error) {
                console.error('Error updating status indicators:', error);
            } finally {
                refreshIndicator.style.display = 'none';
            }
        }
        
        // Update status indicators every 2 seconds
        setInterval(updateStatusIndicators, 2000);
        
        // Chart instance
        let dataChart = null;
        let rawData = null;
        
        // Initialize chart
        function initChart() {
            const ctx = document.getElementById('dataChart').getContext('2d');
            dataChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: 'ADC Code',
                            data: [],
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            yAxisID: 'y',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            tension: 0.1
                        },
                        {
                            label: 'AX (g)',
                            data: [],
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            yAxisID: 'y1',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            tension: 0.1,
                            hidden: false
                        },
                        {
                            label: 'AY (g)',
                            data: [],
                            borderColor: 'rgb(54, 162, 235)',
                            backgroundColor: 'rgba(54, 162, 235, 0.1)',
                            yAxisID: 'y1',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            tension: 0.1,
                            hidden: false
                        },
                        {
                            label: 'AZ (g)',
                            data: [],
                            borderColor: 'rgb(255, 206, 86)',
                            backgroundColor: 'rgba(255, 206, 86, 0.1)',
                            yAxisID: 'y1',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            tension: 0.1,
                            hidden: false
                        }
                    ]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: {
                        mode: 'index',
                        intersect: false,
                    },
                    plugins: {
                        legend: {
                            display: false
                        },
                        tooltip: {
                            enabled: true,
                            mode: 'index',
                            intersect: false
                        }
                    },
                    scales: {
                        x: {
                            title: {
                                display: true,
                                text: 'Time (seconds)'
                            },
                            grid: {
                                color: 'rgba(0, 0, 0, 0.05)'
                            }
                        },
                        y: {
                            type: 'linear',
                            display: true,
                            position: 'left',
                            title: {
                                display: true,
                                text: 'Force (N)'
                            },
                            grid: {
                                color: 'rgba(75, 192, 192, 0.1)'
                            }
                        },
                        y1: {
                            type: 'linear',
                            display: true,
                            position: 'right',
                            title: {
                                display: true,
                                text: 'IMU (g)'
                            },
                            grid: {
                                drawOnChartArea: false
                            }
                        }
                    }
                }
            });
            updateChartLegend();
        }
        
        // Update chart legend
        function updateChartLegend() {
            const legend = document.getElementById('chartLegend');
            if (!dataChart) return;
            
            legend.innerHTML = '';
            dataChart.data.datasets.forEach((dataset, index) => {
                const item = document.createElement('div');
                item.className = 'legend-item';
                item.onclick = () => {
                    const meta = dataChart.getDatasetMeta(index);
                    meta.hidden = !meta.hidden;
                    dataChart.update();
                    updateChartLegend();
                };
                
                const colorBox = document.createElement('div');
                colorBox.className = 'legend-color';
                colorBox.style.backgroundColor = dataset.borderColor;
                if (dataChart.getDatasetMeta(index).hidden) {
                    colorBox.style.opacity = '0.3';
                }
                
                const label = document.createElement('span');
                label.textContent = dataset.label;
                if (dataChart.getDatasetMeta(index).hidden) {
                    label.style.textDecoration = 'line-through';
                    label.style.opacity = '0.5';
                }
                
                item.appendChild(colorBox);
                item.appendChild(label);
                legend.appendChild(item);
            });
        }
        
        // Parse CSV data
        function parseCSV(csvText) {
            const lines = csvText.trim().split('\n');
            if (lines.length < 2) return null;
            
            // Skip header
            const data = [];
            for (let i = 1; i < lines.length; i++) {
                const cols = lines[i].split(',');
                if (cols.length >= 3) {
                    data.push({
                        time: parseFloat(cols[1]),
                        adcCode: parseInt(cols[2]),
                        ax: cols.length > 4 && cols[4] ? parseFloat(cols[4]) : null,
                        ay: cols.length > 5 && cols[5] ? parseFloat(cols[5]) : null,
                        az: cols.length > 6 && cols[6] ? parseFloat(cols[6]) : null
                    });
                }
            }
            return data;
        }
        
        // Apply moving average filter (rolling window)
        // Parameters:
        //   - windowSize: Number of samples to average (1 = no filtering)
        //   - This uses a simple moving average (SMA) with a rolling window
        function applyMovingAverage(data, windowSize) {
            if (windowSize <= 1 || data.length === 0) return data;
            
            const filtered = [];
            const halfWindow = Math.floor(windowSize / 2);
            
            for (let i = 0; i < data.length; i++) {
                // Calculate window bounds (centered on current point)
                const startIdx = Math.max(0, i - halfWindow);
                const endIdx = Math.min(data.length, i + halfWindow + 1);
                const window = data.slice(startIdx, endIdx);
                
                // Calculate moving average for this point
                const avg = {
                    time: data[i].time,  // Keep original time
                    adcCode: window.reduce((sum, d) => sum + d.adcCode, 0) / window.length,
                    ax: data[i].ax !== null ? window.reduce((sum, d) => sum + (d.ax || 0), 0) / window.length : null,
                    ay: data[i].ay !== null ? window.reduce((sum, d) => sum + (d.ay || 0), 0) / window.length : null,
                    az: data[i].az !== null ? window.reduce((sum, d) => sum + (d.az || 0), 0) / window.length : null
                };
                filtered.push(avg);
            }
            return filtered;
        }
        
        // Update chart with data
        function updateChart() {
            if (!dataChart || !rawData) return;
            
            const avgWindow = parseInt(document.getElementById('avgWindow').value);
            const maxPoints = parseInt(document.getElementById('maxPoints').value);
            
            // Apply moving average filter
            let processedData = applyMovingAverage(rawData, avgWindow);
            
            // Limit points if needed
            if (processedData.length > maxPoints) {
                const step = Math.ceil(processedData.length / maxPoints);
                processedData = processedData.filter((_, i) => i % step === 0);
            }
            
            // Loadcell calibration constants (hardcoded in firmware - see loadcell_cal.h)
            const scalingFactor = 0.00667; // N per ADC count (update in loadcell_cal.h)
            const adcBaseline = 8388608;    // 24-bit ADC mid-point (update in loadcell_cal.h)
            
            // Convert ADC to Force (N): Force = (ADC - Baseline) * Scaling
            const convertAdcToForce = (adcCode) => {
                return (adcCode - adcBaseline) * scalingFactor;
            };
            
            // Update chart data
            dataChart.data.labels = processedData.map(d => d.time.toFixed(6));
            dataChart.data.datasets[0].data = processedData.map(d => ({x: d.time, y: convertAdcToForce(d.adcCode)}));
            dataChart.data.datasets[1].data = processedData.map(d => ({x: d.time, y: d.ax !== null ? d.ax : null}));
            dataChart.data.datasets[2].data = processedData.map(d => ({x: d.time, y: d.ay !== null ? d.ay : null}));
            dataChart.data.datasets[3].data = processedData.map(d => ({x: d.time, y: d.az !== null ? d.az : null}));
            
            dataChart.update('none');
            updateChartLegend();
            
            // Calculate and display max values
            updateChartStats(processedData);
            
            showStatus('✅ Chart updated: ' + processedData.length + ' points (' + rawData.length + ' original)', 'success');
        }
        
        // Update chart statistics (max force and max deceleration)
        function updateChartStats(data) {
            if (!data || data.length === 0) {
                document.getElementById('maxForce').textContent = '-';
                document.getElementById('maxDecel').textContent = '-';
                return;
            }
            
            // Loadcell calibration constants (hardcoded in firmware - see loadcell_cal.h)
            const scalingFactor = 0.00667; // N per ADC count (update in loadcell_cal.h)
            const adcBaseline = 8388608;    // 24-bit ADC mid-point (update in loadcell_cal.h)
            
            // Find max ADC and convert to Force (N)
            let maxAdc = data[0].adcCode;
            let maxAdcTime = data[0].time;
            for (let i = 1; i < data.length; i++) {
                if (data[i].adcCode > maxAdc) {
                    maxAdc = data[i].adcCode;
                    maxAdcTime = data[i].time;
                }
            }
            
            // Convert max ADC to Force (N)
            const maxForceN = (maxAdc - adcBaseline) * scalingFactor;
            
            // Find max deceleration (max AZ value)
            let maxDecel = null;
            let maxDecelTime = null;
            for (let i = 0; i < data.length; i++) {
                if (data[i].az !== null && (maxDecel === null || data[i].az > maxDecel)) {
                    maxDecel = data[i].az;
                    maxDecelTime = data[i].time;
                }
            }
            
            // Update display
            const maxForceEl = document.getElementById('maxForce');
            const maxDecelEl = document.getElementById('maxDecel');
            
            if (maxForceEl) {
                maxForceEl.textContent = maxForceN.toFixed(2) + ' N at ' + maxAdcTime.toFixed(6) + 's';
            }
            
            if (maxDecelEl) {
                if (maxDecel !== null) {
                    maxDecelEl.textContent = maxDecel.toFixed(3) + 'g at ' + maxDecelTime.toFixed(6) + 's';
                } else {
                    maxDecelEl.textContent = 'N/A';
                }
            }
        }
        
        // Parse filename to extract date/time for chart title
        // Format: YYYYMMDD_HHMMSS.csv (e.g., "20251204_153012.csv")
        function parseFilenameForTitle(filename) {
            if (!filename) return 'Data Visualization';
            
            // Remove path and extension
            const basename = filename.replace(/^.*\//, '').replace(/\.csv$/, '');
            
            // Parse YYYYMMDD_HHMMSS format
            const match = basename.match(/^(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})$/);
            if (match) {
                const [, year, month, day, hour, minute, second] = match;
                const dateStr = year + '-' + month + '-' + day;
                const timeStr = hour + ':' + minute + ':' + second;
                return '📊 ' + dateStr + ' ' + timeStr;
            }
            
            // Fallback: use filename as-is
            return '📊 ' + basename;
        }
        
        // Load latest CSV from ESP32
        async function loadLatestCsv() {
            try {
                showStatus('Loading CSV file list...', 'info');
                const listResponse = await fetch('/csv/list');
                const fileList = await listResponse.json();
                
                if (!fileList || fileList.length === 0) {
                    showStatus('❌ No CSV files found on SD card', 'error');
                    return;
                }
                
                // Get the most recent file (assuming files are named with timestamp)
                const latestFile = fileList[fileList.length - 1];
                
                showStatus('Loading CSV file: ' + latestFile.filename + '...', 'info');
                const fileResponse = await fetch('/csv/file?file=' + encodeURIComponent(latestFile.filename));
                
                if (!fileResponse.ok) {
                    showStatus('❌ Error loading CSV file: ' + fileResponse.statusText, 'error');
                    return;
                }
                
                const csvText = await fileResponse.text();
                rawData = parseCSV(csvText);
                
                if (!rawData || rawData.length === 0) {
                    showStatus('❌ Error: Invalid CSV file or no data found', 'error');
                    return;
                }
                
                // Update chart title with filename
                const title = parseFilenameForTitle(latestFile.filename);
                document.getElementById('chartTitle').textContent = title;
                
                showStatus('✅ Loaded ' + rawData.length + ' data points from ' + latestFile.filename, 'success');
                updateChart();
            } catch (error) {
                showStatus('❌ Error loading CSV: ' + error, 'error');
            }
        }
        
        // Reload latest CSV (for manual refresh)
        async function reloadLatestCsv() {
            showStatus('Reloading latest CSV...', 'info');
            await loadLatestCsv();
        }
        
        // Load config on page load
        window.onload = function() {
            loadConfig();
            updateStatusIndicators();  // Initial update
            initChart();
            // Auto-load latest CSV on page load
            setTimeout(loadLatestCsv, 500);
        };
    </script>
</body>
</html>
)HTML_PAGE";

// Handle root page
static void handleRoot()
{
    server.send(200, "text/html", htmlPage);
}

// Handle configuration GET (return current config as JSON)
static void handleConfigGet()
{
    String json = "{";
    json += "\"adcSampleRate\":" + String(currentConfig.adcSampleRate) + ",";
    json += "\"adcPgaGain\":" + String(static_cast<uint8_t>(currentConfig.adcPgaGain)) + ",";
    json += "\"imuOdr\":" + String(currentConfig.imuOdr) + ",";
    json += "\"imuAccelRange\":" + String(currentConfig.imuAccelRange) + ",";
    json += "\"imuGyroRange\":" + String(currentConfig.imuGyroRange);
    json += "}";
    
    server.send(200, "application/json", json);
}

// Validate configuration parameters
static bool validateConfig(uint32_t adcRate, uint8_t adcGain, uint32_t imuOdr, 
                           uint16_t imuAccel, uint16_t imuGyro, String &errorMsg)
{
    // Validate ADC sample rate (1000-64000 Hz, must be multiple of 1000)
    if (adcRate < 1000 || adcRate > 64000 || (adcRate % 1000 != 0))
    {
        errorMsg = "ADC sample rate must be between 1000-64000 Hz and multiple of 1000";
        return false;
    }
    
    // Validate ADC PGA gain (0-7)
    if (adcGain > 7)
    {
        errorMsg = "ADC PGA gain must be 0-7 (x1 to x128)";
        return false;
    }
    
    // Validate IMU ODR (15-960 Hz, common values: 15, 30, 60, 120, 240, 480, 960)
    if (imuOdr < 15 || imuOdr > 960)
    {
        errorMsg = "IMU ODR must be between 15-960 Hz";
        return false;
    }
    
    // Validate IMU accelerometer range (2, 4, 8, 16 g)
    if (imuAccel != 2 && imuAccel != 4 && imuAccel != 8 && imuAccel != 16)
    {
        errorMsg = "IMU accelerometer range must be 2, 4, 8, or 16 g";
        return false;
    }
    
    // Validate IMU gyroscope range (125, 250, 500, 1000, 2000 dps)
    if (imuGyro != 125 && imuGyro != 250 && imuGyro != 500 && 
        imuGyro != 1000 && imuGyro != 2000)
    {
        errorMsg = "IMU gyroscope range must be 125, 250, 500, 1000, or 2000 dps";
        return false;
    }
    
    return true;
}

// Handle configuration POST (save new config)
static void handleConfigPost()
{
    // Rate limiting: max 1 request per second
    static uint32_t lastConfigRequest = 0;
    uint32_t now = millis();
    if (now - lastConfigRequest < 1000)
    {
        server.send(429, "text/plain", "Too many requests. Please wait 1 second.");
        return;
    }
    lastConfigRequest = now;
    
    // Parse and validate parameters
    uint32_t adcRate = currentConfig.adcSampleRate;
    uint8_t adcGain = static_cast<uint8_t>(currentConfig.adcPgaGain);
    uint32_t imuOdr = currentConfig.imuOdr;
    uint16_t imuAccel = currentConfig.imuAccelRange;
    uint16_t imuGyro = currentConfig.imuGyroRange;
    
    if (server.hasArg("adcSampleRate"))
    {
        adcRate = server.arg("adcSampleRate").toInt();
    }
    if (server.hasArg("adcPgaGain"))
    {
        adcGain = server.arg("adcPgaGain").toInt();
    }
    if (server.hasArg("imuOdr"))
    {
        imuOdr = server.arg("imuOdr").toInt();
    }
    if (server.hasArg("imuAccelRange"))
    {
        imuAccel = server.arg("imuAccelRange").toInt();
    }
    if (server.hasArg("imuGyroRange"))
    {
        imuGyro = server.arg("imuGyroRange").toInt();
    }
    
    // Validate configuration
    String errorMsg;
    if (!validateConfig(adcRate, adcGain, imuOdr, imuAccel, imuGyro, errorMsg))
    {
        server.send(400, "text/plain", "Invalid configuration: " + errorMsg);
        Serial.print("[WEBCONFIG] Configuration validation failed: ");
        Serial.println(errorMsg);
        return;
    }
    
    // Apply validated configuration
    currentConfig.adcSampleRate = adcRate;
    currentConfig.adcPgaGain = static_cast<AdcPgaGain>(adcGain);
    currentConfig.imuOdr = imuOdr;
    currentConfig.imuAccelRange = imuAccel;
    currentConfig.imuGyroRange = imuGyro;
    
    // Persist configuration to NVS
    Preferences preferences;
    if (preferences.begin("webconfig", false))  // Read-write mode
    {
        preferences.putUInt("adcSampleRate", adcRate);
        preferences.putUInt("adcPgaGain", adcGain);
        preferences.putUInt("imuOdr", imuOdr);
        preferences.putUShort("imuAccelRange", imuAccel);
        preferences.putUShort("imuGyroRange", imuGyro);
        preferences.end();
        Serial.println("[WEBCONFIG] Configuration saved to NVS");
    }
    else
    {
        Serial.println("[WEBCONFIG] WARNING: Failed to open NVS for writing");
    }
    
    server.send(200, "text/plain", "OK");
    Serial.println("[WEBCONFIG] Configuration updated and validated via web interface");
}


// Handle system status endpoint
static void handleStatus()
{
    // Rate limiting: max 2 requests per second
    static uint32_t lastStatusRequest = 0;
    uint32_t now = millis();
    if (now - lastStatusRequest < 500)
    {
        server.send(429, "text/plain", "Too many requests");
        return;
    }
    lastStatusRequest = now;
    
    // Get system statistics
    size_t adcBuffered = adcGetBufferedSampleCount();
    size_t adcOverflow = adcGetOverflowCount();
    size_t imuBuffered = imuGetBufferedSampleCount();
    size_t imuOverflow = imuGetOverflowCount();
    uint32_t adcCounter = adcGetSampleCounter();
    
    // Get logger state
    LoggerState loggerState = loggerGetState();
    bool sessionOpen = loggerIsSessionOpen();
    
    // Get SD card status
    bool sdMounted = sdCardIsMounted();
    bool sdPresent = sdCardCheckPresent();
    uint64_t sdFreeSpace = sdCardGetFreeSpace();
    uint64_t sdTotalSpace = sdCardGetTotalSpace();
    
    // Get write statistics
    LoggerWriteStats writeStats = loggerGetWriteStats();
    
    // Get free heap
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    
    // Build JSON response (expanded buffer for additional fields)
    char jsonBuffer[768];
    snprintf(jsonBuffer, sizeof(jsonBuffer),
        "{"
        "\"adc\":{"
            "\"buffered\":%u,"
            "\"overflow\":%u,"
            "\"counter\":%lu"
        "},"
        "\"imu\":{"
            "\"buffered\":%u,"
            "\"overflow\":%u"
        "},"
        "\"logger\":{"
            "\"state\":%d,"
            "\"sessionOpen\":%s"
        "},"
        "\"sd\":{"
            "\"mounted\":%s,"
            "\"present\":%s,"
            "\"freeSpace\":%llu,"
            "\"totalSpace\":%llu,"
            "\"freePercent\":%.1f"
        "},"
        "\"writes\":{"
            "\"adcFailures\":%u,"
            "\"imuFailures\":%u,"
            "\"adcConsecutiveFailures\":%u,"
            "\"imuConsecutiveFailures\":%u,"
            "\"adcRecordsWritten\":%u,"
            "\"imuRecordsWritten\":%u"
        "},"
        "\"memory\":{"
            "\"freeHeap\":%u,"
            "\"totalHeap\":%u,"
            "\"freePercent\":%.1f"
        "}"
        "}",
        (unsigned)adcBuffered, (unsigned)adcOverflow, (unsigned long)adcCounter,
        (unsigned)imuBuffered, (unsigned)imuOverflow,
        (int)loggerState, sessionOpen ? "true" : "false",
        sdMounted ? "true" : "false", sdPresent ? "true" : "false",
        (unsigned long long)sdFreeSpace, (unsigned long long)sdTotalSpace,
        sdTotalSpace > 0 ? (100.0f * sdFreeSpace / sdTotalSpace) : 0.0f,
        writeStats.adcWriteFailures, writeStats.imuWriteFailures,
        writeStats.adcConsecutiveFailures, writeStats.imuConsecutiveFailures,
        writeStats.adcRecordsWritten, writeStats.imuRecordsWritten,
        freeHeap, totalHeap, (100.0f * freeHeap / totalHeap));
    
    server.send(200, "application/json", jsonBuffer);
}

// Handle CSV file list request
static void handleCsvList()
{
    if (!sdCardIsMounted())
    {
        server.send(500, "application/json", "{\"error\":\"SD card not mounted\"}");
        return;
    }
    
    fs::FS &fs = sdCardGetFs();
    File root = fs.open("/log");
    if (!root)
    {
        server.send(500, "application/json", "{\"error\":\"Failed to open /log directory\"}");
        return;
    }
    
    if (!root.isDirectory())
    {
        server.send(500, "application/json", "{\"error\":\"/log is not a directory\"}");
        root.close();
        return;
    }
    
    // Build JSON array of CSV files
    String json = "[";
    bool first = true;
    File file = root.openNextFile();
    
    while (file)
    {
        if (!file.isDirectory())
        {
            String filename = file.name();
            if (filename.endsWith(".csv"))
            {
                if (!first) json += ",";
                first = false;
                
                // Extract just the filename (remove path)
                int lastSlash = filename.lastIndexOf('/');
                String basename = lastSlash >= 0 ? filename.substring(lastSlash + 1) : filename;
                
                json += "{";
                json += "\"filename\":\"" + basename + "\",";
                json += "\"path\":\"" + filename + "\",";
                json += "\"size\":" + String(file.size());
                json += "}";
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    json += "]";
    server.send(200, "application/json", json);
}

// Handle CSV file content request
static void handleCsvFile()
{
    if (!sdCardIsMounted())
    {
        server.send(500, "text/plain", "SD card not mounted");
        return;
    }
    
    if (!server.hasArg("file"))
    {
        server.send(400, "text/plain", "Missing 'file' parameter");
        return;
    }
    
    String filepath = "/log/" + server.arg("file");
    
    // Security: ensure file is in /log directory and ends with .csv
    if (!filepath.startsWith("/log/") || !filepath.endsWith(".csv"))
    {
        server.send(400, "text/plain", "Invalid file path");
        return;
    }
    
    fs::FS &fs = sdCardGetFs();
    File file = fs.open(filepath, FILE_READ);
    if (!file)
    {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    // Stream file content
    server.setContentLength(file.size());
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + server.arg("file") + "\"");
    server.send(200);
    
    // Stream in chunks to avoid memory issues
    const size_t chunkSize = 4096;
    uint8_t buffer[chunkSize];
    while (file.available())
    {
        size_t bytesRead = file.read(buffer, chunkSize);
        if (bytesRead > 0)
        {
            server.client().write(buffer, bytesRead);
        }
    }
    
    file.close();
}

bool webConfigInit()
{
    // Load calibration values from NVS
    loadCalibrationFromNVS();
    
    // Load saved configuration from NVS (if available)
    Preferences preferences;
    if (preferences.begin("webconfig", true))  // Read-only mode
    {
        uint32_t savedAdcRate = preferences.getUInt("adcSampleRate", 0);
        uint8_t savedAdcGain = preferences.getUInt("adcPgaGain", 255);  // 255 = not set
        uint32_t savedImuOdr = preferences.getUInt("imuOdr", 0);
        uint16_t savedImuAccel = preferences.getUShort("imuAccelRange", 0);
        uint16_t savedImuGyro = preferences.getUShort("imuGyroRange", 0);
        preferences.end();
        
        // Apply saved configuration if valid
        if (savedAdcRate >= 1000 && savedAdcRate <= 64000 && savedAdcGain <= 7)
        {
            currentConfig.adcSampleRate = savedAdcRate;
            currentConfig.adcPgaGain = static_cast<AdcPgaGain>(savedAdcGain);
            Serial.println("[WEBCONFIG] Loaded saved ADC configuration from NVS");
        }
        
        if (savedImuOdr >= 15 && savedImuOdr <= 960)
        {
            currentConfig.imuOdr = savedImuOdr;
            Serial.println("[WEBCONFIG] Loaded saved IMU ODR from NVS");
        }
        
        if (savedImuAccel == 2 || savedImuAccel == 4 || savedImuAccel == 8 || savedImuAccel == 16)
        {
            currentConfig.imuAccelRange = savedImuAccel;
            Serial.println("[WEBCONFIG] Loaded saved IMU accel range from NVS");
        }
        
        if (savedImuGyro == 125 || savedImuGyro == 250 || savedImuGyro == 500 || 
            savedImuGyro == 1000 || savedImuGyro == 2000)
        {
            currentConfig.imuGyroRange = savedImuGyro;
            Serial.println("[WEBCONFIG] Loaded saved IMU gyro range from NVS");
        }
    }
    
    // Get SSID from NVS (or generate and store if first boot)
    String ssid = getOrGenerateSSID();
    
    Serial.print("[WEBCONFIG] Starting WiFi Access Point: ");
    Serial.println(ssid);
    
    // Start WiFi AP (no password)
    if (!WiFi.softAP(ssid.c_str()))
    {
        Serial.println("[WEBCONFIG] Failed to start WiFi AP");
        return false;
    }
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("[WEBCONFIG] AP IP address: ");
    Serial.println(IP);
    Serial.println("[WEBCONFIG] Connect to WiFi network: " + ssid);
    Serial.println("[WEBCONFIG] Open browser to: http://" + IP.toString());
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/config", HTTP_GET, handleConfigGet);
    server.on("/config", HTTP_POST, handleConfigPost);
    server.on("/status", handleStatus);  // System status endpoint
    server.on("/csv/list", HTTP_GET, handleCsvList);  // List CSV files
    server.on("/csv/file", HTTP_GET, handleCsvFile);  // Serve CSV file content
    // Hidden calibration portal (not linked from main page)
    server.on("/cal", HTTP_GET, handleCalibrationPage);  // Calibration portal page
    server.on("/cal", HTTP_POST, handleCalibrationPost);  // Save calibration values
    server.on("/cal/values", HTTP_GET, handleCalibrationGet);  // Get calibration values (JSON)
    
    // Start server
    server.begin();
    
    webConfigActive = true;
    return true;
}

void webConfigHandleClient()
{
    if (webConfigActive)
    {
        server.handleClient();
    }
}

LoggerConfig webConfigGetLoggerConfig()
{
    return currentConfig;
}

void webConfigSetLoggerConfig(const LoggerConfig &config)
{
    currentConfig = config;
}

bool webConfigIsActive()
{
    return webConfigActive;
}

