#!/usr/bin/env python3
"""Servidor do Tube360. Contrato: ../docs/PROTOCOL.md."""

import asyncio
import contextlib
import io
import json
import logging
import math
import re
import random
import shutil
import socket
import threading
import uuid
import time
from collections import deque
from contextlib import asynccontextmanager
from itertools import zip_longest
from pathlib import Path
from urllib.parse import quote
from urllib.request import Request as URLRequest, urlopen

import anyio
import uvicorn
import yt_dlp
from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.exceptions import RequestValidationError
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.responses import FileResponse, HTMLResponse, PlainTextResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from PIL import Image, ImageOps
from pydantic import BaseModel, ConfigDict, Field
from typing import Literal

import pychromecast
from pychromecast.models import CastInfo, HostServiceInfo

BASE = Path(__file__).resolve().parent
# config.json é seu (fica fora do Git); sem ele, vale o config.example.json
CONFIG = json.loads(((BASE / "config.json") if (BASE / "config.json").exists() else (BASE / "config.example.json")).read_text(encoding="utf-8"))
THUMBS = Path.home() / ".cache/tube360/thumbs"
FFMPEG = shutil.which("ffmpeg") or "/usr/bin/ffmpeg"
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{11}$")
LOG = logging.getLogger("tube360")
SIZES = {720: (1280, 720, "5500k"), 480: (854, 480, "2500k"), 360: (640, 360, "1500k")}
metadata = {}
extract_slots = asyncio.Semaphore(4)


def clean(value):
    """Um campo do protocolo nunca pode criar outra coluna ou outra linha."""
    return re.sub(r"[\t\r\n\x00-\x1f\x7f]+", " ", str("" if value is None else value)).strip()


def valid_id(video_id):
    if not ID_PATTERN.fullmatch(video_id):
        raise HTTPException(400, "ID de vídeo inválido.")
    return video_id


class QuietYDL:
    # O yt-dlp inclui URLs assinadas em alguns erros: não as registrar.
    def debug(self, message):
        pass

    def warning(self, message):
        pass

    def error(self, message):
        pass


def ydl_options(**extra):
    options = {
        "quiet": True, "no_warnings": True, "logger": QuietYDL(),
        "socket_timeout": 15, "retries": 1, "extractor_retries": 1,
        "skip_download": True, "noplaylist": True,
        # O identificador brasileiro aceito pelo extrator é "pt" (não "pt-BR").
        "extractor_args": {"youtube": {"lang": ["pt"]}},
        "http_headers": {"Accept-Language": "pt-BR,pt;q=0.9"},
        "remote_components": ["ejs:github"],
        "cachedir": str(BASE / ".yt-dlp-cache"),
    }
    if node := shutil.which("node"):
        options["js_runtimes"] = {"node": {"path": node}}
    if browser := CONFIG.get("cookies_from_browser"):
        options["cookiesfrombrowser"] = (str(browser),)
    # cookies.txt exportado (VPS sem navegador, quando o YouTube pede "confirme que não é um robô")
    if cookie_file := CONFIG.get("cookies_file"):
        options["cookiefile"] = str(Path(cookie_file).expanduser())
    options.update(extra)
    return options


async def extract(target, **options):
    def work():
        with yt_dlp.YoutubeDL(ydl_options(**options)) as ydl:
            return ydl.extract_info(target, download=False)
    async with extract_slots:
        return await asyncio.to_thread(work)


class TTLCache:
    """Compartilha uma extração em andamento e limita o crescimento da memória."""
    def __init__(self, ttl, limit=128):
        self.ttl, self.limit = ttl, limit
        self.values, self.pending = {}, {}

    async def get(self, key, factory, stale_ok=False):
        now = time.monotonic()
        if key in self.values and self.values[key][0] > now:
            return self.values[key][1]
        # vencido mas guardado: entrega na hora e atualiza por trás (o console não espera o yt-dlp)
        stale = stale_ok and key in self.values
        if key not in self.pending:
            async def fill():
                try:
                    result = await factory()
                    if len(self.values) >= self.limit:
                        self.values.pop(next(iter(self.values)))
                    self.values[key] = (time.monotonic() + self.ttl, result)
                    return result
                finally:
                    self.pending.pop(key, None)
            self.pending[key] = asyncio.create_task(fill())
        if stale:
            return self.values[key][1]
        return await asyncio.shield(self.pending[key])


home_cache = TTLCache(600, 1)
search_cache = TTLCache(300)
url_cache = TTLCache(1800)
thumb_cache = TTLCache(60, 256)


def views_text(count):
    if count is None:
        return ""
    count = max(0, int(count))
    if count >= 1_000_000:
        return f"{count / 1_000_000:.1f}".replace(".", ",") + " mi de visualizações"
    if count >= 1000:
        return f"{count / 1000:.1f}".replace(".", ",") + " mil visualizações"
    return f"{count} visualizaç{'ão' if count == 1 else 'ões'}"


def remember(item):
    if len(metadata) >= 2048 and item["id"] not in metadata:
        metadata.pop(next(iter(metadata)))
    metadata[item["id"]] = item
    return item


def normalize(entry):
    video_id = entry.get("id", "")
    duration = max(0, int(entry.get("duration") or 0))
    url = entry.get("url") or entry.get("webpage_url") or ""
    if not ID_PATTERN.fullmatch(video_id) or "/shorts/" in url or 0 < duration < 61:
        return None
    return remember({
        "id": video_id, "duration": duration,
        "channel": clean(entry.get("channel") or entry.get("uploader")),
        "views": views_text(entry.get("view_count")),
        "title": clean(entry.get("title") or video_id),
    })


async def search_videos(query, count=100):
    async def load():
        data = await extract(f"ytsearch{count}:{query}", extract_flat="in_playlist")
        items, seen = [], set()
        for entry in data.get("entries", []):
            item = normalize(entry) if entry else None
            if item and item["id"] not in seen:
                seen.add(item["id"])
                items.append(item)
        return items
    return await search_cache.get((query, count), load, stale_ok=True)


home_extra = []  # páginas a mais da home (rolagem sem fim): buscas de temas sorteados, anexadas sem repetir


async def home_more(have):
    """Mais vídeos para a home quando o console chega ao fim: 4 temas sorteados, intercalados, sem repetir id."""
    pool = CONFIG.get("home_queries") or ["música brasileira"]
    queries = random.sample(pool, min(4, len(pool)))
    results = await asyncio.gather(*(search_videos(clean(q), 40) for q in queries), return_exceptions=True)
    groups = [r for r in results if isinstance(r, list)]
    seen = set(have)
    for row in zip_longest(*groups):
        for item in row:
            if item and item["id"] not in seen:
                home_extra.append(item)
                seen.add(item["id"])


async def home_videos(fresh=False):
    if fresh:
        home_cache.values.pop("home", None)
        home_extra.clear()

    async def load():
        if CONFIG.get("cookies_from_browser"):
            data = await extract(":ytrec", extract_flat="in_playlist", playlistend=100)
            groups = [[normalize(e) for e in data.get("entries", []) if e]]
        else:
            pool = CONFIG.get("home_queries") or ["música brasileira", "ciência Brasil", "games Brasil"]
            # a cada carga sorteia parte dos temas: "Atualizar" no console traz vídeos novos de fato
            queries = random.sample(pool, min(len(pool), 6))
            results = await asyncio.gather(
                *(search_videos(clean(q), max(16, math.ceil(100 / len(queries)) * 2)) for q in queries),
                return_exceptions=True,
            )
            groups = [r for r in results if isinstance(r, list)]
        items, seen = [], set()
        for row in zip_longest(*groups):
            for item in row:
                if item and item["id"] not in seen:
                    items.append(item)
                    seen.add(item["id"])
        if not items:
            raise RuntimeError("feed vazio")
        return items
    return await home_cache.get("home", load, stale_ok=True)


async def resolve_video(video_id):
    async def load():
        # A mesma resolução (até 720p) atende todas as qualidades e buscas temporais.
        data = await extract(
            f"https://www.youtube.com/watch?v={video_id}",
            # H.264 antes: abrir AV1 custa o dobro de CPU (720p, medido num núcleo: 49% contra 26%)
            format="bestvideo[height<=720][vcodec^=avc1][protocol=https]+bestaudio[protocol=https]"
                   "/bestvideo[height<=720][protocol=https]+bestaudio[protocol=https]/best[height<=720]/best",
        )
        formats = data.get("requested_formats") or [data]
        video = next((f for f in formats if f.get("vcodec") != "none"), None)
        audio = next((f for f in formats if f.get("acodec") != "none"), None)
        if not video or not audio:
            raise RuntimeError("vídeo ou áudio indisponível")
        result = {
            "video": video, "audio": audio,
            "duration": max(0, int(data.get("duration") or 0)),
            "title": clean(data.get("title") or video_id),
            "headers": data.get("http_headers", {}),
        }
        remember({"id": video_id, "duration": result["duration"], "title": result["title"],
                  "channel": clean(data.get("channel")), "views": views_text(data.get("view_count"))})
        return result
    return await url_cache.get(video_id, load)


def source_args(info, source, start):
    headers = info["headers"] | source.get("http_headers", {})
    header_text = "".join(f"{clean(k)}: {clean(v)}\r\n" for k, v in headers.items())
    return ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "3",
            "-reconnect_on_network_error", "1", "-reconnect_on_http_error", "5xx",
            "-rw_timeout", "15000000", "-headers", header_text, "-ss", str(start), "-i", source["url"]]


def audio_command(info, start):
    """Só o áudio, em MP3, para o Nest (Google Cast). Começa em `start`, com o tempo do fluxo em zero, como o vídeo."""
    return [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error",
            *source_args(info, info["audio"], start), "-map", "0:a:0", "-vn",
            "-af", "aresample=44100,asetpts=PTS-STARTPTS", "-c:a", "libmp3lame", "-b:a", "192k", "-ar", "44100", "-ac", "2",
            "-flush_packets", "1", "-f", "mp3", "pipe:1"]


def ffmpeg_command(info, start, quality, with_audio=True):
    width, height, bitrate = SIZES[quality]
    cmd = [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error"]
    if not with_audio:
        cmd += source_args(info, info["video"], start)
        cmd += ["-map", "0:v:0", "-an",
                "-vf", f"scale={width}:{height}:force_original_aspect_ratio=decrease:force_divisible_by=2,"
                         f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black,setsar=1,fps=30,setpts=PTS-STARTPTS",
                "-c:v", "mpeg1video", "-b:v", bitrate, "-maxrate", bitrate, "-bufsize", "2048k",
                "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-threads", "4",
                "-avoid_negative_ts", "disabled", "-muxdelay", "0.2", "-muxpreload", "0",
                "-preload", "0", "-packetsize", "2048", "-flush_packets", "1", "-f", "mpeg", "pipe:1"]
        return cmd
    separate = info["video"]["url"] != info["audio"]["url"]
    for source in ([info["video"], info["audio"]] if separate else [info["video"]]):
        headers = info["headers"] | source.get("http_headers", {})
        header_text = "".join(f"{clean(k)}: {clean(v)}\r\n" for k, v in headers.items())
        cmd += ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "3",
                "-reconnect_on_network_error", "1", "-reconnect_on_http_error", "5xx",
                "-rw_timeout", "15000000", "-headers", header_text,
                "-ss", str(start), "-i", source["url"]]
    cmd += ["-map", "0:v:0", "-map", "1:a:0" if separate else "0:a:0",
            "-vf", f"scale={width}:{height}:force_original_aspect_ratio=decrease:force_divisible_by=2,"
                     f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black,setsar=1,fps=30,setpts=PTS-STARTPTS",
            # O encoder MP2 tem 481 amostras de atraso; compensar mantém seu PTS inicial em zero.
            "-af", "aresample=44100,asetpts=PTS-STARTPTS+481/44100/TB",
            "-c:v", "mpeg1video", "-b:v", bitrate, "-maxrate", bitrate, "-bufsize", "2048k",
            "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-threads", "4",
            "-c:a", "mp2", "-ar", "44100", "-ac", "2", "-b:a", "192k",
            # Sem a correção automática, o muxer não soma ~0,53 s aos timestamps.
            # DTS negativo do primeiro quadro é normal com B-frames; PTS começa em zero.
            "-avoid_negative_ts", "disabled", "-muxdelay", "0.2", "-muxpreload", "0",
            "-preload", "0", "-packetsize", "2048",
            "-flush_packets", "1", "-f", "mpeg", "pipe:1"]
    return cmd


class Encoder:
    """Um único leitor do stdout; fila pequena aplica pressão de volta ao ffmpeg."""
    def __init__(self, process):
        self.process = process
        self.output = asyncio.Queue(maxsize=8)
        self.lock = asyncio.Lock()
        self.closed = False
        self.stderr_lines = 0
        self.reader = asyncio.create_task(self.pump())
        self.errors = asyncio.create_task(self.read_errors())

    async def pump(self):
        try:
            while chunk := await self.process.stdout.read(65536):
                await self.output.put(chunk)
        finally:
            if not self.closed:
                await self.output.put(None)

    async def read_errors(self):
        while line := await self.process.stderr.readline():
            if b"buffer underflow" not in line:
                # Sem imprimir URLs/tokens nem gerar milhares de linhas por erro.
                self.stderr_lines += 1
                if self.stderr_lines <= 3:
                    LOG.warning("ffmpeg PID %s: %s", self.process.pid,
                                re.sub(r"https?://\S+", "[URL]", line.decode(errors="replace").strip()))

    async def stop(self):
        async with self.lock:
            if self.closed:
                return
            self.closed = True
            if self.process.returncode is None:
                with contextlib.suppress(ProcessLookupError):
                    self.process.terminate()
            self.reader.cancel()
            await asyncio.gather(self.reader, return_exceptions=True)
            # Drenar também após SIGTERM evita wait() preso num pipe cheio.
            async def discard():
                while await self.process.stdout.read(65536):
                    pass
            drain = asyncio.create_task(discard())
            try:
                await asyncio.wait_for(asyncio.shield(self.process.wait()), 2)
            except asyncio.TimeoutError:
                with contextlib.suppress(ProcessLookupError):
                    self.process.kill()
                await self.process.wait()
            await asyncio.gather(drain, self.errors, return_exceptions=True)
            while not self.output.empty():
                self.output.get_nowait()
            self.output.put_nowait(None)


class StreamManager:
    def __init__(self):
        self.lock = asyncio.Lock()
        self.serial = 0
        self.active = None

    async def begin(self):
        async with self.lock:
            self.serial += 1
            if self.active:
                await self.active.stop()
                self.active = None
            return self.serial

    async def start(self, ticket, command):
        async with self.lock:
            if ticket != self.serial:
                raise HTTPException(409, "Outro vídeo foi solicitado.")
            spawn = asyncio.create_task(asyncio.create_subprocess_exec(
                *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE))
            try:
                process = await asyncio.shield(spawn)
            except asyncio.CancelledError:
                process = await spawn
                with anyio.CancelScope(shield=True):
                    await Encoder(process).stop()
                raise
            self.active = Encoder(process)
            return self.active

    async def finish(self, encoder):
        await encoder.stop()
        async with self.lock:
            if self.active is encoder:
                self.active = None


streams = StreamManager()
audio_streams = StreamManager()  # o áudio do Nest tem vaga própria: não derruba o vídeo do console


class Command(BaseModel):
    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)
    action: Literal["play", "queue", "pause", "resume", "seek", "stop", "next"]
    id: str | None = None
    t: float | None = Field(default=None, ge=0, le=86400 * 30)
    delta: float | None = Field(default=None, ge=-86400 * 30, le=86400 * 30)


class History:
    """O que o Xbox assistiu e onde parou (~/.cache/tube360/historico.json), mais recente primeiro. Fica só no
    notebook; não mexe na conta do YouTube."""
    FILE = Path.home() / ".cache/tube360/historico.json"

    def __init__(self):
        try:
            self.items = {e["id"]: e for e in json.loads(self.FILE.read_text())}
        except Exception:
            self.items = {}
        self.saved_at = 0.0

    def touch(self, video_id, pos, dur, finished=False):
        if not video_id or (pos < 3 and not finished):
            return
        meta = metadata.get(video_id, {})
        entry = self.items.pop(video_id, {"id": video_id})
        entry.update(pos=round(dur if finished else pos, 1), dur=round(dur or entry.get("dur", 0) or meta.get("duration", 0)),
                     ts=time.time(), title=meta.get("title") or entry.get("title", video_id),
                     channel=meta.get("channel") or entry.get("channel", ""))
        self.items[video_id] = entry  # reinserido no fim = mais recente
        while len(self.items) > 300:
            self.items.pop(next(iter(self.items)))
        if finished or time.monotonic() - self.saved_at > 10:
            self.save()

    def save(self):
        self.saved_at = time.monotonic()
        temporary = self.FILE.with_suffix(".tmp")
        temporary.write_text(json.dumps(list(self.items.values()), ensure_ascii=False))
        temporary.replace(self.FILE)

    def position(self, video_id):
        e = self.items.get(video_id)
        return e["pos"] if e else 0.0

    def recent(self, n):
        return list(reversed(list(self.items.values())))[:n]


history = History()


class Remote:
    def __init__(self):
        self.commands, self.queue = deque(), deque()
        self.playing, self.title = "", ""
        self.pos, self.dur, self.paused = 0.0, 0.0, False
        self.last_poll, self.last_ended = 0.0, ""
        self.ended_seq = 0  # sobe a cada fim natural de vídeo informado pelo console (o receptor de Cast pede o próximo)

    def item(self, video_id):
        return dict(metadata.get(video_id, {"id": video_id, "title": video_id, "duration": 0,
                                           "channel": "", "views": ""}))

    def state(self):
        return {"playing": self.playing, "pos": self.pos, "dur": self.dur,
                "paused": self.paused, "title": self.title, "queue": list(self.queue),
                "connected": bool(self.last_poll and time.monotonic() - self.last_poll < 5),
                "pending": len(self.commands), "ended": self.last_ended, "ended_seq": self.ended_seq}

    def poll(self, playing, pos, dur, paused, ended):
        self.playing, self.pos, self.dur, self.paused = playing, pos, dur, bool(paused)
        self.title = self.item(playing)["title"] if playing else ""
        self.last_poll = time.monotonic()
        if playing:
            history.touch(playing, pos, dur)
        if ended and ended != self.last_ended:
            history.touch(ended, dur, dur, finished=True)
            self.last_ended = ended
            self.ended_seq += 1
            if self.queue:
                self.commands.append({"action": "next"})
        elif not ended:
            self.last_ended = ""
        if not self.commands:
            return "none\n"
        cmd = self.commands.popleft()
        action = cmd["action"]
        if action == "next":
            if not self.queue:
                return "stop\n"
            item = self.queue.popleft()
            cmd = {"action": "play", "id": item["id"], "t": item.get("t", 0)}
            action = "play"
        if action == "aviso":
            return f"aviso\t{clean(cmd.get('text', ''))}\n"
        if action == "play":
            return f"play\t{cmd['id']}\t{cmd.get('t') or 0:g}\t{clean(self.item(cmd['id'])['title'])}\n"
        if action == "seek":
            delta = cmd.get("delta")
            if delta is None:
                delta = (cmd.get("t") or 0) - self.pos
            return f"seek\t{delta:g}\n"
        return action + "\n"


remote = Remote()


async def prefetch_thumbs(ids, width=320, height=180):
    """Baixa de antemão as miniaturas de uma lista, poucas por vez, para o console achar tudo pronto."""
    slots = asyncio.Semaphore(6)

    async def one(video_id):
        path = THUMBS / f"{video_id}_{width}x{height}.jpg"
        if path.exists() or not ID_PATTERN.fullmatch(video_id):
            return
        async with slots:
            try:
                await thumb_cache.get(path.name, lambda: asyncio.to_thread(make_thumb, video_id, width, height, path))
            except Exception:
                pass
    await asyncio.gather(*(one(i) for i in ids))


def prefetch_later(items):
    task = asyncio.create_task(prefetch_thumbs([i["id"] for i in items]))
    background.add(task)
    task.add_done_callback(background.discard)


background = set()
last_console = [0.0]  # monotonic do último pedido do console; sem console por 30 min, o aquecimento para


async def warm_loop():
    """Ao ligar e a cada 8 min: a tela inicial e as buscas das categorias do console (config.json, warm_queries)
    ficam no cache, com as miniaturas."""
    first = True
    while True:
        if not first and time.monotonic() - last_console[0] > 1800:
            await asyncio.sleep(60)
            continue
        first = False
        try:
            jobs = [home_videos()] + [search_videos(clean(q)) for q in CONFIG.get("warm_queries", [])]
            for result in await asyncio.gather(*jobs, return_exceptions=True):
                if isinstance(result, list):
                    await prefetch_thumbs([i["id"] for i in result[:32]])
        except Exception:
            LOG.warning("Falha ao esquentar o cache.")
        await asyncio.sleep(480)


@asynccontextmanager
async def lifespan(app):
    THUMBS.mkdir(parents=True, exist_ok=True)
    warm = asyncio.create_task(warm_loop())
    yield
    warm.cancel()
    with anyio.CancelScope(shield=True):
        await streams.begin()


app = FastAPI(title="YouTube 360", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=BASE / "static"), name="static")


@app.exception_handler(RequestValidationError)
async def parameter_error(request: Request, error: RequestValidationError):
    # Mesmo parâmetros inválidos preservam o tipo de erro combinado com o console.
    if request.url.path == "/api/feed":
        return PlainTextResponse("erro\tParâmetros de feed inválidos.\n")
    if request.url.path.startswith("/api/thumb/"):
        return PlainTextResponse("Miniatura indisponível.\n", status_code=404)
    return await request_validation_exception_handler(request, error)


@app.get("/")
async def index():
    # nome da marca vem do config.json ("brand"); o padrão é Tube360
    html = (BASE / "static/index.html").read_text().replace("{{BRAND}}", clean(CONFIG.get("brand") or "Tube360"))
    return HTMLResponse(html, headers={"Cache-Control": "no-cache"})


@app.get("/api/feed", response_class=PlainTextResponse)
async def feed(kind: str = "home", q: str = "", n: int = Query(24, ge=1, le=100), fresh: int = Query(0, ge=0, le=1),
               offset: int = Query(0, ge=0, le=5000)):
    if kind == "history":
        lines = [f"ok\t{min(n, len(history.items))}\tContinuar assistindo"]
        for e in history.recent(n):
            when = time.strftime("%d/%m", time.localtime(e["ts"]))
            lines.append("\t".join([clean(e["id"]), str(int(e.get("dur") or 0)), clean(e.get("channel", "")),
                                     f"Visto em {when}", clean(e.get("title", e["id"])), f"{e.get('pos', 0):.1f}"]))
        return PlainTextResponse("\n".join(lines) + "\n")
    if kind not in ("home", "search") or (kind == "search" and not clean(q)):
        return PlainTextResponse("erro\tInforme uma busca válida.\n")
    query = clean(q)[:250]
    try:
        if kind == "search" and fresh:
            search_cache.values.pop((query, 100), None)
        if kind == "home":
            base = await home_videos(bool(fresh))
            if offset + n > len(base) + len(home_extra):
                await home_more([i["id"] for i in base] + [i["id"] for i in home_extra])
            everything = base + home_extra
        else:
            count = 100
            everything = await search_videos(query, count)
            while offset + n > len(everything) and len(everything) >= count and count < 400:
                count *= 2
                everything = await search_videos(query, count)
        items = everything[offset:offset + n]
        if fresh and offset == 0:
            items = random.sample(items, len(items))
    except Exception:
        LOG.warning("Não foi possível carregar o feed %s.", kind)
        return PlainTextResponse("erro\tNão foi possível consultar o YouTube. Tente novamente.\n")
    prefetch_later(items)
    title = "Para assistir hoje" if kind == "home" else f"Busca: {query}"
    lines = [f"ok\t{len(items)}\t{title}"]
    # 6º campo: onde o Xbox parou neste vídeo (barra de progresso na miniatura), 0 se nunca viu
    lines += ["\t".join([*(clean(i[k]) for k in ("id", "duration", "channel", "views", "title")), f"{history.position(i['id']):.1f}"])
              for i in items]
    return PlainTextResponse("\n".join(lines) + "\n")


def make_thumb(video_id, width, height, path):
    # A origem é fixa; o ID validado não pode virar URL arbitrária.
    variants = ("hqdefault", "mqdefault") if width > 320 or height > 180 else ("mqdefault", "hqdefault")
    for variant in variants:
        try:
            request = URLRequest(f"https://i.ytimg.com/vi/{video_id}/{variant}.jpg",
                                 headers={"User-Agent": "Tube360/1.0"})
            with urlopen(request, timeout=12) as response:
                data = response.read(4 * 1024 * 1024)
            with Image.open(io.BytesIO(data)) as source:
                if source.width < 160:
                    continue
                thumb = ImageOps.fit(source.convert("RGB"), (width, height), Image.Resampling.LANCZOS)
                temporary = path.with_suffix(".tmp")
                thumb.save(temporary, format="JPEG", quality=85, progressive=False)
                temporary.replace(path)
            return path
        except Exception:
            continue
    raise HTTPException(404, "Miniatura indisponível.")


@app.get("/api/thumb/{video_id}")
async def thumb(video_id: str, w: int = Query(320, ge=1, le=1920), h: int = Query(180, ge=1, le=1080)):
    if not ID_PATTERN.fullmatch(video_id):
        raise HTTPException(404, "Miniatura indisponível.")
    path = THUMBS / f"{video_id}_{w}x{h}.jpg"
    if not path.exists():
        await thumb_cache.get(path.name, lambda: asyncio.to_thread(make_thumb, video_id, w, h, path))
    return FileResponse(path, media_type="image/jpeg", headers={"Cache-Control": "public, max-age=86400"})


@app.get("/api/stream/{video_id}")
async def stream(request: Request, video_id: str, t: float = Query(0, ge=0, le=86400 * 30), q: int = 720,
                 a: int = Query(1, ge=0, le=1)):
    valid_id(video_id)
    if q not in SIZES or not math.isfinite(t):
        raise HTTPException(400, "Qualidade ou posição inválida.")
    ticket = await streams.begin()
    try:
        info = await resolve_video(video_id)
    except Exception:
        raise HTTPException(502, "Não foi possível abrir este vídeo. Tente novamente.") from None
    if await request.is_disconnected():
        raise HTTPException(499, "Conexão encerrada.")
    if info["duration"] and t >= info["duration"]:
        raise HTTPException(416, "Posição após o fim do vídeo.")
    encoder = await streams.start(ticket, ffmpeg_command(info, t, q, with_audio=bool(a)))
    try:
        # Só confirmar HTTP 200 depois de receber MPEG; erro de origem vira 502.
        first_read = asyncio.create_task(encoder.output.get())
        deadline = time.monotonic() + 40
        while not first_read.done():
            if await request.is_disconnected() or time.monotonic() > deadline:
                raise HTTPException(504, "O vídeo demorou para responder.")
            await asyncio.wait({first_read}, timeout=0.2)
        first = first_read.result()
        if not first:
            # Uma URL assinada que deixou de funcionar será resolvida de novo na tentativa seguinte.
            url_cache.values.pop(video_id, None)
            raise HTTPException(502, "A origem do vídeo não respondeu. Tente novamente.")
    except BaseException:
        first_read.cancel()
        with anyio.CancelScope(shield=True):
            await streams.finish(encoder)
        raise

    async def body():
        try:
            yield first
            while chunk := await encoder.output.get():
                yield chunk
        finally:
            with anyio.CancelScope(shield=True):
                await streams.finish(encoder)

    return StreamingResponse(body(), media_type="video/mpeg", headers={
        "X-Duration": str(info["duration"]), "X-Title": quote(info["title"], safe=""),
        "Cache-Control": "no-store", "X-Accel-Buffering": "no",
    })


AUDIO_DIR = Path.home() / ".cache/tube360/audio"
audio_jobs = {}  # chave (id, t) -> processo ffmpeg gravando o MP3 (mais rápido que o tempo real)


async def audio_file(video_id, t):
    """MP3 do vídeo a partir de t, gravado em arquivo enquanto converte. O Nest pode pedir de novo, em trechos, quantas
    vezes quiser: um pedido novo não derruba o anterior (antes, cada pedido matava o anterior e o som morria)."""
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    key = (video_id, round(t, 2))
    path = AUDIO_DIR / f"{video_id}_{key[1]:.2f}.mp3"
    job = audio_jobs.get(key)
    if job is None and not path.exists():
        info = await resolve_video(video_id)
        # limpa áudios antigos (cada um tem poucos MB)
        for old in sorted(AUDIO_DIR.glob("*.mp3"), key=lambda f: f.stat().st_mtime)[:-6]:
            with contextlib.suppress(OSError):
                old.unlink()
        cmd = audio_command(info, t)[:-1] + ["-y", str(path)]
        job = await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        audio_jobs[key] = job

        async def reap(k=key, j=job):
            await j.wait()
            audio_jobs.pop(k, None)
        task = asyncio.create_task(reap())
        background.add(task)
        task.add_done_callback(background.discard)
    return path, audio_jobs.get(key)


@app.get("/api/audio/{video_id}")
async def audio(request: Request, video_id: str, t: float = Query(0, ge=0, le=86400 * 30)):
    valid_id(video_id)
    try:
        path, job = await audio_file(video_id, t)
    except Exception:
        raise HTTPException(502, "Não foi possível abrir este áudio.") from None
    start = 0
    rng = request.headers.get("range", "")
    m = re.match(r"bytes=(\d+)-", rng)
    if m:
        start = int(m.group(1))
    LOG.info("áudio do Nest: %s t=%.1f desde o byte %d%s", video_id, t, start, " (convertendo)" if job else "")
    deadline = time.monotonic() + 30
    while not path.exists() or path.stat().st_size <= start:
        if job is None or job.returncode is not None or time.monotonic() > deadline:
            break
        await asyncio.sleep(0.1)

    # Pedido de trecho (Range): responde só o que já foi gravado, com o Content-Range certo; o Nest pede o resto depois.
    end = None
    if m:
        size = path.stat().st_size if path.exists() else 0
        done = job is None or job.returncode is not None
        end = max(start, size - 1)

    async def body():
        pos = start
        with open(path, "rb") as f:
            f.seek(pos)
            while True:
                want = 65536 if end is None else min(65536, end + 1 - pos)
                if want <= 0:
                    break
                chunk = f.read(want)
                if chunk:
                    pos += len(chunk)
                    yield chunk
                    continue
                if end is not None:
                    break
                running = audio_jobs.get((video_id, round(t, 2)))
                if running is None or running.returncode is not None:
                    rest = f.read()
                    if rest:
                        yield rest
                    break
                await asyncio.sleep(0.2)

    headers = {"Cache-Control": "no-store", "Accept-Ranges": "bytes"}
    status = 200
    if m:
        status = 206
        headers["Content-Range"] = f"bytes {start}-{end}/{size if done else '*'}"
    return StreamingResponse(body(), status_code=status, media_type="audio/mpeg", headers=headers)


class Nest:
    """O alto-falante Google Cast (config.json, "nest": {"host", "name"}). Uma thread consulta a posição a cada 250 ms
    enquanto toca; o console lê a última posição (com o instante em que foi lida) em /api/nest/pos."""
    APP = "CC1AD845"  # Default Media Receiver

    def __init__(self):
        self.cc = None
        self.lock = threading.Lock()
        self.state, self.pos, self.stamp, self.error = "IDLE", 0.0, time.monotonic(), ""
        self.active = False
        self.ours = False  # o receptor aberto no Nest é o que este servidor abriu
        self.video_id, self.t0, self.host_ip = "", 0.0, ""  # o que está tocando e de que ponto (posição absoluta = t0 + tempo do Nest)
        self.recover_at = 0.0
        threading.Thread(target=self.watch, daemon=True).start()

    def connect(self):
        if self.cc is not None and self.cc.socket_client.is_connected:
            return self.cc
        cfg = CONFIG.get("nest") or {}
        host = cfg.get("host")
        if not host:
            raise RuntimeError("Nenhum Nest configurado (config.json, nest.host).")
        info = CastInfo(services={HostServiceInfo(host, 8009)}, uuid=uuid.uuid4(), model_name="Nest",
                        friendly_name=cfg.get("name", "Nest"), host=host, port=8009, cast_type="audio", manufacturer="Google")
        last = None
        for tentativa in range(3):
            try:
                cc = pychromecast.get_chromecast_from_cast_info(info, zconf=None, tries=2, timeout=6)
                cc.wait(timeout=8)
                self.cc = cc
                return cc
            except Exception as error:
                last = error
                LOG.warning("Nest: conexão falhou (tentativa %d): %s", tentativa + 1, error)
                time.sleep(1.5)
        raise last

    def play(self, url):
        with self.lock:
            self.state, self.pos, self.stamp, self.error = "LOADING", 0.0, time.monotonic(), ""
            try:
                t0 = time.monotonic()
                cc = self.connect()
                LOG.info("Nest: conectado em %.1f s, app %s", time.monotonic() - t0, cc.status.app_id)
                # Reabre o receptor padrão só se ele não é nosso (Bluetooth, outro app, ou aberto por outro remetente):
                # aberto sem sessão nossa, o Nest ignora o LOAD calado (medido em 2026-11-21). Se o LOAD não pegar em 6 s,
                # reabre e tenta de novo.
                for tentativa in (0, 1):
                    if tentativa or not self.ours or cc.status.app_id != self.APP:
                        cc.quit_app()
                        time.sleep(0.5)
                        cc.start_app(self.APP)
                        deadline = time.monotonic() + 8
                        while cc.status.app_id != self.APP and time.monotonic() < deadline:
                            time.sleep(0.1)
                        time.sleep(1.0)
                        LOG.info("Nest: receptor aberto em %.1f s", time.monotonic() - t0)
                    mc = cc.media_controller
                    mc.play_media(url, "audio/mpeg", title="YouTube 360")
                    deadline = time.monotonic() + (12 if tentativa else 6)
                    while time.monotonic() < deadline:
                        mc.update_status()
                        time.sleep(0.2)
                        if mc.status.player_state in ("BUFFERING", "PLAYING"):
                            break
                    if mc.status.player_state in ("BUFFERING", "PLAYING"):
                        self.ours = True
                        break
                    self.ours = False
                LOG.info("Nest: %s em %.1f s", mc.status.player_state, time.monotonic() - t0)
                self.active = True
            except Exception as error:
                self.state, self.error = "ERRO", str(error)[:120]
                self.cc = None
                LOG.warning("Nest: %s", self.error)

    def prepare(self):
        """Deixa o receptor padrão aberto e nosso, para o primeiro play não pagar a troca de app (Bluetooth → Cast)."""
        with self.lock:
            try:
                cc = self.connect()
                if self.ours and cc.status.app_id == self.APP:
                    return
                t0 = time.monotonic()
                cc.quit_app()
                time.sleep(0.5)
                cc.start_app(self.APP)
                deadline = time.monotonic() + 8
                while cc.status.app_id != self.APP and time.monotonic() < deadline:
                    time.sleep(0.1)
                time.sleep(2.0)
                self.ours = cc.status.app_id == self.APP
                LOG.info("Nest: preparado em %.1f s", time.monotonic() - t0)
            except Exception as error:
                LOG.warning("Nest prepare: %s", error)
                self.cc = None

    def volume(self, level):
        with self.lock:
            try:
                self.connect().set_volume(max(0.0, min(1.0, level / 100)))
            except Exception as error:
                LOG.warning("Nest volume: %s", error)
                self.cc = None

    def command(self, what):
        with self.lock:
            try:
                cc = self.connect()
                mc = cc.media_controller
                if not mc.status.media_session_id:
                    mc.update_status()  # depois de reiniciar ou retomar, o controle ainda não conhece a sessão do Nest
                    time.sleep(0.6)
                if mc.status.media_session_id:
                    {"pause": mc.pause, "resume": mc.play, "stop": mc.stop}[what]()
                elif what == "stop":
                    cc.quit_app()  # sem sessão para parar: fecha o receptor, o som para de qualquer jeito
                    self.ours = False
                if what == "stop":
                    self.active = False
                    self.video_id = ""
                    self.state, self.pos = "IDLE", 0.0
            except Exception as error:
                LOG.warning("Nest %s: %s", what, error)
                self.cc = None

    def watch(self):
        orphan_since = 0.0
        while True:
            time.sleep(0.25)
            if not self.active or self.cc is None:
                orphan_since = 0.0
                continue
            # o Xbox fechou o app, caiu, ou foi para outro vídeo sem avisar: o Nest não pode ficar tocando sozinho
            gone = (time.monotonic() - remote.last_poll > 6) or (self.video_id and remote.playing != self.video_id)
            if gone and self.state != "LOADING":
                orphan_since = orphan_since or time.monotonic()
                if time.monotonic() - orphan_since > 4:
                    LOG.info("Nest: o Xbox não está mais neste vídeo; parando o som")
                    orphan_since = 0.0
                    threading.Thread(target=self.command, args=("stop",), daemon=True).start()
                    self.active = False
                    continue
            else:
                orphan_since = 0.0
            try:
                mc = self.cc.media_controller
                mc.update_status()
                st = mc.status
                took_over = self.cc.status.app_id not in (None, self.APP)
                if st.player_state != getattr(self, "last_logged", None):
                    LOG.info("Nest: estado %s (pos %.1f, motivo %s)", st.player_state, float(st.current_time or 0) + self.t0, st.idle_reason)
                    self.last_logged = st.player_state
                if st.player_state in ("PLAYING", "PAUSED", "BUFFERING") and not took_over:
                    self.state, self.pos, self.stamp = st.player_state, float(st.current_time or 0), time.monotonic()
                elif self.state not in ("LOADING",):
                    self.state = "IDLE"
                    # caiu no meio (Bluetooth ou outro app tomou o Nest, Wi-Fi): toca de novo do ponto em que o Xbox está
                    if self.video_id and (took_over or st.idle_reason in ("INTERRUPTED", "ERROR", "CANCELLED")) \
                            and time.monotonic() - self.recover_at > 8 and remote.playing == self.video_id:
                        self.recover_at = time.monotonic()
                        LOG.warning("Nest: sessão caiu (%s); retomando em %.1f s", "outro app" if took_over else st.idle_reason, remote.pos)
                        self.ours = False
                        self.t0 = remote.pos
                        url = f"http://{self.host_ip}:8091/api/audio/{self.video_id}?t={self.t0:.2f}&s={int(time.time() * 1000)}"
                        threading.Thread(target=self.play, args=(url,), daemon=True).start()
            except Exception:
                pass

    def position(self):
        """Estado e posição extrapolada para agora (só avança tocando)."""
        state, pos, stamp = self.state, self.pos, self.stamp
        if state == "PLAYING":
            pos += time.monotonic() - stamp
        return state, pos + self.t0


nest = Nest()


def lan_ip_towards(host):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((host, 9))
        return s.getsockname()[0]


@app.get("/api/nest/info", response_class=PlainTextResponse)
async def nest_info():
    cfg = CONFIG.get("nest") or {}
    return PlainTextResponse(f"ok\t{clean(cfg.get('name', 'Nest'))}\n" if cfg.get("host") else "erro\tNenhum Nest configurado.\n")


@app.get("/api/nest/play", response_class=PlainTextResponse)
async def nest_play(video_id: str = Query(..., alias="id"), t: float = Query(0, ge=0, le=86400 * 30)):
    valid_id(video_id)
    host = (CONFIG.get("nest") or {}).get("host")
    if not host:
        return PlainTextResponse("erro\tNenhum Nest configurado.\n")
    nest.host_ip = lan_ip_towards(host)
    url = f"http://{nest.host_ip}:8091/api/audio/{video_id}?t={t:.2f}&s={int(time.time() * 1000)}"
    nest.state, nest.pos, nest.stamp = "LOADING", 0.0, time.monotonic()
    nest.video_id, nest.t0 = video_id, t
    threading.Thread(target=nest.play, args=(url,), daemon=True).start()
    return PlainTextResponse("ok\n")


@app.get("/api/nest/prepare", response_class=PlainTextResponse)
async def nest_prepare():
    if not (CONFIG.get("nest") or {}).get("host"):
        return PlainTextResponse("erro\tNenhum Nest configurado.\n")
    threading.Thread(target=nest.prepare, daemon=True).start()
    return PlainTextResponse("ok\n")


@app.get("/api/nest/pos", response_class=PlainTextResponse)
async def nest_pos():
    state, pos = nest.position()
    return PlainTextResponse(f"{state}\t{pos:.3f}\t{clean(nest.error)}\n")


@app.get("/api/nest/volume", response_class=PlainTextResponse)
async def nest_volume(v: float = Query(..., ge=0, le=100)):
    threading.Thread(target=nest.volume, args=(v,), daemon=True).start()
    return PlainTextResponse("ok\n")


CAST_CODE = Path.home() / ".cache/tube360/cast-code.txt"  # escrito pelo receptor de Cast (cast/receptor.mjs)


class CastEvent(BaseModel):
    kind: Literal["connect", "disconnect"]
    name: str = ""


cast_sender = {"name": "", "connected": False}


@app.post("/api/cast/event", response_class=PlainTextResponse)
async def cast_event(event: CastEvent):
    """O receptor (cast/receptor.mjs) avisa quando um celular conecta ou sai; o console mostra na tela."""
    name = clean(event.name)[:40] or "celular"
    cast_sender.update(name=name, connected=event.kind == "connect")
    text = f"Celular conectado: {name}" if event.kind == "connect" else f"{name} desconectou o celular"
    remote.commands.append({"action": "aviso", "text": text})
    return PlainTextResponse("ok\n")


@app.get("/api/cast/code", response_class=PlainTextResponse)
async def cast_code():
    try:
        code = CAST_CODE.read_text().strip()
        if code and time.time() - CAST_CODE.stat().st_mtime < 330:
            who = cast_sender["name"] if cast_sender["connected"] else ""
            return PlainTextResponse(f"ok\t{clean(code)}\t{who}\n")
    except OSError:
        pass
    return PlainTextResponse("erro\tO receptor do YouTube não está ligado.\n")


@app.get("/api/nest/{what}", response_class=PlainTextResponse)
async def nest_command(what: Literal["pause", "resume", "stop"]):
    threading.Thread(target=nest.command, args=(what,), daemon=True).start()
    return PlainTextResponse("ok\n")


@app.post("/api/remote/cmd")
async def command(cmd: Command):
    if cmd.action in ("play", "queue"):
        valid_id(cmd.id or "")
    if cmd.action == "seek" and cmd.t is None and cmd.delta is None:
        raise HTTPException(400, "Informe t ou delta para buscar.")
    if (cmd.action == "queue" and len(remote.queue) >= 256) or (cmd.action != "queue" and len(remote.commands) >= 256):
        raise HTTPException(429, "A fila está cheia.")
    if cmd.action == "queue":
        remote.queue.append(remote.item(cmd.id) | {"t": cmd.t or 0})
    else:
        remote.commands.append(cmd.model_dump(exclude_none=True))
    return {"ok": True}


@app.get("/api/remote/state")
async def state():
    return remote.state()


@app.get("/api/remote/poll", response_class=PlainTextResponse)
async def poll(playing: str = "", pos: float = Query(0, ge=0), dur: float = Query(0, ge=0),
               paused: int = Query(0, ge=0, le=1), ended: str = ""):
    last_console[0] = time.monotonic()
    for video_id in (playing, ended):
        if video_id:
            valid_id(video_id)
    if not math.isfinite(pos) or not math.isfinite(dur):
        raise HTTPException(400, "Posição inválida.")
    return PlainTextResponse(remote.poll(playing, pos, dur, paused, ended),
                             headers={"Cache-Control": "no-store"})


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    # h11 respeita o enquadramento por EOF do HTTP/1.0 usado pelo console.
    uvicorn.run(app, host="0.0.0.0", port=8091, http="h11", access_log=False)
