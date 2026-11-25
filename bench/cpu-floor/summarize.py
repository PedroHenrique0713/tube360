#!/usr/bin/env python3
"""Reject partial runs; never use a PC result to give an Xbox decision."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import statistics

NAMES = {"720p30-4000", "1080p30-2500", "1080p30-5500", "1080p60-2500", "1080p60-5500"}
MODES = {"full", "no_loop_filter", "entropy"}


def parse(text):
    if "F30 FAIL" in text or text.count("F30 DONE PASS") != 1 or text.count("F30 START ") != 1:
        raise ValueError("failed, missing or concatenated run")
    lines = text.splitlines()
    start = dict(re.findall(r"(\w+)=([^\s]+)", next(line for line in lines if "F30 START " in line)))
    platform = start["platform"]
    if platform not in {"pc", "xbox"} or start["threads"] != "6" or start["audit"] != "1" or start["ffmpeg"] != "7.1.5":
        raise ValueError("summary needs six threads and audit, with an explicit platform")
    repeats = int(start["repeats"])
    if repeats < 3:
        raise ValueError("summary needs at least three repetitions")
    groups, audits = defaultdict(list), set()
    for line in lines:
        if "F30 AUDIT " in line:
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            key = row["clip"], row["mode"]
            if key in audits or " PASS" not in line or row["syntax_equal"] != "1":
                raise ValueError("invalid or duplicate audit")
            if int(row["frames"]) != (360 if "60-" in row["clip"] else 180) or int(row["parsed_mb"]) != (
                    3600 if row["clip"].startswith("720") else 8160):
                raise ValueError("invalid audit frame/macroblock count")
            if int(row["reconstructed_mb"]) != (0 if row["mode"] == "entropy" else int(row["parsed_mb"])):
                raise ValueError("reconstruction accounting failed")
            audits.add(key)
        if "F30 RESULT " in line:
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            if row["platform"] != platform or row["threads"] != "6" or " PASS" not in line:
                raise ValueError("mixed platforms/threads or failed result")
            if int(row["frames"]) != (360 if "60-" in row["clip"] else 180):
                raise ValueError("frame count")
            groups[row["clip"], row["mode"]].append(row)
    expected = {(name, mode) for name in NAMES for mode in MODES}
    if set(groups) != expected or audits != expected:
        raise ValueError("expected all five clips, three modes and fifteen audits")
    done = next(line for line in lines if "F30 DONE PASS" in line)
    if f"audits=15 results={15*repeats}" not in done:
        raise ValueError("DONE count differs from requested repetitions")
    summary = []
    for (name, mode), rows in sorted(groups.items()):
        if {int(row["repeat"]) for row in rows} != set(range(1, repeats+1)) or len(rows) != repeats:
            raise ValueError("missing or duplicate repetitions")
        fields = [key for key in rows[0] if key.endswith("_ms")]
        item = {"clip": name, "mode": mode, "repeats": repeats}
        for key in fields:
            values = [float(row[key]) for row in rows]
            if not all(0 <= value < 1e7 for value in values):
                raise ValueError("non-finite/negative/outlandish timing")
            item[key] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
        summary.append(item)
    decision = {"1080p30": "pending_console", "1080p60": "pending_console"}
    if platform == "xbox":
        for fps, budget, target in [(30, 1000/30, 29), (60, 1000/60, 13)]:
            rows = [item for item in summary if item["mode"] == "entropy" and item["clip"].startswith(f"1080p{fps}")]
            worst_mean = max(item["mean_ms"]["max"] for item in rows)
            worst_block = max(item["block30_p95_ms"]["max"] for item in rows)
            decision[f"1080p{fps}"] = ("stop_cpu_floor" if worst_mean >= budget else
                                       "prototype_only" if worst_mean <= target and worst_block <= budget else
                                       "marginal_measure_longer")
    return {"schema": 1, "platform": platform, "threads": 6, "results": summary, "decision": decision,
            "note": "Prototype gate only; no export/packing, GPU, audio, network or UI measured. Short synthetic clips."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = parse(args.log.read_text())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("platform clip mode mean_ms completion_p50_ms completion_p95_ms slice_p50_ms slice_p95_ms block30_p95_ms")
    for row in result["results"]:
        print(result["platform"], row["clip"], row["mode"], *(f'{row[key]["median"]:.4f}' for key in
            ["mean_ms", "completion_p50_ms", "completion_p95_ms", "slice_p50_ms", "slice_p95_ms", "block30_p95_ms"]))
    print(json.dumps(result["decision"]))


if __name__ == "__main__":
    main()
