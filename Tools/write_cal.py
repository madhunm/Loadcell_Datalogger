#!/usr/bin/env python3
"""
Pack a 64-byte factory .cal file for the H562 loadcell datalogger.
CRC16-CCITT (poly 0x1021, init 0xFFFF) over bytes 0..61, little-endian.
"""

from __future__ import annotations

import argparse
import struct
import sys

CAL_FILE_MAGIC = 0x43414C31
CAL_FILE_VER = 1

# Must match Core/Inc/log_record.h CRC16_CCITT_LUT[] and crc16Ccitt() exactly.
_CRC16_CCITT_LUT = (
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
)


def crc16_ccitt(data: bytes) -> int:
    """Bit-identical to firmware crc16Ccitt() in log_record.h."""
    crc = 0xFFFF
    for byte in data:
        idx = ((crc >> 8) ^ byte) & 0xFF
        crc = ((crc << 8) ^ _CRC16_CCITT_LUT[idx]) & 0xFFFF
    return crc


def pack_cal(
    serial: int,
    sensitivity: float,
    cell_corr: float,
    adc_g0: float,
    adc_g1: float,
    off0: float,
    off1: float,
    tare: float,
    batt_div: float,
    prealloc: float,
    adc_crc: float,
    log_usb: float,
) -> bytes:
    """Two-step pack: crc field zero, hash [0:62], repack tail uint16."""
    pad2 = b"\x00\x00"
    reserved6 = b"\x00" * 6
    body = struct.pack(
        "<IIH2s11f6sH",
        CAL_FILE_MAGIC,
        serial & 0xFFFFFFFF,
        CAL_FILE_VER,
        pad2,
        sensitivity,
        adc_g0,
        adc_g1,
        off0,
        off1,
        tare,
        batt_div,
        prealloc,
        adc_crc,
        log_usb,
        cell_corr,
        reserved6,
        0,
    )
    assert len(body) == 64, len(body)
    c = crc16_ccitt(body[0:62])
    out = body[0:62] + struct.pack("<H", c)
    assert len(out) == 64
    return out


def main() -> int:
    p = argparse.ArgumentParser(description="Write a factory .cal binary (64 B).")
    p.add_argument("--serial", type=int, required=True)
    p.add_argument("--sensitivity", type=float, default=0.220919)
    p.add_argument("--cellCorrFactor", type=float, default=1.0)
    p.add_argument("--adcGainCh1", type=float, default=1.0, help="CH0 PGA gain (float)")
    p.add_argument("--adcGainCh2", type=float, default=1.0, help="CH1 PGA gain (float)")
    p.add_argument("--offsetCh1", type=float, default=0.0)
    p.add_argument("--offsetCh2", type=float, default=0.0)
    p.add_argument("--tareOffsetN", type=float, default=0.0)
    p.add_argument("--battDividerRatio", type=float, default=0.5)
    p.add_argument("--preallocMb", type=float, default=64.0)
    p.add_argument("--enableAdcCrc", type=float, default=0.0)
    p.add_argument("--allowLogOnUsb", type=float, default=1.0)
    p.add_argument("--output", "-o", required=True, help="Output path, e.g. 10326.cal")
    args = p.parse_args()

    data = pack_cal(
        args.serial,
        args.sensitivity,
        args.cellCorrFactor,
        args.adcGainCh1,
        args.adcGainCh2,
        args.offsetCh1,
        args.offsetCh2,
        args.tareOffsetN,
        args.battDividerRatio,
        args.preallocMb,
        args.enableAdcCrc,
        args.allowLogOnUsb,
    )

    with open(args.output, "wb") as f:
        f.write(data)

    print(f"Wrote {len(data)} bytes to {args.output}")
    print("Hex:", data.hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
