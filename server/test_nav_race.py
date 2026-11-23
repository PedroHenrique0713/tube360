"""Parar ou tocar pelo celular cancela a playlist da navegação que ainda está carregando."""
import asyncio
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


class CastRace(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with patch.dict(os.environ, HOME=cls.tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_WARM_CACHE='0', TUBE360_ACCOUNT_URL='http://127.0.0.1:1'):
            spec = importlib.util.spec_from_file_location('nav_race_server', Path(__file__).with_name('tube360_server.py'))
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    async def pending_playlist(self):
        from starlette.requests import Request
        request = Request({'type': 'http', 'headers': [(b'user-agent', b'Tube360/1.0')]})
        self.m.remote = self.m.Remote()
        entered, release = asyncio.Event(), asyncio.Event()

        async def slow(*args, **kwargs):
            entered.set()
            await release.wait()
            return {'items': [dict(type='video', id='aaaaaaaaaaa', title='a')], 'more': False}

        self.m.nav_data = slow
        load = asyncio.create_task(self.m.navigation_play(request, 'playlist', 'PLslow', 0, 'pt'))
        await entered.wait()
        return load, release

    async def test_stop_do_celular_cancela_a_playlist_carregando(self):
        load, release = await self.pending_playlist()
        await self.m.command(self.m.Command(action='stop'))
        self.assertEqual(self.m.remote.poll('', 0, 0, 0, ''), 'stop\n')
        release.set()
        await load
        self.assertEqual(self.m.remote.poll('', 0, 0, 0, ''), 'none\n')

    async def test_play_do_celular_vence_a_playlist_carregando(self):
        load, release = await self.pending_playlist()
        await self.m.command(self.m.Command(action='play', id='bbbbbbbbbbb'))
        first = self.m.remote.poll('', 0, 0, 0, '')
        self.assertTrue(first.startswith('play\tbbbbbbbbbbb'), first)
        release.set()
        await load
        self.assertEqual(self.m.remote.poll('bbbbbbbbbbb', 1, 60, 0, ''), 'none\n')


if __name__ == '__main__':
    unittest.main()
