#!/usr/bin/env python3
"""Ed25519 sobre bytes exatos; --key é obrigatório e nunca é copiado para a publicação."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlsplit


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE).stdout


def public_url(url):
    if any(ord(c) < 33 or ord(c) > 126 for c in url):
        return False
    parsed = urlsplit(url)
    if (parsed.scheme != 'https' or parsed.username or parsed.password or parsed.port
            or parsed.fragment or any(c in url for c in ('%', '\\', '/./', '/../'))):
        return False
    return any(url.startswith(prefix) for owner in ('revive360', 'PedroHenrique0713')
               for prefix in (f'https://raw.githubusercontent.com/{owner}/tube360/',
                              f'https://github.com/{owner}/tube360/releases/'))


def test_url(url):
    if any(ord(c) < 33 or ord(c) > 126 for c in url):
        return False
    parsed = urlsplit(url)
    try:
        address = ipaddress.IPv4Address(parsed.hostname)
    except (ValueError, TypeError):
        return False
    return (parsed.scheme == 'http' and (address.is_private or address.is_loopback)
            and parsed.port is not None and not parsed.username and not parsed.password
            and not parsed.query and not parsed.fragment
            and not any(c in url for c in ('%', '\\', '/./', '/../')))


def atomic(path, data):
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key', type=Path, required=True, help='Caminho explícito da chave privada Ed25519')
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
    manifest.add_argument('--allow-test-url', action='store_true', help='Só bancada descartável HTTP na LAN')
    manifest.add_argument('--notes-en', required=True)
    manifest.add_argument('--notes-pt', required=True)
    manifest.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    os.umask(0o077)
    if a.command == 'keygen':
        a.key.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        fd = os.open(a.key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(run('openssl', 'genpkey', '-algorithm', 'ED25519'))
        except BaseException:
            a.key.unlink(missing_ok=True)
            raise
        if a.header:
            public = run('openssl', 'pkey', '-in', str(a.key), '-pubout', '-outform', 'DER')
            if len(public) != 44 or public[:12].hex() != '302a300506032b6570032100':
                raise RuntimeError('chave pública Ed25519 inesperada')
            atomic(a.header, ('#pragma once\nnamespace updates {\ninline constexpr const char* publicKeyHex = "'
                              + public[12:].hex() + '";\n}\n').encode())
        print('Chave criada:', a.key)
        return
    if a.command == 'manifest':
        raw = a.xex.read_bytes()
        if not raw.startswith(b'XEX2') or not 4 <= len(raw) <= 64*1024*1024:
            p.error('esperado XEX2 com até 64 MiB')
        if not 1 <= a.build <= 2147483647 or not re.fullmatch(r'[0-9A-Za-z.+-]{1,40}', a.version):
            p.error('build/versão fora do schema do app')
        if not public_url(a.url) and not (a.allow_test_url and test_url(a.url)):
            p.error('URL fora do repositório público; HTTP local exige --allow-test-url')
        for note in (a.notes_en, a.notes_pt):
            if not 1 <= len(note.encode()) <= 1200 or any(ord(c) < 32 or ord(c) == 127 for c in note):
                p.error('notas devem ter 1–1200 bytes sem controles')
        data = (json.dumps(dict(schema=1, kind='tube360-app', build=a.build,
            version=a.version, url=a.url, size=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
            notes=dict(en=a.notes_en, pt=a.notes_pt)), ensure_ascii=False, indent=2)+'\n').encode()
        target = a.output
    else:
        target, data = a.file, a.file.read_bytes()
    if not data or len(data) > 65536:
        p.error('documento fora do limite de 64 KiB')
    # pkeyutl exige arquivo regular para Ed25519; assina antes de substituir a saída.
    with tempfile.TemporaryDirectory(prefix='tube360-sign-') as folder:
        input_file = Path(folder) / 'document'
        input_file.write_bytes(data)
        sig = run('openssl', 'pkeyutl', '-sign', '-rawin', '-inkey', str(a.key), '-in', str(input_file))
    if len(sig) != 64:
        raise RuntimeError('assinatura Ed25519 inesperada')
    if a.command == 'manifest':
        atomic(target, data)
    output = Path(str(target)+'.sig')
    atomic(output, (sig.hex()+'\n').encode())
    print(output)


if __name__ == '__main__':
    main()
