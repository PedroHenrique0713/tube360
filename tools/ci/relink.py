#!/usr/bin/env python3
"""Run the captured link commands using the matching toolchain and collected files."""
import json
import os
from pathlib import Path
import subprocess

kit = Path(__file__).resolve().parent
for command in json.loads((kit / 'commands.json').read_text()).values():
    command[0] = str(Path(os.environ['XSDK_TOOLCHAIN']) / 'bin' / command[0])
    subprocess.run(command, cwd=kit, check=True)
