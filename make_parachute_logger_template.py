import os, textwrap, zipfile, pathlib, shutil

ROOT_DIRNAME = "parachute_logger_template"
ZIP_NAME = "parachute_logger_template.zip"

def write(root: pathlib.Path, relpath: str, content: str):
    p = root / relpath
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8", newline="\n")

def main():
    root = pathlib.Path(ROOT_DIRNAME)

    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True, exist_ok=True)

    # ---------- platformio.ini ----------
    write(root, "platformio.ini", textwrap.dedent("""\
    [env:esp32s3mini]
    platform = espressif32
    board = esp32-s3-devkitc-1
    framework = arduino
    monitor_speed = 115200
    monitor_filters = esp32_exception_decoder

    build_flags =
      -DCORE_DEBUG_LEVEL=0
      -DARDUINO_USB_CDC_ON_BOOT=1

    ; Notes:
    ; - Update 'board =' if you have a custom board definition.
    ; - Wi-Fi/WebUI intentionally omitted for flight reliability.
    """))

    # ---------- README ----------
    write(root, "README.md", textwrap.dedent("""\
    # Parachute Data Logger (Template)

    Restart-friendly scaffold for a parachute data logger:

    - MAX11270 load cell ADC @ 64 ksps acquisition
    - 500 Hz frame logging to SD (binary `.BIN`)
    - Serial CLI + Windows host tool for long captures + matplotlib plotting
    - No Wi-Fi / WebUI

    ## Hardware pins (as configured)
    ADC (MAX11270) pins:
    - MISO: IO12
    - MOSI: IO13
    - SYNC: IO14
    - RSTB: IO15
    - RDYB: IO16
    - CS:   IO17
    - SCK:  IO18

    ADC CLK pin tied to GND -> internal clock (EXTCK=0).

    ## Serial CLI
    115200 baud. Commands:
    - `help`
    - `status`
    - `scope <hz>` (hz must divide 500; 10/20/25/50 recommended)
    - `scope 0`
    - `startlog` / `stoplog`

    Scope fields:
    `ms,force_mean_N,force_peak_N,accel_mag_g,flags`

    ## Windows host plotting tool
    ```bat
    pip install pyserial matplotlib numpy
    python tools\\host\\scope_plot.py
    ```

    ## BIN -> CSV for Excel
    ```bat
    python tools\\host\\bin2csv.py E:\\LOG0000.BIN E:\\LOG0000.csv
    ```

    ## Notes
    - SD logging uses `SD_MMC` by default. If your hardware uses SPI SD, adjust `src/services/sd_logger.cpp`.
    - IMU and fuel gauge tasks are stubbed (zeros) in this template; integrate your drivers next.
    """))

    # ---------- src/main.cpp ----------
    write(root, "src/main.cpp", textwrap.dedent("""\
    #include <Arduino.h>

    #include "services/adc_frames.h"
    #include "services/sd_logger.h"
    #include "services/scope_stream.h"
    #include "services/serial_cli.h"

    void setup() {
      Serial.begin(115200);
      delay(200);
      setCpuFrequencyMhz(240);

      Serial.println("# Parachute Logger Template boot");

      scope_init();
      start_cli_task();

      if (!logger_begin()) {
        Serial.println("#ERR: SD init failed (logger_begin). Logging will not work until fixed.");
      }
      start_logger_task();

      // Start ADC frame producer (64 ksps acquisition -> 500 Hz frames)
      start_adc_frames();

      Serial.println("# Ready.");
    }

    void loop() {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    """))

    # ---------- format ----------
    write(root, "src/format/pdl_flags.h", textwrap.dedent("""\
    #pragma once
    #include <cstdint>

    enum PdlFlags : uint16_t {
      FLG_OVERLOAD        = 1u << 0,
      FLG_UNDERLOAD       = 1u << 1,
      FLG_COMPRESSION     = 1u << 2,

      FLG_IMU_SAT         = 1u << 3,
      FLG_IMU_FAULT       = 1u << 4,
      FLG_RTC_INVALID     = 1u << 5,

      FLG_SD_WARN         = 1u << 6,
      FLG_SD_FAIL         = 1u << 7,

      FLG_I2C_RECOVERED   = 1u << 8,
      FLG_LOW_BATT        = 1u << 9,

      FLG_DROPPED_FRAME   = 1u << 10,
    };
    """))

    write(root, "src/format/log_format.h", textwrap.dedent("""\
    #pragma once
    #include <cstdint>

    static constexpr uint32_t PDL_MAGIC = 0x314C4450; // "PDL1" little-endian

    #pragma pack(push, 1)

    struct PdlHeaderV1 {
      uint32_t magic;          // PDL_MAGIC
      uint16_t header_ver;     // 1
      uint16_t header_size;    // sizeof(PdlHeaderV1)
      uint16_t frame_ver;      // 1
      uint16_t frame_size;     // sizeof(PdlFrameV1)
      uint32_t build_id;       // optional

      uint32_t adc_rate_hz;    // 64000
      uint32_t frame_rate_hz;  // 500
      uint32_t decim;          // 128

      uint64_t start_mono_us;  // esp_timer_get_time() at session start
      uint32_t start_rtc_epoch;// 0 if invalid
      uint32_t start_rtc_ms;   // 0 if unused

      // force_mN = slope_mN_per_code * (adc_code - tare_adc_code) + offset_mN
      float slope_mN_per_code;
      float offset_mN;

      float accel_g_per_lsb;
      float gyro_dps_per_lsb;

      int32_t overload_mN;
      int32_t underload_mN;
      int32_t compression_mN;

      uint16_t tare_frames;
      int32_t  tare_adc_code;

      uint32_t flags_static;

      uint32_t header_crc32;   // set 0 for now
      uint8_t  reserved[170];  // pad to 256 bytes
    };

    struct PdlFrameV1 {
      uint32_t sample_index;
      uint64_t t_us;           // timestamp of first ADC sample in this 2ms window

      int32_t adc_mean;
      int32_t adc_peak;
      int32_t adc_min;

      int32_t force_mean_mN;
      int32_t force_peak_mN;
      int32_t force_min_mN;

      int16_t ax, ay, az;
      int16_t gx, gy, gz;

      uint16_t flags;

      uint16_t vbat_mV;
      uint16_t soc_centiPct;

      uint16_t pad;            // keep struct at 56 bytes
    };

    #pragma pack(pop)

    static_assert(sizeof(PdlHeaderV1) == 256, "Header must be 256 bytes");
    static_assert(sizeof(PdlFrameV1) == 56, "Frame must be 56 bytes");
    """))

    # ---------- services: frame pipe ----------
    write(root, "src/services/frame_pipe.h", textwrap.dedent("""\
    #pragma once
    #include "freertos/FreeRTOS.h"
    #include "freertos/queue.h"
    #include "format/log_format.h"

    // 500 Hz frame queue produced by ADC task and consumed by logger task.
    extern QueueHandle_t g_frame_q;
    """))
    write(root, "src/services/frame_pipe.cpp", textwrap.dedent("""\
    #include "services/frame_pipe.h"
    QueueHandle_t g_frame_q = nullptr;
    """))

    # ---------- drivers: MAX11270 ----------
    write(root, "src/drivers/max11270.h", textwrap.dedent("""\
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
    """))

    write(root, "src/drivers/max11270.cpp", textwrap.dedent("""\
    #include "drivers/max11270.h"
    #include <cstring>

    #include "esp_timer.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"

    uint64_t Max11270::nowMicros_() { return static_cast<uint64_t>(esp_timer_get_time()); }

    uint8_t Max11270::cmdConversion(bool cal, bool impd, Rate rate) {
      uint8_t b = 0x80;
      if (cal)  b |= 0x20;
      if (impd) b |= 0x10;
      b |= (static_cast<uint8_t>(rate) & 0x0F);
      return b;
    }

    uint8_t Max11270::cmdRegAccess(Reg r, bool read) {
      uint8_t rs = static_cast<uint8_t>(r) & 0x1F;
      uint8_t b = 0xC0;
      b |= static_cast<uint8_t>(rs << 1);
      if (read) b |= 0x01;
      return b;
    }

    Max11270::~Max11270() {
      if (dev_) { spi_bus_remove_device(dev_); dev_ = nullptr; }
      initialized_ = false;
    }

    esp_err_t Max11270::begin(const BusConfig& bus_cfg, const SpiPins& spi_pins, const GpioPins& gpio_pins) {
      bus_cfg_ = bus_cfg; spi_pins_ = spi_pins; gpio_pins_ = gpio_pins;
      esp_err_t err;

      if (bus_cfg_.init_bus) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = spi_pins_.mosi;
        bus.miso_io_num = spi_pins_.miso;
        bus.sclk_io_num = spi_pins_.sclk;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;

        err = spi_bus_initialize(bus_cfg_.host, &bus, bus_cfg_.dma_chan);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
      }

      spi_device_interface_config_t devcfg = {};
      devcfg.clock_speed_hz = bus_cfg_.clock_hz;
      devcfg.mode = 0;
      devcfg.spics_io_num = spi_pins_.cs;
      devcfg.queue_size = bus_cfg_.queue_size;

      err = spi_bus_add_device(bus_cfg_.host, &devcfg, &dev_);
      if (err != ESP_OK) return err;

      err = configureGpios_();
      if (err != ESP_OK) return err;

      initialized_ = true;
      return ESP_OK;
    }

    esp_err_t Max11270::configureGpios_() {
      if (gpio_pins_.rdyb != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << gpio_pins_.rdyb;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        return gpio_config(&io);
      }
      if (gpio_pins_.rstb != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << gpio_pins_.rstb;
        io.mode = GPIO_MODE_OUTPUT;
        auto err = gpio_config(&io);
        if (err != ESP_OK) return err;
        gpio_set_level(gpio_pins_.rstb, 1);
      }
      if (gpio_pins_.sync != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << gpio_pins_.sync;
        io.mode = GPIO_MODE_OUTPUT;
        auto err = gpio_config(&io);
        if (err != ESP_OK) return err;
        gpio_set_level(gpio_pins_.sync, 0);
      }
      return ESP_OK;
    }

    esp_err_t Max11270::hardwareReset(uint32_t low_ms, uint32_t post_ms) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      if (gpio_pins_.rstb == GPIO_NUM_NC) return ESP_ERR_INVALID_ARG;
      gpio_set_level(gpio_pins_.rstb, 0);
      vTaskDelay(pdMS_TO_TICKS(low_ms));
      gpio_set_level(gpio_pins_.rstb, 1);
      vTaskDelay(pdMS_TO_TICKS(post_ms));
      return ESP_OK;
    }

    esp_err_t Max11270::softwareReset(uint32_t post_ms) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      uint8_t ctrl1 = (1u<<5) | (1u<<4);
      auto err = writeReg8(Reg::CTRL1, ctrl1);
      if (err != ESP_OK) return err;
      vTaskDelay(pdMS_TO_TICKS(post_ms));
      return ESP_OK;
    }

    esp_err_t Max11270::transmit(const uint8_t* tx, uint8_t* rx, size_t nbytes) {
      if (!dev_) return ESP_ERR_INVALID_STATE;
      spi_transaction_t t; std::memset(&t, 0, sizeof(t));
      t.length = static_cast<int>(nbytes * 8);
      t.tx_buffer = tx;
      t.rx_buffer = rx;
      return spi_device_polling_transmit(dev_, &t);
    }

    esp_err_t Max11270::writeReg8(Reg r, uint8_t v) {
      txbuf_[0] = cmdRegAccess(r, false);
      txbuf_[1] = v;
      return transmit(txbuf_, nullptr, 2);
    }

    esp_err_t Max11270::readReg8(Reg r, uint8_t* out) {
      if (!out) return ESP_ERR_INVALID_ARG;
      txbuf_[0] = cmdRegAccess(r, true);
      txbuf_[1] = 0x00;
      auto err = transmit(txbuf_, rxbuf_, 2);
      if (err != ESP_OK) return err;
      *out = rxbuf_[1];
      return ESP_OK;
    }

    esp_err_t Max11270::readReg16(Reg r, uint16_t* out) {
      if (!out) return ESP_ERR_INVALID_ARG;
      txbuf_[0] = cmdRegAccess(r, true);
      txbuf_[1] = 0x00; txbuf_[2] = 0x00;
      auto err = transmit(txbuf_, rxbuf_, 3);
      if (err != ESP_OK) return err;
      *out = (uint16_t(rxbuf_[1])<<8) | uint16_t(rxbuf_[2]);
      return ESP_OK;
    }

    esp_err_t Max11270::readReg24(Reg r, uint32_t* out24) {
      if (!out24) return ESP_ERR_INVALID_ARG;
      txbuf_[0] = cmdRegAccess(r, true);
      txbuf_[1] = 0x00; txbuf_[2] = 0x00; txbuf_[3] = 0x00;
      auto err = transmit(txbuf_, rxbuf_, 4);
      if (err != ESP_OK) return err;
      *out24 = (uint32_t(rxbuf_[1])<<16) | (uint32_t(rxbuf_[2])<<8) | uint32_t(rxbuf_[3]);
      return ESP_OK;
    }

    esp_err_t Max11270::readData32(int32_t* out_code) {
      if (!out_code) return ESP_ERR_INVALID_ARG;
      txbuf_[0] = cmdRegAccess(Reg::DATA, true);
      txbuf_[1]=txbuf_[2]=txbuf_[3]=txbuf_[4]=0x00;
      auto err = transmit(txbuf_, rxbuf_, 5);
      if (err != ESP_OK) return err;
      uint32_t u = (uint32_t(rxbuf_[1])<<24) | (uint32_t(rxbuf_[2])<<16) | (uint32_t(rxbuf_[3])<<8) | uint32_t(rxbuf_[4]);
      *out_code = int32_t(u);
      return ESP_OK;
    }

    esp_err_t Max11270::configure(const Settings& s) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      settings_ = s;

      uint8_t ctrl1 = 0;
      if (!s.use_internal_clock) ctrl1 |= (1u<<7);
      if (s.range == InputRange::Unipolar) ctrl1 |= (1u<<3);
      else if (s.bipolar_format == BipolarFormat::OffsetBinary) ctrl1 |= (1u<<2);

      // continuous => SCYCLE=0
      if (!s.continuous_conversion) ctrl1 |= (1u<<1);

      uint8_t ctrl2 = 0;
      ctrl2 |= (uint8_t(s.digital_gain)&0x03) << 6;
      if (s.enable_buffer) ctrl2 |= (1u<<5);
      if (s.pga_low_power) ctrl2 |= (1u<<4);
      if (s.enable_pga)    ctrl2 |= (1u<<3);
      ctrl2 |= (uint8_t(s.pga_gain) & 0x07);

      uint8_t ctrl3 = 0x61;
      if (s.data32) ctrl3 |= (1u<<3);

      uint8_t ctrl5 = 0;
      if (!s.use_system_gain)     ctrl5 |= (1u<<3);
      if (!s.use_system_offset)   ctrl5 |= (1u<<2);
      if (!s.use_self_cal_gain)   ctrl5 |= (1u<<1);
      if (!s.use_self_cal_offset) ctrl5 |= (1u<<0);

      esp_err_t err;
      if ((err = writeReg8(Reg::CTRL1, ctrl1)) != ESP_OK) return err;
      if ((err = writeReg8(Reg::CTRL2, ctrl2)) != ESP_OK) return err;
      if ((err = writeReg8(Reg::CTRL3, ctrl3)) != ESP_OK) return err;
      if ((err = writeReg8(Reg::CTRL5, ctrl5)) != ESP_OK) return err;
      return ESP_OK;
    }

    esp_err_t Max11270::startConversions(Rate rate) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      txbuf_[0] = cmdConversion(false, false, rate);
      return transmit(txbuf_, nullptr, 1);
    }

    esp_err_t Max11270::stopAndPowerDown(PowerDownMode mode) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      uint8_t ctrl1; auto err = readReg8(Reg::CTRL1, &ctrl1);
      if (err != ESP_OK) return err;
      ctrl1 &= ~((1u<<5)|(1u<<4));
      ctrl1 |= (mode == PowerDownMode::Sleep) ? (1u<<4) : (1u<<5);
      if ((err = writeReg8(Reg::CTRL1, ctrl1)) != ESP_OK) return err;
      txbuf_[0] = cmdConversion(false, true, settings_.rate);
      return transmit(txbuf_, nullptr, 1);
    }

    esp_err_t Max11270::readSampleBlocking(Sample* out, uint32_t timeout_us) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      if (!out) return ESP_ERR_INVALID_ARG;
      if (gpio_pins_.rdyb == GPIO_NUM_NC) return ESP_ERR_INVALID_STATE;

      uint64_t t0 = nowMicros_();
      while (gpio_get_level(gpio_pins_.rdyb) != 0) {
        if (timeout_us && (nowMicros_() - t0) >= timeout_us) return ESP_ERR_TIMEOUT;
      }
      out->t_us = nowMicros_();
      int32_t code; auto err = readData32(&code);
      if (err != ESP_OK) return err;
      out->code = code;
      return ESP_OK;
    }

    esp_err_t Max11270::readStatus(Status* out) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;
      if (!out) return ESP_ERR_INVALID_ARG;
      uint16_t raw; auto err = readReg16(Reg::STAT, &raw);
      if (err != ESP_OK) return err;

      out->raw = raw;
      out->rdy   = (raw & (1u<<0)) != 0;
      out->mstat = (raw & (1u<<1)) != 0;
      out->dor   = (raw & (1u<<2)) != 0;
      out->rate  = uint8_t((raw >> 4) & 0x0F);
      out->aor   = (raw & (1u<<8)) != 0;
      out->rderr = (raw & (1u<<9)) != 0;
      return ESP_OK;
    }

    esp_err_t Max11270::selfCalibrate(uint32_t timeout_ms) {
      if (!initialized_) return ESP_ERR_INVALID_STATE;

      uint8_t ctrl5; auto err = readReg8(Reg::CTRL5, &ctrl5);
      if (err != ESP_OK) return err;
      ctrl5 &= ~((1u<<7)|(1u<<6)); // self-cal
      if ((err = writeReg8(Reg::CTRL5, ctrl5)) != ESP_OK) return err;

      txbuf_[0] = cmdConversion(true, false, Rate::R_1000SPS);
      if ((err = transmit(txbuf_, nullptr, 1)) != ESP_OK) return err;

      uint64_t t0 = nowMicros_();
      while (true) {
        Status st; if ((err = readStatus(&st)) != ESP_OK) return err;
        if (!st.mstat) break;
        if ((nowMicros_() - t0) >= uint64_t(timeout_ms) * 1000ULL) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      return ESP_OK;
    }

    double Max11270::codeToVoltsBipolarTwosComp32(int32_t code, double vref_volts) {
      constexpr double denom = 2147483648.0;
      return (double(code) / denom) * vref_volts;
    }

    double Max11270::codeTo_uV_per_V(int32_t code, double vref_volts, double excitation_volts) {
      if (excitation_volts <= 0.0) return 0.0;
      double vin = codeToVoltsBipolarTwosComp32(code, vref_volts);
      return (vin / excitation_volts) * 1e6;
    }
    """))

    # ---------- services: adc frames ----------
    write(root, "src/services/adc_frames.h", textwrap.dedent("""\
    #pragma once
    void start_adc_frames();
    """))

    write(root, "src/services/adc_frames.cpp", textwrap.dedent("""\
    #include <Arduino.h>
    #include <limits>

    #include "drivers/max11270.h"
    #include "format/log_format.h"
    #include "format/pdl_flags.h"
    #include "services/frame_pipe.h"

    static constexpr int ADC_HZ = 64000;
    static constexpr int FRAME_HZ = 500;
    static constexpr int DECIM = ADC_HZ / FRAME_HZ; // 128

    static Max11270 adc;

    // Placeholder calibration: force_mN = slope * (code - tare) + offset
    static float g_slope_mN_per_code = 1.0f;
    static float g_offset_mN = 0.0f;
    static int32_t g_tare_code = 0;

    struct LatestAux {
      int16_t ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
      uint16_t vbat_mV=0;
      uint16_t soc_centiPct=0;
    };
    static LatestAux g_aux;

    static inline int32_t code_to_force_mN(int32_t code) {
      float f = g_slope_mN_per_code * (float)(code - g_tare_code) + g_offset_mN;
      if (f > (float)INT32_MAX) return INT32_MAX;
      if (f < (float)INT32_MIN) return INT32_MIN;
      return (int32_t)lroundf(f);
    }

    static void adc_frame_task(void*) {
      Max11270::BusConfig bus { .host=SPI2_HOST, .dma_chan=SPI_DMA_CH_AUTO, .clock_hz=5'000'000, .init_bus=true, .queue_size=1 };
      Max11270::SpiPins spi { .mosi=GPIO_NUM_13, .miso=GPIO_NUM_12, .sclk=GPIO_NUM_18, .cs=GPIO_NUM_17 };
      Max11270::GpioPins gp { .rdyb=GPIO_NUM_16, .rstb=GPIO_NUM_15, .sync=GPIO_NUM_14 };

      ESP_ERROR_CHECK(adc.begin(bus, spi, gp));
      ESP_ERROR_CHECK(adc.hardwareReset(2, 5));
      ESP_ERROR_CHECK(adc.softwareReset(5));

      Max11270::Settings s;
      s.rate = Max11270::Rate::R_64000SPS;
      s.use_internal_clock = true;      // ADC CLK pin grounded -> internal clock required
      s.continuous_conversion = true;   // SCYCLE=0
      s.data32 = true;
      s.enable_pga = true;
      s.pga_gain = Max11270::PgaGain::X128;

      ESP_ERROR_CHECK(adc.configure(s));
      ESP_ERROR_CHECK(adc.selfCalibrate());
      ESP_ERROR_CHECK(adc.startConversions(s.rate));

      uint32_t frame_idx = 0;

      while (true) {
        int64_t sum = 0;
        int32_t mn = INT32_MAX;
        int32_t mx = INT32_MIN;
        uint64_t t_first = 0;

        for (int i=0; i<DECIM; i++) {
          Max11270::Sample smp;
          ESP_ERROR_CHECK(adc.readSampleBlocking(&smp, 0));
          if (i == 0) t_first = smp.t_us;

          const int32_t code = smp.code;
          sum += code;
          if (code < mn) mn = code;
          if (code > mx) mx = code;
        }

        const int32_t mean = (int32_t)(sum / DECIM);

        PdlFrameV1 fr{};
        fr.sample_index = frame_idx++;
        fr.t_us = t_first;

        fr.adc_mean = mean;
        fr.adc_peak = mx;
        fr.adc_min  = mn;

        fr.force_mean_mN = code_to_force_mN(mean);
        fr.force_peak_mN = code_to_force_mN(mx);
        fr.force_min_mN  = code_to_force_mN(mn);

        fr.ax = g_aux.ax; fr.ay = g_aux.ay; fr.az = g_aux.az;
        fr.gx = g_aux.gx; fr.gy = g_aux.gy; fr.gz = g_aux.gz;
        fr.vbat_mV = g_aux.vbat_mV;
        fr.soc_centiPct = g_aux.soc_centiPct;

        fr.flags = 0;
        fr.pad = 0;

        if (g_frame_q) (void)xQueueSend(g_frame_q, &fr, 0);
      }
    }

    void start_adc_frames() {
      g_frame_q = xQueueCreate(600, sizeof(PdlFrameV1));
      xTaskCreatePinnedToCore(adc_frame_task, "adc_frame", 6144, nullptr, configMAX_PRIORITIES-2, nullptr, 1);
    }
    """))

    # ---------- services: scope stream ----------
    write(root, "src/services/scope_stream.h", textwrap.dedent("""\
    #pragma once
    #include <cstdint>
    #include "format/log_format.h"
    #include "freertos/queue.h"

    struct ScopeSample {
      uint32_t ms;
      float force_mean_N;
      float force_peak_N;
      float accel_mag_g;
      uint16_t flags;
    };

    void scope_init();
    void scope_set_rate(uint16_t hz); // 0 stops; hz must divide 500
    bool scope_is_enabled();

    void scope_feed_frame(const PdlFrameV1& fr);

    extern QueueHandle_t g_scope_q;

    void scope_set_accel_scale(float accel_g_per_lsb);
    """))

    write(root, "src/services/scope_stream.cpp", textwrap.dedent("""\
    #include "services/scope_stream.h"
    #include <cmath>
    #include <limits>

    QueueHandle_t g_scope_q = nullptr;

    static bool g_enabled = false;
    static uint16_t g_N = 20; // default for 25Hz
    static float g_accel_g_per_lsb = 1.0f / 16384.0f;

    struct Agg {
      uint16_t n=0;
      int64_t sum_mean_mN=0;
      int32_t max_peak_mN=INT32_MIN;
      float max_acc_g=0.0f;
      uint16_t flags_or=0;
      uint32_t last_ms=0;
      void reset() {
        n=0; sum_mean_mN=0; max_peak_mN=INT32_MIN; max_acc_g=0.0f; flags_or=0; last_ms=0;
      }
    } a;

    void scope_init() { g_scope_q = xQueueCreate(1, sizeof(ScopeSample)); a.reset(); }
    void scope_set_accel_scale(float s) { g_accel_g_per_lsb = s; }

    void scope_set_rate(uint16_t hz) {
      if (hz == 0) { g_enabled = false; a.reset(); return; }
      if ((500 % hz) != 0) return;
      g_N = 500 / hz;
      a.reset();
      g_enabled = true;
    }

    bool scope_is_enabled() { return g_enabled; }

    void scope_feed_frame(const PdlFrameV1& fr) {
      if (!g_enabled) return;

      a.sum_mean_mN += fr.force_mean_mN;
      if (fr.force_peak_mN > a.max_peak_mN) a.max_peak_mN = fr.force_peak_mN;

      float ax = fr.ax * g_accel_g_per_lsb;
      float ay = fr.ay * g_accel_g_per_lsb;
      float az = fr.az * g_accel_g_per_lsb;
      float mag = sqrtf(ax*ax + ay*ay + az*az);
      if (mag > a.max_acc_g) a.max_acc_g = mag;

      a.flags_or |= fr.flags;
      a.last_ms = (uint32_t)(fr.t_us / 1000ULL);

      a.n++;
      if (a.n >= g_N) {
        ScopeSample s;
        s.ms = a.last_ms;
        s.force_mean_N = (float)((a.sum_mean_mN / (double)g_N) / 1000.0);
        s.force_peak_N = (float)(a.max_peak_mN / 1000.0);
        s.accel_mag_g = a.max_acc_g;
        s.flags = a.flags_or;

        a.reset();
        if (g_scope_q) xQueueOverwrite(g_scope_q, &s);
      }
    }
    """))

    # ---------- services: sd logger ----------
    write(root, "src/services/sd_logger.h", textwrap.dedent("""\
    #pragma once
    #include <cstdint>
    #include "format/log_format.h"

    bool logger_begin();
    void start_logger_task();

    bool logger_start_session(const PdlHeaderV1& hdr);
    void logger_stop_session();
    bool logger_is_logging();

    PdlHeaderV1 logger_make_default_header();
    """))

    write(root, "src/services/sd_logger.cpp", textwrap.dedent("""\
    #include "services/sd_logger.h"

    #include <Arduino.h>
    #include "FS.h"
    #include "SD_MMC.h"

    #include "services/frame_pipe.h"
    #include "services/scope_stream.h"

    static File g_file;
    static volatile bool g_logging = false;
    static uint32_t next_log_id = 0;

    static String make_filename() {
      char buf[32];
      snprintf(buf, sizeof(buf), "/LOG%04u.BIN", next_log_id++);
      return String(buf);
    }

    PdlHeaderV1 logger_make_default_header() {
      PdlHeaderV1 h{};
      h.magic = PDL_MAGIC;
      h.header_ver = 1;
      h.header_size = sizeof(PdlHeaderV1);
      h.frame_ver = 1;
      h.frame_size = sizeof(PdlFrameV1);

      h.adc_rate_hz = 64000;
      h.frame_rate_hz = 500;
      h.decim = 128;

      h.start_mono_us = (uint64_t)esp_timer_get_time();

      h.slope_mN_per_code = 1.0f;
      h.offset_mN = 0.0f;

      h.accel_g_per_lsb = 1.0f / 16384.0f;
      h.gyro_dps_per_lsb = 1.0f;

      memset(h.reserved, 0, sizeof(h.reserved));
      return h;
    }

    bool logger_begin() {
      // 1-bit mode often more tolerant; change to false for 4-bit if your wiring supports it.
      if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("#ERR: SD_MMC.begin failed");
        return false;
      }
      return true;
    }

    bool logger_is_logging() { return g_logging; }

    bool logger_start_session(const PdlHeaderV1& hdr) {
      if (g_logging) return false;

      String fn = make_filename();
      g_file = SD_MMC.open(fn, FILE_WRITE);
      if (!g_file) {
        Serial.println("#ERR: open log file failed");
        return false;
      }

      size_t n = g_file.write((const uint8_t*)&hdr, sizeof(hdr));
      if (n != sizeof(hdr)) {
        Serial.println("#ERR: header write failed");
        g_file.close();
        return false;
      }

      g_file.flush();
      g_logging = true;

      Serial.print("#LOGFILE: ");
      Serial.println(fn);
      return true;
    }

    void logger_stop_session() {
      if (!g_logging) return;
      g_logging = false;
      g_file.flush();
      g_file.close();
      Serial.println("#LOGSTOP");
    }

    static void logger_task(void*) {
      uint32_t last_flush_ms = millis();

      while (true) {
        PdlFrameV1 fr;
        if (g_frame_q && xQueueReceive(g_frame_q, &fr, pdMS_TO_TICKS(200)) == pdTRUE) {
          if (g_logging) {
            size_t n = g_file.write((const uint8_t*)&fr, sizeof(fr));
            if (n != sizeof(fr)) {
              Serial.println("#ERR: SD write failed");
            } else {
              // proxy-to-SD: only feed scope from frames actually written
              scope_feed_frame(fr);
            }

            uint32_t now = millis();
            if (now - last_flush_ms > 1000) {
              g_file.flush();
              last_flush_ms = now;
            }
          }
        }
      }
    }

    void start_logger_task() {
      xTaskCreatePinnedToCore(logger_task, "sd_logger", 6144, nullptr, configMAX_PRIORITIES-4, nullptr, 0);
    }
    """))

    # ---------- services: serial CLI ----------
    write(root, "src/services/serial_cli.h", textwrap.dedent("""\
    #pragma once
    void start_cli_task();
    """))

    write(root, "src/services/serial_cli.cpp", textwrap.dedent("""\
    #include "services/serial_cli.h"

    #include <Arduino.h>
    #include <cstring>

    #include "services/sd_logger.h"
    #include "services/scope_stream.h"

    static char linebuf[96];
    static size_t linelen = 0;

    static void print_help() {
      Serial.println("# Commands:");
      Serial.println("#  status");
      Serial.println("#  scope <hz>   (hz must divide 500; 10/20/25/50 recommended)");
      Serial.println("#  scope 0      (stop)");
      Serial.println("#  startlog");
      Serial.println("#  stoplog");
    }

    static void cli_task(void*) {
      Serial.println("#CLI ready.");
      print_help();

      bool header_sent = false;

      while (true) {
        while (Serial.available()) {
          char c = (char)Serial.read();
          if (c == '\r') continue;
          if (c == '\n') {
            linebuf[linelen] = 0;

            if (strcmp(linebuf, "help") == 0) {
              print_help();
            } else if (strcmp(linebuf, "status") == 0) {
              Serial.printf("#status: logging=%d\\n", (int)logger_is_logging());
            } else if (strncmp(linebuf, "scope", 5) == 0) {
              int hz = 0;
              sscanf(linebuf, "scope %d", &hz);
              if (hz == 0) {
                scope_set_rate(0);
                Serial.println("#scope stopped");
              } else if (500 % hz != 0) {
                Serial.println("#ERR: scope_hz must divide 500");
              } else {
                scope_set_rate((uint16_t)hz);
                header_sent = false;
                Serial.printf("#scope started %d Hz\\n", hz);
              }
            } else if (strcmp(linebuf, "startlog") == 0) {
              auto hdr = logger_make_default_header();
              if (logger_start_session(hdr)) Serial.println("#log started");
              else Serial.println("#ERR: log start failed");
            } else if (strcmp(linebuf, "stoplog") == 0) {
              logger_stop_session();
            } else if (linelen != 0) {
              Serial.println("#ERR: unknown cmd (type help)");
            }

            linelen = 0;
          } else if (linelen < sizeof(linebuf)-1) {
            linebuf[linelen++] = c;
          }
        }

        if (scope_is_enabled()) {
          if (!header_sent) {
            Serial.println("#fields: ms,force_mean_N,force_peak_N,accel_mag_g,flags");
            header_sent = true;
          }

          ScopeSample s;
          if (g_scope_q && xQueueReceive(g_scope_q, &s, 0) == pdTRUE) {
            Serial.printf("%u,%.3f,%.3f,%.3f,0x%04X\\n",
                          s.ms, s.force_mean_N, s.force_peak_N, s.accel_mag_g, s.flags);
          }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }

    void start_cli_task() {
      xTaskCreatePinnedToCore(cli_task, "cli", 4096, nullptr, 2, nullptr, 0);
    }
    """))

    # ---------- services: scope + frame globals ----------
    write(root, "src/services/frame_pipe.cpp", textwrap.dedent("""\
    #include "services/frame_pipe.h"
    QueueHandle_t g_frame_q = nullptr;
    """))

    # ---------- host tools ----------
    write(root, "tools/host/scope_plot.py", textwrap.dedent("""\
    import time, csv
    from collections import deque

    import numpy as np
    import serial
    from serial.tools import list_ports
    import matplotlib.pyplot as plt

    BAUD = 115200
    SCOPE_HZ = 25
    WINDOW_S = 30
    OUTFILE = f"scope_{int(time.time())}.csv"

    def choose_port():
        ports = list(list_ports.comports())
        if not ports:
            raise RuntimeError("No serial ports found.")
        for p in ports:
            d = (p.description or "").upper()
            if "USB" in d or "CP210" in d or "CH340" in d or "JTAG" in d:
                return p.device
        return ports[0].device

    def main():
        port = choose_port()
        print(f"Opening {port} @ {BAUD}")
        ser = serial.Serial(port, BAUD, timeout=1)

        ser.write(f"scope {SCOPE_HZ}\\n".encode("ascii"))

        f = open(OUTFILE, "w", newline="")
        w = csv.writer(f)
        w.writerow(["ms","force_mean_N","force_peak_N","accel_mag_g","flags"])

        maxlen = int(WINDOW_S * SCOPE_HZ * 2)
        t = deque(maxlen=maxlen)
        meanN = deque(maxlen=maxlen)
        peakN = deque(maxlen=maxlen)
        amag = deque(maxlen=maxlen)

        plt.ion()
        fig, ax = plt.subplots()
        l1, = ax.plot([], [], label="force_mean (N)")
        l2, = ax.plot([], [], label="force_peak (N)")
        l3, = ax.plot([], [], label="accel_mag (g)")
        ax.set_xlabel("t (s)")
        ax.legend()

        last_plot = time.time()

        try:
            while True:
                line = ser.readline().decode("ascii", errors="replace").strip()
                if not line:
                    continue
                if line.startswith("#"):
                    print(line)
                    continue

                parts = line.split(",")
                if len(parts) < 5:
                    continue

                try:
                    ms = int(parts[0])
                    fm = float(parts[1])
                    fp = float(parts[2])
                    ag = float(parts[3])
                    flags = parts[4]
                except ValueError:
                    continue

                w.writerow([ms, fm, fp, ag, flags])
                f.flush()

                ts = ms / 1000.0
                t.append(ts); meanN.append(fm); peakN.append(fp); amag.append(ag)

                now = time.time()
                if now - last_plot >= 0.1:
                    last_plot = now
                    tt = np.array(t)
                    if tt.size < 2:
                        continue
                    t0 = tt[-1] - WINDOW_S
                    m = tt >= t0
                    x = tt[m] - tt[m][0]
                    l1.set_data(x, np.array(meanN)[m])
                    l2.set_data(x, np.array(peakN)[m])
                    l3.set_data(x, np.array(amag)[m])
                    ax.relim(); ax.autoscale_view()
                    fig.canvas.draw(); fig.canvas.flush_events()

        except KeyboardInterrupt:
            print("Stopping...")

        finally:
            try:
                ser.write(b"scope 0\\n")
            except Exception:
                pass
            ser.close()
            f.close()
            print("Saved:", OUTFILE)

    if __name__ == "__main__":
        main()
    """))

    write(root, "tools/host/bin2csv.py", textwrap.dedent("""\
    import struct
    import csv
    import sys

    HDR_FMT = "<IHHHHIIIIQIIffffiiiHiII170s"
    HDR_SIZE = 256

    FRAME_FMT = "<IQiii iii6hHHHH"
    FRAME_SIZE = 56

    def main(bin_path, csv_path):
        with open(bin_path, "rb") as f:
            hdr = f.read(HDR_SIZE)
            if len(hdr) != HDR_SIZE:
                raise RuntimeError("short header")

            (magic, hver, hsz, fver, fsz,
             build_id, adc_hz, frame_hz, decim,
             start_us, rtc_epoch, rtc_ms,
             slope, offset, acc_g_lsb, gyr_dps_lsb,
             ov, un, comp,
             tare_frames, tare_code,
             flags_static, crc, _reserved) = struct.unpack(HDR_FMT, hdr)

            if magic != 0x314C4450:
                raise RuntimeError("bad magic")
            if fsz != FRAME_SIZE:
                raise RuntimeError(f"frame size mismatch: {fsz} != {FRAME_SIZE}")

            with open(csv_path, "w", newline="") as out:
                w = csv.writer(out)
                w.writerow([
                    "sample_index","t_us",
                    "force_mean_N","force_peak_N","force_min_N",
                    "ax_g","ay_g","az_g","gx_dps","gy_dps","gz_dps",
                    "flags","vbat_mV","soc_percent"
                ])

                while True:
                    b = f.read(FRAME_SIZE)
                    if not b:
                        break
                    if len(b) != FRAME_SIZE:
                        break

                    (idx, t_us,
                     adc_mean, adc_peak, adc_min,
                     f_mean, f_peak, f_min,
                     ax, ay, az, gx, gy, gz,
                     flags, vbat, soc, pad) = struct.unpack(FRAME_FMT, b)

                    w.writerow([
                        idx, t_us,
                        f_mean / 1000.0, f_peak / 1000.0, f_min / 1000.0,
                        ax * acc_g_lsb, ay * acc_g_lsb, az * acc_g_lsb,
                        gx * gyr_dps_lsb, gy * gyr_dps_lsb, gz * gyr_dps_lsb,
                        f"0x{flags:04X}",
                        vbat,
                        soc / 100.0
                    ])

    if __name__ == "__main__":
        if len(sys.argv) != 3:
            print("Usage: python bin2csv.py LOG0000.BIN LOG0000.csv")
            sys.exit(1)
        main(sys.argv[1], sys.argv[2])
    """))

    # ---------- create zip ----------
    zip_path = pathlib.Path(ZIP_NAME)
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for p in root.rglob("*"):
            if p.is_file():
                z.write(p, arcname=str(p.relative_to(root)))

    print(f"Created folder: {root.resolve()}")
    print(f"Created zip:    {zip_path.resolve()}")

if __name__ == "__main__":
    main()
