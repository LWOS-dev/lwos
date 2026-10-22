#!/usr/bin/env python3
"""C0 integration: TEXT flag, environment, framebuffer pixels and close/reap."""
import struct
from ghost_autoload_test import ROOT, check

if __name__ == '__main__':
    header = (ROOT/'bin/external/textdemo.exe').read_bytes()
    assert struct.unpack_from('<I', header, 8)[0] == 0x10
    for machine in ('pc', 'q35'):
        check(machine, 'text')
        check(machine, 'text-conflict')
