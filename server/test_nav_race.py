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
        self.m.sleep_timer = self.m.SleepTimer()
        entered, release = asyncio.Event(), asyncio.Event()

        async def slow(*args, **kwargs):
            entered.set()
            await release.wait()
            return {'items': [dict(type='video', id=letter * 11, title=letter) for letter in 'abc'], 'more': False}

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

    async def test_timer_cancela_a_playlist_carregando(self):
        load, release = await self.pending_playlist()
        self.m.sleep_timer.set(15, False, 'xbox')
        self.m.sleep_timer.expire()
        self.assertEqual(self.m.remote.poll('', 0, 0, 0, ''), 'desligar\n')
        release.set()
        await load
        self.assertEqual(self.m.remote.poll('', 0, 0, 0, ''), 'none\n')

    async def test_play_do_celular_substitui_a_fila_musical_pronta(self):
        load, release = await self.pending_playlist()
        release.set()
        await load
        self.m.remote.poll('', 0, 0, 0, '')
        await self.m.command(self.m.Command(action='play', id='ddddddddddd'))
        self.assertTrue(self.m.remote.poll('aaaaaaaaaaa', 1, 60, 0, '').startswith('play\tddddddddddd'))
        self.assertEqual(self.m.remote.poll('ddddddddddd', 60, 60, 0, 'ddddddddddd'), 'none\n')
        self.assertFalse(self.m.remote.queue)

    async def test_celular_substitui_play_da_navegacao_ainda_nao_entregue(self):
        load, release = await self.pending_playlist()
        release.set()
        await load
        await self.m.command(self.m.Command(action='play', id='ddddddddddd'))
        self.assertTrue(self.m.remote.poll('', 0, 0, 0, '').startswith('play\tddddddddddd'))
        self.assertFalse(self.m.remote.queue)

    async def test_stop_do_celular_descarta_a_fila_pronta(self):
        load, release = await self.pending_playlist()
        release.set()
        await load
        await self.m.command(self.m.Command(action='stop'))
        self.assertEqual(self.m.remote.poll('', 0, 0, 0, ''), 'stop\n')
        self.assertFalse(self.m.remote.queue)

    async def test_fila_da_navegacao_identifica_inicio_e_proximo_natural(self):
        load, release = await self.pending_playlist()
        release.set()
        await load
        self.assertTrue(self.m.remote.poll('', 0, 0, 0, '').endswith('\tnavigation\n'))
        next_line = self.m.remote.poll('aaaaaaaaaaa', 60, 60, 0, 'aaaaaaaaaaa')
        self.assertTrue(next_line.startswith('play\tbbbbbbbbbbb'))
        self.assertTrue(next_line.endswith('\tnavigation\n'))

    async def test_celular_preserva_itens_da_propria_fila(self):
        load, release = await self.pending_playlist()
        release.set()
        await load
        await self.m.command(self.m.Command(action='queue', id='eeeeeeeeeee'))
        await self.m.command(self.m.Command(action='play', id='ddddddddddd'))
        self.m.remote.poll('', 0, 0, 0, '')
        next_line = self.m.remote.poll('ddddddddddd', 60, 60, 0, 'ddddddddddd')
        self.assertTrue(next_line.startswith('play\teeeeeeeeeee'))
        self.assertNotIn('\tnavigation', next_line)


if __name__ == '__main__':
    unittest.main()
