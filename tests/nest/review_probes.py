#!/usr/bin/env python3
"""R25: medidas do scanner e diagnósticos de limites que dependem de decisão."""
import contextlib
import json
import os
import re
import subprocess
import time

from discovery_test import ROOT, BINARY, DiscoveryTest, receiver


def probe(binary, command, network):
    completed = subprocess.run([str(binary), command], capture_output=True, text=True,
                               timeout=40, check=True, env={**os.environ, 'XT_NEST_NETWORK': network})
    return json.loads(completed.stdout.splitlines()[-1]), completed.stderr


def main():
    DiscoveryTest.setUpClass()
    try:
        # Só enumeração de endereços; nunca abrimos sockets para 10/8 ou 172.16/12.
        for ip, mask, remote in [('10.2.3.4', '255.0.0.0', '10.2.4.2'),
                                 ('172.16.3.4', '255.240.0.0', '172.17.4.2'),
                                 ('192.0.2.4', '255.255.254.0', '192.0.3.2')]:
            raw = subprocess.check_output([str(BINARY), 'plan', ip, mask], text=True)
            hosts = json.loads(raw)['hosts']
            print(json.dumps(dict(case='network-plan', ip=ip, mask=mask, hosts=len(hosts),
                                  same_subnet_target=remote, target_in_plan=remote in hosts)))

        with receiver(DiscoveryTest.folder, '127.77.25.2'):
            result, _ = probe(BINARY, 'scan', '127.77.24.1/255.255.254.0')
            print(json.dumps(dict(case='actual-slash23', target='127.77.25.2', **result)))

        faults = ROOT/'client/build/nest-scan-fault-test'
        with receiver(DiscoveryTest.folder, '127.77.25.254'):
            result, log = probe(faults, 'scan', '127.77.25.1/0.0.0.0')
            assert result['state'] == 2 and result['devices'][0]['host'] == '127.77.25.254', result
            assert result['ms'] < 10000, result
            assert 'maximum=16 remaining=0' in log, log
            print(json.dumps(dict(case='253-hosts-silent-syn', **result)))
            print(log.strip().splitlines()[-1])
        maximum_cancel = 0
        began = time.monotonic()
        for _ in range(12):
            result, log = probe(faults, 'cancel', '127.77.25.1/0.0.0.0')
            assert result['state'] == 3 and result['ms'] < 300, result
            assert re.search(r'maximum=16 remaining=0', log), log
            maximum_cancel = max(maximum_cancel, result['ms'])
        print(f'PASS 12 cancellations: max={maximum_cancel} ms; sockets remaining=0; elapsed={time.monotonic()-began:.3f}s')

        # Serviços que aceitam TCP mas não concluem TLS consomem o orçamento global.
        with contextlib.ExitStack() as stack:
            for last in range(2, 12):
                stack.enter_context(receiver(DiscoveryTest.folder, f'127.77.24.{last}', stall=True))
            stack.enter_context(receiver(DiscoveryTest.folder, '127.77.24.254'))
            result, _ = probe(BINARY, 'scan', '127.77.24.1/0.0.0.0')
            print(json.dumps(dict(case='ten-silent-tls-before-valid-speaker', target='127.77.24.254', **result)))
    finally:
        DiscoveryTest.tearDownClass()


if __name__ == '__main__':
    main()
