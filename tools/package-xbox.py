#!/usr/bin/env python3
"""Prepare matching local Xbox ZIP/7z archives from an explicit file list; never publish them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
HOME_PREFIX = re.compile(rb"/home/[A-Za-z0-9_.-]+/")
PRIVATE_IP = re.compile(rb"\b192\.168\.[0-9]{1,3}\.[0-9]{1,3}\b")


def read_file(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Missing regular file (symlinks refused): {path}")
    return path.read_bytes()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def clean_xex(build, converter, app_name, stem="tube360"):
    original = read_file(build / (stem + ".xex"))
    if not original.startswith(b"XEX2"):
        raise ValueError("Build output is not an XEX2 executable")
    if not HOME_PREFIX.search(original):
        return original, original, 0
    elf = read_file(build / stem)
    if not elf.startswith(b"\x7fELF"):
        raise ValueError("A matching linked ELF is required to remove host paths")
    # Mantém comprimentos e endereços: só troca o prefixo literal das strings de diagnóstico.
    sanitized, replacements = HOME_PREFIX.subn(lambda m: b"/build" + b"/" * (len(m[0]) - 6), elf)
    if not replacements:
        raise ValueError("XEX has home paths absent from the linked ELF; rebuild before packaging")
    if elf[4:6] != b"\x01\x02":
        raise ValueError("Expected a 32-bit big-endian PowerPC ELF")
    header = struct.unpack_from(">16sHHIIIIIHHHHHH", elf)
    if header[2] != 20:
        raise ValueError("Expected a PowerPC executable")
    for index in range(header[12]):
        section = struct.unpack_from(">IIIIIIIIII", elf, header[6] + index * header[11])
        offset, size = section[4:6]
        if section[2] & 4 and elf[offset:offset + size] != sanitized[offset:offset + size]:
            raise ValueError("Path replacement would alter an executable section")
    read_file(converter)
    with tempfile.TemporaryDirectory(prefix="tube360-package-") as directory:
        stage = Path(directory)
        command = [sys.executable, str(converter), "--title-id", "0x58545542",
                   "--title-name", app_name, "--version", "1.0.0.0",
                   str(stage / "tube360.elf"), str(stage / "default.xex")]
        (stage / "tube360.elf").write_bytes(elf)
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if read_file(stage / "default.xex") != original:
            raise ValueError("ELF/converter/title does not reproduce the current build XEX exactly")
        (stage / "tube360.elf").write_bytes(sanitized)
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        result = read_file(stage / "default.xex")
    if not result.startswith(b"XEX2"):
        raise ValueError("Converter did not produce an XEX2 executable")
    return result, original, replacements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Distribution version, e.g. 0.3.0")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "client/build-x360")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".work/releases")
    parser.add_argument("--xex-converter", type=Path,
                        default=ROOT / ".work/native-display/xsdk/tools/elf2xex.py")
    parser.add_argument("--app-name", help="Override title name; by default use the build cache")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("Use a version such as 0.3.0 or 0.3.0-rc.1")

    files = {}
    for weight in (300, 400, 600, 700):
        name = f"inter-{weight}.ttf"
        files[name] = read_file(ROOT / "client/assets" / name)
    for name in ("nest", "server", "settings"):
        filename = f"{name}.example.txt"
        files[filename] = read_file(ROOT / "deploy/xbox" / filename)
    for name in ("LICENSE", "THIRD_PARTY.md"):
        files[name] = read_file(ROOT / name)
    notices = {
        "Monocypher-LICENSE.txt": "client/third_party/monocypher/LICENCE.md",
        "OFL-Inter.txt": "client/assets/OFL-Inter.txt",
        "Phosphor-MIT.txt": "client/third_party/phosphor/LICENSE",
        "FFmpeg-LGPL-2.1.txt": "tools/ffmpeg-x360/COPYING.LGPLv2.1",
        "MbedTLS-LICENSE.txt": ".work/src/mbedtls-3.6.8/LICENSE",
        "curl-COPYING.txt": ".work/src/curl-xenon-a6b70bd06820e1c625f47fe0e592e08c5daaa6b4/COPYING",
        "xsdk-LICENSE.txt": ".work/native-display/xsdk/LICENSE",
        "xsdk-THIRD-PARTY.txt": ".work/native-display/xsdk/THIRD_PARTY_NOTICES.md",
        "vulkan4xenon-LICENSE.txt": ".work/native-display/vulkan4xenon/LICENSE",
        "vulkan4xenon-THIRD-PARTY.txt": ".work/native-display/vulkan4xenon/THIRD_PARTY_NOTICES.md",
    }
    for name, source in notices.items():
        files["licenses/" + name] = read_file(ROOT / source)
    # SDL's notice is in its matching source checkout, located via its build cache.
    cache = read_file(args.build_dir / "CMakeCache.txt").decode()
    title = re.search(r"^APP_NAME:[^=]+=(.+)$", cache, re.M)
    app_name = args.app_name or (title[1] if title else "Tube360")
    match = re.search(r"^SDL3_DIR:[^=]+=(.+)$", cache, re.M)
    if not match:
        raise ValueError("SDL3_DIR is missing from the Xbox CMake cache")
    sdl_build = Path(match[1])
    sdl_cache = read_file(sdl_build / "CMakeCache.txt").decode()
    match = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", sdl_cache, re.M)
    if not match:
        raise ValueError("Cannot locate matching SDL source license")
    files["licenses/SDL-LICENSE.txt"] = read_file(Path(match[1]) / "LICENSE.txt")
    # As licenças dos headers ficam nos próprios headers; preserve seus textos completos.
    for name in ("pl_mpeg.h", "stb_image.h", "stb_truetype.h", "picojson.h"):
        files["licenses/" + name + ".txt"] = read_file(ROOT / "client/third_party" / name)

    executable, original, replacements = clean_xex(args.build_dir, args.xex_converter, app_name)
    # v0.3.0: o launcher travou num console real (caminho relativo no xsdk_launch); o app vai como default.xex.
    files["default.xex"] = executable
    files["START-HERE.txt"] = (
        f"Tube360 {args.version} - Xbox 360\n\n"
        "EN: Copy this entire folder to writable Xbox storage. Open default.xex with Aurora, FSD or XeXMenu.\n"
        "Keep default.xex and all four fonts together. YouTube rule fixes arrive signed, automatically.\n"
        "No server.txt = console only. Example files are inactive; rename and edit only if needed.\n"
        "Nest: host + port on line 1, display name on line 2. Use your speaker's real LAN IP.\n"
        "TV-code account login is in Settings; phone Cast has a separate code in Phone.\n"
        "Press B back to browsing before closing the app, so the speaker receives STOP.\n\n"
        "Sleep timer: 15 min to 2 h or end of video; the console powers itself off, without a PC.\n\n"
        "PT: Copie esta pasta inteira para armazenamento gravavel do Xbox. Abra default.xex no Aurora/FSD/XeXMenu.\n"
        "Mantenha default.xex e as quatro fontes juntos. Correcoes das regras do YouTube chegam assinadas, sozinhas.\n"
        "Sem server.txt = so console. Exemplos sao inativos; renomeie e edite apenas se precisar.\n"
        "Nest: host + porta na linha 1, nome na linha 2. Use o IP local real da caixa.\n"
        "Login por codigo em Configuracoes; Cast do celular tem outro codigo no painel Celular.\n"
        "Saia com B ate a navegacao antes de fechar o app para o STOP chegar a caixa.\n\n"
        "Timer: 15 min a 2 h ou fim do video; o proprio console desliga, sem PC.\n\n"
        "Setup / instalacao: https://github.com/PedroHenrique0713/tube360\n"
        "Keep the license notices. Matching source/relink materials accompany the public release.\n"
    ).encode()
    revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    manifest = {
        "version": args.version, "source_commit": revision,
        "title_name": app_name, "title_id": "0x58545542", "xex_version": "1.0.0.0",
        "source_dirty": bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"])),
        "build_xex_sha256": digest(original), "packaged_xex_sha256": digest(executable),
        "home_prefix_replacements_in_elf": replacements,
        "files": {name: digest(data) for name, data in sorted(files.items())},
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    for name, data in files.items():
        if HOME_PREFIX.search(data) or PRIVATE_IP.search(data):
            raise ValueError(f"Private home path or 192.168 address found in package member: {name}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in (("tube360.xex", executable),):
        (args.output_dir / name).write_bytes(data)
        (args.output_dir / (name + ".sha256")).write_text(f"{digest(data)}  {name}\n")
    output = args.output_dir / f"tube360-v{args.version}-xbox360.zip"
    fd, temporary = tempfile.mkstemp(prefix=output.name + ".", dir=args.output_dir)
    try:
        with os.fdopen(fd, "wb") as stream:
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for name, data in sorted(files.items()):
                    info = zipfile.ZipInfo("Tube360/" + name, (2026, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, data)
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    outputs = [output]
    sevenzip = shutil.which("7zz") or shutil.which("7z")
    if not sevenzip:
        raise ValueError("7z or 7zz is required to create the matching .7z archive")
    seven = output.with_suffix(".7z")
    with tempfile.TemporaryDirectory(prefix="tube360-archive-", dir=args.output_dir.resolve()) as directory:
        stage = Path(directory)
        for name, data in sorted(files.items()):
            target = stage / "Tube360" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o644)
            os.utime(target, (1767225600, 1767225600))
        for folder in sorted(stage.rglob("*"), reverse=True):
            if folder.is_dir():
                os.utime(folder, (1767225600, 1767225600))
        temporary = stage / "release.7z"
        subprocess.run([sevenzip, "a", "-t7z", "-mx=7", "-mmt=2", "-mtc=off", "-mta=off",
                        str(temporary), "Tube360"], cwd=stage, check=True, stdout=subprocess.PIPE)
        os.replace(temporary, seven)
    outputs.append(seven)
    for artifact in outputs:
        checksum = digest(artifact.read_bytes())
        artifact.with_suffix(artifact.suffix + ".sha256").write_text(f"{checksum}  {artifact.name}\n")
        print(f"{artifact}\n{checksum}  {artifact.name}")
    print(f"{len(files)} files per archive; {replacements} home prefixes removed in linked ELFs")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, struct.error, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
