#!/usr/bin/env python3
"""Validate immutable build input and derive safe release/proof metadata."""
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
VERSION = r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?'

def image_reference(text):
    value = text.strip()
    if not re.fullmatch(r'ghcr\.io/revive360/devkit@sha256:[0-9a-f]{64}', value):
        raise ValueError('Record the verified public devkit digest in .github/devkit-image.txt first')
    return value


def release_metadata(ref, run_id):
    if not re.fullmatch(r'[1-9][0-9]*', run_id):
        raise ValueError('Invalid Actions run ID')
    if ref.startswith('refs/tags/'):
        tag = ref.removeprefix('refs/tags/')
        if not re.fullmatch('v' + VERSION, tag):
            raise ValueError('Release tag must be v<semver>')
        return {'version': tag[1:], 'tag': tag, 'draft': 'true'}
    return {'version': '0.3.3-ci.' + run_id, 'tag': '', 'draft': 'false'}

if __name__ == '__main__':
    values = {'image': image_reference((ROOT / '.github/devkit-image.txt').read_text())}
    values.update(release_metadata(os.environ['GITHUB_REF'], os.environ['GITHUB_RUN_ID']))
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        for name, value in values.items():
            output.write(f'{name}={value}\n')
