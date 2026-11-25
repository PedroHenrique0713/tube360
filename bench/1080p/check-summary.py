#!/usr/bin/env python3
"""Calibrate the evidence parser against partial, mixed and forged runs."""
import json
from pathlib import Path
from summarize import parse

bench = Path(__file__).resolve().parent
text = (bench / "results/pc-run.txt").read_text()
result = parse(text)
assert result["cpu_gate"] == "pending_console" and result["real137"]
assert len(result["results"]) == 8
mutants = {
    "truncated": text.split("F33 DONE PASS")[0],
    "missing audit": text.replace("F33 AUDIT", "REMOVED", 1),
    "mixed platform": text.replace("F30 RESULT platform=pc", "F30 RESULT platform=xbox", 1),
    "bad pixels": text.replace("pixels_equal=1", "pixels_equal=0", 1),
    "injected failure": text + "\nF33 FAIL injected\n",
    "concatenated": text + text,
    "nonfinite": text.replace("mean_ms=", "mean_ms=nan discarded=", 1),
    "missing case": text.replace("F33 CASE", "REMOVED", 1),
    "wrong VMX mask": text.replace("role=candidate mask=7", "role=candidate mask=0", 1),
    "no actual Xbox VMX": text.replace("platform=pc", "platform=xbox").replace(
        "cpu_flags=" + result["configuration"]["cpu_flags"], "cpu_flags=0"),
}
rejected = []
for name, mutant in mutants.items():
    try:
        parse(mutant)
    except (ValueError, KeyError):
        rejected.append(name)
    else:
        raise AssertionError(f"accepted invalid evidence: {name}")
(bench / "results/summary-validation.json").write_text(json.dumps(dict(
    rejected=rejected, pc_cannot_qualify_console=True, pixel_audits=4, paired_timing_results=24), indent=2) + "\n")
print("PASS", len(rejected), "invalid logs rejected; PC cannot qualify console")
