# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
import shutil
import socket
import subprocess
import time
import unittest

from common import TestHost, free_port


class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.host = TestHost()

    def tearDown(self):
        self.host.close()

    def test_start_and_stop_twenty_times(self):
        raop, airplay = free_port(), free_port()
        for i in range(20):
            self.assertEqual(self.host.start(raop_port=raop, airplay_port=airplay), 'OK', f'start #{i}')
            t = time.monotonic()
            self.assertEqual(self.host.stop(), 'OK', f'stop #{i}')
            self.assertLess(time.monotonic() - t, 5.0, f'stop #{i} hung')

    def test_start_fails_when_raop_port_is_taken(self):
        blocker = socket.socket()
        blocker.bind(('0.0.0.0', 0))
        blocker.listen(1)
        taken = blocker.getsockname()[1]
        try:
            self.assertEqual(self.host.start(raop_port=taken), 'FAIL')
        finally:
            blocker.close()
        # and recovers once the port is free
        self.assertEqual(self.host.start(raop_port=taken), 'OK')
        self.assertEqual(self.host.stop(), 'OK')

    def test_rtsp_port_accepts_connections_only_while_running(self):
        self.assertEqual(self.host.start(), 'OK')
        port = self.host.raop_port
        socket.create_connection(('127.0.0.1', port), timeout=5).close()
        self.assertEqual(self.host.stop(), 'OK')
        with self.assertRaises(OSError):
            socket.create_connection(('127.0.0.1', port), timeout=2).close()

    @unittest.skipUnless(shutil.which('dns-sd'), 'dns-sd.exe (Bonjour) not installed')
    def test_bonjour_advertises_raop_and_airplay(self):
        name = 'LiteMirrorBonjour'
        self.assertEqual(self.host.start(name=name), 'OK')
        try:
            for service in ('_raop._tcp', '_airplay._tcp'):
                self.assertIn(name, self._browse(service), f'{service} not advertised')
        finally:
            self.host.stop()

    @staticmethod
    def _browse(service, seconds=4):
        proc = subprocess.Popen(['dns-sd', '-B', service, 'local.'],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = proc.communicate(timeout=seconds)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate()
        return out


if __name__ == '__main__':
    unittest.main()
