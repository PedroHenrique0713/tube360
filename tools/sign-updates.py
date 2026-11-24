#!/usr/bin/env python3
"""Ed25519 sobre bytes exatos; a chave privada nunca entra no repositório."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

DEFAULT_KEY = Path.home() / '.config/tube360/update-ed25519.pem'

def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE).stdout

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key', type=Path, default=DEFAULT_KEY)
    sub = p.add_subparsers(dest='command', required=True)
    gen = sub.add_parser('keygen')
    gen.add_argument('--header', type=Path)
    sign = sub.add_parser('sign')
    sign.add_argument('file', type=Path)
    manifest = sub.add_parser('manifest')
    manifest.add_argument('xex', type=Path)
    manifest.add_argument('--build', type=int, required=True)
    manifest.add_argument('--version', required=True)
    manifest.add_argument('--url', required=True)
    manifest.add_argument('--notes-en', required=True)
    manifest.add_argument('--notes-pt', required=True)
    manifest.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    os.umask(0o077)
    if a.command == 'keygen':
        a.key.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        # O_EXCL protege uma chave existente inclusive contra execuções concorrentes.
        fd = os.open(a.key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            pem = run('openssl', 'genpkey', '-algorithm', 'ED25519')
            with os.fdopen(fd, 'wb') as f:
                f.write(pem)
        except BaseException:
            a.key.unlink(missing_ok=True)
            raise
        print('Chave privada:', a.key)
    if a.command == 'manifest':
        raw = a.xex.read_bytes()
        if not raw.startswith(b'XEX2') or not 4 <= len(raw) <= 64*1024*1024:
            p.error('esperado XEX2 com até 64 MiB')
        if not 1 <= a.build <= 2147483647:
            p.error('build fora do limite')
        a.output.write_text(json.dumps(dict(schema=1, kind='tube360-app', build=a.build,
            version=a.version, url=a.url, size=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
            notes=dict(en=a.notes_en, pt=a.notes_pt)), ensure_ascii=False, indent=2)+'\n')
        a.file = a.output
    if a.command in ('sign', 'manifest'):
        sig = run('openssl', 'pkeyutl', '-sign', '-rawin', '-inkey', str(a.key), '-in', str(a.file))
        if len(sig) != 64:
            raise RuntimeError('assinatura Ed25519 inesperada')
        out = Path(str(a.file)+'.sig')
        out.write_text(sig.hex()+'\n')
        print(out)
    if a.command == 'keygen' and a.header:
        public = run('openssl', 'pkey', '-in', str(a.key), '-pubout', '-outform', 'DER')
        if len(public) != 44 or public[:12].hex() != '302a300506032b6570032100':
            raise RuntimeError('chave pública Ed25519 inesperada')
        a.header.write_text('#pragma once\nnamespace updates {\ninline constexpr const char* publicKeyHex = "'+public[12:].hex()+'";\n}\n')
        print('Chave pública:', a.header)

if __name__ == '__main__':
    main()
