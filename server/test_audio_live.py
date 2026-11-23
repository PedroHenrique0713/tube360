"""Rotas do player com o receptor Cast falso. Nenhum teste abre conexão com o alto-falante da casa."""
import asyncio
import importlib.util
import os
from datetime import datetime, timezone
from pathlib import Path
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from starlette.requests import Request
from test_nest import FakeCast


class AudioTests(unittest.IsolatedAsyncioTestCase):
    vid = "F1beepVOD01"

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        with patch.dict(os.environ, HOME=tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL="http://127.0.0.1:1"):
            spec = importlib.util.spec_from_file_location('audio_test_server', Path(__file__).with_name('tube360_server.py'))
            self.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.m)
        self.n = self.m.nest
        self.m.AUDIO_DIR.mkdir(parents=True)
        self.path = self.m.AUDIO_DIR / f"{self.vid}_0.00.mp3"

    async def get_audio(self, byte_range, query=b""):
        req = Request({"type": "http", "query_string": query, "headers": [(b"range", byte_range)]})
        return await self.m.audio(req, self.vid, 0.0)

    async def test_range_aberta_recebe_tambem_a_parte_ainda_nao_convertida(self):
        job = SimpleNamespace(returncode=None)
        partial = self.path.with_suffix(".part.mp3")
        partial.write_bytes(b'a' * 4096)
        with patch.object(self.m, 'audio_file', AsyncMock(return_value=(partial, job))):
            res = await self.get_audio(b'bytes=0-')
            chunks = res.body_iterator
            first = await anext(chunks)
            with partial.open('ab') as out:
                out.write(b'b' * 2048)
            job.returncode = 0
            self.assertEqual(first + b''.join([c async for c in chunks]), b'a' * 4096 + b'b' * 2048)
            self.assertNotIn('content-length', res.headers)

    async def test_intervalo_em_cache_tem_total_e_comprimento_exatos(self):
        self.path.write_bytes(bytes(range(256)))
        res = await self.get_audio(b'bytes=53-72')
        self.assertEqual(res.status_code, 206)
        self.assertEqual(res.headers['content-range'], 'bytes 53-72/256')
        self.assertEqual(b''.join([c async for c in res.body_iterator]), bytes(range(53, 73)))
        self.assertEqual(res.headers.get('content-length'), '20')

    async def test_url_antiga_depois_de_parar_nao_entrega_audio(self):
        self.path.write_bytes(b'a' * 1024)
        self.n.token = 's=1234'
        self.n.command('stop')
        with self.assertRaises(self.m.HTTPException) as err:
            await self.get_audio(b'bytes=0-', b's=1234')
        self.assertEqual(err.exception.status_code, 410)

    async def test_posicao_antiga_nao_continua_avancando_sem_resposta(self):
        self.n.state, self.n.pos, self.n.t0 = 'PLAYING', 5.0, 10.0
        self.n.stamp = time.monotonic() - 4
        state, pos = self.n.position()
        self.assertEqual((state, pos), ('STALE', 15.0))

    async def test_seek_no_mesmo_receptor_para_10_e_60_segundos(self):
        with self.path.open('wb') as f:
            f.truncate(150 * 24000)
        cc = FakeCast()
        self.n.cc, self.n.ours = cc, True
        mc = cc.media_controller
        def seek(pos, **kw):
            mc.calls.append(('seek', pos))
            mc.status.current_time = pos
            mc.status.last_updated = datetime.now(timezone.utc)
        def play(url, mime, **kw):
            mc.calls.append(('load', kw))
            mc.status.content_id, mc.status.media_session_id = url, 42
            mc.status.player_state = 'PLAYING'
            seek(kw.get('current_time', 0))
        mc.seek, mc.play_media = seek, play
        self.m.remote.playing = self.vid
        self.m.CONFIG['nest'] = {'host': '127.0.0.1'}
        with patch.object(self.m, 'resolve_video', AsyncMock(return_value={'duration': 150, 'live': False})):
            for t in (0, 10, 70):
                self.m.remote.last_poll = time.monotonic()
                await self.m.nest_play(self.vid, t)
                for _ in range(60):
                    if self.n.state == 'PLAYING' and abs(self.n.position()[1] - t) < 1:
                        break
                    await asyncio.sleep(.04)
                self.assertEqual(self.n.state, 'PLAYING')
            self.assertEqual([c[1] for c in mc.calls if isinstance(c, tuple) and c[0] == 'seek'], [0, 10, 70])
            loads = [c[1] for c in mc.calls if isinstance(c, tuple) and c[0] == 'load']
            self.assertEqual(len(loads), 1)
            self.assertEqual(loads[0].get('stream_type'), 'BUFFERED')
            self.assertEqual(loads[0]['media_info']['duration'], 150)
        self.n.command('stop')

    async def test_stop_repete_se_o_socket_voltou_antes_do_canal(self):
        cc = FakeCast()
        self.n.cc, self.n.ours = cc, True
        cc.media_controller.status.media_session_id = 42
        self.n.token = "s=42"
        cc.socket_client.app_namespaces = []
        self.n.command('stop')
        self.assertTrue(self.n.want_stop)
        cc.socket_client.app_namespaces = [cc.media_controller.namespace]
        for _ in range(25):
            if 'stop' in cc.media_controller.calls:
                break
            await asyncio.sleep(.1)
        self.assertIn('stop', cc.media_controller.calls)
        self.assertEqual(cc.launches, 0)

    async def test_reabertura_da_live_nao_exige_segunda_extracao_ou_dois_leitores(self):
        video = dict(url="https://example.org/video.m3u8", vcodec="avc1", acodec="none")
        audio = dict(url="https://example.org/audio.m3u8", vcodec="none", acodec="aac")
        muxed = dict(video, url="https://example.org/av.m3u8", height=720, protocol="m3u8_native", acodec="aac")
        data = dict(live_status="is_live", formats=[video, audio, muxed], requested_formats=[video, audio])
        extract = AsyncMock(side_effect=[data, RuntimeError("segunda extração")])
        with patch.object(self.m, "extract_video", extract), patch.object(self.m, "source_refused", return_value=None):
            info = await self.m.resolve_video(self.vid)
        self.assertEqual(info["video"]["url"], muxed["url"])
        self.assertEqual(info["audio"]["url"], muxed["url"])
        self.assertEqual(extract.await_count, 1)

    async def test_retomada_de_audio_ao_vivo_abre_no_quadro_mp3_pedido(self):
        import subprocess
        import shutil
        path = self.m.AUDIO_DIR / "live-test.part.mp3"
        subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-f", "lavfi", "-i", "sine=frequency=1000",
                        "-t", "3", "-c:a", "libmp3lame", "-b:a", "192k", "-ac", "2",
                        "-write_xing", "0", "-id3v2_version", "0", str(path)], check=True)
        self.m.live_audio[self.vid] = path, SimpleNamespace(returncode=None)
        self.n.token = "s=42"
        req = Request({"type": "http", "query_string": b"s=42", "headers": [(b"range", b"bytes=0-625")]})
        res = await self.m.audio(req, self.vid, 1.0)
        whole = path.read_bytes()
        frame = 0
        for _ in range(round(44100 / 1152)):
            self.assertEqual(whole[frame:frame+2], b"\xff\xfb")
            frame += 626 + ((whole[frame+2] >> 1) & 1)
        self.assertNotEqual(whole[:626], whole[frame:frame+626])
        self.assertEqual(b"".join([b async for b in res.body_iterator]), whole[frame:frame+626])

    async def test_stop_antigo_nao_para_sessao_nova(self):
        self.n.sid, self.n.cc, self.n.active = 2, FakeCast(), True
        self.n.cc.media_controller.status.media_session_id = 71
        scope = {"type": "http", "http_version": "1.1", "scheme": "http", "method": "GET",
                 "headers": [], "query_string": b"sid=1", "path": "/api/nest/stop"}
        send = AsyncMock()
        await self.m.app(scope, AsyncMock(return_value={"type": "http.request", "body": b""}), send)
        self.assertEqual(send.await_args_list[0].args[0]["status"], 200)
        await asyncio.sleep(.1)
        self.assertNotIn("stop", self.n.cc.media_controller.calls)
        self.assertTrue(self.n.active)

    async def test_head_do_mp3_pronto_tem_tamanho_exato_sem_corpo(self):
        self.path.write_bytes(b'a' * 10000)
        self.n.token = "s=42"
        req = Request({"type": "http", "method": "HEAD", "query_string": b"s=42", "headers": []})
        res = await self.m.audio(req, self.vid, 0.0)
        self.assertEqual(res.headers.get("content-length"), "10000")
        self.assertEqual(res.body, b"")

    async def test_stop_interrompe_um_get_em_andamento(self):
        self.path.write_bytes(b'a' * 120000)
        self.n.token = "s=42"
        req = Request({"type": "http", "query_string": b"s=42", "headers": []})
        res = await self.m.audio(req, self.vid, 0.0)
        chunks = res.body_iterator
        self.assertLess(len(await anext(chunks)), 120000)
        self.n.command("stop")
        self.assertEqual(b"".join([b async for b in chunks]), b"")
        self.assertNotIn("content-length", res.headers)
