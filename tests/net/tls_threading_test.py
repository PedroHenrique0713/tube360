"""O mbedTLS do PC precisa sair com os locks de threading: o curl divide a entropia entre os workers HTTPS.
Uma variável CMake comum era descartada em silêncio pelo set(CACHE) do mbedtls e o build ficava sem lock (SIGSEGV em mbedtls_sha512_update)."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
binary = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'client/build/tube360'
symbols = subprocess.run(['nm', str(binary)], check=True, capture_output=True, text=True).stdout
assert 'mbedtls_threading_' in symbols, 'mbedTLS built without MBEDTLS_THREADING_C: ' + str(binary)
cache = (binary.parent / 'CMakeCache.txt').read_text()
assert 'MBEDTLS_USER_CONFIG_FILE:FILEPATH=' + str(ROOT / 'client/src/yt/tls-threading.h') in cache, 'user config not in cache'
print('ok: mbedTLS threading present in', binary.name)
