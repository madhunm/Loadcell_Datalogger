/**
 * @file    imu_lsm6dsv.h
 * @brief   LSM6DSV 6-axis IMU driver — public API.
 * @details HA01 2000 Hz ODR, boot-time offset calibration, SFLP game rotation
 *          via FIFO (drain_max=32, DOE-validated), SW quaternion fallback.
 *          SPI2 at 10.0 MHz blocking (no DMA), CS on PB12.
 *          SPI error recovery: abort + deinit + reinit on any bus failure.
 *          Upstream: BSP SPI2 (custom_bus.c), LSM6DSV component driver.
 *          Downstream: main.c polling loop, debug_ui, VIZ_STREAM CDC output.
 * @author  Madhu
 * @date    2026-04-12
 */
#ifndef IMU_LSM6DSV_H
#define IMU_LSM6DSV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  Complete IMU sample with physical-unit conversions.
 */
typedef struct {
    float ax, ay, az;               /**< calibrated accel  (m/s^2) */
    float gx, gy, gz;               /**< calibrated gyro   (dps)   */
    float driftX, driftY, driftZ;   /**< cumulative gyro drift (deg) since cal */
    float qw, qx, qy, qz;          /**< software quaternion */
    float roll, pitch, yaw;         /**< Euler angles (deg) from quaternion */
    float tempC;                    /**< die temperature (deg C) */
    int16_t rawAx, rawAy, rawAz;   /**< raw accel LSB */
    int16_t rawGx, rawGy, rawGz;   /**< raw gyro  LSB */
} imuData_t;

/**
 * @brief  Initialise the LSM6DSV: bus IO, WHO_AM_I, FS/ODR, gyro LP1 filter.
 * @details Registers SPI2 bus callbacks, verifies WHO_AM_I == 0x70, configures
 *          FS = 16 g / 2000 dps, attempts HA01 2000 Hz ODR (falls back to
 *          480 Hz standard), enables gyro LP1 medium filter, and starts the
 *          DWT cycle counter for imuRead() timing instrumentation.
 * @return 0 on success, negative error code on failure.
 * @note   Must be called before imuCalibrate().  SPI2 must be idle.
 * @see    LSM6DSV datasheet §6.1 (WHO_AM_I), §6.17 (HA01 ODR).
 */
int imuInit(void);

/**
 * @brief  Stationary calibration: auto-detect gravity axis, compute offsets.
 * @details Discards 10 settling samples, averages 256 accel+gyro readings,
 *          detects gravity axis (orientation-agnostic), stores SW offsets,
 *          then enables SFLP game rotation engine and FIFO stream mode.
 * @pre    imuInit() succeeded.  Board must be stationary during call (~800 ms).
 * @post   SFLP game rotation enabled at 120 Hz, FIFO streaming SFLP-only.
 * @return 0 on success, negative error code on failure.
 * @see    LSM6DSV datasheet §4.3 (SFLP), §9.5 (FIFO).
 */
int imuCalibrate(void);

/**
 * @brief  Read one IMU sample: accel, gyro, quaternion, Euler, temp, drift.
 * @details Reads accel+gyro registers, applies boot-time calibration offsets,
 *          drains SFLP FIFO (up to FIFO_DRAIN_MAX entries) for latest hardware
 *          quaternion (falls back to SW quaternion if SFLP unavailable),
 *          converts quaternion to Euler, reads die temperature, and records
 *          DWT-based execution timing for diagnostics.
 * @param[out] out  Filled with the latest calibrated data.
 * @return 0 on success, negative error code on failure.
 * @note   Typical execution time ~140 µs at 10 MHz SPI2 (DOE-measured).
 * @pre    imuCalibrate() must have been called at least once.
 */
int imuRead(imuData_t *out);

/**
 * @brief  Read raw (uncalibrated) accel and gyro as signed 16-bit LSBs.
 * @details Intended for binary logging (Phase 10 bin_force_record_t).
 *          No calibration, no quaternion, no FIFO drain.
 * @param[out] accel  3-element array [X, Y, Z] in raw LSBs.
 * @param[out] gyro   3-element array [X, Y, Z] in raw LSBs.
 * @return 0 on success, negative error code on failure.
 * @see    LSM6DSV datasheet Table 2 (accel sensitivity), Table 3 (gyro).
 */
int imuReadRaw(int16_t accel[3], int16_t gyro[3]);

/**
 * @brief  Get the detected gravity axis string (e.g. "+Z", "-X").
 * @return Pointer to a static null-terminated string.
 */
const char *imuGetGravAxis(void);

/**
 * @brief  Get the calibration offsets applied to physical-unit readings.
 * @param[out] ax,ay,az  Accel offsets (m/s^2).
 * @param[out] gx,gy,gz  Gyro offsets (dps).
 */
void imuGetCalOffsets(float *ax, float *ay, float *az,
                      float *gx, float *gy, float *gz);

/**
 * @brief  Get diagnostic counters for SPI health and FIFO status.
 * @param[out] spiErr    Cumulative SPI bus errors (SendRecv failures).
 * @param[out] spiRecov  Cumulative SPI recovery cycles (abort+reinit).
 * @param[out] fifoOvf   Times FIFO level exceeded FIFO_DRAIN_MAX (32).
 * @param[out] fifoMax   Highest FIFO watermark ever observed.
 * @param[out] sflpCnt   Total SFLP game rotation samples consumed.
 * @note   Counters accumulate since boot; no reset API in production build.
 */
void imuGetDiag(uint32_t *spiErr, uint32_t *spiRecov,
                uint32_t *fifoOvf, uint32_t *fifoMax,
                uint32_t *sflpCnt);

/**
 * @brief  Get imuRead() timing diagnostics from DWT cycle counter.
 * @param[out] calls  Total imuRead() invocations since boot.
 * @param[out] maxUs  Worst-case execution time (microseconds).
 * @param[out] avgUs  Average execution time (microseconds, 0 if no calls).
 * @note   Timing is measured end-to-end including FIFO drain and temp read.
 */
void imuGetTimingDiag(uint32_t *calls, uint32_t *maxUs, uint32_t *avgUs);

#ifdef __cplusplus
}
#endif

#endif /* IMU_LSM6DSV_H */
