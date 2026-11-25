#!/usr/bin/env python3
"""F25b: nest.txt legado, TLS lento e vídeo abrindo no player de produção."""
from wifi_player_test import run

if __name__ == '__main__':
    run('saved-tls-1s', handshake_delay=1)
    run('saved-tls-2s', handshake_delay=2)
    run('saved-offline', offline=True)
