/**
 * @file max11270.cpp
 * @brief MAX11270 24-bit Delta-Sigma ADC Driver Implementation
 * 
 * DMA-based zero sample loss implementation:
 * - DRDY interrupt triggers DMA SPI transfer
 * - Hardware handles data transfer without CPU blocking
 * - Post-transaction callback stores data to ring buffer
 * - ISR duration reduced to ~1-2µs (from ~8µs)
 */

#include "max11270.h"
#include "../pin_config.h"
#include <atomic>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include "esp_timer.h"

namespace MAX11270 {

// ============================================================================
// Private State
// ============================================================================

namespace {
    // ESP-IDF SPI handle
    spi_device_handle_t spiDevice = nullptr;
    spi_bus_config_t busConfig = {};
    spi_device_interface_config_t devConfig = {};
    
    // DMA transaction buffers (must be DMA-capable, 32-bit aligned)
    // We use a pool of transactions for continuous mode
    static constexpr size_t DMA_TRANS_POOL_SIZE = 4;  // Allow queuing multiple transactions
    
    // Transaction structure with sample data
    struct DmaTransaction {
        spi_transaction_t trans;
        uint8_t txData[4] __attribute__((aligned(4)));  // Command byte + padding
        uint8_t rxData[4] __attribute__((aligned(4)));  // Received data (3 bytes + padding)
        uint32_t timestamp_us;
        bool inUse;
    };
    
    // Pool of pre-allocated DMA transactions
    DmaTransaction dmaTransPool[DMA_TRANS_POOL_SIZE];
    std::atomic<int> nextTransIndex{0};
    
    // Current configuration
    Config currentConfig;
    
    // Continuous mode state
    volatile bool continuousRunning = false;
    volatile bool dmaInProgress = false;
    ADCRingBufferLarge* ringBuffer = nullptr;
    
    // Overflow detection (ZERO LOSS POLICY)
    volatile bool overflowFlag = false;
    std::atomic<uint32_t> overflowCount{0};
    
    // Statistics (using atomics for ISR safety)
    std::atomic<uint32_t> samplesAcquired{0};
    std::atomic<uint32_t> drdyTimeouts{0};
    std::atomic<uint32_t> spiErrors{0};
    std::atomic<uint32_t> dmaQueueFull{0};
    std::atomic<uint32_t> isrFiredCount{0};  // Debug: count ISR invocations
    volatile uint32_t maxLatencyUs = 0;
    volatile uint32_t lastTimestampUs = 0;
    volatile uint32_t lastDrdyTimeUs = 0;
    
    // Reference voltage (internal 2.5V)
    constexpr float VREF = 2.5f;
    constexpr int32_t ADC_MAX = (1 << 23) - 1;  // 8388607
    
    // Read command for DATA register
    // Command format: 1 1 0 0 | REG[3:0] << 1 | 1 (read)
    static constexpr uint8_t READ_DATA_CMD = 0xC0 | (0x06 << 1) | 0x01;  // 0xCD
    
    // ========================================================================
    // DMA Transaction Pool Management
    // ========================================================================
    
    /**
     * @brief Get a free transaction from the pool (ISR safe)
     */
    DmaTransaction* IRAM_ATTR getFreeTrans() {
        for (int i = 0; i < DMA_TRANS_POOL_SIZE; i++) {
            int idx = (nextTransIndex.load(std::memory_order_relaxed) + i) % DMA_TRANS_POOL_SIZE;
            if (!dmaTransPool[idx].inUse) {
                dmaTransPool[idx].inUse = true;
                nextTransIndex.store((idx + 1) % DMA_TRANS_POOL_SIZE, std::memory_order_relaxed);
                return &dmaTransPool[idx];
            }
        }
        return nullptr;  // All transactions in use
    }
    
    /**
     * @brief Release a transaction back to the pool
     */
    void IRAM_ATTR releaseTrans(DmaTransaction* trans) {
        if (trans) {
            trans->inUse = false;
        }
    }
    
    // ========================================================================
    // DMA Callback (called when SPI transfer completes)
    // ========================================================================
    
    /**
     * @brief Post-transaction callback - processes received ADC data
     * 
     * Called from ISR context when DMA transfer completes.
     * Extracts 24-bit data and pushes to ring buffer.
     */
    void IRAM_ATTR spiPostTransCallback(spi_transaction_t* trans) {
        DmaTransaction* dmaTrans = (DmaTransaction*)trans->user;
        
        if (!dmaTrans || !continuousRunning) {
            if (dmaTrans) releaseTrans(dmaTrans);
            return;
        }
        
        // Extract 24-bit data from rx buffer (bytes 1-3, byte 0 was during command send)
        uint8_t* rx = dmaTrans->rxData;
        int32_t raw = ((int32_t)rx[1] << 16) | ((int32_t)rx[2] << 8) | rx[3];
        
        // Sign extension: if bit 23 is set, extend sign to bits 24-31
        if (raw & 0x800000) {
            raw |= 0xFF000000;
        }
        
        // Check buffer before pushing (ZERO LOSS POLICY)
        if (ringBuffer == nullptr || ringBuffer->isFull()) {
            overflowCount.fetch_add(1, std::memory_order_relaxed);
            overflowFlag = true;
            // Don't stop here - let main loop handle it
            releaseTrans(dmaTrans);
            return;
        }
        
        // Push to ring buffer
        ADCSample sample;
        sample.raw = raw;
        sample.timestamp_us = dmaTrans->timestamp_us;
        
        if (!ringBuffer->push(sample)) {
            overflowCount.fetch_add(1, std::memory_order_relaxed);
            overflowFlag = true;
        } else {
            samplesAcquired.fetch_add(1, std::memory_order_relaxed);
            lastTimestampUs = dmaTrans->timestamp_us;
        }
        
        // Release transaction back to pool
        releaseTrans(dmaTrans);
        dmaInProgress = false;
    }
    
    // ========================================================================
    // DRDY Interrupt Handler
    // ========================================================================
    
    /**
     * @brief DRDY Interrupt Service Routine (DMA version)
     * 
     * Called on falling edge of RDYB pin (data ready).
     * Queues a DMA SPI transaction - does NOT wait for completion.
     * ISR duration: ~1-2µs (just queue the transaction)
     */
    void IRAM_ATTR drdyISR() {
        isrFiredCount.fetch_add(1, std::memory_order_relaxed);  // Debug: count every ISR entry
        
        if (!continuousRunning || overflowFlag) {
            return;
        }
        
        uint32_t now = esp_timer_get_time();
        
        // Calculate latency from last DRDY
        if (lastDrdyTimeUs > 0) {
            uint32_t latency = now - lastDrdyTimeUs;
            if (latency > maxLatencyUs) {
                maxLatencyUs = latency;
            }
        }
        lastDrdyTimeUs = now;
        
        // Get a free transaction from pool
        DmaTransaction* dmaTrans = getFreeTrans();
        if (dmaTrans == nullptr) {
            // All transactions in use - DMA can't keep up!
            dmaQueueFull.fetch_add(1, std::memory_order_relaxed);
            overflowCount.fetch_add(1, std::memory_order_relaxed);
            overflowFlag = true;
            return;
        }
        
        // Prepare transaction
        dmaTrans->timestamp_us = now;
        dmaTrans->txData[0] = READ_DATA_CMD;
        dmaTrans->txData[1] = 0x00;
        dmaTrans->txData[2] = 0x00;
        dmaTrans->txData[3] = 0x00;
        
        // Configure SPI transaction
        memset(&dmaTrans->trans, 0, sizeof(spi_transaction_t));
        dmaTrans->trans.length = 32;  // 4 bytes = 32 bits
        dmaTrans->trans.tx_buffer = dmaTrans->txData;
        dmaTrans->trans.rx_buffer = dmaTrans->rxData;
        dmaTrans->trans.user = dmaTrans;  // Pass our struct to callback
        
        // Queue transaction (non-blocking, DMA handles it)
        esp_err_t err = spi_device_queue_trans(spiDevice, &dmaTrans->trans, 0);
        if (err != ESP_OK) {
            releaseTrans(dmaTrans);
            spiErrors.fetch_add(1, std::memory_order_relaxed);
        } else {
            dmaInProgress = true;
        }
    }
    
    // ========================================================================
    // Stop Continuous Mode (unsafe - for ISR use)
    // ========================================================================
    
    void IRAM_ATTR stopContinuousUnsafe() {
        detachInterrupt(digitalPinToInterrupt(PIN_ADC_RDYB));
        continuousRunning = false;
    }
    
    // ========================================================================
    // Non-DMA Helper Functions (for setup and single reads)
    // ========================================================================
    
    /**
     * @brief Blocking SPI transfer (for initialization and single reads)
     */
    esp_err_t spiTransferBlocking(uint8_t* txData, uint8_t* rxData, size_t len) {
        spi_transaction_t trans = {};
        trans.length = len * 8;
        trans.tx_buffer = txData;
        trans.rx_buffer = rxData;
        return spi_device_transmit(spiDevice, &trans);
    }
    
    /**
     * @brief Send command byte to ADC
     */
    void sendCommandInternal(uint8_t cmd) {
        uint8_t tx[1] = {cmd};
        uint8_t rx[1] = {0};
        spiTransferBlocking(tx, rx, 1);
    }
    
    /**
     * @brief Read register (variable width based on register)
     */
    uint32_t readRegisterInternal(Register reg, uint8_t numBytes) {
        uint8_t tx[4] = {0};
        uint8_t rx[4] = {0};
        
        // Command: 1100 | REG[3:0] << 1 | 1 (read)
        tx[0] = 0xC0 | (static_cast<uint8_t>(reg) << 1) | 0x01;
        
        spiTransferBlocking(tx, rx, 1 + numBytes);
        
        uint32_t value = 0;
        for (uint8_t i = 0; i < numBytes; i++) {
            value = (value << 8) | rx[1 + i];
        }
        
        return value;
    }
    
    /**
     * @brief Write register (variable width based on register)
     */
    void writeRegisterInternal(Register reg, uint32_t value, uint8_t numBytes) {
        uint8_t tx[4] = {0};
        uint8_t rx[4] = {0};
        
        // Command: 1100 | REG[3:0] << 1 | 0 (write)
        tx[0] = 0xC0 | (static_cast<uint8_t>(reg) << 1);
        
        // Send bytes MSB first
        for (int8_t i = numBytes - 1; i >= 0; i--) {
            tx[numBytes - i] = (value >> (i * 8)) & 0xFF;
        }
        
        spiTransferBlocking(tx, rx, 1 + numBytes);
    }
    
    /**
     * @brief Wait for RDYB to go low (data ready)
     */
    bool waitForReady(uint32_t timeout_ms) {
        uint32_t start = millis();
        while (digitalRead(PIN_ADC_RDYB) == HIGH) {
            if (millis() - start > timeout_ms) {
                drdyTimeouts.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            delayMicroseconds(10);
        }
        return true;
    }
    
    /**
     * @brief Read 24-bit data (blocking, for single conversions)
     */
    int32_t readDataBlocking() {
        uint8_t tx[4] = {READ_DATA_CMD, 0, 0, 0};
        uint8_t rx[4] = {0};
        
        spiTransferBlocking(tx, rx, 4);
        
        int32_t raw = ((int32_t)rx[1] << 16) | ((int32_t)rx[2] << 8) | rx[3];
        
        // Sign extension
        if (raw & 0x800000) {
            raw |= 0xFF000000;
        }
        
        return raw;
    }
    
    /**
     * @brief Get number of bytes for a register
     */
    uint8_t getRegisterSize(Register reg) {
        switch (reg) {
            case Register::DATA:
            case Register::SOC:
            case Register::SGC:
            case Register::SCOC:
            case Register::SCGC:
                return 3;  // 24-bit registers
            default:
                return 1;  // 8-bit registers
        }
    }
    
} // anonymous namespace

// ============================================================================
// Public API Implementation
// ============================================================================

bool init() {
    Serial.println("[MAX11270] Initializing with DMA support...");
    
    // Configure GPIO pins
    pinMode(PIN_ADC_CS, OUTPUT);
    digitalWrite(PIN_ADC_CS, HIGH);  // CS inactive (handled by SPI driver)
    
    pinMode(PIN_ADC_RSTB, OUTPUT);
    digitalWrite(PIN_ADC_RSTB, HIGH);  // Not in reset
    
    pinMode(PIN_ADC_SYNC, OUTPUT);
    digitalWrite(PIN_ADC_SYNC, HIGH);  // Must be HIGH to allow conversions
    
    pinMode(PIN_ADC_RDYB, INPUT);  // Data ready input
    
    // Initialize DMA transaction pool
    for (int i = 0; i < DMA_TRANS_POOL_SIZE; i++) {
        memset(&dmaTransPool[i], 0, sizeof(DmaTransaction));
        dmaTransPool[i].inUse = false;
    }
    
    // Configure SPI bus
    busConfig.miso_io_num = PIN_ADC_MISO;
    busConfig.mosi_io_num = PIN_ADC_MOSI;
    busConfig.sclk_io_num = PIN_ADC_SCK;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = 32;  // Small transfers for ADC
    busConfig.flags = SPICOMMON_BUSFLAG_MASTER;
    
    // Initialize SPI bus with DMA channel
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        Serial.printf("[MAX11270] SPI bus init failed: %d\n", err);
        return false;
    }
    
    // Configure SPI device
    devConfig.clock_speed_hz = ADC_SPI_FREQ_HZ;
    devConfig.mode = 0;  // SPI Mode 0 (CPOL=0, CPHA=0)
    devConfig.spics_io_num = PIN_ADC_CS;
    devConfig.queue_size = DMA_TRANS_POOL_SIZE;
    devConfig.pre_cb = nullptr;
    devConfig.post_cb = spiPostTransCallback;  // DMA completion callback
    devConfig.flags = 0;
    
    err = spi_bus_add_device(SPI2_HOST, &devConfig, &spiDevice);
    if (err != ESP_OK) {
        Serial.printf("[MAX11270] SPI device add failed: %d\n", err);
        spi_bus_free(SPI2_HOST);
        return false;
    }
    
    Serial.println("[MAX11270] SPI DMA initialized");
    
    // Hardware reset
    reset();
    
    // Verify communication
    if (!isPresent()) {
        Serial.println("[MAX11270] ERROR: ADC not responding!");
        return false;
    }
    
    // Apply default configuration (64ksps, 128x gain)
    configure(currentConfig);
    
    Serial.printf("[MAX11270] Initialized: %lu sps, gain %dx, DMA enabled\n",
                  rateToHz(currentConfig.rate),
                  gainToMultiplier(currentConfig.gain));
    
    return true;
}

void reset() {
    // Pulse RSTB low for >10ns (we use 10µs to be safe)
    digitalWrite(PIN_ADC_RSTB, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_ADC_RSTB, HIGH);
    
    // Wait for ADC to initialize (tPOR = 200µs typical)
    delay(1);
    
    // Clear internal state
    overflowFlag = false;
    overflowCount.store(0, std::memory_order_relaxed);
    continuousRunning = false;
    dmaInProgress = false;
    resetStatistics();
    
    // Reset transaction pool
    for (int i = 0; i < DMA_TRANS_POOL_SIZE; i++) {
        dmaTransPool[i].inUse = false;
    }
    
    // Run self-calibration for accurate readings
    // Self-cal offset (command 0x10)
    Serial.println("[MAX11270] Running self-calibration...");
    sendCommandInternal(0x10);
    delay(200);  // Wait for calibration (~100ms typical)
    
    // Self-cal gain (command 0x20)
    sendCommandInternal(0x20);
    delay(200);
    
    Serial.println("[MAX11270] Reset and self-cal complete");
}

bool isPresent() {
    // If continuous mode is running, ADC is obviously present
    // (and we can't do blocking SPI calls without causing a crash)
    if (continuousRunning) {
        return true;
    }
    
    // Read STAT1 register - should return valid data
    uint32_t stat = readRegisterInternal(Register::STAT1, 1);
    
    // Check that we got a reasonable response (not all 1s or 0s)
    return (stat != 0xFF) && (stat != 0x00);
}

void setGain(Gain gain) {
    currentConfig.gain = gain;
    
    // CTRL2[2:0] = PGAG[2:0] (PGA gain)
    uint32_t ctrl2 = readRegisterInternal(Register::CTRL2, 1);
    ctrl2 = (ctrl2 & 0xF8) | static_cast<uint8_t>(gain);
    writeRegisterInternal(Register::CTRL2, ctrl2, 1);
    
    Serial.printf("[MAX11270] Gain set to %dx\n", gainToMultiplier(gain));
}

Gain getGain() {
    return currentConfig.gain;
}

void setSampleRate(Rate rate) {
    currentConfig.rate = rate;
    Serial.printf("[MAX11270] Rate set to %lu sps\n", rateToHz(rate));
}

Rate getSampleRate() {
    return currentConfig.rate;
}

void configure(const Config& config) {
    currentConfig = config;
    
    // Set PGA gain in CTRL2
    setGain(config.gain);
    
    // Configure CTRL1 for continuous mode preparation
    uint8_t ctrl1 = 0x00;
    if (config.singleCycle) {
        ctrl1 |= 0x80;
    }
    writeRegisterInternal(Register::CTRL1, ctrl1, 1);
    
    // Configure CTRL3 for sync and calibration
    uint8_t ctrl3 = 0x00;  // All calibrations enabled
    writeRegisterInternal(Register::CTRL3, ctrl3, 1);
}

int32_t readSingle(uint32_t timeout_ms) {
    // Cannot do single reads while continuous DMA mode is running
    // (would cause SPI bus contention and crash)
    if (continuousRunning) {
        return INT32_MIN;  // Return error value
    }
    
    // Build conversion command
    uint8_t rateVal = static_cast<uint8_t>(currentConfig.rate);
    uint8_t cmd = 0x80 | (rateVal & 0x0F);  // Single conversion
    
    sendCommandInternal(cmd);
    
    // Wait for conversion to complete
    if (!waitForReady(timeout_ms)) {
        Serial.println("[MAX11270] Single conversion timeout!");
        return INT32_MIN;
    }
    
    return readDataBlocking();
}

bool startContinuous(ADCRingBufferLarge* buffer) {
    if (buffer == nullptr) {
        Serial.println("[MAX11270] ERROR: Null buffer provided!");
        return false;
    }
    
    if (continuousRunning) {
        Serial.println("[MAX11270] Already running continuous mode");
        return true;
    }
    
    ringBuffer = buffer;
    overflowFlag = false;
    overflowCount.store(0, std::memory_order_relaxed);
    dmaQueueFull.store(0, std::memory_order_relaxed);
    samplesAcquired.store(0, std::memory_order_relaxed);  // Reset sample counter!
    spiErrors.store(0, std::memory_order_relaxed);         // Reset error counter
    isrFiredCount.store(0, std::memory_order_relaxed);     // Reset ISR counter
    lastDrdyTimeUs = 0;
    maxLatencyUs = 0;
    dmaInProgress = false;
    
    // Reset transaction pool
    for (int i = 0; i < DMA_TRANS_POOL_SIZE; i++) {
        dmaTransPool[i].inUse = false;
    }
    
    // Check DRDY pin state before starting
    int drdyState = digitalRead(PIN_ADC_RDYB);
    Serial.printf("[MAX11270] DRDY pin state before start: %s\n", drdyState ? "HIGH" : "LOW");
    
    // Start continuous conversion FIRST (blocking SPI, no ISR yet)
    // Command byte: [7]=1 (convert), [6:5]=01 (continuous), [4:0]=rate
    uint8_t rateVal = static_cast<uint8_t>(currentConfig.rate);
    uint8_t cmd = 0xA0 | (rateVal & 0x0F);  // Continuous mode
    
    Serial.printf("[MAX11270] Sending continuous mode command: 0x%02X (rate=%d)\n", cmd, rateVal);
    sendCommandInternal(cmd);  // Safe - no interrupt attached yet
    
    // Wait for first DRDY (verify ADC is actually converting)
    uint32_t startWait = millis();
    while (digitalRead(PIN_ADC_RDYB) == HIGH) {
        if (millis() - startWait > 100) {  // 100ms timeout
            Serial.println("[MAX11270] ERROR: DRDY never went LOW - ADC not converting!");
            return false;
        }
        delayMicroseconds(10);
    }
    Serial.println("[MAX11270] First DRDY received - ADC is converting");
    
    // Enable ISR processing flag BEFORE attaching interrupt
    // This prevents the race condition where ISR fires but flag is still false
    continuousRunning = true;
    
    // NOW attach DRDY interrupt (falling edge = data ready)
    // Must be after sendCommandInternal to avoid SPI bus contention
    // Must be after continuousRunning=true so ISR doesn't return early
    attachInterrupt(digitalPinToInterrupt(PIN_ADC_RDYB), drdyISR, FALLING);
    
    Serial.printf("[MAX11270] DMA continuous mode started at %lu sps\n",
                  rateToHz(currentConfig.rate));
    
    return true;
}

void stopContinuous() {
    if (!continuousRunning) {
        return;
    }
    
    // Disable interrupt first - stops new DMA transactions from being queued
    detachInterrupt(digitalPinToInterrupt(PIN_ADC_RDYB));
    continuousRunning = false;
    
    // Small delay to let any in-flight ISR complete
    delay(5);
    
    // Drain ALL pending DMA transactions from the SPI queue
    // This is critical - without this, any subsequent blocking SPI call will crash
    spi_transaction_t* completedTrans;
    int drainCount = 0;
    while (spi_device_get_trans_result(spiDevice, &completedTrans, pdMS_TO_TICKS(10)) == ESP_OK) {
        // Transaction completed, release it back to the pool
        DmaTransaction* dmaTrans = (DmaTransaction*)completedTrans->user;
        if (dmaTrans) {
            dmaTrans->inUse = false;
        }
        drainCount++;
        if (drainCount > DMA_TRANS_POOL_SIZE * 2) {
            // Safety limit to prevent infinite loop
            break;
        }
    }
    
    // Reset all transaction pool entries to be safe
    for (int i = 0; i < DMA_TRANS_POOL_SIZE; i++) {
        dmaTransPool[i].inUse = false;
    }
    
    Serial.printf("[MAX11270] STOP: ISR=%lu, Samples=%lu, Dropped=%lu, SPIErr=%lu, Drained=%d\n",
                  isrFiredCount.load(std::memory_order_relaxed),
                  samplesAcquired.load(std::memory_order_relaxed),
                  overflowCount.load(std::memory_order_relaxed),
                  spiErrors.load(std::memory_order_relaxed),
                  drainCount);
}

bool isRunning() {
    return continuousRunning;
}

bool hasOverflow() {
    return overflowFlag;
}

uint32_t getOverflowCount() {
    return overflowCount.load(std::memory_order_relaxed);
}

void clearOverflow() {
    overflowFlag = false;
    overflowCount.store(0, std::memory_order_relaxed);
    dmaQueueFull.store(0, std::memory_order_relaxed);
}

Statistics getStatistics() {
    Statistics stats;
    stats.samplesAcquired = samplesAcquired.load(std::memory_order_relaxed);
    stats.samplesDropped = overflowCount.load(std::memory_order_relaxed);
    stats.drdyTimeouts = drdyTimeouts.load(std::memory_order_relaxed);
    stats.spiErrors = spiErrors.load(std::memory_order_relaxed);
    stats.dmaQueueFull = dmaQueueFull.load(std::memory_order_relaxed);
    stats.maxLatencyUs = maxLatencyUs;
    stats.lastTimestampUs = lastTimestampUs;
    return stats;
}

void resetStatistics() {
    samplesAcquired.store(0, std::memory_order_relaxed);
    drdyTimeouts.store(0, std::memory_order_relaxed);
    spiErrors.store(0, std::memory_order_relaxed);
    dmaQueueFull.store(0, std::memory_order_relaxed);
    maxLatencyUs = 0;
    lastTimestampUs = 0;
    lastDrdyTimeUs = 0;
}

float readTemperature() {
    // MAX11270 temperature sensor - placeholder
    return 25.0f;
}

bool selfCalibrate() {
    Serial.println("[MAX11270] Starting self-calibration...");
    
    sendCommandInternal(0xA0);  // Self-cal command
    
    if (!waitForReady(200)) {
        Serial.println("[MAX11270] Self-calibration timeout!");
        return false;
    }
    
    Serial.println("[MAX11270] Self-calibration complete");
    return true;
}

uint32_t readRegister(Register reg) {
    return readRegisterInternal(reg, getRegisterSize(reg));
}

void writeRegister(Register reg, uint32_t value) {
    writeRegisterInternal(reg, value, getRegisterSize(reg));
}

void sendCommand(uint8_t cmd) {
    sendCommandInternal(cmd);
}

float rawToMicrovolts(int32_t raw) {
    uint8_t gain = gainToMultiplier(currentConfig.gain);
    float fullScale = VREF / gain;
    float resolution = fullScale / (float)(1 << 24);
    return raw * resolution * 1000000.0f;
}

uint32_t rateToHz(Rate rate) {
    static const uint32_t rates[] = {
        2, 4, 8, 16, 31, 63, 125, 250, 500, 1000,
        2000, 4000, 8000, 16000, 32000, 64000
    };
    
    uint8_t idx = static_cast<uint8_t>(rate);
    if (idx < sizeof(rates) / sizeof(rates[0])) {
        return rates[idx];
    }
    return 0;
}

uint8_t gainToMultiplier(Gain gain) {
    return 1 << static_cast<uint8_t>(gain);
}

} // namespace MAX11270
