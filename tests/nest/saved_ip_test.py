#!/usr/bin/env python3
"""Recuperação iniciada pela falha do canal de reprodução, sem Nest real."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'bench/standalone'))
from cast_test import Driver, Receiver


class SavedIPTest(unittest.TestCase):
    def test_playback_failure_recovers_legacy_changed_ip(self):
        with tempfile.TemporaryDirectory(prefix='tube360-saved-ip-') as temp:
            folder = Path(temp)
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                            '-subj', '/CN=Cast fixture', '-keyout', str(folder/'key.pem'),
                            '-out', str(folder/'cert.pem')], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            (folder/'nest.txt').write_text('127.77.24.9 8009\nSala\n')
            receiver = Receiver(folder/'cert.pem', folder/'key.pem', port=8009,
                                host='127.77.24.2', name='Sala', handshake_delay=1)
            old = os.environ.get('XT_NEST_NETWORK')
            os.environ['XT_NEST_NETWORK'] = '127.77.24.1/255.255.255.0'
            driver = None
            try:
                driver = Driver(str(ROOT/'client/build/nest-test'), folder)
                began = time.monotonic()
                driver.command('play https://audio.example/itag/140 0 1')
                driver.until(lambda sample: sample['state'] == 2 and sample['updated'] > 0, timeout=10)
                self.assertTrue((folder/'nest.txt').read_text().startswith('127.77.24.2 8009\n'))
                self.assertEqual(len(receiver.commands('LOAD')), 1)
                print(f'PASS falha no IP legado → busca → novo IP → PLAYING em {time.monotonic()-began:.3f}s')
                driver.command('stop')
                driver.until(lambda sample: sample['quiet'])
            finally:
                if driver:
                    driver.close()
                receiver.close()
                if old is None:
                    os.environ.pop('XT_NEST_NETWORK', None)
                else:
                    os.environ['XT_NEST_NETWORK'] = old
            self.assertEqual(receiver.errors, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
