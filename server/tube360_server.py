#!/usr/bin/env python3
"""Servidor do Tube360. Contrato: ../docs/PROTOCOL.md."""

import asyncio
import contextlib
import io
import hashlib
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
from fastapi.responses import FileResponse, HTMLResponse, PlainTextResponse, RedirectResponse, Response, StreamingResponse
from PIL import Image, ImageOps
from pydantic import BaseModel, ConfigDict, Field
from typing import Literal

import pychromecast
from pychromecast.models import CastInfo, HostServiceInfo

BASE = Path(__file__).resolve().parent
# config.json é seu (fica fora do Git); sem ele, vale o config.example.json
CONFIG_PATH = Path(os.environ.get("TUBE360_CONFIG", BASE / "config.json"))
CONFIG = json.loads((CONFIG_PATH if CONFIG_PATH.exists() else BASE / "config.example.json").read_text(encoding="utf-8"))
LANGUAGE = CONFIG.get("language") if CONFIG.get("language") in ("pt", "en", "es") else "en"
PORT = int(os.environ.get("TUBE360_PORT", "8091"))
ACCOUNT_URL = os.environ.get("TUBE360_ACCOUNT_URL", "http://127.0.0.1:8099").rstrip("/")
THUMBS = Path.home() / ".cache/tube360/thumbs"
FFMPEG = shutil.which("ffmpeg") or "/usr/bin/ffmpeg"
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{11}$")
LOG = logging.getLogger("tube360")
SIZES = {1080: (1920, 1080, "12000k"), 720: (1280, 720, "5500k"), 480: (854, 480, "2500k"), 360: (640, 360, "1500k")}
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


SPANISH = {
    " ago": " atrás",
    "Account connected": "Cuenta conectada",
    "Album": "Álbum",
    "At end of video": "Al terminar el video",
    "Cancel timer": "Cancelar temporizador",
    "Cast · TV code": "Transmitir · Código de TV",
    "Connected now": "Conectado ahora",
    "Continue watching": "Seguir viendo",
    "Could not reach YouTube. Press X to retry.": "No se pudo conectar con YouTube. Presiona X para reintentar.",
    "Could not reach YouTube. Try again.": "No se pudo conectar con YouTube. Inténtalo de nuevo.",
    "Enter a valid search.": "Ingresa una búsqueda válida.",
    "Invalid feed parameters.": "Parámetros de lista no válidos.",
    "Invalid language.": "Idioma no válido.",
    "Last seen": "Última conexión",
    "No Nest configured.": "No hay un Nest configurado.",
    "No contact yet": "Sin conexión todavía",
    "No views": "Sin vistas",
    "Not configured": "Sin configurar",
    "Off": "Desactivado",
    "Playback cancelled.": "Reproducción cancelada.",
    "Receiver unavailable": "Receptor no disponible",
    "Recommended": "Recomendados",
    "Search: ": "Búsqueda: ",
    "Server online": "Servidor conectado",
    "Sign in to YouTube": "Iniciar sesión en YouTube",
    "Sign in to YouTube in Settings.": "Inicia sesión en YouTube desde Configuración.",
    "Sign in to your YouTube account.": "Inicia sesión en tu cuenta de YouTube.",
    "Signed out": "Sesión cerrada",
    "Sleep timer": "Temporizador de apagado",
    "Subscriptions": "Suscripciones",
    "The YouTube receiver is offline.": "El receptor de YouTube está desconectado.",
    "This page refreshes every 15 seconds.": "Esta página se actualiza cada 15 segundos.",
    "Waiting for confirmation": "Esperando confirmación",
    "Watch later": "Ver más tarde",
    "Watched on ": "Visto el ",
    "Xbox and notebook": "Xbox y computadora",
    "Xbox only": "Solo Xbox",
    "Xbox shutting down; notebook shortly": "Xbox se está apagando; la computadora se apagará después",
    "YouTube account": "Cuenta de YouTube",
    "music": "música en español",
    "en-US,en;q=0.9": "es-MX,es;q=0.9",
}


def tr(pt, en, lang=None):
    language = lang or LANGUAGE
    return pt if language == "pt" else SPANISH.get(en, en) if language == "es" else en


def ydl_options(lang=None, cookies=False, skip_clients=(), live_clients=False, **extra):
    lang = lang or LANGUAGE
    youtube_args = {"lang": [lang]}
    if skip_clients or live_clients:
        youtube_args["player_client"] = ["default", *(["mweb"] if live_clients else []),
                                         *(f"-{name}" for name in skip_clients)]
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
    if (lang or LANGUAGE) == "es":
        for divisor, unit in ((1_000_000_000, " mil M"), (1_000_000, " M"), (1000, " mil")):
            if count >= divisor:
                return f"{count / divisor:.1f}".removesuffix(".0") + unit + " vistas"
        return f"{count} vista{'s' if count != 1 else ''}"
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


home_extra = {"pt": [], "en": [], "es": []}  # páginas a mais da home (rolagem sem fim): buscas de temas sorteados, anexadas sem repetir


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


def source_status(info, source, timeout=8):
    headers = info["headers"] | source.get("http_headers", {}) | {"Range": "bytes=0-1"}
    try:
        with urlopen(URLRequest(source["url"], headers=headers), timeout=timeout) as response:
            return response.status
    except HTTPError as error:
        return error.code
    except OSError:
        return None


def audio_catalog(formats):
    result = {}
    for fmt in formats:
        if fmt.get("vcodec") != "none" or fmt.get("acodec") in (None, "none") or not fmt.get("url") or fmt.get("has_drm"):
            continue
        language = clean(fmt.get("language") or "und")
        note = clean(fmt.get("format_note") or "")
        original = "original" in note.lower() or fmt.get("language_preference") == 10
        automatic = "auto" in note.lower()
        code = language + ("-original" if original else "-auto" if automatic else "-dub")
        rank = (fmt.get("ext") == "m4a", float(fmt.get("abr") or fmt.get("tbr") or 0))
        if code not in result or rank > result[code][0]:
            result[code] = (rank, {"code": code, "language": language, "name": note,
                                   "original": original, "automatic": automatic, "format": fmt})
    return [entry[1] for entry in result.values()]


def audio_choice(value):
    if not isinstance(value, str) or not re.fullmatch(r"(?:original|lang:[A-Za-z-]{2,20}|[A-Za-z0-9_-]{1,60})", value):
        raise HTTPException(400, "Faixa de áudio inválida.")
    return value


def choose_audio(info, selection="original"):
    audio_choice(selection)
    tracks = info.get("audio_tracks") or []
    chosen = next((t for t in tracks if t["original"]), None)
    exact = next((t for t in tracks if t["code"] == selection), None)
    chosen = exact or chosen
    if exact is None and not selection.startswith("lang:") and "-" in selection:
        language,kind=selection.rsplit("-",1)
        chosen=next((t for t in tracks if t["language"].split("-")[0]==language.split("-")[0] and t["code"].rsplit("-",1)[-1]==kind),chosen)
    if selection.startswith("lang:"):
        chosen = next((t for t in tracks if t["language"].split("-")[0] == selection[5:].split("-")[0]), chosen)
    if chosen is None:
        chosen = next((t for t in tracks if t["format"]["url"] == info["audio"]["url"]), None)
    # O cache do resolvedor é compartilhado: selecionar uma faixa não pode alterar outra sessão.
    return info | ({"audio": chosen["format"], "audio_selected": chosen["code"], "audio_selection": selection} if chosen else {"audio_selection": selection})


def audio_cache_id(video_id, selection="original"):
    audio_choice(selection)
    return video_id if selection == "original" else video_id + "~" + hashlib.sha256(selection.encode()).hexdigest()[:12]


def hd_source(data, video):
    # O yt-dlp já ordenou os formatos por preferência. A live precisa do seu segmento HLS com áudio.
    if data.get("live_status") == "is_live":
        return None
    for fmt in reversed(data.get("formats") or []):
        height, width = fmt.get("height") or 0, fmt.get("width") or 0
        if (fmt.get("protocol") == "https" and (fmt.get("vcodec") or "").startswith("avc1")
                and not fmt.get("has_drm") and fmt.get("dynamic_range") in (None, "SDR")
                and 0 < (fmt.get("fps") or 0) <= 30
                and (video.get("height") or 0) < height <= 1080 and 0 < width <= 1920
                and (width > 1280 or height > 720)):
            return fmt
    return None


def original_sources(data, video, audio):
    # O remux precisa de AAC direto; a live mantém o caminho HLS com os dois fluxos.
    if (data.get("live_status") == "is_live"
            or not (audio.get("acodec") or "").startswith(("mp4a", "aac"))
            or audio.get("protocol") != "https" or audio.get("has_drm")):
        return {}
    result = {}
    for fmt in [video, *reversed(data.get("formats") or [])]:
        h, w = fmt.get("height") or 0, fmt.get("width") or 0
        for q, (width, height, _) in SIZES.items():
            if (q not in result and 0 < h <= height and 0 < w <= width and (h == height or w == width)
                    and fmt.get("protocol") == "https" and (fmt.get("vcodec") or "").startswith("avc1")
                    and 0 < (fmt.get("fps") or 0) <= (30 if h > 720 else 60.1) and fmt.get("dynamic_range") in (None, "SDR")
                    and not fmt.get("has_drm")):
                result[q] = fmt
    return result


def remux_command(info, video, start, with_audio):
    cmd = [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error", *source_args(info, video, start)]
    separate = with_audio and video["url"] != info["audio"]["url"]
    if separate:
        cmd += source_args(info, info["audio"], start)
    cmd += ["-map", "0:v:0"]
    if with_audio:
        cmd += ["-map", "1:a:0" if separate else "0:a:0"]
    # Um seek copia desde o IDR anterior. O cliente decodifica o pré-roll e apresenta a partir do PTS zero.
    return cmd + ["-c", "copy", "-bsf:v", "h264_mp4toannexb,dump_extra=freq=keyframe",
                  "-mpegts_copyts", "1", "-avoid_negative_ts", "disabled", "-muxdelay", "0",
                  "-flush_packets", "1", "-f", "mpegts", "pipe:1"]


def copy_source(info, quality, mode):
    if not (info["audio"].get("acodec") or "").startswith(("mp4a","aac")) or info["audio"].get("has_drm"):
        return None
    ceiling = min(quality,720) if mode == "720p60" else 1080 if quality >= 720 else quality
    formats = [f for f in info.get("video_formats", []) if f.get("protocol") == "https"
               and (f.get("vcodec") or "").startswith("avc1") and not f.get("has_drm")
               and f.get("dynamic_range") in (None,"SDR")
               and 0 < (f.get("height") or 0) <= ceiling and 0 < (f.get("width") or 0) <= 1920
               and 0 < (f.get("fps") or 0) <= (30 if (f.get("height") or 0) > 720 else 60.1)]
    return max(formats, key=lambda f: (f["height"], mode == "720p60" or f["fps"] <= 30.1, f["fps"]), default=None) or info.get("original", {}).get(min(quality,ceiling))


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
                # Esse formato também serve de reserva quando a fonte de 1080p não abre.
                data = await extract_video(
                    f"https://www.youtube.com/watch?v={video_id}", skip_clients=skip,
                    live_clients=not metadata.get(video_id, {}).get("duration"),
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
            formats = data.get("requested_formats") or [data]
            if data.get("live_status") == "is_live":
                # O mweb traz imagem e som no mesmo segmento. Pedir os clientes na mesma extração também atende
                # transmissões que só o mweb abre. A lista do yt-dlp já está ordenada por preferência crescente.
                muxed = next((f for f in reversed(data.get("formats", []))
                              if (f.get("protocol") or "").startswith("m3u8") and 0 < (f.get("height") or 0) <= 720
                              and (f.get("vcodec") or "").startswith("avc1") and f.get("acodec") not in (None, "none")), None)
                if muxed:
                    formats = [muxed]
                elif len(formats) > 1 and metadata.get(video_id, {}).get("duration"):
                    with contextlib.suppress(Exception):
                        muxed = await extract_video(
                            f"https://www.youtube.com/watch?v={video_id}",
                            extractor_args={"youtube": {"lang": [LANGUAGE], "player_client": ["mweb"]}},
                            format="best[height<=720][vcodec^=avc1][acodec!=none]")
                        formats = [muxed]
            video = next((f for f in formats if f.get("vcodec") != "none"), None)
            audio = next((f for f in formats if f.get("acodec") != "none"), None)
            if not video or not audio:
                raise RuntimeError("vídeo ou áudio indisponível")
            result = {
                "video": video, "audio": audio, "original": original_sources(data, video, audio),
                "duration": 0 if data.get("live_status") == "is_live" else max(0, int(data.get("duration") or 0)),
                "title": clean(data.get("title") or video_id),
                "headers": data.get("http_headers", {}),
                "live": data.get("live_status") == "is_live",
                "audio_tracks": audio_catalog(data.get("formats") or []),
                "video_formats": data.get("formats") or [],
            }
            # O YouTube às vezes recusa (403) todos os links de um cliente (tv_samsung, 2026-11-22), e o link novo do
            # mesmo cliente volta recusado: refaz a extração sem ele.
            if result["live"]:
                break  # ffmpeg valida a playlist ao abrir; testar Range aqui só repetiria o GET
            sources = [video] if video["url"] == audio["url"] else [video, audio]
            hd = hd_source(data, video)
            probes = [asyncio.to_thread(source_status, result, source) for source in sources]
            if hd:
                # A fonte maior é opcional: uma CDN lenta não deve reter a reserva pronta por oito segundos.
                probes.append(asyncio.to_thread(source_status, result, hd, timeout=2))
            statuses = await asyncio.gather(*probes)
            if hd and statuses[-1] in (200, 206):
                result["video_hd"] = hd
            refused = [parse_qs(urlparse(s["url"]).query).get("c", [None])[0]
                       for s, status in zip(sources, statuses) if status == 403]
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
        return choose_audio(result)
    return await url_cache.get(video_id, load)


def source_args(info, source, start):
    headers = info["headers"] | source.get("http_headers", {})
    header_text = "".join(f"{clean(k)}: {clean(v)}\r\n" for k, v in headers.items())
    seek = ["-ss", str(start)]
    if info.get("live"):
        hls = (source.get("protocol") or "").startswith("m3u8") or "m3u8" in urlparse(source["url"]).path
        seek = ["-live_start_index", "-1"] if hls else []
    return ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "3",
            "-reconnect_on_network_error", "1", "-reconnect_on_http_error", "5xx",
            "-rw_timeout", "15000000", "-headers", header_text,
            *seek, "-i", source["url"]]


def mp3_output(stream, target, live=False):
    return ["-map", stream, "-vn", "-af", "aresample=44100,asetpts=PTS-STARTPTS",
            "-c:a", "libmp3lame", "-b:a", "192k", "-ar", "44100", "-ac", "2", "-flush_packets", "1",
            *(["-write_xing", "0", "-id3v2_version", "0"] if live else []), "-f", "mp3", target]


def audio_command(info, start):
    """Só o áudio, em MP3, para o Nest (Google Cast). Começa em `start`, com o tempo do fluxo em zero, como o vídeo."""
    return [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error",
            *source_args(info, info["audio"], start), *mp3_output("0:a:0", "pipe:1")]


def video_encode_args(quality):
    width, height, bitrate = SIZES[quality]
    # Fontes de 50/60 fps: descartar os quadros antes da escala poupa trabalho no notebook.
    filters = (f"fps=30,scale={width}:{height}:force_original_aspect_ratio=decrease:force_divisible_by=2,"
               f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black,setsar=1,setpts=PTS-STARTPTS")
    return ["-vf", filters, "-c:v", "mpeg1video", "-b:v", bitrate, "-maxrate", bitrate, "-bufsize", "2048k",
            "-g", "30" if quality == 720 else "15", "-bf", "2", "-pix_fmt", "yuv420p", "-threads", "4"]


def ffmpeg_command(info, start, quality, with_audio=True, mp3=None):
    """`mp3`: arquivo onde o mesmo ffmpeg grava o som do Nest (só na live; ver stream)."""
    video = info.get("video_hd", info["video"]) if quality == 720 else info["video"]
    cmd = [FFMPEG, "-hide_banner", "-nostdin", "-loglevel", "repeat+error"]
    separate = video["url"] != info["audio"]["url"]
    if not with_audio:
        cmd += source_args(info, video, start)
        if mp3 and separate:
            cmd += source_args(info, info["audio"], start)
        cmd += ["-map", "0:v:0", "-an",
                *video_encode_args(quality),
                "-avoid_negative_ts", "disabled", "-muxdelay", "0.2", "-muxpreload", "0",
                "-preload", "0", "-packetsize", "2048", "-flush_packets", "1", "-f", "mpeg", "pipe:1"]
        if mp3:
            cmd += ["-y", *mp3_output("1:a:0" if separate else "0:a:0", str(mp3), live=True)]
        return cmd
    for source in ([video, info["audio"]] if separate else [video]):
        cmd += source_args(info, source, start)
    cmd += ["-map", "0:v:0", "-map", "1:a:0" if separate else "0:a:0",
            *video_encode_args(quality),
            # O encoder MP2 tem 481 amostras de atraso; compensar mantém seu PTS inicial em zero.
            "-af", "aresample=44100,asetpts=PTS-STARTPTS+481/44100/TB",
            "-c:a", "mp2", "-ar", "44100", "-ac", "2", "-b:a", "192k",
            # Sem a correção automática, o muxer não soma ~0,53 s aos timestamps.
            # DTS negativo do primeiro quadro é normal com B-frames; PTS começa em zero.
            "-avoid_negative_ts", "disabled", "-muxdelay", "0.2", "-muxpreload", "0",
            "-preload", "0", "-packetsize", "2048",
            "-flush_packets", "1", "-f", "mpeg", "pipe:1"]
    if mp3:
        cmd += ["-y", *mp3_output("1:a:0" if separate else "0:a:0", str(mp3), live=True)]
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
        self.video_id, self.info = "", None
        self.active = None

    async def begin(self):
        async with self.lock:
            self.serial += 1
            self.video_id, self.info = "", None
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
            cmd = {"action": "play", "id": item["id"], "t": item.get("t", 0), "source": item.get("source", "")}
            action = "play"
        if action == "aviso":
            return f"aviso\t{clean(cmd.get('text', ''))}\n"
        if action == "play":
            source = "\tnavigation" if cmd.get("source") == "navigation" else ""
            return f"play\t{cmd['id']}\t{cmd.get('t') or 0:g}\t{clean(self.item(cmd['id'])['title'])}{source}\n"
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
        global nav_queue_serial
        if not self.active or self.fired:
            return
        LOG.info("Timer: vencido; enviando desligar ao Xbox (%s)", self.what)
        self.fired = self.shutdown = True
        nav_queue_serial += 1
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
            request.query_params.get("lang") if request.query_params.get("lang") in ("pt", "en", "es") else None) + "\n")
    if request.url.path.startswith("/api/thumb/"):
        return PlainTextResponse("Miniatura indisponível.\n", status_code=404)
    return await request_validation_exception_handler(request, error)


def account_line(status):
    return "ok\t" + "\t".join(clean(status.get(k, "")) for k in ("state", "name", "code", "url")) + "\t" + nav_image(status.get("avatar")) + "\n"


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


# ---------------------------------------------------------------- canais, biblioteca e Música
# URLs de arte vêm só do serviço local; o console recebe uma chave do cache, nunca uma URL.
import hashlib

nav_images = {}
preview_cache = TTLCache(300)
preview_slots = asyncio.Semaphore(3)
nav_lists = {}
nav_queue_serial = 0
NAV_KINDS = ("channel", "subscriptions", "library", "playlist", "history", "later", "search",
             "music", "music_search", "album", "artist", "radio", "music_playlist")


def nav_image(url, banner=False):
    url = str(url or "")
    if url.startswith("//"):
        url = "https:" + url
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in ("i.ytimg.com", "yt3.ggpht.com", "yt3.googleusercontent.com", "lh3.googleusercontent.com"):
        return ""
    return nav_art_key(("banner:" if banner else "") + url, url)


def nav_art_key(identity, source):
    key = hashlib.sha256(identity.encode()).hexdigest()[:24]
    if key not in nav_images and len(nav_images) >= 4096:
        nav_images.pop(next(iter(nav_images)))
    nav_images[key] = source
    return "~" + key


def collection_art(playlist, fallback, stamp):
    # O carimbo da lista invalida também a capa no cache do console ao atualizar a biblioteca.
    return nav_art_key(f"playlist:{stamp}:{playlist}", (playlist, fallback))


async def nav_data(kind, item_id, query, tab, offset, n, fresh=False, filter=0):
    identity = await account_status()
    if (kind in ("subscriptions", "library", "later") or item_id == "WL") and identity["state"] != "signed_in":
        raise HTTPException(401, "signed_out")
    key = (kind, item_id, query, tab, filter, identity.get("state"), identity.get("name"), identity.get("handle"))
    cache = nav_lists.get(key)
    if not cache or time.monotonic() - cache["at"] > 300 or (fresh and not offset):
        if len(nav_lists) >= 24:
            nav_lists.pop(next(iter(nav_lists)))
        cache = {"at": time.monotonic(), "items": [], "more": True, "page": 0, "seen": set(), "header": {}, "lock": asyncio.Lock()}
        nav_lists[key] = cache
    async with asyncio.timeout(25), cache["lock"]:
        while cache["more"] and len(cache["items"]) < offset + n and cache["page"] <= 40:
            page = cache["page"]
            route = f"/nav?kind={kind}&id={quote(item_id)}&q={quote(query)}&tab={tab}&page={page}&fresh={int(fresh and page == 0)}&filter={filter}"
            data = await account_request(route)
            if page == 0:
                cache["header"] = {k: data.get(k, "") for k in ("title", "subtitle", "channel_id", "subscribed")}
                cache["header"]["image"] = nav_image(data.get("image"))
                cache["header"]["banner"] = nav_image(data.get("banner"), banner=True)
                for field in ("history_video", "later_video"):
                    video = data.get(field) or ""
                    cache["header"][field] = video if ID_PATTERN.fullmatch(video) else ""
                if kind == "library":
                    cache["header"]["later_video"] = collection_art("WL", cache["header"]["later_video"], cache["at"])
            for entry in data.get("items", []):
                item_type, entry_id = entry.get("type"), str(entry.get("id", ""))
                unique = (item_type, entry_id)
                if item_type not in ("video", "channel", "playlist", "album", "artist") or not re.fullmatch(r"[A-Za-z0-9_-]{2,160}", entry_id) or unique in cache["seen"]:
                    continue
                if item_type == "video" and not ID_PATTERN.fullmatch(entry_id):
                    continue
                item = {"type": item_type, "id": entry_id, "title": clean(entry.get("title")), "channel": clean(entry.get("channel")),
                        "duration": max(0, int(entry.get("duration") or 0)), "channel_id": clean(entry.get("channel_id")),
                        "section": clean(entry.get("section")), "image": nav_image(entry.get("image"))}
                if kind == "library" and item_type == "playlist":
                    item["image"] = collection_art(entry_id, item["image"], cache["at"])
                if item_type == "video":
                    remember(item | {"views": ""})
                    if not item["image"] or kind not in ("music", "music_search", "album", "artist", "radio", "music_playlist"):
                        item["image"] = entry_id
                cache["seen"].add(unique)
                cache["items"].append(item)
            cache["more"] = bool(data.get("more")) and page < 40
            cache["page"] += 1
        return cache["header"] | {"items": cache["items"][offset:offset + n],
                                   "more": cache["more"] or offset + n < len(cache["items"])}


def nav_subtitle(value, lang):
    def count(match):
        number, scale, label = match.groups()
        scale = (scale or "").strip().lower()
        scale = {"k": 0, "mil": 0, "m": 1, "mi": 1, "b": 2, "bi": 2}.get(scale)
        number = number.replace(".", ",") if lang == "pt" else number.replace(",", ".")
        if scale is not None:
            number += ((" mil", " mi", " bi") if lang == "pt" else ("K", "M", "B"))[scale]
        key = 0 if label.lower().startswith(("subscr", "inscri")) else 1 if label.lower().startswith(("view", "visualiza")) else 2
        if scale is None and float(number.replace(",", ".")) == 1:
            return number + " " + (("inscrito", "visualização", "vídeo") if lang == "pt" else ("suscriptor", "vista", "video") if lang == "es" else ("subscriber", "view", "video"))[key]
        return number + " " + (("inscritos", "visualizações", "vídeos") if lang == "pt" else ("suscriptores", "vistas", "videos") if lang == "es" else ("subscribers", "views", "videos"))[key]
    value = re.sub(r"(\d+(?:[.,]\d+)?)\s*(K|M|B|mil|mi|bi)?\s+(?:de\s+)?(subscribers?|inscritos?|views?|visualizações|videos?|vídeos?)\b", count, clean(value), flags=re.I)
    return re.sub(r"^(Album|Álbum)(?=\s*[•·])", tr("Álbum", "Album", lang), value, flags=re.I)


def nav_response(data, lang=LANGUAGE):
    fields = [data.get("title", ""), nav_subtitle(data.get("subtitle", ""), lang), data.get("image", ""),
              int(bool(data.get("subscribed"))), int(bool(data.get("more"))), data.get("channel_id", ""),
              data.get("banner", ""), data.get("history_video", ""), data.get("later_video", "")]
    lines = ["ok\t" + "\t".join(clean(v) for v in fields)]
    for item in data.get("items", []):
        item = dict(item)
        if item.get("type") == "channel":
            item["channel"] = nav_subtitle(item.get("channel", ""), lang)
        elif item.get("type") == "album":
            item["channel"] = re.sub(r"^(Album|Álbum)\s*[•·]\s*", "", item.get("channel", ""), flags=re.I)
        lines.append("\t".join(clean(item.get(k, "")) for k in
                     ("type", "id", "title", "channel", "duration", "image", "channel_id", "section")))
    return PlainTextResponse("\n".join(lines) + "\n", headers={"Cache-Control": "no-store"})


def nav_error(error, lang):
    signed_out = getattr(error, "status_code", 0) == 401 or getattr(error, "code", 0) == 401
    message = tr("Conecte sua conta do YouTube em Configurações.", "Sign in to YouTube in Settings.", lang) if signed_out else tr(
        "Não foi possível consultar o YouTube. Aperte X para tentar novamente.", "Could not reach YouTube. Press X to retry.", lang)
    return PlainTextResponse("erro\t" + message + "\n", headers={"Cache-Control": "no-store"})


@app.get("/api/nav", response_class=PlainTextResponse)
async def navigation(kind: str, id: str = "", q: str = Query("", max_length=200), tab: Literal["videos", "live", "playlists"] = "videos",
                     offset: int = Query(0, ge=0, le=2000), n: int = Query(24, ge=1, le=100), fresh: bool = False,
                     lang: Literal["pt", "en", "es"] = LANGUAGE, filter: int = Query(0, ge=0, le=3)):
    if kind not in NAV_KINDS or (id and not re.fullmatch(r"[A-Za-z0-9_-]{2,160}", id)):
        raise HTTPException(400, "Invalid navigation.")
    try:
        return nav_response(await nav_data(kind, id, q, tab, offset, n, fresh, filter), lang)
    except Exception as error:
        return nav_error(error, lang)


@app.get("/api/nav/channel", response_class=PlainTextResponse)
async def navigation_channel(id: str, lang: Literal["pt", "en", "es"] = LANGUAGE):
    valid_id(id)
    try:
        data = await account_request("/channel_for_video?id=" + id)
        return nav_response(await nav_data("channel", data["id"], "", "videos", 0, 24), lang)
    except Exception as error:
        return nav_error(error, lang)


@app.get("/api/nav/action", response_class=PlainTextResponse)
async def navigation_action(request: Request, action: Literal["subscribe", "later"], id: str, value: bool = True,
                            lang: Literal["pt", "en", "es"] = LANGUAGE):
    account_native_request(request)
    if not re.fullmatch(r"UC[A-Za-z0-9_-]{22}" if action == "subscribe" else r"[A-Za-z0-9_-]{11}", id):
        raise HTTPException(400, "Invalid item.")
    try:
        await account_request(f"/nav/action?action={action}&id={id}&value={int(value)}", {})
        nav_lists.clear()
        account_pages.pop("subs" if action == "subscribe" else "later", None)
        return PlainTextResponse("ok\n", headers={"Cache-Control": "no-store"})
    except Exception as error:
        return nav_error(error, lang)


@app.get("/api/nav/play", response_class=PlainTextResponse)
async def navigation_play(request: Request, kind: Literal["playlist", "album", "radio", "music_playlist", "later"], id: str = "",
                          start: int = Query(0, ge=0, le=255), lang: Literal["pt", "en", "es"] = LANGUAGE):
    global nav_queue_serial
    account_native_request(request)
    if id and not re.fullmatch(r"[A-Za-z0-9_-]{2,160}", id):
        raise HTTPException(400, "Invalid item.")
    nav_queue_serial += 1
    serial = nav_queue_serial
    try:
        # Só substitui a fila depois que a nova lista carregou inteira até o limite do Cast.
        data = await nav_data(kind, id, "", "videos", 0, 256)
        items = [v for v in data["items"] if v["type"] == "video"][start:]
        if not items:
            raise HTTPException(404, "Empty playlist.")
        if serial != nav_queue_serial:
            return PlainTextResponse("erro\t" + tr("Reprodução cancelada.", "Playback cancelled.", lang) + "\n", headers={"Cache-Control": "no-store"})
        remote.queue.clear()
        remote.queue.extend(v | {"t": 0, "source": "navigation"} for v in items[1:])
        remote.commands = deque(c for c in remote.commands if c["action"] not in ("play", "next", "stop", "pause", "resume", "seek"))
        remote.commands.append({"action": "play", "id": items[0]["id"], "t": 0, "source": "navigation", "at": time.monotonic()})
        return nav_response(data | {"items": items}, lang)
    except Exception as error:
        return nav_error(error, lang)


@app.get("/api/nav/transport", response_class=PlainTextResponse)
async def navigation_transport(request: Request, action: Literal["next", "stop", "clear"]):
    global nav_queue_serial
    account_native_request(request)
    if action in ("stop", "clear"):
        nav_queue_serial += 1
        remote.queue.clear()
        remote.commands = deque(c for c in remote.commands if c["action"] not in ("play", "next", "stop", "pause", "resume", "seek"))
    if len(remote.commands) >= 256:
        raise HTTPException(429, "Queue full.")
    if action == "next":
        # Um play pendente impede que o mesmo poll acrescente outro avanço pelo fim natural.
        if remote.queue:
            item = remote.queue.popleft()
            remote.commands.append({"action": "play", "id": item["id"], "t": item.get("t", 0), "source": item.get("source", ""), "at": time.monotonic()})
        else:
            remote.commands.append({"action": "stop", "source": "navigation", "at": time.monotonic()})
    elif action != "clear":
        remote.commands.append({"action": action, "source": "navigation", "at": time.monotonic()})
    return PlainTextResponse("ok\n", headers={"Cache-Control": "no-store"})


@app.get("/api/nav/lyrics", response_class=PlainTextResponse)
async def navigation_lyrics(id: str):
    valid_id(id)
    try:
        data = await account_request("/lyrics?id=" + id, timeout=15)
        lyrics = clean(data.get("lyrics"))[:12000]
    except Exception:
        lyrics = ""
    return PlainTextResponse("ok\t" + lyrics + "\n", headers={"Cache-Control": "no-store"})


def nav_make_thumb(key, w, h, path):
    url = nav_images.get(key)
    if not url:
        raise HTTPException(404, "Unknown artwork.")
    # Sem redirecionamento para outra origem; a lista de hosts é validada a cada salto.
    import urllib.request
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    with urllib.request.build_opener(NoRedirect).open(URLRequest(url, headers={"User-Agent": "Tube360/1.0"}), timeout=12) as response:
        data = response.read(4 * 1024 * 1024)
    with Image.open(io.BytesIO(data)) as source:
        if source.width * source.height > 16_000_000:
            raise HTTPException(404, "Artwork too large.")
        # Mantém o aspecto no slot 16:9 compartilhado; o cliente recorta o quadrado pelo UV.
        side = min(w, h)
        if (w, h) == (1184, 160) or source.width / source.height >= 1.4:
            thumb = ImageOps.fit(source.convert("RGB"), (w, h), Image.Resampling.LANCZOS)
        else:
            image = ImageOps.fit(source.convert("RGB"), (side, side), Image.Resampling.LANCZOS)
            thumb = Image.new("RGB", (w, h))
            thumb.paste(image, ((w - side) // 2, (h - side) // 2))
        temporary = path.with_suffix(".tmp")
        thumb.save(temporary, format="JPEG", quality=85, progressive=False)
        temporary.replace(path)
    return path


@app.get("/api/thumb/nav/{key}")
async def navigation_thumb(key: str, w: int = Query(320, ge=1, le=1184), h: int = Query(180, ge=1, le=360)):
    if w > 640 and (w, h) != (1184, 160):
        raise HTTPException(404, "Invalid artwork size.")
    if not re.fullmatch(r"[a-f0-9]{24}", key):
        raise HTTPException(404, "Invalid artwork.")
    source = nav_images.get(key)
    if isinstance(source, tuple):
        playlist, fallback = source

        async def preview():
            async with preview_slots:
                try:
                    data = await account_request("/playlist_preview?id=" + quote(playlist), timeout=8)
                    video = data.get("video") or ""
                    if ID_PATTERN.fullmatch(video):
                        return video
                except Exception:
                    pass
                return fallback

        art = await preview_cache.get(key, preview)
        if ID_PATTERN.fullmatch(art):
            return await thumb(art, w, h)
        if not art.startswith("~"):
            raise HTTPException(404, "Empty collection.")
        key = art[1:]
    path = THUMBS / f"nav_{key}_{w}x{h}.jpg"
    if not path.exists():
        await thumb_cache.get(path.name, lambda: asyncio.to_thread(nav_make_thumb, key, w, h, path))
    return FileResponse(path, media_type="image/jpeg", headers={"Cache-Control": "public, max-age=86400"})


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
    if lang not in ("pt", "en", "es"):
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
                        when = time.strftime("%m/%d" if lang == "en" else "%d/%m", time.localtime(entry["ts"]))
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
                 a: int = Query(1, ge=0, le=1), speaker: int = Query(0, ge=0, le=1), codec: str = "", mode: str = "auto", audio: str = "original"):
    valid_id(video_id)
    if q not in SIZES or not math.isfinite(t):
        raise HTTPException(400, "Qualidade ou posição inválida.")
    audio_choice(audio)
    if mode not in ("auto", "720p60", "1080p30"):
        raise HTTPException(400, "Modo de vídeo inválido.")
    opened = time.monotonic()
    ticket = await streams.begin()
    refetched = False
    allow_copy = codec == "h264"
    while True:
        try:
            cached_info = await resolve_video(video_id)
            info = choose_audio(cached_info, audio)
        except Exception as error:
            reason = unplayable_reason(error)
            LOG.info("vídeo %s não abriu (%s): %s", video_id, reason or "erro",
                     re.sub(r"https?://\S+", "<url>", clean(str(error)))[:200])
            raise HTTPException(502, "Não foi possível abrir este vídeo. Tente novamente.",
                                headers={"X-Error": reason} if reason else None) from None
        if await request.is_disconnected():
            raise HTTPException(499, "Conexão encerrada.")
        if info["live"]:
            t = 0.0
        if info["duration"] and not info["live"] and t >= info["duration"]:
            raise HTTPException(416, "Posição após o fim do vídeo.")
        mp3 = None
        if info["live"] and (speaker or not a):
            # Live com o Nest: o som sai do mesmo ffmpeg da imagem. Dois leitores do HLS podem começar em segmentos
            # diferentes (5 s cada), e o servidor não teria como alinhar depois.
            AUDIO_DIR.mkdir(parents=True, exist_ok=True)
            mp3 = AUDIO_DIR / f"{video_id}_live_{ticket}.part.mp3"
            mp3.unlink(missing_ok=True)
        original = copy_source(info,q,mode) if allow_copy and not info["live"] else None
        command = (remux_command(info, original, t, bool(a)) if original else
                   ffmpeg_command(info, t, min(q,720), with_audio=bool(a), mp3=mp3))
        encoder = await streams.start(ticket, command)
        streams.video_id, streams.info = video_id, info
        encoder.mp3 = mp3
        if mp3:
            live_audio[video_id] = (mp3, encoder.process)
        elif speaker and not info["live"] and cached_audio_base(video_id, t) is None:
            async def warm_audio():
                try:
                    await audio_file(video_id, t)
                except Exception as error:
                    LOG.info("Nest: pré-conversão de áudio falhou (%s)", type(error).__name__)
            spawn_background(warm_audio())
        try:
            # Só confirmar HTTP 200 depois de receber MPEG; erro de origem vira 502.
            first_read = asyncio.create_task(encoder.output.get())
            deadline = time.monotonic() + 40
            first = None
            while not first_read.done():
                if original and time.monotonic() > deadline:
                    first_read.cancel()
                    break
                if await request.is_disconnected() or time.monotonic() > deadline:
                    raise HTTPException(504, "O vídeo demorou para responder.")
                await asyncio.wait({first_read}, timeout=0.2)
            else:
                first = first_read.result()
        except BaseException:
            first_read.cancel()
            with anyio.CancelScope(shield=True):
                await streams.finish(encoder)
            drop_live_audio(video_id, encoder)
            raise
        if first:
            break
        if ticket != streams.serial:
            drop_live_audio(video_id, encoder)
            raise HTTPException(409, "Outro vídeo foi solicitado.")
        with anyio.CancelScope(shield=True):
            await streams.finish(encoder)
        drop_live_audio(video_id, encoder)
        if original:
            allow_copy = False
            LOG.info("stream %s: remux sem dados, usando MPEG-1", video_id)
            continue
        # A fonte 1080p passou na sondagem de 2 bytes e foi recusada na leitura: a reserva já resolvida (que também
        # passou) serve agora e nos próximos pulos deste link. O dict é o mesmo guardado no url_cache.
        if q == 720 and info.pop("video_hd", None) is not None:
            cached_info.pop("video_hd",None)
            LOG.info("stream %s: a fonte maior recusou a leitura, usando a reserva", video_id)
            continue
        # A URL assinada foi recusada (o YouTube às vezes responde 403): resolve de novo e tenta mais uma vez.
        if refetched:
            raise HTTPException(502, "A origem do vídeo não respondeu. Tente novamente.")
        refetched = True
        url_cache.values.pop(video_id, None)
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

    LOG.info("stream %s sessão %d: primeiro %s em %.3f s, live=%d", video_id, ticket,
             "H.264" if original else "MPEG-1", time.monotonic() - opened, info["live"])
    return StreamingResponse(body(), media_type="video/mp2t" if original else "video/mpeg", headers={
        "X-Video-Codec": "h264" if original else "mpeg1",
        "X-Source-Fps": str((original or {}).get("fps") or 30),
        "X-Audio-Track": quote(info.get("audio_selected", ""),safe=""),
        "X-Audio-Tracks": quote("\n".join("\t".join((tr["code"],tr["language"],tr["name"][:100],str(int(tr["original"])),str(int(tr["automatic"])))) for tr in info.get("audio_tracks", [])[:64]),safe=""),
        "X-Nest-Handoff": "1",
        "X-Stream-Id": str(ticket), "X-Duration": str(info["duration"]), "X-Live": "1" if info["live"] else "0", "X-Title": quote(info["title"], safe=""),
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
    if encoder.mp3:
        encoder.mp3.unlink(missing_ok=True)  # caminho exclusivo da sessão, mesmo numa reabertura rápida


def finish_audio(key, job, path, partial):
    if audio_jobs.get(key) is not job:
        return
    if job.returncode == 0 and partial.exists() and partial.stat().st_size:
        partial.replace(path)
    else:
        partial.unlink(missing_ok=True)
        url_cache.values.pop(key[0].split("~",1)[0], None)
    audio_jobs.pop(key, None)


def audio_reachable(video_id, base, target, selection="original"):
    video_id=audio_cache_id(video_id,selection)
    if target < base:
        return False
    path = AUDIO_DIR / f"{video_id}_{base:.2f}.mp3"
    # MP3 CBR 192 kbit/s. Reservar dois quadros e o cabeçalho antes da borda que ainda cresce.
    try:
        if path.stat().st_size > 0:
            return True
    except FileNotFoundError:
        pass
    partial = path.with_suffix(".part.mp3")
    try:
        return ((video_id, round(base, 2)) in audio_jobs and
                partial.stat().st_size >= (target - base) * 24000 + 2048)
    except FileNotFoundError:
        return path.is_file()


def cached_audio_base(video_id, target, selection="original"):
    # Um trecho iniciado em 30 s também serve para abrir em 50 s, mesmo após trocar de vídeo ou reiniciar o servidor.
    choices = []
    cache_id=audio_cache_id(video_id,selection)
    for path in AUDIO_DIR.glob(f"{cache_id}_*.mp3"):
        match = re.fullmatch(r"(\d+\.\d{2})(?:\.part)?\.mp3", path.name[len(cache_id) + 1:])
        if match:
            base = float(match[1])
            if audio_reachable(video_id, base, target, selection):
                choices.append(base)
    return min(choices, default=None)


async def audio_file(video_id, t, selection="original"):
    """Só um ffmpeg por trecho. O Nest lê o provisório; só uma conversão completa vira cache."""
    live = live_audio.get(video_id)
    if live and live[1].returncode is None:
        if streams.info and streams.video_id == video_id and choose_audio(streams.info,selection).get("audio_selected") != streams.info.get("audio_selected"):
            raise RuntimeError("trocar a faixa da live requer reabrir o vídeo")
        return live
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    cache_id=audio_cache_id(video_id,selection)
    key = (cache_id, round(t, 2))
    path = AUDIO_DIR / f"{cache_id}_{key[1]:.2f}.mp3"
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
        info = choose_audio(await resolve_video(video_id),selection)
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
        cmd = audio_command(info, t)[:-1]
        if nest.gated_revision and t > 0:
            base = cached_audio_base(video_id, t, selection)
            if base is not None and base < t:
                cached = AUDIO_DIR / f"{cache_id}_{base:.2f}.mp3"
                if cached.is_file():
                    cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-ss", f"{t - base:.3f}",
                           "-i", str(cached), *mp3_output("0:a:0", "pipe:1")[:-1]]
        cmd += ["-y", str(partial)]
        job = await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        audio_jobs[key] = job

        async def reap():
            await job.wait()
            finish_audio(key, job, path, partial)
        spawn_background(reap())
        return partial, job


@app.head("/api/audio/{video_id}")
@app.get("/api/audio/{video_id}")
async def audio(request: Request, video_id: str, t: float = Query(0, ge=0, le=86400 * 30), track: str = "original"):
    valid_id(video_id)
    audio_choice(track)
    if not math.isfinite(t):
        raise HTTPException(400, "Posição inválida.")
    token = request.query_params.get("s")
    if token and nest.token != "s=" + token:
        raise HTTPException(410, "Esta sessão de áudio terminou.")
    try:
        path, job = await audio_file(video_id, t) if track == "original" else await audio_file(video_id, t, track)
    except Exception:
        raise HTTPException(502, "Não foi possível abrir este áudio.") from None
    if token and nest.token != "s=" + token:
        raise HTTPException(410, "Esta sessão de áudio terminou.")
    start = 0
    match = re.fullmatch(r"bytes=(\d+)-(\d*)", request.headers.get("range", ""))
    if match:
        start = int(match.group(1))
    # O MP3 da live é CBR sem cabeçalho ID3/Xing. Buscar a partir de um quadro completo deixa o Nest
    # recuperar o som sem reabrir o HLS ou obrigar o Xbox a voltar para a borda.
    live = live_audio.get(video_id)
    cut = round(t * 44100 / 1152) if token and live and live[0] == path else 0
    estimate = round(cut * 1152 / 44100 * 24000)
    LOG.info("áudio do Nest: %s t=%.1f desde o byte %d%s", video_id, t, start, " (convertendo)" if job else "")
    deadline = time.monotonic() + 30
    reserve = 12000 if token and nest.gated_revision and start == 0 else 0
    while not path.exists() or path.stat().st_size <= estimate + start + max(reserve, 1300 if cut else 0):
        if token and nest.token != "s=" + token:
            raise HTTPException(410, "Esta sessão de áudio terminou.")
        if job is None or job.returncode is not None or time.monotonic() > deadline:
            break
        await asyncio.sleep(0.04)
    # O reap pode ter renomeado entre a chamada e o primeiro byte da resposta.
    if not path.exists() and path.name.endswith(".part.mp3"):
        path = path.with_name(path.name.replace(".part.mp3", ".mp3"))
    if token and nest.token != "s=" + token:
        raise HTTPException(410, "Esta sessão de áudio terminou.")
    try:
        source = path.open("rb")
    except OSError:
        raise HTTPException(502, "Não foi possível abrir este áudio.") from None
    size = os.fstat(source.fileno()).st_size
    origin = 0
    if cut:
        # Padding alterna quadros de 626/627 bytes. Conferir também a próxima palavra de sincronização.
        probe = max(0, estimate - 16)
        source.seek(probe)
        data = source.read(1300)
        for i in range(min(33, len(data) - 628)):
            if data[i:i+2] == b"\xff\xfb" and data[i+2] & 0xfc == 0xb0:
                following = i + 626 + ((data[i+2] >> 1) & 1)
                if data[following:following+2] == b"\xff\xfb":
                    origin = probe + i
                    break
        if not origin:
            source.close()
            raise HTTPException(416, "Quadro de áudio ao vivo indisponível.")
        size -= origin
    if not size:
        source.close()
        raise HTTPException(502, "Não foi possível abrir este áudio.")
    if start >= size:
        source.close()
        raise HTTPException(416, "Trecho indisponível.", headers={"Content-Range": f"bytes */{size}"})
    done = job is None or job.returncode is not None
    # O DMR inicia com bytes=0-. Responder 206 até o tamanho provisório fazia o som acabar ali.
    if match and not done and start == 0 and not match.group(2):
        match = None
    end = size - 1 if match else None
    if match and match.group(2):
        end = min(end, int(match.group(2)))
        if end < start:
            source.close()
            raise HTTPException(416, "Trecho inválido.", headers={"Content-Range": f"bytes */{size}"})

    length = (end + 1 if match else size) - start
    # Uma sessão pode acabar ainda durante o GET. Só prometer um comprimento fixo se pudermos entregá-lo inteiro.
    head = request.scope.get("method") == "HEAD"
    fixed = (done or match is not None) and (head or not token or length <= 8192 and not nest.gated_revision)

    async def body():
        pos = start
        try:
            if token and nest.gated_revision and nest.token == "s=" + token:
                gate = nest.gated_revision
                nest.reader_ready = gate
                while nest.gated_revision == gate and nest.revision == gate and nest.token == "s=" + token:
                    await asyncio.sleep(0.01)
                if nest.token != "s=" + token or nest.revision != gate:
                    return
            source.seek(origin + pos)
            while fixed or not token or nest.token == "s=" + token:
                want = 8192 if pos == start else 32768
                if token:
                    # Um MP3 completo pode caber inteiro no buffer do Nest antes que o STOP encontre Wi-Fi.
                    # Quatro segundos para a partida/Range; em regime bastam 1,5 s além do relógio confirmado.
                    _, confirmed = nest.position()
                    horizon = max(start + 96000, int((nest.target - t + 4) * 24000),
                                  int((confirmed - t + 1.5) * 24000))
                    if pos >= horizon and (end is None or pos <= end) and (not done or pos < size):
                        await asyncio.sleep(0.08)
                        continue
                    want = min(want, max(1, horizon - pos))
                if end is not None:
                    want = min(want, end + 1 - pos)
                if want <= 0:
                    break
                chunk = source.read(want)
                if chunk:
                    pos += len(chunk)
                    yield chunk
                    # O gerador só é retomado depois do envio HTTP do bloco. HEAD e a espera no gate não contam.
                    if token:
                        with nest.intent:
                            if nest.token == "s=" + token:
                                nest.audio_sent_at = time.monotonic()
                    continue
                if end is not None or job is None or job.returncode is not None:
                    break
                await asyncio.sleep(0.04)
        finally:
            source.close()

    headers = {"Cache-Control": "no-store", "Accept-Ranges": "bytes"}
    if fixed:
        headers["Content-Length"] = str(length)
    if match:
        headers["Content-Range"] = f"bytes {start}-{end}/{size if done else '*'}"
    if head:
        source.close()
        response = Response(status_code=206 if match else 200, media_type="audio/mpeg", headers=headers)
        if not fixed:
            del response.headers["content-length"]
        return response
    return StreamingResponse(body(), status_code=206 if match else 200, media_type="audio/mpeg", headers=headers)


class Nest:
    """O alto-falante Google Cast (config.json, "nest": {"host", "name"}). Uma thread consulta a posição a cada 250 ms
    enquanto toca; o console lê a última posição (com o instante em que foi lida) em /api/nest/pos."""
    APP = "CC1AD845"  # Default Media Receiver
    AUDIO_TIMEOUT = 8.0  # maior que os 4 s de MP3 liberados na partida e a cadência de status de 5 s
    START_TIMEOUT = 2.5

    def __init__(self):
        self.cc = None
        self.lock = threading.Lock()
        self.state, self.pos, self.stamp, self.error = "IDLE", 0.0, time.monotonic(), ""
        self.active = False
        self.ours = False  # o receptor aberto no Nest é o que este servidor abriu
        self.owner = "server"
        self.video_id, self.t0, self.host_ip = "", 0.0, ""  # o que está tocando e de que ponto (posição absoluta = t0 + tempo do Nest)
        self.token = ""  # marca "s=<ms>" da URL do último áudio pedido ao Nest
        self.intent = threading.Lock()  # protege a intenção sem esperar a rede do Cast
        self.revision, self.sid, self.closed_sid, self.requested_at = 0, None, 0, 0.0
        self.loaded_id = self.stopping_token = ""
        self.audio_selection = self.loaded_audio = "original"
        self.target, self.confirmed_revision = 0.0, 0
        self.gated_revision = self.reader_ready = 0
        self.audio_sent_at = None
        self.until_fast_status = 0.0
        self.local_sid = None
        # O receiver pode conservar áudio mesmo se o processo do notebook reiniciar entre LOAD e STOP.
        self.owner_file = AUDIO_DIR.parent / f"nest-{PORT}.json"
        self.may_sound = False
        if self.owner_file.exists():
            try:
                owner = json.loads(self.owner_file.read_text())
                if owner["host"] == (CONFIG.get("nest") or {}).get("host"):
                    self.stopping_token, self.may_sound = owner["token"], True
            except (OSError, ValueError, KeyError, TypeError):
                self.may_sound = True
        self.sleep_base = None
        self.want_stop = False  # o stop não chegou ao Nest (Wi-Fi caído): o watch repete quando a conexão voltar
        threading.Thread(target=self.watch, daemon=True).start()

    def connected(self):
        cc = self.cc
        return cc is not None and cc.socket_client.is_connected

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
            deadline = time.monotonic() + self.START_TIMEOUT
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
        cc = pychromecast.get_chromecast_from_cast_info(info, zconf=None, tries=None, retry_wait=0.7, timeout=self.START_TIMEOUT)
        try:
            cc.wait(timeout=self.START_TIMEOUT)
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
        return cc is not None and cc.status is not None and cc.status.app_id == self.APP \
            and cc.media_controller.namespace in cc.socket_client.app_namespaces

    def matches(self, st):
        return bool(self.token and parse_qs(urlparse(st.content_id or "").query).get("s") == [self.token[2:]])

    def request_play(self, video_id, target, duration, live, host_ip, sid, gated=False, audio_selection="original"):
        with self.intent:
            if self.owner != "server":
                return False
            self.revision += 1
            self.gated_revision = self.revision if gated else 0
            self.reader_ready = 0
            self.video_id, self.sid, self.host_ip, self.target = video_id, sid, host_ip, target
            self.audio_selection=audio_selection
            self.want_stop, self.active = False, True
            self.state, self.pos, self.error, self.requested_at = "LOADING", 0.0, "", time.monotonic()
            rev = self.revision
        threading.Thread(target=self.play, args=(rev, video_id, target, duration, live), daemon=True).start()
        return True

    def receiver(self, cc, rev, force=False):
        if rev != self.revision or self.owner != "server":
            return False
        if self.ours and self.media_ready(cc) and not force:
            return True
        # Um DMR aberto por outro remetente pode ignorar LOAD. Depois do launch basta o canal de mídia estar pronto.
        if cc.status and cc.status.app_id:
            cc.quit_app(timeout=self.START_TIMEOUT)
        if rev != self.revision:
            return False
        LOG.info("Nest: dono=server LAUNCH rev=%d", rev)
        cc.start_app(self.APP, timeout=self.START_TIMEOUT)
        deadline = time.monotonic() + 8
        while rev == self.revision and time.monotonic() < deadline:
            if self.media_ready(cc):
                self.ours = True
                return True
            time.sleep(0.05)
        return False

    def play(self, rev, video_id, target, duration, live):
        with self.lock:
            if rev != self.revision:
                return
            try:
                cc = self.connect()
                mc = cc.media_controller
                old_base = self.t0
                selection=self.audio_selection
                reachable = not live and self.loaded_id == video_id and self.loaded_audio == selection and (audio_reachable(video_id, old_base, target) if selection=="original" else audio_reachable(video_id,old_base,target,selection))
                if rev != self.gated_revision and reachable and self.media_ready(cc) and self.matches(mc.status) and mc.status.media_session_id:
                    started = time.monotonic()
                    try:
                        if rev != self.revision:
                            return
                        prior_stamp = mc.status.last_updated
                        mc.seek(target - old_base, timeout=1.5)
                        deadline = started + 2
                        while rev == self.revision and self.media_ready(cc) and time.monotonic() < deadline:
                            st = mc.status
                            advance = float(st.current_time or 0) + old_base - target
                            if self.matches(st) and st.last_updated != prior_stamp and st.player_state in ("PLAYING", "BUFFERING") and (
                                    -0.15 <= advance <= time.monotonic() - started + 0.25):
                                self.confirmed_revision = rev
                                LOG.info("Nest: SEEK %.2f s em %.3f s", target, time.monotonic() - started)
                                return
                            mc.update_status()
                            time.sleep(0.08)
                    except Exception as error:
                        LOG.info("Nest: SEEK sem confirmação (%s)", type(error).__name__)
                # Um LOAD pode reaproveitar o MP3 inteiro mesmo se a sessão Cast anterior já terminou.
                base = target
                if live:
                    # Após uma queda só o leitor MP3 entra mais adiante no mesmo stream da imagem.
                    base = round(target * 44100 / 1152) * 1152 / 44100
                elif rev == self.gated_revision:
                    # A retomada começa no byte zero de um MP3 já recortado no ponto de troca.
                    base = target
                elif reachable:
                    base = old_base
                else:
                    cached = cached_audio_base(video_id, target) if selection=="original" else cached_audio_base(video_id,target,selection)
                    if cached is not None:
                        base = cached
                with self.intent:
                    if rev != self.revision:
                        return
                    self.t0, self.loaded_id = base, video_id
                    self.loaded_audio=selection
                    self.token = "s=" + uuid.uuid4().hex
                    self.audio_sent_at = None
                    url = f"http://{self.host_ip}:{PORT}/api/audio/{video_id}?t={base:.2f}&{self.token}"
                    if selection != "original": url += "&track=" + quote(selection,safe="")
                for attempt in range(2):
                    if rev != self.revision or not self.receiver(cc, rev, force=bool(attempt)):
                        break
                    if rev != self.revision:
                        return
                    opts = {}
                    if not live:
                        opts["current_time"] = target - base
                        if duration > base:
                            opts["media_info"] = {"duration": duration - base}
                    with self.intent:
                        if rev != self.revision or self.owner != "server":
                            return
                        self.owner_file.parent.mkdir(parents=True, exist_ok=True)
                        pending = self.owner_file.with_suffix(".tmp")
                        pending.write_text(json.dumps({"host": (CONFIG.get("nest") or {}).get("host"), "token": self.token}))
                        pending.replace(self.owner_file)
                        self.may_sound = True
                    mc.play_media(url, "audio/mpeg", title="YouTube 360", stream_type="LIVE" if live else "BUFFERED", **opts)
                    deadline = time.monotonic() + (12 if attempt else 9)
                    while rev == self.revision and time.monotonic() < deadline:
                        if self.media_ready(cc):
                            if self.matches(mc.status) and mc.status.player_state in ("BUFFERING", "PLAYING", "PAUSED"):
                                self.confirmed_revision = rev
                                LOG.info("Nest: LOAD %s %.2f s em %.3f s", "LIVE" if live else "BUFFERED",
                                         target, time.monotonic() - self.requested_at)
                                return
                            mc.update_status()
                        time.sleep(0.08)
                raise RuntimeError("O receptor não confirmou o áudio.")
            except Exception as error:
                if rev == self.revision:
                    self.state, self.error = "ERRO", str(error)[:120]
                    LOG.warning("Nest: %s", self.error)

    def prepare(self):
        with self.lock:
            try:
                rev = self.revision
                if self.owner == "server" and not self.active and not self.want_stop:
                    self.receiver(self.connect(), rev)
            except Exception as error:
                LOG.warning("Nest prepare: %s", error)

    def volume(self, level):
        with self.lock:
            if self.owner != "server":
                return
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

    def stop_intent(self, sid=None, closing=True):
        with self.intent:
            if sid is not None and sid != self.sid and sid != streams.serial:
                return None
            if closing:
                self.closed_sid = max(self.closed_sid, sid or self.sid or 0)
            self.gated_revision = self.reader_ready = 0
            self.revision += 1
            if self.token:
                self.stopping_token = self.token
            self.active, self.want_stop = False, True
            self.video_id, self.token, self.loaded_id = "", "", ""
            self.audio_sent_at = None
            self.state, self.pos = "IDLE", 0.0
            return self.revision

    def command(self, what, rev=None):
        if rev is None:
            rev = self.stop_intent() if what == "stop" else self.revision
        with self.lock:
            if rev != self.revision:
                return
            try:
                if what == "stop" and not self.stopping_token:
                    self.want_stop = False  # preparar o receptor não nos dá uma sessão de mídia para parar
                    return
                if what == "stop" and not self.connected():
                    if self.cc and not self.cc.socket_client.stop.is_set():
                        return
                    if not (CONFIG.get("nest") or {}).get("host"):
                        self.want_stop = False
                        return
                cc = self.connect()
                if not self.media_ready(cc):
                    # app_id ausente ou namespace ainda vazio durante reconexão: o STOP continua pendente.
                    if what == "stop" and cc.status and cc.status.app_id not in (None, self.APP):
                        self.want_stop = False
                    return
                mc = cc.media_controller
                st = mc.status
                if what != "stop" and (not self.active or not self.matches(st)):
                    return
                if not st.media_session_id:
                    mc.update_status()
                    time.sleep(0.25)
                if rev != self.revision or not self.media_ready(cc):
                    return
                st = mc.status
                if what == "stop" and self.stopping_token and st.content_id and parse_qs(
                        urlparse(st.content_id).query).get("s") != [self.stopping_token[2:]]:
                    self.want_stop = False  # outro remetente já ocupou o receptor
                    return
                if mc.status.media_session_id:
                    {"pause": mc.pause, "resume": mc.play, "stop": mc.stop}[what](timeout=2)
                elif what == "stop" and self.ours:
                    cc.quit_app(timeout=2)
                    self.ours = False
                if what == "stop" and rev == self.revision:
                    self.want_stop = False
            except Exception as error:
                LOG.warning("Nest %s: %s", what, error)

    @staticmethod
    def fresh_status(controller, timeout):
        received = threading.Event()
        replies = []

        def answer(ok, data):
            if ok and isinstance(data, dict):
                replies.append(data)
            received.set()

        try:
            controller.update_status(callback_function=answer)
        except Exception:
            return {}
        return replies[0] if received.wait(timeout) and replies else {}

    def started_position(self, rev):
        # O cache periódico pode continuar em BUFFERING depois que a primeira amostra já tocou.
        deadline = time.monotonic() + 0.65
        with self.lock:
            while rev == self.revision and time.monotonic() < deadline and self.connected() and self.media_ready(self.cc):
                mc = self.cc.media_controller
                sent = time.monotonic()
                reply = self.fresh_status(mc, 0.1)
                now = time.monotonic()
                if reply.get("type") == "MEDIA_STATUS" and now - sent < 0.12 and self.matches(mc.status):
                    for st in reply.get("status") or []:
                        if st.get("playerState") != "PLAYING" or st.get("mediaSessionId") != mc.status.media_session_id:
                            continue
                        pos = st.get("currentTime")
                        if not isinstance(pos, (float, int)) or not math.isfinite(pos):
                            continue
                        with self.intent:
                            if rev != self.revision:
                                return None
                            self.state, self.pos, self.stamp = "PLAYING", pos, (sent + now) / 2
                            return self.t0 + pos + now - self.stamp
                time.sleep(0.01)
        return None

    def confirm_quiet(self, rev):
        # Serializar com LOAD: até um LOAD que perdeu a resposta pode estar tocando.
        with self.lock:
            if rev != self.revision:
                return False
            if not self.may_sound:
                self.want_stop = False
                return True
            if not self.connected():
                if self.cc and not self.cc.socket_client.stop.is_set():
                    return False
                try:
                    self.connect()
                except Exception:
                    return False
            cc = self.cc
            if self.media_ready(cc):
                reply = self.fresh_status(cc.media_controller, 0.45)
                media = reply.get("status")
                if reply.get("type") != "MEDIA_STATUS" or not isinstance(media, list):
                    return False
                quiet = all(isinstance(st, dict) and st.get("playerState") == "IDLE" for st in media)
            else:
                reply = self.fresh_status(cc.socket_client.receiver_controller, 0.45)
                status = reply.get("status")
                quiet = reply.get("type") == "RECEIVER_STATUS" and isinstance(status, dict) and status.get("applications") == []
            if rev != self.revision:
                return False
            if not quiet:
                if self.stopping_token:
                    self.want_stop = True
                return False
            self.want_stop = self.may_sound = False
            self.stopping_token = ""
            with contextlib.suppress(OSError):
                self.owner_file.unlink(missing_ok=True)
            return True

    def watch(self):
        orphan_since = stop_try = no_receiver_since = 0.0
        stop_channel_ready = False
        while True:
            time.sleep(0.025 if time.monotonic() < self.until_fast_status else 0.20)
            now = time.monotonic()
            retry = self.connected() or self.stopping_token and (not self.cc or self.cc.socket_client.stop.is_set())
            ready = self.want_stop and self.connected() and self.media_ready(self.cc)
            if self.want_stop and retry and (now - stop_try > 1 or ready and not stop_channel_ready) and not self.lock.locked():
                stop_try = now
                threading.Thread(target=self.command, args=("stop", self.revision), daemon=True).start()
            stop_channel_ready = ready
            if not self.active:
                orphan_since = 0.0
                continue
            gone = now - remote.last_poll > 6 or remote.playing != self.video_id
            if gone:
                orphan_since = orphan_since or now
                if now - max(orphan_since, self.requested_at) > 4:
                    rev = self.stop_intent(self.sid)
                    if rev is not None:
                        LOG.info("Nest: o Xbox saiu da sessão; parando o som")
                        threading.Thread(target=self.command, args=("stop", rev), daemon=True).start()
                    continue
            else:
                orphan_since = 0.0
            if self.confirmed_revision != self.revision or not self.connected() or self.lock.locked():
                continue
            if not self.lock.acquire(blocking=False):
                continue
            try:
                if not self.active or self.owner != "server":
                    continue
                cc = self.cc
                rev = self.revision
                if not self.media_ready(cc):
                    no_receiver_since = no_receiver_since or now
                    replaced = cc.status and cc.status.app_id not in (None, self.APP)
                    if cc.status and cc.status.app_id is None and now - no_receiver_since > self.AUDIO_TIMEOUT:
                        no_receiver_since = now
                        reply = self.fresh_status(cc.socket_client.receiver_controller, 0.45)
                        replaced = reply.get("type") == "RECEIVER_STATUS" and (reply.get("status") or {}).get("applications") == []
                    if replaced:
                        with self.intent:
                            if rev == self.revision:
                                self.state = "IDLE"
                    continue
                no_receiver_since = 0.0
                mc = cc.media_controller
                mc.update_status()
                st = mc.status
                if st.player_state == "UNKNOWN":
                    continue
                with self.intent:
                    if rev != self.revision or not self.active:
                        continue
                    ours = self.matches(st)
                    if ours and st.player_state in ("PLAYING", "PAUSED", "BUFFERING"):
                        # update_status() só envia o pedido. A posição vale no last_updated da resposta assíncrona.
                        pos = float(st.current_time or 0)
                        if self.state != st.player_state or self.pos != pos:
                            age = (datetime.now(timezone.utc) - st.last_updated).total_seconds() if st.last_updated else 0.0
                            self.state, self.pos, self.stamp = st.player_state, pos, time.monotonic() - max(0.0, age)
                    elif (st.player_state == "IDLE" or st.content_id and not ours) and (
                            self.state != "LOADING" or now - self.requested_at > 3):
                        self.state = "IDLE"
            except Exception as error:
                LOG.debug("Nest status: %s", type(error).__name__)
            finally:
                self.lock.release()

    def position(self):
        with self.intent:
            if self.state == "LOADING":
                return self.state, self.target
            state, pos, stamp, base = self.state, self.pos, self.stamp, self.t0
            sent = self.audio_sent_at
        now = time.monotonic()
        age = max(0.0, now - stamp)
        if state in ("PLAYING", "BUFFERING") and (sent is None or now - sent > self.AUDIO_TIMEOUT):
            if age > self.AUDIO_TIMEOUT:
                return "STALLED", base + pos
            if state == "PLAYING" and age > 1.2:
                return "STALE", base + pos
        return state, base + pos + (age if state == "PLAYING" else 0.0)


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
async def nest_play(video_id: str = Query(..., alias="id"), t: float = Query(0, ge=0, le=86400 * 30),
                    sid: int | None = None, claim: int = 0, audio: str = "original"):
    valid_id(video_id)
    audio_choice(audio)
    if nest.owner != "server":
        raise HTTPException(409, "O console está com o Nest.")
    host = (CONFIG.get("nest") or {}).get("host")
    if not host:
        return PlainTextResponse("erro\t" + tr("Nenhum Nest configurado.", "No Nest configured.") + "\n")
    if sid is None:
        info = await resolve_video(video_id)
    else:
        # O ffmpeg de um vídeo curto pode terminar enquanto o Xbox ainda está consumindo o seu anel.
        if sid != streams.serial or sid <= nest.closed_sid or streams.video_id != video_id:
            raise HTTPException(409, "Esta sessão de vídeo terminou.")
        info = streams.info
    if info["live"] and sid is None:
        t = 0.0  # clientes antigos mandam a posição anterior ao reabrir a live
    if nest.local_sid is not None and nest.local_sid == streams.serial:
        if (sid != nest.local_sid or not claim or claim != nest.revision or nest.active
                or nest.may_sound or nest.want_stop):
            raise HTTPException(409, "A TV está com o áudio desta sessão.")
    elif claim:
        raise HTTPException(409, "Esta autorização de áudio expirou.")
    if sid is not None and audio=="original": audio=info.get("audio_selection","original")
    if audio=="original":
        accepted = nest.request_play(video_id, t, info["duration"], info["live"], lan_ip_towards(host), sid, gated=bool(claim))
    else:
        accepted = nest.request_play(video_id, t, info["duration"], info["live"], lan_ip_towards(host), sid, gated=bool(claim),audio_selection=audio)
    if accepted is False:
        raise HTTPException(409, "O console está com o Nest.")
    return PlainTextResponse(f"ok\t{nest.revision}\n")


@app.get("/api/nest/owner", response_class=PlainTextResponse)
async def nest_owner(owner: Literal["console", "server"]):
    if nest.owner != owner:
        nest.owner = owner
        LOG.info("Nest: dono=%s solicitado", owner)
        rev = nest.stop_intent(closing=False)
        if nest.may_sound:
            threading.Thread(target=nest.command, args=("stop", rev), daemon=True).start()
        else:
            nest.want_stop = False
            nest.stopping_token = ""
    rev = nest.revision
    if owner == "server":
        # O chamador só pede a posse depois que o worker nativo fechou o seu Cast.
        return PlainTextResponse(f"ok\t{rev}\n")
    quiet = not nest.lock.locked() and await asyncio.to_thread(nest.confirm_quiet, rev)
    if quiet and nest.owner == owner and nest.revision == rev:
        with nest.lock:
            if nest.owner == owner and nest.revision == rev:
                nest.drop()
                nest.ours = False
                LOG.info("Nest: dono=console; servidor silencioso e Cast fechado rev=%d", rev)
                return PlainTextResponse(f"QUIET\t{rev}\n")
    return PlainTextResponse(f"WAIT\t{rev}\n")


@app.get("/api/nest/local", response_class=PlainTextResponse)
async def nest_local(sid: int, rev: int = 0):
    if sid != streams.serial or sid <= nest.closed_sid:
        raise HTTPException(409, "Esta sessão de vídeo terminou.")
    if nest.local_sid != sid or nest.gated_revision:
        if rev and rev != nest.revision:
            raise HTTPException(409, "Esta troca de áudio terminou.")
        nest.local_sid = sid
        stop_rev = nest.stop_intent(sid, closing=False)
        if nest.may_sound:
            threading.Thread(target=nest.command, args=("stop", stop_rev), daemon=True).start()
        else:
            nest.want_stop = False
            nest.stopping_token = ""
    current = nest.revision
    # Sem LOAD pendente, a revisão já cancelou o LAUNCH. Não prender a TV ao timeout da pychromecast.
    quiet = not nest.may_sound or not nest.lock.locked() and await asyncio.to_thread(nest.confirm_quiet, current)
    if current != nest.revision or sid != nest.local_sid:
        raise HTTPException(409, "Esta troca de áudio terminou.")
    return PlainTextResponse(f"{'QUIET' if quiet else 'WAIT'}\t{current}\n")


@app.get("/api/nest/unmute", response_class=PlainTextResponse)
async def nest_unmute(sid: int, rev: int):
    if sid != streams.serial or sid != nest.sid or rev != nest.revision or rev != nest.reader_ready:
        raise HTTPException(409, "Esta troca de áudio terminou.")
    nest.until_fast_status = time.monotonic() + 1.5
    nest.local_sid = None
    nest.gated_revision = 0
    pos = await asyncio.to_thread(nest.started_position, rev)
    if pos is None:
        raise HTTPException(503, "O Nest não confirmou o início.")
    return PlainTextResponse(f"ok\t{pos:.4f}\n")


@app.get("/api/nest/prepare", response_class=PlainTextResponse)
async def nest_prepare():
    if not (CONFIG.get("nest") or {}).get("host"):
        return PlainTextResponse("erro\t" + tr("Nenhum Nest configurado.", "No Nest configured.") + "\n")
    threading.Thread(target=nest.prepare, daemon=True).start()
    return PlainTextResponse("ok\n")


@app.get("/api/nest/pos", response_class=PlainTextResponse)
async def nest_pos(sid: int | None = None):
    state, pos = nest.position() if sid is None or sid == nest.sid else ("IDLE", 0.0)
    armed = nest.reader_ready if nest.reader_ready == nest.gated_revision else 0
    return PlainTextResponse(f"{state}\t{pos:.3f}\t{clean(nest.error)}\t{armed}\n")


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
async def nest_command(what: Literal["pause", "resume", "stop"], sid: int | None = None):
    if sid is None or sid == nest.sid or (what == "stop" and sid == streams.serial):
        rev = nest.stop_intent(sid) if what == "stop" else nest.revision
        if rev is not None:
            threading.Thread(target=nest.command, args=(what, rev), daemon=True).start()
    return PlainTextResponse("ok\n")


@app.post("/api/remote/cmd")
async def command(cmd: Command):
    global nav_queue_serial
    if cmd.action in ("play", "queue"):
        valid_id(cmd.id or "")
    if cmd.action == "seek" and cmd.t is None and cmd.delta is None:
        raise HTTPException(400, "Informe t ou delta para buscar.")
    if (cmd.action == "queue" and len(remote.queue) >= 256) or (cmd.action != "queue" and len(remote.commands) >= 256):
        raise HTTPException(429, "A fila está cheia.")
    if cmd.action in ("play", "stop"):
        # O celular assumiu: descarta também a fila pronta e os comandos locais ainda não entregues.
        nav_queue_serial += 1
        remote.queue = deque(v for v in remote.queue if v.get("source") != "navigation")
        remote.commands = deque(c for c in remote.commands if c.get("source") != "navigation")
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


# ---------------------------------------------------------------- legendas e metadados do player
caption_info_cache = TTLCache(900, 16)
caption_text_cache = TTLCache(900, 24)
CAPTION_BYTES = 2 << 20


def caption_plain(text):
    from html import unescape
    return clean(unescape(re.sub(r"<[^>]*>", "", text)))


def caption_time(text):
    parts = text.replace(",", ".").split(":")
    value = 0.0
    for part in parts:
        value = value * 60 + float(part)
    return value


def caption_cues(raw, ext, automatic=False):
    """VTT do YouTube repete a linha anterior em janelas sobrepostas; não deduplicar falas distantes."""
    cues = []
    if ext == "json3":
        for event in json.loads(raw).get("events", []):
            text = "".join(segment.get("utf8", "") for segment in event.get("segs", []))
            start = float(event.get("tStartMs", 0)) / 1000
            end = start + float(event.get("dDurationMs", 0)) / 1000
            if event.get("aAppend") and cues and start <= cues[-1][1]:
                previous = cues[-1]
                if text.strip():
                    # O texto anexado só existe a partir deste evento, não desde o cartão anterior.
                    cues.append((start, max(previous[1], end), previous[2] + text))
                else:
                    cues[-1] = (previous[0], max(previous[1], end), previous[2])
            else:
                cues.append((start, end, text))
    else:
        stamp = r"(?:\d+:)?\d{2}:\d{2}[.,]\d{3}"
        for block in re.split(r"\n\s*\n", raw.replace("\r", "")):
            match = re.search(rf"({stamp})\s+-->\s+({stamp})[^\n]*\n([\s\S]*)", block)
            if match:
                cues.append((caption_time(match[1]), caption_time(match[2]), match[3]))
    result, previous_words, previous_end = [], [], -1
    for start, end, raw_text in sorted(cues):
        if not math.isfinite(start) or not math.isfinite(end) or start < 0 or end <= start:
            continue
        text = caption_plain(raw_text)
        words = text.split()
        if not words:
            continue
        fresh = words
        if automatic and start <= previous_end + 0.15:
            for count in range(min(len(previous_words), len(words)), 0, -1):
                if previous_words[-count:] == words[:count]:
                    fresh = words[count:]
                    break
        previous_words, previous_end = words, end
        if not fresh:
            if result:
                result[-1] = (result[-1][0], max(result[-1][1], end), result[-1][2])
            continue
        text = " ".join(fresh)
        if result and start < result[-1][1]:
            result[-1] = (result[-1][0], start, result[-1][2])
        # Duas linhas legíveis na TV: frases longas viram cartões consecutivos, sem perder palavras.
        chunks, chunk = [], ""
        for word in text.split():
            if chunk and len(chunk) + len(word) + 1 > 100:
                chunks.append(chunk); chunk = ""
            chunk += (" " if chunk else "") + word
        if chunk:
            chunks.append(chunk)
        length, offset = sum(len(c) for c in chunks), 0
        for chunk in chunks:
            until = offset + len(chunk)
            result.append((start + (end - start) * offset / length, start + (end - start) * until / length, chunk))
            offset = until
    return [cue for cue in result if cue[1] > cue[0]][:20000]


def caption_tracks(data):
    tracks, seen = [], set()
    for kind, field in (("manual", "subtitles"), ("auto", "automatic_captions")):
        for code, formats in data.get(field, {}).items():
            if code in seen or not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", code) or code == "live_chat":
                continue
            usable = [f for f in formats if f.get("url") and f.get("ext") in ("json3", "vtt")]
            if not usable:
                continue
            source = min(usable, key=lambda f: f["ext"] != "json3")
            seen.add(code)
            tracks.append({"code": code, "name": clean(source.get("name") or code), "kind": kind, "source": source})
    return tracks[:256]


def caption_select(tracks, lang, track=""):
    if track:
        return next((t for t in tracks if t["code"] == track), None)
    # Idioma da interface primeiro; dentro dele, a legenda do criador sempre ganha da automática.
    # A tradução para o próprio idioma pode receber 429 enquanto a faixa ASR original continua disponível.
    return min(tracks, key=lambda t: (t["code"].split("-")[0] != lang, t["kind"] != "manual",
                                      not t["code"].endswith("-orig")), default=None)


async def caption_info(video_id, lang):
    async def load():
        return await extract_video(f"https://www.youtube.com/watch?v={video_id}", lang=lang,
                                   skip_download=True, writeautomaticsub=True, ignore_no_formats_error=True)
    return await caption_info_cache.get((video_id, lang), load)


async def caption_download(source, headers):
    def load():
        with urlopen(URLRequest(source["url"], headers=headers | source.get("http_headers", {})), timeout=12) as response:
            raw = response.read(CAPTION_BYTES + 1)
        if len(raw) > CAPTION_BYTES:
            raise ValueError("caption size")
        return raw.decode("utf-8-sig")
    return await asyncio.to_thread(load)


@app.get("/api/captions/{video_id}", response_class=PlainTextResponse)
async def captions(video_id: str, lang: Literal["pt", "en", "es"] = "en", track: str = ""):
    valid_id(video_id)
    if track and not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", track):
        raise HTTPException(400, "Idioma inválido.")
    try:
        async with asyncio.timeout(25):
            data = await caption_info(video_id, lang)
            tracks = caption_tracks(data)
            selected = caption_select(tracks, lang, track)
            live = data.get("is_live") or data.get("live_status") == "is_live"
            cues, reason = [], ""
            if selected and live:
                # O MPEG da live começa em zero; timedtext conta desde o início da transmissão. Sem a origem
                # HLS usada pelo player, mostrar essas frases seria fora de sincronia.
                reason = "live_clock"
            elif selected:
                async def load():
                    source = selected["source"]
                    raw = await caption_download(source, data.get("http_headers", {}))
                    return await asyncio.to_thread(caption_cues, raw, source["ext"], selected["kind"] == "auto")
                cues = await caption_text_cache.get((video_id, selected["code"], selected["kind"]), load)
                if not cues:
                    reason = "live_unavailable" if live else "unavailable"
            else:
                reason = "language_missing" if track else "live_unavailable" if live else "unavailable"
    except Exception as error:
        LOG.info("legendas %s: extração ou download indisponível (%s, HTTP %s)", video_id,
                 type(error).__name__, getattr(error, "code", "-"))
        return PlainTextResponse("erro\tfailed\n", headers={"Cache-Control": "no-store"})
    lines = [f"ok\t{selected['code'] if selected else ''}\t{selected['kind'] if selected else 'none'}\t{int(bool(live))}\t{reason}"]
    date = str(data.get("upload_date") or "")
    if re.fullmatch(r"\d{8}", date):
        date = f"{date[:4]}-{date[4:6]}-{date[6:]}"
    lines.append("meta\t" + "\t".join(clean(data.get(k)) for k in ("title", "channel")) + "\t" + clean(date) + "\t" + clean(data.get("channel_id")))
    lines.extend(f"track\t{t['code']}\t{t['name']}\t{t['kind']}" for t in tracks)
    if not live:
        for chapter in (data.get("chapters") or [])[:128]:
            start = chapter.get("start_time", -1)
            if isinstance(start, (int, float)) and math.isfinite(start) and start >= 0:
                lines.append(f"chapter\t{start:.3f}\t{clean(chapter.get('title'))}")
    lines.extend(f"cue\t{start:.3f}\t{end:.3f}\t{text}" for start, end, text in cues)
    body = "\n".join(lines) + "\n"
    if len(body.encode("utf-8")) > CAPTION_BYTES:
        return PlainTextResponse("erro\ttoo_large\n", headers={"Cache-Control": "no-store"})
    return PlainTextResponse(body, headers={"Cache-Control": "no-store"})


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    # h11 respeita o enquadramento por EOF do HTTP/1.0 usado pelo console.
    # sem o limite, um vídeo aberto segura o desligamento até o systemd matar o processo
    uvicorn.run(app, host="0.0.0.0", port=PORT, http="h11", access_log=False, timeout_graceful_shutdown=3)
