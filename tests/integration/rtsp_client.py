# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
"""Minimal AirPlay RTSP client: enough to reach the end of pair-verify.

Mirrors what an iOS device sends before fp-setup. The pairing maths follows
src/core/pairing.c; tests/unit/test_pairing.cpp pins the same byte layout.
"""
import hashlib
import plistlib
import re
import socket

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

RAW = (serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def _derive(shared, salt):
    h = hashes.Hash(hashes.SHA512())
    h.update(salt + shared)
    return h.finalize()[:16]


class RtspClient:
    def __init__(self, port, host='127.0.0.1', timeout=10.0, proto='RTSP/1.0'):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.proto = proto   # 'HTTP/1.1' talks to the AirPlay port instead
        self.cseq = 0
        self.ed = ed25519.Ed25519PrivateKey.generate()
        self.ed_pub = self.ed.public_key().public_bytes(*RAW)
        self.server_ed_pub = None
        self.shared = None

    def close(self):
        self.sock.close()

    def request(self, method, url, body=b'', content_type=None, headers=()):
        self.cseq += 1
        head = f'{method} {url} {self.proto}\r\nCSeq: {self.cseq}\r\nUser-Agent: LiteMirror-Test/1\r\n'
        for name, value in headers:
            head += f'{name}: {value}\r\n'
        if content_type:
            head += f'Content-Type: {content_type}\r\n'
        if body:
            head += f'Content-Length: {len(body)}\r\n'
        self.sock.sendall(head.encode() + b'\r\n' + body)
        return self._response()

    def _response(self):
        buf = b''
        while b'\r\n\r\n' not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('server closed the connection')
            buf += chunk
        head, _, rest = buf.partition(b'\r\n\r\n')
        lines = head.decode('latin-1').split('\r\n')
        status = int(lines[0].split(' ')[1])
        headers = {}
        for line in lines[1:]:
            k, _, v = line.partition(':')
            headers[k.strip().lower()] = v.strip()
        length = int(headers.get('content-length', '0'))
        while len(rest) < length:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('server closed the connection mid-body')
            rest += chunk
        return status, headers, rest[:length]

    def options(self):
        return self.request('OPTIONS', '*')

    def info(self):
        return self.request('GET', '/info')

    def pair_setup(self):
        status, _, body = self.request('POST', '/pair-setup', self.ed_pub, 'application/octet-stream')
        if status == 200 and len(body) == 32:
            self.server_ed_pub = body
        return status, body

    def pair_verify(self, forge_signature=False):
        """Run both pair-verify steps. Returns (status1, status2)."""
        x = x25519.X25519PrivateKey.generate()
        x_pub = x.public_key().public_bytes(*RAW)

        status1, _, body = self.request(
            'POST', '/pair-verify', b'\x01\x00\x00\x00' + x_pub + self.ed_pub, 'application/octet-stream')
        if status1 != 200 or len(body) != 96:
            return status1, None
        server_x_pub, server_sig = body[:32], body[32:]

        self.shared = x.exchange(x25519.X25519PublicKey.from_public_bytes(server_x_pub))
        key = _derive(self.shared, b'Pair-Verify-AES-Key')
        iv = _derive(self.shared, b'Pair-Verify-AES-IV')
        ctr = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()

        # the server used the first 64 keystream bytes for its own signature
        plain_sig = ctr.update(server_sig)
        ed25519.Ed25519PublicKey.from_public_bytes(self.server_ed_pub).verify(
            plain_sig, server_x_pub + x_pub)

        sig = self.ed.sign(x_pub + server_x_pub)
        if forge_signature:
            sig = bytes(64)
        status2, _, _ = self.request(
            'POST', '/pair-verify', b'\x00\x00\x00\x00' + ctr.update(sig), 'application/octet-stream')
        return status1, status2

    # --- session (what follows fp-setup on a real device) ---

    SESSION_URL = 'rtsp://127.0.0.1/1'

    def _plist(self, method, url, obj, headers=()):
        body = plistlib.dumps(obj, fmt=plistlib.FMT_BINARY)
        return self.request(method, url, body, 'application/x-apple-binary-plist', headers)

    def setup_session(self, name, device_id, pin=None, challenge=None):
        """First SETUP: announce the device. ekey is FairPlay-wrapped on a real
        phone; without fp-setup the server cannot unwrap it, so zeros do.
        With a pin, answers the server's 401 challenge the way iOS does."""
        headers = []
        if pin is not None:
            headers.append(('Authorization', self.digest(challenge, pin, 'SETUP', self.SESSION_URL)))
        return self._plist('SETUP', self.SESSION_URL, {
            'et': 32, 'eiv': bytes(16), 'ekey': bytes(72), 'timingPort': 0,
            'name': name, 'deviceID': device_id, 'model': 'iPhone10,3', 'sourceVersion': '220.68'},
            headers=headers)

    @staticmethod
    def digest(challenge, password, method, uri, username='iTunes'):
        """HTTP Digest (RFC 2069 form, as AirPlay uses it) for a WWW-Authenticate value."""
        realm = re.search(r'realm="([^"]*)"', challenge).group(1)
        nonce = re.search(r'nonce="([^"]*)"', challenge).group(1)
        md5 = lambda s: hashlib.md5(s.encode()).hexdigest()
        response = md5(f'{md5(f"{username}:{realm}:{password}")}:{nonce}:{md5(f"{method}:{uri}")}')
        return (f'Digest username="{username}", realm="{realm}", nonce="{nonce}", '
                f'uri="{uri}", response="{response}"')

    def setup_audio(self):
        """Audio SETUP (stream type 96): the server starts its RTP threads."""
        return self._plist('SETUP', self.SESSION_URL,
                           {'streams': [{'type': 96, 'audioFormat': 0x1000000, 'ct': 8, 'spf': 480}]},
                           headers=[('Transport', 'RTP/AVP/UDP;unicast;mode=record')])

    def set_volume(self, db):
        return self.request('SET_PARAMETER', self.SESSION_URL, f'volume: {db:.6f}\r\n'.encode(), 'text/parameters')

    def set_metadata(self, dmap):
        return self.request('SET_PARAMETER', self.SESSION_URL, dmap, 'application/x-dmap-tagged')

    def set_cover_art(self, jpeg):
        return self.request('SET_PARAMETER', self.SESSION_URL, jpeg, 'image/jpeg')

    def play(self, url, start_position=0.0):
        """AirPlay HTTP /play; needs proto='HTTP/1.1' and the AirPlay port."""
        return self._plist('POST', '/play', {'Content-Location': url, 'Start-Position': start_position})
