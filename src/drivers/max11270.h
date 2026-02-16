#pragma once

#include <cstdint>
#include <cstddef>

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC ((gpio_num_t)-1)
#endif

class Max11270 {
public:
  enum class Reg : uint8_t {
    STAT     = 0x00,
    CTRL1    = 0x01,
    CTRL2    = 0x02,
    CTRL3    = 0x03,
    CTRL4    = 0x04,
    CTRL5    = 0x05,
    DATA     = 0x06,
    SOC_SPI  = 0x07,
    SGC_SPI  = 0x08,
    SCOC_SPI = 0x09,
    SCGC_SPI = 0x0A,
    RAM      = 0x0C,
    SYNC_SPI = 0x0D,
  };

  enum class Rate : uint8_t {
    R_1P9SPS   = 0x0,  R_3P9SPS   = 0x1,  R_7P8SPS   = 0x2,  R_15P6SPS  = 0x3,
    R_31P2SPS  = 0x4,  R_62P5SPS  = 0x5,  R_125SPS   = 0x6,  R_250SPS   = 0x7,
    R_500SPS   = 0x8,  R_1000SPS  = 0x9,  R_2000SPS  = 0xA,  R_4000SPS  = 0xB,
    R_8000SPS  = 0xC,  R_16000SPS = 0xD,  R_32000SPS = 0xE,  R_64000SPS = 0xF,
  };

  enum class InputRange : uint8_t { Bipolar, Unipolar };
  enum class BipolarFormat : uint8_t { TwosComplement, OffsetBinary };
  enum class PgaGain : uint8_t { X1=0, X2=1, X4=2, X8=3, X16=4, X32=5, X64=6, X128=7 };
  enum class DigitalGain : uint8_t { X1=0, X2=1, X4=2, X8=3 };
  enum class PowerDownMode : uint8_t { Standby, Sleep };

  struct SpiPins { gpio_num_t mosi=GPIO_NUM_NC, miso=GPIO_NUM_NC, sclk=GPIO_NUM_NC, cs=GPIO_NUM_NC; };
  struct GpioPins { gpio_num_t rdyb=GPIO_NUM_NC, rstb=GPIO_NUM_NC, sync=GPIO_NUM_NC; };

  struct BusConfig {
    spi_host_device_t host = SPI2_HOST;
    int dma_chan = SPI_DMA_CH_AUTO;
    int clock_hz = 5'000'000;
    bool init_bus = true;
    int queue_size = 1;
  };

  struct Settings {
    Rate rate = Rate::R_64000SPS;
    InputRange range = InputRange::Bipolar;
    BipolarFormat bipolar_format = BipolarFormat::TwosComplement;

    bool continuous_conversion = true;
    bool data32 = true;
    bool use_internal_clock = true;

    bool enable_buffer = false;
    bool enable_pga = true;
    PgaGain pga_gain = PgaGain::X128;
    bool pga_low_power = false;
    DigitalGain digital_gain = DigitalGain::X1;

    bool use_self_cal_offset = true;
    bool use_self_cal_gain   = true;
    bool use_system_offset   = false;
    bool use_system_gain     = false;
  };

  struct Sample { int32_t code=0; uint64_t t_us=0; };
  struct Status { uint16_t raw=0; bool rdy=false, rderr=false, dor=false, aor=false, mstat=false; uint8_t rate=0; };

  Max11270() = default;
  ~Max11270();
  Max11270(const Max11270&) = delete;
  Max11270& operator=(const Max11270&) = delete;

  esp_err_t begin(const BusConfig& bus_cfg, const SpiPins& spi_pins, const GpioPins& gpio_pins);
  esp_err_t hardwareReset(uint32_t low_ms=2, uint32_t post_ms=5);
  esp_err_t softwareReset(uint32_t post_ms=5);

  esp_err_t configure(const Settings& s);
  esp_err_t startConversions(Rate rate);
  esp_err_t stopAndPowerDown(PowerDownMode mode);

  esp_err_t readSampleBlocking(Sample* out, uint32_t timeout_us=0);
  esp_err_t readStatus(Status* out);
  esp_err_t selfCalibrate(uint32_t timeout_ms=500);

  static double codeToVoltsBipolarTwosComp32(int32_t code, double vref_volts);
  static double codeTo_uV_per_V(int32_t code, double vref_volts, double excitation_volts);

  esp_err_t writeReg8(Reg r, uint8_t v);
  esp_err_t readReg8(Reg r, uint8_t* out);
  esp_err_t readReg16(Reg r, uint16_t* out);
  esp_err_t readReg24(Reg r, uint32_t* out24);
  esp_err_t readData32(int32_t* out_code);

private:
  static uint8_t cmdConversion(bool cal, bool impd, Rate rate);
  static uint8_t cmdRegAccess(Reg r, bool read);

  esp_err_t transmit(const uint8_t* tx, uint8_t* rx, size_t nbytes);
  esp_err_t configureGpios_();
  static uint64_t nowMicros_();

  BusConfig bus_cfg_{};
  SpiPins spi_pins_{};
  GpioPins gpio_pins_{};
  spi_device_handle_t dev_ = nullptr;
  bool initialized_ = false;
  Settings settings_{};

  uint8_t txbuf_[8] = {0};
  uint8_t rxbuf_[8] = {0};
};
