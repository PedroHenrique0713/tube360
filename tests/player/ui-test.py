#!/usr/bin/env python3
"""F63: player com fileira de relacionados, volume do app e player de Shorts, no app real (offscreen, rede fechada).
uso: ui-test.py <raiz da árvore com client/build pronto> [pasta de saída]"""
import copy, json, os, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(sys.argv[1]).resolve()
OUT = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(__import__('tempfile').mkdtemp(prefix='tube360-f63-'))
WANTED = set()
OUT.mkdir(parents=True, exist_ok=True)
A = OUT / '_A'   # bench/search (feeds publicos)
B = OUT / '_B'   # bench/search-account (musica, player com midia sintetica)
if not (A / 'tube360-search-replay').exists():
    subprocess.run([sys.executable, str(ROOT / 'bench/search/prepare.py'), str(A)], check=True, stdout=subprocess.DEVNULL)
if not (B / 'tube360-replay').exists():
    subprocess.run([sys.executable, str(ROOT / 'bench/search-account/prepare.py'), str(B)], check=True, stdout=subprocess.DEVNULL)

# video estatico (barras): qualquer quadro sai igual, so o relogio e a barra de progresso dependem do instante
for name,size,rate,dur in (('wide.mp4','1920x800',24,8),('480.mp4','854x480',30,24)):
    marker=B/'replay'/(name+'.static')
    if not marker.exists():
        subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'smptebars=size={size}:rate={rate}:duration={dur}','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-g',str(rate),'-pix_fmt','yuv420p','-movflags','+faststart',str(B/'replay'/name)],check=True)
        marker.write_text('x')

mixed = json.loads((ROOT / 'tests/f52/fixtures/feeds.json').read_text())
base_routes = json.loads((A / 'replay/responses.json').read_text())


def shot(name, binary, assets, env_extra, ms, keys, settings, timeout=60):
    assets.mkdir(parents=True, exist_ok=True)
    for font in (ROOT / 'client/assets').glob('*.ttf'):
        shutil.copyfile(font, assets / font.name)
    (assets / 'settings.txt').write_text(settings)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XT_', 'F47_'))}
    env.update(HOME=str(assets / 'home'), XDG_CONFIG_HOME=str(assets / 'config'), XDG_CACHE_HOME=str(assets / 'cache'),
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_CAST_DISABLE='1',
               XT_ASSETS=str(assets), XT_KEYS=keys, XT_KEYS_MS='180',
               XT_SHOT=str(OUT / (name + '.ppm')), XT_SHOT_MS=str(ms), XT_SHOT_FRAME='2147483647')
    env.update(env_extra)
    with (OUT / (name + '.log')).open('w') as log:
        subprocess.run([str(binary)], env=env, cwd=assets, stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=True)
    assert (OUT / (name + '.ppm')).exists(), name
    print('ok', name, flush=True)


def case_a(name, signed=False, nav=None, nav_id=None, nav_q=None, keys='', ms=3000, screen=None, text=None,
           settings='lang=en\nsound=tv\ncols=4\nshorts=row\n', payload=None):
    if WANTED and name not in WANTED:
        return
    assets = OUT / '_work' / name
    replay = A / 'replay'
    payload = copy.deepcopy(payload or mixed)
    manifest = copy.deepcopy(base_routes)
    (replay / 'public-feed.json').write_text(json.dumps(payload))
    manifest['requests'].insert(0, dict(url='https://www.youtube.com/youtubei/v1/browse?prettyPrint=false',
                                        file='public-feed.json', fields={'browseId': 'FEwhat_to_watch'}))

    def ids(n):
        if isinstance(n, dict):
            for k, v in n.items():
                if k == 'videoId':
                    yield v
                yield from ids(v)
        elif isinstance(n, list):
            for v in n:
                yield from ids(v)
    for vid in set(ids(payload)):
        manifest['requests'].append(dict(url=f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg', file='art.png'))
    (replay / 'responses.json').write_text(json.dumps(manifest))
    accounts = assets / 'accounts'
    accounts.mkdir(parents=True, exist_ok=True)
    channel = {'contents': [{'channelRenderer': {'channelId': 'UC0000000000000000000001', 'title': {'simpleText': 'Fixture channel'}}}]}
    rules = [dict(url='/account/accounts_list?', contains='', json={'contents': [
        {'accountItemRenderer': {'accountName': {'simpleText': 'F52 fixture'}, 'isSelected': True}}]})]
    for bid in ('FEwhat_to_watch', 'FEsubscriptions', 'FEhistory', 'VLWL', 'FEplaylist_aggregation'):
        rules.append(dict(url='/youtubei/v1/browse?', contains=bid, json=payload, authorization='F52_ACCESS'))
    rules.append(dict(url='/youtubei/v1/browse?', contains='FEchannels', json=channel, authorization='F52_ACCESS'))
    (accounts / 'responses.json').write_text(json.dumps(rules))
    assets.mkdir(parents=True, exist_ok=True)
    token = assets / 'yt-console-account.json'
    if signed:
        token.write_text(json.dumps(dict(access_token='F52_ACCESS', refresh_token='F52_REFRESH', expires_at=4070908800,
                                         client=dict(client_id='fixture-client', client_secret='fixture-secret'), name='F52 fixture')))
    else:
        token.unlink(missing_ok=True)
    extra = dict(XT_SEARCH_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(accounts))
    if nav:
        extra['XT_NAV'] = nav
        if nav_id: extra['XT_NAV_ID'] = nav_id
        if nav_q: extra['XT_NAV_QUERY'] = nav_q
    if screen: extra['XT_SCREEN'] = screen
    if text: extra['XT_SEARCH_TEXT'] = text
    shot(name, A / 'tube360-search-replay', assets, extra, ms, keys, settings)
    (replay / 'responses.json').write_text(json.dumps(base_routes))


def case_b(name, signed=True, keys='', ms=3000, settings='lang=pt\ncols=4\nsound=tv\nvideo_mode=auto\n', **extra):
    if WANTED and name not in WANTED:
        return
    assets = OUT / '_work' / name
    assets.mkdir(parents=True, exist_ok=True)
    for f in ('local-library.json', 'yt-console-account.json'):
        (assets / f).unlink(missing_ok=True)
    if signed:
        (assets / 'yt-console-account.json').write_text(json.dumps({'access_token': 'console-fixture-token', 'refresh_token': 'fixture-refresh',
            'expires_at': 4070908800, 'client': {'client_id': 'fixture-client', 'client_secret': 'fixture-secret'}, 'name': 'Demo'}))
    env = dict(F47_REPLAY=str(B / 'replay'), XT_ACCOUNT_FIXTURES=str(B / 'accounts'), F47_AUDIT=str(assets / 'audit.txt'))
    env.update(extra)
    shot(name, B / 'tube360-replay', assets, env, ms, keys, settings)



def ids(n):
    if isinstance(n, dict):
        for k, v in n.items():
            if k == 'videoId': yield v
            yield from ids(v)
    elif isinstance(n, list):
        for v in n: yield from ids(v)

def prep_next():
    """next com a lista de relacionados (o mesmo payload de feeds fixos) e miniaturas."""
    rp = B / 'replay'
    m = json.loads((rp / 'responses.json').read_text())
    if any(r.get('file') == 'f63-next.json' for r in m['requests']): return
    (rp / 'f63-next.json').write_text(json.dumps(mixed))
    m['requests'].insert(0, dict(url='https://www.youtube.com/youtubei/v1/next?prettyPrint=false', file='f63-next.json'))
    for vid in set(ids(mixed)):
        m['requests'].append(dict(url=f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg', file='art.png'))
    (rp / 'responses.json').write_text(json.dumps(m))
prep_next()

def prep_account_next():
    p = B / 'accounts' / 'responses.json'
    rules = json.loads(p.read_text())
    for r in rules:
        if r.get('url') == '/youtubei/v1/next?' and 'contents' not in r['json']:
            r['json']['contents'] = mixed.get('contents', mixed)
    p.write_text(json.dumps(rules))
prep_account_next()


pedidos = lambda n: [l.split('player: pedido ')[1].split(' ')[0] for l in log_of(n).splitlines() if 'player: pedido' in l]
def shot_of(name): return (OUT / (name + '.ppm')).read_bytes()
def log_of(name): return (OUT / (name + '.log')).read_text(errors='replace')
KEEP = dict(signed=True, XT_PLAY='R6MlUcmOul8')
case_b('f63-base', keys='....', ms=3800, **KEEP)
case_b('f63-icons', keys='....D', ms=3800, **KEEP)
case_b('f63-shelf', keys='....DD', ms=3800, **KEEP)
case_b('f63-shelf-right', keys='....DDRRR', ms=4000, **KEEP)
case_b('f63-volume-down', keys='.' * 40 + 'DD', ms=8800, **KEEP)
case_b('f63-volume-up-at-max', keys='.' * 40 + 'U', ms=8800, **KEEP)
case_b('f63-short', keys='....', ms=3800, XT_PLAY_SHORT='1', **KEEP)
case_b('f63-short-actions', keys='....RDD', ms=3800, XT_PLAY_SHORT='1', **KEEP)
case_b('f63-short-b-twice', keys='....RB', ms=3800, XT_PLAY_SHORT='1', **KEEP)
case_b('f63-next', keys='....]', ms=6000, **KEEP)
case_b('f63-next-prev', keys='....][', ms=9000, **KEEP)
case_b('f63-later', keys='....X', ms=4500, signed=False, XT_PLAY='R6MlUcmOul8')
case_b('f63-menu', keys='....S', ms=3800, **KEEP)

# Sem conta, fila local (playlist) e o volume no Nest: casos que o caminho com conta não cobre.
case_b('f63-unsigned-shelf', keys='....DD', ms=3800, signed=False, XT_PLAY='R6MlUcmOul8')
case_b('f63-unsigned-rb', keys='....]', ms=6000, signed=False, XT_PLAY='R6MlUcmOul8')
Q = dict(signed=True, XT_NAV='playlist', XT_NAV_ID='PLfixture')
case_b('f63-q-open', keys='......DA', ms=5000, **Q)
case_b('f63-q-rb', keys='......DA..........]', ms=9000, **Q)
case_b('f63-q-rb-lb', keys='......DA..........]......[', ms=11000, **Q)

def nest_volume(name, keys, volume_line='', ms=9000, sound='nest', external=None):
    # Receptor falso começando em 0,42, com o nest.txt salvo; devolve (volume final da caixa, pedidos SET_VOLUME).
    sys.path.insert(0, str(ROOT / 'bench/standalone'))
    from cast_test import Receiver
    certs = OUT / '_cert'; certs.mkdir(exist_ok=True)
    if not (certs / 'cert.pem').exists():
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=cast-fixture',
                        '-keyout', str(certs / 'key.pem'), '-out', str(certs / 'cert.pem')], check=True, stderr=subprocess.DEVNULL)
    rx = Receiver(certs / 'cert.pem', certs / 'key.pem')
    rx.consume = lambda url: None
    assets = OUT / '_work' / name; assets.mkdir(parents=True, exist_ok=True)
    (assets / 'nest.txt').write_text(f'127.0.0.1 {rx.port}\nNest de bancada\n')
    (assets / 'nest-owner.json').unlink(missing_ok=True)
    timer = None
    if external is not None:  # a caixa muda por fora 3 s depois de abrir (o app lê no próximo status, a cada 2 s)
        timer = __import__('threading').Timer(3, lambda: setattr(rx, 'volume', external)); timer.start()
    try:
        case_b(name, signed=True, keys=keys, ms=ms, XT_PLAY='R6MlUcmOul8',
               settings=f'lang=pt\ncols=4\nsound={sound}\nvideo_mode=auto\n{volume_line}')
        sets = [e for e in rx.events if 'SET_VOLUME' in json.dumps(e)]
        return rx.volume, sets
    finally:
        rx.close()

# A fileira abre com ↓ na linha de ícones e só então: a tela muda, e muda de novo ao andar para a direita.
assert shot_of('f63-icons') != shot_of('f63-base'), 'icons row did not open'
assert shot_of('f63-shelf') != shot_of('f63-icons'), 'shelf did not open'
assert shot_of('f63-shelf-right') != shot_of('f63-shelf'), 'shelf selection did not move'
# Volume com os controles escondidos: dois passos para baixo gravam 90, e o máximo não passa de 100.
assert 'player: volume 95' in log_of('f63-volume-down') and 'player: volume 90' in log_of('f63-volume-down'), log_of('f63-volume-down')[-600:]
assert 'player: volume 100' in log_of('f63-volume-up-at-max')
assert (OUT / '_work/f63-volume-down/settings.txt').read_text().count('volume=90') == 1
# Shorts: tela própria (vertical, ações ao lado); focar a ação muda a tela e B volta o foco ao vídeo sem sair.
assert shot_of('f63-short') != shot_of('f63-base'), 'Short opened in the regular player'
assert shot_of('f63-short-actions') != shot_of('f63-short')
log = log_of('f63-short-b-twice')
assert log.index('shorts: foco 1') < log.index('shorts: foco 0'), 'B from the action column should only return focus to the video'
assert 'shorts: foco' not in log_of('f63-short'), 'focus moved without a key'
# Mapa único: RB = seguinte (o "a seguir"), LB volta ao anterior; X salva para depois; Menu abre a linha de opções.
assert len(pedidos('f63-next')) == 2 and 'R6MlUcmOul8' in pedidos('f63-next')[0], pedidos('f63-next')
nv = pedidos('f63-next-prev')
assert len(nv) == 3 and 'R6MlUcmOul8' in nv[2] and nv[1] != nv[2], nv
assert 'local: save' in log_of('f63-later'), log_of('f63-later')[-400:]
assert shot_of('f63-menu') != shot_of('f63-base') and 'player: pause' not in log_of('f63-menu'), 'Menu should open the options row without pausing'
# Sem conta: a fileira e o RB continuam funcionando pelo caminho do console.
assert len(pedidos('f63-unsigned-rb')) >= 1
# Fila local: RB anda para o item seguinte da fila e LB volta ao anterior (nunca pulando).
qa, qb, qc = pedidos('f63-q-open'), pedidos('f63-q-rb'), pedidos('f63-q-rb-lb')
assert len(qb) == len(qa) + 1 and qb[-1] != qa[-1], (qa, qb)
assert len(qc) == len(qb) + 1 and qc[-1] == qa[-1], (qa, qb, qc)
# Volume: o salvo é só da TV; no Nest a tecla mexe na caixa a partir do nível real (0,42 -> 0,45) e a abertura nunca manda SET_VOLUME.
vol, sets = nest_volume('f63-n-open', '', volume_line='volume=95\n')
assert not sets and abs(vol - .42) < .001, (vol, sets)
vol, sets = nest_volume('f63-n-up', '.' * 30 + 'U', ms=8000)
assert len(sets) == 1 and abs(vol - .45) < .001, (vol, sets)
assert 'volume=' not in (OUT / '_work/f63-n-up/settings.txt').read_text() or 'volume=100' in (OUT / '_work/f63-n-up/settings.txt').read_text()
# O volume da caixa pode mudar por fora (Google Home, voz, botões): o passo parte do que ela informou (0,80 -> 0,85), não do último pedido.
vol, sets = nest_volume('f63-n-ext', '.' * 40 + 'U', ms=11000, external=.8)
assert len(sets) == 1 and abs(vol - .85) < .001, (vol, sets)
vol, sets = nest_volume('f63-n-tv', '.' * 30 + 'D', ms=8000, sound='tv')
assert not sets and 'volume=95' in (OUT / '_work/f63-n-tv/settings.txt').read_text()
print('PASS f63 ui: shelf, volume and Shorts')
