"""Idioma espanhol no servidor de reserva; rede, conta e HOME isolados."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch


class LanguageTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with patch.dict(os.environ, HOME=cls.tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1'):
            spec = importlib.util.spec_from_file_location('language_test_server', Path(__file__).with_name('tube360_server.py'))
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    async def test_spanish_feed_reaches_extractor(self):
        item = dict(id='video000001', duration=60, channel='Canal', views='1200 views', title='Título', pos=0)
        search = AsyncMock(return_value=[item])
        with patch.object(self.m, 'search_videos', search), patch.object(self.m, 'prefetch_later'):
            reply = await self.m.feed(kind='search', q='fútbol', n=24, fresh=0, offset=0, lang='es')
        search.assert_awaited_once_with('fútbol', 100, 'es')
        self.assertIn('Búsqueda: fútbol', reply.body.decode())
        self.assertIn('1.2 mil vistas', reply.body.decode())
        self.assertEqual(self.m.ydl_options(lang='es')['extractor_args']['youtube']['lang'], ['es'])
        self.assertEqual(self.m.ydl_options(lang='es')['http_headers']['Accept-Language'], 'es-MX,es;q=0.9')

    async def test_spanish_metadata_and_validation(self):
        self.assertEqual(self.m.views_text(1, 'es'), '1 vista')
        self.assertEqual(self.m.views_text(2000000, 'es'), '2 M vistas')
        self.assertEqual(self.m.nav_subtitle('1 subscriber · 12 views · 2 videos', 'es'), '1 suscriptor · 12 vistas · 2 videos')
        self.assertEqual(self.m.tr('Português', 'Watch later', 'es'), 'Ver más tarde')
        reply = await self.m.feed(kind='search', q='', n=24, fresh=0, offset=0, lang='es')
        self.assertIn('Ingresa una búsqueda válida.', reply.body.decode())
