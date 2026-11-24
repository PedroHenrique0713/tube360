"""HTTPS local com curl/mbedTLS reais, 120 ms por pedido e 2,5 Mbit/s; sem mudar a rede do host."""
import argparse
import http.server
import json
from pathlib import Path
import shlex
import ssl
import subprocess
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument("work", type=Path)
parser.add_argument("--faults-only", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)


def command(*args):
    subprocess.run(args, cwd=root, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


command("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=localhost",
        "-addext", "subjectAltName=IP:127.0.0.1", "-keyout", str(work / "key.pem"), "-out", str(work / "cert.pem"))
(work / "roots.hpp").write_text('static const char ytRoots[]=R"CA(' + (work / "cert.pem").read_text() + ')CA";\n')
# A CA só entra nesta cópia do objeto https, nunca em tube360 ou no .xex.
command("c++", "-std=c++20", "-O2", "-I" + str(work), "-I" + str(root / "client/src/yt"),
        "-I" + str(root / ".work/src/curl-xenon-a6b70bd06820e1c625f47fe0e592e08c5daaa6b4/include"),
        "-c", str(root / "client/src/yt/https.cpp"), "-o", str(work / "https.o"))
link = shlex.split((root / "client/build/CMakeFiles/yt-network-probe.dir/link.txt").read_text())
link = [x for x in link if not x.startswith("-Wl,--dependency-file=")]
link[link.index("-o") + 1] = str(work / "probe")
link.insert(1, str(work / "https.o"))
subprocess.run(link, cwd=root / "client/build", check=True, stdout=subprocess.DEVNULL)

payload = bytes(i % 251 for i in range(2 * 1024 * 1024))
lock = threading.Lock()
next_byte = 0.0
faults = set()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_GET(self):
        global next_byte
        first, last = map(int, self.headers["Range"].removeprefix("bytes=").split("-"))
        last = min(last, len(payload) - 1)
        time.sleep(0.12)
        self.send_response(206)
        self.send_header("Content-Length", str(last - first + 1))
        self.send_header("Content-Range", f"bytes {first}-{last}/{len(payload)}")
        self.end_headers()
        with lock:
            fault = self.path in ("/drop", "/stall", "/cancel") and self.path not in faults
            if fault:
                faults.add(self.path)
        try:
            for pos in range(first, last + 1, 8192):
                block = payload[pos:min(last + 1, pos + 8192)]
                duration = len(block) * 8 / 2_500_000
                if self.path == "/shared":
                    with lock:
                        next_byte = max(time.monotonic(), next_byte) + duration
                        deadline = next_byte
                    time.sleep(max(0, deadline - time.monotonic()))
                else:
                    time.sleep(duration)
                self.wfile.write(block)
                self.wfile.flush()
                if fault and pos - first >= 32768:
                    if self.path in ("/stall", "/cancel"):
                        time.sleep(14)
                        fault = False
                    else:
                        self.close_connection = True
                        return
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            self.close_connection = True


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(work / "cert.pem", work / "key.pem")
server.socket = context.wrap_socket(server.socket, server_side=True)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
rows = []
try:
    base = f"https://127.0.0.1:{server.server_port}"
    cases = [(128, 1, 16, 0), (512, 1, 16, 0), (512, 1, 64, 0), (1024, 1, 64, 0),
             (512, 2, 64, 0), (512, 2, 64, 1)]
    for path in (() if args.faults_only else ("/per-connection", "/shared")):
        for chunk, lanes, buffer, fresh in cases:
            result = subprocess.run([str(work / "probe"), base + path, str(len(payload)), str(chunk * 1024),
                                     str(lanes), str(buffer * 1024), str(fresh)], capture_output=True, text=True, timeout=30, check=True)
            row = dict(path=path, **json.loads(result.stdout))
            rows.append(row)
            (work / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
            print(json.dumps(row), flush=True)
    for path in ("/drop", "/stall"):
        result = subprocess.run([str(work / "probe"), base + path, str(len(payload)), "read"],
                                capture_output=True, text=True, timeout=35, check=True)
        row = dict(path=path, **json.loads(result.stdout))
        assert row["resumes"] >= 1 and row["bytes"] == len(payload), row
        rows.append(row)
        print(json.dumps(row), flush=True)
    result = subprocess.run([str(work / "probe"), base + "/cancel", str(len(payload)), "cancel"],
                            capture_output=True, text=True, timeout=5, check=True)
    row = dict(path="/cancel", **json.loads(result.stdout))
    rows.append(row)
    print(json.dumps(row), flush=True)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
(work / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
