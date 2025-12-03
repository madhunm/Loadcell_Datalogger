#include "imu.h"

// Global instance (declared extern in imu.h)
SparkFun_LSM6DSV16X g_imu;

// Simple helper to print errors (optional)
static void imuPrintError(const char *msg)
{
    Serial.print("[IMU] ");
    Serial.println(msg);
}

bool imu_init(TwoWire &wire)
{
    // Assume Wire.begin() was already called in main.cpp.
    // The SparkFun driver uses the default Wire instance internally.
    (void)wire; // avoid unused parameter warning

    Serial.println("[IMU] Initializing LSM6DSV16X…");

    if (!g_imu.begin())
    {
        imuPrintError("begin() failed – check I2C wiring / address.");
        return false;
    }

    // Reset to a known state
    g_imu.deviceReset();
    while (!g_imu.getDeviceReset())
    {
        delay(1);
    }
    Serial.println("[IMU] Device reset complete.");

    // Make sure accel/gyro registers update only on whole samples
    g_imu.enableBlockDataUpdate();

    // ---- Accelerometer configuration ----
    //
    // Goal:
    //  - capture ~3 g peaks over ~10 ms
    //  - typical background ≈ 1 g
    //
    // Choose ±8 g for comfortable headroom and decent resolution.
    // ODR 120 Hz is enough for a 10 ms event (~50 Hz fundamental).
    if (!g_imu.setAccelDataRate(LSM6DSV16X_ODR_AT_120Hz))
    {
        imuPrintError("setAccelDataRate() failed.");
        return false;
    }

    if (!g_imu.setAccelFullScale(LSM6DSV16X_8g))
    {
        imuPrintError("setAccelFullScale() failed.");
        return false;
    }

    // Enable accel LP2 filter and choose a reasonably strong bandwidth.
    g_imu.enableFilterSettling();                 // let filters settle on changes
    g_imu.enableAccelLP2Filter();
    g_imu.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

    // ---- Gyroscope configuration ----
    //
    // Gyro is mostly for orientation/context, not critical for the 3 g pulse,
    // so we mirror the 120 Hz ODR and use a wide full-scale.
    if (!g_imu.setGyroDataRate(LSM6DSV16X_ODR_AT_120Hz))
    {
        imuPrintError("setGyroDataRate() failed.");
        return false;
    }

    if (!g_imu.setGyroFullScale(LSM6DSV16X_2000dps))
    {
        imuPrintError("setGyroFullScale() failed.");
        return false;
    }

    g_imu.enableGyroLP1Filter();
    g_imu.setGyroLP1Bandwidth(LSM6DSV16X_GY_ULTRA_LIGHT);

    // ---- Interrupt pins (wired but not actively used yet) ----
    pinMode(IMU_INT1_PIN, INPUT);
    pinMode(IMU_INT2_PIN, INPUT);
    // Later, if you want data-ready on INT1:
    //   g_imu.setIntAccelDataReady(LSM_PIN_ONE);
    // and then attachInterrupt(IMU_INT1_PIN, yourISR, RISING);

    Serial.println("[IMU] Init OK.");
    return true;
}

bool imu_read(float &ax, float &ay, float &az,
              float &gx, float &gy, float &gz)
{
    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    // Option A: use checkStatus() so we don’t read stale data
    if (!g_imu.checkStatus())
    {
        return false; // no new accel+gyro sample yet
    }

    if (!g_imu.getAccel(&accelData))
        return false;
    if (!g_imu.getGyro(&gyroData))
        return false;

    ax = accelData.xData;
    ay = accelData.yData;
    az = accelData.zData;

    gx = gyroData.xData;
    gy = gyroData.yData;
    gz = gyroData.zData;

    return true;
}
