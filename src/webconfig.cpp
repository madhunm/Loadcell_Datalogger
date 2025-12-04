#include "webconfig.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "logger.h"
#include "adc.h"
#include "imu.h"
#include "sdcard.h"

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
// Using custom delimiter to avoid issues with parentheses in JavaScript
static const char* htmlPage = R"HTML_PAGE(
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
        .status-item { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #eee; }
        .status-label { font-weight: bold; color: #666; }
        .status-value { color: #333; }
        .status-value.status-ok { color: #28a745; font-weight: bold; }
        .status-value.status-warning { color: #ffc107; font-weight: bold; }
        .status-value.status-error { color: #dc3545; font-weight: bold; }
    </style>
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
        </div>

        <div class="section">
            <h2>System Status</h2>
            <div id="statusIndicators">
                <div class="status-item">
                    <span class="status-label">SD Card:</span>
                    <span id="sdStatus" class="status-value">Checking...</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Free Space:</span>
                    <span id="sdSpace" class="status-value">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">ADC Buffer:</span>
                    <span id="adcBuffer" class="status-value">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">IMU Buffer:</span>
                    <span id="imuBuffer" class="status-value">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Write Failures:</span>
                    <span id="writeFailures" class="status-value">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Logger State:</span>
                    <span id="loggerState" class="status-value">-</span>
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

    <script>
        // Update system status indicators
        async function updateStatusIndicators() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                
                // SD Card status
                const sdStatusEl = document.getElementById('sdStatus');
                if (data.sd && data.sd.mounted && data.sd.present) {
                    sdStatusEl.textContent = 'OK';
                    sdStatusEl.className = 'status-value status-ok';
                } else {
                    sdStatusEl.textContent = 'ERROR';
                    sdStatusEl.className = 'status-value status-error';
                }
                
                // SD Card free space
                const sdSpaceEl = document.getElementById('sdSpace');
                if (data.sd && data.sd.totalSpace > 0) {
                    const freeMB = (data.sd.freeSpace / (1024 * 1024)).toFixed(1);
                    const totalMB = (data.sd.totalSpace / (1024 * 1024)).toFixed(1);
                    const percent = data.sd.freePercent.toFixed(1);
                    sdSpaceEl.textContent = freeMB + ' MB / ' + totalMB + ' MB (' + percent + '%)';
                    if (data.sd.freePercent < 10) {
                        sdSpaceEl.className = 'status-value status-warning';
                    } else {
                        sdSpaceEl.className = 'status-value status-ok';
                    }
                } else {
                    sdSpaceEl.textContent = 'N/A';
                    sdSpaceEl.className = 'status-value';
                }
                
                // ADC Buffer
                const adcBufferEl = document.getElementById('adcBuffer');
                if (data.adc) {
                    const fillPercent = (data.adc.buffered / 2048 * 100).toFixed(1);
                    adcBufferEl.textContent = data.adc.buffered + ' (' + fillPercent + '%)';
                    if (fillPercent > 90) {
                        adcBufferEl.className = 'status-value status-error';
                    } else if (fillPercent > 75) {
                        adcBufferEl.className = 'status-value status-warning';
                    } else {
                        adcBufferEl.className = 'status-value status-ok';
                    }
                }
                
                // IMU Buffer
                const imuBufferEl = document.getElementById('imuBuffer');
                if (data.imu) {
                    const fillPercent = (data.imu.buffered / 1024 * 100).toFixed(1);
                    imuBufferEl.textContent = data.imu.buffered + ' (' + fillPercent + '%)';
                    if (fillPercent > 90) {
                        imuBufferEl.className = 'status-value status-error';
                    } else if (fillPercent > 75) {
                        imuBufferEl.className = 'status-value status-warning';
                    } else {
                        imuBufferEl.className = 'status-value status-ok';
                    }
                }
                
                // Write Failures
                const writeFailuresEl = document.getElementById('writeFailures');
                if (data.writes) {
                    const totalFailures = data.writes.adcFailures + data.writes.imuFailures;
                    const consecutive = Math.max(data.writes.adcConsecutiveFailures, data.writes.imuConsecutiveFailures);
                    writeFailuresEl.textContent = 'Total: ' + totalFailures + ', Consecutive: ' + consecutive;
                    if (consecutive >= 5) {
                        writeFailuresEl.className = 'status-value status-error';
                    } else if (consecutive > 0) {
                        writeFailuresEl.className = 'status-value status-warning';
                    } else {
                        writeFailuresEl.className = 'status-value status-ok';
                    }
                }
                
                // Logger State
                const loggerStateEl = document.getElementById('loggerState');
                const states = ['IDLE', 'SESSION_OPEN', 'CONVERTING'];
                loggerStateEl.textContent = states[data.logger.state] || 'UNKNOWN';
                if (data.logger.sessionOpen) {
                    loggerStateEl.className = 'status-value status-ok';
                } else {
                    loggerStateEl.className = 'status-value';
                }
            } catch (error) {
                console.error('Error updating status indicators:', error);
            }
        }
        
        // Update status indicators every 2 seconds
        setInterval(updateStatusIndicators, 2000);
        
        // Load config on page load
        window.onload = function() {
            loadConfig();
            updateStatusIndicators();  // Initial update
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

bool webConfigInit()
{
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

