#!/usr/bin/env python3
"""PC real, resposta pública gravada, mídia sintética e HOME/assets isolados."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.work/sponsorblock-e2e'
MEDIA = WORK / 'media'
VIDEO = 'wWC8wVI4E3E'


def prepare():
    MEDIA.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'bench/sponsorblock/fixtures/public-prefix.json', MEDIA / 'public-prefix.json')
    for name, source in [('watch.html', '<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>'),
                         ('base.js', 'var a={signatureTimestamp:20775};')]:
        (MEDIA / name).write_text(source)
    if not (MEDIA / 'video.mp4').exists() or (MEDIA / 'video.mp4').stat().st_size > 16*1024*1024:
        subprocess.run(['ffmpeg', '-y', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc=size=1280x720:rate=30,format=yuv420p',
                        '-t', '40', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '26', '-threads', '2',
                        '-g', '30', '-an', '-movflags', '+faststart', str(MEDIA / 'video.mp4')], check=True)
    if not (MEDIA / 'audio.m4a').exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100',
                        '-t', '40', '-c:a', 'aac', '-movflags', '+faststart', str(MEDIA / 'audio.m4a')], check=True)
    (MEDIA / 'player.json').write_text(json.dumps(dict(playabilityStatus=dict(status='OK'),
        videoDetails=dict(videoId=VIDEO, title='SponsorBlock · Demo local / synthetic media', lengthSeconds='667'),
        streamingData=dict(expiresInSeconds='3600', adaptiveFormats=[
            dict(itag=136, height=720, width=1280, fps=30, mimeType='video/mp4; codecs="avc1.64001f"', url='https://fixture.googlevideo.com/video'),
            dict(itag=140, bitrate=128000, mimeType='audio/mp4; codecs="mp4a.40.2"', url='https://fixture.googlevideo.com/audio')]))))


def run(name, mode='button', lang='pt', screen='player', keys='', seconds=3, at=8, large=False,
        height=720, key_ms=100, speaker=False, **extra):
    assets = WORK / name
    assets.mkdir(exist_ok=True)
    for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets / font.name)
    (assets / 'settings.txt').write_text(f'lang={lang}\nserver=off\nsound={"nest" if speaker else "tv"}\n'
        f'output_height={height}\nlarge_text={"on" if large else "off"}\nsponsorblock={mode}\n')
    if screen == 'player':
        (assets / 'autoplay.txt').write_text(f'{VIDEO} {at}\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(assets), XDG_CACHE_HOME=str(assets / '.cache'), SDL_VIDEO_DRIVER='offscreen',
        SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
        XT_SPONSOR_FIXTURES=str(MEDIA), XT_SHOT=str(WORK / (name + '.ppm')), XT_SHOT_FRAME='2147483647',
        XT_SHOT_MS=str(int(seconds * 1000)), XT_KEYS_MS=str(key_ms), XT_KEYS=keys)
    if screen == 'settings':
        env['XT_SCREEN'] = 'settings'
    env.update(extra)
    nest = None
    if speaker:
        sys.path.insert(0, str(ROOT / 'bench/standalone'))
        from cast_test import Receiver
        cert, key = WORK / 'cert.pem', WORK / 'key.pem'
        if not cert.exists():
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=fixture',
                '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        nest = Receiver(cert, key)
        (assets / 'nest.txt').write_text(f'127.0.0.1 {nest.port}\nNest local F34\n')
    started = time.monotonic()
    try:
        with (WORK / (name + '.log')).open('w') as log:
            subprocess.run([str(ROOT / 'client/build/tube360-sponsorblock-fixture')], cwd=ROOT, env=env,
                stdout=log, stderr=log, timeout=seconds+30, check=True)
    finally:
        if nest:
            nest.close()
            (WORK / (name + '-cast.json')).write_text(json.dumps(nest.events, indent=2))
    text = (WORK / (name + '.log')).read_text()
    assert 'BAD SponsorBlock' not in text, text
    assert '(desligado: só console)' in text, text
    assert 'captura: ' in text, text
    if screen == 'player':
        assert 'primeiro quadro' in text, text
        assert text.count('SponsorBlock fixture GET') == (0 if mode == 'off' else 1), text
        if mode != 'off':
            status = 'http_error' if 'XT_SPONSOR_FAIL' in extra else 'tls_error' if 'XT_SPONSOR_TLS_FAIL' in extra else 'parse_error' if 'XT_SPONSOR_PARSE_FAIL' in extra else 'ok'
            assert f'status={status}' in text, text
    if nest:
        assert not nest.errors and nest.commands('LOAD') and nest.commands('STOP'), nest.errors
        # O seek do player pode reutilizar o LOAD ou enviar SEEK; ambos usam o Cast v2 real em loopback.
        jumps = nest.commands('LOAD') + nest.commands('SEEK')
        assert any(float(e.get('currentTime', 0)) >= 20.8 for e in jumps), jumps
    subprocess.run(['magick', str(WORK / (name + '.ppm')), str(WORK / (name + '.png'))], check=True)
    with (WORK / (name + '.ppm')).open('rb') as f:
        f.readline()
        assert f.readline().strip() == (b'640 480' if height == 480 else b'1280 720')
    print('PASS', name, f'{time.monotonic()-started:.2f}s', flush=True)
    return text


if __name__ == '__main__':
    prepare()
    cases = {}
    for lang in ('pt', 'en', 'es'):
        for mode in ('off', 'button', 'auto'):
            cases[f'settings-{lang}-{mode}'] = dict(screen='settings', lang=lang, mode=mode, keys='D'*15+'A', seconds=2.5, key_ms=70)
        cases[f'button-{lang}'] = dict(lang=lang, seconds=2)
        cases[f'auto-{lang}'] = dict(lang=lang, mode='auto', seconds=3)
    cases.update({
        'settings-sd': dict(screen='settings', lang='es', height=480, keys='D'*15+'A', seconds=2.5, key_ms=70),
        'settings-large': dict(screen='settings', lang='pt', large=True, keys='D'*15+'A', seconds=2.5, key_ms=70),
        'settings-switch': dict(screen='settings', keys='D'*15+'ADR', seconds=3, key_ms=70),
        'settings-back': dict(screen='settings', keys='D'*15+'AB', seconds=3, key_ms=70),
        'button-expired': dict(at=19.5, seconds=3),
        'button-skip': dict(keys='.........Y', seconds=4),
        'button-paused': dict(keys='....A....Y', seconds=3.5),
        'off': dict(mode='off'),
        'auto-undo': dict(mode='auto', keys='..............Y', seconds=4),
        'auto-undo-quality': dict(mode='auto', keys='..............Y..........Y', seconds=4),
        'auto-expired': dict(mode='auto', seconds=9),
        'auto-sd': dict(mode='auto', height=480, seconds=3),
        'button-large': dict(large=True, seconds=2),
        'offline': dict(XT_SPONSOR_FAIL='1'),
        'tls-failure': dict(XT_SPONSOR_TLS_FAIL='1'),
        'parse-failure': dict(XT_SPONSOR_PARSE_FAIL='1'),
        'slow-api': dict(XT_SPONSOR_DELAY_MS='1000', seconds=3),
        'nest-auto': dict(mode='auto', speaker=True, seconds=6),
    })
    wanted = set(sys.argv[1:])
    assert not wanted - cases.keys(), f'unknown cases: {wanted-cases.keys()}'
    results = []
    for name, options in cases.items():
        if wanted and name not in wanted:
            continue
        text = run(name, **options)
        skips = re.findall(r'SponsorBlock: skip ([\d.]+) -> ([\d.]+)', text)
        if name.startswith('auto-') or name == 'nest-auto':
            assert len(skips) == 1 and float(skips[0][1]) == 20.877, text
        if name == 'button-skip':
            assert len(skips) == 1 and float(skips[0][1]) == 20.877, text
        if name in ('off', 'offline', 'button-expired') or name.startswith('button-') and name not in ('button-skip','button-paused'):
            assert not skips, text
        if name == 'auto-undo':
            assert 'SponsorBlock: undo' in text and '-> 7.775' in text, text
            assert float(re.search(r'captura: .*posição ([\d.]+)', text)[1]) < 15, text
        if name == 'auto-undo-quality':
            assert 'SponsorBlock: undo' in text and 'video_mode=720p60' in (WORK/name/'settings.txt').read_text(), text
        if name == 'button-paused':
            assert len(skips)==1 and 'player: pause=1' in text, text
            assert abs(float(re.search(r'captura: .*posição ([\d.]+)',text)[1])-20.877)<.05, text
        if name == 'settings-switch':
            assert 'sponsorblock=off' in (WORK/name/'settings.txt').read_text()
        if name == 'slow-api':
            first = int(re.search(r'primeiro quadro: (\d+) ms', text)[1])
            assert first < 1000, f'first frame waited for slow API: {first}ms'
        results.append(dict(name=name, skips=skips))
    (WORK/'results.json').write_text(json.dumps(results, indent=2))
