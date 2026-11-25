"""Certificados reais em loopback: validade, emissor e nome permanecem verificados."""
from datetime import datetime, timedelta, timezone
import ipaddress
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

with tempfile.TemporaryDirectory(prefix='tube360-reddit-tls-') as tmp:
    base=Path(tmp)
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    (base/'key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    now=datetime.now(timezone.utc)
    for name,start,end,expected in [('expired',now-timedelta(days=3),now-timedelta(days=1),'tls_date'),('future',now+timedelta(days=1),now+timedelta(days=3),'tls_date'),('untrusted',now-timedelta(days=1),now+timedelta(days=1),'tls')]:
        subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'localhost')])
        cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(start).not_valid_after(end).add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]),critical=False).sign(key,hashes.SHA256()))
        (base/'cert.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(base/'cert.pem',base/'key.pem')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(5)
            seen=[]
            def serve():
                conn,_=listener.accept();seen.append(True)
                with conn:
                    try:
                        with context.wrap_socket(conn,server_side=True) as tls:tls.recv(4096)
                    except ssl.SSLError:pass
            thread=threading.Thread(target=serve);thread.start()
            result=subprocess.run([sys.argv[1],f'https://127.0.0.1:{listener.getsockname()[1]}/',expected],timeout=10)
            thread.join();assert seen and result.returncode==0,name
            print('PASS',name,expected,flush=True)
