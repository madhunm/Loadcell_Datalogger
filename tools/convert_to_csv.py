#!/usr/bin/env python3
"""
Loadcell Log to CSV Converter

Converts binary log files to CSV format for analysis in spreadsheet
applications or other tools.

Usage:
    python convert_to_csv.py <input.bin> [output.csv]
    python convert_to_csv.py <input.bin> --json  # Output as JSON
"""

import argparse
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from loadcell_parser import LogFile, to_csv, to_json


def progress_bar(current: int, total: int, width: int = 50):
    """Print a progress bar"""
    if total == 0:
        return
    percent = current / total
    filled = int(width * percent)
    bar = '█' * filled + '░' * (width - filled)
    sys.stdout.write(f'\r[{bar}] {percent*100:.1f}%')
    sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser(
        description='Convert loadcell binary log files to CSV/JSON',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Convert to CSV (auto-named)
    python convert_to_csv.py data/log_001.bin
    
    # Convert to specific output file
    python convert_to_csv.py data/log_001.bin output/data.csv
    
    # Export as JSON with metadata
    python convert_to_csv.py data/log_001.bin --json
    
    # Include IMU data in CSV
    python convert_to_csv.py data/log_001.bin --include-imu
"""
    )
    
    parser.add_argument('input', help='Input binary log file')
    parser.add_argument('output', nargs='?', help='Output file path')
    parser.add_argument('--json', '-j', action='store_true',
                       help='Export as JSON instead of CSV')
    parser.add_argument('--include-imu', '-i', action='store_true',
                       help='Include IMU data in CSV output')
    parser.add_argument('--include-data', action='store_true',
                       help='Include all sample data in JSON output')
    parser.add_argument('--quiet', '-q', action='store_true',
                       help='Suppress progress output')
    
    args = parser.parse_args()
    
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)
    
    # Determine output path
    if args.output:
        output_path = Path(args.output)
    else:
        ext = '.json' if args.json else '.csv'
        output_path = input_path.with_suffix(ext)
    
    # Parse input file
    if not args.quiet:
        print(f"Reading: {input_path}")
    
    try:
        log = LogFile(str(input_path))
    except Exception as e:
        print(f"Error parsing file: {e}", file=sys.stderr)
        sys.exit(1)
    
    if not log.is_valid:
        print("Warning: File header is invalid", file=sys.stderr)
    
    if not args.quiet:
        print(f"  Duration: {log.duration_seconds:.2f}s")
        print(f"  ADC samples: {log.adc_count:,}")
        print(f"  IMU samples: {log.imu_count:,}")
    
    # Convert
    if not args.quiet:
        print(f"Writing: {output_path}")
    
    try:
        if args.json:
            to_json(log, str(output_path), include_data=args.include_data)
        else:
            callback = None if args.quiet else lambda c, t: progress_bar(c, t)
            rows = to_csv(log, str(output_path), 
                         include_imu=args.include_imu,
                         progress_callback=callback)
            if not args.quiet:
                print()  # Newline after progress bar
                print(f"  Wrote {rows:,} rows")
    except Exception as e:
        print(f"\nError writing output: {e}", file=sys.stderr)
        sys.exit(1)
    
    if not args.quiet:
        print("Done!")


if __name__ == '__main__':
    main()


