#!/usr/bin/env python3
"""Falhas de syscalls no receiver real; todo tráfego fica em loopback."""
import http.server
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

from e2e import ROOT, Handler, Lounge, dial, wait_for

WORK = ROOT / '.work/r25/lan'
WORK.mkdir(parents=True, exist_ok=True)


class LanTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=WORK)
        self.folder = Path(self.temp.name)
        # Esta prova não pede mídia; Lounge exige apenas a existência desses arquivos.
        (self.folder/'sample.mp4').write_bytes(b'not requested')
        (self.folder/'thumbnail.jpg').write_bytes(b'not requested')
        self.lounge = Lounge(self.folder)
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 8234), Handler)
        self.server.daemon_threads = True
        self.server.lounge = self.lounge
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.process = None
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(('', 1934))
        self.listener.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                                socket.inet_aton('239.255.255.250')+socket.inet_aton('127.0.0.1'))
        self.listener.settimeout(.1)
        self.address = self.folder/'address'
        self.log_path = WORK/(self._testMethodName+'.log')
        self.log = self.log_path.open('w')

    def tearDown(self):
        try:
            if self.process:
                self.process.communicate('quit\n', timeout=5)
                self.assertEqual(self.process.returncode, 0)
            self.assertFalse(self.lounge.errors, self.lounge.errors)
            self.assertEqual(self.lounge.ranges, 0)
        finally:
            if self.process and self.process.poll() is None:
                self.process.kill()
                self.process.communicate()
            self.listener.close()
            self.log.close()
            self.server.shutdown()
            self.server.server_close()
            self.thread.join()
            self.temp.cleanup()

    def start(self, ip='127.0.0.1', **faults):
        self.address.write_text(ip)
        env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
        env.update(XT_LAN_ADDRESS_FILE=str(self.address), **faults)
        self.process = subprocess.Popen([str(ROOT/'client/build/cast-lan-test'), str(self.folder)],
                                        stdin=subprocess.PIPE, stdout=self.log, stderr=self.log,
                                        text=True, env=env)
        wait_for(lambda: self.lounge.code_count, 'TV code from isolated lounge', 4)

    def notification(self, ip, timeout=3):
        until = time.monotonic()+timeout
        while time.monotonic() < until:
            try:
                packet = self.listener.recvfrom(4096)[0]
            except socket.timeout:
                continue
            if (b'NTS: ssdp:alive' in packet and
                    f'LOCATION: http://{ip}:8134/'.encode() in packet):
                return packet
        self.fail('no NOTIFY for current address '+ip)

    def search(self, ip='127.0.0.1'):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as phone:
            phone.bind(('127.0.0.1', 0))
            phone.settimeout(1)
            phone.sendto(b'M-SEARCH * HTTP/1.1\r\nMAN: "ssdp:discover"\r\nST: ssdp:all\r\n\r\n',
                         ('127.0.0.1', 1934))
            packets = [phone.recvfrom(4096)[0] for _ in range(4)]
        self.assertTrue(all(f'LOCATION: http://{ip}:8134/'.encode() in p for p in packets))

    def test_address_not_ready_then_becomes_available(self):
        self.start('0.0.0.0')
        self.address.write_text('127.0.0.1')
        self.notification('127.0.0.1')
        self.assertNotIn('IP=0.0.0.0', self.log_path.read_text())
        self.search()

    def test_address_changes_after_first_announcement(self):
        self.start()
        self.notification('127.0.0.1')
        self.address.write_text('127.0.0.2')
        self.search('127.0.0.2')
        self.notification('127.0.0.2')

    def test_failed_options_notify_storm_and_repeated_enable_preserve_pairing(self):
        self.start(XT_LAN_FAIL_OPTIONS='1')
        self.notification('127.0.0.1')
        self.search()
        stopped = threading.Event()
        sent = [0]

        def storm():
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as noise:
                while not stopped.is_set():
                    noise.sendto(b'NOTIFY * HTTP/1.1\r\nNT: unrelated\r\nNTS: ssdp:alive\r\n\r\n',
                                 ('127.0.0.1', 1934))
                    sent[0] += 1
                    if sent[0] % 20 == 0:
                        stopped.wait(.001)
        flood = threading.Thread(target=storm)
        flood.start()
        try:
            began = time.monotonic()
            self.assertEqual(dial('GET', '/ssdp/device-desc.xml')[0], 200)
            self.assertEqual(dial('POST', '/apps/YouTube', 'pairingCode=FAKE-PHONE-CODE&theme=cl')[0], 201)
            self.assertLess(time.monotonic()-began, 2)
            time.sleep(.2)
        finally:
            stopped.set()
            flood.join()
        self.assertGreater(sent[0], 100)
        initial = len(list(Path(f'/proc/{self.process.pid}/fd').iterdir()))
        for _ in range(6):
            bye = self.log_path.read_text().count('NOTIFY byebye')
            self.process.stdin.write('off\n')
            self.process.stdin.flush()
            wait_for(lambda: self.log_path.read_text().count('NOTIFY byebye') > bye, 'SSDP closed', 2)
            # Drena anúncios anteriores para não confundir a confirmação de uma nova abertura.
            self.listener.setblocking(False)
            try:
                while self.listener.recvfrom(4096):
                    pass
            except BlockingIOError:
                pass
            self.listener.settimeout(.1)
            self.process.stdin.write('on\n')
            self.process.stdin.flush()
            self.notification('127.0.0.1')
        self.search()
        final = len(list(Path(f'/proc/{self.process.pid}/fd').iterdir()))
        self.assertLessEqual(final, initial+2, (initial, final))
        self.assertEqual(self.lounge.registered, 1)
        print(f'NOTIFY storm={sent[0]} packets; fd before={initial}, after={final}; TV pairing preserved')


if __name__ == '__main__':
    unittest.main(verbosity=2)
