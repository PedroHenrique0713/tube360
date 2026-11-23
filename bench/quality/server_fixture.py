"""Servidor real + mídia/conta falsas locais para a prova de stream/idioma/Nest, sem YouTube."""
import asyncio
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import threading

root=Path(__file__).resolve().parents[2]
media=Path(sys.argv[1]).resolve()
if not os.environ.get("TUBE360_CONFIG") or Path.home()==Path("/home/pedrohenriquequadro"):
    raise RuntimeError("Use HOME e TUBE360_CONFIG isolados no worktree")
port=int(os.environ.get("TUBE360_ACCOUNT_PORT","8235"))


class Files(BaseHTTPRequestHandler):
    def do_GET(self):
        path=media/Path(self.path.split("?",1)[0]).name
        if not path.is_file() or path.suffix not in (".mp4",".m4a",".ts",".m3u8"):
            body=json.dumps(dict(state="signed_out",signed_in=False,items=[])).encode()
            self.send_response(200);self.send_header("Content-Length",str(len(body)));self.end_headers();self.wfile.write(body);return
        size=path.stat().st_size;start=0;end=size-1
        match=re.fullmatch(r"bytes=(\d+)-(\d*)",self.headers.get("Range",""))
        if match: start=int(match[1]);end=min(end,int(match[2]) if match[2] else end)
        if start> end:
            self.send_error(416);return
        self.send_response(206 if match else 200)
        self.send_header("Accept-Ranges","bytes");self.send_header("Content-Length",str(end-start+1))
        if match:self.send_header("Content-Range",f"bytes {start}-{end}/{size}")
        self.end_headers()
        try:
            with path.open("rb") as f:
                f.seek(start);remaining=end-start+1
                while remaining:
                    chunk=f.read(min(65536,remaining))
                    if not chunk: break
                    self.wfile.write(chunk);remaining-=len(chunk)
        except (BrokenPipeError,ConnectionResetError): pass

    def log_message(self,*args): pass


files=ThreadingHTTPServer(("127.0.0.1",port),Files)
threading.Thread(target=files.serve_forever,daemon=True).start()
spec=importlib.util.spec_from_file_location("tube360_quality_server",root/"server/tube360_server.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
formats=[]
for tag,height,fps,name in [(137,1080,30,"1080.mp4"),(136,720,30,"720.mp4"),(298,720,60,"72060.mp4")]:
    formats.append(dict(format_id=str(tag),url=f"http://127.0.0.1:{port}/{name}",width=height*16//9,height=height,fps=fps,vcodec="avc1",acodec="none",protocol="https",ext="mp4"))
for language,name,note in [("en","audio.m4a","English (original)"),("pt","audio-pt.m4a","Portuguese (auto-dubbed)")]:
    formats.append(dict(url=f"http://127.0.0.1:{port}/{name}",vcodec="none",acodec="mp4a",protocol="https",ext="m4a",language=language,format_note=note,abr=128))
info=dict(video=formats[1],audio=formats[-2],video_formats=formats,audio_tracks=m.audio_catalog(formats),duration=40,live=False,title="F15 — servidor / faixas",headers={},original={720:formats[1],1080:formats[0]})


async def resolve(video_id): return info
async def extract(*args,**kwargs): return dict(id="4p_9OPKgcDE",formats=formats,subtitles={},automatic_captions={},duration=40,title=info["title"])
async def empty(*args,**kwargs): return []
m.resolve_video=resolve;m.extract_video=extract;m.home_videos=empty;m.search_videos=empty
import uvicorn
uvicorn.run(m.app,host="127.0.0.1",port=int(os.environ.get("TUBE360_PORT","8135")),log_level="info")
