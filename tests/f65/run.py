#!/usr/bin/env python3
"""Fila da Música (music_queue.hpp): compila e roda tests/f65/queue_test.cpp. Uso: python3 tests/f65/run.py"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='tube360-f65-') as folder:
    binary = Path(folder) / 'queue_test'
    subprocess.run(['c++', '-std=c++20', '-Wall', '-I' + str(ROOT / 'client/src'), str(ROOT / 'tests/f65/queue_test.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)])
sys.exit(result.returncode)
