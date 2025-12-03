#include "imu.h"

// Global instance
SparkFun_LSM6DSV16X imuDevice;

// Optional helper for debug printing
static void imuPrintError(const char *message)
{
    Serial.print("[IMU] ");
    Serial.println(message);
}

bool imuInit(TwoWire &wire)
{
    // We assume Wire.begin(SDA, SCL) has already been called in main.cpp.
    (void)wire; // avoid unused parameter warning if not used directly

    Serial.println("[IMU] Initialising LSM6DSV…");

    if (!imuDevice.begin())
    {
        imuPrintError("begin() failed – check I2C wiring / address (0x6B vs 0x6A).");
        return false;
    }

    // Reset to a known state
    imuDevice.deviceReset();
    while (!imuDevice.getDeviceReset())
    {
        delay(1);
    }
    Serial.println("[IMU] Device reset complete.");

    // Ensure accel/gyro registers update only on whole samples
    imuDevice.enableBlockDataUpdate();

    // ---------------------------------------------------------
    // Accelerometer configuration
    // ---------------------------------------------------------
    //
    // Application:
    //  - Peak ≈ 3 g over ~10 ms
    //  - Background ≈ 1 g
    //
    // Choose ±8 g FS for headroom and decent resolution.
    // Choose ODR = 960 Hz:
    //  - ~1.04 ms per sample (~9–10 samples across a 10 ms event)
    //  - Nyquist ≈ 480 Hz (far above expected mechanical content)
    //  - Noise density is ODR-independent in HP mode; integrated RMS ∝ sqrt(ODR),
    //    so 960 Hz is a reasonable upper bound without getting silly.
    if (!imuDevice.setAccelFullScale(LSM6DSV16X_8g))
    {
        imuPrintError("setAccelFullScale(8g) failed.");
        return false;
    }

    if (!imuDevice.setAccelDataRate(LSM6DSV16X_ODR_AT_960Hz))
    {
        imuPrintError("setAccelDataRate(960 Hz) failed.");
        return false;
    }

    // Enable accel LP2 filter and choose strong bandwidth to tame high-freq noise.
    imuDevice.enableFilterSettling();
    imuDevice.enableAccelLP2Filter();
    imuDevice.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

    // ---------------------------------------------------------
    // Gyroscope configuration
    // ---------------------------------------------------------
    //
    // Gyro is mainly for orientation/context; match ODR to accel (960 Hz)
    // and use a wide full-scale to avoid clipping.
    if (!imuDevice.setGyroFullScale(LSM6DSV16X_2000dps))
    {
        imuPrintError("setGyroFullScale(2000 dps) failed.");
        return false;
    }

    if (!imuDevice.setGyroDataRate(LSM6DSV16X_ODR_AT_960Hz))
    {
        imuPrintError("setGyroDataRate(960 Hz) failed.");
        return false;
    }

    imuDevice.enableGyroLP1Filter();
    imuDevice.setGyroLP1Bandwidth(LSM6DSV16X_GY_ULTRA_LIGHT);

    // ---------------------------------------------------------
    // INT pins (wired, but we just set them as inputs for now)
    // ---------------------------------------------------------
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    // Later, if you want data-ready IRQs instead of polling, you can:
    //   imuDevice.setAccelStatusInterrupt(true);
    //   imuDevice.int1RouteAccelDataReady(true);
    // and attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT1), isr, RISING);

    Serial.println("[IMU] Init OK (FS=±8 g, 2000 dps; ODR=960 Hz).");
    return true;
}

bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz)
{
    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    // Only read if there is fresh data; avoids re-using old samples.
    if (!imuDevice.checkStatus())
    {
        return false; // no new sample yet
    }

    if (!imuDevice.getAccel(&accelData))
    {
        imuPrintError("getAccel() failed.");
        return false;
    }

    if (!imuDevice.getGyro(&gyroData))
    {
        imuPrintError("getGyro() failed.");
        return false;
    }

    ax = accelData.xData;
    ay = accelData.yData;
    az = accelData.zData;

    gx = gyroData.xData;
    gy = gyroData.yData;
    gz = gyroData.zData;

    return true;
}
