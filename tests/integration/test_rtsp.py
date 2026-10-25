# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
import plistlib
import subprocess
import unittest

from common import TestHost
from rtsp_client import RtspClient
from test_lifecycle import MdnsBrowser


class Rtsp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host = TestHost()
        assert cls.host.start() == 'OK'

    @classmethod
    def tearDownClass(cls):
        cls.host.stop()
        cls.host.close()

    def setUp(self):
        self.client = RtspClient(self.host.raop_port)

    def tearDown(self):
        self.client.close()

    def test_options_lists_rtsp_methods(self):
        status, headers, _ = self.client.options()
        self.assertEqual(status, 200)
        self.assertEqual(headers['cseq'], '1')
        for method in ('SETUP', 'RECORD', 'TEARDOWN'):
            self.assertIn(method, headers['public'])

    def test_info_returns_a_binary_plist(self):
        status, headers, body = self.client.info()
        self.assertEqual(status, 200)
        self.assertEqual(headers['content-type'], 'application/x-apple-binary-plist')
        info = plistlib.loads(body)
        for key in ('features', 'model', 'sourceVersion', 'deviceID'):
            self.assertIn(key, info)

    def test_info_describes_this_receiver(self):
        """iOS keeps pk and deviceID from /info; they must be ours, not an Apple TV's."""
        _, server_ed_pub = self.client.pair_setup()
        info = plistlib.loads(self.client.info()[2])
        self.assertEqual(info['pk'], server_ed_pub)
        self.assertEqual(info['name'], 'LiteMirrorTest')
        self.assertEqual(info['deviceID'], info['macAddress'])
        with MdnsBrowser('LiteMirrorTest') as browser:
            airplay = browser.wait_for('_airplay._tcp.local.')
        self.assertEqual(info['deviceID'], airplay.properties[b'deviceid'].decode())
        self.assertEqual(info['model'], airplay.properties[b'model'].decode())

    def test_device_id_is_the_mac_of_the_default_route_adapter(self):
        """VPN and Hyper-V adapters come and go; the id must follow the real NIC
        or iOS forgets the receiver between runs."""
        ps = ('(Get-NetAdapter -InterfaceIndex (Get-NetRoute -DestinationPrefix 0.0.0.0/0 '
              '| Sort-Object RouteMetric,InterfaceMetric | Select-Object -First 1).InterfaceIndex).MacAddress')
        mac = subprocess.run(['powershell', '-NoProfile', '-Command', ps],
                             capture_output=True, text=True, errors='ignore').stdout.strip()
        self.assertRegex(mac, r'^([0-9A-F]{2}-){5}[0-9A-F]{2}$', 'could not read the default route adapter')
        info = plistlib.loads(self.client.info()[2])
        self.assertEqual(info['deviceID'], mac.lower().replace('-', ':'))

    def test_pair_setup_returns_server_ed25519_key(self):
        status, body = self.client.pair_setup()
        self.assertEqual(status, 200)
        self.assertEqual(len(body), 32)

    def test_pair_verify_completes(self):
        self.client.pair_setup()
        self.assertEqual(self.client.pair_verify(), (200, 200))
        # connection still usable afterwards
        self.assertEqual(self.client.options()[0], 200)

    def test_pair_verify_rejects_forged_signature(self):
        self.client.pair_setup()
        status1, status2 = self.client.pair_verify(forge_signature=True)
        self.assertEqual(status1, 200)
        # the server answers, then drops the connection
        with self.assertRaises((ConnectionError, OSError)):
            self.client.options()
            self.client.options()

    def test_pair_setup_rejects_wrong_key_length(self):
        status, _, body = self.client.request('POST', '/pair-setup', b'short', 'application/octet-stream')
        self.assertEqual(status, 200)
        self.assertEqual(body, b'')


DEVICE = 'TestPhone'
DEVICE_ID = 'AA:BB:CC:DD:EE:FF'


class Session(unittest.TestCase):
    """What a paired phone does after fp-setup, as far as we can go without
    FairPlay: open the session, start audio, send SET_PARAMETER updates."""

    def setUp(self):
        self.host = TestHost()
        self.assertEqual(self.host.start(), 'OK')
        self.client = RtspClient(self.host.raop_port)
        self.client.pair_setup()
        self.assertEqual(self.client.pair_verify(), (200, 200))
        self.assertEqual(self.client.setup_session(DEVICE, DEVICE_ID)[0], 200)
        self.assertEqual(self.client.setup_audio()[0], 200)

    def tearDown(self):
        self.client.close()
        self.host.stop()
        self.host.close()

    def test_audio_stream_reports_the_device_connected(self):
        self.assertEqual(self.host.wait_for('EVENT connected'), f'EVENT connected {DEVICE} {DEVICE_ID}')
        self.client.close()
        self.assertEqual(self.host.wait_for('EVENT disconnected'), f'EVENT disconnected {DEVICE} {DEVICE_ID}')

    def test_volume_is_forwarded(self):
        self.assertEqual(self.client.set_volume(-20.0)[0], 200)
        self.assertEqual(self.host.wait_for('EVENT volume'), f'EVENT volume {DEVICE_ID} -20.0')

    def test_metadata_is_forwarded(self):
        dmap = b'mlit' + (36).to_bytes(4, 'big') + b'minm' + (5).to_bytes(4, 'big') + b'Track'
        self.assertEqual(self.client.set_metadata(dmap)[0], 200)
        self.assertEqual(self.host.wait_for('EVENT metadata'), f'EVENT metadata {DEVICE_ID} {len(dmap)}')

    def test_cover_art_is_forwarded(self):
        jpeg = b'\xff\xd8\xff\xe0' + bytes(300) + b'\xff\xd9'
        self.assertEqual(self.client.set_cover_art(jpeg)[0], 200)
        self.assertEqual(self.host.wait_for('EVENT coverart'), f'EVENT coverart {DEVICE_ID} {len(jpeg)}')

    def test_play_url_is_forwarded(self):
        http = RtspClient(self.host.airplay_port, proto='HTTP/1.1')
        try:
            self.assertEqual(http.play('http://example.com/clip.mp4', 0.25)[0], 200)
        finally:
            http.close()
        self.assertEqual(self.host.wait_for('EVENT play'), 'EVENT play http://example.com/clip.mp4')


class Malformed(unittest.TestCase):
    """Requests a buggy or hostile client might send. The server may refuse
    them, but it must stay up for the next client."""

    def setUp(self):
        self.host = TestHost()
        self.assertEqual(self.host.start(), 'OK')
        self.client = RtspClient(self.host.raop_port)

    def tearDown(self):
        self.client.close()
        self.host.stop()
        self.host.close()

    def assertServerStillAnswers(self):
        self.assertIsNone(self.host.proc.poll(), 'testhost died')
        fresh = RtspClient(self.host.raop_port)
        try:
            self.assertEqual(fresh.options()[0], 200)
        finally:
            fresh.close()

    def test_setup_with_missing_keys_does_not_kill_the_server(self):
        try:
            self.client._plist('SETUP', self.client.SESSION_URL, {'et': 32, 'eiv': bytes(16)})
        except (ConnectionError, OSError):
            pass   # dropping the connection is an acceptable answer
        self.assertServerStillAnswers()

    def test_setup_with_short_keys_does_not_kill_the_server(self):
        try:
            self.client._plist('SETUP', self.client.SESSION_URL,
                               {'et': 32, 'eiv': b'\x01', 'ekey': b'\x02', 'timingPort': 0,
                                'name': DEVICE, 'deviceID': DEVICE_ID})
        except (ConnectionError, OSError):
            pass
        self.assertServerStillAnswers()

    def test_teardown_without_stream_type_does_not_kill_the_server(self):
        try:
            self.client._plist('TEARDOWN', self.client.SESSION_URL, {'streams': [{}]})
        except (ConnectionError, OSError):
            pass
        self.assertServerStillAnswers()


if __name__ == '__main__':
    unittest.main()
