#!/usr/bin/env python3
"""Sonda SSDP unicast para o coordenador; não inicia nem pareia reprodução."""
import argparse
import socket
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--port', type=int, default=1900)
args = parser.parse_args()
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
    probe.settimeout(3)
    probe.sendto(b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n'
                 b'MAN: "ssdp:discover"\r\nMX: 1\r\nST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n',
                 (args.host, args.port))
    data, sender = probe.recvfrom(4096)
    print('SSDP sender:', sender)
    print(data.decode('utf-8', errors='replace'))
    assert data.startswith(b'HTTP/1.1 200 OK\r\n')
# Usa o endpoint conhecido do próprio alvo; não segue LOCATION arbitrário.
with urllib.request.urlopen(f'http://{args.host}:8134/ssdp/device-desc.xml', timeout=3) as response:
    print('DIAL:', response.status, response.headers.get('Application-URL'))
    print(response.read(16384).decode('utf-8', errors='replace'))
