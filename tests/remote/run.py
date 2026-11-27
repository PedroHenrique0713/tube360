#!/usr/bin/env python3
"""Controle pelo celular (#46): compila e roda remote_test.cpp (pareamento, recusa sem código, fila, servidor com cliente TCP falso).
Uso: tests/remote/run.py"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'remote_test'
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-pthread', '-I' + str(ROOT / 'client/src'), str(ROOT / 'tests/remote/remote_test.cpp'),
                    '-lSDL3', '-o', str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
