/**
 * @file admin_webui.cpp
 * @brief Implementation of admin web interface
 */

#include "admin_webui.h"

// Static pointers for request handlers
static CalibrationStorage* g_storage = nullptr;
static CalibrationInterp* g_interp = nullptr;
static MAX11270Driver* g_adc_driver = nullptr;

bool AdminWebUI::begin(CalibrationStorage* cal_storage,
                       CalibrationInterp* cal_interp,
                       MAX11270Driver* adc) {
    storage = cal_storage;
    interp = cal_interp;
    adc_driver = adc;
    server_running = false;
    
    // Set global pointers for handlers
    g_storage = cal_storage;
    g_interp = cal_interp;
    g_adc_driver = adc;
    
    server = new AsyncWebServer(80);
    setupRoutes();
    
    return true;
}

void AdminWebUI::setupRoutes() {
    // Serve simple HTML page
    server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Loadcell Logger Admin</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }
        h2 { color: #555; margin-top: 20px; }
        .api-doc { background: #f9f9f9; padding: 15px; border-left: 4px solid #007bff; margin: 10px 0; border-radius: 4px; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; font-family: monospace; }
        .method { font-weight: bold; color: #28a745; }
        .method.post { color: #007bff; }
        .method.put { color: #fd7e14; }
        .method.delete { color: #dc3545; }
        .status { background: #e7f3ff; padding: 15px; border-radius: 4px; margin: 15px 0; }
        button { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; margin: 5px; }
        button:hover { background: #0056b3; }
        #status-output { font-family: monospace; white-space: pre-wrap; background: #333; color: #0f0; padding: 15px; border-radius: 4px; margin-top: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Loadcell Data Logger</h1>
        <p>Admin interface for calibration management</p>
        
        <div class="status">
            <h3>Quick Actions</h3>
            <button onclick="fetchStatus()">Get Status</button>
            <button onclick="fetchLoadcells()">List Loadcells</button>
            <button onclick="fetchActive()">Get Active</button>
            <div id="status-output"></div>
        </div>
        
        <h2>API Endpoints</h2>
        
        <div class="api-doc">
            <span class="method">GET</span> <code>/api/loadcells</code><br>
            List all stored loadcells
        </div>
        
        <div class="api-doc">
            <span class="method">GET</span> <code>/api/loadcells/{id}</code><br>
            Get specific loadcell calibration data
        </div>
        
        <div class="api-doc">
            <span class="method post">POST</span> <code>/api/loadcells</code><br>
            Create new loadcell (JSON body required)
        </div>
        
        <div class="api-doc">
            <span class="method put">PUT</span> <code>/api/loadcells/{id}</code><br>
            Update loadcell calibration (JSON body required)
        </div>
        
        <div class="api-doc">
            <span class="method delete">DELETE</span> <code>/api/loadcells/{id}</code><br>
            Delete loadcell calibration
        </div>
        
        <div class="api-doc">
            <span class="method">GET</span> <code>/api/active</code><br>
            Get currently active loadcell ID
        </div>
        
        <div class="api-doc">
            <span class="method put">PUT</span> <code>/api/active</code><br>
            Set active loadcell (JSON: {"id": "..."})
        </div>
        
        <div class="api-doc">
            <span class="method">GET</span> <code>/api/status</code><br>
            Get system status and live ADC reading
        </div>
        
        <h2>Example: Add Calibration</h2>
        <div class="api-doc">
            <code>curl -X POST http://192.168.4.1/api/loadcells -H "Content-Type: application/json" -d '{"id":"LC-001","model":"S-Beam","serial":"001","capacity_kg":1000,"excitation_V":10,"points":[{"load_kg":0,"output_uV":0},{"load_kg":500,"output_uV":1000},{"load_kg":1000,"output_uV":2000}]}'</code>
        </div>
    </div>
    
    <script>
        function fetchStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(d => document.getElementById('status-output').textContent = JSON.stringify(d, null, 2))
                .catch(e => document.getElementById('status-output').textContent = 'Error: ' + e);
        }
        function fetchLoadcells() {
            fetch('/api/loadcells')
                .then(r => r.json())
                .then(d => document.getElementById('status-output').textContent = JSON.stringify(d, null, 2))
                .catch(e => document.getElementById('status-output').textContent = 'Error: ' + e);
        }
        function fetchActive() {
            fetch('/api/active')
                .then(r => r.json())
                .then(d => document.getElementById('status-output').textContent = JSON.stringify(d, null, 2))
                .catch(e => document.getElementById('status-output').textContent = 'Error: ' + e);
        }
    </script>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", html);
    });
    
    // API: GET /api/loadcells
    server->on("/api/loadcells", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetLoadcells(request);
    });
    
    // API: POST /api/loadcells - Handle JSON body
    server->on("/api/loadcells", HTTP_POST, 
        [](AsyncWebServerRequest* request) {
            // This is called after body is received
        },
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            // Collect body data
            if (index == 0) {
                request->_tempObject = malloc(total + 1);
            }
            memcpy((uint8_t*)request->_tempObject + index, data, len);
            
            if (index + len == total) {
                ((char*)request->_tempObject)[total] = '\0';
                
                // Parse JSON
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, (char*)request->_tempObject);
                free(request->_tempObject);
                request->_tempObject = nullptr;
                
                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }
                
                LoadcellCalibration cal;
                JsonObject obj = doc.as<JsonObject>();
                
                if (!obj.containsKey("id")) {
                    request->send(400, "application/json", "{\"error\":\"Missing id field\"}");
                    return;
                }
                
                strncpy(cal.id, obj["id"].as<const char*>(), LOADCELL_ID_LEN - 1);
                strncpy(cal.model, obj["model"] | "", LOADCELL_MODEL_LEN - 1);
                strncpy(cal.serial, obj["serial"] | "", LOADCELL_SERIAL_LEN - 1);
                cal.capacity_kg = obj["capacity_kg"] | 0.0f;
                cal.excitation_V = obj["excitation_V"] | 0.0f;
                cal.sensitivity_mVV = obj["sensitivity_mVV"] | 0.0f;
                
                JsonArray points = obj["points"];
                cal.num_points = min((int)points.size(), MAX_CALIBRATION_POINTS);
                
                for (uint8_t i = 0; i < cal.num_points; i++) {
                    cal.points[i].load_kg = points[i]["load_kg"];
                    cal.points[i].output_uV = points[i]["output_uV"];
                }
                
                if (g_storage->saveLoadcell(cal)) {
                    request->send(201, "application/json", "{\"status\":\"created\"}");
                } else {
                    request->send(500, "application/json", "{\"error\":\"Failed to save\"}");
                }
            }
        }
    );
    
    // API: PUT /api/active - Set active loadcell
    server->on("/api/active", HTTP_PUT,
        [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                request->_tempObject = malloc(total + 1);
            }
            memcpy((uint8_t*)request->_tempObject + index, data, len);
            
            if (index + len == total) {
                ((char*)request->_tempObject)[total] = '\0';
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, (char*)request->_tempObject);
                free(request->_tempObject);
                request->_tempObject = nullptr;
                
                if (error || !doc.containsKey("id")) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON or missing id\"}");
                    return;
                }
                
                const char* id = doc["id"];
                if (g_storage->setActiveLoadcellId(id)) {
                    // Also update the interpolator
                    LoadcellCalibration cal;
                    if (g_storage->getLoadcellById(id, cal)) {
                        g_interp->setCalibration(cal);
                    }
                    request->send(200, "application/json", "{\"status\":\"active set\"}");
                } else {
                    request->send(404, "application/json", "{\"error\":\"Loadcell not found\"}");
                }
            }
        }
    );
    
    // API: GET /api/active
    server->on("/api/active", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetActive(request);
    });
    
    // API: GET /api/status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });
    
    // Handle 404
    server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "application/json", "{\"error\":\"Not found\"}");
    });
    
    // Enable CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
}

bool AdminWebUI::start(uint16_t port) {
    server->begin();
    server_running = true;
    Serial.printf("WebUI: Server started on port %u\n", port);
    return true;
}

bool AdminWebUI::stop() {
    server->end();
    server_running = false;
    Serial.println("WebUI: Server stopped");
    return true;
}

void AdminWebUI::calibrationToJson(const LoadcellCalibration& cal, JsonObject& obj) {
    obj["id"] = cal.id;
    obj["model"] = cal.model;
    obj["serial"] = cal.serial;
    obj["capacity_kg"] = cal.capacity_kg;
    obj["excitation_V"] = cal.excitation_V;
    obj["sensitivity_mVV"] = cal.sensitivity_mVV;
    
    JsonArray points = obj["points"].to<JsonArray>();
    for (uint8_t i = 0; i < cal.num_points; i++) {
        JsonObject pt = points.add<JsonObject>();
        pt["load_kg"] = cal.points[i].load_kg;
        pt["output_uV"] = cal.points[i].output_uV;
    }
}

bool AdminWebUI::jsonToCalibration(const JsonObject& obj, LoadcellCalibration& cal) {
    if (!obj.containsKey("id")) return false;
    
    strncpy(cal.id, obj["id"].as<const char*>(), LOADCELL_ID_LEN - 1);
    strncpy(cal.model, obj["model"] | "", LOADCELL_MODEL_LEN - 1);
    strncpy(cal.serial, obj["serial"] | "", LOADCELL_SERIAL_LEN - 1);
    
    cal.capacity_kg = obj["capacity_kg"] | 0.0f;
    cal.excitation_V = obj["excitation_V"] | 0.0f;
    cal.sensitivity_mVV = obj["sensitivity_mVV"] | 0.0f;
    
    JsonArray points = obj["points"];
    cal.num_points = min((int)points.size(), MAX_CALIBRATION_POINTS);
    
    for (uint8_t i = 0; i < cal.num_points; i++) {
        cal.points[i].load_kg = points[i]["load_kg"];
        cal.points[i].output_uV = points[i]["output_uV"];
    }
    
    return true;
}

void AdminWebUI::handleGetLoadcells(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    
    uint8_t count = storage->getLoadcellCount();
    for (uint8_t i = 0; i < count; i++) {
        LoadcellCalibration cal;
        if (storage->getLoadcellByIndex(i, cal)) {
            JsonObject obj = array.add<JsonObject>();
            calibrationToJson(cal, obj);
        }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void AdminWebUI::handleGetLoadcell(AsyncWebServerRequest* request) {
    // This would need path parameter parsing - simplified version
    request->send(501, "application/json", "{\"error\":\"Use /api/loadcells to list all\"}");
}

void AdminWebUI::handleDeleteLoadcell(AsyncWebServerRequest* request) {
    // This would need path parameter parsing - simplified version
    request->send(501, "application/json", "{\"error\":\"Not implemented\"}");
}

void AdminWebUI::handleGetActive(AsyncWebServerRequest* request) {
    char active_id[LOADCELL_ID_LEN];
    
    if (storage->getActiveLoadcellId(active_id)) {
        JsonDocument doc;
        doc["id"] = active_id;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(404, "application/json", "{\"error\":\"No active loadcell\"}");
    }
}

void AdminWebUI::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    // Get active loadcell
    char active_id[LOADCELL_ID_LEN];
    if (storage->getActiveLoadcellId(active_id)) {
        doc["active_loadcell"] = active_id;
    }
    
    // Get live ADC reading if available
    if (adc_driver) {
        int32_t raw;
        if (adc_driver->readRaw(raw)) {
            doc["adc_raw"] = raw;
            
            float uV = adc_driver->rawToMicrovolts(raw);
            doc["adc_uV"] = uV;
            
            if (interp->isCalibrated()) {
                float load_kg = interp->convertToKg(uV);
                doc["load_kg"] = load_kg;
            }
        }
    }
    
    doc["loadcell_count"] = storage->getLoadcellCount();
    doc["heap_free"] = ESP.getFreeHeap();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
