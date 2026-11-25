#!/usr/bin/env python3
"""Validate a complete paired F33 run; a decoder bench never qualifies playback."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re
import statistics


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def parse(text):
    if " FAIL" in text or text.count("F33 START ") != 1 or text.count("F33 DONE PASS ") != 1:
        raise ValueError("failed, partial or concatenated run")
    start = fields(next(line for line in text.splitlines() if "F33 START " in line))
    platform = start["platform"]
    if (platform not in {"pc", "xbox"} or start["schema"] != "1" or start["ffmpeg"] != "7.1.5" or
            start["repeats"] != "3" or not 4 <= int(start["threads"]) <= 6 or
            not 0 <= int(start["mask"]) <= 7 or start["filter"] not in {"full", "nonref"}):
        raise ValueError("unsupported run configuration")
    if platform == "xbox" and not int(start["cpu_flags"], 16) & 1:
        raise ValueError("Xbox did not enable VMX")
    names = {"720p30-4000": 180, "1080p30-2500": 180, "1080p30-5500": 180}
    media = [fields(line) for line in text.splitlines() if "F33 MEDIA " in line]
    if len(media) != 1 or media[0]["youtube137"] not in {"0", "1"}:
        raise ValueError("missing media declaration")
    real = media[0]["youtube137"] == "1"
    if real:
        names["youtube137"] = 600
    if platform == "xbox":
        affinity = re.findall(r"decod-worker:\s+(\d+) HW (\d+), afinidade ([\da-fA-F]{8})", text)
        threads = int(start["threads"])
        if len(affinity) != len(names)*8*threads:
            raise ValueError("missing worker placement evidence")
        for index, (worker, hw, status) in enumerate(affinity):
            slot = index % threads
            if int(worker) != slot+1 or int(hw) != (2, 4, 3, 5, 2, 4)[slot] or int(status, 16):
                raise ValueError("failed or changed affinity")
    audits, groups, pending = set(), defaultdict(list), None
    for line in text.splitlines():
        if "F33 AUDIT " in line:
            row = fields(line)
            name = row["clip"]
            if (name not in names or name in audits or " PASS" not in line or
                    row["pixels_equal"] != "1" or row["syntax_equal"] != "1" or
                    int(row["frames"]) != names[name]):
                raise ValueError("invalid/duplicate pixel audit")
            audits.add(name)
        elif "F33 CASE " in line:
            if pending:
                raise ValueError("case without result")
            pending = fields(line)
        elif "F30 RESULT " in line:
            row = fields(line)
            if not pending or " PASS" not in line or row["platform"] != platform or row["mode"] != "full":
                raise ValueError("result outside a FULL case")
            role = pending["role"]
            if (role not in {"candidate", "baseline"} or pending["threads"] != start["threads"] or
                    row["threads"] != start["threads"] or pending["filter"] != start["filter"] or
                    int(pending["mask"]) != (int(start["mask"]) if role == "candidate" else 0)):
                raise ValueError("mixed configurations")
            name = row["clip"]
            if name not in names or int(row["frames"]) != names[name] or int(row["samples"]) != names[name]-42:
                raise ValueError("incomplete decode")
            for key, value in row.items():
                if key.endswith("_ms") and (not math.isfinite(float(value)) or not 0 <= float(value) < 1e7):
                    raise ValueError("invalid timing")
            groups[name, role].append(row)
            pending = None
    expected = {(name, role) for name in names for role in ("candidate", "baseline")}
    done = fields(next(line for line in text.splitlines() if "F33 DONE PASS " in line))
    if (pending or audits != set(names) or set(groups) != expected or
            int(done["audits"]) != len(names) or int(done["results"]) != len(names)*6):
        raise ValueError("incomplete audit/matrix")
    rows = []
    for (name, role), samples in sorted(groups.items()):
        if len(samples) != 3 or {int(r["repeat"]) for r in samples} != {1, 2, 3}:
            raise ValueError("missing/duplicate repetition")
        row = {"clip": name, "role": role}
        for key in ("mean_ms", "completion_p95_ms", "slice_p95_ms", "block30_p95_ms"):
            values = [float(r[key]) for r in samples]
            row[key] = dict(median=statistics.median(values), min=min(values), max=max(values))
        rows.append(row)
    candidate = [r for r in rows if r["role"] == "candidate" and not r["clip"].startswith("720")]
    target = all(r["mean_ms"]["max"] <= 28 and r["block30_p95_ms"]["max"] <= 1000/30 for r in candidate)
    return dict(schema=1, configuration=start, real137=real, results=rows,
                cpu_gate="pending_console" if platform != "xbox" else
                "pending_real137" if not real else "target_met" if target else "optimize_more",
                playback="pending_app_120s_audio_memory_drops")


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("log", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    result = parse(args.log.read_text())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("platform", result["configuration"]["platform"], "cpu_gate", result["cpu_gate"])
    for row in result["results"]:
        print(row["clip"], row["role"], "mean_ms", row["mean_ms"])


if __name__ == "__main__":
    main()
