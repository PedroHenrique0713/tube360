#!/usr/bin/env python3
"""Dois remetentes reais (C++ e pychromecast) contra o mesmo Cast TLS de bancada."""
import asyncio
from dataclasses import replace
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from cast_test import Driver
from owner_receiver import OwnerReceiver

ROOT = Path(__file__).resolve().parents[2]
BINARY = sys.argv[1]


def wait_for(predicate, timeout=6):
    until = time.monotonic()+timeout
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError('deadline')


class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='tube360-owner-')
        cls.work = Path(cls.tmp.name)
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=cast-fixture', '-keyout', str(cls.work/'key'), '-out', str(cls.work/'cert')],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with patch.dict(os.environ, HOME=str(cls.work), TUBE360_CONFIG=str(ROOT/'server/config.example.json'),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1'), patch.object(threading.Thread, 'start'):
            source = os.environ.get('TUBE360_OWNER_SERVER', str(ROOT/'server/tube360_server.py'))
            spec = importlib.util.spec_from_file_location('wire_server', source)
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.receiver = OwnerReceiver(self.work/'cert', self.work/'key')
        self.assets = Path(tempfile.mkdtemp(dir=self.work))
        (self.assets/'nest.txt').write_text(f'127.0.0.1 {self.receiver.port}\nNest de bancada\n')
        self.driver = Driver(BINARY, self.assets)
        with patch.object(threading.Thread, 'start'):
            self.m.nest = self.m.Nest()
        self.nest = self.m.nest
        self.nest.owner_file = self.assets/'server-owner.json'
        self.m.CONFIG['nest'] = {'host': '127.0.0.1'}
        original = self.m.pychromecast.get_chromecast_from_cast_info
        def factory(info, **kwargs):
            info = replace(info, port=self.receiver.port,
                           services={self.m.HostServiceInfo('127.0.0.1', self.receiver.port)})
            return original(info, **kwargs)
        self.factory = patch.object(self.m.pychromecast, 'get_chromecast_from_cast_info', side_effect=factory)
        self.factory.start()

    def tearDown(self):
        self.driver.close()
        self.nest.drop()
        self.factory.stop()
        self.receiver.close()
        self.assertEqual(self.receiver.errors, [])

    def test_release_closes_tls_before_another_sender_can_launch(self):
        self.driver.command('play https://audio.example/itag/140 0 1')
        self.driver.until(lambda s: s['state'] == 2)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'] and not s['connected'], timeout=2)
        wait_for(lambda: any(kind == 'DISCONNECT' and sender and sender.startswith('sender-tube360-')
                             for _, sender, kind in self.receiver.wire), timeout=2)

    def test_rejected_launch_releases_without_load_in_seconds(self):
        self.receiver.reject_launch = True
        began = time.monotonic()
        self.driver.command('play https://audio.example/itag/140 0 1')
        self.driver.until(lambda s: s['state'] == 6, timeout=4.5)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'] and not s['connected'], timeout=1)
        self.assertLess(time.monotonic()-began, 4.5)
        self.assertFalse(self.receiver.commands('LOAD'))

    def test_restart_keeps_ownership_until_journaled_audio_is_quiet_and_closed(self):
        self.driver.command('play https://audio.example/itag/140 0 1')
        self.driver.until(lambda s: s['state'] == 2)
        self.driver.close(crash=True)
        self.receiver.replies = False
        self.driver = Driver(BINARY, self.assets)
        status = self.driver.until(lambda s: s['connected'])
        self.assertFalse(status['released'])
        self.assertFalse(status['quiet'])
        self.receiver.replies = True
        self.driver.until(lambda s: s['quiet'] and s['released'] and not s['connected'])

    def test_control_uncoordinated_senders_reproduce_launch_error(self):
        self.receiver.launch_delay = .8
        self.driver.command('play https://audio.example/itag/140 0 1')
        wait_for(lambda: self.receiver.pending_sender is not None)
        self.nest.prepare()
        errors = [item for item in self.receiver.wire if item[2] == 'LAUNCH_ERROR']
        self.assertTrue(errors, self.receiver.wire)
        launches = {sender for _, sender, kind in self.receiver.wire if kind == 'LAUNCH'}
        self.assertEqual(len(launches), 2)
        print('CONTROL: LAUNCH concorrente de dois remetentes -> LAUNCH_ERROR', flush=True)

    def test_server_console_server_handoff_never_overlaps_senders(self):
        self.nest.prepare()
        self.assertTrue(self.nest.connected())
        async def console_owner():
            for _ in range(60):
                response = await self.m.nest_owner('console')
                if response.body.startswith(b'QUIET\t'):
                    return
                await asyncio.sleep(.03)
            self.fail('server never released')
        asyncio.run(console_owner())
        self.assertIsNone(self.nest.cc)
        self.nest.prepare()  # Um pedido antigo da interface não pode reconectar nem lançar.
        self.assertIsNone(self.nest.cc)
        self.driver.command('play https://audio.example/itag/140 0 1')
        self.driver.until(lambda s: s['state'] == 2)
        self.driver.command('stop')
        self.driver.until(lambda s: s['quiet'] and not s['connected'])
        wait_for(lambda: any(kind == 'DISCONNECT' and sender and sender.startswith('sender-tube360-')
                             for _, sender, kind in self.receiver.wire))
        asyncio.run(self.m.nest_owner('server'))
        self.nest.request_play('abcdefghijk', 0, 40, False, '127.0.0.1', None)
        wait_for(lambda: len(self.receiver.commands('LOAD')) == 2)
        self.assertFalse(any(kind == 'LAUNCH_ERROR' for _, _, kind in self.receiver.wire))
        wire = self.receiver.wire
        first_native = next(t for t, sender, kind in wire if sender and sender.startswith('sender-tube360-') and kind != 'DISCONNECT')
        self.assertTrue(any(t <= first_native and kind == 'DISCONNECT' and sender == 'sender-0' for t, sender, kind in wire), wire)
        print('PASS: server fechou -> console LOAD/STOP/fechou -> server LOAD; zero LAUNCH_ERROR', flush=True)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]], verbosity=2)
