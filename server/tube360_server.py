#!/usr/bin/env python3
"""Servidor do Tube360. Contrato: ../docs/PROTOCOL.md."""

import asyncio
import contextlib
import io
import json
import logging
import math
import os
import re
import random
import shlex
import shutil
import socket
import threading
import uuid
import time
from collections import deque
from datetime import datetime, timezone
from contextlib import asynccontextmanager
from html import escape
from itertools import zip_longest
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import parse_qs, quote, urlparse
from urllib.request import Request as URLRequest, urlopen

import anyio
import uvicorn
import yt_dlp
from yt_dlp.plugins import load_all_plugins
from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.exceptions import RequestValidationError
from fastapi.exception_handlers import request_validation_exception_handler
from fastapi.responses import FileResponse, HTMLResponse, PlainTextResponse, RedirectResponse, StreamingResponse
from PIL import Image, ImageOps
from pydantic import BaseModel, ConfigDict, Field
from typing import Literal

import pychromecast
from pychromecast.models import CastInfo, HostServiceInfo

BASE = Path(__file__).resolve().parent
# config.json é seu (fica fora do Git); sem ele, vale o config.example.json
CONFIG_PATH = Path(os.environ.get("TUBE360_CONFIG", BASE / "config.json"))
CONFIG = json.loads((CONFIG_PATH if CONFIG_PATH.exists() else BASE / "config.example.json").read_text(encoding="utf-8"))
LANGUAGE = "pt" if CONFIG.get("language") == "pt" else "en"
PORT = int(os.environ.get("TUBE360_PORT", "8091"))
ACCOUNT_URL = os.environ.get("TUBE360_ACCOUNT_URL", "http://127.0.0.1:8099").rstrip("/")
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


def tr(pt, en, lang=None):
    return pt if (lang or LANGUAGE) == "pt" else en


def ydl_options(lang=None, cookies=False, skip_clients=(), **extra):
    lang = lang or LANGUAGE
    youtube_args = {"lang": [lang]}
    if skip_clients:
        youtube_args["player_client"] = ["default", *(f"-{name}" for name in skip_clients)]
    options = {
        "quiet": True, "no_warnings": True, "logger": QuietYDL(),
        "socket_timeout": 15, "retries": 1, "extractor_retries": 1,
        "skip_download": True, "noplaylist": True,
        # O identificador brasileiro aceito pelo extrator é "pt" (não "pt-BR").
        "extractor_args": {"youtube": youtube_args},
        "http_headers": {"Accept-Language": tr("pt-BR,pt;q=0.9", "en-US,en;q=0.9", lang)},
        "remote_components": ["ejs:github"],
        "cachedir": str(BASE / ".yt-dlp-cache"),
    }
    if node := shutil.which("node"):
        options["js_runtimes"] = {"node": {"path": node}}
    # cookies só quando pedido (resolve_video, quando o YouTube exige login): conta exposta o mínimo possível
    if cookies and (browser := CONFIG.get("cookies_from_browser")):
        # "chrome" ou "chrome:<perfil ou caminho do perfil>", como no --cookies-from-browser do yt-dlp
        name, _, profile = str(browser).partition(":")
        options["cookiesfrombrowser"] = (name, os.path.expanduser(profile) if profile else None, None, None)
    # cookies.txt exportado (VPS sem navegador, quando o YouTube pede "confirme que não é um robô")
    if cookies and (cookie_file := CONFIG.get("cookies_file")):
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


home_cache = TTLCache(600, 2)
search_cache = TTLCache(300)
url_cache = TTLCache(1800)
thumb_cache = TTLCache(60, 256)


def views_text(count, lang=None):
    if count is None:
        return ""
    count = max(0, int(count))
    if (lang or LANGUAGE) == "en":
        for divisor, unit in ((1_000_000_000, "B"), (1_000_000, "M"), (1000, "K")):
            if count >= divisor:
                return f"{count / divisor:.1f}".removesuffix(".0") + unit + " views"
        return f"{count} view{'s' if count != 1 else ''}"
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


def normalize(entry, lang=None):
    video_id = entry.get("id", "")
    duration = max(0, int(entry.get("duration") or 0))
    url = entry.get("url") or entry.get("webpage_url") or ""
    if not ID_PATTERN.fullmatch(video_id) or "/shorts/" in url or 0 < duration < 61:
        return None
    return remember({
        "id": video_id, "duration": duration,
        "channel": clean(entry.get("channel") or entry.get("uploader")),
        "views": views_text(entry.get("view_count"), lang),
        "title": clean(entry.get("title") or video_id),
    })


async def search_videos(query, count=100, lang=None):
    lang = lang or LANGUAGE
    async def load():
        data = await extract(f"ytsearch{count}:{query}", extract_flat="in_playlist", lang=lang)
        items, seen = [], set()
        for entry in data.get("entries", []):
            item = normalize(entry, lang) if entry else None
            if item and item["id"] not in seen:
                seen.add(item["id"])
                items.append(item)
        return items
    return await search_cache.get((query, count, lang), load, stale_ok=True)


home_extra = {"pt": [], "en": []}  # páginas a mais da home (rolagem sem fim): buscas de temas sorteados, anexadas sem repetir


async def home_more(have, lang=None):
    """Mais vídeos para a home quando o console chega ao fim: 4 temas sorteados, intercalados, sem repetir id."""
    lang = lang or LANGUAGE
    pool = CONFIG.get("home_queries") or [tr("música brasileira", "music", lang)]
    queries = random.sample(pool, min(4, len(pool)))
    results = await asyncio.gather(*(search_videos(clean(q), 40, lang) for q in queries), return_exceptions=True)
    groups = [r for r in results if isinstance(r, list)]
    seen = set(have)
    for row in zip_longest(*groups):
        for item in row:
            if item and item["id"] not in seen:
                home_extra[lang].append(item)
                seen.add(item["id"])


async def home_videos(fresh=False, lang=None):
    lang = lang or LANGUAGE
    if fresh:
        home_cache.values.pop(lang, None)
        home_extra[lang].clear()

    async def load():
        pool = CONFIG.get("home_queries") or [tr("música brasileira", "music", lang), "science", "games"]
        queries = random.sample(pool, min(len(pool), 6))
        results = await asyncio.gather(
            *(search_videos(clean(q), max(16, math.ceil(100 / len(queries)) * 2), lang) for q in queries),
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
    return await home_cache.get(lang, load, stale_ok=True)


cookie_only_until = [0.0]  # enquanto o IP estiver na checagem de robô, vai direto com a conta


def has_cookies():
    return bool(CONFIG.get("cookies_from_browser") or CONFIG.get("cookies_file"))


async def extract_video(url, **options):
    """Sem conta primeiro (mais rápido: ~3 s contra ~8 s com cookies). Se o YouTube pedir "confirme que não é um robô"
    e houver cookies no config, repete com a conta e segue com ela por 30 min antes de tentar sem de novo."""
    if time.monotonic() >= cookie_only_until[0] or not has_cookies():
        try:
            return await extract(url, **options)
        except Exception as error:
            text = str(error).lower()  # a mensagem vem no idioma do extrator ("não é um bot", "not a bot")
            if not has_cookies() or not re.search(r"\bbot\b", text) or not ("login" in text or "sign in" in text):
                raise
            cookie_only_until[0] = time.monotonic() + 1800
            LOG.info("YouTube pediu login (checagem de robô): usando a conta dos cookies por 30 min")
    return await extract(url, cookies=True, **options)


def unplayable_reason(error):
    """Motivo que o console sabe explicar quando o YouTube nega o vídeo (a mensagem vem em pt ou en). Só para recusa
    do próprio YouTube: erro de rede/HTTP (503 "Service Unavailable") e checagem de robô ficam com a mensagem genérica."""
    if not isinstance(error, (yt_dlp.utils.DownloadError, yt_dlp.utils.ExtractorError)):
        return None
    text = str(error).lower()
    if re.search(r"\bbot\b|robô|http error|timed out|tempo esgotado", text):
        return None
    for reason, words in (("members", ("membro", "member")), ("private", ("privado", "is private", "private video")),
                          ("age", ("idade", "age-restricted", "confirm your age")),
                          ("unavailable", ("video unavailable", "vídeo indisponível", "não está mais disponível",
                                           "no longer available", "has been removed", "foi removido",
                                           "has been terminated", "foi encerrada",
                                           "live stream recording is not available", "gravação dessa transmissão"))):
        if any(word in text for word in words):
            return reason
    return None


def source_refused(info, source):
    """Nome do cliente da API (parâmetro `c` da URL) quando a origem responde 403 a este link."""
    headers = info["headers"] | source.get("http_headers", {}) | {"Range": "bytes=0-1"}
    try:
        with urlopen(URLRequest(source["url"], headers=headers), timeout=8):
            return None
    except HTTPError as error:
        if error.code != 403:
            return None
    except OSError:
        return None
    return parse_qs(urlparse(source["url"]).query).get("c", [None])[0]


def clients_named(api_name):
    """Clientes do yt-dlp que se apresentam com esse nome (TVHTML5 = tv, tv_samsung...)."""
    try:
        from yt_dlp.extractor.youtube._base import INNERTUBE_CLIENTS
    except ImportError:  # módulo interno: se mudar de lugar, só perde a troca de cliente
        return []
    return sorted(name for name, client in INNERTUBE_CLIENTS.items()
                  if client["INNERTUBE_CONTEXT"]["client"]["clientName"] == api_name)


REFUSED_TTL = 1800  # segundos
refused_clients = {}  # cliente do yt-dlp -> até quando pular (time.monotonic)


def clients_to_skip():
    now = time.monotonic()
    return sorted(name for name, until in refused_clients.items() if until > now)


async def resolve_video(video_id):
    async def load():
        # A recusa costuma valer para muitos vídeos seguidos (3 de 4 em 2026-11-22): extrair de novo a cada vídeo
        # custava uns 6 s por abertura. O cliente recusado fica de fora por 30 min e depois é testado de novo.
        skip = clients_to_skip()
        for attempt in range(3):
            try:
                # A mesma resolução (até 720p) atende todas as qualidades e buscas temporais.
                data = await extract_video(
                    f"https://www.youtube.com/watch?v={video_id}", skip_clients=skip,
                    # H.264 antes: abrir AV1 custa o dobro de CPU (720p, medido num núcleo: 49% contra 26%).
                    # Áudio m4a antes: começar no meio leva 2,5-3,9 s contra 6,4-6,8 s do webm, cujo índice fica no
                    # fim do arquivo (medido em 2026-11-22); com o som na TV, o vídeo começou em 2,9 s em vez de 7,3 s.
                    format="bestvideo[height<=720][vcodec^=avc1][protocol=https]+bestaudio[ext=m4a][protocol=https]"
                           "/bestvideo[height<=720][vcodec^=avc1][protocol=https]+bestaudio[protocol=https]"
                           "/bestvideo[height<=720][protocol=https]+bestaudio[protocol=https]"
                           # Live só tem HLS (m3u8); nos clientes padrão, com imagem e som separados.
                           "/bestvideo[height<=720][vcodec^=avc1]+bestaudio/bestvideo[height<=720]+bestaudio"
                           "/best[height<=720]/best",
                )
            except Exception:
                if not skip or attempt == 2:
                    raise
                # sem os clientes lembrados pode não sobrar nenhum que sirva: esquece a lista e tenta com todos
                refused_clients.clear()
                skip = []
                continue
            if data.get("live_status") == "is_live" and data.get("requested_formats"):
                # Imagem e som da live em HLS separados começam em segmentos diferentes em ~1 de 3 aberturas (5 s fora
                # de sincronia, medido em 2026-11-22). O mweb dá os dois no mesmo segmento; sem ele, segue o separado.
                # (O android_vr também dá, mas os segmentos dele voltam 403 depois de ~30 s.)
                with contextlib.suppress(Exception):
                    data = await extract_video(
                        f"https://www.youtube.com/watch?v={video_id}",
                        extractor_args={"youtube": {"lang": [LANGUAGE], "player_client": ["mweb"]}},
                        format="best[height<=720][vcodec^=avc1][acodec!=none]")
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
                "live": data.get("live_status") == "is_live",
            }
            # O YouTube às vezes recusa (403) todos os links de um cliente (tv_samsung, 2026-11-22), e o link novo do
            # mesmo cliente volta recusado: refaz a extração sem ele.
            sources = [video] if video["url"] == audio["url"] else [video, audio]
            refused = await asyncio.gather(*(asyncio.to_thread(source_refused, result, s) for s in sources))
            new = sorted({name for api_name in refused if api_name for name in clients_named(api_name)} - set(skip))
            if not new or attempt == 2:
                break
            until = time.monotonic() + REFUSED_TTL
            for name in new:
                refused_clients[name] = until
            skip = sorted(set(skip) | set(new))
            LOG.info("vídeo %s: o YouTube recusou o link de %s, extraindo sem esse cliente por 30 min", video_id, ", ".join(new))
        remember({"id": video_id, "duration": result["duration"], "title": result["title"],
                  "channel": clean(data.get("channel")), "views": views_text(data.get("view_count"))})
        return result
    return await url_cache.get(video_id, load)


def source_args(info, source, start):
    headers = info["headers"] | source.get("http_headers", {})
    header_text = "".join(f"{clean(k)}: {clean(v)}\r\n" for k, v in headers.items())
    return ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "3",
            "-reconnect_on_network_error", "1", "-reconnect_on_http_error", "5xx",
            "-rw_timeout", "15000000", "-headers", header_text,
            # live começa perto do ao vivo; buscar no HLS dela não faz sentido
            *([] if info.get("live") else ["-ss", str(start)]), "-i", source["url"]]


def mp3_output(stream, target):
    return ["-map", stream, "-vn", "-af", "aresample=44100,asetpts=PTS-STARTPTS",
            "-c:a", "libmp3lame", "-b:a", "192k", "-ar", "44100", "-ac", "2", "-flush_packets", "1", "-f", "mp3", target]


def audio_command(info, start):
    """Só o áudio, em MP3, para o Nest (Google Cast). Começa em `start`, com o tempo do fluxo em zero, como o vídeo."""
    return [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error",
            *source_args(info, info["audio"], start), *mp3_output("0:a:0", "pipe:1")]


def ffmpeg_command(info, start, quality, with_audio=True, mp3=None):
    """`mp3`: arquivo onde o mesmo ffmpeg grava o som do Nest (só na live; ver stream)."""
    width, height, bitrate = SIZES[quality]
    cmd = [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error"]
    separate = info["video"]["url"] != info["audio"]["url"]
    if not with_audio:
        cmd += source_args(info, info["video"], start)
        if mp3 and separate:
            cmd += source_args(info, info["audio"], start)
        cmd += ["-map", "0:v:0", "-an",
                "-vf", f"scale={width}:{height}:force_original_aspect_ratio=decrease:force_divisible_by=2,"
                         f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black,setsar=1,fps=30,setpts=PTS-STARTPTS",
                "-c:v", "mpeg1video", "-b:v", bitrate, "-maxrate", bitrate, "-bufsize", "2048k",
                "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-threads", "4",
                "-avoid_negative_ts", "disabled", "-muxdelay", "0.2", "-muxpreload", "0",
                "-preload", "0", "-packetsize", "2048", "-flush_packets", "1", "-f", "mpeg", "pipe:1"]
        if mp3:
            cmd += ["-y", *mp3_output("1:a:0" if separate else "0:a:0", str(mp3))]
        return cmd
    for source in ([info["video"], info["audio"]] if separate else [info["video"]]):
        cmd += source_args(info, source, start)
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
        self.FILE.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.FILE.with_suffix(".tmp")
        temporary.write_text(json.dumps(list(self.items.values()), ensure_ascii=False))
        temporary.replace(self.FILE)

    def position(self, video_id):
        e = self.items.get(video_id)
        return e["pos"] if e else 0.0

    def recent(self, n):
        return list(reversed(list(self.items.values())))[:n]


history = History()


COMMAND_TTL = 15  # segundos


class Remote:
    def __init__(self):
        self.commands, self.queue = deque(), deque()
        self.playing, self.title = "", ""
        self.pos, self.dur, self.paused = 0.0, 0.0, False
        self.last_poll, self.last_ended = 0.0, ""
        self.watch_stamp = 0.0
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
        shutting_down = sleep_timer.poll(playing, pos, dur, paused, ended)
        new_session = playing and (playing != self.playing or time.monotonic() - self.last_poll > 6)
        if new_session:
            spawn_background(account_watched(playing))
            self.watch_stamp = time.monotonic()
        if self.playing and self.pos > 0 and (playing != self.playing or ended == self.playing or
                time.monotonic() - self.watch_stamp >= 30):
            spawn_background(account_watched(self.playing, self.pos, progress=True))
            self.watch_stamp = time.monotonic()
        self.playing, self.pos, self.dur, self.paused = playing, pos, dur, bool(paused)
        self.title = self.item(playing)["title"] if playing else ""
        self.last_poll = time.monotonic()
        if shutting_down:
            return "desligar\n"
        if playing:
            history.touch(playing, pos, dur)
        if ended and ended != self.last_ended:
            history.touch(ended, dur, dur, finished=True)
            self.last_ended = ended
            self.ended_seq += 1
            # tocar/parar já pedidos pelo celular valem mais que o avanço automático da fila
            if self.queue and not any(c["action"] in ("play", "stop") for c in self.commands):
                self.commands.append({"action": "next"})
        elif not ended:
            self.last_ended = ""
        # Pedido do celular feito com o app fechado não vale quando o app abre: repetir em sequência tocar, buscar e
        # pausar velhos abria o vídeo já pausado e o app desistia do Nest (2026-11-22). O console pede a cada ~0,5 s.
        now = time.monotonic()
        while self.commands and now - self.commands[0].get("at", now) > COMMAND_TTL:
            LOG.info("controle: pedido velho do celular descartado (%s)", self.commands.popleft()["action"])
        if not self.commands:
            return "none\n"
        cmd = self.commands.popleft()
        action = cmd["action"]
        if action in ("play", "stop"):
            # um avanço automático pendente não pode desfazer o que o celular acabou de pedir
            self.commands = deque(c for c in self.commands if c["action"] != "next")
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


class SleepTimer:
    """Relógio do servidor; não depende de o console continuar conectado."""
    def __init__(self):
        self.active = False
        self.what, self.mode = "xbox", "minutos"
        self.deadline, self.poweroff_at = 0.0, 0.0
        self.video_id = ""
        self.fired = False
        self.shutdown = False
        self.test = os.environ.get("TUBE360_SLEEP_TEST") == "1"
        self.delay = float(os.environ.get("TUBE360_SLEEP_TEST_DELAY", "20")) if self.test else 20.0

    def remaining(self):
        if not self.active or self.fired:
            return 0
        if self.mode == "fim_do_video" and remote.playing == self.video_id:
            return max(0, math.ceil(remote.dur - remote.pos)) if remote.dur > 0 else -1
        return max(0, math.ceil(self.deadline - time.monotonic())) if self.deadline else -1

    def line(self):
        return f"ok\t{'on' if self.active else 'off'}\t{self.remaining()}\t{self.what}\t{self.mode}\n"

    def cancel(self):
        self.active = self.fired = self.shutdown = False
        self.deadline = self.poweroff_at = 0.0
        self.video_id = ""

    def set(self, minutes, end, what):
        if end and not remote.playing:
            raise HTTPException(409, "No video playing.")
        self.cancel()
        self.active, self.what = True, what
        self.mode = "fim_do_video" if end else "minutos"
        self.video_id = remote.playing if end else ""
        self.deadline = time.monotonic() + (max(0, remote.dur - remote.pos) if end else minutes * 60)
        if end and remote.dur <= 0:
            self.deadline = 0.0
        LOG.info("Timer: ligado (%s, %s)", self.what, self.mode)

    def expire(self):
        if not self.active or self.fired:
            return
        LOG.info("Timer: vencido; enviando desligar ao Xbox (%s)", self.what)
        self.fired = self.shutdown = True
        remote.commands.clear()
        remote.queue.clear()
        if nest.active:
            spawn_background(asyncio.to_thread(nest.command, "stop"))
        if self.what == "xbox_notebook":
            self.poweroff_at = time.monotonic() + self.delay
        else:
            self.active = False

    def poll(self, playing, pos, dur, paused, ended):
        if self.active and not self.fired and self.mode == "fim_do_video":
            if ended == self.video_id:
                self.expire()
            elif playing == self.video_id and dur > 0:
                self.deadline = time.monotonic() + max(0, dur - pos)
        # Prioridade sobre autoplay e comandos de Cast.
        if self.shutdown:
            self.shutdown = False
            return True
        return False

    async def run(self):
        while True:
            now = time.monotonic()
            if self.active and not self.fired:
                gone = not remote.last_poll or now - remote.last_poll >= 60
                if self.deadline and now >= self.deadline and (self.mode == "minutos" or gone):
                    self.expire()
            if self.poweroff_at and now >= self.poweroff_at:
                self.poweroff_at = 0.0
                self.active = False
                LOG.warning("Timer: desligando o notebook agora (%s)", self.what)
                # Sem shell e sem --force: logind fecha a sessão normalmente.
                try:
                    cmd = shlex.split(os.environ.get("TUBE360_POWEROFF_CMD", "systemctl poweroff"))
                    proc = await asyncio.create_subprocess_exec(*cmd)
                    code = await proc.wait()
                    if code:
                        LOG.error("Timer: poweroff retornou %s", code)
                except Exception as error:
                    LOG.error("Timer: poweroff falhou: %s", type(error).__name__)
            await asyncio.sleep(0.2)

    async def fade_nest(self):
        try:
            while True:
                left = self.remaining()
                gain = min(1.0, max(0.0, left / 60)) if self.active and not self.fired and 0 <= left <= 60 else 1.0
                await nest_volume(v=None, fade=gain)
                await asyncio.sleep(1)
        finally:
            await asyncio.to_thread(nest.sleep_volume, 1.0)


sleep_timer = SleepTimer()


async def lid_guard():
    """Enquanto o Xbox usa o app (até 15 min depois do último contato) ou há timer, fechar a tampa do notebook não o
    suspende: um monitor desligado no botão costuma sumir do HDMI, e aí o logind suspenderia no meio do vídeo. Fora
    disso o comportamento normal volta (inibidor do logind, sem senha; o processo morre junto com o serviço)."""
    hold = None
    try:
        while True:
            busy = sleep_timer.active or bool(remote.last_poll and time.monotonic() - remote.last_poll < 900)
            if busy and hold is None:
                with contextlib.suppress(OSError):
                    hold = await asyncio.create_subprocess_exec(
                        "systemd-inhibit", "--what=handle-lid-switch", "--who=Tube360", "--mode=block",
                        "--why=Xbox usando o servidor", "sleep", "infinity",
                        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
                    LOG.info("Tampa: segurando (Xbox em uso ou timer)")
            elif hold is not None and (not busy or hold.returncode is not None):
                if hold.returncode is None:
                    hold.terminate()
                    await hold.wait()
                hold = None
                LOG.info("Tampa: comportamento normal de volta")
            await asyncio.sleep(10)
    finally:
        if hold is not None and hold.returncode is None:
            hold.terminate()


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
    spawn_background(prefetch_thumbs([i["id"] for i in items]))


background = set()
last_console = [0.0]  # monotonic do último pedido do console; sem console por 30 min, o aquecimento para


def spawn_background(coroutine):
    task = asyncio.create_task(coroutine)
    background.add(task)
    task.add_done_callback(background.discard)
    return task


async def account_request(route, body=None, timeout=25):
    def work():
        data = json.dumps(body).encode() if body is not None else None
        request = URLRequest(ACCOUNT_URL + route, data=data, headers={"Content-Type": "application/json"})
        with urlopen(request, timeout=timeout) as response:
            return json.loads(response.read(8 * 1024 * 1024))
    async with asyncio.timeout(timeout):
        return await asyncio.to_thread(work)


async def account_status():
    try:
        return await account_request("/status", timeout=2)
    except Exception:
        return {"state": "signed_out", "name": "", "handle": "", "code": "", "url": ""}


account_pages = {}
account_locks = {kind: asyncio.Lock() for kind in ("home", "subs", "history", "later")}


async def account_items(kind, offset, n, fresh, identity):
    # As páginas do YouTube variam de tamanho; offset conta os vídeos já filtrados.
    async with asyncio.timeout(25), account_locks[kind]:
        cache = account_pages.get(kind)
        owner = (identity.get("name"), identity.get("handle"))
        if not cache or cache["owner"] != owner or time.monotonic() - cache["stamp"] > 300 or (fresh and offset == 0):
            cache = {"owner": owner, "stamp": time.monotonic(), "items": [], "page": 0, "more": True, "seen": set()}
            account_pages[kind] = cache
        while cache["more"] and len(cache["items"]) < offset + n:
            page = cache["page"]
            result = await account_request(f"/feed?kind={kind}&page={page}&fresh={int(page == 0)}")
            for entry in result["items"]:
                if not ID_PATTERN.fullmatch(entry.get("id", "")) or entry["id"] in cache["seen"]:
                    continue
                item = remember({"id": entry["id"], "duration": max(0, int(entry.get("duration") or 0)),
                    "channel": clean(entry.get("channel")), "title": clean(entry.get("title")),
                    "views": clean(entry.get("views_text")), "pos": max(0, float(entry.get("pos") or 0))})
                cache["items"].append(item)
                cache["seen"].add(item["id"])
            cache["more"] = bool(result.get("more")) and page < 1000
            cache["page"] += 1
        return cache["items"][offset:offset + n]


async def account_watched(video_id, pos=0, progress=False):
    try:
        if (await account_status())["state"] == "signed_in":
            await account_request("/progress" if progress else "/watched", {"id": video_id, "pos": pos})
            account_pages.pop("history", None)
    except Exception:
        LOG.warning("Conta: não foi possível registrar %s.", "a posição" if progress else "a reprodução")


def localized_views(value, lang):
    # Os tiles TV vêm no idioma do config; o console pode pedir outro idioma por lista.
    raw = clean(value).replace("\u00a0", " ")
    if raw.lower() in ("no views", "nenhuma visualização", "sem visualizações"):
        return tr("Nenhuma visualização", "No views", lang)
    match = re.fullmatch(r"([\d.,]+)\s*(K|M|B|mil|mi(?:lhões)?|bilhões)?\s*(?:de\s+)?(?:views?|visualizaç(?:ões|ão))", raw, re.I)
    if not match:
        return raw
    number, unit = match.groups()
    unit = (unit or "").lower()
    divisor = {"k": 1000, "mil": 1000, "m": 1000000, "mi": 1000000, "milhões": 1000000,
               "b": 1000000000, "bilhões": 1000000000}.get(unit, 1)
    number = number.replace(",", ".") if divisor > 1 else number.replace(",", "").replace(".", "")
    return views_text(float(number) * divisor, lang)


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
    # plugins do yt-dlp (PO token) carregados uma vez aqui: em paralelo, duas threads registravam o mesmo provedor
    with contextlib.suppress(Exception):
        load_all_plugins()
    warm = asyncio.create_task(warm_loop()) if os.environ.get("TUBE360_WARM_CACHE", "1") != "0" else None
    timer = asyncio.create_task(sleep_timer.run())
    fade = asyncio.create_task(sleep_timer.fade_nest())
    lid = asyncio.create_task(lid_guard())
    yield
    timer.cancel()
    fade.cancel()
    lid.cancel()
    await asyncio.gather(timer, fade, lid, return_exceptions=True)
    if warm:
        warm.cancel()
    with anyio.CancelScope(shield=True):
        await streams.begin()
        for job in list(audio_jobs.values()):
            if job.returncode is None:
                with contextlib.suppress(ProcessLookupError):
                    job.terminate()
        await asyncio.gather(*(job.wait() for job in list(audio_jobs.values())), return_exceptions=True)
        for task in list(background):
            task.cancel()
        await asyncio.gather(*background, return_exceptions=True)


app = FastAPI(title="YouTube 360", lifespan=lifespan)


@app.exception_handler(RequestValidationError)
async def parameter_error(request: Request, error: RequestValidationError):
    # Mesmo parâmetros inválidos preservam o tipo de erro combinado com o console.
    if request.url.path == "/api/feed":
        return PlainTextResponse("erro\t" + tr("Parâmetros de feed inválidos.", "Invalid feed parameters.",
            request.query_params.get("lang") if request.query_params.get("lang") in ("pt", "en") else None) + "\n")
    if request.url.path.startswith("/api/thumb/"):
        return PlainTextResponse("Miniatura indisponível.\n", status_code=404)
    return await request_validation_exception_handler(request, error)


def account_line(status):
    return "ok\t" + "\t".join(clean(status.get(k, "")) for k in ("state", "name", "code", "url")) + "\n"


@app.get("/api/account", response_class=PlainTextResponse)
async def account_info():
    return PlainTextResponse(account_line(await account_status()), headers={"Cache-Control": "no-store"})


async def account_action(action):
    account_pages.clear()
    try:
        return await account_request("/" + action, {})
    except Exception:
        return {"state": "signed_out", "name": "", "code": "", "url": ""}


@app.get("/api/video/{video_id}/next", response_class=PlainTextResponse)
async def video_next(video_id: str):
    """Like da conta e o próximo da reprodução automática: ok, like, id, canal, título."""
    valid_id(video_id)
    try:
        data = await account_request(f"/next?id={video_id}", timeout=15)
    except Exception:
        return PlainTextResponse("erro\n", headers={"Cache-Control": "no-store"})
    nxt = data.get("next") or {}
    if nxt.get("id"):
        valid_id(nxt["id"])
    fields = [data.get("like", ""), nxt.get("id", ""), nxt.get("channel", ""), nxt.get("title", "")]
    return PlainTextResponse("ok\t" + "\t".join(clean(v) for v in fields) + "\n", headers={"Cache-Control": "no-store"})


@app.get("/api/video/{video_id}/rate", response_class=PlainTextResponse)
async def video_rate(request: Request, video_id: str, r: Literal["like", "dislike", "none"]):
    account_native_request(request)
    valid_id(video_id)
    try:
        data = await account_request(f"/rate?id={video_id}&rating={r}", {}, timeout=15)
    except Exception:
        return PlainTextResponse("erro\n", headers={"Cache-Control": "no-store"})
    return PlainTextResponse(f"ok\t{clean(data.get('like', ''))}\n", headers={"Cache-Control": "no-store"})


@app.get("/api/account/login", response_class=PlainTextResponse)
async def account_login(request: Request):
    account_native_request(request)
    return PlainTextResponse(account_line(await account_action("login")), headers={"Cache-Control": "no-store"})


@app.get("/api/account/logout", response_class=PlainTextResponse)
async def account_logout(request: Request):
    account_native_request(request)
    return PlainTextResponse(account_line(await account_action("logout")), headers={"Cache-Control": "no-store"})


def account_native_request(request):
    # GET legado do Xbox: formulários/navegações não podem acionar mudanças na conta.
    if (request.headers.get("user-agent") != "Tube360/1.0" or request.headers.get("origin") or
            request.headers.get("sec-fetch-site")):
        raise HTTPException(403, "Native client required.")


@app.post("/api/account/login")
async def account_login_form(request: Request):
    same_origin_request(request)
    await account_action("login")
    return RedirectResponse("/", status_code=303, headers={"Cache-Control": "no-store"})


def same_origin_request(request):
    if (request.headers.get("origin") != f"{request.url.scheme}://{request.url.netloc}" or
            request.headers.get("sec-fetch-site", "") not in ("", "same-origin")):
        raise HTTPException(403, "Invalid account request origin.")


def sleep_response():
    return PlainTextResponse(sleep_timer.line(), headers={"Cache-Control": "no-store"})


@app.get("/api/sleep", response_class=PlainTextResponse)
async def sleep_info():
    return sleep_response()


@app.get("/api/sleep/set", response_class=PlainTextResponse)
async def sleep_set(request: Request, min: float | None = None, end: int = Query(0, ge=0, le=1),
                    what: Literal["xbox", "xbox_notebook"] = "xbox", target_only: int = Query(0, ge=0, le=1)):
    account_native_request(request)
    if target_only:
        if min is not None or end or not sleep_timer.active or sleep_timer.fired:
            raise HTTPException(409, "No running sleep timer.")
        sleep_timer.what = what
        return sleep_response()
    if (end and min is not None) or (not end and (min is None or not math.isfinite(min) or
            not (0 < min <= 240) or (not sleep_timer.test and (min < 15 or not min.is_integer())))):
        raise HTTPException(400, "Invalid sleep duration.")
    sleep_timer.set(min, bool(end), what)
    return sleep_response()


@app.get("/api/sleep/add", response_class=PlainTextResponse)
async def sleep_add(request: Request, min: int = Query(15, ge=15, le=15)):
    account_native_request(request)
    if not sleep_timer.active or sleep_timer.fired:
        raise HTTPException(409, "No running sleep timer.")
    left = sleep_timer.remaining()
    if left < 0:
        raise HTTPException(409, "Unknown video duration.")
    sleep_timer.mode = "minutos"
    sleep_timer.deadline = time.monotonic() + min * 60 + left
    return sleep_response()


@app.get("/api/sleep/cancel", response_class=PlainTextResponse)
async def sleep_cancel(request: Request):
    account_native_request(request)
    sleep_timer.cancel()
    LOG.info("Timer: cancelado")
    return sleep_response()


@app.post("/api/sleep/cancel")
async def sleep_cancel_form(request: Request):
    same_origin_request(request)
    sleep_timer.cancel()
    LOG.info("Timer: cancelado pela página de status")
    return RedirectResponse("/", status_code=303, headers={"Cache-Control": "no-store"})


@app.get("/")
async def index():
    brand = escape(clean(CONFIG.get("brand") or "Tube360"))
    status = await account_status()
    state = status["state"]
    account_name = escape(status.get("name") or tr("Conta conectada", "Account connected"))
    account_text = {"signed_out": tr("Sem conta conectada", "Signed out"),
                    "pending": tr("Aguardando confirmação", "Waiting for confirmation"),
                    "signed_in": account_name}.get(state, "")
    account_detail = ""
    if state == "pending":
        url = status.get("url", "")
        if not url.startswith("https://"):
            url = "https://www.google.com/device"
        account_detail = f'<p class="code">{escape(status.get("code", ""))}</p><p><a href="{escape(url, quote=True)}">{escape(url)}</a></p>'
    elif state == "signed_out":
        account_detail = '<form method="post" action="/api/account/login"><button>' + tr("Conectar YouTube", "Sign in to YouTube") + '</button></form>'
    age = max(0, int(time.monotonic() - remote.last_poll)) if remote.last_poll else None
    contact = (tr("Há", "Last seen") + f" {age} s" + tr("", " ago")) if age is not None else tr("Nenhum contato ainda", "No contact yet")
    if age is not None and age < 5:
        contact = tr("Conectado agora", "Connected now")
    cfg = CONFIG.get("nest") or {}
    speaker = escape(clean(cfg.get("name") or "Nest")) if cfg.get("host") else tr("Não configurado", "Not configured")
    code = ""
    try:
        if time.time() - CAST_CODE.stat().st_mtime < 330:
            code = clean(CAST_CODE.read_text())
    except OSError:
        pass
    cast = f'<span class="code">{escape(code)}</span>' if code else tr("Receptor indisponível", "Receiver unavailable")
    left = sleep_timer.remaining()
    timer_text = tr("Desligado", "Off")
    timer_form = ""
    if sleep_timer.active:
        target = tr("Xbox e notebook", "Xbox and notebook") if sleep_timer.what == "xbox_notebook" else tr("Só o Xbox", "Xbox only")
        when = tr("No fim do vídeo", "At end of video") if sleep_timer.mode == "fim_do_video" else f"{left // 60}:{left % 60:02d}"
        if sleep_timer.fired:
            when = tr("Xbox desligando; notebook em instantes", "Xbox shutting down; notebook shortly")
        timer_text = f"{target} · {when}"
        timer_form = '<form method="post" action="/api/sleep/cancel"><button>' + tr("Cancelar timer", "Cancel timer") + '</button></form>'
    html = f'''<!doctype html><html lang="{LANGUAGE}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="15">
<title>{brand} · Status</title><style>
*{{box-sizing:border-box}}body{{margin:0;background:#101014;color:#f5f5f7;font:16px/1.6 system-ui,sans-serif}}
main{{max-width:680px;margin:7vh auto;padding:24px}}header{{border-bottom:2px solid #ff0f3c;padding-bottom:20px}}
h1{{font-size:32px;letter-spacing:-1px;margin:0}}h2{{font-size:14px;font-weight:500;color:#a5a5b4;margin:0 0 7px}}
p{{margin:6px 0}}.online{{color:#ff627f}}section{{padding:23px 0;border-bottom:1px solid #30303a}}
a{{color:#ff627f;overflow-wrap:anywhere}}.code{{font:600 24px/1.6 ui-monospace,monospace;letter-spacing:2px}}
button{{margin-top:12px;background:#ff0f3c;color:#fff;border:0;border-radius:7px;padding:12px 18px;font:600 15px system-ui;cursor:pointer}}
a:focus-visible,button:focus-visible{{outline:2px solid white;outline-offset:4px}}footer{{color:#a5a5b4;font-size:13px;margin-top:24px}}
</style></head><body><main><header><h1>{brand}</h1><p class="online">● {tr("Servidor no ar", "Server online")}</p></header>
<section><h2>Xbox 360</h2><p>{contact}</p></section><section><h2>Google Nest</h2><p>{speaker}</p></section>
<section><h2>{tr("Timer de desligar", "Sleep timer")}</h2><p>{timer_text}</p>{timer_form}</section>
<section><h2>{tr("Transmitir · código de TV", "Cast · TV code")}</h2><p>{cast}</p></section>
<section><h2>{tr("Conta do YouTube", "YouTube account")}</h2><p>{account_text}</p>{account_detail}</section>
<footer>{tr("Esta página atualiza a cada 15 segundos.", "This page refreshes every 15 seconds.")}</footer></main></body></html>'''
    return HTMLResponse(html, headers={"Cache-Control": "no-store"})


@app.get("/api/feed", response_class=PlainTextResponse)
async def feed(kind: str = "home", q: str = "", n: int = Query(24, ge=1, le=100), fresh: int = Query(0, ge=0, le=1),
               offset: int = Query(0, ge=0, le=5000), lang: str | None = None):
    last_console[0] = time.monotonic()
    lang = lang or LANGUAGE
    if lang not in ("pt", "en"):
        return PlainTextResponse("erro\t" + tr("Idioma inválido.", "Invalid language.") + "\n")
    if kind not in ("home", "search", "subs", "history", "later") or (kind == "search" and not clean(q)):
        return PlainTextResponse("erro\t" + tr("Informe uma busca válida.", "Enter a valid search.", lang) + "\n")
    query = clean(q)[:250]
    titles = {"home": tr("Para assistir hoje", "Recommended", lang), "search": tr("Busca: ", "Search: ", lang) + query,
              "subs": tr("Inscrições", "Subscriptions", lang), "history": tr("Continuar assistindo", "Continue watching", lang),
              "later": tr("Assistir mais tarde", "Watch later", lang)}
    items = None
    try:
        if kind != "search":
            status = await account_status()
            if status["state"] == "signed_in":
                try:
                    items = await account_items(kind, offset, n, fresh, status)
                except Exception:
                    if kind != "home":
                        raise
                    LOG.warning("Conta: home indisponível; usando buscas do config.")
            else:
                account_pages.clear()
                if kind in ("subs", "later"):
                    return PlainTextResponse("erro\t" + tr("Conecte sua conta do YouTube.", "Sign in to your YouTube account.", lang) + "\n")
                if kind == "history":
                    items = []
                    for entry in history.recent(300)[offset:offset + n]:
                        when = time.strftime("%d/%m" if lang == "pt" else "%m/%d", time.localtime(entry["ts"]))
                        items.append({"id": entry["id"], "duration": int(entry.get("dur") or 0),
                            "channel": entry.get("channel", ""), "views": tr("Visto em ", "Watched on ", lang) + when,
                            "title": entry.get("title", entry["id"]), "pos": entry.get("pos", 0)})
        if items is None:
            if kind == "home":
                base = await home_videos(bool(fresh), lang)
                extra = home_extra[lang]
                if offset + n > len(base) + len(extra):
                    await home_more([i["id"] for i in base + extra], lang)
                everything = base + extra
            else:
                if fresh:
                    for key in list(search_cache.values):
                        if key[0] == query and key[2] == lang:
                            search_cache.values.pop(key, None)
                count = 100
                everything = await search_videos(query, count, lang)
                while offset + n > len(everything) and len(everything) >= count and count < 400:
                    count *= 2
                    everything = await search_videos(query, count, lang)
            items = everything[offset:offset + n]
    except Exception:
        LOG.warning("Não foi possível carregar o feed %s.", kind)
        return PlainTextResponse("erro\t" + tr("Não foi possível consultar o YouTube. Tente novamente.", "Could not reach YouTube. Try again.", lang) + "\n")
    prefetch_later(items)
    lines = [f"ok\t{len(items)}\t{titles[kind]}"]
    for item in items:
        fields = [item["id"], item["duration"], item["channel"], localized_views(item["views"], lang), item["title"]]
        lines.append("\t".join([*(clean(v) for v in fields), f"{item.get('pos', history.position(item['id'])):.1f}"]))
    return PlainTextResponse("\n".join(lines) + "\n", headers={"Cache-Control": "no-store"})


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
    for attempt in range(2):
        try:
            info = await resolve_video(video_id)
        except Exception as error:
            reason = unplayable_reason(error)
            LOG.info("vídeo %s não abriu (%s): %s", video_id, reason or "erro",
                     re.sub(r"https?://\S+", "<url>", clean(str(error)))[:200])
            raise HTTPException(502, "Não foi possível abrir este vídeo. Tente novamente.",
                                headers={"X-Error": reason} if reason else None) from None
        if await request.is_disconnected():
            raise HTTPException(499, "Conexão encerrada.")
        if info["duration"] and t >= info["duration"]:
            raise HTTPException(416, "Posição após o fim do vídeo.")
        mp3 = None
        if info["live"] and not a:
            # Live com o Nest: o som sai do mesmo ffmpeg da imagem. Dois leitores do HLS podem começar em segmentos
            # diferentes (5 s cada), e o servidor não teria como alinhar depois.
            AUDIO_DIR.mkdir(parents=True, exist_ok=True)
            mp3 = AUDIO_DIR / f"{video_id}_live.part.mp3"
            mp3.unlink(missing_ok=True)
        encoder = await streams.start(ticket, ffmpeg_command(info, t, q, with_audio=bool(a), mp3=mp3))
        if mp3:
            live_audio[video_id] = (mp3, encoder.process)
        try:
            # Só confirmar HTTP 200 depois de receber MPEG; erro de origem vira 502.
            first_read = asyncio.create_task(encoder.output.get())
            deadline = time.monotonic() + 40
            while not first_read.done():
                if await request.is_disconnected() or time.monotonic() > deadline:
                    raise HTTPException(504, "O vídeo demorou para responder.")
                await asyncio.wait({first_read}, timeout=0.2)
            first = first_read.result()
        except BaseException:
            first_read.cancel()
            with anyio.CancelScope(shield=True):
                await streams.finish(encoder)
            drop_live_audio(video_id, encoder)
            raise
        if first:
            break
        # A URL assinada foi recusada (o YouTube às vezes responde 403): resolve de novo e tenta mais uma vez.
        url_cache.values.pop(video_id, None)
        with anyio.CancelScope(shield=True):
            await streams.finish(encoder)
        drop_live_audio(video_id, encoder)
        if attempt:
            raise HTTPException(502, "A origem do vídeo não respondeu. Tente novamente.")
        LOG.info("stream %s: a origem recusou, buscando um link novo", video_id)

    async def body():
        try:
            yield first
            while chunk := await encoder.output.get():
                yield chunk
        finally:
            with anyio.CancelScope(shield=True):
                await streams.finish(encoder)
            drop_live_audio(video_id, encoder)

    return StreamingResponse(body(), media_type="video/mpeg", headers={
        "X-Duration": str(info["duration"]), "X-Live": "1" if info["live"] else "0", "X-Title": quote(info["title"], safe=""),
        "Cache-Control": "no-store", "X-Accel-Buffering": "no",
    })


AUDIO_DIR = Path.home() / ".cache/tube360/audio"
audio_jobs = {}  # chave (id, t) -> processo ffmpeg gravando o MP3 provisório
audio_lock = asyncio.Lock()
live_audio = {}  # id -> (MP3, processo): na live, o ffmpeg da imagem grava o som do Nest (ver stream)


def drop_live_audio(video_id, encoder):
    entry = live_audio.get(video_id)
    if entry and entry[1] is encoder.process:
        live_audio.pop(video_id, None)
        entry[0].unlink(missing_ok=True)  # o Nest que ainda lê segue até o fim do que já foi gravado


def finish_audio(key, job, path, partial):
    if audio_jobs.get(key) is not job:
        return
    if job.returncode == 0 and partial.exists() and partial.stat().st_size:
        partial.replace(path)
    else:
        partial.unlink(missing_ok=True)
        url_cache.values.pop(key[0], None)
    audio_jobs.pop(key, None)


async def audio_file(video_id, t):
    """Só um ffmpeg por trecho. O Nest lê o provisório; só uma conversão completa vira cache."""
    live = live_audio.get(video_id)
    if live and live[1].returncode is None:
        return live
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    key = (video_id, round(t, 2))
    path = AUDIO_DIR / f"{video_id}_{key[1]:.2f}.mp3"
    partial = path.with_suffix(".part.mp3")
    async with audio_lock:
        job = audio_jobs.get(key)
        if job is not None and job.returncode is not None:
            finish_audio(key, job, path, partial)
            job = None
        if job is not None:
            return partial, job
        if partial.exists():
            # Sobrou de uma interrupção: nunca reaproveitar como arquivo completo.
            partial.unlink(missing_ok=True)
            url_cache.values.pop(video_id, None)
        if path.exists() and path.stat().st_size == 0:
            path.unlink(missing_ok=True)
            url_cache.values.pop(video_id, None)
        if path.exists():
            return path, None
        info = await resolve_video(video_id)
        if info["live"]:
            # um leitor à parte começaria noutro segmento e viraria cache de um trecho velho
            raise RuntimeError("live sem a imagem aberta no console")
        active = {f"{i}_{start:.2f}.part.mp3" for i, start in audio_jobs} | {p.name for p, _ in live_audio.values()}
        for old in AUDIO_DIR.glob("*.part.mp3"):
            if old.name not in active:
                old.unlink(missing_ok=True)
        complete = [f for f in AUDIO_DIR.glob("*.mp3") if not f.name.endswith(".part.mp3")]
        for old in sorted(complete, key=lambda f: f.stat().st_mtime)[:-6]:
            old.unlink(missing_ok=True)
        cmd = audio_command(info, t)[:-1] + ["-y", str(partial)]
        job = await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        audio_jobs[key] = job

        async def reap():
            await job.wait()
            finish_audio(key, job, path, partial)
        spawn_background(reap())
        return partial, job


@app.get("/api/audio/{video_id}")
async def audio(request: Request, video_id: str, t: float = Query(0, ge=0, le=86400 * 30)):
    valid_id(video_id)
    if not math.isfinite(t):
        raise HTTPException(400, "Posição inválida.")
    try:
        path, job = await audio_file(video_id, t)
    except Exception:
        raise HTTPException(502, "Não foi possível abrir este áudio.") from None
    start = 0
    match = re.fullmatch(r"bytes=(\d+)-(\d*)", request.headers.get("range", ""))
    if match:
        start = int(match.group(1))
    LOG.info("áudio do Nest: %s t=%.1f desde o byte %d%s", video_id, t, start, " (convertendo)" if job else "")
    deadline = time.monotonic() + 30
    while not path.exists() or path.stat().st_size <= start:
        if job is None or job.returncode is not None or time.monotonic() > deadline:
            break
        await asyncio.sleep(0.1)
    # O reap pode ter renomeado entre a chamada e o primeiro byte da resposta.
    if not path.exists() and path.name.endswith(".part.mp3"):
        path = path.with_name(path.name.replace(".part.mp3", ".mp3"))
    try:
        source = path.open("rb")
    except OSError:
        raise HTTPException(502, "Não foi possível abrir este áudio.") from None
    size = os.fstat(source.fileno()).st_size
    if not size:
        source.close()
        raise HTTPException(502, "Não foi possível abrir este áudio.")
    if start >= size:
        source.close()
        raise HTTPException(416, "Trecho indisponível.", headers={"Content-Range": f"bytes */{size}"})
    done = job is None or job.returncode is not None
    end = size - 1 if match else None
    if match and match.group(2):
        end = min(end, int(match.group(2)))
        if end < start:
            source.close()
            raise HTTPException(416, "Trecho inválido.", headers={"Content-Range": f"bytes */{size}"})

    async def body():
        pos = start
        try:
            source.seek(pos)
            while True:
                want = 65536 if end is None else min(65536, end + 1 - pos)
                if want <= 0:
                    break
                chunk = source.read(want)
                if chunk:
                    pos += len(chunk)
                    yield chunk
                    continue
                if end is not None or job is None or job.returncode is not None:
                    break
                await asyncio.sleep(0.2)
        finally:
            source.close()

    headers = {"Cache-Control": "no-store", "Accept-Ranges": "bytes"}
    if match:
        headers["Content-Range"] = f"bytes {start}-{end}/{size if done else '*'}"
    return StreamingResponse(body(), status_code=206 if match else 200, media_type="audio/mpeg", headers=headers)


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
        self.token = ""  # marca "s=<ms>" da URL do último áudio pedido ao Nest
        self.recover_at = 0.0
        self.sleep_base = None
        self.want_stop = False  # o stop não chegou ao Nest (Wi-Fi caído): o watch repete quando a conexão voltar
        threading.Thread(target=self.watch, daemon=True).start()

    def connected(self):
        return self.cc is not None and self.cc.socket_client.is_connected

    def drop(self):
        cc, self.cc = self.cc, None
        if cc is not None:
            try:
                cc.disconnect(timeout=1)
            except Exception:
                pass

    def connect(self):
        """Um cliente só, que a pychromecast reconecta sozinha depois de queda de Wi-Fi. Trocar o cliente sem desligar
        o antigo deixava vários vivos ao mesmo tempo (3 heartbeats caindo no mesmo instante, 2026-11-23)."""
        cc = self.cc
        if cc is not None and not cc.socket_client.stop.is_set():
            deadline = time.monotonic() + 4
            while not cc.socket_client.is_connected and time.monotonic() < deadline:
                time.sleep(0.1)
            if cc.socket_client.is_connected:
                return cc
        # morto, ou reconectando com espera longa (a pychromecast dobra o intervalo a cada falha): começa de novo
        self.drop()
        cfg = CONFIG.get("nest") or {}
        host = cfg.get("host")
        if not host:
            raise RuntimeError("Nenhum Nest configurado (config.json, nest.host).")
        info = CastInfo(services={HostServiceInfo(host, 8009)}, uuid=uuid.uuid4(), model_name="Nest",
                        friendly_name=cfg.get("name", "Nest"), host=host, port=8009, cast_type="audio", manufacturer="Google")
        cc = pychromecast.get_chromecast_from_cast_info(info, zconf=None, tries=None, retry_wait=1, timeout=6)
        try:
            cc.wait(timeout=10)
        except Exception as error:
            try:
                cc.disconnect(timeout=1)
            except Exception:
                pass
            LOG.warning("Nest: conexão falhou: %s", error)
            raise
        self.cc = cc
        return cc

    def media_ready(self, cc):
        """O receptor padrão aberto e com o canal de mídia pronto. Sem isso, qualquer pedido do media_controller
        (update_status inclusive) faz a pychromecast relançar o receptor, e relançar corta o som que está tocando:
        depois de cada queda de Wi-Fi o laço relançava o app a cada 250 ms (2026-11-23)."""
        return cc.status is not None and cc.status.app_id == self.APP \
            and cc.media_controller.namespace in cc.socket_client.app_namespaces

    def play(self, url):
        with self.lock:
            self.want_stop = False
            self.state, self.pos, self.stamp, self.error = "LOADING", 0.0, time.monotonic(), ""
            try:
                t0 = time.monotonic()
                cc = self.connect()
                LOG.info("Nest: conectado em %.1f s, app %s", time.monotonic() - t0, cc.status.app_id)
                # Reabre o receptor padrão só se ele não é nosso (Bluetooth, outro app, ou aberto por outro remetente):
                # aberto sem sessão nossa, o Nest ignora o LOAD calado (medido em 2026-11-21). Se o LOAD não pegar em 9 s,
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
                    # 9 s na primeira: começando no meio, o primeiro áudio leva até ~4 s; com 6 s o LOAD lento era
                    # tomado por ignorado, o receptor era reaberto e o Nest caía (2026-11-22, 12:46)
                    deadline = time.monotonic() + (12 if tentativa else 9)
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

    def volume(self, level):
        with self.lock:
            try:
                if self.sleep_base is not None:
                    self.sleep_base = level / 100
                self.connect().set_volume(max(0.0, min(1.0, level / 100)))
            except Exception as error:
                LOG.warning("Nest volume: %s", error)

    def sleep_volume(self, gain):
        with self.lock:
            if self.sleep_base is None and (gain >= 1 or not self.active or self.cc is None):
                return
            try:
                if self.sleep_base is None:
                    self.sleep_base = float(self.cc.status.volume_level)
                self.cc.set_volume(self.sleep_base * gain)
                if gain >= 1:
                    self.sleep_base = None
            except Exception as error:
                LOG.warning("Nest timer volume: %s", type(error).__name__)

    def command(self, what):
        with self.lock:
            if what == "stop":
                self.active = False
                self.video_id = ""
                self.state, self.pos = "IDLE", 0.0
                if self.cc is None:
                    self.want_stop = False
                    return
                if not self.connected():
                    # sem esperar a conexão voltar segurando a trava (o próximo vídeo pediria o Nest atrás dela)
                    self.want_stop = True
                    LOG.info("Nest: sem conexão para parar; repito quando ela voltar")
                    return
            try:
                cc = self.connect()
                if not self.media_ready(cc):
                    # outro app no Nest, ou o receptor fechado: não há som nosso; relançar só para parar tocaria nada
                    if what == "stop":
                        self.want_stop = False
                    return
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
                    self.want_stop = False
            except Exception as error:
                LOG.warning("Nest %s: %s", what, error)
                if what == "stop":
                    self.want_stop = True

    def watch(self):
        orphan_since = stop_try = 0.0
        while True:
            time.sleep(0.25)
            if self.want_stop and self.connected() and time.monotonic() - stop_try > 3 and not self.lock.locked():
                stop_try = time.monotonic()
                LOG.info("Nest: conexão de volta; parando o som que ficou tocando")
                threading.Thread(target=self.command, args=("stop",), daemon=True).start()
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
                cc = self.cc
                if not cc.socket_client.is_connected or cc.status is None:
                    continue  # Wi-Fi caiu: o Nest costuma seguir tocando; o console segue no último relógio bom
                took_over = cc.status.app_id not in (None, self.APP)
                mc = cc.media_controller
                if not took_over and not self.media_ready(cc):
                    continue  # logo depois de reconectar, o canal de mídia ainda não voltou (ver media_ready)
                if not took_over:
                    mc.update_status()
                st = mc.status
                if st.player_state == "UNKNOWN" and not took_over:
                    continue  # status zerado pela reconexão: não é IDLE, a resposta do Nest ainda não chegou
                if st.player_state != getattr(self, "last_logged", None):
                    LOG.info("Nest: estado %s (pos %.1f, motivo %s)", st.player_state, float(st.current_time or 0) + self.t0, st.idle_reason)
                    self.last_logged = st.player_state
                # Logo depois do LOAD o Nest ainda informa o som anterior (pausado, por exemplo): o console tomava esse
                # estado como do vídeo novo. Só vale o status do áudio pedido por último (marca s= única na URL).
                current = self.token in (st.content_id or "")
                if st.player_state in ("PLAYING", "PAUSED", "BUFFERING") and not took_over and current:
                    # update_status() só pede: st é a última resposta, que pode ter 250 ms. A posição vale para a hora
                    # em que ela chegou (last_updated), não para agora; carimbar com agora atrasava a imagem.
                    age = (datetime.now(timezone.utc) - st.last_updated).total_seconds() if st.last_updated else 0.0
                    self.state, self.pos, self.stamp = st.player_state, float(st.current_time or 0), time.monotonic() - max(0.0, age)
                elif self.state not in ("LOADING",):
                    self.state = "IDLE"
                    # caiu no meio (Bluetooth ou outro app tomou o Nest, Wi-Fi): toca de novo do ponto em que o Xbox está
                    if self.video_id and (took_over or st.idle_reason in ("INTERRUPTED", "ERROR", "CANCELLED")) \
                            and time.monotonic() - self.recover_at > 8 and remote.playing == self.video_id:
                        self.recover_at = time.monotonic()
                        LOG.warning("Nest: sessão caiu (%s); retomando em %.1f s", "outro app" if took_over else st.idle_reason, remote.pos)
                        self.ours = False
                        self.t0 = remote.pos
                        stamp = int(time.time() * 1000)
                        self.token = f"s={stamp}"
                        url = f"http://{self.host_ip}:{PORT}/api/audio/{self.video_id}?t={self.t0:.2f}&s={stamp}"
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
    return PlainTextResponse(f"ok\t{clean(cfg.get('name', 'Nest'))}\n" if cfg.get("host") else "erro\t" + tr("Nenhum Nest configurado.", "No Nest configured.") + "\n")


@app.get("/api/nest/play", response_class=PlainTextResponse)
async def nest_play(video_id: str = Query(..., alias="id"), t: float = Query(0, ge=0, le=86400 * 30)):
    valid_id(video_id)
    host = (CONFIG.get("nest") or {}).get("host")
    if not host:
        return PlainTextResponse("erro\t" + tr("Nenhum Nest configurado.", "No Nest configured.") + "\n")
    nest.host_ip = lan_ip_towards(host)
    stamp = int(time.time() * 1000)
    url = f"http://{nest.host_ip}:{PORT}/api/audio/{video_id}?t={t:.2f}&s={stamp}"
    nest.state, nest.pos, nest.stamp = "LOADING", 0.0, time.monotonic()
    nest.video_id, nest.t0, nest.token = video_id, t, f"s={stamp}"
    threading.Thread(target=nest.play, args=(url,), daemon=True).start()
    return PlainTextResponse("ok\n")


@app.get("/api/nest/prepare", response_class=PlainTextResponse)
async def nest_prepare():
    if not (CONFIG.get("nest") or {}).get("host"):
        return PlainTextResponse("erro\t" + tr("Nenhum Nest configurado.", "No Nest configured.") + "\n")
    threading.Thread(target=nest.prepare, daemon=True).start()
    return PlainTextResponse("ok\n")


@app.get("/api/nest/pos", response_class=PlainTextResponse)
async def nest_pos():
    state, pos = nest.position()
    return PlainTextResponse(f"{state}\t{pos:.3f}\t{clean(nest.error)}\n")


@app.get("/api/nest/volume", response_class=PlainTextResponse)
async def nest_volume(v: float | None = Query(None, ge=0, le=100), fade: float | None = Query(None, ge=0, le=1)):
    if fade is not None:
        await asyncio.to_thread(nest.sleep_volume, fade)
    elif v is not None:
        threading.Thread(target=nest.volume, args=(v,), daemon=True).start()
    else:
        raise HTTPException(400, "Volume required.")
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
    text = tr(f"Celular conectado: {name}", f"Phone connected: {name}") if event.kind == "connect" else tr(f"{name} desconectou o celular", f"Phone disconnected: {name}")
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
    return PlainTextResponse("erro\t" + tr("O receptor do YouTube não está ligado.", "The YouTube receiver is offline.") + "\n")


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
        remote.commands.append(cmd.model_dump(exclude_none=True) | {"at": time.monotonic()})
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
    # sem o limite, um vídeo aberto segura o desligamento até o systemd matar o processo
    uvicorn.run(app, host="0.0.0.0", port=PORT, http="h11", access_log=False, timeout_graceful_shutdown=3)
