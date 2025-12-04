#include "webconfig.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "logger.h"
#include "adc.h"
#include "imu.h"

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
static const char* htmlPage = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Loadcell Datalogger Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
        .section { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }
        .section h2 { margin-top: 0; color: #555; }
        label { display: block; margin: 10px 0 5px 0; font-weight: bold; color: #666; }
        input, select { width: 100%; padding: 8px; margin-bottom: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { background: #4CAF50; color: white; padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 10px 5px 0 0; }
        button:hover { background: #45a049; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
        #chartContainer { margin-top: 20px; height: 400px; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@3.9.1/dist/chart.min.js"></script>
</head>
<body>
    <div class="container">
        <h1>Loadcell Datalogger Configuration</h1>
        
        <div class="section">
            <h2>ADC Settings</h2>
            <form id="adcForm">
                <label for="adcSampleRate">ADC Sample Rate (Hz):</label>
                <input type="number" id="adcSampleRate" name="adcSampleRate" value="64000" min="1000" max="64000" step="1000">
                
                <label for="adcPgaGain">PGA Gain:</label>
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
            </form>
        </div>

        <div class="section">
            <h2>IMU Settings</h2>
            <form id="imuForm">
                <label for="imuOdr">IMU Sample Rate (Hz):</label>
                <input type="number" id="imuOdr" name="imuOdr" value="960" min="15" max="960" step="15">
                
                <label for="imuAccelRange">Accelerometer Range (±g):</label>
                <select id="imuAccelRange" name="imuAccelRange">
                    <option value="2">±2g</option>
                    <option value="4">±4g</option>
                    <option value="8">±8g</option>
                    <option value="16" selected>±16g</option>
                </select>
                
                <label for="imuGyroRange">Gyroscope Range (dps):</label>
                <select id="imuGyroRange" name="imuGyroRange">
                    <option value="125">±125 dps</option>
                    <option value="250">±250 dps</option>
                    <option value="500">±500 dps</option>
                    <option value="1000">±1000 dps</option>
                    <option value="2000" selected>±2000 dps</option>
                </select>
            </form>
        </div>

        <div class="section">
            <button onclick="saveConfig()">Save Configuration</button>
            <button onclick="loadConfig()">Load Current Config</button>
            <button onclick="toggleChart()">Toggle Live Chart</button>
        </div>

        <div id="status"></div>

        <div id="chartSection" style="display: none;">
            <div class="section">
                <h2>Live Data Chart</h2>
                <canvas id="chartContainer"></canvas>
            </div>
        </div>
    </div>

    <script>
        let chart = null;
        let chartVisible = false;
        let dataUpdateInterval = null;

        function showStatus(message, type) {
            const statusDiv = document.getElementById('status');
            statusDiv.className = 'status ' + type;
            statusDiv.textContent = message;
            setTimeout(() => statusDiv.textContent = '', 3000);
        }

        async function saveConfig() {
            const adcRate = document.getElementById('adcSampleRate').value;
            const adcGain = document.getElementById('adcPgaGain').value;
            const imuOdr = document.getElementById('imuOdr').value;
            const imuAccel = document.getElementById('imuAccelRange').value;
            const imuGyro = document.getElementById('imuGyroRange').value;

            const params = new URLSearchParams({
                adcSampleRate: adcRate,
                adcPgaGain: adcGain,
                imuOdr: imuOdr,
                imuAccelRange: imuAccel,
                imuGyroRange: imuGyro
            });

            try {
                const response = await fetch('/config?' + params.toString(), { method: 'POST' });
                const text = await response.text();
                showStatus('Configuration saved successfully!', 'success');
            } catch (error) {
                showStatus('Error saving configuration: ' + error, 'error');
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
                
                showStatus('Configuration loaded!', 'info');
            } catch (error) {
                showStatus('Error loading configuration: ' + error, 'error');
            }
        }

        function toggleChart() {
            chartVisible = !chartVisible;
            const chartSection = document.getElementById('chartSection');
            
            if (chartVisible) {
                chartSection.style.display = 'block';
                initChart();
                startDataUpdates();
            } else {
                chartSection.style.display = 'none';
                stopDataUpdates();
                if (chart) {
                    chart.destroy();
                    chart = null;
                }
            }
        }

        function initChart() {
            const ctx = document.getElementById('chartContainer').getContext('2d');
            chart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        { label: 'ADC Code', data: [], borderColor: 'rgb(75, 192, 192)', backgroundColor: 'rgba(75, 192, 192, 0.2)', yAxisID: 'y' },
                        { label: 'AX (g)', data: [], borderColor: 'rgb(255, 99, 132)', backgroundColor: 'rgba(255, 99, 132, 0.2)', yAxisID: 'y1', hidden: true },
                        { label: 'AY (g)', data: [], borderColor: 'rgb(54, 162, 235)', backgroundColor: 'rgba(54, 162, 235, 0.2)', yAxisID: 'y1', hidden: true },
                        { label: 'AZ (g)', data: [], borderColor: 'rgb(255, 206, 86)', backgroundColor: 'rgba(255, 206, 86, 0.2)', yAxisID: 'y1', hidden: true }
                    ]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        y: { position: 'left', title: { display: true, text: 'ADC Code' } },
                        y1: { position: 'right', title: { display: true, text: 'Acceleration (g)' }, grid: { drawOnChartArea: false } }
                    },
                    animation: { duration: 0 },
                    elements: { point: { radius: 0 } }
                }
            });
        }

        async function updateChart() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                if (!chart) return;
                
                const maxPoints = 100;
                const time = new Date().toLocaleTimeString();
                
                // Add ADC data
                chart.data.datasets[0].data.push({x: time, y: data.adcCode || 0});
                if (chart.data.datasets[0].data.length > maxPoints) {
                    chart.data.datasets[0].data.shift();
                }
                
                // Add IMU data if available
                if (data.imu) {
                    chart.data.datasets[1].data.push({x: time, y: data.imu.ax || 0});
                    chart.data.datasets[2].data.push({x: time, y: data.imu.ay || 0});
                    chart.data.datasets[3].data.push({x: time, y: data.imu.az || 0});
                    
                    chart.data.datasets[1].data = chart.data.datasets[1].data.slice(-maxPoints);
                    chart.data.datasets[2].data = chart.data.datasets[2].data.slice(-maxPoints);
                    chart.data.datasets[3].data = chart.data.datasets[3].data.slice(-maxPoints);
                }
                
                chart.data.labels.push(time);
                if (chart.data.labels.length > maxPoints) {
                    chart.data.labels.shift();
                }
                
                chart.update('none');
            } catch (error) {
                console.error('Error updating chart:', error);
            }
        }

        function startDataUpdates() {
            dataUpdateInterval = setInterval(updateChart, 100); // Update every 100ms
        }

        function stopDataUpdates() {
            if (dataUpdateInterval) {
                clearInterval(dataUpdateInterval);
                dataUpdateInterval = null;
            }
        }

        // Load config on page load
        window.onload = function() {
            loadConfig();
        };
    </script>
</body>
</html>
)";

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

// Handle configuration POST (save new config)
static void handleConfigPost()
{
    if (server.hasArg("adcSampleRate"))
    {
        currentConfig.adcSampleRate = server.arg("adcSampleRate").toInt();
    }
    if (server.hasArg("adcPgaGain"))
    {
        uint8_t gainCode = server.arg("adcPgaGain").toInt();
        if (gainCode <= 7)
        {
            currentConfig.adcPgaGain = static_cast<AdcPgaGain>(gainCode);
        }
    }
    if (server.hasArg("imuOdr"))
    {
        currentConfig.imuOdr = server.arg("imuOdr").toInt();
    }
    if (server.hasArg("imuAccelRange"))
    {
        currentConfig.imuAccelRange = server.arg("imuAccelRange").toInt();
    }
    if (server.hasArg("imuGyroRange"))
    {
        currentConfig.imuGyroRange = server.arg("imuGyroRange").toInt();
    }
    
    server.send(200, "text/plain", "OK");
    Serial.println("[WEBCONFIG] Configuration updated via web interface");
}

// Handle data endpoint (for charting)
// Note: This reads samples directly from sensors to avoid interfering with logging buffer
static void handleData()
{
    String json = "{";
    
    // Read ADC directly (if data is ready)
    int32_t adcCode = 0;
    uint32_t adcIndex = adcGetSampleCounter();
    if (adcIsDataReady())
    {
        adcReadSample(adcCode);
    }
    json += "\"adcCode\":" + String(adcCode) + ",";
    json += "\"adcIndex\":" + String(adcIndex);
    
    // Read IMU directly (non-blocking)
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    if (imuRead(ax, ay, az, gx, gy, gz))
    {
        json += ",\"imu\":{";
        json += "\"ax\":" + String(ax, 3) + ",";
        json += "\"ay\":" + String(ay, 3) + ",";
        json += "\"az\":" + String(az, 3) + ",";
        json += "\"gx\":" + String(gx, 3) + ",";
        json += "\"gy\":" + String(gy, 3) + ",";
        json += "\"gz\":" + String(gz, 3);
        json += "}";
    }
    
    json += "}";
    
    server.send(200, "application/json", json);
}

bool webConfigInit()
{
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
    server.on("/data", handleData);
    
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

