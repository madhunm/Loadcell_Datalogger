/**
 * @file admin_webui.h
 * @brief Admin web interface with REST API for calibration management
 */

#ifndef ADMIN_WEBUI_H
#define ADMIN_WEBUI_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../calibration/calibration_storage.h"
#include "../calibration/calibration_interp.h"
#include "../drivers/max11270_driver.h"

/**
 * @brief Admin WebUI with RESTful API for loadcell management
 */
class AdminWebUI {
public:
    /**
     * @brief Initialize the web server
     * @param cal_storage Pointer to calibration storage
     * @param cal_interp Pointer to calibration interpolator
     * @param adc Pointer to ADC driver (for live preview)
     * @return true if successful
     */
    bool begin(CalibrationStorage* cal_storage, 
               CalibrationInterp* cal_interp,
               MAX11270Driver* adc);
    
    /**
     * @brief Start the web server
     * @param port Port number (default: 80)
     * @return true if successful
     */
    bool start(uint16_t port = 80);
    
    /**
     * @brief Stop the web server
     * @return true if successful
     */
    bool stop();
    
    /**
     * @brief Check if server is running
     * @return true if active
     */
    bool isRunning() const { return server_running; }
    
private:
    AsyncWebServer* server;
    CalibrationStorage* storage;
    CalibrationInterp* interp;
    MAX11270Driver* adc_driver;
    bool server_running;
    
    /**
     * @brief Setup API routes
     */
    void setupRoutes();
    
    /**
     * @brief API: GET /api/loadcells - List all loadcells
     */
    void handleGetLoadcells(AsyncWebServerRequest* request);
    
    /**
     * @brief API: GET /api/loadcells/{id} - Get specific loadcell
     */
    void handleGetLoadcell(AsyncWebServerRequest* request);
    
    /**
     * @brief API: DELETE /api/loadcells/{id} - Delete loadcell
     */
    void handleDeleteLoadcell(AsyncWebServerRequest* request);
    
    /**
     * @brief API: GET /api/active - Get active loadcell ID
     */
    void handleGetActive(AsyncWebServerRequest* request);
    
    /**
     * @brief API: GET /api/status - Get system status
     */
    void handleGetStatus(AsyncWebServerRequest* request);
    
    /**
     * @brief Convert LoadcellCalibration to JSON
     */
    void calibrationToJson(const LoadcellCalibration& cal, JsonObject& obj);
    
    /**
     * @brief Parse JSON to LoadcellCalibration
     */
    bool jsonToCalibration(const JsonObject& obj, LoadcellCalibration& cal);
};

#endif // ADMIN_WEBUI_H
