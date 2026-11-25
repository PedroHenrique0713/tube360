#!/usr/bin/env python3
"""Build a fresh disposable test kit. Does not open network/console/private config."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
cli = argparse.ArgumentParser()
cli.add_argument('--output', type=Path, default=ROOT/'bench/1080p-app/.work/console')
cli.add_argument('--build-dir', type=Path, default=ROOT/'client/build-x360')
cli.add_argument('--artifact-report', type=Path, default=ROOT/'bench/1080p-app/results/artifacts.json')
args = cli.parse_args()
stage = args.output.resolve()
if not stage.is_relative_to(ROOT):
    cli.error('stage must be inside this worktree')
if stage.exists() and any(stage.iterdir()):
    cli.error('choose a NEW empty stage; do not overwrite a Nest STOP journal')
cache = (args.build_dir/'CMakeCache.txt').read_text()
if 'TUBE360_F40_BENCH:BOOL=ON' not in cache:
    cli.error('Xbox build must enable F40 probe')
files = {}

def copy(source, relative):
    target = stage/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    files[relative] = hashlib.sha256(target.read_bytes()).hexdigest()


def write(relative, text):
    target = stage/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)
    files[relative] = hashlib.sha256(target.read_bytes()).hexdigest()


for case, video, start, nest, full, output in (
    ('tv137', '4p_9OPKgcDE', 31, False, False, 720),
    ('tv137-1080', '4p_9OPKgcDE', 31, False, False, 1080),
    ('nest137', '4p_9OPKgcDE', 31, True, False, 720),
    ('motion137', '83OlZMzqQJc', 480, False, False, 720),
    ('full137', '4p_9OPKgcDE', 31, False, True, 720),
):
    copy(args.build_dir/'tube360.xex', f'{case}/tube360.xex')
    fonts = sorted((ROOT/'client/assets').glob('inter-*.ttf'))
    if len(fonts) != 4:
        cli.error('exactly four fonts required')
    for font in fonts:
        copy(font, f'{case}/{font.name}')
    write(f'{case}/autoplay.txt', f'{video} {start}\n')
    write(f'{case}/settings.txt', f'lang=pt\nserver=off\nquality=720\nvideo_mode=1080p30\noutput_height={output}\nsound={"nest" if nest else "tv"}\nh264_loop_filter={"full" if full else "auto"}\n')
    write(f'{case}/f40-playback.txt', f'120 10 137 {int(nest)} {0 if full else 48} {video}\n')
    write(f'{case}/video-metrics.txt', '0\n')
    if not nest:
        write(f'{case}/mudo.txt', 'Remove before the audible TV acceptance run.\n')
    if nest:
        write(f'{case}/nest.example.txt', 'REPLACE_WITH_SPEAKER_IP 8009\nF40 bench speaker\n')
for name in ('LICENSE', 'THIRD_PARTY.md'):
    copy(ROOT/name, name)
copy(ROOT/'bench/1080p-app/README.md', 'README.md')
manifest = dict(stage=str(stage), files=files, executed_on_console=False, console_gate='pending_console',
                source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                ffmpeg_recipe=(ROOT/'.work/ffmpeg-xbox/.tube360-source-id').read_text().strip())
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
args.artifact_report.write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Prepared {len(files)} files in {stage}; no console access')
