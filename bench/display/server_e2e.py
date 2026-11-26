"""Servidor FastAPI real como sentinela: preferências antigas não o habilitam no cliente SD."""
import asyncio
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()


def serve():
    media = work/'media'
    class Account(BaseHTTPRequestHandler):
        def do_GET(self):
            path = media/Path(self.path.split('?', 1)[0]).name
            if path.is_file() and path.suffix in ('.mp4', '.m4a'):
                data = path.read_bytes()
            else:
                data = json.dumps(dict(state='signed_out', signed_in=False, items=[])).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass
        def log_message(self, *args):
            pass
    account = ThreadingHTTPServer(('127.0.0.1', 8252), Account)
    threading.Thread(target=account.serve_forever, daemon=True).start()
    spec = importlib.util.spec_from_file_location('display_server', ROOT/'server/tube360_server.py')
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0,str(ROOT/'server'))
    spec.loader.exec_module(m)
    @m.app.middleware('http')
    async def audit_requests(request, call_next):
        with (work/'server-requests.txt').open('a') as audit:
            audit.write(request.url.path+'\n')
        return await call_next(request)
    async def home(*args, **kwargs):
        return [dict(id=f'video{i:06}', duration=125, channel='Canal de prueba', views='1200 views',
                     title=f'Paisajes y viajes en tren — episodio {i}', pos=0) for i in range(1, 25)]
    async def resolve(video):
        origin='http://127.0.0.1:8252/'
        video=dict(url=origin+'720.mp4', width=1280, height=720, fps=30, vcodec='avc1', acodec='none', protocol='https', ext='mp4')
        audio=dict(url=origin+'audio.m4a', vcodec='none', acodec='mp4a', protocol='https', ext='m4a')
        return dict(video=video, audio=audio, duration=10, live=False, title='Video de prueba en SD', headers={}, original={720:video})
    def thumb(video_id, width, height, path):
        subprocess.run(['magick', str(work/'replay/art.png'), '-resize', f'{width}x{height}!', str(path)], check=True)
    m.home_videos=home
    m.search_videos=home
    m.resolve_video=resolve
    m.make_thumb=thumb
    m.prefetch_later=lambda items: None
    import uvicorn
    uvicorn.run(m.app, host='127.0.0.1', port=8152, log_level='warning')


if '--serve' in sys.argv:
    serve()
    raise SystemExit
home = work/'server-home'
home.mkdir(exist_ok=True)
config = json.loads((ROOT/'server/config.example.json').read_text())
config.pop('cookies_from_browser', None)
config.update(language='es', warm_cache=False)
(work/'config.json').write_text(json.dumps(config))
env = dict(os.environ, HOME=str(home), TUBE360_CONFIG=str(work/'config.json'), TUBE360_PORT='8152',
           TUBE360_ACCOUNT_URL='http://127.0.0.1:8252', TUBE360_WARM_CACHE='0')
(work/'server-requests.txt').unlink(missing_ok=True)
with (work/'server.log').open('w') as log:
    proc = subprocess.Popen([sys.executable, __file__, str(work), '--serve'], env=env, stdout=log, stderr=subprocess.STDOUT)
    (work/'server.pid').write_text(str(proc.pid))
    try:
        for attempt in range(100):
            try:
                with urllib.request.urlopen('http://127.0.0.1:8152/api/account', timeout=.5):
                    break
            except Exception:
                if proc.poll() is not None:
                    raise RuntimeError((work/'server.log').read_text())
                time.sleep(.1)
        else:
            raise RuntimeError('server startup timeout')
        with urllib.request.urlopen('http://127.0.0.1:8152/api/feed?kind=home&n=24&offset=0&fresh=0&lang=es') as reply:
            body=reply.read().decode()
        assert 'Recomendados' in body and '1.2 mil vistas' in body
        requests_before = (work/'server-requests.txt').read_text()
        (work/'blocked').mkdir(exist_ok=True)
        for name, extra in [('480-server-home', {}), ('480-server-player', {'XT_PLAY':'4p_9OPKgcDE'})]:
            assets=work/name
            assets.mkdir(exist_ok=True)
            for font in (ROOT/'client/assets').glob('inter-*.ttf'):
                shutil.copyfile(font, assets/font.name)
            (assets/'settings.txt').write_text('lang=es\nserver=auto\noutput_height=480\ncols=4\nsound=tv\n')
            appenv=dict(env, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1',
                        XT_SERVER='127.0.0.1:8152', XT_ASSETS=str(assets), XT_CATALOG_REPLAY=str(work/'blocked'),
                        XT_SHOT=str(assets/'screen.ppm'), XT_SHOT_MS='4000', **extra)
            with (assets/'app.log').open('w') as app_log:
                subprocess.run([str(work/'tube360-replay')], env=appenv, stdout=app_log, stderr=subprocess.STDOUT, timeout=40, check=True)
            subprocess.run(['magick', str(assets/'screen.ppm'), str(assets/'screen.png')], check=True)
            app_log=(assets/'app.log').read_text()
            assert 'captura: ' in app_log
            assert '(desligado: só console)' in app_log, app_log
            assert (work/'server-requests.txt').read_text() == requests_before, 'app contacted the legacy server'
            if extra:
                assert 'primeiro quadro:' not in app_log and 'estado=5 erro=console' in app_log, app_log
        print('HTTP es feed + 2 SD app cases passed; no legacy server requests; isolated server PID', proc.pid)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
