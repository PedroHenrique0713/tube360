import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
from tempfile import TemporaryDirectory
from urllib.error import HTTPError

spec=importlib.util.spec_from_file_location('site_build','site/build.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
real_github=build.github
calls=[]
def github(path):
    calls.append(path)
    if path.endswith('page=1'):
        return [{'assets':[{'name':'app.zip','download_count':10},{'name':'app.7z','download_count':20},{'name':'app.xex','download_count':30},{'name':'app.zip.sha256','download_count':999},{'name':'source.tar.gz','download_count':999}]}]+[{'assets':[]}]*99
    return [{'assets':[{'name':'APP.ZIP','download_count':7}]}]
build.github=github
assert build.download_count()==67
assert calls==['/releases?per_page=100&page=1','/releases?per_page=100&page=2']
base=json.loads(Path('site/src/github.json').read_text())
with TemporaryDirectory() as folder:
    build.CACHE=Path(folder)/'github.json';build.CACHE.write_text(json.dumps(base))
    args=SimpleNamespace(offline=True,no_cache_write=True,version=None,download_url=None,size=None,stars=None,downloads=None)
    def no_network(path): raise AssertionError('offline accessed network')
    build.github=no_network
    assert build.get_data(args)['download_count']==base['download_count']
    for invalid in [-1,True,1.5,'97']:
        try: build.valid_data({**base,'download_count':invalid})
        except ValueError: pass
        else: raise AssertionError(f'accepted {invalid!r}')
    build.github=lambda path: (_ for _ in ()).throw(HTTPError('https://api.github.com',403,'limit',{'X-RateLimit-Reset':'9999999999'},None))
    args.offline=False
    fallback=build.get_data(args)
    assert fallback['download_count']==base['download_count'] and fallback['retry_after']==9999999999
    args.offline=True;args.downloads=1234
    assert build.get_data(args)['download_count']==1234
    assert json.loads(build.CACHE.read_text())==base
print('PASS: package suffixes/checksum exclusion, all-release pagination, invalid counts, offline with zero requests, API 403 fallback, CLI override, no-cache-write.')

# A different offline version must not inherit the old publication date.
with TemporaryDirectory() as folder:
    build.CACHE=Path(folder)/'github.json';build.CACHE.write_text(json.dumps(base))
    args=SimpleNamespace(offline=True,no_cache_write=True,version='v9.9.9',download_url=None,size=None,stars=None,downloads=None,published_at=None)
    assert 'published_at' not in build.get_data(args)
    args.published_at='2026-11-25T12:00:00Z'
    assert build.get_data(args)['published_at']==args.published_at
    for invalid in ('not-a-date','2026-11-25T12:00:00',123):
        try:build.valid_data({**base,'published_at':invalid})
        except (ValueError,TypeError):pass
        else:raise AssertionError('invalid published_at accepted')
print('PASS: release timestamps/timezones, offline publication override and version/date isolation.')
