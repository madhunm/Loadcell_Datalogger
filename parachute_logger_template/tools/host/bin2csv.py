import struct
import csv
import sys

HDR_FMT = "<IHHHHIIIIQIIffffiiiHiII170s"
HDR_SIZE = 256

FRAME_FMT = "<IQiii iii6hHHHH"
FRAME_SIZE = 56

def main(bin_path, csv_path):
    with open(bin_path, "rb") as f:
        hdr = f.read(HDR_SIZE)
        if len(hdr) != HDR_SIZE:
            raise RuntimeError("short header")

        (magic, hver, hsz, fver, fsz,
         build_id, adc_hz, frame_hz, decim,
         start_us, rtc_epoch, rtc_ms,
         slope, offset, acc_g_lsb, gyr_dps_lsb,
         ov, un, comp,
         tare_frames, tare_code,
         flags_static, crc, _reserved) = struct.unpack(HDR_FMT, hdr)

        if magic != 0x314C4450:
            raise RuntimeError("bad magic")
        if fsz != FRAME_SIZE:
            raise RuntimeError(f"frame size mismatch: {fsz} != {FRAME_SIZE}")

        with open(csv_path, "w", newline="") as out:
            w = csv.writer(out)
            w.writerow([
                "sample_index","t_us",
                "force_mean_N","force_peak_N","force_min_N",
                "ax_g","ay_g","az_g","gx_dps","gy_dps","gz_dps",
                "flags","vbat_mV","soc_percent"
            ])

            while True:
                b = f.read(FRAME_SIZE)
                if not b:
                    break
                if len(b) != FRAME_SIZE:
                    break

                (idx, t_us,
                 adc_mean, adc_peak, adc_min,
                 f_mean, f_peak, f_min,
                 ax, ay, az, gx, gy, gz,
                 flags, vbat, soc, pad) = struct.unpack(FRAME_FMT, b)

                w.writerow([
                    idx, t_us,
                    f_mean / 1000.0, f_peak / 1000.0, f_min / 1000.0,
                    ax * acc_g_lsb, ay * acc_g_lsb, az * acc_g_lsb,
                    gx * gyr_dps_lsb, gy * gyr_dps_lsb, gz * gyr_dps_lsb,
                    f"0x{flags:04X}",
                    vbat,
                    soc / 100.0
                ])

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python bin2csv.py LOG0000.BIN LOG0000.csv")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
