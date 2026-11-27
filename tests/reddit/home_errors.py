"""F32/5: Home real, replay HTTPS e preferências/arquivo de servidor legados em loopback, ignorados em silêncio."""
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
TITLES = {
    'pt': ('Acerte a data e a hora do console', 'Falha na conexão segura. Confira as atualizações do app.', 'Falha no DNS. Confira o DNS nas configurações de rede.', 'Sem conexão. Confira o cabo ou Wi-Fi.', 'YouTube recusou o pedido. Tente mais tarde.', 'Não deu para carregar esta lista'),
    'en': ('Set the console date and time', 'Secure connection failed. Check for app updates.', 'DNS failed. Check your network DNS settings.', 'No connection. Check your cable or Wi-Fi.', 'YouTube refused the request. Try again later.', 'Could not load this list'),
    'es': ('Ajusta la fecha y la hora de la consola', 'Falló la conexión segura. Busca actualizaciones de la app.', 'Falló el DNS. Revisa el DNS en los ajustes de red.', 'Sin conexión. Revisa el cable o Wi-Fi.', 'YouTube rechazó la solicitud. Inténtalo más tarde.', 'No se pudo cargar esta lista'),
}
REFRESH = {'pt': 'Atualizar', 'en': 'Refresh', 'es': 'Actualizar'}
REASONS = ('tls_date', 'tls', 'dns', 'network', 'http', 'json')


class Reserve(http.server.BaseHTTPRequestHandler):
    paths = []

    def do_GET(self):
        self.paths.append(self.path)
        self.send_response(503)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *_):
        pass


def capture(base, lang, reason, *, mode='default', sd=False):
    name = f'{lang}-{reason}-{mode}' + ('-sd' if sd else '')
    folder = base / name
    folder.mkdir(exist_ok=True)
    assets = folder / 'assets'
    assets.mkdir(exist_ok=True)
    for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
        shutil.copy(font, assets)
    settings = f'lang={lang}\nsound=tv\n'
    if mode in ('server', 'auto'):
        settings += 'server=auto\n'
    if mode == 'off':
        settings += 'server=off\n'
    if sd:
        settings += 'output_height=480\nlarge_text=on\n'
    (assets / 'settings.txt').write_text(settings)
    server_file = assets / 'server.txt'
    server_file.unlink(missing_ok=True)
    if mode in ('server', 'off'):
        server_file.write_text(f'127.0.0.1 {reserve.server_port}\n')
    assert mode != 'default' or ('server=' not in settings and not server_file.exists())
    (folder / 'error.txt').write_text(reason)
    image = base / 'proofs' / (name + '.ppm')
    image.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(folder), XDG_CACHE_HOME=str(folder / 'cache'),
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1',
               XT_ASSETS=str(assets), XT_YT_REPLAY=str(folder), XT_SHOT=str(image), XT_SHOT_MS='1000')
    before = len(Reserve.paths)
    result = subprocess.run([str(ROOT / 'client/build/tube360')], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    log = result.stdout.decode(errors='replace')
    (image.with_suffix('.log')).write_text(log)
    assert result.returncode == 0, (name, result.returncode, log[-2000:])
    match = re.search(r'captura erro Home: (.*?) \| (.*?) \| (.*)', log)
    assert match, (name, 'Home error was not drawn', log[-2000:])
    title, help_text, detail = match.groups()
    assert 'tela 0,' in log, (name, 'capture is not Home')
    assert title == TITLES[lang][REASONS.index(reason)], (name, match[0])
    assert help_text == REFRESH[lang], (name, help_text)
    assert not any(word in (title + help_text + detail).lower() for word in ('server', 'servidor', '0.0.0.0')), name
    assert len(Reserve.paths) == before, (name, 'a legacy server preference or server.txt caused a request')
    if reason == 'tls_date':
        assert re.search(r'\d{4}-\d\d-\d\d \d\d:\d\d:\d\d UTC', detail), (name, detail)
    else:
        assert not detail, (name, detail)
    with Image.open(image) as im:
        assert im.size == ((640, 480) if sd else (1280, 720)), name
        im.save(image.with_suffix('.png'))
    image.unlink()
    print('PASS', name, title, flush=True)
    return dict(name=name, mode=mode, title=title,
                help=help_text, detail=detail, screenshot=str(image.with_suffix('.png')))


base = Path(sys.argv[1]).resolve()
(base / 'proofs').mkdir(parents=True, exist_ok=True)
reports = []
with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Reserve) as reserve:
    thread = threading.Thread(target=reserve.serve_forever, daemon=True)
    thread.start()
    try:
        for lang in ('pt', 'en', 'es'):
            for reason in REASONS:
                reports.append(capture(base, lang, reason))
            reports.append(capture(base, lang, 'network', mode='server'))
            reports.append(capture(base, lang, 'network', mode='auto'))
            reports.append(capture(base, lang, 'tls_date', mode='off'))
            reports.append(capture(base, lang, 'tls', sd=True))
    finally:
        reserve.shutdown()
        thread.join(timeout=5)
(base / 'results.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2) + '\n')
# Cada coluna é um idioma; a última linha é o servidor legado configurado, que não muda nada.
montage = Image.new('RGB', (1280, 240 * 7), (24, 24, 29))
for row, reason in enumerate((*REASONS, 'server')):
    for col, lang in enumerate(('pt', 'en', 'es')):
        name = f'{lang}-{reason}-default' if reason != 'server' else f'{lang}-network-server'
        with Image.open(base / 'proofs' / (name + '.png')) as im:
            montage.paste(im.resize((426, 240)), (col * 426, row * 240))
montage.save(base / 'proofs' / 'home-errors-montage.png')
print('PASS 30 Home captures: default, legacy server.txt with server=auto or off, and SD/large text', flush=True)
