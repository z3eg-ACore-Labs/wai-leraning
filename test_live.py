"""Offline action selection/transport checks; no SOAP server is contacted."""
import unittest
import urllib.error
from unittest.mock import Mock, patch

from live import Soap, choose_action


class ActionTests(unittest.TestCase):
    def test_observation_timeouts_retry_but_actions_do_not(self):
        client = Soap('http://127.0.0.1:7878/', 'operator', 'test-placeholder', 'waiepisode ')
        response = Mock()
        response.read.return_value = b'<Envelope><executeCommandResponse>ok</executeCommandResponse></Envelope>'
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        for command in ('waiepisode observe', 'waiepisode party 2 observe'):
            opener = Mock()
            opener.open.side_effect = [TimeoutError(), urllib.error.URLError(TimeoutError()), response]
            with patch('urllib.request.build_opener', return_value=opener):
                self.assertEqual(client.command(command), 'ok')
            self.assertEqual(opener.open.call_count, 3)
        for command in ('waiepisode act fixture 1 0 v5', 'waiepisode observe'):
            opener = Mock()
            opener.open.side_effect = TimeoutError()
            with patch('urllib.request.build_opener', return_value=opener), self.assertRaises(TimeoutError):
                client.command(command)
            self.assertEqual(opener.open.call_count, 3 if command.endswith('observe') else 1)
        opener = Mock()
        opener.open.side_effect = urllib.error.HTTPError(client.url, 401, 'Unauthorized', {}, None)
        with patch('urllib.request.build_opener', return_value=opener), self.assertRaises(urllib.error.HTTPError):
            client.command('waiepisode observe')
        self.assertEqual(opener.open.call_count, 1)

    def setUp(self):
        self.spells = [779, 5232, 5487, 6808, 8618, 16857]
        self.obs = {'busy': False, 'lootable': False, 'enemy': False, 'attacking': False,
                    'features': [0] * 16, 'distance': 0, 'allowed_spells': [5232, 5487]}

    def test_spell_mask_prevents_best_unavailable_spell(self):
        action, source = choose_action(self.obs, [.9, .03, .02, .03, .01, .01], self.spells)
        self.assertEqual(action, 'cast 5232')
        self.assertTrue(source.startswith('learned/'))

    def test_no_legal_action_waits_instead_of_cheating(self):
        self.obs['allowed_spells'] = []
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells)[0], 'wait')

    def test_loot_and_attack_are_honestly_labelled_helpers(self):
        self.obs.update(lootable=True, distance=8)
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells), ('approach', 'native/loot'))
        self.obs.update(distance=2)
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells), ('loot', 'native/loot'))
        self.obs.update(lootable=False, enemy=True)
        self.obs['features'][4] = 1
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells)[0], 'attack')

    def test_busy_does_not_interrupt_existing_cast(self):
        self.obs.update(busy=True, lootable=True)
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells)[0], 'wait')

    def test_skinnable_corpse_is_approached_before_in_range_inference(self):
        self.obs['features'][7] = 1
        self.obs.update(distance=8, allowed_spells=[])
        self.assertEqual(choose_action(self.obs, [1] * 6, self.spells), ('approach', 'native/corpse-approach'))

    def test_transport_requires_local_endpoint_and_credentials(self):
        for url in ('https://example.com/', 'http://example.com/', 'file:///test', 'http://u:p@127.0.0.1/'):
            with self.assertRaises(ValueError):
                Soap(url, 'operator', 'test-placeholder')
        with self.assertRaises(ValueError):
            Soap('http://127.0.0.1:7878/', '', '')

    def test_command_injection_rejected_before_network(self):
        client = Soap('http://127.0.0.1:7878/', 'operator', 'test-placeholder')
        for command in ('server shutdown', 'wai observe\nserver shutdown', 'wai stop;server shutdown'):
            with self.assertRaises(ValueError):
                client.command(command)

    def test_episode_namespace_is_explicit_and_preserves_soap_transport(self):
        client = Soap('http://127.0.0.1:7878/', 'operator', 'test-placeholder', 'waiepisode ')
        with self.assertRaises(ValueError):
            client.command('wai observe')
        response = Mock()
        response.read.return_value = b'<Envelope><executeCommandResponse>fixture</executeCommandResponse></Envelope>'
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        opener = Mock()
        opener.open.return_value = response
        with patch('urllib.request.build_opener', return_value=opener):
            self.assertEqual(client.command('waiepisode observe'), 'fixture')
        request = opener.open.call_args.args[0]
        self.assertIn(b'<command>waiepisode observe</command>', request.data)
        with self.assertRaises(ValueError):
            Soap('http://127.0.0.1:7878/', 'operator', 'test-placeholder', 'server ')


if __name__ == '__main__':
    unittest.main()
