#!/usr/bin/env python3
"""F71 (rodada 3), no app de PC de verdade (offscreen, replay fechado, conta fictícia):
  1) Start (Menu) pausa e retoma de qualquer ponto do player, dos Shorts e do Tocando agora; View abre as opções;
  2) aba "Ao vivo" do Início (#4): só lives das inscrições, estreia agendada fora, mensagem sem live e sem conta, nos 4 idiomas;
  3) faixa cinza no pé do Início: nenhuma fileira cortada pela borda de baixo vira tira lisa quando a miniatura ainda não chegou.
Uso: python3 tests/f71/run.py <pasta de trabalho> [caso...]   (precisa de client/build pronto; a pasta recebe o replay e as capturas)"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve(); work.mkdir(parents=True, exist_ok=True)
wanted = set(sys.argv[2:])
if not (work / 'tube360-replay').exists():
    subprocess.run([sys.executable, str(ROOT / 'bench/search-account/prepare.py'), str(work)], check=True, stdout=subprocess.DEVNULL)
shots = work / 'shots'; shots.mkdir(exist_ok=True)
fails = []


def check(cond, what):
    print(('ok    ' if cond else 'FALHA ') + what, flush=True)
    if not cond: fails.append(what)


def tile(vid, title, overlays, extra=None):
    t = {'tileRenderer': {'contentType': 'TILE_CONTENT_TYPE_VIDEO', 'contentId': vid, 'onSelectCommand': {'watchEndpoint': {'videoId': vid}},
         'metadata': {'tileMetadataRenderer': {'title': {'simpleText': title}, 'lines': [{'lineRenderer': {'items': [{'lineItemRenderer': {'text': {'simpleText': 'Canal de demonstração'}}}]}}]}},
         'header': {'tileHeaderRenderer': {'thumbnailOverlays': overlays}}}}
    t['tileRenderer'].update(extra or {})
    return t


def shelf(title, tiles):
    return {'shelfRenderer': {'headerRenderer': {'shelfHeaderRenderer': {'avatarLockup': {'avatarLockupRenderer': {'title': {'simpleText': title}}}}},
                              'content': {'horizontalListRenderer': {'items': tiles}}}}


LIVE = [{'thumbnailOverlayTimeStatusRenderer': {'text': {'simpleText': 'AO VIVO'}, 'style': 'LIVE'}}]
UPCOMING = [{'thumbnailOverlayTimeStatusRenderer': {'text': {'simpleText': 'ESTREIA'}, 'style': 'UPCOMING'}}]
SHORT = [{'thumbnailOverlayTimeStatusRenderer': {'text': {'simpleText': 'SHORTS'}, 'style': 'SHORTS'}}]


def timed(text):
    return [{'thumbnailOverlayTimeStatusRenderer': {'text': {'simpleText': text}, 'style': 'DEFAULT'}}]


def prepare_case(case, fixture, thumbnails=True):
    """Cópia da conta e do replay do caso (links para os arquivos grandes), com a fixture pedida."""
    accounts, replay = case / 'accounts', case / 'replay'
    shutil.copytree(work / 'accounts', accounts); replay.mkdir()
    for f in (work / 'replay').iterdir():
        if f.name != 'responses.json': (replay / f.name).symlink_to(f)
    manifest = json.loads((work / 'replay/responses.json').read_text())
    rules = json.loads((accounts / 'responses.json').read_text())
    ids = []
    if fixture == 'shelves':  # Início da conta como o TVHTML5 manda: prateleiras com título e uma de Shorts
        sections = []
        for si, name in enumerate(['Recomendados', 'Continuar assistindo', 'Novidades dos seus canais', 'Música para você']):
            tiles = [tile(f'home{si:02d}{ti:05d}', f'Vídeo {si + 1}.{ti + 1}', timed('12:34')) for ti in range(8)]
            ids += [t['tileRenderer']['contentId'] for t in tiles]
            sections.append(shelf(name, tiles))
            if si == 0:
                shorts = [tile(f'shrt{ti:07d}', f'Short {ti + 1}', SHORT) for ti in range(6)]
                ids += [t['tileRenderer']['contentId'] for t in shorts]
                sections.append(shelf('Shorts', shorts))
        for r in rules:
            if r.get('contains') == '"browseId":"FEwhat_to_watch"':
                r['json'] = {'contents': {'tvBrowseRenderer': {'content': {'tvSurfaceContentRenderer': {'content': {'sectionListRenderer': {'contents': sections}}}}}}}
    for r in rules:
        if r.get('contains') == '"browseId":"FEsubscriptions"':
            if fixture == 'nolive':  # nenhuma live (só estreia agendada e vídeos comuns)
                r['json']['contents'] = [t for t in r['json']['contents'] if not t['tileRenderer']['contentId'].startswith('live')]
            ids += [t['tileRenderer']['contentId'] for t in r['json']['contents']]
    if thumbnails:
        for vid in ids:
            for kind in ('sddefault', 'hq720', 'mqdefault', 'hqdefault'):
                manifest['requests'].insert(0, {'url': f'https://i.ytimg.com/vi/{vid}/{kind}.jpg', 'file': 'art.png', 'fields': {}})
    (accounts / 'responses.json').write_text(json.dumps(rules))
    (replay / 'responses.json').write_text(json.dumps(manifest))
    return accounts, replay


def snap(name, lang='pt', mode='720', signed=True, keys='', ms=3200, fixture=None, thumbnails=True, **extra):
    case = work / 'cases' / name; shutil.rmtree(case, ignore_errors=True); (case / 'assets').mkdir(parents=True)
    accounts, replay = prepare_case(case, fixture, thumbnails) if fixture else (work / 'accounts', work / 'replay')
    assets = case / 'assets'
    for font in (ROOT / 'client/assets').glob('*.ttf'): shutil.copy(font, assets)
    modes = {'720': 'output_height=720\n', '43': 'output_height=480\n'}
    (assets / 'settings.txt').write_text(f'lang={lang}\ncols=4\nsound=tv\nvideo_mode=auto\n' + modes[mode])
    if signed:
        (assets / 'yt-console-account.json').write_text(json.dumps({'access_token': 'console-fixture-token', 'refresh_token': 'fixture-refresh', 'expires_at': 4070908800,
            'client': {'client_id': 'fixture-client', 'client_secret': 'fixture-secret'}, 'name': 'Demo'}))
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XT_', 'F47_'))}
    env.update(HOME=str(case / 'home'), XDG_CONFIG_HOME=str(case / 'config'), XDG_CACHE_HOME=str(case / 'cache'), SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy',
               LP_NUM_THREADS='2', XT_CAST_DISABLE='1', XT_ASSETS=str(assets), F47_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(accounts), F47_AUDIT=str(case / 'audit.txt'),
               XT_SHOT=str(case / 'screen.ppm'), XT_SHOT_MS=str(ms), XT_KEYS=keys, XT_KEYS_MS='180', XT_SHOT_FRAME='2147483647')
    env.update(extra)
    with (case / 'app.log').open('w') as out:
        subprocess.run([str(work / 'tube360-replay')], env=env, cwd=case, stdout=out, stderr=subprocess.STDOUT, timeout=90, check=True)
    log = (case / 'app.log').read_text(errors='replace')
    assert 'captura:' in log, log[-2000:]
    with Image.open(case / 'screen.ppm') as im: im.convert('RGB').save(shots / (name + '.png'))
    return log, Image.open(shots / (name + '.png'))


def pauses(log): return [line.split('pause=')[1][0] for line in log.splitlines() if 'player: pause=' in line]


def case(name): return not wanted or name in wanted


PLAY = dict(XT_PLAY='R6MlUcmOul8')
# ---- 1) Start pausa e retoma
if case('start-bar'):  # foco na barra de tempo (padrão com controles à mostra)
    log, _ = snap('start-bar', keys='.' * 16 + 'S' + '.' * 4 + 'S', ms=7200, **PLAY)
    check(pauses(log) == ['1', '0'], f'Start na barra pausa e retoma ({pauses(log)})')
if case('start-buttons'):  # foco na linha de botões: o Start pausa, A continuaria abrindo o botão
    log, _ = snap('start-buttons', keys='.' * 16 + 'DL' + 'S', ms=6500, **PLAY)
    check(pauses(log) == ['1'] and 'painel=0' in log, f'Start com o foco na linha de botões pausa e não abre painel ({pauses(log)})')
if case('start-hidden'):  # controles escondidos pela inatividade
    log, _ = snap('start-hidden', keys='.' * 45 + 'S', ms=10200, **PLAY)
    check(pauses(log) == ['1'], f'Start com os controles escondidos pausa ({pauses(log)})')
if case('start-loading'):  # a trava de carregamento é a do A: durante a abertura nenhum dos dois cria pausa invisível, e depois os dois pausam
    for lead in (0, 2, 4, 7):
        by_start = pauses(snap(f'start-loading-s{lead}', keys='.' * lead + 'S', ms=1500 + lead * 200, **PLAY)[0])
        by_a = pauses(snap(f'start-loading-a{lead}', keys='.' * lead + 'A', ms=1500 + lead * 200, **PLAY)[0])
        check(by_start == by_a, f'Start e A decidem igual com {lead} passos de espera (Start {by_start}, A {by_a})')
if case('view-options'):  # View (antes o Menu) abre as opções com os controles escondidos
    _, hidden = snap('view-hidden', keys='.' * 45, ms=9600, **PLAY)
    _, viewed = snap('view-options', keys='.' * 45 + 'V', ms=10200, **PLAY)
    check(hidden.tobytes() != viewed.tobytes() and pauses(snap('view-nopause', keys='.' * 16 + 'V', ms=5200, **PLAY)[0]) == [], 'View mostra as opções e não pausa')
if case('start-shorts'):
    log, _ = snap('start-shorts', keys='.' * 16 + 'S' + '.' * 4 + 'S', ms=7200, XT_PLAY_SHORT='1', **PLAY)
    check(pauses(log) == ['1', '0'], f'Start nos Shorts pausa e retoma ({pauses(log)})')
    log, _ = snap('start-shorts-actions', keys='.' * 16 + 'R' + 'S', ms=6000, XT_PLAY_SHORT='1', **PLAY)
    check(pauses(log) == ['1'] and 'comentarios' not in log, f'Start nos Shorts com o foco nas ações pausa ({pauses(log)})')
if case('start-music'):  # abre a rádio (A) e espera a primeira faixa tocar antes do Start
    music = dict(XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1')
    log, _ = snap('start-music', keys='...A' + '.' * 28 + 'S' + '.' * 5 + 'S', ms=11500, **music)
    check(pauses(log) == ['1', '0'], f'Start no Tocando agora pausa e retoma ({pauses(log)})')
    log, _ = snap('start-music-rail', keys='...A' + '.' * 28 + 'LLLL' + 'S', ms=10500, **music)  # ← até o trilho lateral e Start de lá
    check(pauses(log) == ['1'], f'Start no Tocando agora com o foco no trilho pausa ({pauses(log)})')
    log, _ = snap('start-music-queue', keys='...A' + '.' * 28 + 'RRRRRRRRRRR' + 'S', ms=11000, **music)  # foco na fila (A seguir)
    check(pauses(log) == ['1'], f'Start no Tocando agora com o foco na fila pausa ({pauses(log)})')
# ---- 2) aba Ao vivo
LIVE_KEYS = '....URR....'  # grade -> chips -> Inscrições -> Ao vivo
if case('live'):
    for lang in ('pt', 'en', 'es', 'ar'):
        log, im = snap(f'live-{lang}', lang=lang, keys=LIVE_KEYS, ms=4600, fixture='shelves')
        reds = sum(1 for r, g, b in im.crop((130, 140, 960, 300)).get_flattened_data() if abs(r - 255) < 6 and abs(g - 15) < 8 and abs(b - 60) < 8)
        check('feed live: 3 itens' in log and reds > 600, f'Ao vivo ({lang}): só as 3 lives com selo AO VIVO, sem estreia agendada nem vídeo comum (selo={reds}px)')
    log, _ = snap('subs-pt', keys='....UR....', ms=4600, fixture='shelves')
    check('feed subs: 7 itens' in log, 'Inscrições segue com todos os vídeos (3 lives + 4 comuns; a estreia agendada não entra)')
if case('live-empty'):
    for lang in ('pt', 'en', 'es', 'ar'):
        log, im = snap(f'live-empty-{lang}', lang=lang, keys=LIVE_KEYS, ms=4600, fixture='nolive')
        text = sum(1 for r, g, b in im.crop((130, 150, 1100, 260)).get_flattened_data() if r > 120 and abs(r - g) < 12 and abs(g - b) < 12)  # texto da mensagem (cinza claro)
        check('feed live: 0 itens' in log and text > 300, f'Ao vivo sem nenhuma live ({lang}): mensagem na tela ({text}px de texto)')
if case('live-signed-out'):
    for lang in ('pt', 'en', 'es', 'ar'):
        log, im = snap(f'live-out-{lang}', lang=lang, signed=False, keys='....UR....', ms=4600, fixture='nolive')
        text = sum(1 for r, g, b in im.crop((130, 150, 1100, 260)).get_flattened_data() if r > 120 and abs(r - g) < 12 and abs(g - b) < 12)
        check('feed live' not in log and text > 300, f'Ao vivo sem conta ({lang}): mensagem na tela e nenhum pedido de rede ({text}px de texto)')
# ---- 3) faixa cinza no pé do Início
GRAY = (0x24, 0x24, 0x2b)  # ui::SURFACE2, a cor do bloco de miniatura que ainda não chegou
if case('strip'):
    for mode in ('720', '43'):
        for lang in ('pt', 'ar'):
            for n, keys in (('top', ''), ('d1', '.D'), ('d2', '.DD'), ('d3', '.DDD')):
                # sem nenhuma miniatura (as que não chegam a tempo no console): nenhuma fileira cortada pela borda de baixo (y=646 da tela de
                # 720) pode virar tira lisa; um cartão inteiro ainda sem imagem tem fundo cinza, mas termina antes da borda
                log, im = snap(f'strip-{mode}-{lang}-{n}', lang=lang, mode=mode, keys=keys, ms=3600, fixture='shelves', thumbnails=False)
                w, h = im.size
                edge = round(646 * h / 720)
                cut = [sum(1 for x in range(int(w * 0.1), w) if im.getpixel((x, y)) == GRAY) for y in (edge - 3, edge - 2, edge - 1)]
                check(max(cut) < 24, f'Início {mode} {lang} {n}: nenhuma tira cinza cortada na borda de baixo ({cut}px na última linha)')
print(f'\n{len(fails)} falha(s)' if fails else '\nf71: tudo certo')
sys.exit(1 if fails else 0)
