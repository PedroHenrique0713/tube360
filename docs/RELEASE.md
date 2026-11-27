# Preparing an Xbox release

v0.3.2 / build 7 is prepared locally; creating artifacts does not publish them. The maintainer integrates the final commit, checks the privacy findings and tests installation before publication. The owner’s console results from 2026-11-25 are in [CONSOLE-032-FINAL.md](CONSOLE-032-FINAL.md). Follow the ordered [repository transfer and publication procedure](REPOSITORY-TRANSFER.md), including the raw compatibility gate. See [changelog](../CHANGELOG.md) and [privacy audit](PRIVACY-AUDIT.md).

## App ZIP and 7z

After the [Xbox build](BUILD.md), from the repository root:

```bash
python3 tools/package-xbox.py --version 0.3.2
python3 -m zipfile -t .work/releases/tube360-v0.3.2-xbox360.zip
7z t .work/releases/tube360-v0.3.2-xbox360.7z
(cd .work/releases && sha256sum -c tube360-v0.3.2-xbox360.zip.sha256 tube360-v0.3.2-xbox360.7z.sha256)
```

Both archives contain the same files. Install 7z/7zz on the build host.
The default destination is `.work/releases/`; `--output-dir` and `--build-dir` select alternate directories.
`--app-name` overrides the title detected from the build cache and must match the build. The package contains one `Tube360/` folder with:

- `default.xex`, the stable `tube360-launcher.xex`, and `tube360.xex`, the app, both copied/converted from the current `client/build-x360` build;
- the four Inter and four Noto Sans Arabic TTF weights 300, 400, 600 and 700, with their license notices;
- inactive `settings.example.txt`, `nest.example.txt`;
- `START-HERE.txt`, `LICENSE`, `THIRD_PARTY.md`, third-party notices and a hash manifest.

Only this explicit list is copied. The app's settings, grants, tokens, Cast identity, history, Nest journal,
logs, cookies and machine config never become inputs. Source files used for license notices retain their
copyright statements. `nest.example.txt` starts with host/port and name. Its reader does not support leading comments. The sample addresses are documentation-only;
replace them with the user's real LAN address. Example suffixes remain inactive on installation.

Some prebuilt libraries embed absolute host paths in diagnostic strings. When the XEX contains `/home/…`,
the packager requires the matching linked ELF and the private xsdk `tools/elf2xex.py`. It first regenerates
the original XEX byte-for-byte (refusing a mismatched ELF/converter/title), then replaces `/home/<user>/`
prefixes with equal-length generic `/build/` paths in an ELF copy and converts that copy. String lengths,
ELF addresses and executable instructions are preserved. The original build is untouched. A final scan rejects
remaining home prefixes and `192.168.*` literals in every ZIP member. This check complements the repository
and visual audit; it does not infer whether an arbitrary string is someone's name or a live credential.

`manifest.json` records distribution version, source commit, dirty-tree status, original/packaged XEX hashes,
path replacements and hashes of every other member. ZIP metadata is fixed for repeatability; a clean commit
is required for the coordinator's final distributable. App packaging deliberately does not archive `.git`,
`.work` or an existing console installation.

## Matching source and relink materials

The current [third-party notice](../THIRD_PARTY.md#xbox-release-and-relinking) calls for matching source and a
relink kit beside each public binary. The app ZIP alone is not that full distribution. Prepare these again
from the final integrated commit, after addressing the findings in [the audit](PRIVACY-AUDIT.md):

1. Export the exact Tube360 source tree at the release commit (no `.git`, personal files or worktree metadata).
   Include all build scripts, headers, shaders, license texts and this relink script. Do not rewrite history
   to remove findings without a separate coordinated decision.
2. Supply `.work/src/ffmpeg-7.1.5.tar.xz`, `tools/build-ffmpeg-x360.sh`, `tools/ffmpeg-x360/` and the dated build
   configuration. Preserve the pinned source digest from the script. Include Mbed TLS/curl sources and the
   private modified xsdk/vulkan4xenon source copies used by this build, with their upstream notices and SDL
   revision/source. Record toolchain version and source revisions without private host paths.
3. Place the following *matching* products in a relink kit (regular files, no links back into a developer's HOME):

| Kit path | Build source |
|---|---|
| `objects/main.cpp.obj` | `client/build-x360/CMakeFiles/tube360.dir/…/client/src/main.cpp.obj` |
| `objects/shaders_bundle.c.obj` | `client/build-x360/CMakeFiles/tube360.dir/shaders_bundle.c.obj` |
| `objects/ffmpeg-libc.c.obj` | `client/build-x360/CMakeFiles/tube360.dir/ffmpeg-libc.c.obj` |
| `objects/player-fault.c.obj` | `client/build-x360/CMakeFiles/tube360.dir/player-fault.c.obj` |
| `support/xsdk.ld`, `support/libxsdk.a` | `.work/native-display/xsdk/src/xsdk.ld`, `build/libxsdk.a` |
| `support/libvulkan4xenon.a` | `.work/native-display/vulkan4xenon/build/libvulkan4xenon.a` |
| `support/libSDL3.a` | The SDL library identified by this build's link command/cache |
| `objects/launcher.cpp.obj` | `client/build-x360/CMakeFiles/tube360-launcher.dir/…/client/src/update/launcher.cpp.obj` |
| `support/libtube360-update.a`, `libtube360-update-core.a` | `client/build-x360/libtube360-update.a`, `client/build-x360/yt/libtube360-update-core.a` |
| `support/libtube360-yt.a` | `client/build-x360/yt/libtube360-yt.a` |
| `support/libtube360-text.a` | `client/build-x360/libtube360-text.a` (FriBidi; include vendored source, COPYING and `client/text.cmake`) |
| `support/libtube360-cast.a` | `client/build-x360/libtube360-cast.a` |
| `support/libcurl.a` | `client/build-x360/yt/curl/lib/libcurl.a` |
| `support/libmbedtls.a`, `libmbedx509.a`, `libmbedcrypto.a` | `client/build-x360/yt/tls/upstream/library/` |
| `support/libeverest.a` | `client/build-x360/yt/tls/upstream/3rdparty/everest/libeverest.a` |
| `support/libp256m.a` | `client/build-x360/yt/tls/upstream/3rdparty/p256-m/libp256m.a` |
| `support/libquickjs-ng.a` | `client/build-x360/yt/libquickjs-ng.a` |
| `ffmpeg-lib/libavformat.a`, `libavcodec.a`, `libswresample.a`, `libavutil.a` | `.work/ffmpeg-xbox/lib/` |
| `relink.sh` | `tools/ffmpeg-x360/relink.sh` |

4. Include the corresponding SDK/support licenses, application/source hashes and instructions. With the
   matching GCC and xsdk installed, `XSDK_TOOLCHAIN=<gcc-root> XSDK_DIR=<xsdk-source> ./relink.sh` links a
   replacement title. `FFMPEG_LIB=<modified-library-directory>` selects replacement FFmpeg archives. Exercise
   this command before publishing the kit, then package/sanitize the resulting executable and audit archives
   for diagnostic paths. Do not strip or replace required copyright/attribution notices.

The XEX metadata version remains `1.0.0.0`; `0.3.2` is the distribution version. A source ZIP generated from an
unreviewed checkout or from an earlier branch is not matching release source.

## Integration acceptance

No publication, console upload, reboot or real-device tests are performed by the packager. On the final merged
build the coordinator should install the ZIP to fresh writable storage and check fonts/startup, account sign-in/out, history, library, subscriptions, Music, captions, both HD profiles/output
sizes and original/dubbed audio. Check native phone pairing separately from account login. Test Nest Wi-Fi
recovery and B/exit STOP, then a native sleep shutdown with the owner. Follow the hardware measurements in
[STANDALONE.md](STANDALONE.md#transport-and-qualification). Rebuild and regenerate artifacts after integration.

## Static website candidate

Run `python3 site/build.py` to build from the latest public release. Before v0.3.2 is published, prepare its
local candidate explicitly (the future asset link will only resolve after the coordinator publishes it):

```bash
python3 site/build.py --offline --no-cache-write --version v0.3.2 \
  --download-url https://github.com/revive360/tube360/releases/download/v0.3.2/tube360-v0.3.2-xbox360.zip \
  --size "$(stat -c %s .work/releases/tube360-v0.3.2-xbox360.zip)"
```

`--no-cache-write` keeps final artifact sizes out of the tracked cache while rendering the exact local values.
Runtime GitHub refresh never replaces a prepared page with a different release version. Rebuild after
publication, then inspect both languages at desktop and phone widths before deployment.
See [screenshot provenance](SCREENSHOTS.md).

## Signed release handoff

The final 0.3.2 uses build ID 7, provided no ID ≥7 has already been offered. Public 0.3.0/0.3.1 packages used the app directly as `default.xex` and require a complete manual package upgrade. Keep the stable launcher as `default.xex`; the update payload is the sanitized `.work/releases/tube360.xex`, identical to `Tube360/tube360.xex` in the ZIP/7z. Never sign the raw private build and then publish a different sanitized payload.

On the public repository's `rules` branch publish `rules.json`, `rules.json.sig`, `app.json`, `app.json.sig` at the root, preserving the exact signed bytes. Embedded defaults use version 1; choose a remote rule version higher than every version already offered, including rejected versions. The app manifest points to the v0.3.2 release asset `tube360.xex`. Build 7 does not offer itself as an upgrade; future releases need strictly higher build IDs in both binary and manifest. Rule versions must also increase, including after a rejected update. Do not publish desktop QA payloads or the private signing key. See [UPDATES.md](UPDATES.md).

Release assets: `tube360-v0.3.2-xbox360.zip`, `.zip.sha256`, `tube360-v0.3.2-xbox360.7z`, `.7z.sha256`, `tube360.xex`, `tube360.xex.sha256`, plus the corresponding source/relink materials described above. Install complete archives once for older single-XEX installations. The exact local artifact paths and checksums are recorded in the coordinator's integration report. This preparation performs no publication.
