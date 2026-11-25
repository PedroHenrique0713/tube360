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

# The changelog drives dates, order and inclusion; locale files hold reusable wording only.
markdown=Path('CHANGELOG.md').read_text()
releases=build.changelog_releases(markdown)
assert [r['version'] for r in releases]==['0.3.2','0.3.1','0.3.0']
assert [len(r['items']) for r in releases]==[7,4,6]
assert releases[0]['date'] is None and releases[1]['date']=='2026-11-24'
for release in releases:
    for item in release['items']:
        assert markdown.splitlines()[item['line']-1].startswith('- ')
# Removing a source change removes its summary without editing site dictionaries.
removed=build.changelog_releases(markdown.replace('- Public search suggestions','- A different change'))
assert 'search' not in [item['key'] for item in removed[0]['items']]
# A new source heading/date changes the teaser and release URL automatically.
future=build.changelog_releases(markdown.replace('## [0.3.2]','## [0.3.3] — 2027-01-02',1))
for lang in ['en','pt-BR']:
    dictionary=json.loads(Path(f'site/src/i18n/{lang}.json').read_text())
    teaser,history=build.release_sections(releases,dictionary,lang)
    assert 'v0.3.2' not in history and 'datetime="2026-11-24"' in history
    assert 'release-0-3-2' in history and 'data-changelog-line=' in history
    changed_teaser,changed_history=build.release_sections(future,dictionary,lang)
    assert '0.3.3' in changed_teaser and '/releases/tag/v0.3.3' in changed_history
    assert 'datetime="2027-01-02"' in changed_history
    dictionary['release_summaries']['local']='<script>alert(1)</script>'
    assert '<script>' not in build.release_sections(releases,dictionary,lang)[0]
print('PASS: CHANGELOG versions/dates, 3–7 sourced summaries per release, source removal, future version/date propagation, pending URL and HTML escaping in EN/PT.')
try:build.render('<p>{{native}}</p>',{'native':{'Settings':'Settings'}})
except ValueError as error:assert 'scalar' in str(error)
else:raise AssertionError('a translation dictionary leaked into visible page copy')
print('PASS: template rejects nested translation objects instead of exposing internal data.')

# F51: one complete pagination pass supplies both the total and every version.
def version_pages(path):
    if path.endswith('page=1'):
        return [{'tag_name':'v0.3.2','assets':[{'name':'app.zip','download_count':10},{'name':'app.7z','download_count':20},{'name':'app.xex','download_count':30},{'name':'SHA256SUMS','download_count':999}]}]+[{'tag_name':f'v-empty-{i}','assets':[]} for i in range(99)]
    return [{'tag_name':'v0.3.1','assets':[{'name':'APP.ZIP','download_count':7}]}]
build.github=version_pages
total,versions=build.download_statistics()
assert total==67 and versions[0]['downloads']==60 and versions[-1]['downloads']==7
assert sum(row['downloads'] for row in versions)==total
build.valid_data({**base,'download_count':total,'version_downloads':versions})
for invalid in ([{'tag_name':'v1','downloads':True}], [{'tag_name':'v1','downloads':-1}], [{'tag_name':'v1','downloads':66}], '67'):
    try:build.valid_data({**base,'download_count':67,'version_downloads':invalid})
    except ValueError:pass
    else:raise AssertionError(f'invalid breakdown accepted: {invalid}')
for count,goal in [(0,25),(24,25),(25,50),(50,100),(100,250),(250,500),(500,1000),(999,1000),(1000,2500)]:
    assert build.next_star_goal(count)==goal
for lang in ['en','pt-BR']:
    dictionary=json.loads(Path(f'site/src/i18n/{lang}.json').read_text())
    values=build.community_values({**base,'download_count':67,'version_downloads':versions},dictionary,lang)
    assert 'v0.3.2' in values['version_bars'] and '60' in values['version_bars']
    assert json.loads(values['community_json'])['downloads']==67
    assert '<script>' not in build.community_values({**base,'version_downloads':[{'tag_name':'<script>','downloads':base['download_count']}]},dictionary,lang)['version_bars']
# An explicit total must never inherit a version breakdown for a different total.
with TemporaryDirectory() as folder:
    build.CACHE=Path(folder)/'github.json';build.CACHE.write_text(json.dumps({**base,'download_count':67,'version_downloads':versions}))
    args=SimpleNamespace(offline=True,no_cache_write=True,version=None,download_url=None,size=None,stars=None,downloads=68,published_at=None)
    assert 'version_downloads' not in build.get_data(args)
print('PASS: F51 complete version totals, pagination, checksum exclusion, invalid breakdowns, milestone boundaries, escaped snapshot/HTML and offline total override.')

# Exercise the real HTTP fallback, not a replacement implementation of github().
from io import BytesIO
real_urlopen=build.urlopen
requests=[]
def migration_response(request,timeout):
    requests.append(request.full_url)
    if '/revive360/' in request.full_url:
        raise HTTPError(request.full_url,404,'Not Found',{},None)
    return BytesIO(b'{"stargazers_count":11}')
try:
    build.github=real_github;build.urlopen=migration_response;build.API_REPO=build.REPO
    assert real_github('')['stargazers_count']==11
    assert requests==['https://api.github.com/repos/revive360/tube360','https://api.github.com/repos/PedroHenrique0713/tube360']
    requests.clear();real_github('/releases/latest')
    assert requests==['https://api.github.com/repos/PedroHenrique0713/tube360/releases/latest']
finally:build.urlopen=real_urlopen;build.API_REPO=build.REPO
print('PASS: new organization first, legacy API on 404 only, selected origin reused during the build.')
for lang in ['en','pt-BR']:
    dictionary=json.loads(Path(f'site/src/i18n/{lang}.json').read_text())
    runtime=build.runtime_translations(dictionary,Path('site/src/main.js').read_text()+Path('site/src/community.js').read_text())
    assert 'hero_description' not in runtime and 'release_summaries' not in runtime
    assert runtime['native']==dictionary['native'] and runtime['stars_remaining']==dictionary['stars_remaining']
    assert runtime['quality']==dictionary['quality']
    assert build.runtime_translations(dictionary,'t.hero_description; t["release_summaries"]')['hero_description']==dictionary['hero_description']
print('PASS: runtime keeps direct/dynamic translations, excludes duplicate HTML copy and retains future explicitly referenced editorial keys.')
