"""Uma CA particular na porta local nunca pode ser aceita pelas raízes do título."""
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import threading


with tempfile.TemporaryDirectory() as tmp, socket.socket() as listener:
    cert = pathlib.Path(tmp) / "localhost.crt"
    key = pathlib.Path(tmp) / "localhost.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=IP:127.0.0.1",
                    "-days", "1", "-keyout", str(key), "-out", str(cert)],
                   check=True, capture_output=True)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(5)
    accepted = threading.Event()

    def serve():
        conn, _ = listener.accept()
        with conn:
            accepted.set()
            try:
                with ctx.wrap_socket(conn, server_side=True) as stream:
                    stream.recv(4096)
                    stream.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok")
            except ssl.SSLError:
                pass

    thread = threading.Thread(target=serve)
    thread.start()
    subprocess.run([sys.argv[1], "--untrusted", f"https://127.0.0.1:{listener.getsockname()[1]}/"],
                   check=True, timeout=10)
    thread.join()
    if not accepted.is_set():
        raise RuntimeError("TLS client did not reach loopback server")
