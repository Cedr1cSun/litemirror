# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
"""Minimal AirPlay RTSP client: enough to reach the end of pair-verify.

Mirrors what an iOS device sends before fp-setup. The pairing maths follows
src/core/pairing.c; tests/unit/test_pairing.cpp pins the same byte layout.
"""
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
    def __init__(self, port, host='127.0.0.1', timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.cseq = 0
        self.ed = ed25519.Ed25519PrivateKey.generate()
        self.ed_pub = self.ed.public_key().public_bytes(*RAW)
        self.server_ed_pub = None
        self.shared = None

    def close(self):
        self.sock.close()

    def request(self, method, url, body=b'', content_type=None):
        self.cseq += 1
        head = f'{method} {url} RTSP/1.0\r\nCSeq: {self.cseq}\r\nUser-Agent: LiteMirror-Test/1\r\n'
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
