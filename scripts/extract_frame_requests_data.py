#!/usr/bin/env python3
"""
Extract target_s, frames_processed, and decode_time_ms from the
"frame_requests" list in a JSON report, print them, and show
mean/median stats for frames_processed and decode_time_ms.

Usage:
    ./extract_frame_requests.py <path_to_json_file>
    python3 extract_frame_requests.py <path_to_json_file>

If no path is given, it looks for "allintra_report.json" in the
current directory.
"""

import json
import sys
import statistics
from pathlib import Path


def load_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except FileNotFoundError:
        sys.exit(f"Error: file not found: {path}")
    except json.JSONDecodeError as e:
        sys.exit(f"Error: '{path}' is not valid JSON ({e})")


def main():
    # Determine input file: from command line arg, or default filename
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        path = Path("allintra_report.json")
        if not path.exists():
            sys.exit(
                "Error: no file given and 'allintra_report.json' not found "
                "in the current directory.\n"
                "Usage: ./extract_frame_requests.py <path_to_json_file>"
            )

    data = load_json(path)

    if "frame_requests" not in data:
        sys.exit(f"Error: '{path}' has no \"frame_requests\" key.")

    frame_requests = data["frame_requests"]
    if not isinstance(frame_requests, list) or not frame_requests:
        sys.exit(f"Error: \"frame_requests\" in '{path}' is empty or not a list.")

    target_s_vals = []
    frames_processed_vals = []
    decode_time_ms_vals = []

    print(f"File: {path}")
    print(f"{'target_s':>10}  {'frames_processed':>17}  {'decode_time_ms':>15}")
    print("-" * 48)

    for i, req in enumerate(frame_requests, start=1):
        missing = [k for k in ("target_s", "frames_processed", "decode_time_ms") if k not in req]
        if missing:
            print(f"  [skipping entry {i}: missing {missing}]")
            continue

        target_s = req["target_s"]
        frames_processed = req["frames_processed"]
        decode_time_ms = req["decode_time_ms"]

        target_s_vals.append(target_s)
        frames_processed_vals.append(frames_processed)
        decode_time_ms_vals.append(decode_time_ms)

        print(f"{target_s:>10}  {frames_processed:>17}  {decode_time_ms:>15.3f}")

    if not frames_processed_vals:
        sys.exit("No valid entries found to summarize.")

    print("-" * 48)
    print("\nSummary statistics:")
    print(f"  frames_processed  -> mean: {statistics.mean(frames_processed_vals):.3f}   "
          f"median: {statistics.median(frames_processed_vals):.3f}   "
          f"max: {max(frames_processed_vals)}")
    print(f"  decode_time_ms    -> mean: {statistics.mean(decode_time_ms_vals):.3f}   "
          f"median: {statistics.median(decode_time_ms_vals):.3f}   "
          f"worst: {max(decode_time_ms_vals):.3f}")

    worst_idx = decode_time_ms_vals.index(max(decode_time_ms_vals))
    max_frames_idx = frames_processed_vals.index(max(frames_processed_vals))
    print(f"\n  Worst decode_time_ms: {decode_time_ms_vals[worst_idx]:.3f} ms "
          f"(target_s={target_s_vals[worst_idx]})")
    print(f"  Max frames_processed: {frames_processed_vals[max_frames_idx]} "
          f"(target_s={target_s_vals[max_frames_idx]})")


if __name__ == "__main__":
    main()
