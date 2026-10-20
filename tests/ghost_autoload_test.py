#!/usr/bin/env python3
"""QEMU boot: GUI input handoff, idle, exit, and autoload failure fallback."""
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from resman_isr_test import ROOT, symbols

RM = symbols('resman')
GHOST = symbols('external/ghost')


def check(machine, scenario):
    with tempfile.TemporaryDirectory(prefix='lwcnc-autoload-') as directory:
        work = Path(directory)
        disk = work / 'disk.img'
        shutil.copyfile(ROOT / 'lwcnc.img', disk)
        partition = f'{disk}@@{2048 * 512}'
        if scenario == 'missing':
            subprocess.run(['mdel', '-i', partition, '::/GHOST.EXE'], check=True)
        elif scenario in ('invalid', 'return'):
            source = ROOT / 'fsroot/TEST.EXE'
            if scenario == 'invalid':
                source = work / 'invalid.exe'
                source.write_bytes(bytes(128))
            subprocess.run(['mcopy', '-o', '-i', partition, str(source),
                            '::/GHOST.EXE'], check=True)

        sock = work / 'qmp.sock'
        guest = subprocess.Popen([
            'qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-drive', f'file={disk},format=raw,if=none,id=disk',
            *(['-device', 'piix3-ide,id=compat'] if machine == 'q35' else []),
            '-device', 'ide-hd,drive=disk,bus=' +
            ('compat.0' if machine == 'q35' else 'ide.0'),
            '-boot', 'c', '-qmp', f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists():
                    break
                if guest.poll() is not None:
                    raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5)
                connection.connect(str(sock))
                stream = connection.makefile('rwb')
                stream.readline()

                def qmp(command, arguments=None):
                    stream.write((json.dumps({'execute': command,
                                              'arguments': arguments or {}}) + '\n').encode())
                    stream.flush()
                    while True:
                        reply = json.loads(stream.readline())
                        if 'return' in reply:
                            return reply['return']
                        if 'error' in reply:
                            raise RuntimeError(reply)

                def memory(address, size):
                    path = work / 'ram'
                    qmp('pmemsave', {'val': address, 'size': size, 'filename': str(path)})
                    return path.read_bytes()

                def word(address):
                    return int.from_bytes(memory(address, 4), 'little')

                def state(task_id):
                    return word(RM['task'] + task_id * 168 + 4)

                def screen():
                    return memory(0xb8000, 4000)[::2].decode('ascii', 'replace')

                def wait_for(predicate):
                    deadline = time.monotonic() + 12
                    while time.monotonic() < deadline:
                        if predicate():
                            return
                        if guest.poll() is not None:
                            raise RuntimeError(guest.stderr.read().decode())
                        time.sleep(.05)
                    raise AssertionError((scenario, screen()))

                def key(name):
                    qmp('send-key', {'keys': [{'type': 'qcode', 'data': name}],
                                     'hold-time': 40})
                    time.sleep(.15)

                qmp('qmp_capabilities')
                if scenario == 'normal':
                    wait_for(lambda: word(RM['keyboard_owner']) == 1 and state(0) == 4)
                    wait_for(lambda: state(1) in (2, 3))
                    assert memory(RM['task'] + 168 + 8, 10) == b'GHOST.EXE\0'
                    base = word(RM['task'] + 168 + 28)

                    def red_pixel():
                        fb = word(base + GHOST['fb'])
                        pitch = word(base + GHOST['pitch'])
                        return fb and pitch and word(fb + 123 * pitch + 123 * 4) == 0xff0000

                    wait_for(red_pixel)
                    key('a')
                    assert state(0) == 4, 'monitor consumed GUI input'
                    assert word(RM['keyboard_owner']) == 1
                    key('esc')
                    wait_for(lambda: state(1) == 5 and word(RM['keyboard_owner']) == 0)
                    assert word(RM['task'] + 168 + 52) == 0
                elif scenario == 'return':
                    wait_for(lambda: state(1) == 5)
                    assert word(RM['task'] + 168 + 52) == 42
                    assert word(RM['keyboard_owner']) == 0
                else:
                    wait_for(lambda: state(0) == 4 and state(63) == 3)
                    result = int.from_bytes(memory(RM['ghost_autoload_result'], 4),
                                            'little', signed=True)
                    # Existing image_load reports -2 for a bad LWP magic.
                    assert result == (-7 if scenario == 'missing' else -2), result
                    assert state(1) == 0, 'failed load published a task'
                    assert word(RM['keyboard_owner']) == 0

                # The existing monitor startup alarm waits for one key.
                wait_for(lambda: state(0) == 4)
                key('a')
                wait_for(lambda: 'FILE EXPLORER' in screen())
                # With monitor blocked and no GUI task left, idle must run.
                wait_for(lambda: state(0) == 4 and state(63) == 3)
                key('tab')
                assert 'FILE EXPLORER' in screen()
                print(f'PASS: {machine} {scenario}: autoload/input/monitor/idle', flush=True)
                qmp('quit')
                guest.wait(timeout=3)
        finally:
            if guest.poll() is None:
                guest.terminate()
                guest.wait(timeout=3)


if __name__ == '__main__':
    for machine in ('pc', 'q35'):
        for scenario in ('normal', 'return', 'missing', 'invalid'):
            check(machine, scenario)
