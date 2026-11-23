"""Contrato de CC sem conta, downloads ou cache de produção."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch


class CaptionTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with patch.dict(os.environ, HOME=cls.tmp.name,
                        TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1'):
            spec = importlib.util.spec_from_file_location('caption_test_server', Path(__file__).with_name('tube360_server.py'))
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_vtt_rolling_window_and_real_repetition_after_gap(self):
        raw = '''WEBVTT

00:00:01.000 --> 00:00:03.000 align:start position:0%
<c>Olá &amp; bem-vindos</c>

00:00:02.000 --> 00:00:04.000
Olá &amp; bem-vindos
ao <00:00:03.000>canal

00:00:03.000 --> 00:00:05.000
ao canal

00:00:08.000 --> 00:00:10.000
ao canal
'''
        self.assertEqual(self.m.caption_cues(raw, 'vtt', True),
                         [(1, 2, 'Olá & bem-vindos'), (2, 5, 'ao canal'), (8, 10, 'ao canal')])

    def test_manual_keeps_intentional_repeated_words(self):
        raw = 'WEBVTT\n\n00:00:01.000 --> 00:00:02.000\nnão não\n\n00:00:02.000 --> 00:00:03.000\nnão não\n'
        self.assertEqual([c[2] for c in self.m.caption_cues(raw, 'vtt')], ['não não', 'não não'])

    def test_json3_segments_append_and_control_characters(self):
        raw = json.dumps({'events': [
            {'tStartMs': 1000, 'dDurationMs': 2000, 'segs': [{'utf8': 'Hello '}, {'utf8': '&amp; welcome'}]},
            {'tStartMs': 2000, 'dDurationMs': 2000, 'aAppend': 1, 'segs': [{'utf8': '\nto\tthe channel'}]},
            {'tStartMs': 5000, 'dDurationMs': 0, 'segs': [{'utf8': 'no duration'}]},
            {'tStartMs': 6000, 'dDurationMs': 1000, 'segs': [{'utf8': '\n'}]},
        ]})
        self.assertEqual(self.m.caption_cues(raw, 'json3', True), [(1, 4, 'Hello & welcome to the channel')])

    def test_invalid_and_long_cues(self):
        text = ' '.join('palavra' for _ in range(70))
        raw = json.dumps({'events': [{'tStartMs': 1000, 'dDurationMs': 10000, 'segs': [{'utf8': text}]},
                                     {'tStartMs': -1000, 'dDurationMs': 2000, 'segs': [{'utf8': 'invalid'}]}]})
        cues = self.m.caption_cues(raw, 'json3')
        self.assertEqual(' '.join(c[2] for c in cues), text)
        self.assertTrue(all(len(c[2]) <= 100 and c[1] > c[0] for c in cues))
        self.assertEqual(cues[0][0], 1)
        self.assertEqual(cues[-1][1], 11)

    def test_manual_preferred_and_interface_language(self):
        source = lambda name: [{'ext': 'vtt', 'url': 'https://example.invalid/'+name, 'name': name}]
        tracks = self.m.caption_tracks({'subtitles': {'en': source('English')},
                                        'automatic_captions': {'en': source('duplicate'), 'pt': source('Português')}})
        self.assertEqual(len(tracks), 2)
        self.assertEqual(self.m.caption_select(tracks, 'en')['kind'], 'manual')
        self.assertEqual(self.m.caption_select(tracks, 'pt')['code'], 'pt')
        self.assertEqual(self.m.caption_select(tracks, 'fr')['code'], 'en')
        self.assertIsNone(self.m.caption_select(tracks, 'pt', 'de'))
        tracks.extend([{'code': 'pt-orig', 'kind': 'auto'}])
        self.assertEqual(self.m.caption_select(tracks, 'pt')['code'], 'pt-orig')

    async def test_missing_language_retains_choices_and_metadata(self):
        data = {'title': 'Title\tunsafe\nfield', 'channel': 'Channel', 'upload_date': '20261123',
                'subtitles': {'en': [{'ext': 'vtt', 'url': 'https://example.invalid/cc'}]}}
        with patch.object(self.m, 'caption_info', AsyncMock(return_value=data)), \
             patch.object(self.m, 'caption_download', AsyncMock()) as download:
            response = await self.m.captions('H14bBuluwB8', 'pt', 'de')
        text = response.body.decode()
        self.assertTrue(text.startswith('ok\t\tnone\t0\tlanguage_missing\n'))
        self.assertIn('track\ten\ten\tmanual', text)
        self.assertIn('meta\tTitle unsafe field\tChannel\t2026-11-23\t', text)
        download.assert_not_awaited()

    async def test_live_without_captions_and_failed_download_are_distinct(self):
        with patch.object(self.m, 'caption_info', AsyncMock(return_value={'is_live': True})):
            response = await self.m.captions('H14bBuluwB8', 'en', '')
        self.assertIn(b'none\t1\tlive_unavailable', response.body)
        data = {'subtitles': {'en': [{'ext': 'vtt', 'url': 'https://example.invalid/cc'}]}}
        with patch.object(self.m, 'caption_info', AsyncMock(return_value=data)), \
             patch.object(self.m, 'caption_download', AsyncMock(side_effect=OSError('offline'))):
            response = await self.m.captions('H14bBuluwB8', 'en', '')
        self.assertEqual(response.body, b'erro\tfailed\n')

    async def test_live_timedtext_does_not_pretend_to_use_the_mpeg_epoch(self):
        data = {'is_live': True, 'automatic_captions': {'en': [{'ext': 'json3', 'url': 'https://example.invalid/live'}]}}
        with patch.object(self.m, 'caption_info', AsyncMock(return_value=data)), \
             patch.object(self.m, 'caption_download', AsyncMock()) as download:
            response = await self.m.captions('H14bBuluwB8', 'en', '')
        self.assertIn(b'auto\t1\tlive_clock', response.body)
        self.assertNotIn(b'cue\t', response.body)
        download.assert_not_awaited()

    async def test_route_actual_asgi_validation_and_plaintext(self):
        async def request(path, query=b''):
            messages = []
            async def receive():
                return {'type': 'http.request', 'body': b'', 'more_body': False}
            async def send(message):
                messages.append(message)
            await self.m.app({'type': 'http', 'http_version': '1.0', 'method': 'GET', 'scheme': 'http',
                              'path': path, 'raw_path': path.encode(), 'query_string': query,
                              'headers': [], 'client': ('127.0.0.1', 1), 'server': ('127.0.0.1', 8114)}, receive, send)
            start = next(m for m in messages if m['type'] == 'http.response.start')
            body = b''.join(m.get('body', b'') for m in messages if m['type'] == 'http.response.body')
            return start['status'], dict(start['headers']), body
        with patch.object(self.m, 'caption_info', AsyncMock(return_value={})):
            status, headers, body = await request('/api/captions/H14bBuluwB8', b'lang=pt')
            self.assertEqual(status, 200)
            self.assertTrue(headers[b'content-type'].startswith(b'text/plain'))
            self.assertIn(b'unavailable', body)
            self.assertEqual((await request('/api/captions/invalid'))[0], 400)
            self.assertEqual((await request('/api/captions/H14bBuluwB8', b'lang=fr'))[0], 422)


if __name__ == '__main__':
    unittest.main()
