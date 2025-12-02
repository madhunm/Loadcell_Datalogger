#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h> //Library for the Logging status LED
#include "FS.h"                //FAT system library used by the SD-MMC library
#include "SD_MMC.h"            //SD-MMC library
#include "RX8900.h"            //RTC Library
#include "imu.h"               //IMU Library
#include "pins.h"              //Pinconfig header

// ---- NeoPixel setup ----
#define NEOPIXEL_NUMPIXELS 1

// neoPixel Object
Adafruit_NeoPixel statusPixel(NEOPIXEL_NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// RTC object
RX8900 rx8900;

// Simple flag set from the INT ISR when a 1 Hz update happens
volatile bool g_rtcUpdatePending = false;

void IRAM_ATTR rtcIntIsr()
{
  g_rtcUpdatePending = true;
  // Keep ISR short; we clear flags in the main context
}

// ---- System state machine ----
enum SystemState
{
  STATE_INIT = 0, // peripherals coming up
  STATE_READY,    // ready to log (waiting for button)
  STATE_LOGGING   // actively logging
};

SystemState systemState = STATE_INIT;

// ---- Helper: set status LED colors ----
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

bool initSdCard()
{
  Serial.println("[INIT][SD] Initializing SD card (SD_MMC 4-bit)...");

  // Configure and read card-detect pin (assumed active-low: LOW = card present)
  pinMode(PIN_SD_CD, INPUT_PULLUP);
  delay(5); // small settle time

  if (digitalRead(PIN_SD_CD) == HIGH)
  {
    Serial.println("[INIT][SD] No SD card detected (CD pin HIGH).");
    return false;
  }
  Serial.println("[INIT][SD] Card detect OK (card present).");

  // Configure SDMMC pins for ESP32-S3
  // On ESP32-S3, SDMMC pins can be freely mapped via GPIO matrix and SD_MMC.setPins()
  if (!SD_MMC.setPins(
          PIN_SD_CLK,
          PIN_SD_CMD,
          PIN_SD_D0,
          PIN_SD_D1,
          PIN_SD_D2,
          PIN_SD_D3))
  {
    Serial.println("[INIT][SD] SD_MMC.setPins() failed!");
    return false;
  }
  Serial.printf("[INIT][SD] Pins set: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d\n",
                PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0,
                PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);

  // Mount the card
  // Signature (current core): begin(mountpoint, mode1bit, format_if_mount_failed, freq, maxOpenFiles)
  Serial.println("[INIT][SD] Calling SD_MMC.begin()...");
  if (!SD_MMC.begin("/sdcard", false, false))
  { // 4-bit, don't auto-format
    Serial.println("[INIT][SD] Card mount failed.");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("[INIT][SD] No SD_MMC card attached after mount.");
    return false;
  }

  Serial.print("[INIT][SD] Card type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC/SDXC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSizeMB = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[INIT][SD] Card size: %llu MB\n", cardSizeMB);

  // Optional: ensure /log directory exists for logging
  if (!SD_MMC.exists("/log"))
  {
    Serial.println("[INIT][SD] Creating /log directory...");
    if (!SD_MMC.mkdir("/log"))
    {
      Serial.println("[INIT][SD] WARNING: Failed to create /log (non-fatal).");
    }
    else
    {
      Serial.println("[INIT][SD] /log created.");
    }
  }
  else
  {
    Serial.println("[INIT][SD] /log already exists.");
  }

  Serial.println("[INIT][SD] SD card initialization OK.");
  return true;
}

bool initImu()
{
  Serial.println(F("[IMU] Initialising LSM6DSV..."));

  // INT pins from the IMU to the ESP32 — keep as plain inputs for now.
  pinMode(PIN_IMU_INT1, INPUT);
  pinMode(PIN_IMU_INT2, INPUT);

  // Start the IMU on the default I2C bus and address.
  // SparkFun board defaults to I2C address 0x6B. If your SDO/SA0 pin is
  // strapped low (0x6A) you may need to adjust the library's address or
  // use an overload of begin() that takes an address.
  if (!imu.begin())
  {
    Serial.println(F("[IMU] begin() failed – check wiring and I2C address (0x6B vs 0x6A)."));
    return false;
  }

  // Reset device to a known state (SparkFun example pattern).
  imu.deviceReset();
  while (!imu.getDeviceReset())
  {
    delay(1);
  }
  Serial.println(F("[IMU] Reset complete, applying configuration."));

  // Avoid partial updates: accel/gyro registers update only when both are ready.
  imu.enableBlockDataUpdate();

  // ***** Accelerometer config *****
  // Full-scale: ±16 g
  imu.setAccelFullScale(LSM6DSV16X_16g);

  // Output data rate: start low for bring-up (7.5 Hz).
  // Once everything works, we can bump this up (e.g. 120 / 480 Hz) by
  // changing this enum.
  imu.setAccelDataRate(LSM6DSV16X_ODR_AT_7Hz5);

  // Enable accel filtering (same pattern as SparkFun example 1).
  imu.enableFilterSettling();
  imu.enableAccelLP2Filter();
  imu.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

  // ***** Gyroscope config *****
  imu.setGyroFullScale(LSM6DSV16X_2000dps);
  imu.setGyroDataRate(LSM6DSV16X_ODR_AT_15Hz);

  imu.enableGyroLP1Filter();
  imu.setGyroLP1Bandwidth(LSM6DSV16X_GY_ULTRA_LIGHT);

  Serial.println(F("[IMU] Configuration done."));

  return true;
}

bool initRtc()
{
  Serial.println("[INIT][RTC] Initializing RX8900...");

  // INT is open-drain active-LOW → need a pull-up
  pinMode(PIN_RTC_INT, INPUT_PULLUP);

  // Attach interrupt on falling edge (HIGH → LOW)
  attachInterrupt(
      digitalPinToInterrupt(PIN_RTC_INT),
      rtcIntIsr,
      FALLING);

  // Library initialization
  rx8900.RX8900Init(); // Sets up RX8900 internal registers (per lib default)

  // Configure the "time update interrupt" to 1-second period
  // false = 1 second, true = 1 minute (from example comments)
  (void)rx8900.UpdateInterruptTimingChange(false);

  // InterruptSettings(AIE, TIE, UIE)
  // We only want "UIE" (time update interrupt); alarm and timer off for now.
  (void)rx8900.InterruptSettings(false, false, true);

  Serial.println("[INIT][RTC] RX8900 configured for 1 Hz update interrupt.");
  return true;
}

bool initAdc()
{
  Serial.println("[INIT] ADC...");
  // TODO: add real MAX11270 init
  delay(50);
  return true;
}

bool initPeripherals()
{
  bool ok = true;

  if (!initSdCard())
    ok = false;
  if (!initImu())
    ok = false;
  if (!initRtc())
    ok = false;
  if (!initAdc())
    ok = false;

  return ok;
}

// ---- Logging stub ----
void loggingTick()
{
  // Placeholder for your real logging loop (ADC read + SD write etc.)
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (now - lastPrint > 1000)
  {
    lastPrint = now;
    Serial.println("[LOG] Logging tick...");
  }
}

void handleRtcUpdate()
{
  if (!g_rtcUpdatePending)
  {
    return;
  }

  g_rtcUpdatePending = false;

  uint8_t flagChange = 0;
  // Figure out what interrupt fired (UF / TF / AF)
  rx8900.JudgeInterruptSignalType(&flagChange);
  Serial.print("[RTC] flagChange: ");
  Serial.println(flagChange, BIN);

  // Here we only expect the Update Flag (UF) to have toggled.
  // Clear only the UF bit: AF=false, TF=false, UF=true
  rx8900.ClearOccurrenceNotification(false, false, true);

  // Now you know "one second has passed" in RTC time.
  // You can:
  //  - increment a software seconds counter
  //  - mark a new log 'epoch' for stats
  //  - or periodically read the time registers (we’ll do that later)
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

  // Logstart: external pulldown to GND; button goes to 3V3
  pinMode(PIN_LOGSTART_BUTTON, INPUT);

  // SPI pins for ADC
  pinMode(PIN_ADC_CS, OUTPUT);
  pinMode(PIN_ADC_SCK, OUTPUT);
  pinMode(PIN_ADC_MOSI, OUTPUT);
  pinMode(PIN_ADC_MISO, INPUT);

  pinMode(PIN_ADC_SYNC, OUTPUT);
  pinMode(PIN_ADC_RSTB, OUTPUT);
  pinMode(PIN_ADC_RDYB, INPUT);

  // RTC / IMU interrupt lines
  pinMode(PIN_RTC_INT, INPUT);
  pinMode(PIN_RTC_FOUT, INPUT);
  pinMode(PIN_IMU_INT1, INPUT);
  pinMode(PIN_IMU_INT2, INPUT);

  // ---- I2C bus ----
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.printf("I2C started on SDA=%d SCL=%d\n", PIN_I2C_SDA, PIN_I2C_SCL);

  // ---- NeoPixel ----
  statusPixel.begin();
  statusPixel.setBrightness(50); // tweak later if needed
  setStatusRed();                // 1) red while initializing

  // ---- Peripheral initialization ----
  systemState = STATE_INIT;
  setStatusRed();

  bool ok = initPeripherals();
  if (ok)
  {
    systemState = STATE_READY;
    setStatusGreen(); // ready to log
    Serial.println("[STATE] READY: system ready to log (waiting for Logstart button)");
  }
  else
  {
    systemState = STATE_INIT; // stays red
    Serial.println("[ERROR] Peripheral init failed. Staying in INIT state.");
  }
}

void loop()
{
  handleRtcUpdate();

  // ---- Logstart button handling ----
  // External pulldown: LOW = idle, HIGH = pressed
  static bool lastButton = false;
  bool nowButton = (digitalRead(PIN_LOGSTART_BUTTON) == HIGH);

  // Simple edge detection: rising edge = button press
  if (nowButton && !lastButton)
  {
    Serial.println("[BUTTON] Logstart pressed.");
    if (systemState == STATE_READY)
    {
      systemState = STATE_LOGGING;
      Serial.println("[STATE] LOGGING: logging started.");
      // For now LED stays green; if you want a different color for "actively logging",
      // change it here.
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
    // In this stub we don't re-run init; just sit here
    break;

  case STATE_READY:
    // System is ready and LED is green, waiting for button
    break;

  case STATE_LOGGING:
    // 3) Logging only runs after Logstart button press
    loggingTick();
    break;
  }

  delay(10);
}
