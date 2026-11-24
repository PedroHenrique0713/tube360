import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch
from membership import membership

FIXTURES = Path(__file__).resolve().parents[1] / 'bench/catalog/fixtures'


class MembershipTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        with patch.dict(os.environ, HOME=tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1', TUBE360_WARM_CACHE='0'):
            spec = importlib.util.spec_from_file_location('membership_server', Path(__file__).with_name('tube360_server.py'))
            self.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.m)

    def test_recordings_and_variants(self):
        for lang in ('pt', 'en'):
            node = json.loads((FIXTURES / f'members-{lang}.json').read_text())['contents'][0]['lockupViewModel']
            self.assertEqual(membership(node), 'members')
        for case in json.loads((FIXTURES / 'members-cases.json').read_text())['cases']:
            with self.subTest(case['name']):
                self.assertEqual(membership(case['node']), case['expected'])
        self.assertEqual(self.m.normalize(dict(id='member00001', duration=600, availability='subscriber_only'))['membership'], 'members')
        self.assertEqual(self.m.normalize(dict(id='public00001', duration=600))['membership'], '')

    async def test_account_feeds_and_search_wire(self):
        entries = [dict(id='member00001', duration=600, title='Exclusivo', channel='Canal', membership='members'),
                   dict(id='member00002', duration=600, title='Acesso', channel='Canal', membership='member')]
        status = dict(state='signed_in', name='Fixture', handle='fixture')
        with patch.object(self.m, 'account_status', AsyncMock(return_value=status)), \
             patch.object(self.m, 'account_request', AsyncMock(return_value=dict(items=entries, more=False))), \
             patch.object(self.m, 'prefetch_later'), \
             patch.object(self.m, 'search_videos', AsyncMock(return_value=[dict(e, views='') for e in entries])):
            for kind in ('home', 'subs', 'history', 'later', 'search'):
                reply = await self.m.feed(kind, 'fixture', 24, 0, 0, 'pt')
                fields = [line.split('\t') for line in reply.body.decode().splitlines()[1:]]
                self.assertEqual([f[6] for f in fields], ['members', 'member'], kind)
                self.assertTrue(all(len(f) == 7 for f in fields))

    async def test_navigation_preserves_state_after_account_normalization(self):
        entry = dict(type='video', id='member00001', duration=600, title='Exclusivo', membership='member')
        with patch.object(self.m, 'account_status', AsyncMock(return_value=dict(state='signed_in'))), \
             patch.object(self.m, 'account_request', AsyncMock(return_value=dict(items=[entry], more=False))):
            for kind in ('channel', 'subscriptions', 'library', 'music', 'music_search'):
                data = await self.m.nav_data(kind, '', '', 'videos', 0, 24)
                reply = self.m.nav_response(data)
                self.assertEqual(reply.body.decode().splitlines()[1].split('\t')[8], 'member', kind)

    async def test_local_history_remembers_restriction_without_persisting_access(self):
        self.m.metadata['member00001'] = dict(title='Membro', membership='member', duration=600)
        self.m.history.touch('member00001', 30, 600)
        self.assertEqual(self.m.history.recent(1)[0]['membership'], 'members')
        with patch.object(self.m, 'account_status', AsyncMock(return_value=dict(state='signed_out'))), \
             patch.object(self.m, 'prefetch_later'):
            reply = await self.m.feed('history', '', 24, 0, 0, 'pt')
        self.assertEqual(reply.body.decode().splitlines()[1].split('\t')[6], 'members')
