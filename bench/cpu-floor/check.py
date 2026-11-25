#!/usr/bin/env python3
"""Independent pixel oracle and rejection checks, after a complete PC run with --trace."""
import csv
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from generate import BENCH, SPECS
from summarize import parse


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("--log", type=Path, default=BENCH / "results/pc-run.txt")
    cli.add_argument("--output", type=Path, default=BENCH / "results/verification-pc.json")
    args = cli.parse_args()
    log = args.log.read_text()
    summary = parse(log)
    assert summary["platform"] == "pc"
    assert set(summary["decision"].values()) == {"pending_console"}
    rejected = []
    for name, bad in [
        ("truncated", log.split("F30 DONE PASS")[0]),
        ("missing audit", log.replace("F30 AUDIT", "REMOVED", 1)),
        ("mixed platform", log.replace("F30 RESULT platform=pc", "F30 RESULT platform=xbox", 1)),
        ("failed", log + "\nF30 FAIL injected\n"),
        ("duplicated run", log + log),
        ("missing repetition", "\n".join(line for line in log.splitlines()
                                        if not ("F30 RESULT" in line and "clip=720p30-4000" in line and
                                                "mode=full" in line and "repeat=1" in line))),
    ]:
        try:
            parse(bad)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError(f"summarizer accepted {name}")
    traces = BENCH / ".work/trace-pc"
    evidence = []
    for name, _, _, fps, _, _ in SPECS:
        mode_rows = {}
        for mode in ("full", "no_loop_filter", "entropy"):
            path = traces / f"{name}-{mode}.tsv"
            with path.open() as file:
                rows = list(csv.DictReader(file, delimiter="\t"))
            assert len(rows) == fps*6
            assert {row["type"] for row in rows} == {"I", "P", "B"}
            mode_rows[mode] = rows
            evidence.append({"clip": name, "mode": mode, "frames": len(rows), "trace_sha256":
                             hashlib.sha256(path.read_bytes()).hexdigest()})
        fields = ("pts", "type", "parsed", "syntax_hash", "nnz_hash", "coeff_hash", "prediction_hash")
        expected = [[row[key] for key in fields] for row in mode_rows["full"]]
        for mode, rows in mode_rows.items():
            assert [[row[key] for key in fields] for row in rows] == expected, (name, mode)
            if mode == "entropy":
                assert all(row["reconstructed"] == "0" and not row["pixel_md5"] for row in rows)
                continue
            command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-threads", "2"]
            if mode == "no_loop_filter":
                command += ["-skip_loop_filter", "all"]
            command += ["-i", str(BENCH / "media" / f"{name}.mp4"), "-an", "-f", "framemd5", "-"]
            output = subprocess.check_output(command, text=True)
            hashes = [line.rsplit(",", 1)[1].strip() for line in output.splitlines() if not line.startswith("#")]
            assert hashes == [row["pixel_md5"] for row in rows], (name, mode, "pixel oracle")
        assert any(a["pixel_md5"] != b["pixel_md5"] for a, b in
                   zip(mode_rows["full"], mode_rows["no_loop_filter"])), "loop filter control must change pixels"
    # O executável também precisa rejeitar mídia incompleta, sem devolver PASS parcial.
    with tempfile.TemporaryDirectory(prefix="bad-media-", dir=BENCH / ".work") as temp:
        data = (BENCH / "media/720p30-4000.mp4").read_bytes()
        (Path(temp) / "720p30-4000.mp4").write_bytes(data[:len(data)//2])
        result = subprocess.run([str(BENCH / "build-pc/cpu-floor"), "--media", temp, "--clip", "0", "--repeats", "0"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert result.returncode != 0 and "F30 FAIL" in result.stdout and "F30 DONE PASS" not in result.stdout
    report = {"platform": "pc", "oracle": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
              "pixel_frames_compared": sum(item[3]*6*2 for item in SPECS), "pixel_mismatches": 0,
              "parse_traces": evidence, "rejected_logs": rejected, "truncated_media_rejected": True}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f'F30 CHECK PASS pixel_frames={report["pixel_frames_compared"]} mismatches=0 '
          f'parse_traces={len(evidence)} rejected_logs={len(rejected)} truncated_media_rejected=1')


if __name__ == "__main__":
    main()
