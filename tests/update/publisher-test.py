#!/usr/bin/env python3
"""Contratos da ferramenta de publicação, sempre com chave descartável."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/sign-updates.py'


def call(*args, success=True):
    result = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True)
    assert (result.returncode == 0) == success, result.stderr.decode()
    return result


with tempfile.TemporaryDirectory(prefix='tube360-publisher-') as temporary:
    folder = Path(temporary)
    key = folder / 'key.pem'
    call('--key', key, 'keygen')
    original_key = key.read_bytes()
    call('--key', key, 'keygen', success=False)
    assert key.read_bytes() == original_key
    payload = folder / 'payload.xex'
    payload.write_bytes(b'XEX2test publisher payload')
    output = folder / 'app.json'
    args = ['--key', key, 'manifest', payload, '--build', '42', '--version', '0.3.2',
            '--url', 'https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.2/tube360.xex',
            '--notes-en', 'Exact packaged payload.', '--notes-pt', 'Payload exato do pacote.', '--output', output]
    call(*args)
    data = json.loads(output.read_bytes())
    assert data['size'] == payload.stat().st_size
    assert data['sha256'] == hashlib.sha256(payload.read_bytes()).hexdigest()
    signature = bytes.fromhex(Path(str(output) + '.sig').read_text().strip())
    assert len(signature) == 64
    public = folder / 'public.pem'
    subprocess.run(['openssl', 'pkey', '-in', str(key), '-pubout', '-out', str(public)], check=True, capture_output=True)
    binary_signature = folder / 'signature.bin'
    binary_signature.write_bytes(signature)
    subprocess.run(['openssl', 'pkeyutl', '-verify', '-rawin', '-pubin', '-inkey', str(public),
                    '-in', str(output), '-sigfile', str(binary_signature)], check=True, capture_output=True)
    saved = output.read_bytes()
    for owner in ('revive360', 'PedroHenrique0713'):
        for origin, path in (('github.com', 'releases/download/v0.3.2/tube360.xex'),
                             ('raw.githubusercontent.com', 'rules/tube360.xex')):
            changed = args.copy()
            changed[changed.index('--url') + 1] = f'https://{origin}/{owner}/tube360/{path}'
            call(*changed)
            assert json.loads(output.read_bytes())['url'] == changed[changed.index('--url') + 1]
    call(*args)
    for flag, value in [('--build', '0'), ('--version', 'has spaces'), ('--notes-en', 'line\nbreak'),
                        ('--url', 'http://127.0.0.1:8266/good.xex'),
                        ('--url', 'https://github.com/attacker/tube360/releases/app.xex'),
                        ('--url', 'https://github.com/revive360/tube360-evil/releases/app.xex'),
                        ('--url', 'https://github.com.evil.example/revive360/tube360/releases/x'),
                        ('--url', 'https://raw.githubusercontent.com/revive360/tube360/%2e%2e/x'),
                        ('--url', 'https://github.com:443/revive360/tube360/releases/x'),
                        ('--url', 'https://github.com/revive360/tube360/releases/x#fragment'),
                        ('--url', 'https://github.com@evil.example/PedroHenrique0713/tube360/releases/x')]:
        changed = args.copy()
        changed[changed.index(flag) + 1] = value
        call(*changed, success=False)
        assert output.read_bytes() == saved, 'invalid input cannot replace published manifest'
    changed = args.copy()
    changed[changed.index('--url') + 1] = 'http://127.0.0.1:8266/good.xex'
    call(*changed, '--allow-test-url')
    call('sign', output, success=False)  # No implicit access to a maintainer key.
    print('PASS publisher: exact size/hash, real Ed25519 verification, both owners/hosts, no key overwrite, adversarial URL/schema rejects, explicit test HTTP and key')
