#!/usr/bin/env python3
"""Confere os bytes realmente distribuídos e a igualdade de ZIP/7z/payload solto."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('zip', type=Path)
p.add_argument('--test', action='store_true')
a = p.parse_args()
with zipfile.ZipFile(a.zip) as archive:
    files = {name.split('/', 1)[1]: archive.read(name) for name in archive.namelist()}
manifest = json.loads(files['manifest.json'])
assert manifest['layout'] == 'launcher+app' and manifest['test_only'] == a.test
assert set(files) == set(manifest['files']) | {'manifest.json'}
for name, expected in manifest['files'].items():
    assert hashlib.sha256(files[name]).hexdigest() == expected, name
    assert not name.endswith(('.pem', '.key')) and 'private/' not in name
assert files['default.xex'].startswith(b'XEX2') and files['tube360.xex'].startswith(b'XEX2')
assert files['default.xex'] != files['tube360.xex']
assert files['tube360.xex'] == (a.zip.parent / 'tube360.xex').read_bytes()
assert manifest['packaged_xex_sha256'] == hashlib.sha256(files['tube360.xex']).hexdigest()
assert manifest['packaged_launcher_sha256'] == hashlib.sha256(files['default.xex']).hexdigest()
assert ('TEST-ONLY.txt' in files) == a.test
assert (b'TUBE360-DISPOSABLE-UPDATE-TEST' in files['tube360.xex']) == a.test
for weight in (300, 400, 600, 700):
    assert f'inter-{weight}.ttf' in files
seven = a.zip.with_suffix('.7z')
with tempfile.TemporaryDirectory(prefix='tube360-archive-proof-') as temporary:
    subprocess.run([shutil.which('7z') or shutil.which('7zz'), 'x', '-y', '-o' + temporary, str(seven.resolve())],
                   check=True, capture_output=True)
    top = next(Path(temporary).iterdir())
    extracted = {str(f.relative_to(top)): f.read_bytes() for f in top.rglob('*') if f.is_file()}
    assert files == extracted, 'ZIP and 7z must have identical file contents'
print('PASS package:', a.zip, len(files), 'members; ZIP = 7z; launcher/app distinct; all file hashes and detached payload match')
