#!/usr/bin/env python3
"""Lê o log UDP do xsdk: sequência de quatro bytes e texto UTF-8."""
import argparse
import socket
import struct
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', type=int, default=7466)
a = p.parse_args()
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind(('0.0.0.0', a.port))
    print('UDP', a.port, 'Ctrl+C encerra', flush=True)
    try:
        while True:
            data, sender = sock.recvfrom(65535)
            if len(data) >= 4:
                print(sender[0], struct.unpack('!I', data[:4])[0], data[4:].decode('utf-8', errors='replace'), flush=True)
    except KeyboardInterrupt:
        pass
