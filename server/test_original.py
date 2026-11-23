import importlib.util
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import AsyncMock, patch


class OriginalTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        with patch.dict(os.environ, HOME=tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name("config.example.json"))):
            spec = importlib.util.spec_from_file_location("original_test_server", Path(__file__).with_name("tube360_server.py"))
            self.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.m)
        self.video = dict(vcodec="avc1.4d401f", acodec="none", width=1280, height=720,
                          fps=29.97, protocol="https", url="https://cdn.test/video")
        self.audio = dict(acodec="mp4a.40.2", protocol="https", url="https://cdn.test/audio")
        self.info = dict(video=self.video, audio=self.audio, original={720: self.video},
                         live=False, headers={}, duration=90, title="Futebol")

    async def request(self, capability, chunks):
        commands = []
        async def start(ticket, cmd):
            commands.append(cmd)
            return SimpleNamespace(output=SimpleNamespace(get=AsyncMock(side_effect=chunks.pop(0))))
        with patch.object(self.m, "resolve_video", AsyncMock(return_value=self.info)), \
             patch.object(self.m.streams, "start", start), patch.object(self.m.streams, "finish", AsyncMock()):
            req = SimpleNamespace(is_disconnected=AsyncMock(return_value=False),
                                  query_params={"codec": capability})
            res = await self.m.stream(req, "F6football1", 10.25, 720, 1, 0, capability)
            self.assertEqual(b"".join([b async for b in res.body_iterator]), b"media")
        return res, commands

    async def test_opt_in_copies_both_streams_and_seeks_both_inputs(self):
        res, (cmd,) = await self.request("h264", [[b"media", None]])
        self.assertEqual(res.headers["X-Video-Codec"], "h264")
        self.assertEqual(res.media_type, "video/mp2t")
        self.assertIn("copy", cmd)
        self.assertEqual(cmd.count("10.25"), 2)
        self.assertNotIn("-vf", cmd)

    async def test_clients_without_capability_keep_mpeg(self):
        res, (cmd,) = await self.request("", [[b"media", None]])
        self.assertIn("mpeg1video", cmd)
        self.assertEqual(res.media_type, "video/mpeg")

    async def test_remux_failure_retries_converted_with_same_ticket(self):
        res, commands = await self.request("h264", [[None], [b"media", None]])
        self.assertEqual(len(commands), 2)
        self.assertIn("copy", commands[0])
        self.assertIn("mpeg1video", commands[1])
        self.assertEqual(res.headers["X-Video-Codec"], "mpeg1")

    def sources(self, *formats, live=False):
        data = dict(formats=list(formats), live_status="is_live" if live else "not_live")
        return self.m.original_sources(data, self.video, self.audio)

    def test_ineligible_sources(self):
        for changes in [dict(fps=60), dict(fps=None), dict(vcodec="vp9"), dict(vcodec="av01"),
                        dict(dynamic_range="HDR10"), dict(protocol="m3u8_native"), dict(has_drm=True)]:
            with self.subTest(changes=changes), patch.dict(self.video, changes):
                self.assertIsNone(self.sources().get(720))
        self.assertIsNone(self.sources(live=True).get(720))
        self.assertIsNone(self.sources().get(480))
        with patch.dict(self.audio, acodec="opus"):
            self.assertIsNone(self.sources().get(720))

    def test_480_rendition_ignores_hd_transcode_source(self):
        sd = dict(self.video, width=854, height=480)
        hd = dict(self.video, width=1920, height=1080)
        sources = self.sources(sd, hd)
        self.assertIs(sources.get(480), sd)
        self.assertIs(sources.get(720), self.video)
