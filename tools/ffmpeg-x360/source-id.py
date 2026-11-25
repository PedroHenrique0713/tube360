#!/usr/bin/env python3
"""Identity of the pinned build recipe and the complete local LGPL patch set."""
import hashlib
import argparse
from pathlib import Path
import shutil
import subprocess

cli = argparse.ArgumentParser()
cli.add_argument("--cc")
cli.add_argument("--compiler-only", action="store_true")
args = cli.parse_args()

root = Path(__file__).resolve().parents[2]
files = [] if args.compiler_only else [root / "tools/build-ffmpeg-x360.sh"]
if not args.compiler_only:
    files += sorted(p for p in Path(__file__).parent.iterdir() if p.is_file())
digest = hashlib.sha256()
for path in files:
    digest.update(path.relative_to(root).as_posix().encode() + b"\0")
    digest.update(path.read_bytes() + b"\0")
if args.cc:
    cc = Path(shutil.which(args.cc) or args.cc).resolve(strict=True)
    cc1 = Path(subprocess.check_output([str(cc), "-print-prog-name=cc1"], text=True).strip()).resolve(strict=True)
    for path in (cc, cc1):
        digest.update(str(path).encode() + b"\0")
        with path.open("rb") as stream:
            digest.update(hashlib.file_digest(stream, "sha256").digest())
print(digest.hexdigest())
