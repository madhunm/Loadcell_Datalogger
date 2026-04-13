/**
 * @file    log_record.h
 * @brief   Packed binary record types, magic, validity flags, and CRC16-CCITT
 *          for SD logging.
 * @details Defines on-disk structs shared by ISR record assembly, main-loop SD
 *          path, and offline Python decode tools.  Wire format is stable across
 *          phases; only C identifiers are normalised to project camelCase rules.
 * @author  Madhu
 * @date    2026-04-12
 */

#ifndef LOG_RECORD_H
#define LOG_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Record type tags ─────────────────────────────────────────────── */
#define REC_TYPE_ADC    0x01
#define REC_TYPE_FORCE  0x02
#define REC_TYPE_META   0x03

/* ── Binary file magic ────────────────────────────────────────────── */
#define BIN_FILE_MAGIC  0x4C44434CUL  /* 'LDCL' */
#define BIN_FORMAT_VER  1

/* ── Validity flags (binForceRecord_t.validity) ───────────────────── */
#define VALIDITY_ADC_OK       0x01
#define VALIDITY_IMU_OK       0x02
#define VALIDITY_BATT_FRESH   0x04
#define VALIDITY_NO_OVERFLOW  0x08
#define VALIDITY_ADS_CRC_OK   0x10
#define VALIDITY_CAL_DEFAULT  0x20

/* ── Packed struct typedefs ───────────────────────────────────────── */

/**
 * @brief  Binary file header written once at session start (64 bytes).
 * @see    Master plan "Dual-File Logging Format — Binary File Header"
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< 'LDCL' = 0x4C44434C                    */
    uint16_t version;           /**< Format version (BIN_FORMAT_VER)         */
    uint16_t headerSize;        /**< sizeof(binFileHeader_t)                 */
    uint32_t clkinHz;           /**< Measured CLKIN at session start          */
    uint32_t sysclkHz;          /**< SYSCLK frequency                        */
    uint16_t ltcDac;            /**< Trimmed LTC6903 DAC value               */
    uint16_t adcOsr;            /**< ADC oversampling ratio (128)             */
    uint16_t adcRecordRate;     /**< 8000                                    */
    uint16_t forceRecordRate;   /**< 500                                     */
    float    sensitivity;       /**< uV/N from config.txt                    */
    float    tareOffset;        /**< N from config.txt                       */
    uint8_t  adcGainCh1;        /**< ADS131M02 gain setting CH1              */
    uint8_t  adcGainCh2;        /**< ADS131M02 gain setting CH2              */
    uint8_t  imuOdr;            /**< LSM6DSV ODR setting                     */
    uint8_t  imuFsAccel;        /**< Full-scale accel (16 g)                 */
    uint32_t rtcEpoch;          /**< RTC time at start (s since 2000-01-01)  */
    uint8_t  fwVersion[8];      /**< e.g. "v0.10.0\0"                       */
    uint8_t  reserved[12];      /**< Pad to 64 bytes                         */
    uint16_t crc16;             /**< CRC of bytes 0..61                      */
} binFileHeader_t;              /* 64 bytes */

/**
 * @brief  ADC record emitted at 8 kHz (16 bytes).
 * @see    Master plan "Record Type 0x01"
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;              /**< REC_TYPE_ADC (0x01)                     */
    uint8_t  flags;             /**< bit 0: ADS CRC OK, bit 1: overflow     */
    uint16_t seqLo;             /**< Low 16 bits of 8 kHz counter            */
    int32_t  sumCh0;            /**< Sum of 8 raw 24-bit CH0 samples         */
    int32_t  sumCh1;            /**< Sum of 8 raw 24-bit CH1 samples         */
    uint16_t crc16;             /**< CRC of bytes 0..13                      */
} binAdcRecord_t;               /* 16 bytes */

/**
 * @brief  Force + IMU record emitted at 500 Hz (32 bytes).
 * @see    Master plan "Record Type 0x02"
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;              /**< REC_TYPE_FORCE (0x02)                   */
    uint8_t  validity;          /**< Validity flags bitmask                  */
    uint16_t seqLo;             /**< Low 16 bits of 500 Hz counter           */
    uint32_t timestampMs;       /**< ms since logging start                  */
    float    forceN;            /**< Computed ratiometric force (N)           */
    int16_t  accelX;            /**< LSM6DSV raw accel X                     */
    int16_t  accelY;            /**< LSM6DSV raw accel Y                     */
    int16_t  accelZ;            /**< LSM6DSV raw accel Z                     */
    int16_t  gyroX;             /**< LSM6DSV raw gyro X                      */
    int16_t  gyroY;             /**< LSM6DSV raw gyro Y                      */
    int16_t  gyroZ;             /**< LSM6DSV raw gyro Z                      */
    int32_t  sumCh0_128;        /**< Full 128-sample CH0 ADC sum             */
    uint16_t crc16;             /**< CRC of bytes 0..29                      */
} binForceRecord_t;             /* 32 bytes */

/**
 * @brief  Metadata record emitted at 1 Hz (32 bytes).
 * @see    Master plan "Record Type 0x03"
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;              /**< REC_TYPE_META (0x03)                    */
    uint8_t  reserved;
    uint16_t secondNum;         /**< Seconds since logging start             */
    uint32_t clkinHz;           /**< Measured CLKIN this second               */
    int16_t  mcuTempX10;        /**< MCU die temp x10 (235 = 23.5 C)        */
    uint16_t batteryMv;         /**< Battery voltage in mV                   */
    uint32_t drdyTotal;         /**< Cumulative DRDY count                   */
    uint32_t missTotal;         /**< Cumulative missed DRDYs                 */
    uint32_t overflowTotal;     /**< Cumulative ring overflows               */
    uint16_t adsStatus;         /**< Last ADS131M02 STATUS word              */
    uint16_t padding;
    uint16_t crc16;             /**< CRC of bytes 0..29                      */
} binMetaRecord_t;              /* 32 bytes */

/* ── CRC16-CCITT (poly 0x1021, init 0xFFFF, no final XOR) ────────── */

/**
 * @brief  256-entry lookup table for byte-at-a-time CRC16-CCITT.
 * @note   Stored in flash (.rodata); ~512 bytes.  Cost per byte: ~8 cycles
 *         at 250 MHz vs ~20 cycles for the bit-loop variant.
 */
static const uint16_t CRC16_CCITT_LUT[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x4864, 0x5845, 0x6826, 0x7807, 0x08E0, 0x18C1, 0x28A2, 0x38A3,
    0xC94C, 0xD96D, 0xE90E, 0xF92F, 0x89C8, 0x99E9, 0xA98A, 0xB9AB,
    0x5A75, 0x4A54, 0x7A37, 0x6A16, 0x1AF1, 0x0AD0, 0x3AB3, 0x2A92,
    0xDB7D, 0xCB5C, 0xFB3F, 0xEB1E, 0x9BF9, 0x8BD8, 0xBBBB, 0xAB9A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x85A9, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x04A1, 0x7446, 0x6467, 0x5404, 0x4425,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD9EC, 0xC9CD, 0xF9AE, 0xE98F, 0x9968, 0x8949, 0xB92A, 0xA90B,
    0x58E4, 0x48C5, 0x78A6, 0x6887, 0x1860, 0x0841, 0x3822, 0x2803,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

/**
 * @brief  Compute CRC16-CCITT over a byte buffer.
 * @param[in] data  Pointer to input bytes.
 * @param[in] len   Number of bytes.
 * @return CRC16 value (poly 0x1021, init 0xFFFF, no final XOR).
 * @note   ~8 cycles/byte at 250 MHz with the table-driven approach.
 */
static inline uint16_t crc16Ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++)
    {
        uint8_t idx = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (uint16_t)((crc << 8) ^ CRC16_CCITT_LUT[idx]);
    }
    return crc;
}

#ifdef __cplusplus
}
#endif

#endif /* LOG_RECORD_H */
