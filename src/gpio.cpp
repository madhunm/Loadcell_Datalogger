#include "gpio.h"
#include "pins.h"
#include <Wire.h>

void gpioInit()
{
    // ---- Logstart button ----
    // External pulldown (LOW idle, HIGH pressed)
    pinMode(PIN_LOGSTART_BUTTON, INPUT);

    // ---- ADC SPI pins ----
    // Note: adcInit() also configures these, but setting them here is harmless
    pinMode(PIN_ADC_CS, OUTPUT);
    pinMode(PIN_ADC_SCK, OUTPUT);
    pinMode(PIN_ADC_MOSI, OUTPUT);
    pinMode(PIN_ADC_MISO, INPUT);
    pinMode(PIN_ADC_SYNC, OUTPUT);
    pinMode(PIN_ADC_RSTB, OUTPUT);
    pinMode(PIN_ADC_RDYB, INPUT);

    // ---- RTC pins ----
    pinMode(PIN_RTC_FOUT, INPUT); // FOUT routed but not used yet
    // PIN_RTC_INT is configured by rtcInit()

    // ---- IMU interrupt pins ----
    // IMU module also sets these, but setting here ensures correct state
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    // ---- I2C bus ----
    // Initialize I2C for RTC + IMU + gauge, etc.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}


