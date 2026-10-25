# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
"""Drive litemirror-testhost.exe over stdin/stdout."""
import os
import queue
import subprocess
import threading
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def testhost_path():
    exe = os.environ.get('LITEMIRROR_TESTHOST')
    if not exe:
        config = os.environ.get('LITEMIRROR_CONFIG', 'Debug')
        exe = os.path.join(REPO, 'build', 'bin', config, 'litemirror-testhost.exe')
    if not os.path.exists(exe):
        raise FileNotFoundError(f'{exe} not built; run build.ps1 first')
    return exe


class TestHost:
    """One testhost process. Lines it prints are queued; cmd() waits for a reply."""

    def __init__(self):
        self.proc = subprocess.Popen(
            [testhost_path()], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
            bufsize=1, cwd=os.path.dirname(testhost_path()))
        self.lines = queue.Queue()
        self.log = []
        threading.Thread(target=self._pump, daemon=True).start()
        self.wait_for('READY')

    def _pump(self):
        for line in self.proc.stdout:
            self.lines.put(line.rstrip('\r\n'))
        self.lines.put(None)

    def wait_for(self, prefix, timeout=15.0):
        """Return the first line starting with prefix; LOG lines are kept in self.log."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'no "{prefix}" within {timeout}s; last log: {self.log[-5:]}')
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if line is None:
                raise EOFError(f'testhost exited; last log: {self.log[-5:]}')
            if line.startswith('LOG '):
                self.log.append(line)
            if line.startswith(prefix):
                return line

    def cmd(self, line, timeout=15.0):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()
        reply = self.wait_for(('OK', 'FAIL', 'ERR'), timeout)
        return reply

    def start(self, name='LiteMirrorTest', raop_port=5200, airplay_port=7200):
        return self.cmd(f'start {name} {raop_port} {airplay_port}')

    def stop(self):
        return self.cmd('stop')

    def events(self, prefix='EVENT'):
        out = []
        while True:
            try:
                line = self.lines.get_nowait()
            except queue.Empty:
                return out
            if line is None:
                return out
            if line.startswith('LOG '):
                self.log.append(line)
            elif line.startswith(prefix):
                out.append(line)

    def close(self):
        try:
            self.proc.stdin.write('quit\n')
            self.proc.stdin.flush()
            self.proc.wait(timeout=15)
        except Exception:
            self.proc.kill()
