"""Cast TLS com remetentes simultâneos, LAUNCH demorado e recusa durante outro LAUNCH."""
import json
import socket
import ssl
import struct
import threading
import time
from cast_test import Receiver, CastMessage, CONNECTION, HEARTBEAT, RECEIVER, MEDIA, APP


class OwnerReceiver(Receiver):
    def __init__(self, cert, key):
        self.wire = []
        self.workers = []
        self.launch_delay = .25
        self.pending_sender = None
        self.reject_launch = False
        self.ignore_launch = False
        super().__init__(cert, key)
        self.fragment = False

    def run(self):
        while not self.quit:
            try:
                raw, _ = self.listener.accept()
            except socket.timeout:
                continue
            worker = threading.Thread(target=self.serve, args=(raw,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def serve(self, raw):
        sender = None
        try:
            with self.context.wrap_socket(raw, server_side=True) as client:
                self.clients.append(client)
                client.settimeout(.1)
                pending = b''
                while not self.quit:
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
                        request = CastMessage.FromString(pending[4:length+4])
                        pending = pending[length+4:]
                        sender = request.source_id
                        self.handle(client, request)
        except (OSError, ssl.SSLError):
            pass
        except Exception as error:
            self.errors.append(repr(error))
        finally:
            self.wire.append((time.monotonic(), sender, 'DISCONNECT'))
            self.clients = [c for c in self.clients if c.fileno() >= 0]
            raw.close()

    def close(self):
        super().close()
        for worker in self.workers:
            worker.join(3)

    def handle(self, client, request):
        data = json.loads(request.payload_utf8)
        kind = data['type']
        self.wire.append((time.monotonic(), request.source_id, kind))
        self.events.append((request.namespace, data))
        rid = data.get('requestId', 0)
        if request.namespace == CONNECTION:
            return
        if request.namespace == HEARTBEAT:
            if kind == 'PING':
                self.reply(client, request, {'type': 'PONG'})
            return
        if request.namespace == RECEIVER:
            if kind == 'LAUNCH':
                if self.ignore_launch:
                    return
                with self.lock:
                    rejected = self.reject_launch or self.pending_sender not in (None, request.source_id)
                    if not rejected:
                        self.pending_sender = request.source_id
                if rejected:
                    self.wire.append((time.monotonic(), request.source_id, 'LAUNCH_ERROR'))
                    self.reply(client, request, {'type': 'LAUNCH_ERROR', 'reason': 'CANCELLED', 'requestId': rid})
                    return
                time.sleep(self.launch_delay)
                with self.lock:
                    self.launched = True
                    self.pending_sender = None
            elif kind == 'STOP':
                self.launched = False
                self.resource = None
                self.state = 'IDLE'
            elif kind == 'SET_VOLUME':
                self.volume = data['volume'].get('level', self.volume)
                self.muted = data['volume'].get('muted', self.muted)
            else:
                assert kind == 'GET_STATUS', data
            apps = [{'appId': APP, 'transportId': 'transport-fixture', 'sessionId': 'app-fixture',
                     'displayName': 'Default Media Receiver', 'statusText': '',
                     'namespaces': [{'name': MEDIA}], 'isIdleScreen': False}] if self.launched else []
            self.reply(client, request, {'type': 'RECEIVER_STATUS', 'requestId': rid, 'status': {
                'applications': apps, 'volume': {'level': self.volume, 'muted': self.muted}}})
            return
        assert request.namespace == MEDIA, request.namespace
        if kind == 'LOAD':
            self.resource = data['media']
            self.position = data.get('currentTime', 0)
            self.state = 'PLAYING' if data.get('autoplay', True) else 'PAUSED'
            self.stamp = time.monotonic()
            self.session += 1
            if self.consume:
                self.consume(self.resource['contentId'])
        elif kind in ('PLAY', 'PAUSE', 'STOP', 'SEEK'):
            self.position = data.get('currentTime', self.at())
            self.stamp = time.monotonic()
            if kind == 'STOP':
                self.state = 'IDLE'
            elif kind == 'SEEK':
                self.state = 'PAUSED' if data.get('resumeState') == 'PLAYBACK_PAUSE' else 'PLAYING'
            else:
                self.state = 'PLAYING' if kind == 'PLAY' else 'PAUSED'
        else:
            assert kind == 'GET_STATUS', data
        self.reply(client, request, {'type': 'MEDIA_STATUS', 'requestId': rid, 'status': self.status()})
