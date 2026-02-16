#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

/**
 * MAX11270 driver (ESP-IDF SPI master).
 *
 * Design goals:
 *  - Correct command/register bitfields per datasheet
 *  - Deterministic reads at high sample rates (use spi_device_polling_transmit)
 *  - RDYB-driven acquisition (polling) suitable for 64 ksps
 *
 * Notes:
 *  - For 64 ksps continuous: set CTRL1.SCYCLE=0 and send conversion command with RATE=1111.
 *  - Read data via register-access read of DATA register; if CTRL3.DATA32=1, read 4 bytes.
 */
class Max11270 {
public:
  // Register addresses (RS[4:0]) per datasheet
  enum class Reg : uint8_t {
    STAT     = 0x00, // 16-bit
    CTRL1    = 0x01, // 8-bit
    CTRL2    = 0x02, // 8-bit
    CTRL3    = 0x03, // 8-bit
    CTRL4    = 0x04, // 8-bit
    CTRL5    = 0x05, // 8-bit
    DATA     = 0x06, // 24 or 32-bit depending on CTRL3.DATA32
    SOC_SPI  = 0x07, // 24-bit
    SGC_SPI  = 0x08, // 24-bit
    SCOC_SPI = 0x09, // 24-bit
    SCGC_SPI = 0x0A, // 24-bit
    RAM      = 0x0C, // special
    SYNC_SPI = 0x0D, // special
  };

  // RATE[3:0] values per datasheet table
  enum class Rate : uint8_t {
    R_1P9SPS   = 0x0,
    R_3P9SPS   = 0x1,
    R_7P8SPS   = 0x2,
    R_15P6SPS  = 0x3,
    R_31P2SPS  = 0x4,
    R_62P5SPS  = 0x5,
    R_125SPS   = 0x6,
    R_250SPS   = 0x7,
    R_500SPS   = 0x8,
    R_1000SPS  = 0x9,
    R_2000SPS  = 0xA,
    R_4000SPS  = 0xB,
    R_8000SPS  = 0xC,
    R_16000SPS = 0xD,
    R_32000SPS = 0xE,
    R_64000SPS = 0xF,
  };

  enum class InputRange : uint8_t {
    Bipolar,   // ±Vref
    Unipolar,  // 0..Vref
  };

  enum class BipolarFormat : uint8_t {
    TwosComplement, // CTRL1.FORMAT = 0 (bipolar only)
    OffsetBinary,   // CTRL1.FORMAT = 1 (bipolar only)
  };

  enum class PgaGain : uint8_t {
    X1   = 0,
    X2   = 1,
    X4   = 2,
    X8   = 3,
    X16  = 4,
    X32  = 5,
    X64  = 6,
    X128 = 7,
  };

  enum class DigitalGain : uint8_t {
    X1 = 0,
    X2 = 1,
    X4 = 2,
    X8 = 3,
  };

  enum class PowerDownMode : uint8_t {
    Standby, // CTRL1.PD=10, then send IMPD=1
    Sleep,   // CTRL1.PD=01, then send IMPD=1
  };

  struct SpiPins {
    gpio_num_t mosi = GPIO_NUM_NC;
    gpio_num_t miso = GPIO_NUM_NC;
    gpio_num_t sclk = GPIO_NUM_NC;
    gpio_num_t cs   = GPIO_NUM_NC;
  };

  struct GpioPins {
    gpio_num_t rdyb = GPIO_NUM_NC; // active low
    gpio_num_t rstb = GPIO_NUM_NC; // optional, active low
    gpio_num_t sync = GPIO_NUM_NC; // optional, active high reset pulse (usually held low)
  };

  struct BusConfig {
    spi_host_device_t host = SPI2_HOST;
    int dma_chan = SPI_DMA_CH_AUTO;   // IDF v4.4+ accepts AUTO; adjust if needed
    int clock_hz = 5'000'000;         // MAX11270 SCLK max is 5MHz
    bool init_bus = true;             // if false, assumes spi_bus_initialize already done
    int queue_size = 1;               // polling driver doesn't need large queue
  };

  struct Settings {
    Rate rate = Rate::R_64000SPS;
    InputRange range = InputRange::Bipolar;
    BipolarFormat bipolar_format = BipolarFormat::TwosComplement;

    bool continuous_conversion = true;   // for SCYCLE=0 continuous mode
    bool data32 = true;                  // CTRL3.DATA32
    bool use_internal_clock = true;      // CTRL1.EXTCK=0

    bool enable_buffer = false;          // CTRL2.BUFEN
    bool enable_pga = true;              // CTRL2.PGAEN
    PgaGain pga_gain = PgaGain::X128;    // typical for load cell
    bool pga_low_power = false;          // CTRL2.LPMODE

    DigitalGain digital_gain = DigitalGain::X1; // CTRL2.DGAIN

    // Calibration coefficients usage (CTRL5 NOS* bits)
    bool use_self_cal_offset = true;
    bool use_self_cal_gain   = true;
    bool use_system_offset   = false;
    bool use_system_gain     = false;
  };

  struct Sample {
    int32_t code = 0;        // 32-bit result if data32 enabled and bipolar twos complement
    uint64_t t_us = 0;       // timestamp captured immediately after RDYB low (best-effort)
  };

  struct Status {
    uint16_t raw = 0;
    bool rdy = false;      // RDY bit (redundant to RDYB)
    bool rderr = false;    // read collision
    bool dor = false;      // overrange (clipped)
    bool aor = false;      // analog overrange
    bool mstat = false;    // modulator busy
    uint8_t rate = 0;      // previous conversion rate
  };

  Max11270() = default;
  ~Max11270();

  Max11270(const Max11270&) = delete;
  Max11270& operator=(const Max11270&) = delete;

  // One-shot init: initialize SPI bus (optional), add device, configure GPIOs
  esp_err_t begin(const BusConfig& bus_cfg, const SpiPins& spi_pins, const GpioPins& gpio_pins);

  // Hardware reset via RSTB pin (if provided)
  esp_err_t hardwareReset(uint32_t low_ms = 2, uint32_t post_ms = 5);

  // Software reset (CTRL1.PD=11)
  esp_err_t softwareReset(uint32_t post_ms = 5);

  // Write control registers and basic operating mode
  esp_err_t configure(const Settings& s);

  // Start conversions at given rate (conversion-mode command)
  esp_err_t startConversions(Rate rate);

  // Stop conversions and enter standby/sleep using IMPD=1 + CTRL1.PD selection
  esp_err_t stopAndPowerDown(PowerDownMode mode);

  // Read a sample (waits for RDYB low, then reads DATA register).
  // timeout_us = 0 => wait forever (tight loop). For 64ksps use 0.
  esp_err_t readSampleBlocking(Sample* out, uint32_t timeout_us = 0);

  // Read the 16-bit status register
  esp_err_t readStatus(Status* out);

  // Self calibration (CTRL5.CAL=00, command CAL=1). Polls MSTAT to completion.
  esp_err_t selfCalibrate(uint32_t timeout_ms = 500);

  // Helpers to convert code to volts (bipolar, twos complement, N=32).
  // volts ˜ code / 2^31 * Vref. (-Vref is exact at code=-2^31; +Vref is 1 LSB below).
  static double codeToVoltsBipolarTwosComp32(int32_t code, double vref_volts);

  // For load cell convenience: µV/V = (V_in / V_exc) * 1e6
  static double codeTo_uV_per_V(int32_t code, double vref_volts, double excitation_volts);

  // Raw register access
  esp_err_t writeReg8(Reg r, uint8_t v);
  esp_err_t readReg8(Reg r, uint8_t* out);
  esp_err_t readReg16(Reg r, uint16_t* out);
  esp_err_t readReg24(Reg r, uint32_t* out24);
  esp_err_t readData32(int32_t* out_code);

  bool isReady() const { return initialized_; }

private:
  // Command helpers (per datasheet)
  static uint8_t cmdConversion(bool cal, bool impd, Rate rate);               // MODE=0
  static uint8_t cmdRegAccess(Reg r, bool read);                               // MODE=1

  esp_err_t transmit(const uint8_t* tx, uint8_t* rx, size_t nbytes);

  esp_err_t configureGpios_();

  static uint64_t nowMicros_();

  // Pins/config
  BusConfig bus_cfg_{};
  SpiPins spi_pins_{};
  GpioPins gpio_pins_{};

  // SPI handle
  spi_device_handle_t dev_ = nullptr;

  // State
  bool initialized_ = false;
  Settings settings_{};

  // scratch
  uint8_t txbuf_[8] = {0};
  uint8_t rxbuf_[8] = {0};
};

