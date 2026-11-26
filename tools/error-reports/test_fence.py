"""Orçamento normal da fence não confirma falha nem determina a assinatura."""
import unittest

import receiver as r


INFO = ('boot app #0001 +1ms begin frame.fence first-frame timeout=5000ms\n'
        'boot app #0002 +2ms begin frame.complete GPU fence timeout=5000ms; '
        'physical screen cannot be verified by software\n')


class FenceTests(unittest.TestCase):
    def data(self, error, log=INFO):
        return dict(version='0.3.3', build='33', hardware='Corona', screen='Player',
                    installation_id='1' * 32, category='crash', error=error, log=log)

    def test_information_is_not_crash_or_network_timeout(self):
        for log in INFO.splitlines() + [INFO, r.scrub(INFO)]:
            with self.subTest(log=log):
                self.assertEqual(r.classify(self.data('diagnostic', log)), ('other', 'other'))
                self.assertEqual(r.classify(self.data(log, '')), ('other', 'other'))
        self.assertEqual(r.classify(self.data('diagnostic', 'GPU fence timeout=5000ms\nDNS failed')),
                         ('network', 'network.dns'))
        self.assertEqual(r.classify(self.data('diagnostic', INFO + 'DNS failed')),
                         ('network', 'network.dns'))

    def test_legacy_sanitized_information(self):
        legacy = self.data('diagnostic', r.scrub(INFO))
        legacy.pop('category')
        legacy.pop('installation_id')
        self.assertIn('frame.fence [video] timeout=', legacy['log'])
        clean, *_, point = r.normalize(legacy)
        self.assertEqual((clean['category'], point), ('other', 'other'))

    def test_actual_failures_remain_crashes(self):
        for failure, point in [('Vulkan falhou (2): vkWaitForFences(g.dev, 1, &fence, VK_TRUE, 5000000000)', 'gpu.fence'),
                               ('gpu.vulkan -4: vkWaitForFences(...)', 'gpu.fence'),
                               ('boot app error gpu.vulkan 2: vkWaitForFences(...)', 'gpu.fence'),
                               ('Vulkan falhou (-4): vkQueueSubmit(...)', 'gpu.vulkan'),
                               ('gpu.fence failed', 'gpu.fence'), ('gpu fence timed out', 'gpu.fence'),
                               ('gpu_fence failed', 'gpu.fence'), ('gpu-fence failed', 'gpu.fence'),
                               ('gpu.fence timeout', 'gpu.fence'), ('gpu.fence status=dead', 'gpu.fence')]:
            with self.subTest(failure=failure):
                self.assertEqual(r.classify(self.data(failure)), ('crash', point))
                self.assertEqual(r.classify(self.data('diagnostic', INFO + failure)), ('crash', point))

    def test_fault_signature_ignores_incidental_gpu_information(self):
        for error in ('segmentation fault', 'excecao: codigo c0000005 endereco 82001234',
                      'unclean session; last checkpoint: app.loop'):
            without = r.normalize(self.data(error, ''))
            with_info = r.normalize(self.data(error))
            self.assertEqual(without[-2:], with_info[-2:])
            self.assertNotEqual(with_info[-1], 'gpu.fence')

    def test_open_session_and_legacy_checkpoint_stay_reportable(self):
        self.assertEqual(r.classify(self.data('unclean session')), ('crash', 'session.unclean'))
        for point in ('gpu.fence', 'frame.fence', 'frame.complete'):
            self.assertEqual(r.classify(self.data('unclean session; last checkpoint: ' + point)),
                             ('crash', 'session.unclean'))
            self.assertEqual(r.classify(self.data('startup.incomplete last checkpoint: ' + point)),
                             ('startup', 'startup.handoff'))
