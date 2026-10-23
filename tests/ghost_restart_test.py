#!/usr/bin/env python3
"""Real Esc -> Monitor -> F7 GHOST lifecycle, including keyboard/mouse/PIT."""
import json
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def symbols(path):
    return {p[2]: int(p[0], 16) for line in subprocess.check_output(
        ['nm', '-n', str(ROOT / 'bin' / path)], text=True).splitlines()
        if len(p := line.split()) == 3}


def check(machine):
    rm, monitor = symbols('resman.elf'), symbols('monitor.elf')
    with tempfile.TemporaryDirectory(prefix='lwcnc-ghost-restart-') as directory:
        work = Path(directory)
        sock = work / 'qmp.sock'
        guest = subprocess.Popen([
            'qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-snapshot', '-display', 'none', '-serial', 'none', '-monitor', 'none',
            '-no-reboot', '-drive', f'file={ROOT / "lwcnc.img"},format=raw,if=none,id=disk',
            *(['-device', 'piix3-ide,id=compat'] if machine == 'q35' else []),
            '-device', 'ide-hd,drive=disk,bus=' + ('compat.0' if machine == 'q35' else 'ide.0'),
            '-boot', 'c', '-qmp', f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while not sock.exists():
                if guest.poll() is not None:
                    raise RuntimeError(guest.stderr.read().decode())
                assert time.monotonic() < deadline
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5)
                connection.connect(str(sock))
                stream = connection.makefile('rwb')
                stream.readline()

                def qmp(command, arguments=None):
                    stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
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
                    return word(rm['task'] + task_id * 168 + 4)

                def screen():
                    return memory(0xb8000, 4000)[::2].decode('ascii', 'replace')

                def wait_for(predicate):
                    deadline = time.monotonic() + 12
                    while time.monotonic() < deadline:
                        if predicate():
                            return
                        if guest.poll() is not None:
                            raise RuntimeError(guest.stderr.read().decode())
                        time.sleep(.03)
                    raise AssertionError({'machine': machine, 'owner': word(rm['keyboard_owner']),
                                          'input': word(rm['input_enabled']), 'screen': screen()})

                def key(name):
                    qmp('send-key', {'keys': [{'type': 'qcode', 'data': name}], 'hold-time': 40})
                    time.sleep(.15)

                qmp('qmp_capabilities')
                wait_for(lambda: word(rm['keyboard_owner']) == 1 and word(rm['input_enabled']) == 1)
                owner = 1
                for cycle in range(3):
                    # Desktop has no focused window, so Esc exits GHOST itself.
                    key('esc')
                    wait_for(lambda: state(owner) == 5 and word(rm['keyboard_owner']) == 0)
                    wait_for(lambda: word(rm['input_enabled']) == 0 and state(0) == 4)
                    if cycle == 0:
                        key('a')  # Monitor's boot alarm.
                        wait_for(lambda: 'FILE EXPLORER' in screen())
                        for _ in range(3):
                            key('tab')
                        count = word(monitor['filelist_length'])
                        entries = memory(monitor['filelist_ptr'], count * 24)
                        index = next(i for i in range(count) if entries[i*24:i*24+11] == b'GHOST   EXE')
                        for _ in range(index):
                            key('down')
                    key('f7')
                    wait_for(lambda: word(rm['keyboard_owner']) != 0 and word(rm['input_enabled']) == 1)
                    owner = word(rm['keyboard_owner'])
                    assert state(owner) in (2, 3, 4)
                    before = word(rm['timer_ticks'])
                    x, y = word(rm['position']), word(rm['position'] + 4)
                    qmp('input-send-event', {'events': [
                        {'type': 'rel', 'data': {'axis': 'x', 'value': 20}},
                        {'type': 'rel', 'data': {'axis': 'y', 'value': 10}}]})
                    wait_for(lambda: word(rm['position']) == x+20 and word(rm['position']+4) == y+10)
                    wait_for(lambda: word(rm['timer_ticks']) != before)
                    # Monitor must yield its input to the restarted host.
                    wait_for(lambda: state(0) == 4)
                key('esc')
                wait_for(lambda: state(owner) == 5 and word(rm['keyboard_owner']) == 0)
                key('tab')
                wait_for(lambda: 'FILE EXPLORER' in screen())
                print(f'PASS: {machine}: 3 F7 GHOST restarts; keyboard/mouse/PIT and monitor return', flush=True)
                qmp('quit')
                guest.wait(timeout=3)
        finally:
            if guest.poll() is None:
                guest.terminate()
                guest.wait(timeout=3)


if __name__ == '__main__':
    for machine in ('pc', 'q35'):
        check(machine)
