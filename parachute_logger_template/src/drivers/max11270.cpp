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
