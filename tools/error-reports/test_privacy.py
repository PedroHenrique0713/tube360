"""Canários sintéticos para a segunda limpeza do receptor."""
import unittest
import test_receiver as fixture
r = fixture.r


class PrivacyTests(unittest.TestCase):
    def test_privacy_canaries(self):
        self.data = dict(version='0.3.3', build='33', hardware='Corona', screen='Player',
                         category='crash', installation_id='1'*32, error='unclean session', log='')
        cases = [
            ('peer 203.0.113.42:8009', '203.0.113.42'),
            ('peer 2001:db8:abcd::42', '2001:db8:abcd::42'),
            ('peer [2001:db8::43]:8443', '2001:db8::43'),
            ('peer fe80::42%eth0', 'fe80::42'),
            ('peer livingroom.local:8009', 'livingroom.local'),
            ('https://youtube.com/watch?v=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
            ('https://youtu.be/dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
            ('https://youtube.com/shorts/dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
            ('id=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
            ('{"id":"dQw4w9WgXcQ"}', 'dQw4w9WgXcQ'),
            ('Cast: Sala da Maria', 'Sala da Maria'),
            ('SSID=Casa Secreta', 'Casa Secreta'),
            ('wifi connected Minha Rede', 'Minha Rede'),
            ('account: Canal da Maria', 'Canal da Maria'),
            ('person@example.invalid', 'person@example.invalid'),
            ('Bearer short.secret', 'short.secret'),
            ('ya29.short.secret', 'ya29.short.secret'),
            ('github_pat_short.private.value', 'short.private.value'),
            ('refresh_token=short.secret', 'short.secret'),
            ('access_token=short.secret', 'short.secret'),
            ('SAPISID=short.private.value', 'short.private.value'),
            ('__Secure-1PSID=short.private.value', 'short.private.value'),
            ('path /home/Alice/.config/tube360', 'Alice'),
            ('path /Users/Alice/tube360', 'Alice'),
            ('path C:\\Users\\Alice\\tube360', 'Alice'),
        ]
        for raw, secret in cases:
            with self.subTest(raw=raw):
                normalized = r.normalize({**self.data, 'hardware': raw, 'error': 'unclean session; ' + raw, 'log': raw})
                self.assertNotIn(secret, normalized[1])
        clean = r.scrub('\n'.join(raw for raw, _ in cases))
        self.assertEqual(r.scrub(clean), clean)

