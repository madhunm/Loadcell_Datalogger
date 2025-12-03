#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "FS.h"
#include "SD_MMC.h"
#include "RX8900.h"
#include "imu.h"
#include "pins.h"

// ---- NeoPixel setup ----
#define NEOPIXEL_NUMPIXELS 1

Adafruit_NeoPixel statusPixel(NEOPIXEL_NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// ---- RTC ----
RX8900 rtcDevice;
volatile bool rtcUpdatePending = false;

void IRAM_ATTR rtcIntIsr()
{
  rtcUpdatePending = true;
}

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

// ---- SD card init ----
bool initSdCard()
{
  Serial.println("[INIT][SD] Initialising SD card (SD_MMC 4-bit)...");

  pinMode(PIN_SD_CD, INPUT_PULLUP);
  delay(5);

  if (digitalRead(PIN_SD_CD) == HIGH)
  {
    Serial.println("[INIT][SD] No SD card detected (CD pin HIGH).");
    return false;
  }
  Serial.println("[INIT][SD] Card detect OK (card present).");

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

  if (!SD_MMC.begin("/sdcard", false, false))
  {
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
    Serial.println("MMC");
  else if (cardType == CARD_SD)
    Serial.println("SDSC");
  else if (cardType == CARD_SDHC)
    Serial.println("SDHC/SDXC");
  else
    Serial.println("UNKNOWN");

  uint64_t cardSizeMb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[INIT][SD] Card size: %llu MB\n", cardSizeMb);

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

  Serial.println("[INIT][SD] SD card initialisation OK.");
  return true;
}

// ---- IMU init wrapper ----
bool initImu()
{
  Serial.println("[INIT][IMU] Initialising IMU...");
  bool ok = imuInit(Wire);
  if (!ok)
  {
    Serial.println("[INIT][IMU] IMU init failed.");
  }
  return ok;
}

// ---- RTC init ----
bool initRtc()
{
  Serial.println("[INIT][RTC] Initialising RX8900...");

  pinMode(PIN_RTC_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RTC_INT), rtcIntIsr, FALLING);

  rtcDevice.RX8900Init();
  rtcDevice.UpdateInterruptTimingChange(false);    // false = 1 second, true = 1 minute
  rtcDevice.InterruptSettings(false, false, true); // AIE, TIE, UIE

  Serial.println("[INIT][RTC] RX8900 configured for 1 Hz update interrupt.");
  return true;
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
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  if (now - lastPrint > 1000)
  {
    lastPrint = now;
    Serial.println("[LOG] Logging tick...");
  }
}

// ---- RTC 1 Hz handler ----
void handleRtcUpdate()
{
  if (!rtcUpdatePending)
    return;

  rtcUpdatePending = false;

  uint8_t flagChange = 0;
  rtcDevice.JudgeInterruptSignalType(&flagChange);
  Serial.print("[RTC] flagChange: 0b");
  Serial.println(flagChange, BIN);

  // Clear the update flag (UF) only
  rtcDevice.ClearOccurrenceNotification(false, false, true);
}

// ---- Arduino setup / loop ----
void setup()
{
  delay(500);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Loadcell Logger – ESP32-S3 bring-up");
  Serial.println("------------------------------------");

  // GPIO directions
  pinMode(PIN_LOGSTART_BUTTON, INPUT);

  pinMode(PIN_ADC_CS, OUTPUT);
  pinMode(PIN_ADC_SCK, OUTPUT);
  pinMode(PIN_ADC_MOSI, OUTPUT);
  pinMode(PIN_ADC_MISO, INPUT);

  pinMode(PIN_ADC_SYNC, OUTPUT);
  pinMode(PIN_ADC_RSTB, OUTPUT);
  pinMode(PIN_ADC_RDYB, INPUT);

  pinMode(PIN_RTC_INT, INPUT_PULLUP);
  pinMode(PIN_RTC_FOUT, INPUT);

  pinMode(PIN_IMU_INT1, INPUT);
  pinMode(PIN_IMU_INT2, INPUT);

  // I2C bus for RTC + IMU + others
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
  handleRtcUpdate();

  // Logstart button (external pulldown: LOW idle, HIGH pressed)
  static bool lastButton = false;
  bool nowButton = (digitalRead(PIN_LOGSTART_BUTTON) == HIGH);

  if (nowButton && !lastButton)
  {
    Serial.println("[BUTTON] Logstart pressed.");
    if (systemState == STATE_READY)
    {
      systemState = STATE_LOGGING;
      Serial.println("[STATE] LOGGING: logging started.");
      setStatusGreen();
    }
    else
    {
      Serial.printf("[STATE] Button press ignored (state=%d)\n", systemState);
    }
  }
  lastButton = nowButton;

  switch (systemState)
  {
  case STATE_INIT:
    // Stuck in error state; nothing to do
    break;

  case STATE_READY:
    // Ready; waiting for logstart button
    break;

  case STATE_LOGGING:
    loggingTick();
    break;
  }

  delay(10);
}
