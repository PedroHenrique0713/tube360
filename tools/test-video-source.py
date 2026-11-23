#!/usr/bin/env python3
"""Regressões do seletor de fonte; usa HTTP local, sem cookies ou acesso ao YouTube.

TUBE360_REVIEW_SERVER=/tmp/server.py permite aferir o mesmo teste contra outra revisão.
Execute com HOME e TUBE360_CONFIG isolados, como os testes do servidor.
"""
import asyncio
import importlib.util
import os
import sys
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest.mock import AsyncMock, patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "video_source_test", os.environ.get("TUBE360_REVIEW_SERVER", root / "server/tube360_server.py"))
s = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = s
spec.loader.exec_module(s)


class SourceTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.release = threading.Event()
        cls.requests = []

        class Origin(BaseHTTPRequestHandler):
            def do_GET(self):
                cls.requests.append(self.path)
                if self.path == "/slow":
                    cls.release.wait(9)
                if self.path.startswith("/refused"):
                    self.send_error(403)
                    return
                try:
                    self.send_response(206)
                    self.send_header("Content-Length", "2")
                    self.end_headers()
                    self.wfile.write(b"xx")
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def log_message(self, *args):
                pass

        cls.origin = ThreadingHTTPServer(("127.0.0.1", 8222), Origin)
        cls.thread = threading.Thread(target=cls.origin.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.release.set()
        cls.origin.shutdown()
        cls.origin.server_close()
        cls.thread.join()

    def setUp(self):
        self.requests.clear()
        self.low = self.fmt(720, "/low")
        self.high = self.fmt(1080, "/high")
        self.audio = dict(url="http://127.0.0.1:8222/audio", vcodec="none", acodec="mp4a")

    @staticmethod
    def fmt(height, path):
        return dict(url="http://127.0.0.1:8222" + path, height=height, width=height*16//9,
                    vcodec="avc1.640028", acodec="none", protocol="https", fps=30)

    def data(self, high=None):
        return dict(requested_formats=[self.low, self.audio],
                    formats=[self.low, self.high if high is None else high], title="fixture")

    async def resolve(self, data):
        with patch.object(s, "url_cache", s.TTLCache(30)), patch.object(s, "remember"), \
                patch.object(s, "refused_clients", {}), \
                patch.object(s, "extract_video", AsyncMock(return_value=data)):
            return await s.resolve_video("F2SOURCE001")

    async def test_slow_optional_hd_keeps_fallback_within_four_seconds(self):
        started = time.monotonic()
        info = await self.resolve(self.data(dict(self.high, url="http://127.0.0.1:8222/slow")))
        elapsed = time.monotonic() - started
        self.assertNotIn("video_hd", info)
        self.assertLess(elapsed, 4, f"fonte opcional reteve a reserva por {elapsed:.2f}s")

    async def test_reachable_hd_is_used_only_for_720(self):
        info = await self.resolve(self.data())
        for quality in s.SIZES:
            for audio in (False, True):
                cmd = s.ffmpeg_command(info, 7, quality, with_audio=audio)
                inputs = [cmd[i+1] for i, option in enumerate(cmd) if option == "-i"]
                expected = self.high if quality == 720 else self.low
                self.assertEqual(inputs, [expected["url"]] + ([self.audio["url"]] if audio else []))

    async def test_refused_hd_keeps_working_lower_source(self):
        info = await self.resolve(self.data(dict(self.high, url="http://127.0.0.1:8222/refused")))
        self.assertNotIn("video_hd", info)
        self.assertEqual(info["video"], self.low)

    async def test_non_avc_and_high_fps_do_not_replace_source(self):
        for change in (dict(fps=60), dict(vcodec="av01"), dict(vcodec="vp9"), dict(has_drm=True)):
            with self.subTest(change=change):
                info = await self.resolve(self.data(dict(self.high, **change)))
                self.assertNotIn("video_hd", info)
                self.assertNotIn("/high", self.requests)

    async def test_refused_api_client_is_retried_and_remembered(self):
        first = self.data()
        first["requested_formats"] = [dict(self.low, url="http://127.0.0.1:8222/refused?c=TVHTML5"), self.audio]
        extract = AsyncMock(side_effect=[first, self.data()])
        with patch.object(s, "url_cache", s.TTLCache(30)), patch.object(s, "remember"), \
                patch.object(s, "refused_clients", {}), patch.object(s, "extract_video", extract), \
                patch.object(s, "clients_named", return_value=["tv_samsung"]):
            info = await s.resolve_video("F2SOURCE001")
            self.assertEqual(extract.await_count, 2)
            self.assertEqual(extract.await_args.kwargs["skip_clients"], ["tv_samsung"])
            self.assertIn("tv_samsung", s.clients_to_skip())
            self.assertEqual(info["video"], self.low)

    async def test_live_keeps_joint_audio_and_skips_hd(self):
        joint = dict(self.low, acodec="mp4a", protocol="m3u8_native", live_status="is_live")
        info = await self.resolve(dict(joint, formats=[self.high]))
        for audio in (True, False):
            cmd = s.ffmpeg_command(info, 10, 720, with_audio=audio, mp3=None if audio else "/tmp/f2-unused.mp3")
            self.assertEqual(cmd.count("-i"), 1)
            self.assertNotIn("-ss", cmd)
            self.assertEqual("libmp3lame" in cmd, not audio)
        self.assertNotIn("/high", self.requests)


if __name__ == "__main__":
    unittest.main(verbosity=2)
