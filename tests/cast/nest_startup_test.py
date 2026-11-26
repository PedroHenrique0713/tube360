#!/usr/bin/env python3
"""Player/áudio/Cast TLS reais com Nest simulado; todo tráfego fica em loopback."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'bench/standalone'))
from cast_test import Driver, MEDIA, RECEIVER, Receiver

BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT/'client/build/nest-startup-test'
CAST, RELEASING, LOCAL = 0, 1, 2


class Nest(Receiver):
    mode = 'normal'
    loading_for = 0
    loaded_at = 0
    saw_loading = False
    omit_applications = True
    drop_after_load = False

    def handle(self, client, request):
        data = json.loads(request.payload_utf8)
        if self.mode == 'silent':
            self.events.append((request.namespace, data))
            return
        if self.mode == 'busy' and request.namespace == RECEIVER:
            self.events.append((request.namespace, data))
            self.reply(client, request, {'type': 'RECEIVER_STATUS', 'requestId': data['requestId'],
                       'status': {'applications': [{'appId': 'BluetoothIot', 'displayName': 'Bluetooth Audio',
                                                   'transportId': 'bluetooth', 'sessionId': 'bluetooth'}]}})
            return
        if request.namespace == MEDIA:
            if self.drop_after_load:
                self.events.append((request.namespace, data))
                return
            if data['type'] == 'LOAD':
                self.loaded_at = time.monotonic()
                if self.mode == 'load-muted':
                    self.replies = False
                    super().handle(client, request)
                    self.replies = True
                    self.drop_after_load = True
                    return
        super().handle(client, request)

    def status(self):
        result = super().status()
        if result and self.loaded_at and time.monotonic()-self.loaded_at < self.loading_for:
            self.saw_loading = True
            result[0].update(playerState='IDLE', extendedStatus={'playerState': 'LOADING'}, currentTime=0)
        return result

    def reply(self, client, request, payload, source=None):
        if self.omit_applications and payload.get('type') == 'RECEIVER_STATUS' and not payload['status'].get('applications'):
            payload = dict(payload, status={k: v for k, v in payload['status'].items() if k != 'applications'})
        super().reply(client, request, payload, source)


class Player:
    def __init__(self, assets, media):
        env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
        env.update(SDL_AUDIO_DRIVER='dummy', XT_NEST_NETWORK='127.77.53.1/255.255.255.252')
        self.log_path = assets/'player.log'
        self.log = self.log_path.open('w')
        self.process = subprocess.Popen([str(BINARY), str(assets), str(media)], env=env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, text=True)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=lambda: [self.lines.put(line) for line in self.process.stdout], daemon=True)
        self.reader.start()

    def sample(self):
        if self.process.poll() is not None:
            raise AssertionError(self.log_path.read_text())
        self.process.stdin.write('sample\n')
        self.process.stdin.flush()
        return json.loads(self.lines.get(timeout=5))

    def until(self, predicate, timeout=11):
        deadline = time.monotonic()+timeout
        sample = None
        while time.monotonic() < deadline:
            sample = self.sample()
            if predicate(sample):
                return sample
            time.sleep(.025)
        raise AssertionError(f'deadline: {sample}\n{self.log_path.read_text()}')

    def close(self):
        try:
            if self.process.poll() is None:
                self.process.stdin.write('quit\n')
                self.process.stdin.flush()
            self.process.wait(8)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(5)
            self.process.stdin.close()
            self.reader.join(2)
            self.process.stdout.close()
            self.log.close()


class StartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='tube360-f53-startup-')
        cls.folder = Path(cls.temp.name)
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=cast-fixture', '-keyout', str(cls.folder/'key.pem'),
                        '-out', str(cls.folder/'cert.pem')], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for source, args, output in [
            ('color=size=160x90:rate=30', ['-an', '-c:v', 'libx264', '-preset', 'ultrafast'], 'video.mp4'),
            ('sine=frequency=660:sample_rate=44100', ['-vn', '-c:a', 'aac'], 'audio.m4a'),
        ]:
            subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', source, '-t', '60',
                            '-threads', '1', *args, '-movflags', '+faststart', str(cls.folder/output)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.assets = Path(tempfile.mkdtemp(dir=self.folder))
        self.receiver = Nest(self.folder/'cert.pem', self.folder/'key.pem')
        self.player = None

    def tearDown(self):
        try:
            if self.player:
                self.player.close()
                self.assertEqual(self.player.process.returncode, 0, self.player.log_path.read_text())
        finally:
            self.receiver.close()
        self.assertEqual(self.receiver.errors, [])

    def start(self, mode='normal', owner=False):
        self.receiver.mode = mode
        (self.assets/'nest.txt').write_text(f'127.0.0.1 {self.receiver.port}\nFake Nest\n')
        if owner:
            record = {'token': 'previous-tube360', 'url': 'https://fixture.googlevideo.com/old',
                      'key': 'previous-video', 'host': '127.0.0.1', 'port': self.receiver.port}
            (self.assets/'nest-owner.json').write_text(json.dumps(record))
            if mode == 'normal':
                self.receiver.launched = True
                self.receiver.resource = {'contentId': record['url'], 'customData': {'tube360': record['token']}}
                self.receiver.state = 'PLAYING'
                self.receiver.position = 15
                self.receiver.stamp = time.monotonic()
        self.player = Player(self.assets, self.folder)

    def fallback(self, mode, owner=False):
        self.start(mode, owner)
        sample = self.player.until(lambda s: s['route'] == LOCAL and s['audible'] and s['frames'] > 0 and s['notices'] == 1)
        self.assertGreaterEqual(sample['elapsedMs'], 7800)
        self.assertLess(sample['elapsedMs'], 10000)
        self.assertGreater(sample['gain'], 0)
        self.assertGreater(sample['pcm'], 0)
        self.assertIn('som na TV', sample['noticeText'])
        self.assertNotEqual(sample['state'], 5, 'fallback must not fail the video')
        print(f'PASS {mode} owner={owner}: TV audible + frame + toast at {sample["elapsedMs"]} ms', flush=True)
        return sample

    def test_busy(self):
        self.fallback('busy')
        self.assertFalse(self.receiver.commands('LOAD') or self.receiver.commands('STOP'))
        self.assertTrue(any(data['type'] == 'LAUNCH' for ns, data in self.receiver.events if ns == RECEIVER))

    def test_busy_with_previous_owner(self):
        self.fallback('busy', True)
        self.assertFalse((self.assets/'nest-owner.json').exists(), 'fresh other app disproves previous ownership')
        self.assertFalse(any(data['type'] == 'STOP' for ns, data in self.receiver.events))
        deadline = time.monotonic()+1.6
        while time.monotonic() < deadline:
            sample = self.player.sample()
            self.assertEqual(sample['route'], LOCAL, 'failed startup must not retry automatically')
            self.assertEqual(sample['notices'], 1)
            time.sleep(.03)

    def test_silent(self):
        self.fallback('silent')
        self.assertFalse(self.receiver.commands('LOAD'))

    def test_silent_with_previous_owner(self):
        self.fallback('silent', True)
        self.assertTrue((self.assets/'nest-owner.json').exists(), 'silence cannot disprove ownership')

    def test_sender_deadline_with_silent_previous_owner(self):
        self.receiver.mode = 'silent'
        (self.assets/'nest.txt').write_text(f'127.0.0.1 {self.receiver.port}\nFake Nest\n')
        (self.assets/'nest-owner.json').write_text(json.dumps({
            'token': 'previous-tube360', 'url': 'https://fixture.googlevideo.com/old',
            'key': 'previous-video', 'host': '127.0.0.1', 'port': self.receiver.port,
        }))
        # O binário do sender é o mesmo usado pela suíte Cast existente, sem o prazo do Player.
        old_network = os.environ.get('XT_NEST_NETWORK')
        os.environ['XT_NEST_NETWORK'] = '127.77.53.1/255.255.255.252'
        driver = Driver(str(ROOT/'client/build/nest-test'), self.assets)
        try:
            began = time.monotonic()
            driver.command('play https://fixture.googlevideo.com/audio 0 1')
            sample = driver.until(lambda s: s['state'] == 6, timeout=10)
            delay = time.monotonic()-began
            self.assertGreater(delay, 8.5)
            self.assertLess(delay, 10)
            self.assertFalse(sample['quiet'])
            self.assertTrue((self.assets/'nest-owner.json').exists())
            print(f'PASS sender alone: silent prior owner -> ERROR after {delay:.3f} s, journal retained', flush=True)
        finally:
            driver.close()
            if old_network is None:
                os.environ.pop('XT_NEST_NETWORK', None)
            else:
                os.environ['XT_NEST_NETWORK'] = old_network

    def test_load_without_reply(self):
        self.fallback('load-muted')
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        self.assertTrue((self.assets/'nest-owner.json').exists())
        self.receiver.drop_after_load = False
        self.player.until(lambda s: s['quiet'], 3)
        self.assertTrue(self.receiver.commands('STOP'), 'retry cleanup when Nest responds again')
        self.assertFalse((self.assets/'nest-owner.json').exists())
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)

    def test_normal_idle_and_transient_loading(self):
        self.receiver.loading_for = .6
        self.start()
        sample = self.player.until(lambda s: s['route'] == CAST and s['frames'] > 0 and s['clockReady'], 5)
        self.assertTrue(self.receiver.saw_loading, 'exercise IDLE + extendedStatus LOADING')
        self.assertFalse(sample['audible'] or sample['notices'])
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        print(f'PASS normal: missing applications + transient LOADING -> Nest at {sample["elapsedMs"]} ms', flush=True)

    def test_loading_for_five_seconds(self):
        self.receiver.loading_for = 5.2
        self.start()
        sample = self.player.until(lambda s: s['route'] == CAST and s['frames'] > 0 and s['clockReady'], 8)
        self.assertGreater(sample['elapsedMs'], 5000)
        self.assertFalse(sample['audible'] or sample['notices'])
        print(f'PASS slow LOAD: Nest at {sample["elapsedMs"]} ms, no premature IDLE failure', flush=True)

    def test_legitimate_previous_owner(self):
        self.start(owner=True)
        sample = self.player.until(lambda s: s['route'] == CAST and s['frames'] > 0 and s['clockReady'], 5)
        commands = [data for ns, data in self.receiver.events if ns == MEDIA and data['type'] in ('LOAD', 'STOP')]
        self.assertEqual([data['type'] for data in commands[:2]], ['STOP', 'LOAD'])
        self.assertEqual(commands[0]['mediaSessionId'], 7, 'stop exactly the previous owned session')
        self.assertFalse(sample['audible'] or sample['notices'])
        self.assertTrue((self.assets/'nest-owner.json').exists())
        print('PASS legitimate previous owner: STOP owned session 7 -> LOAD, Nest playing, no TV/toast', flush=True)

    def test_playback_silence_keeps_25_seconds(self):
        self.start()
        self.player.until(lambda s: s['route'] == CAST and s['frames'] > 0 and s['clockReady'], 5)
        first_stop = []
        original = self.receiver.handle
        def handle(client, request):
            if request.namespace == MEDIA and json.loads(request.payload_utf8)['type'] == 'STOP':
                first_stop.append(time.monotonic())
            original(client, request)
        self.receiver.handle = handle
        self.receiver.replies = False
        began = time.monotonic()
        while time.monotonic()-began < 24:
            sample = self.player.sample()
            self.assertEqual(sample['route'], CAST, 'startup deadline must not affect established playback')
            self.assertEqual(sample['notices'], 0)
            time.sleep(.04)
        self.player.until(lambda s: s['route'] == RELEASING, 3)
        self.receiver.replies = True
        sample = self.player.until(lambda s: s['route'] == LOCAL and s['audible'] and s['notices'] == 1, 4)
        self.assertTrue(first_stop)
        delay = first_stop[0]-began
        self.assertGreater(delay, 24)
        self.assertLess(delay, 27)
        print(f'PASS playback silence: first STOP after {delay:.3f} s, TV only after quiet confirmation', flush=True)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]], verbosity=2)
