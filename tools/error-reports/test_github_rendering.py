"""Regressão de Markdown hostil, com o contrato HTTP de GitHub local."""
import unittest

import test_receiver as fixture

FakeGitHub, r = fixture.FakeGitHub, fixture.r


class GitHubRenderingTests(unittest.TestCase):
    setUp = fixture.ReceiverTests.setUp
    tearDown = fixture.ReceiverTests.tearDown
    start = fixture.ReceiverTests.start
    post = fixture.ReceiverTests.post
    emit = fixture.ReceiverTests.emit
    close_day = fixture.ReceiverTests.close_day

    def test_github_fields_are_literal_text(self):
        payload = 'failure #123 [click](../../issues/123) <a href="/relative">click</a>'
        for i in range(1, 4):
            self.emit(i, version='0.3.3 #123', hardware=payload, build=payload,
                      error='unclean session; ' + payload, log='@someone\n' + payload + '\n```\n#456')
        self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        issue = FakeGitHub.issues[0]
        self.assertNotIn('#123', issue['title'])
        self.assertIn(payload, issue['body'])
        for line in issue['body'].splitlines():
            if any(s in line for s in ('#123', '#456', '[click]', '<a ', '@someone', '```')):
                self.assertTrue(line.startswith('    '), repr(line))
        self.close_day(); self.store.deliver()
        self.assertNotIn(payload, FakeGitHub.issues[-1]['body'])

