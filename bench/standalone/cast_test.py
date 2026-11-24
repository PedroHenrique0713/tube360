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
        self.request_delay = self.response_delay = 0
        self.drop_media = False
        self.media_replies = self.dropped = 0
        self.pending_frames = []
        self.launched = False
        self.resource = None
        self.state = 'IDLE'
        self.position = 0
        self.stamp = time.monotonic()
        self.session = 7
        self.volume = .42
        self.muted = False
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
        if payload.get('type') == 'MEDIA_STATUS':
            self.media_replies += 1
            if self.drop_media and self.media_replies % 10 in (0, 1, 2):
                self.dropped += 1
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
        if self.response_delay:
            self.pending_frames.append((time.monotonic()+self.response_delay, client, frame))
        else:
            self.send_frame(client, frame)

    def send_frame(self, client, frame):
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
            elif kind == 'SET_VOLUME':
                assert 0 <= data['volume']['level'] <= 1 and isinstance(data['volume']['muted'], bool)
                self.volume = data['volume']['level']
                self.muted = data['volume']['muted']
            elif kind == 'STOP':
                assert data['sessionId'] == 'app-fixture'
                self.launched = False
                self.resource = None
                self.state = 'IDLE'
            else:
                assert kind == 'GET_STATUS'
            apps = [{'appId': APP, 'transportId': 'transport-fixture', 'sessionId': 'app-fixture',
                     'namespaces': [{'name': MEDIA}] if self.namespaces else []}] if self.launched else []
            self.reply(client, request, {'type': 'RECEIVER_STATUS', 'requestId': rid, 'status': {'applications': apps, 'volume': {'level': self.volume, 'muted': self.muted}}})
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
                    client.settimeout(.01)
                    pending = b''
                    incoming = []
                    while not self.quit and self.online:
                        now = time.monotonic()
                        with self.lock:
                            while incoming and incoming[0][0] <= now:
                                self.handle(client, incoming.pop(0)[1])
                            for due, destination, frame in list(self.pending_frames):
                                if due <= now:
                                    self.pending_frames.remove((due, destination, frame))
                                    self.send_frame(destination, frame)
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
                                if self.request_delay:
                                    incoming.append((time.monotonic()+self.request_delay, message))
                                else:
                                    self.handle(client, message)
            except (OSError, ssl.SSLError):
                pass
            except Exception as error:
                self.errors.append(repr(error))
            finally:
                self.pending_frames.clear()
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

    def test_receiver_pause_resume_without_local_command(self):
        self.play()
        with self.receiver.lock:
            self.receiver.position = self.receiver.at()
            self.receiver.stamp = time.monotonic()
            self.receiver.state = 'PAUSED'
        paused = self.driver.until(lambda s: s['state'] == 3)
        time.sleep(.4)
        self.assertAlmostEqual(self.driver.command('sample')['position'], paused['position'], delta=.03)
        with self.receiver.lock:
            self.receiver.stamp = time.monotonic()
            self.receiver.state = 'PLAYING'
        self.driver.until(lambda s: s['state'] == 2 and s['position'] > paused['position']+.1)
        self.assertFalse(self.receiver.commands('PAUSE') or self.receiver.commands('PLAY'))

    def test_receiver_idle_with_error_can_confirm_release(self):
        self.play()
        self.receiver.state = 'IDLE'
        original = self.receiver.status
        self.receiver.status = lambda: [dict(item, idleReason='ERROR') for item in original()]
        self.driver.until(lambda s: s['state'] == 5)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])
        self.assertFalse((self.assets/'nest-owner.json').exists())

    def test_phone_volume_mute_and_timer_gain_compose(self):
        self.play()
        self.assertEqual(self.receiver.volume, .42)
        self.driver.command('volume 0.35 0')
        self.driver.until(lambda _: abs(self.receiver.volume-.35) < .0001)
        self.driver.command('fade 0.5')
        self.driver.until(lambda _: abs(self.receiver.volume-.175) < .0001)
        self.driver.command('volume 0.35 1')
        self.driver.until(lambda _: self.receiver.muted)
        self.driver.command('volume 0.7 0')
        self.driver.until(lambda _: not self.receiver.muted and abs(self.receiver.volume-.35) < .0001)
        self.driver.command('fade 1')
        self.driver.until(lambda _: abs(self.receiver.volume-.7) < .0001)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])
        self.assertAlmostEqual(self.receiver.volume,.7)

    def test_timer_fade_restores_existing_receiver_volume_and_mute(self):
        self.receiver.muted = True
        self.play()
        self.assertEqual(self.receiver.volume,.42)
        self.driver.command('fade 0.25')
        self.driver.until(lambda _: abs(self.receiver.volume-.105) < .0001)
        self.assertTrue(self.receiver.muted)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'])
        self.assertAlmostEqual(self.receiver.volume,.42)
        self.assertTrue(self.receiver.muted)

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

    def test_wifi_130ms_and_30_percent_status_loss_keeps_clock(self):
        self.receiver.request_delay = self.receiver.response_delay = .065
        self.receiver.drop_media = True
        sample = self.play()
        first = sample['updated']
        errors = []
        until = time.monotonic()+6
        while time.monotonic() < until:
            sample = self.driver.command('sample')
            self.assertEqual(sample['state'], 2)
            self.assertFalse(sample['quiet'])
            predicted = sample['position']+(sample['now']-sample['updated'])/1000
            errors.append(abs(predicted-self.receiver.at()))
            time.sleep(.05)
        self.assertGreater(sample['updated'], first+4000)
        self.assertGreater(self.receiver.dropped, 2)
        self.assertLess(max(errors), .12)
        self.assertEqual(len(self.receiver.commands('LOAD')), 1)
        print(f'Wi-Fi: RTT>=130 ms, dropped={self.receiver.dropped}/{self.receiver.media_replies}, clock error max={max(errors)*1000:.1f} ms')

    def test_very_late_status_is_activity_without_retiming_video(self):
        sample = self.play()
        self.receiver.request_delay = self.receiver.response_delay = .65
        time.sleep(1)
        before = self.driver.command('sample')
        after = self.driver.until(lambda s: s['observed'] > before['observed']+1000, timeout=4)
        self.assertEqual(after['state'], 2)
        self.assertEqual(after['updated'], before['updated'])
        self.assertGreater(after['observed'], after['updated'])

    def test_silence_tolerates_more_than_eight_seconds(self):
        sample = self.play()
        self.receiver.replies = False
        time.sleep(9)
        after = self.driver.command('sample')
        self.assertEqual(after['state'], 2)
        self.assertEqual(after['updated'], sample['updated'])
        self.assertFalse(after['quiet'])
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

    def test_reordered_reply_in_same_generation_cannot_rewind_clock(self):
        first = self.play()
        old_id = self.receiver.commands('LOAD')[0]['requestId']
        self.driver.until(lambda s: s['updated'] > first['updated']+200)
        original = self.receiver.reply

        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                payload = dict(payload, requestId=old_id,
                               status=[dict(item, currentTime=1) for item in payload['status']])
            original(client, request, payload, source)
        self.receiver.reply = reply
        try:
            for _ in range(12):
                self.assertGreaterEqual(self.driver.command('sample')['position'], 12)
                time.sleep(.04)
        finally:
            self.receiver.reply = original

    def test_reordered_empty_status_cannot_release_playing_media(self):
        first = self.play()
        old_id = self.receiver.commands('LOAD')[0]['requestId']
        self.driver.until(lambda s: s['updated'] > first['updated']+200)
        original = self.receiver.reply

        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                payload = dict(payload, requestId=old_id, status=[])
            original(client, request, payload, source)
        self.receiver.reply = reply
        try:
            time.sleep(.6)
            sample = self.driver.command('sample')
            self.assertEqual(sample['state'], 2, 'old empty MEDIA_STATUS released current audio')
            self.assertEqual(sample['updated'], self.driver.command('sample')['updated'])
        finally:
            self.receiver.reply = original

    def test_reordered_receiver_status_cannot_forget_active_transport(self):
        original = self.receiver.reply
        idle = []

        def capture(client, request, payload, source=None):
            if payload.get('type') == 'RECEIVER_STATUS' and not payload['status'].get('applications'):
                idle.append(payload)
            original(client, request, payload, source)
        self.receiver.reply = capture
        self.play()
        self.assertTrue(idle)

        def reply(client, request, payload, source=None):
            if payload.get('type') == 'RECEIVER_STATUS':
                payload = idle[-1]
            original(client, request, payload, source)
        self.receiver.reply = reply
        try:
            time.sleep(2.6)
            before = self.driver.command('sample')
            time.sleep(.7)
            after = self.driver.command('sample')
            self.assertGreater(after['updated'], before['updated'], 'old receiver status erased the active transport')
        finally:
            self.receiver.reply = original

    def test_status_without_time_still_orders_state_changes(self):
        self.play()
        original = self.receiver.reply
        deferred = []

        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                if not deferred:
                    deferred.append(dict(payload, status=[dict(item, currentTime=1)
                                                          for item in payload['status']]))
                    return
                current = [dict(item, playerState='PAUSED') for item in payload['status']]
                for item in current:
                    item.pop('currentTime', None)
                original(client, request, dict(payload, status=current), source)
                payload = deferred[0]
            original(client, request, payload, source)
        self.receiver.reply = reply
        try:
            time.sleep(.7)
            self.assertEqual(self.driver.command('sample')['state'], 3,
                             'old PLAYING overtook a newer PAUSED without currentTime')
        finally:
            self.receiver.reply = original

    def test_reordered_status_cannot_replace_media_session(self):
        first = self.play()
        old_id = self.receiver.commands('LOAD')[0]['requestId']
        self.driver.until(lambda s: s['updated'] > first['updated']+200)
        original = self.receiver.reply

        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS' and payload.get('status') and not self.receiver.commands('STOP'):
                payload = dict(payload, requestId=old_id,
                               status=[dict(item, mediaSessionId=999) for item in payload['status']])
            original(client, request, payload, source)
        self.receiver.reply = reply
        try:
            time.sleep(.6)
            self.driver.command('stop')
            # O receptor externo rejeita STOP para uma sessão que ele nunca abriu.
            self.driver.until(lambda _: bool(self.receiver.commands('STOP')), timeout=2)
            self.assertEqual(self.receiver.commands('STOP')[0]['mediaSessionId'], self.receiver.session)
            self.driver.until(lambda s: s['quiet'], timeout=2)
        finally:
            self.receiver.reply = original

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

    def test_long_pause_does_not_count_as_a_playback_stall(self):
        self.play()
        self.driver.command('pause')
        self.driver.until(lambda s: s['state'] == 3)
        time.sleep(8.2)
        original = self.receiver.at
        frozen = original()
        self.receiver.at = lambda: frozen
        self.driver.command('resume')
        self.driver.until(lambda s: s['state'] == 2, timeout=2)
        time.sleep(.3)
        self.assertEqual(self.driver.command('sample')['state'], 2)
        self.receiver.at = original

    def test_partial_media_status_keeps_the_clock_observation(self):
        self.play()
        original = self.receiver.reply
        def reply(client, request, payload, source=None):
            if payload.get('type') == 'MEDIA_STATUS':
                payload = dict(payload, status=[{'mediaSessionId': item['mediaSessionId']} for item in payload['status']])
            original(client, request, payload, source)
        self.receiver.reply = reply
        time.sleep(.5)
        before = self.driver.command('sample')
        time.sleep(.5)
        after = self.driver.command('sample')
        self.assertEqual(after['state'], 2)
        self.assertEqual(after['updated'], before['updated'])
        self.receiver.reply = original


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
