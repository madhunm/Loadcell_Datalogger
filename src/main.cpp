#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

#include "pins.h"
#include "sdcard.h"
#include "rtc.h"
#include "imu.h"

// ---- NeoPixel setup ----
#define NEOPIXEL_NUMPIXELS 1

Adafruit_NeoPixel statusPixel(NEOPIXEL_NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// ---- System state machine ----
enum SystemState
{
    STATE_INIT = 0,
    STATE_READY,
    STATE_LOGGING
};

SystemState systemState = STATE_INIT;

// ---- Status LED helpers ----
void setStatusRed()
{
    statusPixel.setPixelColor(0, statusPixel.Color(255, 0, 0));
    statusPixel.show();
}

void setStatusGreen()
{
    statusPixel.setPixelColor(0, statusPixel.Color(0, 255, 0));
    statusPixel.show();
}

// ---- ADC init stub ----
bool initAdc()
{
    Serial.println("[INIT][ADC] (stub) MAX11270 not initialised yet.");
    return true;
}

// ---- Combined peripheral init ----
bool initPeripherals()
{
    bool ok = true;

    if (!sdCardInit())
        ok = false;

    if (!imuInit(Wire))
        ok = false;

    if (!rtcInit())
        ok = false;

    if (!initAdc())
        ok = false;

    return ok;
}

// ---- Logging stub ----
void loggingTick()
{
    static uint32_t lastPrint = 0;
    uint32_t now = millis();
    if (now - lastPrint > 1000)
    {
        lastPrint = now;
        Serial.println("[LOG] Logging tick...");
    }
}

void setup()
{
    delay(500);

    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("Loadcell Logger – ESP32-S3 bring-up");
    Serial.println("------------------------------------");

    // ---- GPIO directions ----

    // Logstart button: external pulldown (LOW idle, HIGH pressed)
    pinMode(PIN_LOGSTART_BUTTON, INPUT);

    // ADC SPI pins
    pinMode(PIN_ADC_CS, OUTPUT);
    pinMode(PIN_ADC_SCK, OUTPUT);
    pinMode(PIN_ADC_MOSI, OUTPUT);
    pinMode(PIN_ADC_MISO, INPUT);

    pinMode(PIN_ADC_SYNC, OUTPUT);
    pinMode(PIN_ADC_RSTB, OUTPUT);
    pinMode(PIN_ADC_RDYB, INPUT);

    // RTC pins
    pinMode(PIN_RTC_FOUT, INPUT); // FOUT routed but not used yet

    // IMU interrupt pins as inputs (IMU module also sets them)
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    // I2C bus for RTC + IMU + gauge, etc.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("I2C started on SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);

    // NeoPixel
    statusPixel.begin();
    statusPixel.setBrightness(50);
    setStatusRed();

    // Peripheral initialisation
    systemState = STATE_INIT;
    Serial.println("[STATE] INIT: bringing up peripherals...");

    bool ok = initPeripherals();
    if (ok)
    {
        systemState = STATE_READY;
        setStatusGreen();
        Serial.println("[STATE] READY: system ready to log (waiting for Logstart button)");
    }
    else
    {
        systemState = STATE_INIT;
        setStatusRed();
        Serial.println("[ERROR] Peripheral init failed. Staying in INIT state.");
    }
}

void loop()
{
    // Service RTC 1 Hz update (if a tick occurred)
    rtcHandleUpdate();

    // ---- Logstart button handling ----
    // External pulldown: LOW = idle, HIGH = pressed
    static bool lastButton = false;
    bool nowButton = (digitalRead(PIN_LOGSTART_BUTTON) == HIGH);

    if (nowButton && !lastButton)
    {
        Serial.println("[BUTTON] Logstart pressed.");
        if (systemState == STATE_READY)
        {
            systemState = STATE_LOGGING;
            Serial.println("[STATE] LOGGING: logging started.");
            // LED stays green for "ready/logging" for now
            setStatusGreen();
        }
        else
        {
            Serial.printf("[STATE] Button press ignored (state=%d)\n", systemState);
        }
    }
    lastButton = nowButton;

    // ---- State-specific work ----
    switch (systemState)
    {
    case STATE_INIT:
        // Error state; nothing more to do here
        break;

    case STATE_READY:
        // System ready; waiting for logstart button
        break;

    case STATE_LOGGING:
        // Logging loop (ADC/SD/IMU hooks will go here later)
        loggingTick();
        break;
    }

    delay(10);
}
