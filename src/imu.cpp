#include "imu.h"

// Global IMU instance
SparkFun_LSM6DSV16X imuDevice;

bool imuInit(TwoWire &wire)
{
    (void)wire; // Wire is the default I2C bus used inside the SparkFun driver

    Serial.println("[IMU] Initialising LSM6DSV…");

    if (!imuDevice.begin())
    {
        Serial.println("[IMU] begin() failed – check I2C wiring / address (0x6B vs 0x6A).");
        return false;
    }

    // Reset to a known state
    imuDevice.deviceReset();
    while (!imuDevice.getDeviceReset())
    {
        delay(1);
    }
    Serial.println("[IMU] Device reset complete.");

    // Make sure accel/gyro registers update only on complete samples
    imuDevice.enableBlockDataUpdate();

    // ---------------------------------------------------------
    // Accelerometer: ±16 g, 960 Hz
    // ---------------------------------------------------------
    if (!imuDevice.setAccelFullScale(LSM6DSV16X_16g))
    {
        Serial.println("[IMU] setAccelFullScale(16g) failed.");
        return false;
    }
    if (!imuDevice.setAccelDataRate(LSM6DSV16X_ODR_AT_960Hz))
    {
        Serial.println("[IMU] setAccelDataRate(960 Hz) failed.");
        return false;
    }

    imuDevice.enableFilterSettling();
    imuDevice.enableAccelLP2Filter();
    imuDevice.setAccelLP2Bandwidth(LSM6DSV16X_XL_STRONG);

    // ---------------------------------------------------------
    // Gyroscope: ±2000 dps, 960 Hz
    // ---------------------------------------------------------
    if (!imuDevice.setGyroFullScale(LSM6DSV16X_2000dps))
    {
        Serial.println("[IMU] setGyroFullScale(2000 dps) failed.");
        return false;
    }
    if (!imuDevice.setGyroDataRate(LSM6DSV16X_ODR_AT_960Hz))
    {
        Serial.println("[IMU] setGyroDataRate(960 Hz) failed.");
        return false;
    }

    imuDevice.enableGyroLP1Filter();
    imuDevice.setGyroLP1Bandwidth(LSM6DSV16X_GY_ULTRA_LIGHT);

    // Configure interrupt pins as plain inputs for now
    pinMode(PIN_IMU_INT1, INPUT);
    pinMode(PIN_IMU_INT2, INPUT);

    Serial.println("[IMU] Init OK (FS=±16 g, 2000 dps; ODR=960 Hz).");
    return true;
}

bool imuRead(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz)
{
    sfe_lsm_data_t accelData;
    sfe_lsm_data_t gyroData;

    if (!imuDevice.checkStatus())
    {
        // No fresh data yet
        return false;
    }

    if (!imuDevice.getAccel(&accelData))
    {
        Serial.println("[IMU] getAccel() failed.");
        return false;
    }
    if (!imuDevice.getGyro(&gyroData))
    {
        Serial.println("[IMU] getGyro() failed.");
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
