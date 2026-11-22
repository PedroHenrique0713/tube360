"""Timer isolado; nenhum teste pode chamar o desligamento real."""
import asyncio
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch


class SleepTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        with patch.dict(os.environ, HOME=cls.tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_SLEEP_TEST='1', TUBE360_SLEEP_TEST_DELAY='0.01', TUBE360_ACCOUNT_URL='http://127.0.0.1:1'):
            spec = importlib.util.spec_from_file_location('sleep_test_server', Path(__file__).with_name('tube360_server.py'))
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.marker = self.root / 'poweroff'
        self.marker.unlink(missing_ok=True)
        self.env = patch.dict(os.environ, TUBE360_SLEEP_TEST='1', TUBE360_SLEEP_TEST_DELAY='0.01',
                              TUBE360_POWEROFF_CMD=f'{sys.executable} -c "from pathlib import Path; Path(\'{self.marker}\').write_text(\'ok\')"')
        self.env.start()
        self.m.remote = self.m.Remote()
        self.m.sleep_timer = self.m.SleepTimer()
        self.stops = []
        self.m.nest = SimpleNamespace(active=False, command=self.stops.append)
        self.timer = self.m.sleep_timer

    async def asyncTearDown(self):
        for task in list(self.m.background):
            task.cancel()
        await asyncio.gather(*self.m.background, return_exceptions=True)
        self.env.stop()

    async def run_clock(self, seconds=0.5):
        task = asyncio.create_task(self.timer.run())
        try:
            await asyncio.sleep(seconds)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_production_duration_and_add_contract(self):
        from starlette.requests import Request
        req = Request({'type': 'http', 'headers': [(b'user-agent', b'Tube360/1.0')]})
        self.timer.test = False
        for value in (1.0, 14.0, 15.5, 241.0, float('nan'), float('inf')):
            with self.assertRaises(self.m.HTTPException):
                await self.m.sleep_set(req, min=value, end=0, what='xbox', target_only=0)
        for value in (15.0, 240.0):
            response = await self.m.sleep_set(req, min=value, end=0, what='xbox', target_only=0)
            self.assertIn(b'ok\ton\t', response.body)
        before = self.timer.remaining()
        await self.m.sleep_add(req, min=15)
        self.assertAlmostEqual(self.timer.remaining(), before + 900, delta=1)

    async def test_xbox_never_powers_off_notebook(self):
        self.timer.set(0.001, False, 'xbox')
        await self.run_clock()
        self.assertFalse(self.marker.exists())
        self.assertTrue(self.timer.poll('', 0, 0, False, ''))
        self.assertFalse(self.timer.poll('', 0, 0, False, ''))

    async def test_disconnected_console_and_notebook(self):
        self.m.remote.last_poll = time.monotonic() - 61
        self.timer.set(0.001, False, 'xbox_notebook')
        await self.run_clock()
        self.assertEqual(self.marker.read_text(), 'ok')

    async def test_cancel_grace_prevents_poweroff(self):
        self.timer.set(15, False, 'xbox_notebook')
        self.timer.expire()
        self.timer.cancel()
        await self.run_clock()
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.timer.shutdown)

    async def test_replacing_timer_prevents_old_poweroff(self):
        self.timer.set(15, False, 'xbox_notebook')
        self.timer.expire()
        self.timer.set(30, False, 'xbox')
        await self.run_clock()
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.timer.shutdown)

    async def test_end_matches_current_video_and_overrides_queue(self):
        self.m.remote.playing = 'timerdemo01'
        self.m.remote.dur = 100
        self.timer.set(None, True, 'xbox')
        self.m.remote.queue.append({'id': 'timerdemo02'})
        self.assertFalse(self.timer.poll('', 0, 100, False, 'timerdemo02'))
        self.assertTrue(self.timer.poll('', 0, 100, False, 'timerdemo01'))
        self.assertFalse(self.m.remote.queue)

    async def test_end_does_not_expire_by_estimate_with_console_present(self):
        self.m.remote.playing = 'timerdemo01'
        self.m.remote.dur = 100
        self.m.remote.last_poll = time.monotonic()
        self.timer.set(None, True, 'xbox')
        self.timer.deadline = time.monotonic() - 1
        await self.run_clock()
        self.assertFalse(self.timer.fired)

    async def test_nest_stops_on_expiry(self):
        self.m.nest.active = True
        self.timer.set(15, False, 'xbox')
        self.timer.expire()
        await asyncio.sleep(0.05)
        self.assertEqual(self.stops, ['stop'])

    async def test_expiry_restores_nest_during_notebook_grace(self):
        levels = []
        speaker = self.m.Nest.__new__(self.m.Nest)
        speaker.lock = threading.Lock()
        speaker.sleep_base = 0.64
        speaker.active = False
        speaker.cc = SimpleNamespace(status=SimpleNamespace(volume_level=0), set_volume=levels.append)
        self.m.nest = speaker
        self.timer.active = self.timer.fired = True
        self.timer.what = 'xbox_notebook'
        task = asyncio.create_task(self.timer.fade_nest())
        try:
            await asyncio.sleep(0.05)
            self.assertEqual(levels, [0.64])
            self.assertIsNone(speaker.sleep_base)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_nest_fade_and_original_volume_restored(self):
        levels = []
        speaker = SimpleNamespace(lock=threading.Lock(), sleep_base=None, active=True,
                                  cc=SimpleNamespace(status=SimpleNamespace(volume_level=0.64), set_volume=levels.append))
        for gain in (0.75, 0.5, 0.25, 1):
            self.m.Nest.sleep_volume(speaker, gain)
        self.assertEqual(levels, [0.48, 0.32, 0.16, 0.64])
        self.assertIsNone(speaker.sleep_base)


if __name__ == '__main__':
    unittest.main()
