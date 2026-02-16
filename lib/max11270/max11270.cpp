#include "max11270.h"

#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------- Utility ----------
uint64_t Max11270::nowMicros_() {
  return static_cast<uint64_t>(esp_timer_get_time());
}

// ---------- Command builders (datasheet-faithful) ----------
// Conversion Mode command byte: [START=1][MODE=0][CAL][IMPD][RATE3..0]
uint8_t Max11270::cmdConversion(bool cal, bool impd, Rate rate) {
  uint8_t b = 0x80; // START=1, MODE=0
  if (cal)  b |= 0x20; // B5
  if (impd) b |= 0x10; // B4
  b |= (static_cast<uint8_t>(rate) & 0x0F);
  return b;
}

// Register Access Mode command byte: [START=1][MODE=1][RS4..0][R/W]
uint8_t Max11270::cmdRegAccess(Reg r, bool read) {
  uint8_t rs = static_cast<uint8_t>(r) & 0x1F;
  uint8_t b = 0xC0;              // START=1, MODE=1
  b |= static_cast<uint8_t>(rs << 1);
  if (read) b |= 0x01;
  return b;
}

// ---------- Lifecycle ----------
Max11270::~Max11270() {
  if (dev_) {
    spi_bus_remove_device(dev_);
    dev_ = nullptr;
  }
  // NOTE: spi_bus_free(host) not called here, because the bus may be shared.
  initialized_ = false;
}

esp_err_t Max11270::begin(const BusConfig& bus_cfg,
                          const SpiPins& spi_pins,
                          const GpioPins& gpio_pins) {
  bus_cfg_ = bus_cfg;
  spi_pins_ = spi_pins;
  gpio_pins_ = gpio_pins;

  esp_err_t err;

  if (bus_cfg_.init_bus) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = spi_pins_.mosi;
    bus.miso_io_num = spi_pins_.miso;
    bus.sclk_io_num = spi_pins_.sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 0; // default

    err = spi_bus_initialize(bus_cfg_.host, &bus, bus_cfg_.dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return err;
    }
    // ESP_ERR_INVALID_STATE is acceptable if bus already inited elsewhere.
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = bus_cfg_.clock_hz;
  devcfg.mode = 0; // SPI mode 0: sample on rising edge, shift on falling edge
  devcfg.spics_io_num = spi_pins_.cs;
  devcfg.queue_size = bus_cfg_.queue_size;
  devcfg.flags = 0;

  err = spi_bus_add_device(bus_cfg_.host, &devcfg, &dev_);
  if (err != ESP_OK) return err;

  err = configureGpios_();
  if (err != ESP_OK) return err;

  initialized_ = true;
  return ESP_OK;
}

esp_err_t Max11270::configureGpios_() {
  // RDYB (active low)
  if (gpio_pins_.rdyb != GPIO_NUM_NC) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpio_pins_.rdyb;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;   // typical
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
  }

  // RSTB (active low)
  if (gpio_pins_.rstb != GPIO_NUM_NC) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpio_pins_.rstb;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level(gpio_pins_.rstb, 1); // deassert
  }

  // SYNC (usually held low)
  if (gpio_pins_.sync != GPIO_NUM_NC) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gpio_pins_.sync;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
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

  // CTRL1.PD = 11 resets all registers to POR (subregulator powered). Then PD bits revert to 00.
  // Build CTRL1 with PD=11 and leave others default.
  // CTRL1 bits: EXTCK(7) SYNC(6) PD1(5) PD0(4) U/B(3) FORMAT(2) SCYCLE(1) CONTSC(0)
  uint8_t ctrl1 = 0;
  ctrl1 |= (1u << 5); // PD1
  ctrl1 |= (1u << 4); // PD0

  esp_err_t err = writeReg8(Reg::CTRL1, ctrl1);
  if (err != ESP_OK) return err;

  vTaskDelay(pdMS_TO_TICKS(post_ms));
  return ESP_OK;
}

// ---------- Low-level SPI ----------
esp_err_t Max11270::transmit(const uint8_t* tx, uint8_t* rx, size_t nbytes) {
  if (!dev_) return ESP_ERR_INVALID_STATE;
  if (nbytes == 0) return ESP_OK;

  spi_transaction_t t;
  std::memset(&t, 0, sizeof(t));

  t.length = static_cast<int>(nbytes * 8);
  t.tx_buffer = tx;
  t.rx_buffer = rx;

  // Polling transmit for determinism and speed
  return spi_device_polling_transmit(dev_, &t);
}

// ---------- Register access ----------
esp_err_t Max11270::writeReg8(Reg r, uint8_t v) {
  uint8_t cmd = cmdRegAccess(r, /*read=*/false);
  txbuf_[0] = cmd;
  txbuf_[1] = v;
  return transmit(txbuf_, nullptr, 2);
}

esp_err_t Max11270::readReg8(Reg r, uint8_t* out) {
  if (!out) return ESP_ERR_INVALID_ARG;
  uint8_t cmd = cmdRegAccess(r, /*read=*/true);
  txbuf_[0] = cmd;
  txbuf_[1] = 0x00;
  esp_err_t err = transmit(txbuf_, rxbuf_, 2);
  if (err != ESP_OK) return err;
  *out = rxbuf_[1];
  return ESP_OK;
}

esp_err_t Max11270::readReg16(Reg r, uint16_t* out) {
  if (!out) return ESP_ERR_INVALID_ARG;
  uint8_t cmd = cmdRegAccess(r, /*read=*/true);
  txbuf_[0] = cmd;
  txbuf_[1] = 0x00;
  txbuf_[2] = 0x00;
  esp_err_t err = transmit(txbuf_, rxbuf_, 3);
  if (err != ESP_OK) return err;
  // MSB first
  *out = (static_cast<uint16_t>(rxbuf_[1]) << 8) | static_cast<uint16_t>(rxbuf_[2]);
  return ESP_OK;
}

esp_err_t Max11270::readReg24(Reg r, uint32_t* out24) {
  if (!out24) return ESP_ERR_INVALID_ARG;
  uint8_t cmd = cmdRegAccess(r, /*read=*/true);
  txbuf_[0] = cmd;
  txbuf_[1] = 0x00;
  txbuf_[2] = 0x00;
  txbuf_[3] = 0x00;
  esp_err_t err = transmit(txbuf_, rxbuf_, 4);
  if (err != ESP_OK) return err;
  *out24 = (static_cast<uint32_t>(rxbuf_[1]) << 16) |
           (static_cast<uint32_t>(rxbuf_[2]) << 8)  |
            static_cast<uint32_t>(rxbuf_[3]);
  return ESP_OK;
}

esp_err_t Max11270::readData32(int32_t* out_code) {
  if (!out_code) return ESP_ERR_INVALID_ARG;

  // DATA register read. If CTRL3.DATA32=1, read 4 bytes after command.
  uint8_t cmd = cmdRegAccess(Reg::DATA, /*read=*/true);
  txbuf_[0] = cmd;
  txbuf_[1] = 0x00;
  txbuf_[2] = 0x00;
  txbuf_[3] = 0x00;
  txbuf_[4] = 0x00;

  esp_err_t err = transmit(txbuf_, rxbuf_, 5);
  if (err != ESP_OK) return err;

  uint32_t u = (static_cast<uint32_t>(rxbuf_[1]) << 24) |
               (static_cast<uint32_t>(rxbuf_[2]) << 16) |
               (static_cast<uint32_t>(rxbuf_[3]) << 8)  |
                static_cast<uint32_t>(rxbuf_[4]);

  *out_code = static_cast<int32_t>(u);
  return ESP_OK;
}

// ---------- Configuration ----------
esp_err_t Max11270::configure(const Settings& s) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;
  settings_ = s;

  // CTRL1:
  // EXTCK(7)=0 internal clock, SYNC(6)=0 pulse sync, PD(5:4)=00 normal
  // U/B(3)=1 unipolar else bipolar, FORMAT(2)= bipolar only
  // SCYCLE(1)=0 continuous conversion mode, CONTSC(0)=0
  uint8_t ctrl1 = 0;

  if (!s.use_internal_clock) ctrl1 |= (1u << 7); // EXTCK=1
  // SYNC mode left at 0
  // PD bits left at 00

  if (s.range == InputRange::Unipolar) {
    ctrl1 |= (1u << 3); // U/B=1
    // FORMAT ignored in unipolar (always offset binary)
  } else {
    // bipolar
    if (s.bipolar_format == BipolarFormat::OffsetBinary) ctrl1 |= (1u << 2); // FORMAT=1
  }

  if (s.continuous_conversion) {
    ctrl1 &= ~(1u << 1); // SCYCLE=0 (continuous conversion mode)
    ctrl1 &= ~(1u << 0); // CONTSC=0 (not used in SCYCLE=0)
  } else {
    // single-cycle mode:
    ctrl1 |= (1u << 1);  // SCYCLE=1
    ctrl1 &= ~(1u << 0); // CONTSC=0 => single conversion (not continuous single-cycle)
  }

  // CTRL2:
  // DGAIN1:0 (7:6), BUFEN(5), LPMODE(4), PGAEN(3), PGAG2:0 (2:0)
  uint8_t ctrl2 = 0;
  ctrl2 |= (static_cast<uint8_t>(s.digital_gain) & 0x03) << 6;
  if (s.enable_buffer)  ctrl2 |= (1u << 5);
  if (s.pga_low_power)  ctrl2 |= (1u << 4);
  if (s.enable_pga)     ctrl2 |= (1u << 3);
  ctrl2 |= (static_cast<uint8_t>(s.pga_gain) & 0x07);

  // CTRL3:
  // Preserve reserved/default bits. Datasheet default is 0x61 (B6=1, B5=1, B0=1).
  // DATA32 is bit3.
  uint8_t ctrl3 = 0x61;
  if (s.data32) ctrl3 |= (1u << 3);

  // CTRL4: keep default 0x0F (DIO bits high, DIR bits 0, reserved bit4=1 per datasheet default)
  // Safer is read-modify-write; but if you don't use GPIOs on MAX11270, leave as POR by not writing.
  // We'll not touch CTRL4 here.

  // CTRL5:
  // NOSYSG(3), NOSYSO(2), NOSCG(1), NOSCO(0) inverted enable semantics.
  // CAL bits are [7:6], reserved [5:4]
  uint8_t ctrl5 = 0;
  // reserved [5:4] default 0
  // NOS* defaults are: NOSYSG=1, NOSYSO=1, NOSCG=0, NOSCO=0 (i.e., system disabled; self enabled)
  if (!s.use_system_gain)   ctrl5 |= (1u << 3); // NOSYSG=1 disables system gain
  if (!s.use_system_offset) ctrl5 |= (1u << 2); // NOSYSO=1 disables system offset
  if (!s.use_self_cal_gain) ctrl5 |= (1u << 1); // NOSCG=1 disables self gain
  if (!s.use_self_cal_offset)ctrl5 |= (1u << 0); // NOSCO=1 disables self offset
  // CAL[1:0] left at 00 unless calibration is requested

  // Apply writes
  esp_err_t err;
  if ((err = writeReg8(Reg::CTRL1, ctrl1)) != ESP_OK) return err;
  if ((err = writeReg8(Reg::CTRL2, ctrl2)) != ESP_OK) return err;
  if ((err = writeReg8(Reg::CTRL3, ctrl3)) != ESP_OK) return err;
  if ((err = writeReg8(Reg::CTRL5, ctrl5)) != ESP_OK) return err;

  return ESP_OK;
}

// ---------- Start/Stop conversions ----------
esp_err_t Max11270::startConversions(Rate rate) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;
  uint8_t cmd = cmdConversion(/*cal=*/false, /*impd=*/false, rate);
  txbuf_[0] = cmd;
  return transmit(txbuf_, nullptr, 1);
}

esp_err_t Max11270::stopAndPowerDown(PowerDownMode mode) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;

  // Set PD bits in CTRL1 then send IMPD=1 conversion command.
  uint8_t ctrl1;
  esp_err_t err = readReg8(Reg::CTRL1, &ctrl1);
  if (err != ESP_OK) return err;

  // clear PD bits
  ctrl1 &= ~((1u << 5) | (1u << 4));

  if (mode == PowerDownMode::Sleep) {
    ctrl1 |= (1u << 4); // PD=01
  } else {
    ctrl1 |= (1u << 5); // PD=10
  }

  err = writeReg8(Reg::CTRL1, ctrl1);
  if (err != ESP_OK) return err;

  uint8_t cmd = cmdConversion(/*cal=*/false, /*impd=*/true, settings_.rate);
  txbuf_[0] = cmd;
  return transmit(txbuf_, nullptr, 1);
}

// ---------- Sampling ----------
esp_err_t Max11270::readSampleBlocking(Sample* out, uint32_t timeout_us) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;
  if (!out) return ESP_ERR_INVALID_ARG;
  if (gpio_pins_.rdyb == GPIO_NUM_NC) return ESP_ERR_INVALID_STATE;

  const uint64_t t0 = nowMicros_();

  // RDYB is active low when result is ready and stays low until data read completes.
  // Tight polling is preferred at 64 ksps.
  while (gpio_get_level(gpio_pins_.rdyb) != 0) {
    if (timeout_us != 0 && (nowMicros_() - t0) >= timeout_us) {
      return ESP_ERR_TIMEOUT;
    }
    // no vTaskDelay here; keep deterministic
  }

  out->t_us = nowMicros_();

  // Read 32-bit code (requires CTRL3.DATA32=1).
  int32_t code;
  esp_err_t err = readData32(&code);
  if (err != ESP_OK) return err;

  out->code = code;
  return ESP_OK;
}

// ---------- Status ----------
esp_err_t Max11270::readStatus(Status* out) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;
  if (!out) return ESP_ERR_INVALID_ARG;

  uint16_t raw;
  esp_err_t err = readReg16(Reg::STAT, &raw);
  if (err != ESP_OK) return err;

  out->raw = raw;
  out->rdy   = (raw & (1u << 0)) != 0;
  out->mstat = (raw & (1u << 1)) != 0;
  out->dor   = (raw & (1u << 2)) != 0;
  out->aor   = (raw & (1u << 8)) != 0;
  out->rderr = (raw & (1u << 9)) != 0;
  out->rate  = static_cast<uint8_t>((raw >> 4) & 0x0F);
  return ESP_OK;
}

// ---------- Calibration ----------
esp_err_t Max11270::selfCalibrate(uint32_t timeout_ms) {
  if (!initialized_) return ESP_ERR_INVALID_STATE;

  // Set CTRL5.CAL[1:0]=00 (self calibration).
  uint8_t ctrl5;
  esp_err_t err = readReg8(Reg::CTRL5, &ctrl5);
  if (err != ESP_OK) return err;

  ctrl5 &= ~((1u << 7) | (1u << 6)); // CAL1:CAL0 = 00
  err = writeReg8(Reg::CTRL5, ctrl5);
  if (err != ESP_OK) return err;

  // Issue calibration command (CAL=1). RATE bits are still present but self-cal uses internal sequence.
  uint8_t cmd = cmdConversion(/*cal=*/true, /*impd=*/false, Rate::R_1000SPS);
  txbuf_[0] = cmd;
  err = transmit(txbuf_, nullptr, 1);
  if (err != ESP_OK) return err;

  // Poll MSTAT until it clears
  const uint64_t t0 = nowMicros_();
  while (true) {
    Status st;
    err = readStatus(&st);
    if (err != ESP_OK) return err;
    if (!st.mstat) break;

    if ((nowMicros_() - t0) >= (static_cast<uint64_t>(timeout_ms) * 1000ULL)) {
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  return ESP_OK;
}

// ---------- Conversions ----------
double Max11270::codeToVoltsBipolarTwosComp32(int32_t code, double vref_volts) {
  // Two's complement full-scale: -2^31..(2^31-1)
  constexpr double denom = 2147483648.0; // 2^31
  return (static_cast<double>(code) / denom) * vref_volts;
}

double Max11270::codeTo_uV_per_V(int32_t code, double vref_volts, double excitation_volts) {
  if (excitation_volts <= 0.0) return 0.0;
  double vin = codeToVoltsBipolarTwosComp32(code, vref_volts);
  return (vin / excitation_volts) * 1e6;
}
