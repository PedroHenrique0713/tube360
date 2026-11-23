#!/usr/bin/env python3
"""Receptor Cast de bancada. TLS e CastMessage usam implementações externas ao cliente C++."""
import json
import pathlib
import queue
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pychromecast.generated.cast_channel_pb2 import CastMessage

CONNECTION = 'urn:x-cast:com.google.cast.tp.connection'
HEARTBEAT = 'urn:x-cast:com.google.cast.tp.heartbeat'
RECEIVER = 'urn:x-cast:com.google.cast.receiver'
MEDIA = 'urn:x-cast:com.google.cast.media'
APP = 'CC1AD845'


class Receiver:
    def __init__(self, cert, key, port=0):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(cert, key)
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(('127.0.0.1', port))
        self.listener.listen()
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.quit = False
        self.online = True
        self.replies = True
        self.fragment = True
        self.namespaces = True
        self.bad_frame = False
        self.consume = None
        self.launched = False
        self.resource = None
        self.state = 'IDLE'
        self.position = 0
        self.stamp = time.monotonic()
        self.session = 7
        self.events = []
        self.errors = []
        self.clients = []
        self.lock = threading.RLock()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def close(self):
        self.quit = True
        self.disconnect()
        self.thread.join(3)
        self.listener.close()

    def disconnect(self):
        for client in list(self.clients):
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def at(self):
        return self.position + (time.monotonic()-self.stamp if self.state == 'PLAYING' else 0)

    def status(self):
        if not self.resource:
            return []
        return [{'mediaSessionId': self.session, 'playerState': self.state, 'currentTime': self.at(),
                 'playbackRate': 1, 'media': self.resource}]

    def reply(self, client, request, payload, source=None):
        if not self.replies and request.namespace != HEARTBEAT:
            return
        if self.bad_frame:
            self.bad_frame = False
            client.sendall(struct.pack('!I', 65537))
            return
        response = CastMessage(protocol_version=0, source_id=source or request.destination_id,
                               destination_id=request.source_id, namespace=request.namespace,
                               payload_type=0, payload_utf8=json.dumps(payload))
        body = response.SerializeToString()
        frame = struct.pack('!I', len(body))+body
        # Fragmenta inclusive o prefixo de tamanho. O sender precisa preservar a leitura parcial.
        if self.fragment:
            for part in (frame[:1], frame[1:3], frame[3:19], frame[19:]):
                client.sendall(part)
        else:
            client.sendall(frame)

    def handle(self, client, request):
        assert request.IsInitialized() and request.protocol_version == 0 and request.payload_type == 0
        data = json.loads(request.payload_utf8)
        kind = data['type']
        self.events.append((request.namespace, data))
        if request.namespace == CONNECTION:
            assert kind in ('CONNECT', 'CLOSE')
            return
        if request.namespace == HEARTBEAT:
            assert kind in ('PING', 'PONG')
            if kind == 'PING':
                self.reply(client, request, {'type': 'PING'})
                self.reply(client, request, {'type': 'PONG'})
            return
        assert isinstance(data['requestId'], int) and data['requestId'] > 0
        rid = data['requestId']
        if request.namespace == RECEIVER:
            assert request.destination_id == 'receiver-0'
            if kind == 'LAUNCH':
                assert data['appId'] == APP
                self.launched = True
            else:
                assert kind == 'GET_STATUS'
            apps = [{'appId': APP, 'transportId': 'transport-fixture', 'sessionId': 'app-fixture',
                     'namespaces': [{'name': MEDIA}] if self.namespaces else []}] if self.launched else []
            self.reply(client, request, {'type': 'RECEIVER_STATUS', 'requestId': rid, 'status': {'applications': apps}})
            return
        assert request.namespace == MEDIA and self.launched and request.destination_id == 'transport-fixture'
        if kind == 'LOAD':
            resource = data['media']
            assert resource['contentType'] == 'audio/mp4' and resource['streamType'] == 'BUFFERED'
            assert resource['customData']['tube360'] and isinstance(data['autoplay'], bool)
            if self.consume:
                self.consume(resource['contentId'])
            self.resource = resource
            self.session += 1
            self.position = data['currentTime']
            self.stamp = time.monotonic()
            self.state = 'PLAYING' if data['autoplay'] else 'PAUSED'
        elif kind in ('SEEK', 'PLAY', 'PAUSE', 'STOP'):
            assert data['mediaSessionId'] == self.session
            self.position = data.get('currentTime', self.at())
            self.stamp = time.monotonic()
            if kind == 'STOP':
                self.state = 'IDLE'
            elif kind == 'SEEK':
                assert data['resumeState'] in ('PLAYBACK_START', 'PLAYBACK_PAUSE')
                self.state = 'PLAYING' if data['resumeState'] == 'PLAYBACK_START' else 'PAUSED'
            else:
                self.state = 'PLAYING' if kind == 'PLAY' else 'PAUSED'
        else:
            assert kind == 'GET_STATUS'
        self.reply(client, request, {'type': 'MEDIA_STATUS', 'requestId': rid, 'status': self.status()})

    def run(self):
        while not self.quit:
            try:
                raw, _ = self.listener.accept()
            except socket.timeout:
                continue
            try:
                if not self.online:
                    raw.close()
                    continue
                with self.context.wrap_socket(raw, server_side=True) as client:
                    self.clients.append(client)
                    client.settimeout(.1)
                    pending = b''
                    while not self.quit and self.online:
                        try:
                            block = client.recv(65536)
                        except socket.timeout:
                            continue
                        if not block:
                            break
                        pending += block
                        while len(pending) >= 4:
                            length = struct.unpack('!I', pending[:4])[0]
                            assert 0 < length <= 65536
                            if len(pending) < length+4:
                                break
                            message = CastMessage.FromString(pending[4:length+4])
                            pending = pending[length+4:]
                            with self.lock:
                                self.handle(client, message)
            except (OSError, ssl.SSLError):
                pass
            except Exception as error:
                self.errors.append(repr(error))
            finally:
                self.clients = [c for c in self.clients if c.fileno() >= 0]
                raw.close()

    def commands(self, kind):
        return [data for ns, data in self.events if ns == MEDIA and data['type'] == kind]


class Driver:
    def __init__(self, binary, directory):
        self.process = subprocess.Popen([binary, str(directory)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=lambda: [self.lines.put(line) for line in self.process.stdout], daemon=True)
        self.reader.start()

    def command(self, command):
        self.process.stdin.write(command+'\n')
        self.process.stdin.flush()
        return json.loads(self.lines.get(timeout=6))

    def until(self, predicate, timeout=7):
        deadline = time.monotonic()+timeout
        sample = None
        while time.monotonic() < deadline:
            sample = self.command('sample')
            if predicate(sample):
                return sample
            time.sleep(.03)
        raise AssertionError(f'status deadline: {sample}')

    def close(self, crash=False):
        if self.process.poll() is None:
            if crash:
                self.process.kill()
            else:
                self.process.stdin.write('quit\n')
                self.process.stdin.flush()
            self.process.wait(8)
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()


class CastTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='tube360-cast-')
        cls.root = pathlib.Path(cls.directory.name)
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=cast-fixture', '-keyout', str(cls.root/'key.pem'),
                        '-out', str(cls.root/'cert.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def setUp(self):
        self.receiver = Receiver(self.root/'cert.pem', self.root/'key.pem')
        self.assets = pathlib.Path(tempfile.mkdtemp(dir=self.root))
        (self.assets/'nest.txt').write_text(f'127.0.0.1 {self.receiver.port}\nCast de bancada\n')
        self.driver = Driver(sys.argv[1], self.assets)

    def tearDown(self):
        self.driver.close()
        self.receiver.close()
        self.assertEqual(self.receiver.errors, [])

    def play(self, position=12, autoplay=1):
        self.driver.command(f'play https://audio.example/itag/140 {position} {autoplay}')
        return self.driver.until(lambda s: s['state'] == (2 if autoplay else 3) and s['updated'] > 0)

    def test_transport_pause_seek_and_stop(self):
        self.play()
        self.driver.command('pause')
        paused = self.driver.until(lambda s: s['state'] == 3)
        time.sleep(.3)
        self.assertAlmostEqual(self.driver.command('sample')['position'], paused['position'], delta=.03)
        self.driver.command('resume')
        self.driver.until(lambda s: s['state'] == 2)
        self.play(70)
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        self.assertEqual(len(self.receiver.commands('SEEK')), 1)
        self.assertGreaterEqual(self.driver.command('sample')['position'], 70)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])
        self.assertTrue(self.receiver.commands('STOP'))
        self.assertFalse((self.assets/'nest-owner.json').exists())

    def test_sparse_status_preserves_observation_time(self):
        sample = self.play()
        self.receiver.replies = False
        time.sleep(2)
        later = self.driver.command('sample')
        self.assertEqual(later['updated'], sample['updated'])
        self.assertEqual(later['state'], 2)
        self.assertFalse(later['quiet'])
        self.receiver.replies = True
        self.driver.until(lambda s: s['updated'] > sample['updated'])

    def test_quiet_needs_fresh_reply(self):
        self.play()
        self.receiver.replies = False
        self.driver.command('stop')
        time.sleep(.8)
        self.assertFalse(self.driver.command('sample')['quiet'])
        self.receiver.replies = True
        self.driver.until(lambda s: s['quiet'])

    def test_stop_survives_partition(self):
        self.play()
        self.receiver.online = False
        self.receiver.disconnect()
        self.driver.until(lambda s: not s['connected'])
        self.driver.command('stop')
        time.sleep(.8)
        self.assertFalse(self.driver.command('sample')['quiet'])
        self.assertTrue((self.assets/'nest-owner.json').exists())
        self.receiver.online = True
        self.driver.until(lambda s: s['quiet'])
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        self.assertTrue(self.receiver.commands('STOP'))

    def test_stop_after_process_restart(self):
        self.play()
        self.driver.close(crash=True)
        self.driver = Driver(sys.argv[1], self.assets)
        self.driver.until(lambda s: s['quiet'] and not (self.assets/'nest-owner.json').exists())
        self.assertTrue(self.receiver.commands('STOP'))
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)

    def test_preload_waits_for_play(self):
        sample = self.play(33, 0)
        self.assertEqual(sample['state'], 3)
        time.sleep(.3)
        self.assertAlmostEqual(self.driver.command('sample')['position'], 33, delta=.03)
        self.driver.command('resume')
        self.driver.until(lambda s: s['state'] == 2)

    def test_replacing_resource_stops_owned_media_first(self):
        self.play()
        self.driver.command('play https://audio.example/new/itag/140 2 1')
        self.driver.until(lambda s: s['state'] == 2 and s['position'] < 10)
        commands = [data['type'] for ns, data in self.receiver.events if ns == MEDIA and data['type'] in ('LOAD', 'STOP')]
        self.assertEqual(commands[:3], ['LOAD', 'STOP', 'LOAD'])

    def test_reconnect_waits_for_media_namespace(self):
        self.play()
        self.receiver.namespaces = False
        self.receiver.disconnect()
        time.sleep(2)
        self.assertFalse(self.driver.command('sample')['quiet'])
        self.assertEqual(sum(data['type'] == 'LAUNCH' for ns, data in self.receiver.events), 1)
        self.receiver.namespaces = True
        self.driver.until(lambda s: s['connected'] and s['state'] == 2)
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)

    def test_oversized_frame_reconnects_without_reloading(self):
        self.play()
        self.receiver.bad_frame = True
        stamp = self.driver.command('sample')['updated']
        self.driver.until(lambda s: s['updated'] > stamp+700)
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        self.assertTrue(any(ns == HEARTBEAT and data['type'] == 'PONG' for ns, data in self.receiver.events))

    def test_nonadvancing_playing_clock_stalls(self):
        self.play()
        frozen = self.receiver.at()
        self.receiver.at = lambda: frozen
        self.driver.until(lambda s: s['state'] == 7, timeout=10)
        self.assertFalse(self.driver.command('sample')['quiet'])
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])

    def test_nonadvancing_buffering_clock_stalls(self):
        self.play()
        frozen = self.receiver.at()
        self.receiver.at = lambda: frozen
        self.receiver.state = 'BUFFERING'
        self.driver.until(lambda s: s['state'] == 7, timeout=10)

    def test_new_seek_ignores_old_clock_reply(self):
        self.play()
        old_id = self.receiver.commands('LOAD')[0]['requestId']
        original = self.receiver.reply
        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS' and self.receiver.commands('SEEK'):
                stale = dict(payload, requestId=old_id)
                stale['status'] = [dict(self.receiver.status()[0], currentTime=1)]
                original(client, request, stale, source)
            original(client, request, payload, source)
        self.receiver.reply = reply
        self.play(80)
        for _ in range(20):
            self.assertGreaterEqual(self.driver.command('sample')['position'], 80)
            time.sleep(.03)

    def test_content_id_identifies_receiver_without_custom_data(self):
        original = self.receiver.reply
        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                payload = dict(payload, status=[dict(item, media={key: value for key, value in item['media'].items()
                                                                 if key != 'customData'}) for item in payload['status']])
            original(client, request, payload, source)
        self.receiver.reply = reply
        self.play()
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])

    def test_incomplete_status_does_not_confirm_quiet(self):
        self.play()
        original = self.receiver.reply
        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                payload = dict(payload, status=[{key: value for key, value in item.items() if key != 'playerState'}
                                               for item in payload['status']])
            original(client, request, payload, source)
        self.receiver.reply = reply
        self.driver.command('stop')
        time.sleep(.8)
        self.assertFalse(self.driver.command('sample')['quiet'])
        self.receiver.reply = original
        self.driver.until(lambda s: s['quiet'])

    def test_load_requires_owner_journal(self):
        (self.assets/'nest-owner.json.tmp').mkdir()
        self.driver.command('play https://audio.example/itag/140 0 1')
        sample = self.driver.until(lambda s: s['state'] == 6)
        self.assertTrue(sample['quiet'])
        self.assertFalse(self.receiver.commands('LOAD'))

    def test_recovery_stops_original_endpoint_after_config_change(self):
        self.play()
        self.driver.close(crash=True)
        other = Receiver(self.root/'cert.pem', self.root/'key.pem')
        try:
            (self.assets/'nest.txt').write_text(f'127.0.0.1 {other.port}\nOutro Nest\n')
            self.driver = Driver(sys.argv[1], self.assets)
            self.driver.until(lambda s: s['quiet'] and not (self.assets/'nest-owner.json').exists())
            self.assertTrue(self.receiver.commands('STOP'))
            self.play()
            self.assertEqual(len(other.commands('LOAD')), 1)
            self.driver.command('stop')
            self.driver.until(lambda s: s['quiet'])
        finally:
            other.close()

    def test_recovery_stops_endpoint_after_config_removal(self):
        self.play()
        self.driver.close(crash=True)
        (self.assets/'nest.txt').unlink()
        self.driver = Driver(sys.argv[1], self.assets)
        self.driver.until(lambda s: s['quiet'] and not (self.assets/'nest-owner.json').exists())
        self.assertTrue(self.receiver.commands('STOP'))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
