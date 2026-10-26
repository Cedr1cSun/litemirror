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
        # what we ask the phone for: 1080p at 60 fps
        display = info['displays'][0]
        self.assertEqual((display['widthPixels'], display['heightPixels']), (1920, 1080))
        self.assertEqual(display['maxFPS'], 60)
        self.assertEqual(display['refreshRate'], 60)

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


class Pin(unittest.TestCase):
    """With a PIN the first SETUP needs Digest credentials, as on an Apple TV
    with a password; the rest of the connection is then trusted."""

    def setUp(self):
        self.host = TestHost()

    def tearDown(self):
        self.host.close()

    def paired_client(self):
        client = RtspClient(self.host.raop_port)
        client.pair_setup()
        self.assertEqual(client.pair_verify(), (200, 200))
        return client

    def test_fixed_pin_is_checked_on_the_first_setup(self):
        self.assertEqual(self.host.cmd('pin fixed 2580'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        client = self.paired_client()
        status, headers, _ = client.setup_session(DEVICE, DEVICE_ID)
        self.assertEqual(status, 401)
        challenge = headers['www-authenticate']
        self.assertIn('Digest realm="raop"', challenge)
        self.assertIn('nonce="', challenge)
        self.assertEqual(client.setup_session(DEVICE, DEVICE_ID, '0000', challenge)[0], 401)
        self.assertEqual(client.setup_session(DEVICE, DEVICE_ID, '2580', challenge)[0], 200)
        self.assertEqual(client.setup_audio()[0], 200)   # no second challenge on the same connection
        self.assertEqual(self.host.wait_for('EVENT connected'), f'EVENT connected {DEVICE} {DEVICE_ID}')
        client.close()
        self.assertEqual(self.host.stop(), 'OK')

    def test_random_pin_is_shown_until_the_phone_gets_it_right(self):
        self.assertEqual(self.host.cmd('pin random'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        client = self.paired_client()
        status, headers, _ = client.setup_session(DEVICE, DEVICE_ID)
        self.assertEqual(status, 401)
        shown = self.host.wait_for('EVENT pin').split()
        self.assertRegex(shown[2], r'^\d{4}$')
        self.assertEqual(client.setup_session(DEVICE, DEVICE_ID, shown[2], headers['www-authenticate'])[0], 200)
        self.assertEqual(self.host.wait_for('EVENT pin'), 'EVENT pin -')   # accepted: take it off the screen
        client.close()
        self.assertEqual(self.host.stop(), 'OK')

    def test_random_pin_is_taken_down_when_the_phone_gives_up(self):
        self.assertEqual(self.host.cmd('pin random'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        client = self.paired_client()
        self.assertEqual(client.setup_session(DEVICE, DEVICE_ID)[0], 401)
        self.host.wait_for('EVENT pin')
        client.close()
        self.assertEqual(self.host.wait_for('EVENT pin'), 'EVENT pin -')
        self.assertEqual(self.host.stop(), 'OK')

    def test_pin_is_advertised_so_the_phone_asks_for_it(self):
        self.assertEqual(self.host.cmd('pin fixed 1234'), 'OK')
        self.assertEqual(self.host.start(name='LiteMirrorPin'), 'OK')
        with MdnsBrowser('LiteMirrorPin') as browser:
            raop = browser.wait_for('_raop._tcp.local.')
            airplay = browser.wait_for('_airplay._tcp.local.')
        self.assertEqual(raop.properties[b'pw'], b'true')
        self.assertEqual(airplay.properties[b'pw'], b'true')
        info = plistlib.loads(RtspClient(self.host.raop_port).info()[2])
        self.assertTrue(info['statusFlags'] & 0x40, 'PasswordRequired flag missing')
        self.assertEqual(self.host.stop(), 'OK')

    def test_pin_off_needs_no_credentials(self):
        self.assertEqual(self.host.cmd('pin off'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        client = self.paired_client()
        self.assertEqual(client.setup_session(DEVICE, DEVICE_ID)[0], 200)
        client.close()
        self.assertEqual(self.host.stop(), 'OK')


class Display(unittest.TestCase):
    """The size and frame rate in /info are what the phone encodes at; a
    smaller display means less bandwidth and CPU."""

    def setUp(self):
        self.host = TestHost()

    def tearDown(self):
        self.host.close()

    def test_info_advertises_the_chosen_display(self):
        self.assertEqual(self.host.cmd('display 1280 720 30'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        display = plistlib.loads(RtspClient(self.host.raop_port).info()[2])['displays'][0]
        self.assertEqual((display['width'], display['height']), (1280, 720))
        self.assertEqual((display['widthPixels'], display['heightPixels']), (1280, 720))
        self.assertEqual(display['maxFPS'], 30)
        self.assertEqual(display['refreshRate'], 30)
        self.assertEqual(self.host.stop(), 'OK')


class SecondDevice(unittest.TestCase):
    """A second phone while one is mirroring: turned away (the default, with
    the 453 an Apple TV sends when busy) or let in, replacing the first."""

    def setUp(self):
        self.host = TestHost()

    def tearDown(self):
        self.host.close()

    def connect(self, name, device_id):
        client = RtspClient(self.host.raop_port)
        client.pair_setup()
        self.assertEqual(client.pair_verify(), (200, 200))
        status = client.setup_session(name, device_id)[0]
        if status == 200:
            self.assertEqual(client.setup_audio()[0], 200)
        return client, status

    def test_second_device_is_refused_by_default(self):
        self.assertEqual(self.host.start(), 'OK')
        first, _ = self.connect(DEVICE, DEVICE_ID)
        self.assertEqual(self.host.wait_for('EVENT connected'), f'EVENT connected {DEVICE} {DEVICE_ID}')
        second, status = self.connect('OtherPhone', '11:22:33:44:55:66')
        self.assertEqual(status, 453)
        self.assertEqual(first.set_volume(-10.0)[0], 200)   # the first one carries on
        self.assertEqual(self.host.wait_for('EVENT volume'), f'EVENT volume {DEVICE_ID} -10.0')
        self.assertEqual(self.host.events(), [])   # no connected event for the second
        second.close()
        first.close()
        self.assertEqual(self.host.stop(), 'OK')

    def test_first_device_may_connect_again_after_it_left(self):
        self.assertEqual(self.host.start(), 'OK')
        first, _ = self.connect(DEVICE, DEVICE_ID)
        self.host.wait_for('EVENT connected')
        first.close()
        self.host.wait_for('EVENT disconnected')
        again, status = self.connect(DEVICE, DEVICE_ID)
        self.assertEqual(status, 200)
        again.close()
        self.assertEqual(self.host.stop(), 'OK')

    def test_ask_policy_puts_the_question_to_the_shell(self):
        """The shell is asked while the second phone waits; its answer decides."""
        self.assertEqual(self.host.cmd('second ask'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        first, _ = self.connect(DEVICE, DEVICE_ID)
        self.assertEqual(self.host.wait_for('EVENT connected'), f'EVENT connected {DEVICE} {DEVICE_ID}')

        import threading
        outcome = {}
        def second_phone():
            outcome['client'], outcome['status'] = self.connect('OtherPhone', '11:22:33:44:55:66')
        t = threading.Thread(target=second_phone)
        t.start()
        self.assertEqual(self.host.wait_for('EVENT second'), 'EVENT second OtherPhone 11:22:33:44:55:66')
        self.assertTrue(t.is_alive(), 'the second phone was answered before the shell decided')
        self.assertEqual(self.host.cmd('admit no'), 'OK')
        t.join(10)
        self.assertEqual(outcome['status'], 453)
        self.assertEqual(first.set_volume(-5.0)[0], 200)   # the first one is untouched
        outcome['client'].close()

        t = threading.Thread(target=second_phone)
        t.start()
        self.host.wait_for('EVENT second')
        self.assertEqual(self.host.cmd('admit yes'), 'OK')
        t.join(10)
        self.assertEqual(outcome['status'], 200)
        self.assertEqual(self.host.wait_for('EVENT disconnected'), f'EVENT disconnected {DEVICE} {DEVICE_ID}')
        self.assertEqual(self.host.wait_for('EVENT connected'), 'EVENT connected OtherPhone 11:22:33:44:55:66')
        outcome['client'].close()
        self.assertEqual(self.host.stop(), 'OK')

    def test_second_device_replaces_the_first_when_asked(self):
        self.assertEqual(self.host.cmd('second replace'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        first, _ = self.connect(DEVICE, DEVICE_ID)
        self.assertEqual(self.host.wait_for('EVENT connected'), f'EVENT connected {DEVICE} {DEVICE_ID}')
        second, status = self.connect('OtherPhone', '11:22:33:44:55:66')
        self.assertEqual(status, 200)
        self.assertEqual(self.host.wait_for('EVENT disconnected'), f'EVENT disconnected {DEVICE} {DEVICE_ID}')
        self.assertEqual(self.host.wait_for('EVENT connected'), 'EVENT connected OtherPhone 11:22:33:44:55:66')
        with self.assertRaises((ConnectionError, OSError)):   # the first one was cut off
            first.options()
            first.options()
        second.close()
        self.assertEqual(self.host.stop(), 'OK')


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

    def test_teardown_of_a_stream_never_set_up_does_not_kill_the_server(self):
        for stream_type in (96, 110):
            try:
                self.client._plist('TEARDOWN', self.client.SESSION_URL, {'streams': [{'type': stream_type}]})
            except (ConnectionError, OSError):
                self.client = RtspClient(self.host.raop_port)
            self.assertServerStillAnswers()

    def test_parameter_requests_without_a_content_type_do_not_kill_the_server(self):
        for method in ('GET_PARAMETER', 'SET_PARAMETER'):
            try:
                self.client.request(method, self.client.SESSION_URL)
            except (ConnectionError, OSError):
                self.client = RtspClient(self.host.raop_port)
            self.assertServerStillAnswers()

    def test_fp_setup_answers_only_the_modes_it_knows(self):
        # the 16-byte first message: version 3 at byte 4, the mode at byte 14 (0 to 3 are known)
        for mode in (0, 3):
            status, _, body = self.client.request('POST', '/fp-setup', bytes([0x46, 0x50, 0x4c, 0x59, 3, 1, 1, 0, 0, 0, 0, 4, 0, 0, mode, 0]))
            self.assertEqual((status, len(body)), (200, 142))
        status, _, body = self.client.request('POST', '/fp-setup', bytes([0x46, 0x50, 0x4c, 0x59, 3, 1, 1, 0, 0, 0, 0, 4, 0, 0, 200, 0]))
        self.assertEqual(len(body), 0)
        self.assertServerStillAnswers()

    def test_an_oversized_body_is_cut_off(self):
        try:
            self.client.request('SET_PARAMETER', self.client.SESSION_URL, bytes(2 * 1024 * 1024), 'text/parameters')
        except (ConnectionError, OSError):
            pass   # dropping the connection is the expected answer
        self.assertServerStillAnswers()


if __name__ == '__main__':
    unittest.main()
