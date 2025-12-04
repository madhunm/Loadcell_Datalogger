#include "sdcard.h"

#include "FS.h"
#include "SD_MMC.h"
#include "pins.h"

static bool sdCardMounted = false;

bool sdCardInit()
{
    Serial.println("[INIT][SD] Initialising SD card (SD_MMC 4-bit)...");

    // Card-detect pin (active-low: LOW = card present)
    pinMode(PIN_SD_CD, INPUT_PULLUP);
    delay(5);

    if (digitalRead(PIN_SD_CD) == HIGH)
    {
        Serial.println("[INIT][SD] No SD card detected (CD pin HIGH).");
        sdCardMounted = false;
        return false;
    }
    Serial.println("[INIT][SD] Card detect OK (card present).");

    // Configure SDMMC pins
    if (!SD_MMC.setPins(
            PIN_SD_CLK,
            PIN_SD_CMD,
            PIN_SD_D0,
            PIN_SD_D1,
            PIN_SD_D2,
            PIN_SD_D3))
    {
        Serial.println("[INIT][SD] SD_MMC.setPins() failed!");
        sdCardMounted = false;
        return false;
    }

    Serial.printf("[INIT][SD] Pins set: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d\n",
                  PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0,
                  PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);

    // Mount the card at /sdcard, 4-bit mode, do not auto-format
    if (!SD_MMC.begin("/sdcard", false, false))
    {
        Serial.println("[INIT][SD] Card mount failed.");
        sdCardMounted = false;
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("[INIT][SD] No SD_MMC card attached after mount.");
        sdCardMounted = false;
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

    // Ensure /log directory exists
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
    sdCardMounted = true;
    return true;
}

bool sdCardIsMounted()
{
    return sdCardMounted;
}

fs::FS &sdCardGetFs()
{
    // SD_MMC is a global FS-like object provided by the core
    return SD_MMC;
}

bool sdCardCheckPresent()
{
    // Card-detect pin is active-low: LOW = card present, HIGH = card removed
    // Pin has internal pullup, so when card is removed, pin goes HIGH
    bool cardPresent = (digitalRead(PIN_SD_CD) == LOW);
    
    // Update mounted state if card was removed
    if (!cardPresent && sdCardMounted)
    {
        Serial.println("[SD] Card removal detected via CD pin!");
        sdCardMounted = false;
        // Note: We don't unmount here to avoid issues if card is reinserted quickly
        // The filesystem will handle errors on next access
    }
    
    return cardPresent;
}

uint64_t sdCardGetFreeSpace()
{
    if (!sdCardMounted)
    {
        return 0;
    }
    return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

uint64_t sdCardGetTotalSpace()
{
    if (!sdCardMounted)
    {
        return 0;
    }
    return SD_MMC.totalBytes();
}