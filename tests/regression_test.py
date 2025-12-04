import argparse
import csv
import subprocess
import sys
import os
import tempfile
from typing import Dict, Tuple, Optional, List

def run_websift(binary_path, input_warc, limit, output_csv):
    cmd = [
        binary_path,
        input_warc,
        "--limit", str(limit),
        "--csv-output", output_csv
    ]
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("Error running websift:")
        print(result.stderr)
        sys.exit(1)
    return result.stdout

def _load_csv(path: str, required_cols: List[str]) -> Tuple[Optional[Dict[str, Dict[str, str]]], Optional[str]]:
    """
    Load a CSV into a dict keyed by record_id. Returns (data, error_msg).
    """
    data: Dict[str, Dict[str, str]] = {}
    try:
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            if reader.fieldnames is None:
                return None, "CSV has no header row."
            missing = [c for c in required_cols if c not in reader.fieldnames]
            if missing:
                return None, f"Missing required columns {missing} in {path}"
            for row in reader:
                rid = row.get("record_id")
                if not rid:
                    return None, f"Encountered row without record_id in {path}"
                data[rid] = {
                    "status": row.get("status", ""),
                    "reason": row.get("reason", "") or "",
                }
    except Exception as e:
        return None, f"Error reading {path}: {e}"
    return data, None


def compare_csv(baseline_path, current_path):
    required_cols = ['record_id', 'status', 'reason']

    base_map, err = _load_csv(baseline_path, required_cols)
    if err:
        print(err)
        return False

    curr_map, err = _load_csv(current_path, required_cols)
    if err:
        print(err)
        return False

    all_ids = set(base_map.keys()) | set(curr_map.keys())
    mismatches = []
    
    for rid in all_ids:
        if rid not in base_map:
            mismatches.append({'id': rid, 'type': 'extra_in_current'})
            continue
        if rid not in curr_map:
            mismatches.append({'id': rid, 'type': 'missing_in_current'})
            continue
            
        row_base = base_map[rid]
        row_curr = curr_map[rid]
        
        if row_base['status'] != row_curr['status']:
            mismatches.append({
                'id': rid, 
                'type': 'status_mismatch', 
                'base': row_base['status'], 
                'curr': row_curr['status'],
                'reason_base': row_base['reason'],
                'reason_curr': row_curr['reason']
            })
        elif row_base['reason'] != row_curr['reason']:
             mismatches.append({
                'id': rid, 
                'type': 'reason_mismatch', 
                'base': row_base['reason'], 
                'curr': row_curr['reason']
            })

    if not mismatches:
        print("\nSUCCESS: Results match baseline perfectly.")
        return True
    else:
        print(f"\nFAILURE: Found {len(mismatches)} mismatches.")
        for m in mismatches[:10]:
            print(m)
        if len(mismatches) > 10:
            print("...")

        # Save diff report
        diff_path = "tests/diff_report.csv"
        with open(diff_path, "w", newline="") as f:
            fieldnames = sorted({k for m in mismatches for k in m.keys()})
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(mismatches)
        print(f"Diff report saved to {diff_path}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Run regression test for websift")
    parser.add_argument("--binary", default="./build/websift", help="Path to websift binary")
    parser.add_argument("--input", required=True, help="Path to input WARC file")
    parser.add_argument("--baseline", default="tests/test_data/baseline.csv", help="Path to baseline CSV")
    parser.add_argument("--limit", type=int, default=1000, help="Number of records to process")
    
    args = parser.parse_args()

    if not os.path.exists(args.binary):
        print(f"Binary not found: {args.binary}")
        sys.exit(1)
        
    if not os.path.exists(args.input):
        print(f"Input file not found: {args.input}")
        sys.exit(1)

    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as tmp:
        output_csv = tmp.name

    try:
        print(f"Running test with limit {args.limit}...")
        run_websift(args.binary, args.input, args.limit, output_csv)
        
        print("Comparing results...")
        success = compare_csv(args.baseline, output_csv)
        
        if not success:
            sys.exit(1)
            
    finally:
        if os.path.exists(output_csv):
            os.remove(output_csv)

if __name__ == "__main__":
    main()
