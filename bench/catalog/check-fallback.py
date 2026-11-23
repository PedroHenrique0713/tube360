"""Exercita a reserva do PC com HTTP local, conta falsa e erro gravado do player nativo."""
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading

app, source, output = map(lambda p: pathlib.Path(p).resolve(), sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
for codec in ('aac', 'mp2'):
    replay = output / ('replay-' + codec)
    shutil.copytree(source, replay, dirs_exist_ok=True)
    manifest = json.loads((replay / 'responses.json').read_text())
    for entry in manifest['requests']:
        if entry.get('fields', {}).get('videoId') == 'F12audio001' and '/player?' in entry['url']:
            entry['status'] = 503
    (replay / 'responses.json').write_text(json.dumps(manifest))
    (replay / 'assets/settings.txt').write_text('lang=pt\ncols=4\nquality=720\nsound=tv\nserver=auto\n')
    media = output / ('fallback.' + ('ts' if codec == 'aac' else 'mpg'))
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source / 'video.mp4'),
                    '-c:v', 'copy' if codec == 'aac' else 'mpeg1video', '-threads', '2',
                    '-c:a', 'aac' if codec == 'aac' else 'mp2', '-ar', '44100', '-ac', '2',
                    '-muxdelay', '0' if codec == 'aac' else '0.7', '-f', 'mpegts' if codec == 'aac' else 'mpeg', str(media)], check=True)
    data = media.read_bytes()
    paths = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            paths.append(self.path)
            stream = self.path.startswith('/api/stream/')
            body = data if stream else b'ok\t\n'
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('X-Duration', '4')
            self.send_header('X-Video-Codec', 'h264' if codec == 'aac' else 'mpeg1')
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass
        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 8132), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy', 'LP_NUM_THREADS': '2',
               'XT_ASSETS': str(replay / 'assets'), 'XT_CATALOG_REPLAY': str(replay),
               'XT_SERVER': '127.0.0.1:8132', 'XT_NAV': 'radio', 'XT_NAV_ID': 'F12audio001', 'XT_MUSIC': '1',
               'XT_KEYS': 'UA', 'XT_KEYS_MS': '400', 'XT_SHOT_FRAME': '2147483647', 'XT_SHOT_MS': '3000',
               'XT_SHOT': str(output / ('fallback-' + codec + '.ppm'))}
        log = output / ('fallback-' + codec + '.log')
        with log.open('w') as stream:
            subprocess.run([str(app)], env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=30, check=True)
        content = log.read_text()
        assert any(p.startswith('/api/stream/F12audio001') for p in paths), paths
        assert 'musica: reserva no PC' in content, content
        assert 'musica: primeiro audio' in content, content
        assert 'primeiro quadro:' not in content, content
        assert 'posição 0.00' not in content, content
        subprocess.run(['magick', str(output / ('fallback-' + codec + '.ppm')), str(output / ('fallback-' + codec + '.png'))], check=True)
        print(codec + ' fallback passed', flush=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
