import asyncio
from datetime import datetime, timezone
import importlib.util
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, patch
from urllib.parse import unquote, urlparse, parse_qs


class AudioTracksTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        with patch.dict(os.environ,HOME=self.tmp.name,TUBE360_CONFIG=str(Path(__file__).with_name("config.example.json")),TUBE360_ACCOUNT_URL="http://127.0.0.1:1"):
            spec=importlib.util.spec_from_file_location("audio_tracks_server",Path(__file__).with_name("tube360_server.py"))
            self.m=importlib.util.module_from_spec(spec); spec.loader.exec_module(self.m)
        self.video=dict(url="https://media.test/720",width=1280,height=720,fps=30,vcodec="avc1",acodec="none",protocol="https")
        self.en=dict(url="https://media.test/en",ext="m4a",abr=128,language="en",format_note="English (original)",vcodec="none",acodec="mp4a",protocol="https")
        self.pt=self.en|dict(url="https://media.test/pt",language="pt",format_note="Portuguese (auto-dubbed)")
        self.info=dict(video=self.video,audio=self.pt,audio_tracks=self.m.audio_catalog([self.pt,self.en]),duration=40,title="fixture",headers={},live=False,original={720:self.video})

    async def asyncTearDown(self):
        for task in list(self.m.background): task.cancel()
        await asyncio.gather(*self.m.background,return_exceptions=True)

    def test_original_and_interface_choices_do_not_mutate_shared_resolver(self):
        original=self.m.choose_audio(self.info)
        self.assertEqual(original["audio"]["url"],self.en["url"])
        self.assertEqual(original["audio_selected"],"en-original")
        self.assertIs(self.info["audio"],self.pt)
        for choice in ("pt-auto","lang:pt"):
            self.assertEqual(self.m.choose_audio(self.info,choice)["audio"]["url"],self.pt["url"])
        self.assertEqual(self.m.choose_audio(self.info,"pt-BR-auto")["audio"]["url"],self.pt["url"])
        self.assertEqual(self.m.choose_audio(self.info,"lang:ja")["audio"]["url"],self.en["url"])

    def test_profiles_and_high_frame_rate_ceilings(self):
        formats=[self.video,self.video|dict(height=1080,width=1920,url="https://media.test/1080"),self.video|dict(fps=60,url="https://media.test/60"),self.video|dict(height=1080,width=1920,fps=60,url="https://media.test/reject")]
        info=self.info|dict(video_formats=formats)
        self.assertEqual(self.m.copy_source(info,720,"720p60")["url"],"https://media.test/60")
        for mode in ("1080p30","auto"):
            self.assertEqual(self.m.copy_source(info,1080,mode)["url"],"https://media.test/1080")
        self.assertIsNone(self.m.copy_source(info,480,"720p60"))
        self.assertEqual(self.m.copy_source(info,1080,"720p60")["url"],"https://media.test/60")

    def test_untrusted_track_values_are_rejected(self):
        for value in ("../../file", "pt\n-auto", "a"*100, "lang:pt?x=1"):
            with self.assertRaises(self.m.HTTPException): self.m.audio_choice(value)

    async def test_cache_and_concurrent_jobs_are_partitioned_by_track(self):
        self.m.AUDIO_DIR.mkdir(parents=True)
        original=self.m.AUDIO_DIR/"abcdefghijk_0.00.mp3";original.write_bytes(b"original")
        done=asyncio.Event()
        class Job:
            returncode=None
            async def wait(self): await done.wait(); return 0
        job=Job(); create=AsyncMock(return_value=job)
        with patch.object(self.m,"resolve_video",AsyncMock(return_value=self.info)),patch.object(self.m.asyncio,"create_subprocess_exec",create):
            first,second=await asyncio.gather(self.m.audio_file("abcdefghijk",0,"pt-auto"),self.m.audio_file("abcdefghijk",0,"pt-auto"))
            path,process=first
            self.assertEqual(first,second);self.assertIs(process,job);self.assertEqual(create.await_count,1)
            self.assertNotEqual(path,original);self.assertIn("~",path.name)
            command=create.call_args.args
            self.assertIn(self.pt["url"],command);self.assertNotIn(self.en["url"],command)
            self.assertEqual(await self.m.audio_file("abcdefghijk",0),(original,None))

    async def test_stream_carries_catalog_and_selected_audio(self):
        encoder=SimpleNamespace(output=asyncio.Queue(),process=SimpleNamespace(returncode=None),mp3=None)
        encoder.output.put_nowait(b"media");encoder.output.put_nowait(None)
        request=SimpleNamespace(is_disconnected=AsyncMock(return_value=False))
        start=AsyncMock(return_value=encoder)
        with patch.object(self.m,"resolve_video",AsyncMock(return_value=self.info)),patch.object(self.m.streams,"begin",AsyncMock(return_value=7)),patch.object(self.m.streams,"start",start),patch.object(self.m.streams,"finish",AsyncMock()):
            response=await self.m.stream(request,"abcdefghijk",t=0,q=720,a=1,speaker=0,codec="h264",audio="pt-auto")
            self.assertEqual(unquote(response.headers["X-Audio-Track"]),"pt-auto")
            self.assertIn("en-original",unquote(response.headers["X-Audio-Tracks"]))
            self.assertIn(self.pt["url"],start.call_args.args[1]);self.assertNotIn(self.en["url"],start.call_args.args[1])
            self.assertEqual(b"".join([p async for p in response.body_iterator]),b"media")

    async def test_nest_loads_selected_track_instead_of_seeking_old_language(self):
        n=self.m.nest;calls=[]
        status=SimpleNamespace(content_id="",player_state="IDLE",media_session_id=1,current_time=0,last_updated=datetime.now(timezone.utc))
        def play(url,*args,**kwargs):
            calls.append(url);status.content_id=url;status.player_state="PLAYING";status.last_updated=datetime.now(timezone.utc)
        media=SimpleNamespace(namespace="urn:x-cast:com.google.cast.media",status=status,play_media=play,update_status=lambda:None,seek=lambda *a,**k:self.fail("old-language MP3 must not be reused with SEEK"))
        cast=SimpleNamespace(status=SimpleNamespace(app_id=n.APP),media_controller=media,socket_client=SimpleNamespace(app_namespaces=[media.namespace]))
        n.connect=lambda:cast;n.ours=True
        for choice in ("pt-auto","en-original"):
            n.request_play("abcdefghijk",0,40,False,"127.0.0.1",None,audio_selection=choice)
            deadline=time.monotonic()+3
            while n.confirmed_revision!=n.revision and time.monotonic()<deadline: await asyncio.sleep(.01)
            self.assertEqual(n.confirmed_revision,n.revision)
            self.assertEqual(parse_qs(urlparse(calls[-1]).query)["track"],[choice])
        self.assertEqual(len(calls),2)
