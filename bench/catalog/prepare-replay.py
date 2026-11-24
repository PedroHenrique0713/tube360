"""Prepara a prova do app com catálogo público gravado e AAC sintético de quatro segundos."""
import json
import pathlib
import shutil
import subprocess
import sys

source = pathlib.Path(__file__).parent / 'fixtures'
dest = pathlib.Path(sys.argv[1]).resolve()
dest.mkdir(parents=True, exist_ok=True)
for path in source.iterdir():
    shutil.copyfile(path, dest / path.name)

requests = []

def route(origin, endpoint, fields, filename):
    requests.append({'url': origin + endpoint, 'fields': fields, 'file': filename})

music, web = 'https://music.youtube.com/', 'https://www.youtube.com/'
route(music, '', {}, 'music-context.html')
route(web, '', {}, 'web-context.html')
browse, search, nxt = ['youtubei/v1/' + n + '?prettyPrint=false' for n in ('browse', 'search', 'next')]
for fields, name in [
    ({'browseId': 'FEmusic_home'}, 'music-home'),
    ({'browseId': 'FEmusic_explore'}, 'music-explore'),
    ({'browseId': 'FEmusic_charts'}, 'music-charts'),
    ({'browseId': 'MPREb_GuNCgmKDLZT'}, 'music-album'),
    ({'browseId': 'UCMJqedJ0OZgh9mjdRf26Xzg'}, 'music-artist'),
    ({'browseId': 'VLPLF12'}, 'playlist'),
    ({'browseId': 'MPLYfixture'}, 'lyrics'),
]:
    route(music, browse, fields, name + '.json')
route(web, browse, {'browseId': 'VLPLF12'}, 'playlist.json')
route(web, browse, {'continuation': 'CONT_PLAYLIST'}, 'playlist-more.json')
route(music, nxt, {'playlistId': 'RDAMVMF12audio001'}, 'radio.json')
route(music, nxt, {'videoId': 'F12audio001'}, 'lyrics-next.json')
route(music, search, {'query': 'jazz instrumental'}, 'music-search.json')
cid = 'UC_x5XG1OV2P6uZZ5FSM9Ttw'
channel = json.loads((source / 'channel.json').read_text())
for tab in channel['contents']['twoColumnBrowseResultsRenderer']['tabs']:
    renderer = tab.get('tabRenderer', {})
    ep = renderer.get('endpoint', {})
    url = ep.get('commandMetadata', {}).get('webCommandMetadata', {}).get('url', '')
    for name, suffix in [('videos', '/videos'), ('live', '/streams'), ('playlists', '/playlists')]:
        if url.endswith(suffix):
            route(web, browse, {'browseId': cid, 'params': ep['browseEndpoint']['params']}, 'channel-' + name + '.json')
route(web, browse, {'browseId': cid}, 'channel.json')
for token in ('CONT_channel_videos', 'CONT_channel_live', 'CONT_channel_playlists'):
    route(web, browse, {'continuation': token}, 'continuation.json')
# A primeira continuação pública da Home é encerrada nesta prova de interface.
(dest / 'empty.json').write_text('{"continuationContents":{"sectionListContinuation":{"contents":[]}}}')
route(music, browse, {'continuation': 'CONT_music_home'}, 'empty.json')
route(web, 'sw.js_data', {}, 'tv-session.txt')
for request in list(requests):
    if request['url'].startswith(music+'youtubei/'):
        requests.append({**request, 'url': request['url']+'&alt=json'})

subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                'sine=frequency=440:sample_rate=44100:duration=4', '-ac', '2', '-c:a', 'aac',
                '-b:a', '128k', '-movflags', '+faststart', str(dest / 'audio.m4a')], check=True)
subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                'testsrc2=size=1280x720:rate=30:duration=4', '-f', 'lavfi', '-i',
                'sine=frequency=440:sample_rate=44100:duration=4', '-ac', '2', '-c:v', 'libx264',
                '-threads', '2', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p', '-g', '30',
                '-c:a', 'aac', '-movflags', '+faststart', str(dest / 'video.mp4')], check=True)
requests.append({'url': 'https://fixture.googlevideo.com/unused-video', 'file': 'video.mp4', 'range': True})

subprocess.run(['magick', '-size', '320x180', 'gradient:#67396b-#d39465', str(dest / 'art.png')], check=True)
subprocess.run(['magick', '-size', '1060x175', 'gradient:#124748-#572b62', str(dest / 'banner.png')], check=True)

urls = set()
video_ids = set()
banners = set()

def images(value, key=''):
    if isinstance(value, dict):
        for k, v in value.items():
            if k == 'videoId' and isinstance(v, str):
                video_ids.add(v)
            elif k == 'url' and isinstance(v, str) and any(h in v for h in ('ytimg.com/', 'googleusercontent.com/', 'ggpht.com/')):
                urls.add(v)
                if key in ('banner', 'imageBannerViewModel'):
                    banners.add(v)
            else:
                images(v, key if key in ('banner', 'imageBannerViewModel') else k)
    elif isinstance(value, list):
        for v in value:
            images(v, key)

for path in source.glob('*.json'):
    images(json.loads(path.read_text()))
urls.update('https://i.ytimg.com/vi/' + vid + '/mqdefault.jpg' for vid in video_ids)
for url in sorted(urls):
    requests.append({'url': url, 'file': 'banner.png' if url in banners else 'art.png'})
for i in range(1, 4):
    vid = 'F12audio00' + str(i)
    session = '<script>{"VISITOR_DATA":"fixture-visitor","jsUrl":"/s/player/fixture/base.js"}</script>'
    (dest / 'watch.html').write_text(session)
    route(web, 'watch?v=' + vid + '&bpctr=9999999999&has_verified=1', {}, 'watch.html')
    (dest / 'base.js').write_text('var player={signatureTimestamp:20775};')
    route(web, 's/player/fixture/base.js', {}, 'base.js')
    data = {
        'playabilityStatus': {'status': 'OK'},
        'videoDetails': {'videoId': vid, 'title': 'Faixa de teste ' + str(i), 'lengthSeconds': '4'},
        'streamingData': {'expiresInSeconds': '3600', 'formats': [
            {'itag': 22, 'mimeType': 'video/mp4; codecs="avc1, mp4a"', 'height': 720,
             'width': 1280, 'fps': 30, 'url': 'https://fixture.googlevideo.com/unused-video'}],
            'adaptiveFormats': [
                {'itag': 136, 'mimeType': 'video/mp4; codecs="avc1"', 'height': 720,
                 'width': 1280, 'fps': 30, 'url': 'https://fixture.googlevideo.com/unused-video'},
                {'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a"', 'bitrate': 128000,
                 'url': 'https://fixture.googlevideo.com/audio-' + vid + '.m4a'}]},
    }
    filename = 'player-' + vid + '.json'
    (dest / filename).write_text(json.dumps(data))
    route(web, 'youtubei/v1/player?prettyPrint=false', {'videoId': vid}, filename)
    requests.append({'url': 'https://fixture.googlevideo.com/audio-' + vid + '.m4a', 'file': 'audio.m4a', 'range': True})
(dest / 'responses.json').write_text(json.dumps({'requests': requests}))
assets = dest / 'assets'
assets.mkdir(exist_ok=True)
for font in (source.parents[2] / 'client/assets').glob('inter-*.ttf'):
    shutil.copyfile(font, assets / font.name)
(assets / 'settings.txt').write_text('lang=pt\ncols=4\nquality=720\nsound=tv\nserver=off\n')
print(dest)
