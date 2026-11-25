# Startup diagnostics

Both the launcher and app record startup independently of the GPU, bundled fonts, Xbox Live or XBDM.
These diagnostics localize a stall; they do not establish compatibility with an untested exploit or board.

## Files and durability

The native CRT's first hook writes `boot-crt.log` beside the loaded XEX, using `ExLoadedImageName` and
kernel file calls before SDK locks, pthread, newlib or C++ constructors. Every marker calls
`NtWriteFile` and `NtFlushBuffersFile`. This file is **overwritten on every title startup**, including
launcher → app handoff. Collect it after the failure, before launching another title from this folder.

Once newlib/pthread are available, `boot-early.log` records the remaining CRT stages. `main` opens
`boot-launcher.log` or `boot-app.log`; these three files append, restarting the sequence at `begin entry`.
Read the most recent run. They use an unbuffered FILE plus `fflush` and `fsync` after every event;
the xsdk implements `fsync` with `NtFlushBuffersFile`. `boot_event` preserves the caller's `errno`.
The legacy `launcher.log` also flushes/syncs each line. Existing `startup.log` and the SDK's title log
remain useful, but are not substitutes for these checkpoints.

All boot files stay in the physical title directory, including `\Device\Mass0\…` on USB. If no image
path is available, `game:\` is used: it aliases the current title folder, not a fixed HDD. There is no
fallback to another drive. Near xsdk's 260-byte POSIX path limit, shorter names are used:
`bc.log` (raw CRT), `be.log`, `bl.log`, `ba.log`. A folder that already exhausts that limit can still
prevent the regular log from opening. The raw kernel logger does not depend on POSIX path resolution.

Storage that cannot create/write/flush the file cannot guarantee a log. Regular logging reports one
`durability=failed` notice if flushing fails; the raw logger cannot reliably report failure of its own
file. A hung file call can itself block. Kernel flush is the strongest request used here, not a guarantee
against a USB bridge's volatile cache or power loss. No log can run before the loader resolves required
imports and enters the title.

## Reading the stages

An unmatched `begin` identifies the call being attempted. `end` means that call returned, not that all
hardware worked. A later worker can write after another thread's pending `begin`; inspect open stages,
not just the last line. `+Nms` is elapsed monotonic time within that file/run, including flush latency.

| File / stage | What the log establishes; next investigation if it stops there |
| --- | --- |
| raw `crt.entry` | Reached `xsdk_start`, before SDK initialization. Absence can also mean loader/storage failure. |
| raw `crt.locks`, `crt.microcode`, `crt.pthread`, `crt.newlib` | Each SDK initialization is bracketed separately. A missing `end` precedes constructors, SDL and GPU. |
| raw `crt.runtime-ready` | Runtime supports the regular logger; raw file is closed. |
| early `entry`, `crt.image` | Physical XEX directory was selected and regular log opened. |
| early/app/launcher `video.board`, `video.policy-file`, `video.policy` | Optional hardware-info data export, raw flags/board family, bounded driver-file read and immutable selected path, before GPU setup. |
| early `crt.sdk-log` | SDK title log, `dbgprint.txt` and `net_log.txt` setup; this includes legacy log preallocation and may be slow on USB. |
| early `environment.dbgprint` | Whether debug output was requested and its optional kernel export found. No XBDM connection. |
| early `crt.timezone`, `crt.unwind`, `crt.constructors`, `crt.main` | Timezone query, exception frame registration, C++ constructors and the call to main, respectively. |
| app/launcher `entry`, `storage.paths` | Actual XEX path, inferred HDD/USB/unknown drive, cwd and boot-log location. |
| `storage.volume` | `statvfs` result, block size/free blocks/name limit; filesystem remains unknown. No disk scan. |
| `environment.xbdm`, `environment.export`, `environment.kernel` | Lookup result/status for optional diagnostics and kernel version. A version does not identify the exploit. |
| `video.mode`, `video.connector`, `memory.available` | Dashboard dimensions/interlace/refresh, raw AV enum plus HDMI/component/VGA/unknown label, and available title pages. Queries only. |
| app `environment.fault-handler`, `fault-handler` | Disabled by default. Opt-in status is explicit; see ABI restriction below. |
| app `settings`, `sdl.init`, `storage.writable` | Interface configuration, SDL audio/controller initialization and writable-folder check. |
| `fonts.preflight`, `fonts.header` | Four bundled TTF headers, each filename and validity; no Inter rendering required. |
| app `gpu.init`, `gpu.instance`, `gpu.device`, `gpu.swapchain` | Requested output, Vulkan instance/surface, physical device/queue and actual swapchain extent/buffer count. |
| app `gpu.resources`, `gpu.white-texture` | Pipeline/buffer setup, then initial texture upload/GPU wait, before loading font atlases. |
| app `fonts.atlas`, `fonts.read`, `fonts.upload`, `icons` | Full TTF reads with byte counts, atlas upload and embedded glyph upload. |
| app `video.memory`, `audio.init` | Mapped R8 capability, player/audio setup and whether mapped video is selected. This does not allocate all future decoder surfaces. |
| `network.init`, `network.xnet`, `network.wsa`, `network.socket-policy`, `network.link` | Local socket probe, startup statuses, option support and link/address readiness. No remote connection in the probe; EAGAIN means DHCP pending. |
| app `network.workers`, `phone.start` | HTTP/account/update threads created and optional phone receiver enabled. Workers can finish asynchronously. |
| `tls.global`, `tls.first-request` | curl/Mbed TLS init and first real request, with clock epoch, curl/HTTP result, failure class and certificate flags. No URL/query/body/cookie/token is written to this boot log. Replay explicitly says no TLS request was made. |
| app `app.first-frame`, `frame.fence/acquire/submit/present/complete` | First GPU submission and present return are separated; fence/acquire/completion request finite 5-second waits. Completion does not prove the physical TV image. |
| launcher `launcher.legacy-log`, `launcher.gpu/swapchain/text/first-frame` | Legacy log creation, native GPU, present buffers, embedded text and first submission/fence. |
| launcher `launcher.release-gpu`, `launcher.handoff` | Resources returned before launch; handoff is expected not to return on success. The app's new CRT/entry markers confirm arrival. The existing 20-second watchdog handles a returning/stalled handoff when its thread can run. |
| `startup.fail`, `gpu.cleanup`, `error-screen.gpu` | A startup failure and the attempt to return/acquire GPU for the built-in error screen. This path can also stall if hardware is unresponsive. |
| app `app.exit` | Reached normal shutdown. Abrupt termination is expected to omit this marker. |
| `settings.output` | A Settings output change, including GPU wait/teardown and swapchain reconstruction; records requested height and actual resulting extent. Also logged after the first frame. |
| `display.fonts` | Layout/font-atlas reconstruction at startup, after an output change or when toggling large text; records extent, large-text flag and success. A pending begin can include waiting for the old atlas's GPU work. |

The first-frame native acquire path now rejects a failed `xsdk_present_begin` and an unknown buffer address.
Previously a failure could appear to acquire an image. Timeout conversion saturates before rounding,
including UINT64_MAX. A driver call that never returns cannot be bounded by a caller timeout. Initial
texture/font uploads and GPU cleanup can still wait inside the driver; their preceding markers locate this.
Checkpoint writes stop after the first frame, except actual errors and the first TLS request.

## USB and memory review

Neither launcher nor app hardcodes HDD for asset loading/handoff: xsdk's current directory starts as
`game:\\` and `xsdk_launch` resolves relative names there. Boot logs derive the physical path from the image. This review does
not prove the loader established the correct alias on every payload; `storage.paths` exposes it.

The app reads four small TTFs once per atlas load, validates headers before GPU initialization, and bounds
each file to 16 MiB. Reads are synchronous: a slow or failing device can delay them; the header/read markers
show which file and how far it got. Boot logging intentionally adds synchronous I/O at startup.
The SDK's older title logger has a separate 128-byte path buffer and fallback folders; its path is not
evidence of where the new boot log resides.

A 1 TB device does not identify its filesystem. The XEX and bundled fonts are far below FAT32's per-file
limit; long paths, missing files, bridge behavior and mounting still need physical tests. No format,
recursive scan or migration is attempted. Test both short paths and folders with spaces.

In native mode, mapped video uses physical cached memory allocated by the kernel, with explicit cache maintenance;
allocation failure retains the staging fallback. To compare the existing staging path, place
`0 4 1 -1` in `decode-config.txt` in a disposable title folder (the third value disables mapped video).
This does **not** disable the native display/scaler modifications or first-frame texture uploads.

## Environment audit and ABadAvatar

ABadAvatar has **not been tested**. Its primary repository describes launching a separate payload;
exploit entry alone does not identify which kernel/HV permissions remain for the app. Record the exploit,
payload name/version and launch method separately. The log always says `exploit=unknown`.

| Dependency | Handling and remaining requirement |
| --- | --- |
| Unsigned XEX loading, executable/physical memory and GPU MMIO permissions | Supplied by the user's payload. The app does not install BadUpdate/HV/kernel patches or bypass access denial. Failure before title entry can leave no new log. |
| Kernel/XAM imports | Standard file, VM/physical memory, threads, Vd GPU/display, input, title loader and NetDll exports remain required. `boot_link.py` enumerates the **linked** `.xsdk.imports`, not the SDK's unused catalog. An absent required import can stop loading before logging. |
| Kernel version, AV connector, memory/video diagnostics | Runtime optional lookups log module/procedure status and continue if missing. `MmQueryStatistics` and `VdQueryVideoMode` are nevertheless also required by other existing GPU/display SDK paths; optional telemetry does not remove those static dependencies. |
| XBDM | Optional module presence lookup only; no XBDM import, connection, command or required debugger patch. Absence is logged and startup continues. |
| DbgPrint | Off without opt-in; dynamically resolve kernel ordinal 3 only when requested, with requested/present status. No strong DbgPrint import in either title. The file logger remains independent. |
| SDK `net_log.txt` | Existing optional UDP diagnostics can initialize networking during `crt.sdk-log`. Omit it for the first isolated reproduction; boot logs need no PC receiver. |
| XNet custom limits | Log exact startup status. Retry documented defaults only on WSAEINVAL/WSAEOPNOTSUPP (10022/10045). |
| Socket option 0x5801 | Log accepted/unsupported/denied. Only WSAENOPROTOOPT/WSAEOPNOTSUPP (10042/10045) retain a standard socket. Access denied 10013 closes/fails normally. A retained socket does not prove connectivity on an unpatched kernel. TLS verification stays on. |
| Xbox Live | No account login in the socket probe or native public browsing. NetDll networking and a usable LAN/DNS are still necessary. |
| Fault handler ABI | Default off. `fault-handler.txt` containing `1` opts into the prior measured kernel-17559 handler, only if version and export 0x36d exist. It reads PCR offsets 0x70/0x74 and exception-context offsets; identical build numbers do not prove ABI compatibility. Leave off on untested environments. |
| Fixed addresses | Hardware GPU/display MMIO and PPC cache operations remain in xsdk/vulkan4xenon. They are device-specific, not BadUpdate scratch addresses. Pool addresses come from allocation/physical-address APIs. This audit does not validate every board's scaler/GPU behavior. |
| Thread affinity | Existing SDK/SDL worker placement uses kernel APIs and reports creation/placement errors; the application does not write kernel thread tables. |

The board policy below disables Tube360's display/mapped-memory extensions on an unvalidated board.
This includes Jasper, and does not claim that the upstream GPU driver has been tested on that board.
See the [board and exploit audit](HARDWARE-COMPATIBILITY.md) for chip revisions, family-detection limits,
the upstream register paths and a qualification plan. In particular, Falcon flags also cover Opus;
the enclosure and a board-family label do not establish an exact hardware revision.

## Board policy and choosing a video path

[Board simulation](BOARD-SIMULATION.md) describes desktop `XT_BOARD`, the opt-in `board-sim.txt`
contract, bounded failpoints and the exact-family native rule. Real kernel detection stays separate
from simulation. The owner's native baseline can be retained with the explicit override below.

Place **`video-driver.txt` beside both XEX files**, with exactly one of these words (whitespace/newline allowed):

| Content | Effect on the next title startup |
| --- | --- |
| `safe` | Conservative SDK framebuffer/scaler and staging video. HD progressive retains the original 1280×720 preference; SD/interlaced uses the current kernel viewport in all Vulkan layers. No custom SD/1080 scaler, sampled linear R8, physical cached memory type, or added cache flush/invalidate instructions. |
| `native` | Force Tube360 framebuffer/mapped-video extensions, including on an unknown board; SD/interlaced output still preserves the kernel viewport/scaler. Logs that compatibility is unverified. Existing `decode-config.txt` may still disable mapped video. |
| `auto`, or no file | Corona/Winchester use native to preserve the coordinator's 360 E baseline; other/unknown families use safe. This family policy does not claim physical testing on both families or every subrevision. |

An unreadable, empty, oversized, embedded-NUL or unrecognized file fails safe. Selection is read once before
GPU initialization and stays fixed until the title is relaunched. Launcher and app each read the file in
their own XEX directory; keep them together for consistent behavior. No rebuild or account change is needed.
For example, the complete content to restore the previous path is:

```text
native
```

To force the conservative path, replace that word with `safe`; removing the file restores the family default. A requested output
of 480/1080 in settings is ignored by the safe driver; the log records the request and the actual extent.
Source decoding quality remains governed by the existing playback profile. Staging adds CPU copies, so
console playback performance requires measurement. This is a fallback from the Tube360 extensions,
**not a claim of safe operation on every physical console**.

The coordinator explicitly requires the owner's **360 E baseline to keep native by default** (Adendo 4).
Auto therefore enables Corona/Winchester and keeps Xenon/Zephyr/Falcon/Jasper/Trinity/unknown conservative.
This supersedes the earlier empty allowlist. Confirming the exact board/subrevision remains part of the
console procedure with the f38 board investigation. A user override does not mark a board as tested.

Detection resolves the optional kernel **XboxHardwareInfo data export 0x156**, reads its flags, and
classifies the upper nibble: Xenon, Zephyr, Falcon, Jasper, Trinity, Corona, Winchester, otherwise unknown.
It does not use SMC commands, PCI/MMIO probes, CPU keys, NAND or fixed kernel addresses. This is the kernel's
reported board family; it cannot distinguish every subrevision or prove which ANA/HANA chip is fitted.
Missing module/export stays unknown and safe. The model/path appears in `boot-early.log` before C++
constructors and is repeated in `boot-app.log`/`boot-launcher.log` before their video setup.
After the SDK opens its logger, and again at main, the normal title log also receives
`video: model=... flags=... path=... requested=... native_default=... reason=...`.
This uses the SDK's normal sink, including the existing optional `net_log.txt` UDP destination. `startup.log`
is reserved for errors and can be empty on a successful start; neither XBDM nor `boot-crt.log` is needed to
see the normal board/path summary. The override file and raw hardware flags are never inferred from the exploit.

### Isolated Corona 480i/component contract

This branch accepts a **limited** `board-sim.txt` beside the XEX, read once before GPU setup:

```ini
board=corona
driver=native
mode=480i
cable=component
```

Repeat with `driver=safe` in a separate temporary folder. This models Tupapa2712's **Slim Corona**,
RGH, component CRT combination. Corona E is a different connector profile: `board=corona-e` with
component is rejected. The four supported keys are `board`, `driver`, `mode`, `cable`; duplicate,
unknown, malformed or oversized input selects conservative safe mode without SD injection.
This is the common display subset of F38's simulation format; F38's fault/memory/delay fields and
environment overrides are not implemented here. Do not copy its full configuration into this branch.

An explicit simulation driver takes precedence over `video-driver.txt`; removing `board-sim.txt`
restores real board detection and the existing family default. The log records real flags/model in
`video.board` separately from `board.sim ... profile=corona ... mode=480i cable=component simulated=1`.
`board.sim.file` brackets the file read before any GPU access. Simulation changes policy/aspect and
selects the kernel SD framebuffer path in **both native and safe**. It still reads the actual kernel
viewport and preserves the physical signal/scaler. It cannot make an HDMI-connected 360 E emit a
component 480i signal. Desktop captures model 640×480 layout; hardware-boundary tests use the actual
prepared SDK/Vulkan pipeline. Physical interlaced output still needs the right console/cable/CRT.

For both drivers, require matching surface/swapchain/present viewport, completed first show before
IRQ registration, no custom scaler writes, and fixed SD output when changing output settings. The
integration must retain the SD viewport correction even if F38 previously rejected safe 480i as an
inherited-viewport error. See `tests/reddit/README.md` for the positive cases and calibration mutant.

### Settings resource changes

In safe mode or a dashboard SD/interlaced mode, output-size requests save the preference but **do not touch** the current surface, swapchain,
framebuffers, pipelines or font atlas. A localized help message explains that the current output is fixed.
Fit/crop changes adjust geometry/UV coordinates only. English/Portuguese/Spanish share the bundled atlas;
changing language rebuilds category labels, not GPU/display resources.

Large text and actual native/desktop output changes build replacement fonts locally, check GPU idle and
reset completed command-pool references. Only after a replacement atlas upload finishes is the old atlas
retired. Text caches use the font revision. A failed idle/reset stops before retiring resources. Output
recreation logs each retirement/setup stage and checks the first new frame again. These waits cannot
interrupt a kernel/GPU call that never returns; the preceding durable marker still locates it.
Playback quality measurements reset around these operations, so a Settings stall is not treated as a
fresh decoder/network pressure window.

Relevant stages: `settings.language`, `settings.fit`, `settings.output`, `settings.layout`, `fonts.pack`,
`settings.gpu-idle`, `settings.commands-reset`, `settings.output-retire`, `fonts.upload`, `fonts.retire`,
and the repeated first-frame checkpoints for an actual recreated output. No mapped-video pool is rebuilt
by language, fit or text changes. See the test README for real controller-driven desktop stress and the
strict native-output test.

### What still accesses hardware

The line-counter wait **already exists upstream**. Conservative operation does not remove the xsdk's
LUT, surface or interrupt-register access. In HD progressive, `safe` preserves the upstream 720p scaler;
in SD/interlaced, it uses the upstream `XSDK_DISPLAY_KERNEL` path, without custom scaler writes. Tests
compare complete register snapshots against the original source using the corresponding mode; that is
not a physical Jasper test. Previously the SDK could return 640×480 while V4X required 1280×720. The
display extent, capabilities, surface, swapchain images and presentation now agree with the **actual kernel
viewport**, which need not equal the dashboard signal dimensions. A durable `display.kernel-viewport`
begin precedes the register read. Invalid/zero dimensions are rejected instead of assuming HDMI or HD.

`display.signal` logs dashboard width/height, interlace, refresh, aspect and selection. SD (including 480p)
and all interlaced modes use the dashboard scaler and timings in both safe and native policies. No
`VdSetDisplayMode` is used. The UI uses dashboard 4:3/16:9 aspect with independent horizontal/vertical
scales, safe margins and large text, so a PAL 640×576 or 720×576 viewport does not imply square pixels.
The connector log retains raw AV-pack values; known Xenia Canary enum labels include 5 composite/S-Video,
2 SCART and 7 TV, alongside HDMI/component/VGA. It does not identify the exact cable or chip from a guess.

Hardware procedure: a 480p HDMI dashboard test checks progressive SD only. Actual 480i requires the
appropriate Xbox 360 E composite AV cable and a compatible TV/capture input; HDMI 480p is not a substitute.
For 576i/50 Hz, use a console/region/cable/display combination that already offers PAL-50. Do not change
region/firmware for this check. Keep baseline photos/settings and restore them afterward. See the test
README for steps. The manufacturer's [Xbox 360 E composite wiring diagram, page 8](https://gzhls.at/blob/ldb/8/7/f/1/f2a7b4149a7ae795e80dfd6d1a7a87322d30.pdf)
and the [Xenia Canary AV-pack enum](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xam_info.cc)
are the references; physical signal/scaler/IRQ compatibility still requires the console run.

Both paths now bracket these setup operations with durable events **before** the access:

| Stage | Next operation |
| --- | --- |
| `gpu.alloc-ring/alloc-writeback/alloc-stream`, `gpu.hiz` | Kernel allocations and initial HiZ state. |
| `gpu.engines/ring/writeback/system-fence` | Vd engine/ring/writeback/system-fence calls. |
| `gpu.setup/setup-fence` | Initial GPU setup packets, then the initial fence wait. |
| `display.output` | Safe 1280×720 selection, or native dashboard query and SD/1080 selection. |
| `display.splash/open` | Existing splash handoff and display open. |
| `display.viewport` | MMIO read 0x1961. |
| `display.lut` | LUT registers 0x1930/0x1922/0x1921/0x1927/0x1928; requests a 100 ms bounded poll. |
| `display.saved-surface/scaler-snapshot` | Save surface registers 0x1844/0x1848/0x184d/0x184e and ten scaler registers. |
| `display.describe/buffers` | XAM display description, front-buffer allocation and black clear. |
| `display.first-show/scaler-write/surface-write` | First show, scaler update lock 0x1973 plus ten registers, then surface addresses/pitch/extents and 0x183b latch. Surface poll requests 100 ms. |
| `display.line-counter` | Read 0x194c until the requested height or 50,000 µs. The end event reports actual line and elapsed wait; it can return without reaching the height. |
| `display.interrupt` | Install the callback that will read 0x1951/0x194c and flip surfaces later. |
| `display.restore` | Restore saved scaler/surface state on close. |

No file I/O is added inside the interrupt callback: setup tracing is disabled **before** registration.
Subsequent frames/flips do not flush boot checkpoints. A hardware/MMIO/kernel call that never returns can
still prevent a software timeout from running. An initial display-show error is now propagated, buffers
are freed, and the interrupt callback is not installed; it was previously ignored by `xsdk_present_open`.

For FernJayCee, first collect a **Jasper + auto/safe** attempt and its last open stage from the temporary
folder. HDD and USB have both failed according to the coordinator: the drive is not an established cause.
Only compare `native` if a further controlled attempt is appropriate. On the owner's 360 E, compare safe
with the prior `native` path, preserving raw board flags, AV pack, dashboard mode, actual extent and TV
photo. Keep the logs from each attempt before relaunching. No such console experiment was performed here.

For a user report, request **only the boot files**, `startup.log`, relevant title/launcher logs, display
connector/mode and exploit/payload/version. Do not request account, cookie, pairing or OAuth files. Logs
include local file paths and the console clock; redact personal folder names before public posting.

## Reproduction and console handoff

PC commands and native simulations are in [tests/reddit](../tests/reddit/README.md). The native tests execute
the real logger/platform/socket code with simulated kernel exports; they are not an ABadAvatar or GPU test.
The final ELF audit checks that raw hooks are strong symbols and actually called before SDK locks/newlib.

For the coordinator, use the existing temporary-folder procedure. Copy the two matching XEX builds and
four TTFs to a short disposable folder on HDD or USB, without account/debug/network-log marker files.
Launch directly and through the launcher. Capture the TV and collect `boot-crt.log`, `boot-early.log`,
`boot-app.log`, `boot-launcher.log` (or their short names), `startup.log`, `launcher.log` and the SDK title log.
After a hard freeze, power off as necessary and collect the files **before relaunching**. Compare the last
unmatched begin on each thread/stage; do not infer that USB is the cause from drive capacity or a white flash.
Repeat HDD/USB, HDMI/component and path cases only on hardware available to the coordinator, then archive
the evidence and remove only that disposable folder. No physical console was accessed for this delivery.

Primary references: [xsdk source](https://github.com/xenosworks/xsdk),
[vulkan4xenon](https://github.com/xenosworks/vulkan4xenon),
[ABadAvatar](https://github.com/shutterbug2000/ABadAvatar),
[Xenia kernel export table](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_table.inc),
[Xenia XAM export table](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xam/xam_table.inc),
[Xenia Canary AV pack options](https://github.com/xenia-canary/xenia-canary/wiki/Options).
Board-family interpretation: [XeUnshackle GetMoboByHWFlags](https://github.com/Byrom90/XeUnshackle/blob/main/Utils.cpp),
with the data export verified against the Xenia kernel table above.
