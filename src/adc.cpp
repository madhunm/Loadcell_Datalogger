#include "adc.h"

// Use the default SPI instance
static SPIClass &adcSpi = SPI;

// Internal helpers
static inline void adcSelect()
{
    digitalWrite(ADC_CS_PIN, LOW);
}

static inline void adcDeselect()
{
    digitalWrite(ADC_CS_PIN, HIGH);
}

// Build command byte for register mode (MODE = 1).
// START = 1, MODE = 1, RS[4:0] = reg, R/W in bit 0.
static uint8_t buildRegisterCommand(uint8_t reg, bool isRead)
{
    uint8_t cmd = 0;
    cmd |= 0x80;                 // START = 1 (bit 7)
    cmd |= 0x40;                 // MODE = 1 (bit 6, register mode)
    cmd |= ((reg & 0x1F) << 1);  // RS4:0 bits 5:1
    if (isRead)
    {
        cmd |= 0x01;             // R/W = 1 = read
    }
    return cmd;
}

// Build command byte for conversion mode (MODE = 0).
// START = 1, MODE = 0, CAL, IMPD, RATE3:0
static uint8_t buildConversionCommand(uint8_t rateCode, bool doCalibration, bool immediatePowerDown)
{
    uint8_t cmd = 0;
    cmd |= 0x80; // START = 1
    // MODE = 0, so bit 6 stays 0
    if (doCalibration)
    {
        cmd |= 0x20; // CAL bit (B5)
    }
    if (immediatePowerDown)
    {
        cmd |= 0x10; // IMPD bit (B4)
    }
    cmd |= (rateCode & 0x0F); // RATE3:0 in B3..B0
    return cmd;
}

// Low-level register access
bool adcWriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t cmd = buildRegisterCommand(reg, false);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    adcSpi.transfer(value);
    adcSpi.endTransaction();
    adcDeselect();

    return true;
}

bool adcReadRegister(uint8_t reg, uint8_t &value)
{
    uint8_t cmd = buildRegisterCommand(reg, true);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    value = adcSpi.transfer(0x00);
    adcSpi.endTransaction();
    adcDeselect();

    return true;
}

// Read a 24-bit register (DATA and calibration registers)
static bool adcReadRegister24(uint8_t reg, uint32_t &raw)
{
    uint8_t cmd = buildRegisterCommand(reg, true);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);

    uint8_t b2 = adcSpi.transfer(0x00); // MSB
    uint8_t b1 = adcSpi.transfer(0x00);
    uint8_t b0 = adcSpi.transfer(0x00); // LSB

    adcSpi.endTransaction();
    adcDeselect();

    raw = (static_cast<uint32_t>(b2) << 16) |
          (static_cast<uint32_t>(b1) << 8)  |
          (static_cast<uint32_t>(b0));

    return true;
}

// Convert 24-bit two's complement to signed 32-bit
static int32_t signExtend24(uint32_t raw24)
{
    if (raw24 & 0x800000UL)
    {
        // Negative number, sign-extend
        raw24 |= 0xFF000000UL;
    }
    return static_cast<int32_t>(raw24);
}

// Public API

bool adcInit(AdcPgaGain pgaGain)
{
    // PGA gain code is just the enum value (0..7)
    uint8_t pgaGainCode = static_cast<uint8_t>(pgaGain) & 0x07;

    // Configure GPIOs
    pinMode(ADC_CS_PIN,   OUTPUT);
    pinMode(ADC_RSTB_PIN, OUTPUT);
    pinMode(ADC_SYNC_PIN, OUTPUT);
    pinMode(ADC_RDYB_PIN, INPUT_PULLUP); // RDYB is active-low output from ADC

    // Ensure bus is idle
    digitalWrite(ADC_CS_PIN,   HIGH);
    digitalWrite(ADC_SYNC_PIN, HIGH); // SYNC idle high
    digitalWrite(ADC_RSTB_PIN, HIGH); // RSTB inactive (active low)

    // Initialize SPI with your pin mapping
    adcSpi.begin(ADC_SCK_PIN, ADC_MISO_PIN, ADC_MOSI_PIN, ADC_CS_PIN);

    // Hardware reset: pull RSTB low briefly then high
    digitalWrite(ADC_RSTB_PIN, LOW);
    delayMicroseconds(10);  // t_RST min is small; 10 µs is safe
    digitalWrite(ADC_RSTB_PIN, HIGH);

    // Give the ADC some time to come out of reset
    delay(5);

    // Optional: clear any stale status by reading STAT
    uint8_t stat = 0;
    adcReadRegister(ADC_REG_STAT, stat);

    // Configure CTRL1:
    //   EXTCK = 0  -> use internal clock
    //   SYNCMODE = 0 -> default sync behavior
    //   PD1:0 = 00 -> active (not standby/sleep)
    //   U/~B = 0   -> bipolar mode (better for bridge around 0V)
    //   FORMAT = 0 -> two's complement
    //   SCYCLE = 0 -> continuous conversion mode
    //   CONTSC = 0 -> normal continuous
    uint8_t ctrl1 =
        (0 << 7) | // EXTCK
        (0 << 6) | // SYNCMODE
        (0 << 5) | // PD1
        (0 << 4) | // PD0
        (0 << 3) | // U/~B (0 = bipolar)
        (0 << 2) | // FORMAT (0 = two's complement)
        (0 << 1) | // SCYCLE (0 = continuous)
        (0 << 0);  // CONTSC (0 = normal continuous)

    adcWriteRegister(ADC_REG_CTRL1, ctrl1);

    // Configure CTRL2:
    //   DGAIN1:0 = 00 (no extra digital gain)
    //   BUFEN = 0   (input buffer disabled; adjust later if needed)
    //   LPMODE = 0  (normal power mode; we want speed)
    //   PGAEN = 1   (enable PGA)
    //   PGAG[2:0] = pgaGainCode (x1..x128 depending on enum)
    uint8_t ctrl2 =
        (0 << 7) |                    // DGAIN1
        (0 << 6) |                    // DGAIN0
        (0 << 5) |                    // BUFEN
        (0 << 4) |                    // LPMODE
        (1 << 3) |                    // PGAEN
        (pgaGainCode & 0x07);         // PGAG2:0

    adcWriteRegister(ADC_REG_CTRL2, ctrl2);

    // CTRL3, CTRL4, CTRL5: leave at defaults for now.

    return true;
}

bool adcStartContinuous(uint8_t rateCode)
{
    // Ensure SCYCLE=0 in CTRL1 (continuous). We already set that in adcInit().
    // Just issue a conversion command with RATE bits.
    uint8_t cmd = buildConversionCommand(rateCode & 0x0F, false, false);

    adcSelect();
    adcSpi.beginTransaction(SPISettings(ADC_SPI_CLOCK_HZ, ADC_SPI_BIT_ORDER, ADC_SPI_MODE));
    adcSpi.transfer(cmd);
    adcSpi.endTransaction();
    adcDeselect();

    // Give first conversion time to complete; at 64 ksps this is ~15.6 µs.
    delayMicroseconds(100);

    return true;
}

bool adcIsDataReady()
{
    // RDYB is active LOW: LOW = data ready
    return (digitalRead(ADC_RDYB_PIN) == LOW);
}

bool adcReadSample(int32_t &code)
{
    uint32_t raw24 = 0;
    if (!adcReadRegister24(ADC_REG_DATA, raw24))
    {
        return false;
    }

    code = signExtend24(raw24);
    return true;
}
