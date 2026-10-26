# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
import contextlib
import socket
import subprocess
import threading
import time
import unittest

from zeroconf import ServiceBrowser, ServiceStateChange, Zeroconf

from common import TestHost, free_port

# lm_error values from src/sdk/include/litemirror.h
LM_E_RAOP_PORT = 4
LM_E_AIRPLAY_PORT = 5


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
        with self._blocked_port() as taken:
            self.assertEqual(self._start_error(raop_port=taken), LM_E_RAOP_PORT)
        # and recovers once the port is free
        self.assertEqual(self.host.start(raop_port=taken), 'OK')
        self.assertEqual(self.host.stop(), 'OK')

    def test_start_fails_when_airplay_port_is_taken(self):
        with self._blocked_port() as taken:
            self.assertEqual(self._start_error(airplay_port=taken), LM_E_AIRPLAY_PORT)

    @staticmethod
    @contextlib.contextmanager
    def _blocked_port():
        blocker = socket.socket()
        blocker.bind(('0.0.0.0', 0))
        blocker.listen(1)
        try:
            yield blocker.getsockname()[1]
        finally:
            blocker.close()

    def _start_error(self, **ports):
        reply = self.host.start(**ports)
        self.assertTrue(reply.startswith('FAIL '), reply)
        return int(reply.split()[1])

    def test_rtsp_port_accepts_connections_only_while_running(self):
        self.assertEqual(self.host.start(), 'OK')
        port = self.host.raop_port
        socket.create_connection(('127.0.0.1', port), timeout=5).close()
        self.assertEqual(self.host.stop(), 'OK')
        with self.assertRaises(OSError):
            socket.create_connection(('127.0.0.1', port), timeout=2).close()

    def test_mdns_advertises_raop_and_airplay(self):
        """Browse the LAN the way an iPhone does and check both services."""
        name = 'LiteMirrorMdns'
        self.assertEqual(self.host.start(name=name), 'OK')
        with MdnsBrowser(name) as browser:
            raop = browser.wait_for('_raop._tcp.local.')
            airplay = browser.wait_for('_airplay._tcp.local.')
            self.assertEqual(raop.port, self.host.raop_port)
            self.assertEqual(airplay.port, self.host.airplay_port)
            self.assertIn(b'et', raop.properties)
            self.assertIn(b'deviceid', airplay.properties)
            self.assertIn(b'features', airplay.properties)

            self.assertEqual(self.host.stop(), 'OK')
            self.assertTrue(browser.wait_gone('_raop._tcp.local.'), 'raop still advertised after stop')
            self.assertTrue(browser.wait_gone('_airplay._tcp.local.'), 'airplay still advertised after stop')

    def test_runs_without_bonjour(self):
        """Most users have no Bonjour; the server must never load Apple's dnssd.dll."""
        self.assertEqual(self.host.start(), 'OK')
        modules = subprocess.run(['tasklist', '/M', 'dnssd.dll', '/FI', f'PID eq {self.host.proc.pid}'],
                                 capture_output=True, text=True, errors='ignore').stdout
        self.assertNotIn('dnssd.dll', modules.lower(), 'testhost loaded Bonjour')
        self.assertEqual(self.host.stop(), 'OK')

    def test_mdns_registers_again_after_restart(self):
        name = 'LiteMirrorAgain'
        for _ in range(2):
            t = time.monotonic()
            self.assertEqual(self.host.start(name=name), 'OK')
            # mDNS probing costs ~0.75 s per service; both must run at once
            self.assertLess(time.monotonic() - t, 1.2, 'start too slow')
            with MdnsBrowser(name) as browser:
                browser.wait_for('_raop._tcp.local.')
            self.assertEqual(self.host.stop(), 'OK')


LM_E_ADDRESS = 10


def lan_address():
    """An IPv4 address of this machine other than loopback, or None."""
    for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
        if not ip.startswith('127.'):
            return ip
    return None


class Bind(unittest.TestCase):
    """Listen and announce on one address only: a user with a VPN or a hotspot
    adapter picks the one the phone can reach."""

    def setUp(self):
        self.host = TestHost()

    def tearDown(self):
        self.host.close()

    def test_bound_receiver_listens_on_that_address_only(self):
        ip = lan_address()
        if ip is None:
            self.skipTest('no LAN address on this machine')
        self.assertEqual(self.host.cmd(f'bind {ip}'), 'OK')
        self.assertEqual(self.host.start(name='LiteMirrorBind'), 'OK')
        socket.create_connection((ip, self.host.raop_port), timeout=5).close()
        socket.create_connection((ip, self.host.airplay_port), timeout=5).close()
        with self.assertRaises(OSError):
            socket.create_connection(('127.0.0.1', self.host.raop_port), timeout=2).close()
        with MdnsBrowser('LiteMirrorBind') as browser:
            raop = browser.wait_for('_raop._tcp.local.')
        # Windows adds the adapter's IPv6 link-local address of its own accord
        self.assertEqual([a for a in raop.parsed_addresses() if ':' not in a], [ip])
        self.assertEqual(self.host.stop(), 'OK')

    def test_bind_to_an_address_this_machine_lacks_is_an_error(self):
        self.assertEqual(self.host.cmd('bind 203.0.113.1'), 'OK')   # valid syntax, nobody here
        reply = self.host.start()
        self.assertTrue(reply.startswith('FAIL '), reply)
        self.assertEqual(int(reply.split()[1]), LM_E_ADDRESS)
        self.assertEqual(self.host.cmd('bind -'), 'OK')
        self.assertEqual(self.host.start(), 'OK')
        self.assertEqual(self.host.stop(), 'OK')


class MdnsBrowser:
    """Collects mDNS services whose instance name ends with '@<name>' or
    equals <name> (RAOP prefixes the MAC address, AirPlay does not)."""

    TYPES = ('_raop._tcp.local.', '_airplay._tcp.local.')

    def __init__(self, name):
        self.name = name
        self.found = {}
        self.lock = threading.Lock()

    def __enter__(self):
        self.zc = Zeroconf()
        self.browser = ServiceBrowser(self.zc, list(self.TYPES), handlers=[self._on_change])
        return self

    def __exit__(self, *exc):
        self.zc.close()

    def _mine(self, service_name, type_):
        instance = service_name[:-len(type_) - 1]
        return instance == self.name or instance.endswith('@' + self.name)

    def _on_change(self, zeroconf, service_type, name, state_change):
        # zeroconf passes these as keyword arguments, so the names are fixed
        if not self._mine(name, service_type):
            return
        with self.lock:
            if state_change is ServiceStateChange.Removed:
                self.found.pop(service_type, None)
            else:
                info = zeroconf.get_service_info(service_type, name, timeout=3000)
                if info:
                    self.found[service_type] = info

    def wait_for(self, type_, seconds=8.0):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            with self.lock:
                if type_ in self.found:
                    return self.found[type_]
            time.sleep(0.1)
        raise AssertionError(f'{type_} for {self.name} not seen within {seconds}s')

    def wait_gone(self, type_, seconds=8.0):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            with self.lock:
                if type_ not in self.found:
                    return True
            time.sleep(0.1)
        return False


if __name__ == '__main__':
    unittest.main()
