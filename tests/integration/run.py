# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LiteMirror contributors
"""Run the integration tests against a built litemirror-testhost.exe.

    python tests/integration/run.py [--config Debug|Release]

Needs the `cryptography` package (pip install -r requirements.txt).
"""
import argparse
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', default='Debug', choices=['Debug', 'Release'])
    parser.add_argument('-k', dest='pattern', default='test_*.py')
    args = parser.parse_args()
    os.environ['LITEMIRROR_CONFIG'] = args.config

    sys.path.insert(0, HERE)
    suite = unittest.defaultTestLoader.discover(HERE, pattern=args.pattern)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
