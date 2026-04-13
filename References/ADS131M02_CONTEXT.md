# ADS131M02 Driver Context Reference

Source: ADS131M02 Datasheet (SBAS853A, Jan 2020, Rev Apr 2021) + TI reference driver + MikroE ADC15 Click driver.

This file is a machine-readable context reference for writing the ADS131M02 driver on the STM32H562 platform.

---

## Device Overview

- 2-channel, simultaneously-sampling, 24-bit delta-sigma ADC
- Programmable data rate: 250 SPS to 64 kSPS (HR mode, fCLKIN = 8.192 MHz)
- Programmable gain: 1, 2, 4, 8, 16, 32, 64, 128
- Internal 1.2V reference (FSR = +/-1.2V / Gain)
- SPI Mode 1 (CPOL=0, CPHA=1)
- Package: 20-pin TSSOP or WQFN

---

## Pin Connections (this board)

| Signal | MCU Pin | Direction | Notes |
|--------|---------|-----------|-------|
| ADC_CS | PA4 | Output | Active low, software-controlled |
| ADC_DRDY | PA2 | Input | EXTI2, falling edge |
| ADC_Reset | PA3 | Output | Active low hardware reset (SYNC/RESET) |
| SCLK | PA5 | SPI1 | 12.5 MHz (PLL1Q/4) |
| MOSI (DIN) | PA7 | SPI1 | |
| MISO (DOUT) | PA6 | SPI1 | |
| CLKIN | PC6 | LTC6903 output | 8.192 MHz (trimmed at boot) |

SPI1 config: Mode 1, 8-bit, MSB-first, software NSS, 12.5 MHz, full-duplex, DMA channels linked (CH0=TX, CH1=RX).

---

## SPI Frame Format

Communication is frame-based. Each frame = N words. Word size is configurable (16/24/32 bits) via MODE register WLENGTH[1:0]. **Default = 24-bit words.**

### Frame structure (24-bit word mode, ADS131M02 = 2 channels):

**4 words per frame = 12 bytes total**

| Word | DIN (host to ADC) | DOUT (ADC to host) |
|------|--------------------|--------------------|
| 0 | Command (16 bits, MSB-aligned, zero-padded to 24 bits) | Response to PREVIOUS command / STATUS |
| 1 | Data / zeros | Channel 0 ADC data (24-bit, signed) |
| 2 | Data / zeros | Channel 1 ADC data (24-bit, signed) |
| 3 | CRC / zeros | CRC (16 bits, MSB-aligned) |

**Critical rule:** DOUT carries the response to the command from the PREVIOUS frame. Register reads require two frames: Frame 1 = RREG command, Frame 2 = NULL to clock out register data.

### Byte order within each word:
- MSB first (big-endian)
- 24-bit word: [byte0=bits 23:16] [byte1=bits 15:8] [byte2=bits 7:0]
- 16-bit register values are MSB-aligned in 24-bit words: [MSB] [LSB] [0x00]
- 24-bit ADC data occupies all 3 bytes: [MSB] [MID] [LSB]

---

## Command Opcodes

| Command | Word (16-bit) | Description | Response (next frame) |
|---------|---------------|-------------|----------------------|
| NULL | 0x0000 | No operation | STATUS register |
| RESET | 0x0011 | Reset device to defaults | 0xFF22 (ack) |
| STANDBY | 0x0022 | Enter low-power standby | 0x0022 (echo) |
| WAKEUP | 0x0033 | Exit standby | 0x0033 (echo) |
| LOCK | 0x0555 | Lock interface (only NULL/RREG/UNLOCK accepted) | 0x0555 (echo) |
| UNLOCK | 0x0655 | Unlock interface | 0x0655 (echo) |
| RREG | 0xA000 | OR with address/count | See below |
| WREG | 0x6000 | OR with address/count | See below |

### RREG command word format: `101a aaaa annn nnnn`
- Bits [15:13] = 101 (RREG prefix)
- Bits [12:7] = register address (6 bits, supports 0x00-0x3F)
- Bits [6:0] = number of registers to read minus 1

**Encoding: `RREG_OPCODE | (addr << 7) | (count - 1)`**

Single register read (count=1): `0xA000 | (addr << 7)`
- Response: register data appears as Word 0 of the NEXT frame

### WREG command word format: `011a aaaa annn nnnn`
- Same bit layout as RREG but prefix = 011
- Data words follow immediately after command word in same frame

**Encoding: `WREG_OPCODE | (addr << 7) | (count - 1)`**

Single register write: command word + data word in same frame (total >= 4 words).
- Response (next frame): `010a aaaa ammm mmmm` where mmm mmmm = registers actually written - 1

### MikroE discrepancy note:
The MikroE driver uses `(addr << 8)` instead of `(addr << 7)`. The datasheet clearly specifies the address field as bits [12:7] = 6 bits. The TI `(addr << 7)` convention is correct per the datasheet command table: "101a aaaa annn nnnn". Use `(addr << 7)`.

---

## Register Map

### Device Settings (Read-Only)

| Addr | Name | Reset | Description |
|------|------|-------|-------------|
| 0x00 | ID | 0x22xx | Bits [15:12]=0010, [11:8]=CHANCNT (0010=2ch), [7:0]=REVID |
| 0x01 | STATUS | 0x0500 | See STATUS bits below |

### Global Settings

| Addr | Name | Reset | Description |
|------|------|-------|-------------|
| 0x02 | MODE | 0x0510 | SPI config, word length, DRDY format, CRC enables |
| 0x03 | CLOCK | 0x030E | Channel enables, TBM, OSR, power mode |
| 0x04 | GAIN1 | 0x0000 | PGA gain for CH0 and CH1 |
| 0x05 | RESERVED | 0x0000 | Always write 0x0000 |
| 0x06 | CFG | 0x0600 | Global-chop config, current-detect config |
| 0x07 | THRSHLD_MSB | 0x0000 | Current-detect threshold MSB |
| 0x08 | THRSHLD_LSB | 0x0000 | Current-detect threshold LSB + DC block filter |

### Channel-Specific Settings

| Addr | Name | Reset | Description |
|------|------|-------|-------------|
| 0x09 | CH0_CFG | 0x0000 | Phase cal, DC block, MUX select for CH0 |
| 0x0A | CH0_OCAL_MSB | 0x0000 | Offset calibration CH0 (upper 16 of 24 bits) |
| 0x0B | CH0_OCAL_LSB | 0x0000 | Offset calibration CH0 (lower 8 bits + reserved) |
| 0x0C | CH0_GCAL_MSB | 0x8000 | Gain calibration CH0 (upper 16 of 24 bits; 0x8000 = gain 1.0) |
| 0x0D | CH0_GCAL_LSB | 0x0000 | Gain calibration CH0 (lower 8 bits + reserved) |
| 0x0E | CH1_CFG | 0x0000 | Phase cal, DC block, MUX select for CH1 |
| 0x0F | CH1_OCAL_MSB | 0x0000 | Offset calibration CH1 |
| 0x10 | CH1_OCAL_LSB | 0x0000 | Offset calibration CH1 |
| 0x11 | CH1_GCAL_MSB | 0x8000 | Gain calibration CH1 |
| 0x12 | CH1_GCAL_LSB | 0x0000 | Gain calibration CH1 |
| 0x3E | REGMAP_CRC | 0x0000 | Register map CRC (read-only when REG_CRC_EN=1) |

---

## Key Register Bit Fields

### STATUS (0x01) - Read-Only

| Bit | Name | Description |
|-----|------|-------------|
| 15 | LOCK | 1=interface locked |
| 14 | F_RESYNC | 1=resynchronization occurred |
| 13 | REG_MAP | 1=register map CRC changed |
| 12 | CRC_ERR | 1=input CRC error |
| 11 | CRC_TYPE | 0=CCITT, 1=ANSI |
| 10 | RESET | 1=reset occurred (set after POR/reset) |
| 9:8 | WLENGTH | 00=16bit, 01=24bit, 10=32bit-zeropad, 11=32bit-signext |
| 7:2 | RESERVED | Always 0 |
| 1 | DRDY1 | 1=CH1 data available |
| 0 | DRDY0 | 1=CH0 data available |

### MODE (0x02) - Reset = 0x0510

| Bit | Name | Reset | Description |
|-----|------|-------|-------------|
| 15:14 | RESERVED | 00 | Write 00 |
| 13 | REG_CRC_EN | 0 | Register map CRC enable |
| 12 | RX_CRC_EN | 0 | SPI input CRC enable |
| 11 | CRC_TYPE | 0 | 0=CCITT, 1=ANSI |
| 10 | RESET | 1 | Write 0 to clear reset flag in STATUS |
| 9:8 | WLENGTH | 01 | Word length (01=24-bit default) |
| 7:5 | RESERVED | 000 | Write 000 |
| 4 | TIMEOUT | 1 | SPI timeout enable |
| 3:2 | DRDY_SEL | 00 | DRDY source: 00=most lagging ch |
| 1 | DRDY_HiZ | 0 | 0=push-pull, 1=open-drain |
| 0 | DRDY_FMT | 0 | 0=level, 1=pulse |

### CLOCK (0x03) - Reset = 0x030E

| Bit | Name | Reset | Description |
|-----|------|-------|-------------|
| 15:10 | RESERVED | 000000 | Read-only zeros |
| 9 | CH1_EN | 1 | Channel 1 enable |
| 8 | CH0_EN | 1 | Channel 0 enable |
| 7:6 | RESERVED | 00 | Write 00 |
| 5 | TBM | 0 | Turbo mode: 1=OSR 64 override (M02-SPECIFIC) |
| 4:2 | OSR[2:0] | 011 | OSR: 000=128, 001=256, 010=512, 011=1024, 100=2048, 101=4096, 110=8192, 111=16384 |
| 1:0 | PWR[1:0] | 10 | Power: 00=VLP, 01=LP, 10=HR, 11=HR |

**For 64 kSPS with fCLKIN = 8.192 MHz:**
- fDATA = fMOD / OSR = 4.096 MHz / 64 = 64 kSPS
- Need TBM=1 (forces OSR=64), PWR=10 (HR), CH0_EN=1, CH1_EN=1
- CLOCK = 0x0322 = 0000_0011_0010_0010

**Verification:** 0x0322 decoded:
- Bits [15:10] = 000000 (reserved)
- Bit 9 = 1 (CH1_EN)
- Bit 8 = 1 (CH0_EN)
- Bits [7:6] = 00 (reserved)
- Bit 5 = 1 (TBM = Turbo mode ON -> OSR=64)
- Bits [4:2] = 000 (OSR=128, but overridden by TBM=1 -> OSR=64)
- Bits [1:0] = 10 (HR mode)

### GAIN1 (0x04) - Reset = 0x0000

| Bit | Name | Description |
|-----|------|-------------|
| 15:8 | RESERVED | Write 0 |
| 7 | RESERVED | Write 0 |
| 6:4 | PGAGAIN1[2:0] | CH1 PGA gain: 000=1, 001=2, 010=4, 011=8, 100=16, 101=32, 110=64, 111=128 |
| 3 | RESERVED | Write 0 |
| 2:0 | PGAGAIN0[2:0] | CH0 PGA gain: same encoding as CH1 |

---

## Data Rate Table (HR mode, fCLKIN = 8.192 MHz)

| TBM | OSR[2:0] | Effective OSR | fDATA |
|-----|----------|---------------|-------|
| 1 | xxx | 64 | 64 kSPS |
| 0 | 000 | 128 | 32 kSPS |
| 0 | 001 | 256 | 16 kSPS |
| 0 | 010 | 512 | 8 kSPS |
| 0 | 011 | 1024 | 4 kSPS (default) |
| 0 | 100 | 2048 | 2 kSPS |
| 0 | 101 | 4096 | 1 kSPS |
| 0 | 110 | 8192 | 500 SPS |
| 0 | 111 | 16384 | 250 SPS |

---

## Reset Sequence

### Hardware reset (via SYNC/RESET pin = ADC_Reset = PA3):
1. Assert PA3 LOW for >= 1 ms
2. Release PA3 HIGH
3. Wait for DRDY rising edge (or wait tPOR ~ 0.5 ms + tREGACQ)
4. First SPI response after reset: NULL command returns reset acknowledgment pattern

### Software reset (via SPI RESET command):
1. Send RESET command (0x0011) in a full 4-word frame (12 bytes)
2. Command latches at end of frame -- frame MUST be complete (all 4 words clocked)
3. Wait tREGACQ before next SPI communication
4. Response in next frame: 0xFF22 (for ADS131M02)

### Post-reset state:
- All registers at default values
- CLOCK = 0x030E (CH0+CH1 enabled, OSR=1024, HR, fDATA=4kSPS)
- MODE = 0x0510 (24-bit words, timeout enabled, DRDY level format)
- STATUS bit 10 (RESET) = 1
- Interface is UNLOCKED (can write registers immediately)
- Fast-settling filter active for first 2 conversions, then sinc3

---

## Init Sequence for 64 kSPS Operation

1. Hardware reset (PA3 low 1ms, high, wait 50ms for safe margin)
2. Send NULL command -- expect STATUS with RESET bit set (bit 10 = 1)
3. (Optional) Send UNLOCK if device was previously locked
4. Read ID register (0x00) -- verify upper byte = 0x22
5. Write MODE = 0x0110 (clear RESET bit, keep 24-bit words, disable timeout for DMA operation)
   - Or keep MODE = 0x0510 (default with timeout) if polling
6. Write CLOCK = 0x0322 (CH0+CH1 enabled, TBM=1, OSR=64, HR, fDATA=64kSPS)
7. Write GAIN1 = 0x0000 (PGA gain 1x on both channels)
8. Read back all written registers to verify
9. (Optional) Send LOCK to prevent accidental register changes

---

## ADC Data Sign Extension (24-bit mode)

ADC data is 24-bit two's complement. To convert to int32_t:

```c
static inline int32_t ads_sign_extend_24(uint32_t raw24)
{
    if (raw24 & 0x800000)
        return (int32_t)(raw24 | 0xFF000000);
    else
        return (int32_t)(raw24);
}
```

Or equivalently (TI style):
```c
return (int32_t)(raw24 << 8) >> 8;
```

---

## CRC Details

- Polynomial CCITT (default): x^16 + x^12 + x^5 + 1 = 0x1021
- Polynomial ANSI (optional): x^16 + x^15 + x^2 + 1 = 0x8005
- Seed: 0xFFFF
- Coverage: all words in frame (including zero-padding in 24/32-bit modes)
- Output CRC always present (cannot be disabled); input CRC optional (RX_CRC_EN in MODE)
- CRC word is 16-bit, MSB-aligned in the word

---

## Timing Constraints

| Parameter | Value | Notes |
|-----------|-------|-------|
| tPOR | 0.5 ms | Power-on reset time |
| tREGACQ | < 1 ms | Register acquisition time after reset |
| SPI max SCLK | 25 MHz | (this board: 12.5 MHz, 50% margin) |
| SCLK min period | 64 ns (DVDD >= 2.7V) | |
| CS to SCLK setup | 10 ns | |
| CLKIN max (HR) | 8.4 MHz | (this board: 8.192 MHz) |
| DMA transfer time | ~7.7 us | 12 bytes at 12.5 MHz |
| DRDY period (64kSPS) | 15.625 us | fCLKIN / 128 (with TBM=1, OSR=64) |

**Timing margin at 64 kSPS:**
- DRDY period: 15.625 us
- SPI frame time: ~7.7 us (12 bytes at 12.5 MHz)
- Available margin: ~7.9 us (51% of period)

---

## Reference Driver Notes

### TI driver (References/TI/ads131m0x.h, ads131m0x.c):
- Generic M0x family driver (M02/M04/M06/M08)
- CHANNEL_COUNT = 4 in this copy (need to mentally adjust for M02 = 2)
- Uses `(addr << 7)` for RREG/WREG -- CORRECT per datasheet
- Frame size = (CHANNEL_COUNT + 2) words = 4 words for M02
- CRC seed = 0xFFFF, supports CCITT and ANSI
- `signExtend()` handles 16/24/32-bit modes
- `adcStartup()`: toggleRESET -> restoreDefaults -> NULL -> write CLOCK -> write MODE

### MikroE driver (References/MikroE/adc15.h, adc15.c):
- Specific to ADC15 Click = ADS131M02
- Uses `(addr << 8)` for RREG/WREG -- INCORRECT per datasheet but works for low addresses
- CLOCK value 0x0322 for 64 kSPS -- CORRECT and validated on hardware
- `adc15_default_cfg()`: set LTC6903 freq -> HW reset -> set 24-bit words -> write CLOCK 0x0322 -> set gains
- `adc15_read_adc_value()`: 4-word NULL frame, CRC16-CCITT validation
- Always uses 4 words per frame (correct for M02)

### Key difference:
TI uses `(addr << 7)`, MikroE uses `(addr << 8)`. Datasheet confirms TI is correct:
"RREG: 101a aaaa annn nnnn" = 6-bit address in bits [12:7].
For addresses 0-6 (common init registers), both produce the same result because bit 12 is 0 for these addresses. Difference shows at address >= 64 (which doesn't exist) or in the nnn nnnn field interpretation.

**Use TI convention `(addr << 7)` for correctness.**
