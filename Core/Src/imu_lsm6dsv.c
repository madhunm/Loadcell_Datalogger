/**
 * @file    imu_lsm6dsv.c
 * @brief   LSM6DSV 6-axis IMU driver — hardened for field use.
 * @details HA01 2000 Hz ODR, boot-time offset calibration, SFLP game rotation
 *          via FIFO (drain_max=32, DOE-validated), SW quaternion fallback.
 *          SPI2 at 10.0 MHz, blocking (no DMA), CS on PB12.
 *          SPI error recovery: abort + deinit + reinit on any bus failure.
 * @author  Madhu
 * @date    2026-04-12
 */

#include "imu_lsm6dsv.h"
#include "main.h"
#include "custom_bus.h"
#include "lsm6dsv.h"
#include "lsm6dsv_reg.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/** @see LSM6DSV datasheet Table 2 — FS=16 g -> 0.488 mg/LSB */
#define ACC_SENSITIVITY_16G   0.488f
#define ACC_MG_TO_MSS         (9.80665f / 1000.0f)

/** @see LSM6DSV datasheet Table 3 — FS=2000 dps -> 70.0 mdps/LSB */
#define GYRO_SENSITIVITY_2000 70.0f
#define GYRO_MDPS_TO_DPS      (1.0f / 1000.0f)

#define CAL_DISCARD   10
#define CAL_SAMPLES   256
#define GRAVITY_MSS   9.80665f
#define DEG_PER_RAD   (180.0f / 3.14159265358979f)

/** @brief  Max FIFO entries to drain per imuRead(). DOE-validated: 32. */
#define FIFO_DRAIN_MAX  32

static LSM6DSV_Object_t imuObj;
static uint8_t imuReady;

static float calAx, calAy, calAz;
static float calGx, calGy, calGz;
static char  gravTag[4] = "??";

static float driftX, driftY, driftZ;
static uint32_t driftLastTick;

static float swQw = 1.0f, swQx, swQy, swQz;

static float sflpQx, sflpQy, sflpQz, sflpQw = 1.0f;
static uint8_t sflpValid;

/** @brief  Diagnostic counters — readable via imuGetDiag(). */
static uint32_t diagSpiErrors;
static uint32_t diagSpiRecovers;
static uint32_t diagFifoOverflows;
static uint32_t diagFifoMaxSeen;
static uint32_t diagSflpSamples;
static uint32_t diagReadCalls;
static uint32_t diagReadMaxUs;
static uint32_t diagReadTotalUs;

/* ── SPI2 bus callbacks + recovery ──────────────────────────────── */

static int32_t busInit(void)  { return BSP_SPI2_Init(); }
static int32_t busDeinit(void){ return BSP_SPI2_DeInit(); }

extern SPI_HandleTypeDef hspi2;

/** @brief  Reset SPI2 peripheral after a bus error. */
static void spiRecover(void)
{
    HAL_SPI_Abort(&hspi2);
    HAL_SPI_DeInit(&hspi2);
    HAL_SPI_Init(&hspi2);
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
    diagSpiRecovers++;
}

/**
 * @brief  SPI read callback for LSM6DSV_IO_t — bit 7 set for read flag.
 * @note   On bus error, increments diagSpiErrors and calls spiRecover().
 * @see    LSM6DSV datasheet §6.2.1 (SPI read protocol).
 */
static int32_t spiRead(uint16_t addr, uint16_t reg,
                        uint8_t *data, uint16_t len)
{
    (void)addr;
    static uint8_t tx[33], rx[33];
    uint16_t total = len + 1u;
    if (total > sizeof(tx))
        total = sizeof(tx);

    tx[0] = (uint8_t)reg | 0x80u;
    memset(&tx[1], 0, total - 1u);

    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
    int32_t ret = BSP_SPI2_SendRecv(tx, rx, total);
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);

    if (ret == BSP_ERROR_NONE)
    {
        memcpy(data, &rx[1], len);
    }
    else
    {
        diagSpiErrors++;
        spiRecover();
    }
    return ret;
}

/**
 * @brief  SPI write callback for LSM6DSV_IO_t — bit 7 clear for write flag.
 * @note   On bus error, increments diagSpiErrors and calls spiRecover().
 */
static int32_t spiWrite(uint16_t addr, uint16_t reg,
                         uint8_t *data, uint16_t len)
{
    (void)addr;
    static uint8_t tx[33];
    uint16_t total = len + 1u;
    if (total > sizeof(tx))
        total = sizeof(tx);

    tx[0] = (uint8_t)reg;
    memcpy(&tx[1], data, total - 1u);

    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
    int32_t ret = BSP_SPI2_Send(tx, total);
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);

    if (ret != BSP_ERROR_NONE)
    {
        diagSpiErrors++;
        spiRecover();
    }
    return ret;
}

static void busDelay(uint32_t ms) { HAL_Delay(ms); }
static int32_t busGetTick(void)   { return (int32_t)HAL_GetTick(); }

/* ── Quaternion helpers ─────────────────────────────────────────── */

/**
 * @brief  Convert quaternion to Euler angles (ZYX intrinsic convention).
 * @note   asinf argument clamped to [-1, 1] to prevent NaN at gimbal lock.
 */
static void quatToEuler(float w, float x, float y, float z,
                         float *roll, float *pitch, float *yaw)
{
    float sinr = 2.0f * (w * x + y * z);
    float cosr = 1.0f - 2.0f * (x * x + y * y);
    *roll = atan2f(sinr, cosr) * DEG_PER_RAD;

    float sinp = 2.0f * (w * y - z * x);
    if (sinp > 1.0f)        sinp =  1.0f;
    else if (sinp < -1.0f)  sinp = -1.0f;
    *pitch = asinf(sinp) * DEG_PER_RAD;

    float siny = 2.0f * (w * z + x * y);
    float cosy = 1.0f - 2.0f * (y * y + z * z);
    *yaw = atan2f(siny, cosy) * DEG_PER_RAD;
}

/**
 * @brief  Integrate gyro rates into the software quaternion (small-angle approx).
 * @note   Renormalises after each step; guards against zero-norm divide.
 */
static void swQuatIntegrate(float gx, float gy, float gz, float dt)
{
    float halfDt = 0.5f * dt * (3.14159265358979f / 180.0f);
    float dqw = 1.0f;
    float dqx = gx * halfDt;
    float dqy = gy * halfDt;
    float dqz = gz * halfDt;

    float nw = swQw * dqw - swQx * dqx - swQy * dqy - swQz * dqz;
    float nx = swQw * dqx + swQx * dqw + swQy * dqz - swQz * dqy;
    float ny = swQw * dqy - swQx * dqz + swQy * dqw + swQz * dqx;
    float nz = swQw * dqz + swQx * dqy - swQy * dqx + swQz * dqw;

    float mag2 = nw * nw + nx * nx + ny * ny + nz * nz;
    if (mag2 > 1e-8f)
    {
        float norm = 1.0f / sqrtf(mag2);
        swQw = nw * norm;
        swQx = nx * norm;
        swQy = ny * norm;
        swQz = nz * norm;
    }
}

/* ── Half-float decoder ─────────────────────────────────────────── */

/** @brief  Decode IEEE 754 half-precision (16-bit) to 32-bit float.
 *  @return The float value, or 0.0f if the input is NaN/Inf/out-of-range. */
static float halfToFloat(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) & 1u;
    uint32_t exp  = (uint32_t)(h >> 10) & 0x1Fu;
    uint32_t frac = (uint32_t)(h & 0x3FFu);

    if (exp == 0x1Fu)
        return 0.0f;

    float result;
    if (exp == 0u)
    {
        result = (float)frac / 1024.0f;
        result *= (1.0f / 16384.0f);
    }
    else
    {
        result = (1.0f + (float)frac / 1024.0f);
        int shift = (int)exp - 15;
        if (shift >= 0)
            result *= (float)(1 << shift);
        else
            result /= (float)(1 << (-shift));
    }
    if (sign) result = -result;

    if (result > 1.5f || result < -1.5f)
        return 0.0f;

    return result;
}

/** @brief  Drain FIFO for latest SFLP game rotation quaternion.
 *  @return Number of game rotation samples consumed. */
static int fifoDrainSflp(void)
{
    stmdev_ctx_t *ctx = &imuObj.Ctx;
    lsm6dsv_fifo_status_t fstat;

    if (lsm6dsv_fifo_status_get(ctx, &fstat) != 0)
        return 0;

    uint32_t level = (uint32_t)fstat.fifo_level;

    if (level > diagFifoMaxSeen)
        diagFifoMaxSeen = level;
    if ((int)level > FIFO_DRAIN_MAX)
        diagFifoOverflows++;

    int toDrain = (int)level;
    if (toDrain > FIFO_DRAIN_MAX)
        toDrain = FIFO_DRAIN_MAX;

    int found = 0;
    for (int i = 0; i < toDrain; i++)
    {
        lsm6dsv_fifo_out_raw_t raw;
        if (lsm6dsv_fifo_out_raw_get(ctx, &raw) != 0)
            break;

        if (raw.tag == LSM6DSV_SFLP_GAME_ROTATION_VECTOR_TAG)
        {
            uint16_t hx = (uint16_t)(raw.data[1] << 8 | raw.data[0]);
            uint16_t hy = (uint16_t)(raw.data[3] << 8 | raw.data[2]);
            uint16_t hz = (uint16_t)(raw.data[5] << 8 | raw.data[4]);

            float qx = halfToFloat(hx);
            float qy = halfToFloat(hy);
            float qz = halfToFloat(hz);

            float w2 = 1.0f - (qx * qx + qy * qy + qz * qz);
            float qw = (w2 > 0.0f) ? sqrtf(w2) : 0.0f;

            float norm2 = qw * qw + qx * qx + qy * qy + qz * qz;
            if (norm2 > 0.5f && norm2 < 1.5f)
            {
                sflpQw = qw;
                sflpQx = qx;
                sflpQy = qy;
                sflpQz = qz;
                sflpValid = 1;
                found++;
                diagSflpSamples++;
            }
        }
    }
    return found;
}

/* ── Public API ─────────────────────────────────────────────────── */

/** @brief  Initialise LSM6DSV — see imu_lsm6dsv.h for full documentation. */
int imuInit(void)
{
    LSM6DSV_IO_t io = {
        .Init     = busInit,
        .DeInit   = busDeinit,
        .BusType  = LSM6DSV_SPI_4WIRES_BUS,
        .Address  = 0,
        .WriteReg = spiWrite,
        .ReadReg  = spiRead,
        .GetTick  = busGetTick,
        .Delay    = busDelay,
    };

    imuReady = 0;

    if (LSM6DSV_RegisterBusIO(&imuObj, &io) != LSM6DSV_OK)
    {
        printf("[IMU] RegisterBusIO FAILED\r\n");
        return -1;
    }

    uint8_t id = 0;
    if (LSM6DSV_ReadID(&imuObj, &id) != LSM6DSV_OK)
    {
        printf("[IMU] ReadID FAILED\r\n");
        return -2;
    }
    if (id != LSM6DSV_ID)
    {
        printf("[IMU] WHO_AM_I=0x%02X (expected 0x%02X)\r\n", id, LSM6DSV_ID);
        return -3;
    }

    if (LSM6DSV_Init(&imuObj) != LSM6DSV_OK)
    {
        printf("[IMU] Init FAILED\r\n");
        return -4;
    }

    /* Accel & Gyro: FS first, then ODR */
    if (LSM6DSV_ACC_SetFullScale(&imuObj, 16) != LSM6DSV_OK)
        return -5;
    if (LSM6DSV_GYRO_SetFullScale(&imuObj, 2000) != LSM6DSV_OK)
        return -8;

    /* Try HA01 2000 Hz, fall back to standard 480 Hz */
    stmdev_ctx_t *ctx = &imuObj.Ctx;
    const char *odrLabel = "480Hz";

    lsm6dsv_data_rate_t ha01 = LSM6DSV_ODR_HA01_AT_2000Hz;
    if (lsm6dsv_xl_data_rate_set(ctx, ha01) == 0 &&
        lsm6dsv_gy_data_rate_set(ctx, ha01) == 0)
    {
        odrLabel = "HA01 2000Hz";
    }
    else
    {
        LSM6DSV_ACC_SetOutputDataRate(&imuObj, 480.0f);
        LSM6DSV_GYRO_SetOutputDataRate(&imuObj, 480.0f);
    }

    if (LSM6DSV_ACC_Enable(&imuObj) != LSM6DSV_OK)
        return -7;
    if (LSM6DSV_GYRO_Enable(&imuObj) != LSM6DSV_OK)
        return -10;

    /* Gyro LP1 digital filter */
    lsm6dsv_filt_gy_lp1_set(ctx, 1);
    lsm6dsv_filt_gy_lp1_bandwidth_set(ctx, LSM6DSV_GY_MEDIUM);

    /* Enable DWT cycle counter for imuRead() timing */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    imuReady = 1;
    printf("[IMU] init OK, WHO_AM_I=0x%02X, ODR=%s, FS=16g/2000dps LP1=MED\r\n", id, odrLabel);
    return 0;
}

/** @brief  Boot-time calibration + SFLP enable — see imu_lsm6dsv.h. */
int imuCalibrate(void)
{
    if (!imuReady)
        return -1;

    LSM6DSV_AxesRaw_t accRaw, gyroRaw;

    for (int i = 0; i < CAL_DISCARD; i++)
    {
        HAL_Delay(3);
        LSM6DSV_ACC_GetAxesRaw(&imuObj, &accRaw);
        LSM6DSV_GYRO_GetAxesRaw(&imuObj, &gyroRaw);
    }

    double sumAx = 0, sumAy = 0, sumAz = 0;
    double sumGx = 0, sumGy = 0, sumGz = 0;

    for (int i = 0; i < CAL_SAMPLES; i++)
    {
        HAL_Delay(3);
        if (LSM6DSV_ACC_GetAxesRaw(&imuObj, &accRaw) != LSM6DSV_OK)
            return -2;
        if (LSM6DSV_GYRO_GetAxesRaw(&imuObj, &gyroRaw) != LSM6DSV_OK)
            return -3;

        sumAx += (double)accRaw.x;
        sumAy += (double)accRaw.y;
        sumAz += (double)accRaw.z;
        sumGx += (double)gyroRaw.x;
        sumGy += (double)gyroRaw.y;
        sumGz += (double)gyroRaw.z;
    }

    float avgAx = (float)(sumAx / CAL_SAMPLES) * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS;
    float avgAy = (float)(sumAy / CAL_SAMPLES) * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS;
    float avgAz = (float)(sumAz / CAL_SAMPLES) * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS;
    float avgGx = (float)(sumGx / CAL_SAMPLES) * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS;
    float avgGy = (float)(sumGy / CAL_SAMPLES) * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS;
    float avgGz = (float)(sumGz / CAL_SAMPLES) * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS;

    /* Auto-detect gravity axis */
    float absAx = (avgAx >= 0) ? avgAx : -avgAx;
    float absAy = (avgAy >= 0) ? avgAy : -avgAy;
    float absAz = (avgAz >= 0) ? avgAz : -avgAz;

    calAx = avgAx;
    calAy = avgAy;
    calAz = avgAz;

    if (absAx >= absAy && absAx >= absAz)
    {
        calAx -= (avgAx > 0) ? GRAVITY_MSS : -GRAVITY_MSS;
        gravTag[0] = (avgAx > 0) ? '+' : '-';
        gravTag[1] = 'X';
    }
    else if (absAy >= absAx && absAy >= absAz)
    {
        calAy -= (avgAy > 0) ? GRAVITY_MSS : -GRAVITY_MSS;
        gravTag[0] = (avgAy > 0) ? '+' : '-';
        gravTag[1] = 'Y';
    }
    else
    {
        calAz -= (avgAz > 0) ? GRAVITY_MSS : -GRAVITY_MSS;
        gravTag[0] = (avgAz > 0) ? '+' : '-';
        gravTag[1] = 'Z';
    }
    gravTag[2] = '\0';

    calGx = avgGx;
    calGy = avgGy;
    calGz = avgGz;

    driftX = driftY = driftZ = 0.0f;
    driftLastTick = HAL_GetTick();

    swQw = 1.0f;  swQx = swQy = swQz = 0.0f;

    printf("[IMU] cal grav=%s: ax=%+.3f ay=%+.3f az=%+.3f gx=%+.2f gy=%+.2f gz=%+.2f\r\n",
           gravTag,
           (double)calAx, (double)calAy, (double)calAz,
           (double)calGx, (double)calGy, (double)calGz);

    /* Enable SFLP game rotation engine */
    {
        stmdev_ctx_t *ctx = &imuObj.Ctx;
        lsm6dsv_sflp_data_rate_set(ctx, LSM6DSV_SFLP_120Hz);
        lsm6dsv_sflp_game_rotation_set(ctx, 1);
    }

    /* Configure FIFO: stream mode, batch only SFLP game rotation */
    {
        stmdev_ctx_t *ctx = &imuObj.Ctx;
        lsm6dsv_fifo_xl_batch_set(ctx, LSM6DSV_XL_NOT_BATCHED);
        lsm6dsv_fifo_gy_batch_set(ctx, LSM6DSV_GY_NOT_BATCHED);
        lsm6dsv_fifo_sflp_raw_t sflpBatch = { .game_rotation = 1, .gravity = 0, .gbias = 0 };
        lsm6dsv_fifo_sflp_batch_set(ctx, sflpBatch);
        lsm6dsv_fifo_mode_set(ctx, LSM6DSV_STREAM_MODE);
        printf("[IMU] SFLP+FIFO stream OK, drain_max=%d\r\n", FIFO_DRAIN_MAX);
    }

    return 0;
}

/** @brief  Read one calibrated IMU sample — see imu_lsm6dsv.h. */
int imuRead(imuData_t *out)
{
    if (!imuReady || out == NULL)
        return -1;

    uint32_t t0 = DWT->CYCCNT;

    LSM6DSV_AxesRaw_t accRaw, gyroRaw;

    if (LSM6DSV_ACC_GetAxesRaw(&imuObj, &accRaw) != LSM6DSV_OK)
        return -2;
    if (LSM6DSV_GYRO_GetAxesRaw(&imuObj, &gyroRaw) != LSM6DSV_OK)
        return -3;

    out->rawAx = accRaw.x;
    out->rawAy = accRaw.y;
    out->rawAz = accRaw.z;
    out->rawGx = gyroRaw.x;
    out->rawGy = gyroRaw.y;
    out->rawGz = gyroRaw.z;

    out->ax = (float)accRaw.x * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS - calAx;
    out->ay = (float)accRaw.y * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS - calAy;
    out->az = (float)accRaw.z * ACC_SENSITIVITY_16G * ACC_MG_TO_MSS - calAz;

    out->gx = (float)gyroRaw.x * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS - calGx;
    out->gy = (float)gyroRaw.y * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS - calGy;
    out->gz = (float)gyroRaw.z * GYRO_SENSITIVITY_2000 * GYRO_MDPS_TO_DPS - calGz;

    /* Drift integration */
    uint32_t now = HAL_GetTick();
    float dt = 0.0f;
    if (driftLastTick != 0)
    {
        dt = (float)(now - driftLastTick) * 0.001f;
        driftX += out->gx * dt;
        driftY += out->gy * dt;
        driftZ += out->gz * dt;
    }
    driftLastTick = now;

    out->driftX = driftX;
    out->driftY = driftY;
    out->driftZ = driftZ;

    /* Software quaternion from gyro (always maintained as fallback) */
    if (dt > 0.0f)
        swQuatIntegrate(out->gx, out->gy, out->gz, dt);

    /* SFLP quaternion from FIFO */
    fifoDrainSflp();

    if (sflpValid)
    {
        out->qw = sflpQw;
        out->qx = sflpQx;
        out->qy = sflpQy;
        out->qz = sflpQz;
    }
    else
    {
        out->qw = swQw;
        out->qx = swQx;
        out->qy = swQy;
        out->qz = swQz;
    }

    quatToEuler(out->qw, out->qx, out->qy, out->qz,
                &out->roll, &out->pitch, &out->yaw);

    /* Die temperature */
    {
        int16_t rawTemp = 0;
        stmdev_ctx_t *ctx = &imuObj.Ctx;
        lsm6dsv_temperature_raw_get(ctx, &rawTemp);
        out->tempC = 25.0f + (float)rawTemp / 256.0f;
    }

    /* Timing */
    {
        uint32_t cycles = DWT->CYCCNT - t0;
        uint32_t us = cycles / (HAL_RCC_GetSysClockFreq() / 1000000u);
        diagReadCalls++;
        diagReadTotalUs += us;
        if (us > diagReadMaxUs)
            diagReadMaxUs = us;
    }

    return 0;
}

/** @brief  Read raw uncalibrated accel+gyro — see imu_lsm6dsv.h. */
int imuReadRaw(int16_t accel[3], int16_t gyro[3])
{
    if (!imuReady)
        return -1;

    LSM6DSV_AxesRaw_t accRaw, gyroRaw;

    if (LSM6DSV_ACC_GetAxesRaw(&imuObj, &accRaw) != LSM6DSV_OK)
        return -2;
    if (LSM6DSV_GYRO_GetAxesRaw(&imuObj, &gyroRaw) != LSM6DSV_OK)
        return -3;

    accel[0] = accRaw.x;
    accel[1] = accRaw.y;
    accel[2] = accRaw.z;
    gyro[0]  = gyroRaw.x;
    gyro[1]  = gyroRaw.y;
    gyro[2]  = gyroRaw.z;

    return 0;
}

/** @brief  Return detected gravity axis tag — see imu_lsm6dsv.h. */
const char *imuGetGravAxis(void)
{
    return gravTag;
}

/** @brief  Return calibration offsets — see imu_lsm6dsv.h. */
void imuGetCalOffsets(float *ax, float *ay, float *az,
                      float *gx, float *gy, float *gz)
{
    *ax = calAx;  *ay = calAy;  *az = calAz;
    *gx = calGx;  *gy = calGy;  *gz = calGz;
}

/** @brief  Return SPI/FIFO diagnostic counters — see imu_lsm6dsv.h. */
void imuGetDiag(uint32_t *spiErr, uint32_t *spiRecov,
                uint32_t *fifoOvf, uint32_t *fifoMax,
                uint32_t *sflpCnt)
{
    *spiErr   = diagSpiErrors;
    *spiRecov = diagSpiRecovers;
    *fifoOvf  = diagFifoOverflows;
    *fifoMax  = diagFifoMaxSeen;
    *sflpCnt  = diagSflpSamples;
}

/** @brief  Return imuRead() DWT timing diagnostics — see imu_lsm6dsv.h. */
void imuGetTimingDiag(uint32_t *calls, uint32_t *maxUs, uint32_t *avgUs)
{
    *calls = diagReadCalls;
    *maxUs = diagReadMaxUs;
    *avgUs = (diagReadCalls > 0) ? (diagReadTotalUs / diagReadCalls) : 0;
}

