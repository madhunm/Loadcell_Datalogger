#!/usr/bin/env python3
"""Convert PDL binary log to CSV per src/format/log_format.h."""
import argparse
import csv
import struct
import sys

PDL_MAGIC = 0x314C4450
HDR_FMT = "<IHHHHIIIIQIIffffiiiHiII170s"
HDR_SIZE = 256
FRAME_V1_FMT = "<IQ6i6h4H"
FRAME_V1_SIZE = 56
FRAME_V2_FMT = "<IQ6i6h4HQ"
FRAME_V2_SIZE = 64


def main():
    parser = argparse.ArgumentParser(description="Convert PDL .bin log to CSV")
    parser.add_argument("input", help="Input .bin file")
    parser.add_argument("output", help="Output .csv file")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        hdr_b = f.read(HDR_SIZE)
        if len(hdr_b) != HDR_SIZE:
            raise RuntimeError("Short header")

        (
            magic,
            header_ver,
            header_size,
            frame_ver,
            frame_size,
            build_id,
            adc_rate_hz,
            frame_rate_hz,
            decim,
            start_mono_us,
            start_rtc_epoch,
            start_rtc_ms,
            slope_mN_per_code,
            offset_mN,
            accel_g_per_lsb,
            gyro_dps_per_lsb,
            overload_mN,
            underload_mN,
            compression_mN,
            tare_frames,
            tare_adc_code,
            flags_static,
            header_crc32,
            _reserved,
        ) = struct.unpack(HDR_FMT, hdr_b)

        if magic != PDL_MAGIC:
            raise RuntimeError("Bad magic")

        has_imu_ts = frame_ver >= 2 and frame_size >= FRAME_V2_SIZE
        frame_fmt = FRAME_V2_FMT if has_imu_ts else FRAME_V1_FMT
        frame_sz = FRAME_V2_SIZE if has_imu_ts else FRAME_V1_SIZE

        with open(args.output, "w", newline="") as out:
            w = csv.writer(out)
            if has_imu_ts:
                w.writerow(
                    [
                        "sample_index",
                        "t_us",
                        "adc_mean",
                        "adc_peak",
                        "adc_min",
                        "force_mean_mN",
                        "force_peak_mN",
                        "force_min_mN",
                        "ax",
                        "ay",
                        "az",
                        "gx",
                        "gy",
                        "gz",
                        "flags",
                        "vbat_mV",
                        "soc_centiPct",
                        "imu_sample_t_us",
                    ]
                )
            else:
                w.writerow(
                    [
                        "sample_index",
                        "t_us",
                        "adc_mean",
                        "adc_peak",
                        "adc_min",
                        "force_mean_mN",
                        "force_peak_mN",
                        "force_min_mN",
                        "ax",
                        "ay",
                        "az",
                        "gx",
                        "gy",
                        "gz",
                        "flags",
                        "vbat_mV",
                        "soc_centiPct",
                    ]
                )

            while True:
                b = f.read(frame_sz)
                if not b or len(b) != frame_sz:
                    break

                if has_imu_ts:
                    (
                        sample_index,
                        t_us,
                        adc_mean,
                        adc_peak,
                        adc_min,
                        force_mean_mN,
                        force_peak_mN,
                        force_min_mN,
                        ax,
                        ay,
                        az,
                        gx,
                        gy,
                        gz,
                        flags,
                        vbat_mV,
                        soc_centiPct,
                        pad,
                        imu_sample_t_us,
                    ) = struct.unpack(FRAME_V2_FMT, b)
                    w.writerow(
                        [
                            sample_index,
                            t_us,
                            adc_mean,
                            adc_peak,
                            adc_min,
                            force_mean_mN,
                            force_peak_mN,
                            force_min_mN,
                            ax,
                            ay,
                            az,
                            gx,
                            gy,
                            gz,
                            flags,
                            vbat_mV,
                            soc_centiPct,
                            imu_sample_t_us,
                        ]
                    )
                else:
                    (
                        sample_index,
                        t_us,
                        adc_mean,
                        adc_peak,
                        adc_min,
                        force_mean_mN,
                        force_peak_mN,
                        force_min_mN,
                        ax,
                        ay,
                        az,
                        gx,
                        gy,
                        gz,
                        flags,
                        vbat_mV,
                        soc_centiPct,
                        pad,
                    ) = struct.unpack(FRAME_V1_FMT, b)
                    w.writerow(
                        [
                            sample_index,
                            t_us,
                            adc_mean,
                            adc_peak,
                            adc_min,
                            force_mean_mN,
                            force_peak_mN,
                            force_min_mN,
                            ax,
                            ay,
                            az,
                            gx,
                            gy,
                            gz,
                            flags,
                            vbat_mV,
                            soc_centiPct,
                        ]
                    )

    print("Wrote", args.output)


if __name__ == "__main__":
    main()
