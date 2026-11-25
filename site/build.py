#!/usr/bin/env python3
"""Build both static pages. Python standard library only; no browser build step."""

import argparse
from datetime import datetime, timezone
import html
import json
from pathlib import Path
import re
import shutil
import sys
from urllib.error import HTTPError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
DIST = ROOT / "dist"
REPO = "revive360/tube360"
LEGACY_REPO = "PedroHenrique0713/tube360"
API_REPO = REPO
ORIGIN = "https://tube360.hypermind.space"
CACHE = SRC / "github.json"


def valid_data(data):
    url = urlparse(data["download_url"])
    if not (url.scheme == "https" and url.netloc == "github.com"
            and any(url.path.startswith(f"/{repo}/releases/download/") for repo in (REPO, LEGACY_REPO))):
        raise ValueError("download_url must be a release asset from this repository")
    if not isinstance(data["tag_name"], str) or not data["tag_name"].strip():
        raise ValueError("missing release tag")
    for key in ("size", "stargazers_count", "download_count"):
        if type(data[key]) is not int or data[key] < (1 if key == "size" else 0):
            raise ValueError(f"invalid {key}")
    if "version_downloads" in data:
        versions = data["version_downloads"]
        if not isinstance(versions, list) or any(
                not isinstance(row, dict) or not isinstance(row.get("tag_name"), str)
                or not row["tag_name"].strip() or type(row.get("downloads")) is not int
                or row["downloads"] < 0 for row in versions):
            raise ValueError("invalid version_downloads")
        if sum(row["downloads"] for row in versions) != data["download_count"]:
            raise ValueError("version downloads must match the complete total")
    if data.get("published_at"):
        if not isinstance(data["published_at"], str):
            raise ValueError("published_at must be an ISO 8601 timestamp")
        published = datetime.fromisoformat(data["published_at"].replace("Z", "+00:00"))
        if published.tzinfo is None:
            raise ValueError("published_at must include a timezone")
    return data


def download_statistics():
    total, page, versions = 0, 1, []
    while True:
        releases = github(f"/releases?per_page=100&page={page}")
        for release in releases:
            subtotal = 0
            for asset in release["assets"]:
                if asset["name"].lower().endswith((".zip", ".7z", ".xex")):
                    count = asset["download_count"]
                    if type(count) is not int or count < 0:
                        raise ValueError("invalid asset download_count")
                    total += count
                    subtotal += count
            versions.append({"tag_name": release.get("tag_name", ""), "downloads": subtotal,
                             "published_at": release.get("published_at")})
        if len(releases) < 100:
            return total, versions
        page += 1


def download_count():
    return download_statistics()[0]


def github(path):
    global API_REPO
    req = Request(f"https://api.github.com/repos/{API_REPO}{path}", headers={
        "Accept": "application/vnd.github+json", "User-Agent": "Tube360-site-build"})
    try:
        with urlopen(req, timeout=8) as response:
            return json.load(response)
    except HTTPError as error:
        # The new organization returns 404 until the repository is transferred.
        if error.code != 404 or API_REPO != REPO:
            raise
        API_REPO = LEGACY_REPO
        return github(path)


def get_data(args):
    cached = None
    if CACHE.exists():
        try:
            cached = valid_data(json.loads(CACHE.read_text(encoding="utf-8")))
        except (ValueError, KeyError, TypeError):
            print("Ignoring invalid GitHub cache.", file=sys.stderr)
    data = dict(cached or {})
    if not args.offline:
        try:
            release = github("/releases/latest")
            # the release also carries a .7z for the Aurora Homebrew Store; the page offers the .zip
            asset = next(a for a in release["assets"] if a["name"].endswith(".zip"))
            repo = github("")
            downloads, versions = download_statistics()
            data = valid_data({"tag_name": release["tag_name"],
                               "download_url": asset["browser_download_url"],
                               "size": asset["size"],
                               "stargazers_count": repo["stargazers_count"],
                               "download_count": downloads,
                               "version_downloads": versions,
                               "published_at": release.get("published_at"),
                               "fetched_at": datetime.now(timezone.utc).isoformat()})
        except Exception as error:
            if not cached:
                raise ValueError("GitHub unavailable and no cache. Run --offline with "
                                 "--version, --download-url, --size, --stars and --downloads.") from error
            print(f"GitHub unavailable ({error}); using saved build values. "
                  "Use --offline to skip requests.", file=sys.stderr)
            if isinstance(error, HTTPError) and error.code in (403, 429):
                # Do not make every visitor repeat a request GitHub already refused.
                reset = error.headers.get("X-RateLimit-Reset", "")
                data["retry_after"] = int(reset) if reset.isdigit() else int(datetime.now(timezone.utc).timestamp()) + 3600
    if args.version is not None and args.version != data.get("tag_name"):
        data.pop("published_at", None)
    if args.downloads is not None and args.downloads != data.get("download_count"):
        data.pop("version_downloads", None)
    if getattr(args, "published_at", None) is not None:
        data["published_at"] = args.published_at
    for arg, key in ((args.version, "tag_name"), (args.download_url, "download_url"),
                     (args.size, "size"), (args.stars, "stargazers_count"),
                     (args.downloads, "download_count")):
        if arg is not None:
            data[key] = arg
    try:
        valid_data(data)
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("Complete offline values required: --version TAG "
                         "--download-url URL --size BYTES --stars COUNT --downloads COUNT") from error
    if not args.no_cache_write:
        temp = CACHE.with_suffix(".json.tmp")
        temp.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temp.replace(CACHE)
    return data


def render(template, values):
    def replace(match):
        key = match[1]
        if key not in values:
            raise ValueError(f"Missing template value: {key}")
        if isinstance(values[key], (dict, list)):
            raise ValueError(f"Template value must be scalar: {key}")
        return values[key] if key in {"runtime_json", "community_json", "version_bars", "demo_section", "latest_news", "release_history"} else html.escape(str(values[key]), quote=True)
    return re.sub(r"\{\{([a-zA-Z0-9_]+)\}\}", replace, template)


def next_star_goal(count):
    for goal in (25, 50, 100, 250, 500):
        if count < goal:
            return goal
    scale = 10 ** (len(str(count)) - 1)
    return next(int(scale * multiple) for multiple in (1, 2.5, 5, 10) if scale * multiple > count)


def community_values(data, translations, lang):
    number = lambda value: f'{value:,}'.replace(',', '.' if lang == 'pt-BR' else ',')
    goal = next_star_goal(data['stargazers_count'])
    versions = data.get('version_downloads', [])
    largest = max((row['downloads'] for row in versions), default=0)
    rows = [
        f'<li data-version="{html.escape(row["tag_name"], quote=True)}"><div><span>{html.escape(row["tag_name"])}</span>'
        f'<strong>{number(row["downloads"])}</strong></div><div class="version-track" aria-hidden="true">'
        f'<i style="--fill:{row["downloads"] / largest if largest else 0:.6f}"></i></div></li>'
        for row in versions]
    bars = f'<ul>{"".join(rows[:3])}</ul>'
    if len(rows) > 3:
        bars += f'<details><summary>{html.escape(translations["older_versions"])}</summary><ul>{"".join(rows[3:])}</ul></details>'
    snapshot = {'stars': data['stargazers_count'], 'downloads': data['download_count'],
                'versions': versions, 'fetchedAt': data.get('fetched_at'),
                'latestRelease': {'tag_name': data['tag_name'], 'published_at': data.get('published_at')}}
    return {'star_goal': goal, 'star_goal_text': number(goal),
            'star_remaining': translations['stars_remaining'].format(count=number(goal - data['stargazers_count'])),
            'star_progress': data['stargazers_count'] / goal,
            'version_bars': bars, 'version_pending_hidden': 'hidden' if versions else '',
            'community_json': json.dumps(snapshot, ensure_ascii=False).replace('<', '\\u003c')}


def runtime_translations(translations, script):
    # Editorial copy already lives in HTML; keep every key referenced by runtime
    # code, including literal names used by t[page], t[name] and filter choices.
    used = set(re.findall(r'\bt\.([A-Za-z_]\w*)', script))
    used.update(re.findall(r'''['"]([A-Za-z_]\w*)['"]''', script))
    editorial_suffixes = ('_description', '_intro', '_title', '_title_1', '_title_2', '_intro_1', '_intro_2')
    return {key: value for key, value in translations.items()
            if key in used or (key not in {'release_summaries', 'release_labels'}
                               and not key.endswith(editorial_suffixes))}



# Os tópicos escolhem mudanças presentes no CHANGELOG; a redação localizada é reutilizada
# entre versões. Datas, ordem, presença e procedência nunca vêm dos dicionários do site.
RELEASE_TOPICS = (
    ("quality", r"^Native H\.264 1080p30 decoding"),
    ("music", r"^Music now tries the TV music catalogue"),
    ("search", r"^Public search suggestions"),
    ("local", r"^Watch later and resume without an account"),
    ("sponsor", r"^SponsorBlock with Off"),
    ("large_mp4", r"^Xbox MP4 opening"),
    ("app_updates", r"^Signed app updates through"),
    ("traffic_fix", r"^Videos open again when Google flags"),
    ("dislike_fix", r"^Estimated dislike counts load"),
    ("cast_name", r"^The phone's cast list shows"),
    ("cast_limit", r"^Phone casting works with the TV code"),
    ("native", r"^Console-only YouTube over HTTPS"),
    ("account", r"^TV-code account sign-in stored"),
    ("comments", r"^Comments with controller scrolling"),
    ("speed", r"^Eight playback speeds"),
    ("crt", r"^4:3 / 480 CRT layout"),
    ("rule_updates", r"^Signed Ed25519 YouTube rule updates"),
)


def changelog_releases(markdown):
    releases, current, section = [], None, ""
    for line_number, line in enumerate(markdown.splitlines(), 1):
        heading = re.fullmatch(r"## \[(\d+\.\d+\.\d+)\](?:\s*[—-]\s*(\d{4}-\d{2}-\d{2}))?\s*", line)
        if heading:
            version, date = heading.groups()
            if date:
                datetime.strptime(date, "%Y-%m-%d")
            current = {"version": version, "date": date, "changes": []}
            releases.append(current)
            section = ""
        elif current and line.startswith("### "):
            section = line[4:].strip()
        elif current and line.startswith("- "):
            current["changes"].append({"text": line[2:].strip(), "section": section, "line": line_number})
        elif current and current["changes"] and line.startswith("  ") and line.strip():
            current["changes"][-1]["text"] += " " + line.strip()
    for release in releases[:3]:
        items = []
        for key, pattern in RELEASE_TOPICS:
            source = next((change for change in release["changes"] if re.search(pattern, change["text"], re.I)), None)
            if source:
                items.append({"key": key, **source})
            if len(items) == 7:
                break
        if len(items) < 3:
            raise ValueError(f'CHANGELOG {release["version"]}: fewer than three supported user-facing changes; extend RELEASE_TOPICS and localized release_summaries')
        release["items"] = items
    if len(releases) < 3:
        raise ValueError("CHANGELOG must contain at least three versions")
    return releases[:3]


def release_sections(releases, translations, lang):
    labels, summaries = translations["release_labels"], translations["release_summaries"]
    escape = lambda value: html.escape(str(value), quote=True)
    def date_markup(release):
        if not release["date"]:
            return f'<span class="release-pending">{escape(labels["pending"])}</span>'
        date = datetime.strptime(release["date"], "%Y-%m-%d")
        shown = date.strftime("%d/%m/%Y" if lang == "pt-BR" else "%b %d, %Y")
        return f'<time datetime="{release["date"]}">{shown}</time>'
    def release_url(release):
        return f'https://github.com/{REPO}/releases' + (f'/tag/v{release["version"]}' if release["date"] else '')
    cards = []
    for release in releases:
        entries = ''.join(f'<li data-topic="{item["key"]}" data-changelog-line="{item["line"]}">{escape(summaries[item["key"]])}</li>' for item in release["items"])
        cards.append(f'''<article class="release-entry" id="release-{release["version"].replace('.', '-')}" data-version="{release["version"]}"><div class="release-meta"><h3>{release["version"]}</h3>{date_markup(release)}</div><div class="release-content"><ul>{entries}</ul><a class="text-link" href="{release_url(release)}">{escape(labels["release_link"] if release["date"] else labels["upcoming_link"])}</a></div></article>''')
    history = f'''<section class="updates-section section-wrap" id="releases" aria-labelledby="releases-title"><div class="section-heading" data-reveal><h2 id="releases-title">{escape(labels["title"])}</h2><p>{escape(labels["intro"])}</p></div><div class="release-history">{''.join(cards)}</div><a class="changelog-link text-link" href="https://github.com/{REPO}/blob/HEAD/CHANGELOG.md">{escape(labels["source_link"])}</a></section>'''
    latest = releases[0]
    highlights = ''.join(f'<li>{escape(summaries[item["key"]])}</li>' for item in latest["items"][:3])
    teaser = f'''<section class="latest-news section-wrap" id="latest-news" aria-labelledby="latest-news-title"><div class="news-heading"><div>{date_markup(latest)}<h2 id="latest-news-title">{escape(labels["latest_title"].format(version=latest["version"]))}</h2></div><a class="text-link" href="#releases">{escape(labels["read_all"])}</a></div><ul>{highlights}</ul></section>'''
    return teaser, history


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Use arguments or src/github.json without network requests")
    parser.add_argument("--no-cache-write", action="store_true", help="Render supplied release values without changing the saved GitHub cache")
    parser.add_argument("--version")
    parser.add_argument("--published-at", help="Release publication timestamp with timezone (ISO 8601)")
    parser.add_argument("--download-url")
    parser.add_argument("--size", type=int, help="Release asset size in bytes")
    parser.add_argument("--stars", type=int)
    parser.add_argument("--downloads", type=int, help="Total downloads of release ZIP, 7z and XEX packages")
    args = parser.parse_args()
    data = get_data(args)
    template = (SRC / "template.html").read_text(encoding="utf-8")
    runtime_script = (SRC / "main.js").read_text(encoding="utf-8") + (SRC / "community.js").read_text(encoding="utf-8")
    demo = (SRC / "assets/demo.mp4").is_file()
    releases = changelog_releases((ROOT.parent / "CHANGELOG.md").read_text(encoding="utf-8"))
    # dist/ is what gets published: start empty so nothing left over (test prints, reports) ever ships
    if DIST.exists():
        shutil.rmtree(DIST)
    for lang, route, og_file, locale in (("en", "/", "og-en.png", "en_US"),
                                          ("pt-BR", "/pt-br/", "og-pt-br.png", "pt_BR")):
        translations = json.loads((SRC / "i18n" / f"{lang}.json").read_text(encoding="utf-8"))
        latest_news, release_history = release_sections(releases, translations, lang)
        size = f'{data["size"] / 1_000_000:.1f}'
        stars = f'{data["stargazers_count"]:,}'
        downloads = f'{data["download_count"]:,}'
        if lang == "pt-BR":
            size, stars = size.replace(".", ","), stars.replace(",", ".")
            downloads = downloads.replace(",", ".")
        values = {**translations, **community_values(data, translations, lang), "lang": lang, "canonical": ORIGIN + route,
                  "og_image": ORIGIN + "/assets/" + og_file,
                  "og_locale": locale, "other_locale": "pt_BR" if lang == "en" else "en_US",
                  "en_current": "page" if lang == "en" else "false",
                  "pt_current": "page" if lang == "pt-BR" else "false",
                  "version": data["tag_name"], "download_url": data["download_url"],
                  "size_bytes": data["size"], "stars_raw": data["stargazers_count"],
                  "downloads_raw": data["download_count"], "downloads_text": downloads,
                  "published_at": data.get("published_at") or "",
                  "published_text": (datetime.fromisoformat(data["published_at"].replace("Z", "+00:00")).strftime("%d/%m/%Y" if lang == "pt-BR" else "%b %d, %Y") if data.get("published_at") else translations["release_date_unknown"]),
                  "fetched_at": data.get("fetched_at", ""),
                  "github_retry_after": data.get("retry_after", 0),
                  "release_text": f'{data["tag_name"]} · {size} MB ZIP', "stars_text": stars,
                  "runtime_json": json.dumps(runtime_translations(translations, runtime_script), ensure_ascii=False, separators=(',', ':')).replace("<", "\\u003c"),
                  "demo_section": "", "latest_news": latest_news, "release_history": release_history}
        if demo:
            values["demo_section"] = render('''<section class="demo-section section-wrap" id="demo" aria-labelledby="recording-title">
<div class="recording-copy"><h2 id="recording-title">{{demo_title}}</h2><p>{{demo_description}}</p></div>
<video class="recording-frame" poster="/assets/img/player.jpg" controls playsinline preload="none" width="1280" height="720" aria-label="{{demo_title}}"><source src="/assets/demo.mp4" type="video/mp4"></video>
</section>''', values)
        output = DIST / route.lstrip("/") / "index.html"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(render(template, values), encoding="utf-8")
    shutil.copytree(SRC / "assets", DIST / "assets")
    for filename in ("style.css", "main.js", "community.js", "community.css"):
        shutil.copyfile(SRC / filename, DIST / filename)
    print(f'Built EN + PT-BR: {data["tag_name"]}, {data["size"]} bytes, '
          f'{data["stargazers_count"]} stars, {data["download_count"]} downloads. '
          f'Demo: {"included" if demo else "absent"}.')


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        sys.exit(f"Build failed: {error}")
