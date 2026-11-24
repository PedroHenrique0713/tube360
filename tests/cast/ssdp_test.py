#!/usr/bin/env python3
"""SSDP multicast real em loopback; XAM/YouTube/celular físicos não entram na prova."""
import http.server
import os
from pathlib import Path
import socket
import subprocess
import threading
from unittest.mock import patch

from e2e import ROOT, Handler, Lounge, app, dial, wait_for

WORK = ROOT / '.work/f25/ssdp'
GROUP = '239.255.255.250'
PORT = 1934
WORK.mkdir(parents=True, exist_ok=True)
for name, arguments in (
    ('sample.mp4', ['-f', 'lavfi', '-i', 'color=size=320x180:rate=30', '-t', '1', '-c:v', 'libx264', '-threads', '1']),
    ('thumbnail.jpg', ['-f', 'lavfi', '-i', 'color=size=320x180', '-frames:v', '1', '-threads', '1']),
):
    if not (WORK / name).exists():
        subprocess.run(['ffmpeg', '-v', 'error', *arguments, str(WORK / name)], check=True)
lounge = Lounge(WORK)
server = http.server.ThreadingHTTPServer(('127.0.0.1', 8234), Handler)
server.daemon_threads = True
server.lounge = lounge
thread = threading.Thread(target=server.serve_forever)
thread.start()
try:
    for fallback in (False, True):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(('', PORT))
            listener.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                                socket.inet_aton(GROUP)+socket.inet_aton('127.0.0.1'))
            listener.settimeout(4)
            env = {'XT_CAST_TEST_MULTICAST': '1'}
            if fallback:
                env['XT_CAST_TEST_NO_MEMBERSHIP'] = '1'
            name = 'without-membership' if fallback else 'multicast'
            with patch.dict(os.environ, env), app(ROOT/'client/build/cast-test-app', WORK, 'pt', name, 4):
                wait_for(lambda: lounge.code_count, 'fixture online')
                while True:
                    data, sender = listener.recvfrom(4096)
                    if b'NOTIFY * HTTP/1.1' in data and b'NTS: ssdp:alive' in data:
                        assert b'LOCATION: http://127.0.0.1:8134/ssdp/device-desc.xml' in data
                        break
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as phone:
                    phone.bind(('127.0.0.1', 0))
                    phone.settimeout(2)
                    phone.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('127.0.0.1'))
                    # Sem membership, prova a recepção unicast e o envio NOTIFY. O Linux pode compartilhar o grupo do listener.
                    target = '127.0.0.1' if fallback else GROUP
                    phone.sendto(b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1934\r\n'
                                 b'MAN: "ssdp:discover"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n', (target, PORT))
                    replies = [phone.recvfrom(4096)[0] for _ in range(4)]
                    assert all(packet.startswith(b'HTTP/1.1 200 OK') for packet in replies)
                    assert any(b'ST: urn:dial-multiscreen-org:service:dial:1\r\n' in packet for packet in replies)
                    assert len({packet.split(b'USN: ')[1].split(b'\r\n')[0] for packet in replies}) == 4
                assert dial('GET', '/ssdp/device-desc.xml')[0] == 200
                assert dial('GET', '/apps/YouTube')[0] == 200
            log = (WORK/(name+'.log')).read_text()
            assert 'membership=0' in log if fallback else 'membership=1' in log
            assert 'respostas=4 errno=0' in log
            print(f'PASS {name}: NOTIFY multicast, 4 M-SEARCH replies, DIAL description/app')
    assert not lounge.errors, lounge.errors
finally:
    server.shutdown()
    server.server_close()
    thread.join()
