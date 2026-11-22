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
REPO = "PedroHenrique0713/tube360"
ORIGIN = "https://tube360.hypermind.space"
CACHE = SRC / "github.json"


def valid_data(data):
    url = urlparse(data["download_url"])
    if not (url.scheme == "https" and url.netloc == "github.com"
            and url.path.startswith(f"/{REPO}/releases/download/")):
        raise ValueError("download_url must be a release asset from this repository")
    if not isinstance(data["tag_name"], str) or not data["tag_name"].strip():
        raise ValueError("missing release tag")
    for key in ("size", "stargazers_count"):
        if type(data[key]) is not int or data[key] < (1 if key == "size" else 0):
            raise ValueError(f"invalid {key}")
    return data


def github(path):
    req = Request(f"https://api.github.com/repos/{REPO}{path}", headers={
        "Accept": "application/vnd.github+json", "User-Agent": "Tube360-site-build"})
    with urlopen(req, timeout=8) as response:
        return json.load(response)


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
            data = valid_data({"tag_name": release["tag_name"],
                               "download_url": asset["browser_download_url"],
                               "size": asset["size"],
                               "stargazers_count": repo["stargazers_count"],
                               "fetched_at": datetime.now(timezone.utc).isoformat()})
        except Exception as error:
            if not cached:
                raise ValueError("GitHub unavailable and no cache. Run --offline with "
                                 "--version, --download-url, --size and --stars.") from error
            print(f"GitHub unavailable ({error}); using saved build values. "
                  "Use --offline to skip requests.", file=sys.stderr)
            if isinstance(error, HTTPError) and error.code in (403, 429):
                # Do not make every visitor repeat a request GitHub already refused.
                reset = error.headers.get("X-RateLimit-Reset", "")
                data["retry_after"] = int(reset) if reset.isdigit() else int(datetime.now(timezone.utc).timestamp()) + 3600
    for arg, key in ((args.version, "tag_name"), (args.download_url, "download_url"),
                     (args.size, "size"), (args.stars, "stargazers_count")):
        if arg is not None:
            data[key] = arg
    try:
        valid_data(data)
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("Complete offline values required: --version TAG "
                         "--download-url URL --size BYTES --stars COUNT") from error
    temp = CACHE.with_suffix(".json.tmp")
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(CACHE)
    return data


def render(template, values):
    def replace(match):
        key = match[1]
        if key not in values:
            raise ValueError(f"Missing template value: {key}")
        return values[key] if key in {"runtime_json", "demo_section"} else html.escape(str(values[key]), quote=True)
    return re.sub(r"\{\{([a-zA-Z0-9_]+)\}\}", replace, template)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Use arguments or src/github.json without network requests")
    parser.add_argument("--version")
    parser.add_argument("--download-url")
    parser.add_argument("--size", type=int, help="Release asset size in bytes")
    parser.add_argument("--stars", type=int)
    args = parser.parse_args()
    data = get_data(args)
    template = (SRC / "template.html").read_text(encoding="utf-8")
    demo = (SRC / "assets/demo.mp4").is_file()
    # dist/ is what gets published: start empty so nothing left over (test prints, reports) ever ships
    if DIST.exists():
        shutil.rmtree(DIST)
    for lang, route, og_file, locale in (("en", "/", "og-en.png", "en_US"),
                                          ("pt-BR", "/pt-br/", "og-pt-br.png", "pt_BR")):
        translations = json.loads((SRC / "i18n" / f"{lang}.json").read_text(encoding="utf-8"))
        size = f'{data["size"] / 1_000_000:.1f}'
        stars = f'{data["stargazers_count"]:,}'
        if lang == "pt-BR":
            size, stars = size.replace(".", ","), stars.replace(",", ".")
        values = {**translations, "lang": lang, "canonical": ORIGIN + route,
                  "og_image": ORIGIN + "/assets/" + og_file,
                  "og_locale": locale, "other_locale": "pt_BR" if lang == "en" else "en_US",
                  "en_current": "page" if lang == "en" else "false",
                  "pt_current": "page" if lang == "pt-BR" else "false",
                  "version": data["tag_name"], "download_url": data["download_url"],
                  "size_bytes": data["size"], "stars_raw": data["stargazers_count"],
                  "github_retry_after": data.get("retry_after", 0),
                  "release_text": f'{data["tag_name"]} · {size} MB ZIP', "stars_text": stars,
                  "runtime_json": json.dumps(translations, ensure_ascii=False).replace("<", "\\u003c"),
                  "demo_section": ""}
        if demo:
            values["demo_section"] = render('''<section class="demo-section section-wrap" id="demo" aria-labelledby="recording-title">
<div class="recording-copy"><h2 id="recording-title">{{demo_title}}</h2><p>{{demo_description}}</p></div>
<video class="recording-frame" poster="/assets/img/player.jpg" controls playsinline preload="none" width="1280" height="720" aria-label="{{demo_title}}"><source src="/assets/demo.mp4" type="video/mp4"></video>
</section>''', values)
        output = DIST / route.lstrip("/") / "index.html"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(render(template, values), encoding="utf-8")
    shutil.copytree(SRC / "assets", DIST / "assets")
    for filename in ("style.css", "main.js"):
        shutil.copyfile(SRC / filename, DIST / filename)
    print(f'Built EN + PT-BR: {data["tag_name"]}, {data["size"]} bytes, '
          f'{data["stargazers_count"]} stars. Demo: {"included" if demo else "absent"}.')


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        sys.exit(f"Build failed: {error}")
