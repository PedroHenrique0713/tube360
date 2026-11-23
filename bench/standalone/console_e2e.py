#!/usr/bin/env python3
"""Composição F11/F12/F13 no app desktop; conta e Cast inteiramente locais."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import time
from cast_test import Receiver

root = pathlib.Path(__file__).resolve().parents[2]
proof = pathlib.Path(sys.argv[1]).resolve()
replay = proof / 'replay'
binary = proof / 'tube360-replay'
baseline = json.loads((replay / 'responses.json').read_text())
accounts = proof / 'account-fixtures'
accounts.mkdir(exist_ok=True)
rules = json.loads((root / 'bench/account/fixtures/responses.json').read_text())
(accounts / 'responses.json').write_text(json.dumps(rules))
shutil.copyfile(root / 'bench/account/fixtures/thumb.png', accounts / 'thumb.png')


def manifest(token):
    data = json.loads(json.dumps(baseline))
    for request in data['requests']:
        if request['url'].startswith('https://music.youtube.com/youtubei/'):
            request['authorization'] = token
    # Os vídeos da biblioteca falsa usam a mesma mídia sintética da prova pública.
    for index in range(1, 9):
        video = f'video{index:06}'
        player = json.loads((replay / 'player-F12audio001.json').read_text())
        player['videoDetails']['videoId'] = video
        filename = f'player-{video}.json'
        (replay / filename).write_text(json.dumps(player))
        data['requests'].extend([
            {'url': f'https://www.youtube.com/watch?v={video}&bpctr=9999999999&has_verified=1', 'file': 'watch.html'},
            {'url': 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false', 'fields': {'videoId': video}, 'file': filename},
            {'url': f'https://i.ytimg.com/vi/{video}/mqdefault.jpg', 'file': 'art.png'},
        ])
    (replay / 'responses.json').write_text(json.dumps(data))


def run(name, seconds=2, token=None, expired=False, speaker=None, scenario=None, expected=None, **config):
    manifest(expected if expected is not None else 'fixture-access' if token else '')
    assets = proof / name
    assets.mkdir(exist_ok=True)
    for font in (root / 'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets / font.name)
    (assets / 'settings.txt').write_text('lang=pt\nquality=720\ncols=4\nserver=off\nsound=' + ('nest' if speaker else 'tv') + '\n')
    account = assets / 'yt-console-account.json'
    account.unlink(missing_ok=True)
    if token:
        account.write_text(json.dumps({'access_token': 'expired-fixture' if expired else 'fixture-access',
            'refresh_token': 'fixture-refresh', 'expires_at': int(time.time()) + (-10 if expired else 3600),
            'client': {'client_id': 'fixture-tv-client', 'client_secret': 'fixture-tv-secret'},
            'name': 'Conta de demonstração'}))
    if speaker:
        (assets / 'nest.txt').write_text(f'127.0.0.1 {speaker.port}\nNest de bancada\n')
    audit = proof / f'{name}-account.tsv'
    music_audit = proof / f'{name}-music.tsv'
    audit.unlink(missing_ok=True)
    music_audit.unlink(missing_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('XT_')}
    env.update(XT_CAST_DISABLE='1', SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2',
        XT_ASSETS=str(assets), XT_SERVER='127.0.0.1:8118', XT_ACCOUNT_FIXTURES=str(accounts),
        XT_ACCOUNT_AUDIT=str(audit), XT_CATALOG_REPLAY=str(replay), XT_CATALOG_AUDIT=str(music_audit),
        XT_SHOT=str(proof / f'{name}.ppm'), XT_SHOT_FRAME='2147483647', XT_SHOT_MS=str(int(seconds*1000)),
        XT_KEYS_MS='400')
    env.update(config)
    thread = None
    try:
        if scenario:
            thread = threading.Thread(target=scenario, daemon=True)
            thread.start()
        with (proof / f'{name}.log').open('w') as log:
            subprocess.run([str(binary)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=seconds+25, check=True)
    finally:
        if thread:
            thread.join(5)
        if speaker:
            speaker.close()
            (proof / f'{name}-cast.json').write_text(json.dumps(speaker.events, indent=2))
    subprocess.run(['magick', str(proof / f'{name}.ppm'), str(proof / f'{name}.png')], check=True)
    log = (proof / f'{name}.log').read_text()
    if speaker:
        assert not speaker.errors, speaker.errors
        assert speaker.commands('LOAD') and speaker.commands('STOP'), name
        assert not (assets / 'nest-owner.json').exists(), f'{name}: STOP not confirmed'
    print(name, flush=True)
    return log, audit.read_text() if audit.exists() else '', music_audit.read_text() if music_audit.exists() else ''


try:
    run('account-code', .9, XT_SCREEN='settings', XT_KEYS='A')
    # Depois de entrar, volta ao menu e abre a Música com o token recém-obtido.
    log, account, music = run('account-login-music', 8.8, expected='fixture-access', XT_SCREEN='settings', XT_KEYS='A...............BUUA')
    assert '/o/oauth2/token' in account and 'authorized_fixture' in music
    log, account, _ = run('account-home', 5, token=True)
    assert '/account/accounts_list' in account and '/youtubei/v1/browse' in account
    log, account, _ = run('account-library', token=True, XT_NAV='library')
    assert '/youtubei/v1/browse' in account
    log, account, music = run('account-refresh-music', token=True, expired=True, XT_NAV='music', XT_MUSIC='1')
    assert '/o/oauth2/token' in account and 'authorized_fixture' in music
    # Sai em Configurações, volta ao menu e abre Music como visitante.
    log, account, music = run('account-logout-music', 3.5, token=True, expected='', XT_SCREEN='settings', XT_KEYS='AABUUA')
    assert '/o/oauth2/revoke' in account
    assert 'authorized_fixture' in music and 'rejected_fixture' not in music
    assert not (proof / 'account-logout-music/yt-console-account.json').exists()
    log, account, _ = run('account-playlist', 9.5, token=True, XT_NAV='playlist', XT_NAV_ID='PLfixture', XT_KEYS='UA')
    assert 'primeiro quadro' in log and '/api/stats/' in account
    assert log.count('primeiro quadro') >= 3 and 'encerrando video000002' in log
    log, account, _ = run('account-music-history', 3, token=True, XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1', XT_KEYS='UA')
    assert 'musica: primeiro audio' in log and '/api/stats/' in account

    cert, key = proof / 'cast-cert.pem', proof / 'cast-key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=console-fixture', '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    receiver = Receiver(cert, key)
    log, _, _ = run('music-nest', 3.6, speaker=receiver, XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1', XT_KEYS='UA.A.A')
    assert 'musica: primeiro audio' in log and 'som: Nest' in log and 'primeiro quadro' not in log
    assert receiver.commands('PAUSE') and receiver.commands('PLAY')
    receiver = Receiver(cert, key)

    def idle():
        time.sleep(2)
        receiver.position = receiver.at()
        receiver.stamp = time.monotonic()
        receiver.state = 'IDLE'

    log, _, _ = run('music-nest-failover', 3.7, speaker=receiver, scenario=idle,
        XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1', XT_KEYS='UA')
    assert 'som: Cast confirmou silêncio' in log and 'som: TV' in log and 'primeiro quadro' not in log
    receiver = Receiver(cert, key)
    log, _, _ = run('music-nest-video-roundtrip', 5.2, speaker=receiver,
        XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1', XT_KEYS='UARRRA..BB')
    assert log.count('musica: primeiro audio') == 2 and 'primeiro quadro' in log
    assert len(receiver.commands('LOAD')) == 1 and len(receiver.commands('SEEK')) == 2
finally:
    (replay / 'responses.json').write_text(json.dumps(baseline))

print('Console account, Music credentials, private queue, history and audio-only Cast verified', flush=True)
