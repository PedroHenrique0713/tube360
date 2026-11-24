# Preparing an Xbox release

v0.3.0 is prepared locally; creating artifacts does not publish them. The coordinator integrates other fronts,
checks the privacy findings, qualifies the console and chooses when to publish. **Native sleep timer parity is
not complete in this base: `/api/sleep/*` still requires a server. Integrate and exercise a local timer before
claiming that every feature works without a PC.** See [changelog](../CHANGELOG.md)
and [privacy audit](PRIVACY-AUDIT.md). Compare the last public baseline with `git log 89a7158..HEAD`.

## App ZIP

After the [Xbox build](BUILD.md), from the repository root:

```bash
python3 tools/package-xbox.py --version 0.3.0
python3 -m zipfile -t .work/releases/tube360-0.3.0-xbox360.zip
(cd .work/releases && sha256sum -c tube360-0.3.0-xbox360.zip.sha256)
```

The default destination is `.work/releases/`; `--output-dir` and `--build-dir` select alternate directories.
`--app-name` overrides the title detected from the build cache and must match the build. The package contains one `Tube360/` folder with:

- `default.xex`, copied/converted from the current `client/build-x360` build;
- the four Inter TTF weights 300, 400, 600 and 700;
- inactive `settings.example.txt`, `nest.example.txt`, `server.example.txt`;
- `START-HERE.txt`, `LICENSE`, `THIRD_PARTY.md`, third-party notices and a hash manifest.

Only this explicit list is copied. The app's settings, grants, tokens, Cast identity, history, Nest journal,
logs, cookies and machine config never become inputs. Source files used for license notices retain their
copyright statements. `nest.example.txt` starts with host/port and name; `server.example.txt` starts with
host/port. Their readers do not support leading comments. The sample addresses are documentation-only;
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
| `support/libtube360-yt.a` | `client/build-x360/yt/libtube360-yt.a` |
| `support/libtube360-cast.a` | `client/build-x360/libtube360-cast.a` |
| `support/libcurl.a` | `client/build-x360/yt/curl/lib/libcurl.a` |
| `support/libmbedtls.a`, `libmbedx509.a`, `libmbedcrypto.a` | `client/build-x360/yt/tls/upstream/library/` |
| `support/libeverest.a` | `client/build-x360/yt/tls/upstream/3rdparty/everest/libeverest.a` |
| `support/libp256m.a` | `client/build-x360/yt/tls/upstream/3rdparty/p256-m/libp256m.a` |
| `ffmpeg-lib/libavformat.a`, `libavcodec.a`, `libswresample.a`, `libavutil.a` | `.work/ffmpeg-xbox/lib/` |
| `relink.sh` | `tools/ffmpeg-x360/relink.sh` |

4. Include the corresponding SDK/support licenses, application/source hashes and instructions. With the
   matching GCC and xsdk installed, `XSDK_TOOLCHAIN=<gcc-root> XSDK_DIR=<xsdk-source> ./relink.sh` links a
   replacement title. `FFMPEG_LIB=<modified-library-directory>` selects replacement FFmpeg archives. Exercise
   this command before publishing the kit, then package/sanitize the resulting executable and audit archives
   for diagnostic paths. Do not strip or replace required copyright/attribution notices.

The XEX metadata version remains `1.0.0.0`; `0.3.0` is the distribution version. A source ZIP generated from an
unreviewed checkout or from an earlier branch is not matching release source.

## Integration acceptance

No publication, console upload, reboot or real-device tests are performed by the packager. On the final merged
build the coordinator should install the ZIP to fresh writable storage and check fonts/startup without
`server.txt`, account sign-in/out, history, library, subscriptions, Music, captions, both HD profiles/output
sizes and original/dubbed audio. Check native phone pairing separately from account login. Test Nest Wi-Fi
recovery and B/exit STOP, then a sleep shutdown with the owner after native timer integration. Follow the hardware measurements in
[STANDALONE.md](STANDALONE.md#transport-and-qualification). Rebuild and regenerate artifacts after integration.
