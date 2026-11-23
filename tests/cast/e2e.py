#!/usr/bin/env python3
"""Celular e YouTube falsos em loopback, com o main/player/render reais e server=off."""
import argparse
import contextlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
IDS = ['F14test0001', 'F14test0002', 'F14test0003']


def wait_for(predicate, label, timeout=15):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        result = predicate()
        if result:
            return result
        time.sleep(.04)
    raise AssertionError('timeout ' + label)


class Lounge:
    def __init__(self, work):
        self.cv = threading.Condition()
        self.pending = []
        self.events = []
        self.errors = []
        self.aid = 2
        self.session = 0
        self.offset = 0
        self.rpc_count = 0
        self.disconnect = False
        self.reject = False
        self.registered = 0
        self.code_count = 0
        self.devices = []
        self.ranges = 0
        self.video = (work / 'sample.mp4').read_bytes()
        self.thumbnail = (work / 'thumbnail.jpg').read_bytes()

    def message(self, name, payload):
        with self.cv:
            self.aid += 1
            row = [self.aid, [name, payload]]
            self.pending.append(row)
            self.cv.notify_all()
            return row

    def seen(self, name, predicate=lambda x: True, after=0):
        with self.cv:
            return next((e for e in self.events[after:] if e[0] == name and predicate(e[1])), None)

    def mark(self):
        with self.cv:
            return len(self.events)


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'

    def log_message(self, *args):
        pass

    @property
    def lounge(self):
        return self.server.lounge

    def response(self, body, status=200, headers=None):
        if isinstance(body, dict):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        try:
            self.get()
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as e:
            self.lounge.errors.append(repr(e))
            self.response('mock assertion', 500)

    def get(self):
        lounge = self.lounge
        path = urlsplit(self.path).path
        if path.endswith('/generate_screen_id'):
            self.response('FAKE-SCREEN-F14')
        elif path == '/api/lounge/bc/bind':
            query = parse_qs(urlsplit(self.path).query)
            assert query['SID'] == ['FAKE-SID-' + str(lounge.session)], query
            assert query['gsessionid'] == ['FAKE-GSESSION'], query
            assert query['RID'] == ['rpc'] and query['AID'], query
            with lounge.cv:
                lounge.rpc_count += 1
                if lounge.reject:
                    lounge.reject = False
                    self.response('', 400)
                    return
            session = lounge.session
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            while True:
                with lounge.cv:
                    lounge.cv.wait_for(lambda: lounge.pending or lounge.disconnect, timeout=.4)
                    if lounge.session != session:
                        return
                    if lounge.disconnect:
                        lounge.disconnect = False
                        return
                    rows, lounge.pending = lounge.pending, []
                payload = json.dumps(rows, ensure_ascii=False, separators=(',', ':')).encode()
                frame = str(len(payload)).encode() + b'\n' + payload
                # Corta inclusive o prefixo de tamanho e os bytes UTF-8.
                for start in range(0, len(frame), 7):
                    self.wfile.write(frame[start:start + 7])
                    self.wfile.flush()
        elif path == '/':
            self.response('"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}')
        elif path == '/watch':
            self.response('"VISITOR_DATA":"FAKE-VISITOR","jsUrl":"/fixture.js"')
        elif path == '/fixture.js':
            self.response('signatureTimestamp:12345')
        elif path.startswith('/vi/') and path.endswith('.jpg'):
            self.response(lounge.thumbnail, headers={'Content-Type': 'image/jpeg'})
        elif path == '/sample.mp4':
            byte_range = self.headers.get('Range', '')
            assert byte_range.startswith('bytes='), byte_range
            first, last = map(int, byte_range[6:].split('-'))
            last = min(last, len(lounge.video) - 1)
            lounge.ranges += 1
            self.response(lounge.video[first:last + 1], 206,
                          {'Content-Range': f'bytes {first}-{last}/{len(lounge.video)}'})
        else:
            raise AssertionError('unexpected GET ' + self.path)

    def do_POST(self):
        try:
            self.post()
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as e:
            self.lounge.errors.append(repr(e))
            self.response('mock assertion', 500)

    def post(self):
        lounge = self.lounge
        body = self.rfile.read(int(self.headers['Content-Length'])).decode()
        path = urlsplit(self.path).path
        if path.startswith('/api/lounge/'):
            assert self.headers['Content-Type'] == 'application/x-www-form-urlencoded'
            fields = {k: v[0] for k, v in parse_qs(body).items()}
            if path.endswith('/get_lounge_token_batch'):
                assert fields == {'screen_ids': 'FAKE-SCREEN-F14'}, fields
                self.response({'screens': [{'loungeToken': 'FAKE-LOUNGE-TOKEN', 'expiration': 2400}]})
            elif path.endswith('/get_pairing_code'):
                assert fields['lounge_token'] == 'FAKE-LOUNGE-TOKEN'
                assert fields['screen_name'] == 'YouTube 360'
                lounge.code_count += 1
                self.response('123456789012')
            elif path.endswith('/register_pairing_code'):
                assert fields['pairing_code'] == 'FAKE-PHONE-CODE', fields
                assert fields['access_type'] == 'permanent'
                lounge.registered += 1
                self.response('')
            elif path.endswith('/bind'):
                query = parse_qs(urlsplit(self.path).query)
                assert query['device'] == ['LOUNGE_SCREEN']
                assert query['loungeIdToken'] == ['FAKE-LOUNGE-TOKEN']
                count = int(fields['count'])
                with lounge.cv:
                    if count == 0:
                        lounge.session += 1
                        lounge.offset = 0
                        payload = json.dumps([[0, ['c', 'FAKE-SID-' + str(lounge.session), '', 8]],
                                              [1, ['S', 'FAKE-GSESSION']]])
                        self.response(str(len(payload)) + '\n' + payload)
                        return
                    assert query['SID'] == ['FAKE-SID-' + str(lounge.session)], query
                    assert int(fields['ofs']) == lounge.offset, fields
                    lounge.offset += count
                    for index in range(count):
                        prefix = f'req{index}_'
                        event = fields[prefix + '_sc']
                        data = {k[len(prefix):]: v for k, v in fields.items()
                                if k.startswith(prefix) and k != prefix + '_sc'}
                        lounge.events.append((event, data))
                self.response('')
            else:
                raise AssertionError('unexpected lounge ' + path)
        elif path == '/youtubei/v1/player':
            video_id = json.loads(body)['videoId']
            assert video_id in IDS
            self.response({'playabilityStatus': {'status': 'OK'},
                           'videoDetails': {'videoId': video_id, 'title': 'F14 native cast fixture', 'lengthSeconds': '12'},
                           'streamingData': {'expiresInSeconds': '3600', 'formats': [
                               {'itag': 22, 'width': 1280, 'height': 720, 'fps': 30, 'bitrate': 1800000,
                                'mimeType': 'video/mp4; codecs="avc1.64001f, mp4a.40.2"',
                                'url': 'https://fixture.googlevideo.com/sample.mp4'}]}})
        elif path == '/youtubei/v1/next' and json.loads(body).get('playlistId'):
            request = json.loads(body)
            assert request['context']['client']['clientName'] == 'TVHTML5'
            assert request['playlistId'] == 'FAKE-QUEUE'
            assert request['enableMdxAutoplay'] and request['isMdxPlayback']
            target = {'nextVideoRenderer': {'autoplayEndpointRenderer': {'endpoint': {'watchEndpoint': {'videoId': IDS[2]}}}}} if request['videoId'] != IDS[2] else {}
            self.response({'contents': {'singleColumnWatchNextResults': {'autoplay': {'autoplay': {'sets': [target]}}}}})
        elif path in ('/youtubei/v1/browse', '/youtubei/v1/search', '/youtubei/v1/next'):
            self.response({'contents': {'videoRenderer': {'videoId': IDS[2], 'title': {'simpleText': 'F14 recorded feed'}}}})
        else:
            raise AssertionError('unexpected POST ' + path)


def dial(method, path, body=None, headers=None):
    conn = http.client.HTTPConnection('127.0.0.1', 8134, timeout=15)
    try:
        conn.request(method, path, body, headers or {})
        response = conn.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        conn.close()


def discover():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(1)
        sock.sendto(b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1934\r\n'
                    b'MAN: "ssdp:discover"\r\nMX: 1\r\nST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n',
                    ('127.0.0.1', 1934))
        data, _ = sock.recvfrom(4096)
        assert b'200 OK' in data and b'LOCATION: http://127.0.0.1:8134/ssdp/device-desc.xml' in data


@contextlib.contextmanager
def app(binary, work, lang, shot, seconds, rail=False, server_mode="off"):
    assets = work / ('assets-' + lang)
    home = work / 'home'
    home.mkdir(exist_ok=True)
    assets.mkdir(exist_ok=True)
    for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
        shutil.copy(font, assets)
    (assets / 'settings.txt').write_text(f'lang={lang}\ncols=4\nquality=720\nsound=tv\nserver={server_mode}\n')
    env = dict(os.environ, HOME=str(home), XDG_CACHE_HOME=str(home / '.cache'), SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_ASSETS=str(assets),
               XT_SERVER='127.0.0.1:8134', XT_SHOT=str(work / (shot + '.ppm')), XT_SHOT_MS=str(seconds * 1000))
    if rail:
        env.update(XT_SCREEN='rail', XT_KEYS='DDDDDA', XT_KEYS_MS='160')
    log = open(work / (shot + '.log'), 'w')
    process = subprocess.Popen([str(binary)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        yield process
        assert process.wait(timeout=seconds + 25) == 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()


def exercise(lounge):
    wait_for(lambda: lounge.code_count, 'TV code')
    discover()
    status, headers, body = dial('GET', '/ssdp/device-desc.xml')
    assert status == 200 and headers['Application-URL'] == 'http://127.0.0.1:8134/apps/'
    root = ET.fromstring(body)
    assert root.find('.//{*}friendlyName').text == 'YouTube 360'
    status, _, body = dial('GET', '/apps/YouTube')
    assert status == 200 and ET.fromstring(body).find('{*}state').text == 'running'
    assert dial('POST', '/apps/YouTube', 'pairingCode=%00bad')[0] == 400
    assert dial('POST', '/apps/YouTube', 'pairingCode=a&pairingCode=b')[0] == 400
    status, headers, _ = dial('POST', '/apps/YouTube', urlencode({'pairingCode': 'FAKE-PHONE-CODE', 'theme': 'cl'}))
    assert status == 201 and headers['Location'].endswith('/apps/YouTube/run')
    assert lounge.registered == 1
    devices = [{'id': 'FAKE-PHONE', 'type': 'REMOTE_CONTROL', 'name': 'Celular F14', 'user': 'Família F14', 'theme': 'cl'}]
    lounge.message('loungeStatus', {'devices': json.dumps(devices, ensure_ascii=False)})
    lounge.message('setPlaylist', {'listId': 'FAKE-QUEUE', 'videoIds': ','.join(IDS), 'videoId': IDS[0], 'currentIndex': '0', 'currentTime': '0'})
    playing = lambda video_id: lambda d: d.get('videoId') == video_id and d.get('state') == '1'
    wait_for(lambda: lounge.seen('nowPlaying', playing(IDS[0])), 'native H264 playback')
    mark = lounge.mark()
    duplicate = lounge.message('pause', {})
    wait_for(lambda: lounge.seen('onStateChange', lambda d: d.get('state') == '2', mark), 'pause')
    # O replay do mesmo AID deve ser descartado depois de uma retomada nova.
    mark = lounge.mark()
    lounge.message('play', {})
    wait_for(lambda: lounge.seen('onStateChange', lambda d: d.get('state') == '1', mark), 'resume')
    with lounge.cv:
        lounge.pending.append(duplicate)
        lounge.cv.notify_all()
    time.sleep(.4)
    mark = lounge.mark()
    lounge.message('getNowPlaying', {})
    wait_for(lambda: lounge.seen('nowPlaying', lambda d: d.get('state') == '1', mark), 'discard duplicate pause')
    lounge.message('seekTo', {'newTime': '4'})
    wait_for(lambda: lounge.seen('nowPlaying', lambda d: d.get('videoId') == IDS[0] and d.get('state') == '1' and float(d.get('currentTime', 0)) >= 4, mark), 'absolute seek')
    mark = lounge.mark()
    lounge.message('setVolume', {'volume': '35', 'muted': 'false'})
    lounge.message('getVolume', {})
    wait_for(lambda: lounge.seen('onVolumeChanged', lambda d: float(d['volume']) == 35, mark), 'volume')
    lounge.message('setVolume', {'volume': '35', 'muted': 'true'})
    wait_for(lambda: lounge.seen('onVolumeChanged', lambda d: d['muted'] == 'true', mark), 'mute')
    lounge.message('setVolume', {'volume': '70', 'muted': 'false'})
    mark = lounge.mark()
    lounge.message('next', {})
    wait_for(lambda: lounge.seen('nowPlaying', playing(IDS[1]), mark), 'next')
    mark = lounge.mark()
    lounge.message('previous', {})
    wait_for(lambda: lounge.seen('nowPlaying', playing(IDS[0]), mark), 'previous')
    mark = lounge.mark()
    lounge.message('updatePlaylist', {'listId': 'FAKE-QUEUE', 'videoIds': IDS[0] + ',' + IDS[2]})
    lounge.message('seekTo', {'newTime': '11'})
    wait_for(lambda: lounge.seen('nowPlaying', playing(IDS[2]), mark), 'natural end advances updated queue')
    mark = lounge.mark()
    lounge.message('stopVideo', {})
    wait_for(lambda: lounge.seen('nowPlaying', lambda d: d.get('state') == '-1', mark), 'stop')
    lounge.message('loungeStatus', {'devices': '[]', 'connectionEventDetails': json.dumps({'ui': 'true'})})
    count = lounge.rpc_count
    with lounge.cv:
        lounge.disconnect = True
        lounge.cv.notify_all()
    wait_for(lambda: lounge.rpc_count > count, 'RPC reconnect')
    mark = lounge.mark()
    lounge.message('getNowPlaying', {})
    wait_for(lambda: lounge.seen('nowPlaying', lambda d: d.get('state') == '-1', mark), 'state after reconnect')
    count = lounge.session
    with lounge.cv:
        lounge.reject = True
        lounge.disconnect = True
        lounge.cv.notify_all()
    wait_for(lambda: lounge.session > count, 'new SID after expired server session', 20)
    # Deixa um vídeo visível na captura final.
    mark = lounge.mark()
    lounge.message('setPlaylist', {'listId': 'FAKE-QUEUE', 'videoIds': IDS[0], 'videoId': IDS[0], 'currentIndex': '0'})
    wait_for(lambda: lounge.seen('nowPlaying', playing(IDS[0]), mark), 'play after session recovery')
    lounge.message('setAutoplayMode', {'autoplayMode': 'ENABLED'})
    wait_for(lambda: lounge.seen('onHasPreviousNextChanged', lambda d: d.get('hasNext') == 'true', mark), 'autoplay suggestion from native next')
    assert lounge.ranges > 0
    assert not lounge.errors, lounge.errors


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--binary', type=Path, default=ROOT / 'client/build/cast-test-app')
    args.add_argument('--work', type=Path, default=ROOT / '.work/f14')
    opts = args.parse_args()
    work = opts.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    sample = work / 'sample.mp4'
    if not sample.exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '12', '-c:v', 'libx264',
                        '-preset', 'ultrafast', '-threads', '2', '-pix_fmt', 'yuv420p', '-g', '30', '-c:a', 'aac',
                        '-movflags', '+faststart', str(sample)], check=True)
    thumbnail = work / 'thumbnail.jpg'
    if not thumbnail.exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(sample), '-frames:v', '1',
                        '-vf', 'scale=320:180', '-threads', '1', str(thumbnail)], check=True)
    lounge = Lounge(work)
    server = ThreadingHTTPServer(('127.0.0.1', 8234), Handler)
    server.daemon_threads = True
    server.lounge = lounge
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    started = time.monotonic()
    try:
        with app(opts.binary.resolve(), work, 'pt', 'player', 20):
            exercise(lounge)
        for lang in ('pt', 'en'):
            with lounge.cv:
                lounge.pending.clear()
                count = lounge.code_count
                rpc = lounge.rpc_count
            with app(opts.binary.resolve(), work, lang, 'pairing-' + lang, 4, rail=True):
                wait_for(lambda: lounge.code_count > count and lounge.rpc_count > rpc, 'manual pairing session')
                lounge.message('loungeStatus', {'devices': json.dumps([{'id': 'FAKE-PHONE', 'type': 'REMOTE_CONTROL', 'name': 'F14 phone', 'theme': 'cl'}])})
        count = lounge.code_count
        mark = lounge.mark()
        with app(opts.binary.resolve(), work, 'pt', 'reserve-auto', 5, server_mode='auto'):
            wait_for(lambda: lounge.code_count > count, 'native receiver with PC reserve enabled')
            lounge.message('setPlaylist', {'listId': 'FAKE-QUEUE', 'videoIds': IDS[0], 'videoId': IDS[0], 'currentIndex': '0'})
            wait_for(lambda: lounge.seen('nowPlaying', lambda d: d.get('videoId') == IDS[0] and d.get('state') == '1', mark), 'native cast with server=auto')
        assert not lounge.errors, lounge.errors
        (work / 'events.json').write_text(json.dumps(lounge.events, indent=2, ensure_ascii=False))
        print(f'PASS SSDP, DIAL, TV code, lounge, queue, pause/resume, seek, volume/mute, next/previous, natural end, reconnect, SID recovery')
        print(f'PASS decoded 720p30 fixture with server=off, {lounge.ranges} range reads, {lounge.session} sessions, {time.monotonic() - started:.2f}s')
        for name in ('player', 'pairing-pt', 'pairing-en', 'reserve-auto'):
            subprocess.run(['magick', str(work / (name + '.ppm')), str(work / (name + '.png'))], check=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
