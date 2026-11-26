"""Framing hostil dentro do teto HTTP, usando sockets locais reais."""
import socket
import unittest

import test_receiver as fixture

FakeGitHub, r = fixture.FakeGitHub, fixture.r


class FramingTests(unittest.TestCase):
    setUp = fixture.ReceiverTests.setUp
    tearDown = fixture.ReceiverTests.tearDown
    start = fixture.ReceiverTests.start
    post = fixture.ReceiverTests.post
    emit = fixture.ReceiverTests.emit
    close_day = fixture.ReceiverTests.close_day

    def wire(self, headers, body=b'', path='/api/report'):
        with socket.create_connection(('127.0.0.1', self.http.server_port), timeout=3) as sock:
            sock.sendall(b'POST ' + path.encode() + b' HTTP/1.1\r\nHost: localhost\r\n' + headers + b'\r\n' + body)
            sock.shutdown(socket.SHUT_WR)
            result = b''
            while chunk := sock.recv(65536):
                result += chunk
        self.assertTrue(result.startswith(b'HTTP/1.1 '), repr(result))
        return int(result.split(b' ', 2)[1])

    def test_deep_json_is_400(self):
        self.assertEqual(self.post(raw=b'[' * 30000 + b'0' + b']' * 30000)[0], 400)

    def test_huge_length_is_413(self):
        self.assertEqual(self.wire(b'Content-Length: ' + b'9' * 5000 + b'\r\n'), 413)

    def test_non_ascii_length_is_400(self):
        self.assertEqual(self.wire(b'Content-Length: \xb2\r\n'), 400)

