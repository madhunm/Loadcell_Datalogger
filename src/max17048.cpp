#include "max17048.h"
#include "pins.h"

static TwoWire *s_wire = nullptr;
static bool s_initialized = false;

// Helper function to read 16-bit register
static uint16_t readRegister16(uint8_t reg)
{
    if (!s_wire) return 0;
    
    s_wire->beginTransmission(MAX17048_I2C_ADDRESS);
    s_wire->write(reg);
    if (s_wire->endTransmission(false) != 0)
    {
        return 0;
    }
    
    if (s_wire->requestFrom(MAX17048_I2C_ADDRESS, (uint8_t)2) != 2)
    {
        return 0;
    }
    
    uint16_t value = s_wire->read();
    value |= (s_wire->read() << 8);
    
    return value;
}

// Helper function to write 16-bit register
static bool writeRegister16(uint8_t reg, uint16_t value)
{
    if (!s_wire) return false;
    
    s_wire->beginTransmission(MAX17048_I2C_ADDRESS);
    s_wire->write(reg);
    s_wire->write(value & 0xFF);        // Low byte
    s_wire->write((value >> 8) & 0xFF); // High byte
    return (s_wire->endTransmission() == 0);
}

bool max17048Init(TwoWire &wire)
{
    s_wire = &wire;
    
    // Check if device is present by reading version register
    uint16_t version = readRegister16(MAX17048_REG_VERSION);
    if (version == 0 || version == 0xFFFF)
    {
        Serial.println("[MAX17048] Device not found on I2C bus");
        s_initialized = false;
        return false;
    }
    
    Serial.printf("[MAX17048] Initialized successfully. Version: 0x%04X\n", version);
    s_initialized = true;
    return true;
}

float max17048ReadVoltage()
{
    if (!s_initialized) return -1.0f;
    
    uint16_t vcell = readRegister16(MAX17048_REG_VCELL);
    if (vcell == 0 && readRegister16(MAX17048_REG_VCELL) == 0)
    {
        return -1.0f; // Error reading
    }
    
    // VCELL is in 78.125 µV units (LSB = 78.125 µV)
    // Convert to Volts: value * 78.125e-6
    return (float)vcell * 78.125e-6f;
}

float max17048ReadSOC()
{
    if (!s_initialized) return -1.0f;
    
    uint16_t soc = readRegister16(MAX17048_REG_SOC);
    if (soc == 0 && readRegister16(MAX17048_REG_SOC) == 0)
    {
        return -1.0f; // Error reading
    }
    
    // SOC is in 1/256% units (LSB = 1/256%)
    // Convert to percent: value / 256.0
    return (float)soc / 256.0f;
}

float max17048ReadChargeRate()
{
    if (!s_initialized) return 0.0f;
    
    uint16_t crate = readRegister16(MAX17048_REG_CRATE);
    if (crate == 0 && readRegister16(MAX17048_REG_CRATE) == 0)
    {
        return 0.0f; // Error reading
    }
    
    // CRATE is in 0.208% per hour units (LSB = 0.208%/hr)
    // Convert to %/hr: value * 0.208
    // Note: Value is signed (two's complement)
    int16_t signedCrate = (int16_t)crate;
    return (float)signedCrate * 0.208f;
}

bool max17048ReadStatus(Max17048Status *status)
{
    if (!s_initialized || !status) return false;
    
    // Read voltage
    status->voltage = max17048ReadVoltage();
    if (status->voltage < 0.0f) return false;
    
    // Read SOC
    status->soc = max17048ReadSOC();
    if (status->soc < 0.0f) return false;
    
    // Read charge rate
    status->chargeRate = max17048ReadChargeRate();
    
    // Read status register
    uint16_t statusReg = readRegister16(MAX17048_REG_STATUS);
    
    // Check alert bit (bit 2)
    status->alert = (statusReg & 0x0004) != 0;
    
    // Check power-on reset bit (bit 1)
    status->powerOnReset = (statusReg & 0x0002) != 0;
    
    return true;
}

bool max17048IsPresent()
{
    if (!s_wire) return false;
    
    uint16_t version = readRegister16(MAX17048_REG_VERSION);
    return (version != 0 && version != 0xFFFF);
}

uint16_t max17048GetVersion()
{
    if (!s_initialized) return 0;
    
    return readRegister16(MAX17048_REG_VERSION);
}


