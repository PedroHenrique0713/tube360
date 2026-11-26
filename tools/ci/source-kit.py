#!/usr/bin/env python3
"""Collect corresponding sources and replayable link inputs from this CI build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def collect_link(command, build, kit):
    args = shlex.split(command)
    result = [Path(args[0]).name]
    output = False
    for arg in args[1:]:
        if output:
            result.append(Path(arg).name + '.elf')
            output = False
            continue
        if arg == '-o':
            output = True
        path = (build / arg).resolve()
        if not arg.startswith('-') and path.is_file():
            folder = 'ffmpeg-lib' if path.name in ('libavformat.a', 'libavcodec.a', 'libswresample.a', 'libavutil.a') else 'inputs'
            name = path.name if folder == 'ffmpeg-lib' else hashlib.sha256(str(path).encode()).hexdigest()[:16] + '-' + path.name
            relative = Path(folder) / name
            (kit / folder).mkdir(exist_ok=True)
            shutil.copy2(path, kit / relative)
            result.append(relative.as_posix())
        else:
            if arg.startswith('/'):
                raise ValueError('Uncollected absolute linker input: ' + arg)
            result.append(arg)
    if output:
        raise ValueError('Link command is missing its output')
    return result


def source_filter(info):
    if any(part in ('.git', 'build', 'build-xsdk', '__pycache__') for part in Path(info.name).parts):
        return None
    return info


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'archive', '--format=tar.gz', '-o', str(a.output.resolve() / 'tube360-source.tar.gz'), 'HEAD'], cwd=ROOT, check=True)
    build = ROOT / 'client/build-x360'
    with tempfile.TemporaryDirectory() as folder:
        kit = Path(folder)
        commands = {}
        for target in ('tube360', 'tube360-launcher'):
            commands[target] = collect_link((build / f'CMakeFiles/{target}.dir/link.txt').read_text(), build, kit)
        (kit / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        shutil.copy2(ROOT / 'tools/ci/relink.py', kit)
        (kit / 'README.txt').write_text('Use the exact attested devkit image recorded in CI-BUILD.json.\nXSDK_TOOLCHAIN=<prefix> python3 relink.py\nReplace ffmpeg-lib/*.a to relink with your modified FFmpeg.\nAll inputs include the launcher platform object and boot libraries from the real link commands.\n')
        subprocess.run(['python3', str(kit / 'relink.py')], cwd=kit, check=True)
        for target in commands:
            if (kit / (target + '.elf')).read_bytes() != (build / target).read_bytes():
                raise ValueError('Relinked ELF differs from the build: ' + target)
        with tarfile.open(a.output / 'tube360-relink.tar.gz', 'w:gz') as archive:
            archive.add(kit, arcname='relink')
    with tarfile.open(a.output / 'tube360-dependencies-source.tar.gz', 'w:gz') as archive:
        for name in ('xsdk', 'vulkan4xenon'):
            archive.add(ROOT / '.work/native-display' / name, arcname=name, filter=source_filter)
        sdl = Path(os.environ['SDL3_XENON_DIR']).parent
        archive.add(sdl, arcname='SDL-xenon', filter=source_filter)
        for pattern in ('ffmpeg-*.tar.xz', 'mbedtls-*.tar.bz2', 'curl-xenon.tar.gz'):
            matches = list((ROOT / '.work/src').glob(pattern))
            if not matches:
                if pattern.startswith('ffmpeg'):
                    matches = list(Path('/opt/devkit/.deps/tube360/.work/src').glob(pattern))
                if not matches:
                    raise ValueError('Missing corresponding source: ' + pattern)
            for source in matches:
                archive.add(source, arcname=source.name)


if __name__ == '__main__':
    main()
