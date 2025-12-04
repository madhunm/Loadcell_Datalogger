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
        .status-card[style*="grid-column"] {
            min-width: 100%;
        }
        .status.warning {
            background: #feebc8;
            color: #7c2d12;
            border-left: 4px solid #ed8936;
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
        /* Tab Navigation */
        .tabs {
            display: flex;
            border-bottom: 2px solid #e2e8f0;
            margin: 25px 0 0 0;
            gap: 0;
        }
        .tab {
            padding: 12px 24px;
            background: transparent;
            border: none;
            border-bottom: 3px solid transparent;
            cursor: pointer;
            font-size: 15px;
            font-weight: 600;
            color: #718096;
            transition: all 0.2s;
            position: relative;
            bottom: -2px;
        }
        .tab:hover {
            color: #4a5568;
            background: #f7fafc;
        }
        .tab.active {
            color: #667eea;
            border-bottom-color: #667eea;
            background: transparent;
        }
        .tab-content {
            display: none;
            padding: 20px 0;
        }
        .tab-content.active {
            display: block;
            animation: fadeIn 0.3s ease-in;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        small {
            display: block;
            margin-top: 5px;
            font-size: 12px;
            color: #718096;
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
        
        <!-- Tab Navigation -->
        <div class="tabs">
            <button class="tab active" onclick="switchTab('status', this)">📈 Status</button>
            <button class="tab" onclick="switchTab('data', this)">📊 Data Visualization</button>
            <button class="tab" onclick="switchTab('help', this)">❓ Help & User Guide</button>
        </div>
        
        <!-- Status Tab -->
        <div id="tab-status" class="tab-content active">
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
                <div class="status-card" style="grid-column: span 2;">
                    <div class="status-header">
                        <span class="status-label">💿 SD Card Storage</span>
                        <span id="sdSpace" class="status-value">-</span>
                    </div>
                    <div style="margin-top: 15px;">
                        <div style="margin-bottom: 8px;"><strong>Total:</strong> <span id="sdTotal">-</span></div>
                        <div style="margin-bottom: 8px;"><strong>Free:</strong> <span id="sdFree">-</span></div>
                        <div style="margin-bottom: 8px;"><strong>Used:</strong> <span id="sdUsed">-</span></div>
                        <div class="progress-bar" style="margin-top: 10px;">
                            <div id="sdSpaceBar" class="progress-fill" style="width: 0%;"></div>
                        </div>
                        <div id="sdSpaceStatus" style="margin-top: 10px; padding: 8px; border-radius: 4px; font-size: 12px;"></div>
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
        </div>
        
        <!-- Data Visualization Tab -->
        <div id="tab-data" class="tab-content">
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
            <div class="chart-stats" id="chartStats" style="margin-top: 15px; padding: 15px; background: #f7fafc; border-radius: 8px; font-size: 14px;">
                <div style="margin-bottom: 15px;">
                    <strong style="display: block; margin-bottom: 8px;">Max Force (N):</strong>
                    <div id="maxForce" style="display: flex; flex-wrap: wrap; gap: 8px;"></div>
                </div>
                <div>
                    <strong style="display: block; margin-bottom: 8px;">Max Deceleration:</strong>
                    <div id="maxDecel" style="display: flex; flex-wrap: wrap; gap: 8px;"></div>
                </div>
            </div>
            </div>
        </div>
        
        <!-- Help Tab -->
        <div id="tab-help" class="tab-content">
            <div class="section">
                <h2>❓ Help & User Guide</h2>
            <div style="line-height: 1.8; color: #4a5568;">
                <h3 style="color: #2d3748; margin-top: 0;">How to Use the Datalogger</h3>
                <ol style="padding-left: 20px;">
                    <li><strong>Start Logging:</strong> Press the LOG_START button on the device to begin a logging session.</li>
                    <li><strong>Monitor Status:</strong> Watch the NeoPixel LED for visual feedback on system status.</li>
                    <li><strong>Stop Logging:</strong> Press the LOG_START button again to stop the session.</li>
                    <li><strong>Wait for Conversion:</strong> The LED will blink orange/yellow while converting binary logs to CSV (do not remove SD card).</li>
                    <li><strong>Safe to Remove:</strong> When the LED shows a green double-blink pattern, it's safe to remove the SD card.</li>
                    <li><strong>View Data:</strong> Insert the SD card into your computer and open the CSV files in spreadsheet software.</li>
                </ol>
                
                <h3 style="color: #2d3748; margin-top: 25px;">NeoPixel LED Status Indicators</h3>
                <p style="margin-bottom: 15px;">The NeoPixel LED provides visual feedback about the system state. Below are all the patterns you may see:</p>
                
                <div id="neopixelPatterns" style="display: grid; gap: 15px;">
                    <!-- Patterns will be dynamically generated here -->
                </div>
            </div>
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

        // Note: ADC/IMU configuration has been moved to the calibration portal (/cal)

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
                
                // SD Card free space (progress bar)
                const sdSpaceEl = document.getElementById('sdSpace');
                const sdTotalEl = document.getElementById('sdTotal');
                const sdFreeEl = document.getElementById('sdFree');
                const sdUsedEl = document.getElementById('sdUsed');
                const sdSpaceStatusEl = document.getElementById('sdSpaceStatus');
                
                if (data.sd && data.sd.totalSpace > 0) {
                    const freeMB = (data.sd.freeSpace / (1024 * 1024)).toFixed(1);
                    const totalMB = (data.sd.totalSpace / (1024 * 1024)).toFixed(1);
                    const usedMB = ((data.sd.totalSpace - data.sd.freeSpace) / (1024 * 1024)).toFixed(1);
                    const percent = data.sd.freePercent;
                    
                    sdSpaceEl.textContent = percent.toFixed(1) + '% Free';
                    sdTotalEl.textContent = totalMB + ' MB';
                    sdFreeEl.textContent = freeMB + ' MB';
                    sdUsedEl.textContent = usedMB + ' MB';
                    
                    // Update progress bar
                    const sdSpaceBar = document.getElementById('sdSpaceBar');
                    if (sdSpaceBar) {
                        sdSpaceBar.style.width = percent + '%';
                        if (percent < 10) {
                            sdSpaceBar.className = 'progress-fill error';
                        } else if (percent < 25) {
                            sdSpaceBar.className = 'progress-fill warning';
                        } else {
                            sdSpaceBar.className = 'progress-fill';
                        }
                    }
                    
                    // Status message
                    if (percent < 10) {
                        sdSpaceEl.className = 'status-value status-error';
                        sdSpaceStatusEl.className = 'status error';
                        sdSpaceStatusEl.textContent = '⚠️ Critical: Less than 10% free space remaining!';
                    } else if (percent < 25) {
                        sdSpaceEl.className = 'status-value status-warning';
                        sdSpaceStatusEl.className = 'status warning';
                        sdSpaceStatusEl.textContent = '⚠️ Warning: Less than 25% free space remaining.';
                    } else {
                        sdSpaceEl.className = 'status-value status-ok';
                        sdSpaceStatusEl.className = 'status success';
                        sdSpaceStatusEl.textContent = '✓ Storage space is healthy.';
                    }
                } else {
                    sdSpaceEl.textContent = 'N/A';
                    sdSpaceEl.className = 'status-value';
                    sdTotalEl.textContent = '-';
                    sdFreeEl.textContent = '-';
                    sdUsedEl.textContent = '-';
                    
                    // Reset progress bar
                    const sdSpaceBar = document.getElementById('sdSpaceBar');
                    if (sdSpaceBar) {
                        sdSpaceBar.style.width = '0%';
                        sdSpaceBar.className = 'progress-fill';
                    }
                    
                    if (sdSpaceStatusEl) {
                        sdSpaceStatusEl.textContent = '';
                        sdSpaceStatusEl.className = '';
                    }
                }
                
                // Write Failures
                const writeFailuresEl = document.getElementById('writeFailures');
                if (data.writeStats) {
                    const totalFailures = data.writeStats.adcFailures + data.writeStats.imuFailures;
                    const consecutive = Math.max(data.writeStats.adcConsecutiveFailures, data.writeStats.imuConsecutiveFailures);
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
        let csvData = null;
        let rawData = null;
        
        // Data comparison: store multiple datasets
        let comparisonData = []; // Array of {filename, data, color}
        let availableFiles = []; // List of available CSV files
        let autoRefreshInterval = null;
        let autoRefreshEnabled = false;
        
        // Color palette for comparison datasets
        const comparisonColors = [
            'rgb(75, 192, 192)',   // Teal
            'rgb(255, 99, 132)',   // Red
            'rgb(54, 162, 235)',   // Blue
            'rgb(255, 206, 86)',   // Yellow
            'rgb(153, 102, 255)',  // Purple
            'rgb(255, 159, 64)',   // Orange
            'rgb(201, 203, 207)', // Grey
            'rgb(255, 99, 255)'    // Magenta
        ];
        
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
        
        // NeoPixel pattern definitions (matching neopixel.cpp)
        const neopixelPatterns = [
            {
                name: 'OFF',
                description: 'LED is off',
                color: '#000000',
                pattern: 'solid',
                timing: null
            },
            {
                name: 'INIT',
                description: 'System initializing / peripherals starting up',
                color: '#0080FF', // RGB(0, 128, 255) - Blue (ANSI Z535.1 - Mandatory Action)
                pattern: 'solid',
                timing: null
            },
            {
                name: 'READY',
                description: 'System ready to start logging (breathing green pattern)',
                color: '#00FF00', // RGB(0, 255, 0) - green
                pattern: 'breathing',
                timing: { period: 2000, minBrightness: 30, maxBrightness: 255 }
            },
            {
                name: 'LOGGING',
                description: 'Actively logging data to SD card',
                color: '#00FF00', // RGB(0, 255, 0) - green
                pattern: 'solid',
                timing: null
            },
            {
                name: 'CONVERTING',
                description: 'Converting binary logs to CSV (DO NOT remove SD card)',
                color: '#FFA500', // RGB(255, 165, 0) - Yellow/Amber (ANSI Z535.1 - Warning)
                pattern: 'blink',
                timing: { on: 250, off: 250, pulses: 1, gap: 0 }
            },
            {
                name: 'SAFE TO REMOVE',
                description: 'CSV conversion complete (safe to remove SD card)',
                color: '#00FF00', // RGB(0, 255, 0) - green
                pattern: 'double-blink',
                timing: { on: 100, off: 100, pulses: 2, gap: 800 }
            },
            {
                name: 'ERROR: SD Card',
                description: 'SD card initialization or access error',
                color: '#FF0000', // RGB(255, 0, 0) - red
                pattern: 'double-blink',
                timing: { on: 100, off: 100, pulses: 2, gap: 600 }
            },
            {
                name: 'ERROR: RTC',
                description: 'Real-time clock initialization error',
                color: '#FFA500', // RGB(255, 165, 0) - Yellow/Amber (ANSI Z535.1 - Warning)
                pattern: 'blink',
                timing: { on: 200, off: 800, pulses: 1, gap: 0 }
            },
            {
                name: 'ERROR: IMU',
                description: 'IMU (accelerometer/gyroscope) initialization error',
                color: '#FF0000', // RGB(255, 0, 0) - Red (ANSI Z535.1 - Emergency/Hazard)
                pattern: 'triple-blink',
                timing: { on: 100, off: 100, pulses: 3, gap: 600 }
            },
            {
                name: 'ERROR: ADC',
                description: 'ADC (loadcell) initialization error',
                color: '#FF0000', // RGB(255, 0, 0) - Red (ANSI Z535.1 - Emergency/Hazard)
                pattern: 'double-blink',
                timing: { on: 200, off: 300, pulses: 2, gap: 500 }
            },
            {
                name: 'ERROR: Write Failure',
                description: 'Persistent SD card write failures (critical)',
                color: '#FF0000', // RGB(255, 0, 0) - red
                pattern: 'fast-blink',
                timing: { on: 50, off: 50, pulses: 1, gap: 0 }
            },
            {
                name: 'ERROR: Low Space',
                description: 'SD card running low on free space',
                color: '#FFA500', // RGB(255, 165, 0) - orange
                pattern: 'double-blink',
                timing: { on: 200, off: 200, pulses: 2, gap: 400 }
            },
            {
                name: 'ERROR: Buffer Full',
                description: 'ADC or IMU ring buffer overflow',
                color: '#FF0000', // RGB(255, 0, 0) - Red (ANSI Z535.1 - Emergency/Hazard)
                pattern: 'triple-blink',
                timing: { on: 100, off: 100, pulses: 3, gap: 400 }
            }
        ];
        
        // Generate NeoPixel pattern display with animations
        function generateNeopixelPatterns() {
            const container = document.getElementById('neopixelPatterns');
            if (!container) return;
            
            container.innerHTML = '';
            
            neopixelPatterns.forEach(pattern => {
                const patternDiv = document.createElement('div');
                patternDiv.style.cssText = 'background: white; padding: 15px; border-radius: 8px; border: 2px solid #e2e8f0; display: flex; align-items: center; gap: 15px;';
                
                // LED indicator
                const ledDiv = document.createElement('div');
                ledDiv.style.cssText = 'width: 40px; height: 40px; border-radius: 50%; background: ' + pattern.color + '; box-shadow: 0 0 10px ' + pattern.color + '; position: relative; flex-shrink: 0;';
                
                // Add animation based on pattern type
                if (pattern.pattern === 'breathing' && pattern.timing) {
                    const period = pattern.timing.period / 1000; // Convert ms to seconds
                    ledDiv.style.animation = `breathing ${period}s ease-in-out infinite`;
                } else if (pattern.pattern === 'blink' && pattern.timing) {
                    const duration = (pattern.timing.on + pattern.timing.off) / 1000;
                    ledDiv.style.animation = `blink ${duration}s infinite`;
                } else if (pattern.pattern === 'double-blink' && pattern.timing) {
                    const cycleTime = (pattern.timing.on + pattern.timing.off) * pattern.timing.pulses + pattern.timing.gap;
                    ledDiv.style.animation = `doubleBlink ${cycleTime / 1000}s infinite`;
                } else if (pattern.pattern === 'triple-blink' && pattern.timing) {
                    const cycleTime = (pattern.timing.on + pattern.timing.off) * pattern.timing.pulses + pattern.timing.gap;
                    ledDiv.style.animation = `tripleBlink ${cycleTime / 1000}s infinite`;
                } else if (pattern.pattern === 'fast-blink' && pattern.timing) {
                    const duration = (pattern.timing.on + pattern.timing.off) / 1000;
                    ledDiv.style.animation = `blink ${duration}s infinite`;
                }
                
                // Text info
                const textDiv = document.createElement('div');
                textDiv.style.cssText = 'flex: 1;';
                textDiv.innerHTML = `
                    <div style="font-weight: 600; color: #2d3748; margin-bottom: 4px;">${pattern.name}</div>
                    <div style="font-size: 13px; color: #718096;">${pattern.description}</div>
                `;
                
                patternDiv.appendChild(ledDiv);
                patternDiv.appendChild(textDiv);
                container.appendChild(patternDiv);
            });
        }
        
        // Add CSS animations for blink patterns
        const style = document.createElement('style');
        style.textContent = `
            @keyframes breathing {
                0%, 100% { 
                    opacity: 0.12; /* ~30/255 brightness */
                    transform: scale(0.95);
                }
                50% { 
                    opacity: 1; /* 255/255 brightness */
                    transform: scale(1);
                }
            }
            @keyframes blink {
                0%, 50% { opacity: 1; }
                50.01%, 100% { opacity: 0.1; }
            }
            @keyframes doubleBlink {
                0%, 4.5% { opacity: 1; }
                4.51%, 9% { opacity: 0.1; }
                9.01%, 13.5% { opacity: 1; }
                13.51%, 18% { opacity: 0.1; }
                18.01%, 100% { opacity: 0.1; }
            }
            @keyframes tripleBlink {
                0%, 2.5% { opacity: 1; }
                2.51%, 5% { opacity: 0.1; }
                5.01%, 7.5% { opacity: 1; }
                7.51%, 10% { opacity: 0.1; }
                10.01%, 12.5% { opacity: 1; }
                12.51%, 100% { opacity: 0.1; }
            }
        `;
        document.head.appendChild(style);
        
        // Load calibration values from endpoint
        let calibrationScale = 0.00667;
        let calibrationOffset = 8388608;
        
        async function loadCalibration() {
            try {
                const response = await fetch('/cal/values');
                const data = await response.json();
                calibrationScale = data.scale;
                calibrationOffset = data.offset;
            } catch (error) {
                console.warn('Could not load calibration, using defaults:', error);
            }
        }
        
        // Update chart to use calibration values (supports comparison mode)
        function updateChart() {
            if (!dataChart) {
                console.error('Chart not initialized!');
                showStatus('❌ Chart not initialized. Please refresh the page.', 'error');
                return;
            }
            
            const avgWindow = parseInt(document.getElementById('avgWindow').value);
            const maxPoints = parseInt(document.getElementById('maxPoints').value);
            
            // Use calibration values from endpoint
            const convertAdcToForce = (adcCode) => {
                return (adcCode - calibrationOffset) * calibrationScale;
            };
            
            // Clear existing datasets (keep base structure)
            const baseDatasets = [];
            let allLabels = [];
            
            // Process main dataset if available
            if (rawData && rawData.length > 0) {
                let processedData = applyMovingAverage(rawData, avgWindow);
                if (processedData.length > maxPoints) {
                    const step = Math.ceil(processedData.length / maxPoints);
                    processedData = processedData.filter((_, i) => i % step === 0);
                }
                allLabels = processedData.map(d => d.time.toFixed(6));
                
                // Add main dataset
                baseDatasets.push({
                    label: 'Force (N) - Current',
                    data: processedData.map(d => ({ x: d.time, y: convertAdcToForce(d.adcCode) })),
                    borderColor: 'rgb(75, 192, 192)',
                    backgroundColor: 'rgba(75, 192, 192, 0.1)',
                    yAxisID: 'y',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    tension: 0.1
                });
                baseDatasets.push({
                    label: 'AX (g)',
                    data: processedData.map(d => ({ x: d.time, y: d.ax !== null ? d.ax : null })),
                    borderColor: 'rgb(255, 99, 132)',
                    backgroundColor: 'rgba(255, 99, 132, 0.1)',
                    yAxisID: 'y1',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    tension: 0.1,
                    hidden: false
                });
                baseDatasets.push({
                    label: 'AY (g)',
                    data: processedData.map(d => ({ x: d.time, y: d.ay !== null ? d.ay : null })),
                    borderColor: 'rgb(54, 162, 235)',
                    backgroundColor: 'rgba(54, 162, 235, 0.1)',
                    yAxisID: 'y1',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    tension: 0.1,
                    hidden: false
                });
                baseDatasets.push({
                    label: 'AZ (g)',
                    data: processedData.map(d => ({ x: d.time, y: d.az !== null ? d.az : null })),
                    borderColor: 'rgb(255, 206, 86)',
                    backgroundColor: 'rgba(255, 206, 86, 0.1)',
                    yAxisID: 'y1',
                    borderWidth: 1.5,
                    pointRadius: 0,
                    tension: 0.1,
                    hidden: false
                });
            }
            
            // Add comparison datasets
            const allProcessedDatasets = []; // Store processed data with labels for stats calculation
            if (rawData && rawData.length > 0) {
                let processedData = applyMovingAverage(rawData, avgWindow);
                if (processedData.length > maxPoints) {
                    const step = Math.ceil(processedData.length / maxPoints);
                    processedData = processedData.filter((_, i) => i % step === 0);
                }
                allProcessedDatasets.push({ label: 'Current', data: processedData });
            }
            
            comparisonData.forEach((comp, idx) => {
                if (comp.data && comp.data.length > 0) {
                    let processedComp = applyMovingAverage(comp.data, avgWindow);
                    if (processedComp.length > maxPoints) {
                        const step = Math.ceil(processedComp.length / maxPoints);
                        processedComp = processedComp.filter((_, i) => i % step === 0);
                    }
                    
                    // Store for stats calculation
                    allProcessedDatasets.push({ label: comp.filename, data: processedComp });
                    
                    // Use comparison color
                    const color = comp.color || comparisonColors[idx % comparisonColors.length];
                    
                    baseDatasets.push({
                        label: 'Force (N) - ' + comp.filename,
                        data: processedComp.map(d => ({ x: d.time, y: convertAdcToForce(d.adcCode) })),
                        borderColor: color,
                        backgroundColor: color.replace('rgb', 'rgba').replace(')', ', 0.1)'),
                        yAxisID: 'y',
                        borderWidth: 1.5,
                        pointRadius: 0,
                        tension: 0.1,
                        borderDash: [5, 5] // Dashed line for comparison
                    });
                }
            });
            
            // Update chart
            dataChart.data.labels = allLabels;
            dataChart.data.datasets = baseDatasets;
            dataChart.update('none');
            updateChartLegend();
            
            // Calculate and display max values for all datasets (main + comparison)
            // Map dataset labels to their colors
            const datasetColors = {};
            baseDatasets.forEach((ds, idx) => {
                // Only track Force datasets (yAxisID === 'y')
                if (ds.yAxisID === 'y') {
                    let label = ds.label.replace('Force (N) - ', '');
                    // Map "Current" to match the dataset label
                    if (label === 'Current') {
                        label = 'Current';
                    }
                    datasetColors[label] = ds.borderColor;
                }
            });
            updateChartStats(allProcessedDatasets, datasetColors);
            
            const totalPoints = (rawData ? rawData.length : 0) + comparisonData.reduce((sum, comp) => sum + (comp.data ? comp.data.length : 0), 0);
            showStatus('✅ Chart updated: ' + baseDatasets.length + ' datasets, ' + totalPoints + ' total points', 'success');
        }
        
        // Update chart statistics (max force and max deceleration) for all datasets
        function updateChartStats(datasets, datasetColors) {
            if (!datasetColors) datasetColors = {};
            const maxForceEl = document.getElementById('maxForce');
            const maxDecelEl = document.getElementById('maxDecel');
            
            if (!datasets || datasets.length === 0) {
                if (maxForceEl) maxForceEl.innerHTML = '<span style="color: #718096;">-</span>';
                if (maxDecelEl) maxDecelEl.innerHTML = '<span style="color: #718096;">-</span>';
                return;
            }
            
            // Calculate stats for each dataset
            const stats = [];
            datasets.forEach(dataset => {
                if (!dataset.data || dataset.data.length === 0) return;
                
                // Find max ADC and convert to Force (N)
                let maxAdc = dataset.data[0].adcCode;
                let maxAdcTime = dataset.data[0].time;
                for (let i = 1; i < dataset.data.length; i++) {
                    if (dataset.data[i].adcCode > maxAdc) {
                        maxAdc = dataset.data[i].adcCode;
                        maxAdcTime = dataset.data[i].time;
                    }
                }
                const maxForceN = (maxAdc - calibrationOffset) * calibrationScale;
                
                // Find max deceleration (max AZ value)
                let maxDecel = null;
                let maxDecelTime = null;
                for (let i = 0; i < dataset.data.length; i++) {
                    if (dataset.data[i].az !== null && (maxDecel === null || dataset.data[i].az > maxDecel)) {
                        maxDecel = dataset.data[i].az;
                        maxDecelTime = dataset.data[i].time;
                    }
                }
                
                // Get color for this dataset
                const color = datasetColors[dataset.label] || 'rgb(75, 192, 192)';
                
                stats.push({
                    label: dataset.label,
                    maxForce: maxForceN,
                    maxForceTime: maxAdcTime,
                    maxDecel: maxDecel,
                    maxDecelTime: maxDecelTime,
                    color: color
                });
            });
            
            // Update display - show stats in color-coded containers
            if (maxForceEl) {
                maxForceEl.innerHTML = '';
                if (stats.length === 0) {
                    maxForceEl.innerHTML = '<span style="color: #718096;">-</span>';
                } else {
                    stats.forEach(stat => {
                        const badge = document.createElement('div');
                        badge.style.cssText = 'display: inline-block; padding: 6px 12px; margin: 4px 0; background: ' + stat.color.replace('rgb', 'rgba').replace(')', ', 0.15)') + '; border-left: 4px solid ' + stat.color + '; border-radius: 4px; font-size: 13px; color: #2d3748;';
                        badge.innerHTML = '<strong style="color: ' + stat.color + ';">' + stat.label + ':</strong> ' + stat.maxForce.toFixed(2) + ' N at ' + stat.maxForceTime.toFixed(6) + 's';
                        maxForceEl.appendChild(badge);
                    });
                }
            }
            
            if (maxDecelEl) {
                maxDecelEl.innerHTML = '';
                if (stats.length === 0) {
                    maxDecelEl.innerHTML = '<span style="color: #718096;">-</span>';
                } else {
                    stats.forEach(stat => {
                        const badge = document.createElement('div');
                        badge.style.cssText = 'display: inline-block; padding: 6px 12px; margin: 4px 0; background: ' + stat.color.replace('rgb', 'rgba').replace(')', ', 0.15)') + '; border-left: 4px solid ' + stat.color + '; border-radius: 4px; font-size: 13px; color: #2d3748;';
                        if (stat.maxDecel !== null) {
                            badge.innerHTML = '<strong style="color: ' + stat.color + ';">' + stat.label + ':</strong> ' + stat.maxDecel.toFixed(3) + 'g at ' + stat.maxDecelTime.toFixed(6) + 's';
                        } else {
                            badge.innerHTML = '<strong style="color: ' + stat.color + ';">' + stat.label + ':</strong> N/A';
                        }
                        maxDecelEl.appendChild(badge);
                    });
                }
            }
        }
        
        // Tab switching function
        function switchTab(tabName, element) {
            // Hide all tabs
            document.querySelectorAll('.tab-content').forEach(tab => {
                tab.classList.remove('active');
            });
            document.querySelectorAll('.tab').forEach(btn => {
                btn.classList.remove('active');
            });
            
            // Show selected tab
            document.getElementById('tab-' + tabName).classList.add('active');
            if (element) {
                element.classList.add('active');
            } else {
                // Fallback: find the button by text content
                document.querySelectorAll('.tab').forEach(btn => {
                    if (btn.textContent.includes(tabName === 'status' ? 'Status' : tabName === 'data' ? 'Data Visualization' : 'Help')) {
                        btn.classList.add('active');
                    }
                });
            }
            
            // Initialize tab content if needed
            if (tabName === 'data' && !dataChart) {
                initChart();
                setTimeout(loadLatestCsv, 100);
            } else if (tabName === 'help' && document.getElementById('neopixelPatterns').children.length === 0) {
                generateNeopixelPatterns();
            }
        }
        
        // File comparison functions
        async function showFileSelector() {
            const panel = document.getElementById('fileComparisonPanel');
            panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
            if (panel.style.display === 'block') {
                await loadFileList();
            }
        }
        
        async function loadFileList() {
            const container = document.getElementById('fileListContainer');
            container.innerHTML = '<p>Loading file list...</p>';
            
            try {
                const response = await fetch('/csv/list');
                availableFiles = await response.json();
                
                container.innerHTML = '';
                if (availableFiles.length === 0) {
                    container.innerHTML = '<p style="color: #718096;">No CSV files found.</p>';
                    return;
                }
                
                availableFiles.forEach((file, idx) => {
                    const fileDiv = document.createElement('div');
                    fileDiv.style.cssText = 'padding: 10px; margin: 5px 0; background: white; border-radius: 6px; border: 1px solid #e2e8f0; display: flex; justify-content: space-between; align-items: center;';
                    
                    const fileInfo = document.createElement('span');
                    fileInfo.textContent = file.filename + ' (' + (file.size / 1024).toFixed(1) + ' KB)';
                    
                    const addBtn = document.createElement('button');
                    addBtn.textContent = '➕ Add';
                    addBtn.className = 'secondary';
                    addBtn.style.cssText = 'padding: 6px 12px; font-size: 12px;';
                    addBtn.onclick = () => addFileToComparison(file.filename);
                    
                    // Check if already added
                    if (comparisonData.find(c => c.filename === file.filename)) {
                        addBtn.textContent = '✓ Added';
                        addBtn.disabled = true;
                        addBtn.style.opacity = '0.5';
                    }
                    
                    fileDiv.appendChild(fileInfo);
                    fileDiv.appendChild(addBtn);
                    container.appendChild(fileDiv);
                });
            } catch (error) {
                container.innerHTML = '<p style="color: #f56565;">Error loading file list: ' + error + '</p>';
            }
        }
        
        async function addFileToComparison(filename) {
            // Check if already added
            if (comparisonData.find(c => c.filename === filename)) {
                showStatus('File already added to comparison', 'info');
                return;
            }
            
            try {
                showStatus('Loading ' + filename + '...', 'info');
                const response = await fetch('/csv/file?file=' + encodeURIComponent(filename));
                if (!response.ok) throw new Error('Failed to load file');
                const csvText = await response.text();
                
                const parsedData = parseCSV(csvText);
                if (!parsedData || parsedData.length === 0) {
                    showStatus('Error: Invalid CSV file', 'error');
                    return;
                }
                
                const color = comparisonColors[comparisonData.length % comparisonColors.length];
                comparisonData.push({ filename, data: parsedData, color });
                
                updateComparisonDisplay();
                updateChart();
                showStatus('✅ Added ' + filename + ' to comparison', 'success');
            } catch (error) {
                showStatus('❌ Error loading file: ' + error, 'error');
            }
        }
        
        function updateComparisonDisplay() {
            const container = document.getElementById('selectedFiles');
            if (comparisonData.length === 0) {
                container.innerHTML = '<p style="color: #718096; font-size: 12px;">No files selected for comparison.</p>';
                return;
            }
            
            container.innerHTML = '<strong style="font-size: 12px; margin-bottom: 8px; display: block;">Selected Files (' + comparisonData.length + '):</strong>';
            comparisonData.forEach((comp, idx) => {
                const fileTag = document.createElement('div');
                fileTag.style.cssText = 'display: inline-block; padding: 6px 12px; margin: 4px; background: white; border-radius: 6px; border: 2px solid ' + comp.color + '; font-size: 12px;';
                fileTag.innerHTML = '<span style="display: inline-block; width: 12px; height: 12px; background: ' + comp.color + '; border-radius: 50%; margin-right: 6px;"></span>' + comp.filename + ' <button onclick="removeFromComparison(\'' + comp.filename + '\')" style="margin-left: 8px; background: #f56565; color: white; border: none; border-radius: 4px; padding: 2px 6px; cursor: pointer; font-size: 10px;">✕</button>';
                container.appendChild(fileTag);
            });
        }
        
        function removeFromComparison(filename) {
            comparisonData = comparisonData.filter(c => c.filename !== filename);
            updateComparisonDisplay();
            updateChart();
            loadFileList(); // Refresh to re-enable add button
        }
        
        function clearComparison() {
            comparisonData = [];
            updateComparisonDisplay();
            updateChart();
            loadFileList();
        }
        
        // Auto-refresh functions
        function toggleAutoRefresh() {
            const toggle = document.getElementById('autoRefreshToggle');
            const intervalSelect = document.getElementById('refreshInterval');
            autoRefreshEnabled = toggle.checked;
            
            intervalSelect.disabled = !autoRefreshEnabled;
            
            if (autoRefreshEnabled) {
                startAutoRefresh();
            } else {
                stopAutoRefresh();
            }
        }
        
        function updateAutoRefresh() {
            if (autoRefreshEnabled) {
                stopAutoRefresh();
                startAutoRefresh();
            }
        }
        
        function startAutoRefresh() {
            stopAutoRefresh(); // Clear any existing interval
            
            const interval = parseInt(document.getElementById('refreshInterval').value);
            const statusEl = document.getElementById('autoRefreshStatus');
            
            statusEl.textContent = 'Auto-refresh: ON (' + (interval / 1000) + 's)';
            statusEl.style.color = '#48bb78';
            
            autoRefreshInterval = setInterval(() => {
                updateStatusIndicators();
                if (rawData) {
                    reloadLatestCsv();
                }
            }, interval);
        }
        
        function stopAutoRefresh() {
            if (autoRefreshInterval) {
                clearInterval(autoRefreshInterval);
                autoRefreshInterval = null;
            }
            const statusEl = document.getElementById('autoRefreshStatus');
            statusEl.textContent = 'Auto-refresh: OFF';
            statusEl.style.color = '#718096';
        }
        
        // Load config on page load
        window.onload = function() {
            updateStatusIndicators();  // Initial update
            initChart();
            generateNeopixelPatterns();  // Generate help section
            loadCalibration();  // Load calibration values
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

// Load calibration values from NVS
static void loadCalibrationFromNVS()
{
    Preferences preferences;
    if (preferences.begin("calibration", true))  // Read-only mode
    {
        loadcellScale = preferences.getFloat("loadcellScale", LOADCELL_SCALING_FACTOR_N_PER_ADC);
        loadcellOffset = preferences.getUInt("loadcellOffset", LOADCELL_ADC_BASELINE);
        preferences.end();
        Serial.printf("[WEBCONFIG] Loaded calibration: scale=%.6f, offset=%u\n", loadcellScale, loadcellOffset);
    }
}

// Save calibration values to NVS
static void saveCalibrationToNVS()
{
    Preferences preferences;
    if (preferences.begin("calibration", false))  // Read-write mode
    {
        preferences.putFloat("loadcellScale", loadcellScale);
        preferences.putUInt("loadcellOffset", loadcellOffset);
        preferences.end();
        Serial.printf("[WEBCONFIG] Saved calibration: scale=%.6f, offset=%u\n", loadcellScale, loadcellOffset);
    }
}

// Save current logger configuration to NVS
static void saveConfigToNVS()
{
    Preferences preferences;
    if (preferences.begin("webconfig", false))  // Read-write mode
    {
        preferences.putUInt("adcSampleRate", currentConfig.adcSampleRate);
        preferences.putUInt("adcPgaGain", static_cast<uint8_t>(currentConfig.adcPgaGain));
        preferences.putUInt("imuOdr", currentConfig.imuOdr);
        preferences.putUShort("imuAccelRange", currentConfig.imuAccelRange);
        preferences.putUShort("imuGyroRange", currentConfig.imuGyroRange);
        preferences.end();
        Serial.println("[WEBCONFIG] Configuration saved to NVS");
    }
    else
    {
        Serial.println("[WEBCONFIG] WARNING: Failed to open NVS for writing");
    }
}

// Get calibration values as JSON
static void handleCalibrationGet()
{
    char json[256];
    snprintf(json, sizeof(json),
        "{\"scale\":%.6f,\"offset\":%u}",
        loadcellScale, loadcellOffset);
    server.send(200, "application/json", json);
}

// Handle calibration POST (save calibration values)
static void handleCalibrationPost()
{
    // Rate limiting: max 1 request per second
    static uint32_t lastCalRequest = 0;
    uint32_t now = millis();
    if (now - lastCalRequest < 1000)
    {
        server.send(429, "text/plain", "Too many requests. Please wait 1 second.");
        return;
    }
    lastCalRequest = now;
    
    // Parse parameters
    if (server.hasArg("loadcellScale"))
    {
        loadcellScale = server.arg("loadcellScale").toFloat();
        if (loadcellScale <= 0.0f || loadcellScale > 1000.0f)
        {
            server.send(400, "text/plain", "Invalid loadcell scale (must be 0.000001 to 1000)");
            return;
        }
    }
    if (server.hasArg("loadcellOffset"))
    {
        loadcellOffset = server.arg("loadcellOffset").toInt();
        if (loadcellOffset > 16777215)  // Max 24-bit value
        {
            server.send(400, "text/plain", "Invalid loadcell offset (must be 0 to 16777215)");
            return;
        }
    }
    
    // Save to NVS
    saveCalibrationToNVS();
    
    server.send(200, "text/plain", "OK");
    Serial.println("[WEBCONFIG] Calibration values updated");
}

// Handle ADC optimization request
static void handleAdcOptimize()
{
    // Rate limiting: max 1 request per 30 seconds (optimization takes time)
    static uint32_t lastOptRequest = 0;
    uint32_t now = millis();
    if (now - lastOptRequest < 30000)
    {
        server.send(429, "text/plain", "Too many requests. Please wait 30 seconds.");
        return;
    }
    lastOptRequest = now;
    
    // Get parameters (optional - use defaults if not provided)
    size_t samplesPerTest = 5000;  // Default: 5000 samples per test
    if (server.hasArg("samples"))
    {
        samplesPerTest = server.arg("samples").toInt();
        if (samplesPerTest < 100 || samplesPerTest > 50000)
        {
            server.send(400, "text/plain", "Invalid samples parameter (100-50000)");
            return;
        }
    }
    
    // Get optimization mode
    AdcOptimizationMode mode = ADC_OPT_MODE_NOISE_ONLY;
    int32_t baselineAdc = 0;
    if (server.hasArg("mode"))
    {
        int modeInt = server.arg("mode").toInt();
        if (modeInt == 1) mode = ADC_OPT_MODE_SNR_SINGLE;
        else if (modeInt == 2) mode = ADC_OPT_MODE_SNR_MULTIPOINT;
    }
    
    if (server.hasArg("baseline"))
    {
        baselineAdc = server.arg("baseline").toInt();
    }
    
    // For multi-point mode, we need to handle it differently (see separate endpoint)
    if (mode == ADC_OPT_MODE_SNR_MULTIPOINT)
    {
        server.send(400, "text/plain", "Use /cal/optimize-multipoint endpoint for multi-point optimization");
        return;
    }
    
    // Get search strategy (default: ADAPTIVE for speed)
    AdcSearchStrategy strategy = ADC_SEARCH_ADAPTIVE;
    if (server.hasArg("strategy"))
    {
        int strategyInt = server.arg("strategy").toInt();
        if (strategyInt == 0) strategy = ADC_SEARCH_EXHAUSTIVE;
        else if (strategyInt == 1) strategy = ADC_SEARCH_ADAPTIVE;
        else if (strategyInt == 2) strategy = ADC_SEARCH_GRADIENT;
    }
    
    // Progress callback for real-time updates (Optimization #3)
    static String lastProgressStatus = "";
    auto progressCallback = [](size_t current, size_t total, const char* status) {
        // Store progress for web endpoint (could use WebSocket in future)
        lastProgressStatus = String(status) + " (" + String(current) + "/" + String(total) + ")";
        Serial.printf("[ADC_OPT] Progress: %s\n", status);
    };
    
    // Perform optimization (adc.h already included at top of file)
    AdcOptimizationResult result;
    bool success = adcOptimizeSettings(
        mode,
        strategy,
        nullptr, 0,  // Use default gains (all 8)
        nullptr, 0,  // Use default rates (common values)
        samplesPerTest,
        baselineAdc,
        nullptr, 0,  // No load points for single-point modes
        progressCallback,
        result);
    
    if (!success || !result.success)
    {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Optimization failed\"}");
        return;
    }
    
    // Return result as JSON
    char json[256];
    snprintf(json, sizeof(json),
        "{\"success\":true,\"optimalGain\":%d,\"optimalSampleRate\":%lu,\"noiseLevel\":%.2f}",
        static_cast<int>(result.optimalGain),
        result.optimalSampleRate,
        result.noiseLevel);
    
    server.send(200, "application/json", json);
    
    // Update current config with optimal settings
    currentConfig.adcPgaGain = result.optimalGain;
    currentConfig.adcSampleRate = result.optimalSampleRate;
    
    // Save to NVS
    saveConfigToNVS();
    
    Serial.println("[WEBCONFIG] ADC optimization complete, settings updated");
    
    // Note: ADC sampling task was stopped during optimization and needs to be
    // restarted by the user (by starting a new logging session). This is intentional
    // to prevent interference during calibration.
}

// Handle multi-point ADC optimization
static void handleAdcOptimizeMultipoint()
{
    // Rate limiting: max 1 request per 30 seconds
    static uint32_t lastOptRequest = 0;
    uint32_t now = millis();
    if (now - lastOptRequest < 30000)
    {
        server.send(429, "text/plain", "Too many requests. Please wait 30 seconds.");
        return;
    }
    lastOptRequest = now;
    
    // Get parameters
    size_t samplesPerTest = 5000;
    if (server.hasArg("samples"))
    {
        samplesPerTest = server.arg("samples").toInt();
        if (samplesPerTest < 100 || samplesPerTest > 50000)
        {
            server.send(400, "text/plain", "Invalid samples parameter (100-50000)");
            return;
        }
    }
    
    // Parse load points from JSON body
    if (!server.hasArg("plain"))
    {
        server.send(400, "text/plain", "Missing load points data");
        return;
    }
    
    String jsonData = server.arg("plain");
    
    // Parse JSON (simple parsing - expecting array of load points)
    // Format: {"loadPoints":[{"baseline":8388608,"weight":0.2,"measured":true},...]}
    // For now, we'll use a simpler approach: parse from query parameters
    
    // Alternative: Use query parameters for each load point
    // ?baseline0=X&weight0=Y&baseline1=X&weight1=Y...
    const size_t MAX_LOAD_POINTS = 10;
    AdcLoadPoint loadPoints[MAX_LOAD_POINTS];
    size_t numLoadPoints = 0;
    
    // Parse load points from query parameters
    for (size_t i = 0; i < MAX_LOAD_POINTS; i++)
    {
        String baselineKey = "baseline" + String(i);
        String weightKey = "weight" + String(i);
        String measuredKey = "measured" + String(i);
        
        if (server.hasArg(baselineKey) && server.hasArg(weightKey))
        {
            loadPoints[numLoadPoints].baselineAdc = server.arg(baselineKey).toInt();
            loadPoints[numLoadPoints].weight = server.arg(weightKey).toFloat();
            loadPoints[numLoadPoints].measured = server.hasArg(measuredKey) ? 
                                                 (server.arg(measuredKey).toInt() != 0) : true;
            loadPoints[numLoadPoints].snrDb = 0.0f;
            loadPoints[numLoadPoints].signalRms = 0.0f;
            loadPoints[numLoadPoints].noiseRms = 0.0f;
            numLoadPoints++;
        }
    }
    
    if (numLoadPoints == 0)
    {
        server.send(400, "text/plain", "No load points provided");
        return;
    }
    
    // Get search strategy (default: ADAPTIVE)
    AdcSearchStrategy strategy = ADC_SEARCH_ADAPTIVE;
    if (server.hasArg("strategy"))
    {
        int strategyInt = server.arg("strategy").toInt();
        if (strategyInt == 0) strategy = ADC_SEARCH_EXHAUSTIVE;
        else if (strategyInt == 1) strategy = ADC_SEARCH_ADAPTIVE;
        else if (strategyInt == 2) strategy = ADC_SEARCH_GRADIENT;
    }
    
    // Validate load points (Optimization #9)
    const char *warnings[10];
    size_t numWarnings = 0;
    bool valid = adcValidateLoadPoints(loadPoints, numLoadPoints, warnings, 10, numWarnings);
    
    if (!valid && numWarnings > 0)
    {
        String errorMsg = "Load point validation failed: ";
        for (size_t i = 0; i < numWarnings && i < 3; i++)  // Limit to 3 warnings
        {
            if (i > 0) errorMsg += "; ";
            errorMsg += warnings[i];
        }
        server.send(400, "application/json", "{\"success\":false,\"error\":\"" + errorMsg + "\"}");
        return;
    }
    
    // Progress callback
    static String lastProgressStatus = "";
    auto progressCallback = [](size_t current, size_t total, const char* status) {
        lastProgressStatus = String(status) + " (" + String(current) + "/" + String(total) + ")";
        Serial.printf("[ADC_OPT] Progress: %s\n", status);
    };
    
    // Perform multi-point optimization
    AdcOptimizationResult result;
    bool success = adcOptimizeSettings(
        ADC_OPT_MODE_SNR_MULTIPOINT,
        strategy,
        nullptr, 0,  // Use default gains
        nullptr, 0,  // Use default rates
        samplesPerTest,
        0,  // Baseline not used in multi-point mode
        loadPoints,
        numLoadPoints,
        progressCallback,
        result);
    
    if (!success || !result.success)
    {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Optimization failed\"}");
        return;
    }
    
    // Return result as JSON with load point details
    String json = "{\"success\":true,\"optimalGain\":" + String(static_cast<int>(result.optimalGain)) +
                  ",\"optimalSampleRate\":" + String(result.optimalSampleRate) +
                  ",\"weightedSnrDb\":" + String(result.snrDb, 2) +
                  ",\"loadPoints\":[";
    
    for (size_t i = 0; i < numLoadPoints; i++)
    {
        if (i > 0) json += ",";
        json += "{\"baseline\":" + String(loadPoints[i].baselineAdc) +
                ",\"snrDb\":" + String(loadPoints[i].snrDb, 2) +
                ",\"signalRms\":" + String(loadPoints[i].signalRms, 2) +
                ",\"noiseRms\":" + String(loadPoints[i].noiseRms, 2) +
                ",\"weight\":" + String(loadPoints[i].weight, 2) + "}";
    }
    
    json += "]}";
    
    server.send(200, "application/json", json);
    
    // Update current config with optimal settings
    currentConfig.adcPgaGain = result.optimalGain;
    currentConfig.adcSampleRate = result.optimalSampleRate;
    
    // Save to NVS
    saveConfigToNVS();
    
    Serial.println("[WEBCONFIG] Multi-point ADC optimization complete, settings updated");
}

// Handle load point measurement (for multi-point optimization)
static void handleMeasureLoadPoint()
{
    // Rate limiting: max 10 requests per second
    static uint32_t lastRequest = 0;
    uint32_t now = millis();
    if (now - lastRequest < 100)
    {
        server.send(429, "text/plain", "Too many requests");
        return;
    }
    lastRequest = now;
    
    // Get parameters
    size_t numSamples = 5000;
    if (server.hasArg("samples"))
    {
        numSamples = server.arg("samples").toInt();
        if (numSamples < 100 || numSamples > 50000)
        {
            server.send(400, "text/plain", "Invalid samples parameter");
            return;
        }
    }
    
    int32_t baselineAdc = 0;
    bool autoDetect = false;
    
    if (server.hasArg("autodetect") && server.arg("autodetect").toInt() != 0)
    {
        // Optimization #8: Auto-detect load point
        autoDetect = true;
        int32_t previousAdc = 8388608;  // Default baseline
        if (server.hasArg("previous"))
        {
            previousAdc = server.arg("previous").toInt();
        }
        
        int32_t changeThreshold = 1000;  // Default 1000 ADC counts
        if (server.hasArg("changeThreshold"))
        {
            changeThreshold = server.arg("changeThreshold").toInt();
        }
        
        float stabilityThreshold = 100.0f;
        if (server.hasArg("stabilityThreshold"))
        {
            stabilityThreshold = server.arg("stabilityThreshold").toFloat();
        }
        
        // Auto-detect load point
        int32_t detectedAdc = 0;
        bool detected = adcAutoDetectLoadPoint(previousAdc, changeThreshold, 
                                              stabilityThreshold, detectedAdc, 30000);
        
        if (!detected)
        {
            server.send(408, "application/json", "{\"success\":false,\"error\":\"Load point detection timeout\"}");
            return;
        }
        
        baselineAdc = detectedAdc;
    }
    else
    {
        // Manual baseline
        if (server.hasArg("baseline"))
        {
            baselineAdc = server.arg("baseline").toInt();
        }
        else
        {
            server.send(400, "text/plain", "Missing baseline parameter");
            return;
        }
    }
    
    // Optimization #2: Auto-detect stability before measuring
    if (!autoDetect && server.hasArg("waitStable") && server.arg("waitStable").toInt() != 0)
    {
        int32_t stableAdc = 0;
        float stabilityThreshold = 100.0f;
        if (server.hasArg("stabilityThreshold"))
        {
            stabilityThreshold = server.arg("stabilityThreshold").toFloat();
        }
        
        // Wait for stability
        uint32_t startWait = millis();
        while (millis() - startWait < 10000)  // Max 10 seconds
        {
            if (adcCheckLoadStability(200, stabilityThreshold, stableAdc, 2000))
            {
                baselineAdc = stableAdc;  // Update to stable value
                break;
            }
            delay(100);
        }
    }
    
    // Measure SNR at current load
    float signalRms = 0.0f, noiseRms = 0.0f, snrDb = 0.0f;
    bool success = adcMeasureSnr(numSamples, baselineAdc, signalRms, noiseRms, snrDb, 10000);
    
    if (!success)
    {
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Measurement failed\"}");
        return;
    }
    
    // Return result as JSON
    char json[256];
    snprintf(json, sizeof(json),
        "{\"success\":true,\"snrDb\":%.2f,\"signalRms\":%.2f,\"noiseRms\":%.2f,\"baselineAdc\":%ld}",
        snrDb, signalRms, noiseRms, baselineAdc);
    
    server.send(200, "application/json", json);
}

// Handle calibration portal page (hidden route /cal)
static void handleCalibrationPage()
{
    const char* html = R"HTML_CAL(
<!DOCTYPE html>
<html>
<head>
    <title>Calibration Portal - Loadcell Datalogger</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { box-sizing: border-box; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0; 
            padding: 20px; 
            background: linear-gradient(135deg, #1a202c 0%, #2d3748 100%);
            min-height: 100vh;
        }
        .container { 
            max-width: 800px; 
            margin: 0 auto; 
            background: white; 
            padding: 30px; 
            border-radius: 12px; 
            box-shadow: 0 10px 40px rgba(0,0,0,0.3);
        }
        h1 { 
            color: #2d3748; 
            border-bottom: 3px solid #667eea; 
            padding-bottom: 15px; 
            margin-bottom: 25px;
        }
        .warning {
            background: #fff3cd;
            border: 2px solid #ffc107;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 25px;
            color: #856404;
        }
        .warning strong { display: block; margin-bottom: 8px; }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            font-weight: 600;
            color: #4a5568;
            margin-bottom: 8px;
        }
        input[type="number"] {
            width: 100%;
            padding: 12px;
            border: 2px solid #e2e8f0;
            border-radius: 6px;
            font-size: 16px;
            transition: border-color 0.2s;
        }
        input[type="number"]:focus {
            outline: none;
            border-color: #667eea;
        }
        .button {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 6px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .button:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
        }
        .button:active {
            transform: translateY(0);
        }
        .section {
            margin-bottom: 30px;
            padding: 20px;
            background: #f7fafc;
            border-radius: 8px;
        }
        .section h2 {
            color: #2d3748;
            margin-top: 0;
            margin-bottom: 15px;
        }
        .status {
            margin-top: 15px;
            padding: 12px;
            border-radius: 6px;
            font-weight: 500;
        }
        .status.success {
            background: #c6f6d5;
            color: #22543d;
        }
        .status.error {
            background: #fed7d7;
            color: #742a2a;
        }
        .status.info {
            background: #bee3f8;
            color: #2c5282;
        }
        .button-group {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
        }
        select {
            width: 100%;
            padding: 12px;
            border: 2px solid #e2e8f0;
            border-radius: 6px;
            font-size: 16px;
            transition: border-color 0.2s;
        }
        select:focus {
            outline: none;
            border-color: #667eea;
        }
        small {
            display: block;
            margin-top: 5px;
            font-size: 12px;
            color: #718096;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 Calibration Portal</h1>
        
        <div class="warning">
            <strong>⚠️ Internal Use Only</strong>
            This portal is for initial loadcell calibration only. Do not share this URL with end users.
        </div>
        
        <div class="section">
            <h2>Loadcell Calibration</h2>
            <form id="calForm">
                <div class="form-group">
                    <label for="loadcellScale">Scaling Factor (N per ADC count):</label>
                    <input type="number" id="loadcellScale" name="loadcellScale" step="0.000001" min="0.000001" max="1000" required>
                    <small style="color: #718096;">Formula: Force (N) = (ADC_Code - Offset) × Scale</small>
                </div>
                <div class="form-group">
                    <label for="loadcellOffset">ADC Baseline/Offset (24-bit value):</label>
                    <input type="number" id="loadcellOffset" name="loadcellOffset" min="0" max="16777215" required>
                    <small style="color: #718096;">ADC value that corresponds to 0N (zero force). Typically 8,388,608 for 24-bit mid-point.</small>
                </div>
            </form>
            <div id="calStatus"></div>
        </div>
        
        <div class="section">
            <h2>ADC Settings</h2>
            <form id="adcForm">
                <div class="form-group">
                    <label for="adcSampleRate">ADC Sample Rate (Hz)</label>
                    <input type="number" id="adcSampleRate" name="adcSampleRate" value="64000" min="1000" max="64000" step="1000">
                    <small>Range: 1,000 - 64,000 Hz</small>
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
            
            <div class="form-group" style="margin-top: 25px; padding-top: 20px; border-top: 2px solid #e2e8f0;">
                <h3 style="margin-top: 0; color: #2d3748; font-size: 18px;">🔍 Auto-Optimize ADC Settings</h3>
                <p style="color: #4a5568; margin-bottom: 15px;">
                    Automatically find the optimal PGA gain and sample rate combination with lowest noise.
                    <strong>Ensure loadcell is at ZERO FORCE (unloaded) before starting!</strong>
                </p>
                <div class="form-group">
                    <label for="optSamples">Samples per Test:</label>
                    <input type="number" id="optSamples" value="5000" min="1000" max="50000" step="1000">
                    <small>More samples = more accurate but slower (default: 5000)</small>
                </div>
                <button type="button" class="button" onclick="startOptimization()" id="optButton" style="background: linear-gradient(135deg, #48bb78 0%, #38a169 100%);">
                    🚀 Start Optimization
                </button>
                <div id="optStatus" style="margin-top: 15px;"></div>
                <div id="optProgress" style="margin-top: 10px; display: none;">
                    <div style="background: #e2e8f0; border-radius: 4px; height: 20px; overflow: hidden;">
                        <div id="optProgressBar" style="background: linear-gradient(90deg, #667eea, #764ba2); height: 100%; width: 0%; transition: width 0.3s;"></div>
                    </div>
                    <div id="optProgressText" style="margin-top: 8px; font-size: 12px; color: #718096;"></div>
                </div>
            </div>
            
            <div class="form-group" style="margin-top: 30px; padding-top: 25px; border-top: 2px solid #e2e8f0;">
                <h3 style="margin-top: 0; color: #2d3748; font-size: 18px;">📊 Multi-Point SNR Optimization (Recommended)</h3>
                <p style="color: #4a5568; margin-bottom: 20px;">
                    Optimize for maximum Signal-to-Noise Ratio across multiple load points. 
                    This provides better results than noise-only optimization.
                    <strong>Follow the 5-point loading sequence below.</strong>
                </p>
                
                <div class="form-group">
                    <label for="mpOptSamples">Samples per Test:</label>
                    <input type="number" id="mpOptSamples" value="5000" min="1000" max="50000" step="1000">
                    <small>More samples = more accurate but slower (default: 5000)</small>
                </div>
                
                <div style="background: #fff; border: 2px solid #e2e8f0; border-radius: 8px; padding: 20px; margin: 20px 0;">
                    <h4 style="margin-top: 0; color: #2d3748;">5-Point Measurement Sequence</h4>
                    <p style="color: #4a5568; font-size: 14px; margin-bottom: 15px;">
                        <strong>Loading Phase:</strong> Apply weights in increasing order (0N → 25% → 50% → 75% → 100%)<br>
                        <strong>Unloading Phase:</strong> Remove weights in decreasing order (100% → 75% → 50% → 25% → 0N)
                    </p>
                    
                    <div id="loadPointsContainer">
                        <!-- Load points will be generated here -->
                    </div>
                    
                    <div style="margin-top: 20px;">
                        <button type="button" class="button" onclick="startMultipointOptimization()" id="mpOptButton" style="background: linear-gradient(135deg, #48bb78 0%, #38a169 100%);">
                            🚀 Start Multi-Point Optimization
                        </button>
                        <button type="button" class="button" onclick="resetLoadPoints()" style="background: #e2e8f0; color: #4a5568; margin-left: 10px;">
                            🔄 Reset
                        </button>
                    </div>
                    
                    <div id="mpOptStatus" style="margin-top: 15px;"></div>
                    <div id="mpOptProgress" style="margin-top: 10px; display: none;">
                        <div style="background: #e2e8f0; border-radius: 4px; height: 20px; overflow: hidden;">
                            <div id="mpOptProgressBar" style="background: linear-gradient(90deg, #667eea, #764ba2); height: 100%; width: 0%; transition: width 0.3s;"></div>
                        </div>
                        <div id="mpOptProgressText" style="margin-top: 8px; font-size: 12px; color: #718096;"></div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="section">
            <h2>IMU Settings</h2>
            <form id="imuForm">
                <div class="form-group">
                    <label for="imuOdr">IMU Sample Rate (Hz)</label>
                    <input type="number" id="imuOdr" name="imuOdr" value="960" min="15" max="960" step="15">
                    <small>Range: 15 - 960 Hz</small>
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
            <div class="button-group" style="display: flex; gap: 10px; margin-top: 15px;">
                <button type="button" class="button" onclick="saveAllConfig()">💾 Save All Settings</button>
                <button type="button" class="button" style="background: #e2e8f0; color: #4a5568;" onclick="loadAllConfig()">📥 Load Current Config</button>
            </div>
            <div id="configStatus"></div>
        </div>
    </div>
    
    <script>
        // Load current calibration values
        async function loadCalibration() {
            try {
                const response = await fetch('/cal/values');
                const data = await response.json();
                document.getElementById('loadcellScale').value = data.scale;
                document.getElementById('loadcellOffset').value = data.offset;
            } catch (error) {
                console.error('Error loading calibration:', error);
            }
        }
        
        // Prevent form submission (use Save All Settings button instead)
        document.getElementById('calForm').addEventListener('submit', (e) => {
            e.preventDefault();
            saveAllConfig();
        });
        
        // Load all configuration (calibration + ADC/IMU)
        async function loadAllConfig() {
            await loadCalibration();
            await loadAdcImuConfig();
        }
        
        // Load ADC/IMU configuration
        async function loadAdcImuConfig() {
            try {
                const response = await fetch('/config');
                const config = await response.json();
                document.getElementById('adcSampleRate').value = config.adcSampleRate || 64000;
                document.getElementById('adcPgaGain').value = config.adcPgaGain || 2;
                document.getElementById('imuOdr').value = config.imuOdr || 960;
                document.getElementById('imuAccelRange').value = config.imuAccelRange || 16;
                document.getElementById('imuGyroRange').value = config.imuGyroRange || 2000;
            } catch (error) {
                console.error('Error loading ADC/IMU config:', error);
            }
        }
        
        // Save all configuration (calibration + ADC/IMU)
        async function saveAllConfig() {
            const statusDiv = document.getElementById('configStatus');
            statusDiv.className = 'status info';
            statusDiv.textContent = '💾 Saving all settings...';
            
            try {
                // Save calibration
                const calForm = document.getElementById('calForm');
                const calFormData = new FormData(calForm);
                const calParams = new URLSearchParams(calFormData);
                const calResponse = await fetch('/cal', {
                    method: 'POST',
                    body: calParams
                });
                
                if (!calResponse.ok) {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error saving calibration: ' + await calResponse.text();
                    return;
                }
                
                // Save ADC/IMU config
                const adcRate = parseInt(document.getElementById('adcSampleRate').value);
                const adcGain = parseInt(document.getElementById('adcPgaGain').value);
                const imuOdr = parseInt(document.getElementById('imuOdr').value);
                const imuAccel = parseInt(document.getElementById('imuAccelRange').value);
                const imuGyro = parseInt(document.getElementById('imuGyroRange').value);
                
                // Validate
                if (adcRate < 1000 || adcRate > 64000) {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error: ADC Sample Rate must be between 1,000 and 64,000 Hz';
                    return;
                }
                if (imuOdr < 15 || imuOdr > 960) {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error: IMU Sample Rate must be between 15 and 960 Hz';
                    return;
                }
                
                const configParams = new URLSearchParams({
                    adcSampleRate: adcRate,
                    adcPgaGain: adcGain,
                    imuOdr: imuOdr,
                    imuAccelRange: imuAccel,
                    imuGyroRange: imuGyro
                });
                
                const configResponse = await fetch('/config?' + configParams.toString(), {
                    method: 'POST'
                });
                
                if (configResponse.ok) {
                    statusDiv.className = 'status success';
                    statusDiv.textContent = '✅ All settings saved successfully!';
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Error saving ADC/IMU config: ' + await configResponse.text();
                }
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '❌ Error: ' + error;
            }
        }
        
        // ADC Optimization function
        async function startOptimization() {
            const button = document.getElementById('optButton');
            const statusDiv = document.getElementById('optStatus');
            const progressDiv = document.getElementById('optProgress');
            const progressBar = document.getElementById('optProgressBar');
            const progressText = document.getElementById('optProgressText');
            
            // Disable button
            button.disabled = true;
            button.textContent = '⏳ Optimizing...';
            
            // Show progress
            progressDiv.style.display = 'block';
            progressBar.style.width = '0%';
            progressText.textContent = 'Starting optimization...';
            
            statusDiv.className = 'status info';
            statusDiv.textContent = '🔍 Starting optimization. This may take 2-5 minutes. Please wait...';
            
            try {
                const samples = parseInt(document.getElementById('optSamples').value);
                const response = await fetch('/cal/optimize?samples=' + samples, {
                    method: 'POST'
                });
                
                if (!response.ok) {
                    throw new Error('Optimization failed: ' + response.statusText);
                }
                
                const result = await response.json();
                
                if (result.success) {
                    // Update form fields with optimal values
                    document.getElementById('adcSampleRate').value = result.optimalSampleRate;
                    document.getElementById('adcPgaGain').value = result.optimalGain;
                    
                    // Update progress
                    progressBar.style.width = '100%';
                    progressText.textContent = 'Complete!';
                    
                    statusDiv.className = 'status success';
                    statusDiv.innerHTML = '✅ Optimization complete!<br>' +
                        'Optimal Gain: x' + (1 << result.optimalGain) + '<br>' +
                        'Optimal Sample Rate: ' + result.optimalSampleRate + ' Hz<br>' +
                        'Noise Level: ' + result.noiseLevel.toFixed(2) + ' ADC counts<br><br>' +
                        '<strong>Settings have been updated. Click "Save All Settings" to persist.</strong>';
                } else {
                    throw new Error(result.error || 'Optimization failed');
                }
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '❌ Error: ' + error.message;
                progressBar.style.width = '0%';
                progressText.textContent = 'Failed';
            } finally {
                // Re-enable button
                button.disabled = false;
                button.textContent = '🚀 Start Optimization';
            }
        }
        
        // Load calibration on page load
        loadAllConfig();
    </script>
</body>
</html>
)HTML_CAL";
    
    server.send(200, "text/html", html);
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
    server.on("/cal/optimize", HTTP_POST, handleAdcOptimize);  // ADC optimization endpoint
    
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

