#!/usr/bin/env python3
"""
Loadcell Log File Validation Tool

Validates binary log files for integrity, checking:
- File header and footer
- CRC32 checksum
- Sequence number gaps
- Sample counts

Usage:
    python validate_log.py <logfile.bin> [--no-crc] [--json]
    python validate_log.py --batch <directory> [--json]
"""

import argparse
import sys
import json
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from loadcell_parser import LogFile, validate_file, ValidationReport


def main():
    parser = argparse.ArgumentParser(
        description='Validate loadcell binary log files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Validate a single file
    python validate_log.py data/log_001.bin
    
    # Validate all .bin files in a directory
    python validate_log.py --batch data/
    
    # Output as JSON
    python validate_log.py data/log_001.bin --json
    
    # Skip CRC check (faster)
    python validate_log.py data/log_001.bin --no-crc
"""
    )
    
    parser.add_argument('filepath', nargs='?', help='Path to log file')
    parser.add_argument('--batch', '-b', metavar='DIR', 
                       help='Validate all .bin files in directory')
    parser.add_argument('--no-crc', action='store_true',
                       help='Skip CRC32 verification (faster)')
    parser.add_argument('--no-gaps', action='store_true',
                       help='Skip sequence gap checking')
    parser.add_argument('--json', '-j', action='store_true',
                       help='Output results as JSON')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Show detailed output')
    
    args = parser.parse_args()
    
    if not args.filepath and not args.batch:
        parser.print_help()
        sys.exit(1)
    
    # Collect files to validate
    files = []
    if args.batch:
        batch_dir = Path(args.batch)
        if not batch_dir.is_dir():
            print(f"Error: {args.batch} is not a directory", file=sys.stderr)
            sys.exit(1)
        files = list(batch_dir.glob('*.bin'))
        if not files:
            print(f"No .bin files found in {args.batch}", file=sys.stderr)
            sys.exit(1)
    else:
        files = [Path(args.filepath)]
    
    # Validate files
    results = []
    all_valid = True
    
    for filepath in files:
        if not filepath.exists():
            print(f"Error: File not found: {filepath}", file=sys.stderr)
            all_valid = False
            continue
        
        report = validate_file(
            str(filepath),
            check_crc=not args.no_crc,
            check_gaps=not args.no_gaps
        )
        
        results.append(report)
        if not report.is_valid:
            all_valid = False
    
    # Output results
    if args.json:
        output = []
        for r in results:
            output.append({
                'filepath': r.filepath,
                'is_valid': r.is_valid,
                'header_valid': r.header_valid,
                'footer_valid': r.footer_valid,
                'footer_present': r.footer_present,
                'crc_valid': r.crc_valid,
                'crc_expected': f"0x{r.crc_expected:08X}",
                'crc_computed': f"0x{r.crc_computed:08X}",
                'adc_count': r.adc_count,
                'imu_count': r.imu_count,
                'gaps_count': len(r.gaps),
                'total_missing': r.total_missing,
                'errors': r.errors,
                'warnings': r.warnings,
            })
        print(json.dumps(output if len(output) > 1 else output[0], indent=2))
    else:
        for report in results:
            print(report.summary())
            print()
    
    # Exit code
    sys.exit(0 if all_valid else 1)


if __name__ == '__main__':
    main()

