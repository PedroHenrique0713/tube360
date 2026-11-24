"""Posse do Cast: preparar não disputa o console, e cancelar LAUNCH não prende a TV."""
import asyncio
import importlib.util
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch


class OwnerTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with patch.dict(os.environ, HOME=cls.tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1'), patch.object(threading.Thread, 'start'):
            source = os.environ.get('TUBE360_OWNER_SERVER', str(Path(__file__).with_name('tube360_server.py')))
            spec = importlib.util.spec_from_file_location('owner_server', source)
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        with patch.object(threading.Thread, 'start'):
            self.m.nest = self.m.Nest()
        self.nest = self.m.nest

    async def test_handoff_disconnects_server_before_console_ack(self):
        self.nest.cc = Mock()
        self.nest.ours = True
        with patch.object(self.nest, 'command'):
            reply = await self.m.nest_owner('console')
        self.assertTrue(reply.body.startswith(b'QUIET\t'))
        self.assertIsNone(self.nest.cc)
        self.assertFalse(self.nest.ours)
        self.assertFalse(self.nest.request_play('abcdefghijk', 0, 40, False, '127.0.0.1', None))
        with patch.object(self.nest, 'connect') as connect:
            self.nest.prepare()
            self.nest.volume(30)
        connect.assert_not_called()
        reply = await self.m.nest_owner('server')
        self.assertTrue(reply.body.startswith(b'ok\t'))
        self.assertEqual(self.nest.owner, 'server')

    async def test_handoff_waits_for_actual_media_silence(self):
        self.nest.may_sound = True
        with patch.object(self.nest, 'command'), patch.object(self.nest, 'confirm_quiet', return_value=False), patch.object(self.nest, 'drop') as drop:
            reply = await self.m.nest_owner('console')
        self.assertTrue(reply.body.startswith(b'WAIT\t'))
        drop.assert_not_called()

    async def test_cancel_launch_without_load_releases_tv_even_when_launch_is_blocked(self):
        self.m.streams.serial = 42
        self.nest.sid = 42
        self.nest.lock.acquire()
        try:
            with patch.object(self.nest, 'command'):
                reply = await asyncio.wait_for(self.m.nest_local(42), .2)
        finally:
            self.nest.lock.release()
        self.assertTrue(reply.body.startswith(b'QUIET\t'))
        self.assertFalse(self.nest.active)

    async def test_console_handoff_cannot_ack_after_another_owner_takes_over(self):
        def change_owner(revision):
            self.nest.owner = 'server'
            return True
        with patch.object(self.nest, 'command'), patch.object(self.nest, 'confirm_quiet', side_effect=change_owner), patch.object(self.nest, 'drop') as drop:
            reply = await self.m.nest_owner('console')
        self.assertTrue(reply.body.startswith(b'WAIT\t'))
        drop.assert_not_called()

    def test_cancel_between_launch_and_load_cannot_send_late_audio(self):
        nest = self.nest
        nest.revision = 1
        cast = Mock()

        class CancelBeforeLoad:
            def __gt__(self, other):
                nest.stop_intent(closing=False)
                return False

        with patch.object(nest, 'connect', return_value=cast), patch.object(nest, 'receiver', return_value=True), \
                patch.object(self.m, 'cached_audio_base', return_value=None):
            nest.play(1, 'abcdefghijk', 0, CancelBeforeLoad(), False)
        cast.media_controller.play_media.assert_not_called()
        self.assertFalse(nest.may_sound)
