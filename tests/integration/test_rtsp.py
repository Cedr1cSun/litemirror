# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
import plistlib
import unittest

from common import TestHost
from rtsp_client import RtspClient


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


if __name__ == '__main__':
    unittest.main()
