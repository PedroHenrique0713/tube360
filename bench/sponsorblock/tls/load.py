#!/usr/bin/env python3
"""Dois pedidos públicos anônimos, mesmo TLS do Xbox, com contenção numa CPU Linux."""
import concurrent.futures
import multiprocessing
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def burn(stop, cpu):
    os.sched_setaffinity(0, {cpu})
    n = 1
    while not stop.is_set():
        for _ in range(10000):
            n = (n * 1664525 + 1013904223) & 0xffffffff


def main():
    binary = str(Path(sys.argv[1]).resolve())
    cpu = min(os.sched_getaffinity(0))
    stop = multiprocessing.Event()
    burners = [multiprocessing.Process(target=burn, args=(stop, cpu)) for _ in range(2)]
    for process in burners:
        process.start()
    try:
        with tempfile.TemporaryDirectory(prefix='tube360-public-load-') as directory:
            env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
            env.update(HOME=directory, XDG_CACHE_HOME=directory)
            def request(url):
                args = ['taskset', '-c', str(cpu), binary] + ([url] if url else [])
                start = time.monotonic()
                result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=12)
                return url or 'SponsorBlock prefix 5f6b', result, time.monotonic()-start
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
                results = list(executor.map(request, [None, 'https://www.youtube.com/robots.txt']))
            for name, result, elapsed in results:
                print(f'REQUEST {name} cpu={cpu} cpu_burners=2 wall_seconds={elapsed:.3f}', flush=True)
                print(result.stdout, flush=True)
                assert result.returncode == 0, result.stderr + result.stdout
                assert 'HTTP=200' in result.stdout and 'nonblocking=1' in result.stdout, result.stdout
                if name.startswith('SponsorBlock'):
                    assert 'segments=1' in result.stdout, result.stdout
            print('PASS two real requests, Xbox mbedTLS config, shared CPU with two burners', flush=True)
    finally:
        stop.set()
        for process in burners:
            process.join(5)
            if process.is_alive():
                process.terminate()
                process.join()


if __name__ == '__main__':
    main()
