#!/usr/bin/env python3
"""Synthetic 3-hour fragmented MP4s; sparse padding models multi-GiB CDN resources."""
import argparse
from pathlib import Path
import struct
import subprocess


def atoms(data):
    offset = 0
    while offset + 8 <= len(data):
        size, kind = struct.unpack_from('>I4s', data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError('invalid template atom')
        yield kind, data[offset:offset + size]
        offset += size


def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for name, fps, hdr in [('long25', 25, False), ('hdr60', 60, True)]:
        case = directory / name
        case.mkdir(exist_ok=True)
        template = case / 'template.mp4'
        command = ['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                   f'color=c=blue:size=1280x720:rate={fps}', '-t', '2', '-an',
                   '-c:v', 'libx264', '-threads', '2', '-preset', 'ultrafast',
                   '-g', str(2 * fps), '-pix_fmt', 'yuv420p']
        if hdr:
            command += ['-color_primaries', 'bt2020', '-color_trc', 'smpte2084',
                        '-colorspace', 'bt2020nc', '-x264-params',
                        'colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc']
        subprocess.run(command + ['-movflags', '+dash+default_base_moof+skip_trailer',
                                 '-frag_duration', '2000000', str(template)], check=True)
        boxes = dict(atoms(template.read_bytes()))
        moof, mdat = boxes[b'moof'], boxes[b'mdat']
        timescale = struct.unpack_from('>I', boxes[b'sidx'], 16)[0]
        count, stride = 5400, 512 * 1024
        # Version 0 has two 32-bit time/offset fields (32 bytes including header).
        sidx = struct.pack('>I4sIIIIIHH', 32 + 12 * count, b'sidx', 0, 1,
                           timescale, 0, 0, 0, count)
        sidx += struct.pack('>III', stride, 2 * timescale, 0x90000000) * count
        prefix = boxes[b'ftyp'] + boxes[b'moov'] + sidx
        fragment = bytearray(moof + mdat)
        struct.pack_into('>I', fragment, len(moof), stride - len(moof))
        tfdt = fragment.index(b'tfdt') + 8
        mfhd = fragment.index(b'mfhd') + 8
        if fragment[tfdt - 4] != 1:
            raise ValueError('expected 64-bit tfdt in ffmpeg template')
        (case / 'prefix.bin').write_bytes(prefix)
        (case / 'fragment.bin').write_bytes(fragment)
        (case / 'meta.txt').write_text(f'{count} {stride} {timescale} {fps} {tfdt} {mfhd}\n')
        with (case / 'source.mp4').open('wb') as output:
            output.write(prefix)
            for i in range(count):
                struct.pack_into('>Q', fragment, tfdt, i * 2 * timescale)
                struct.pack_into('>I', fragment, mfhd, i + 1)
                output.seek(len(prefix) + i * stride)
                output.write(fragment)
            output.truncate(len(prefix) + count * stride)
        print(f'{name}: duration=10800 fps={fps} HDR_metadata={int(hdr)} '
              f'sidx_entries={count} bytes={len(prefix) + count * stride}', flush=True)
    return directory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    prepare(parser.parse_args().directory)
