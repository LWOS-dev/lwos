#!/usr/bin/env python3
"""Boot real TUI, edit/save/reboot/reopen on disposable PC/Q35 disk copies."""
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYMS = {v[2]: int(v[0], 16) for line in subprocess.check_output(
    ['nm', '-n', str(ROOT/'bin/monitor.elf')], text=True).splitlines()
    if len(v := line.split()) == 3}


def check(machine):
    with tempfile.TemporaryDirectory(prefix='lwos-hex-') as directory:
        work = Path(directory)
        disk = work/'test.img'
        shutil.copyfile(ROOT/'lwcnc.img', disk)
        (work/'HEXTEST.BIN').write_bytes(b'ABC')
        subprocess.run(['mcopy', '-o', '-i', f'{disk}@@1048576',
                        str(work/'HEXTEST.BIN'), '::/HEXTEST.BIN'], check=True)
        sock = work/'qmp.sock'
        guest = subprocess.Popen([
            'qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-drive', f'file={disk},format=raw,if=none,id=disk',
            *(['-device', 'piix3-ide,id=compat'] if machine == 'q35' else []),
            '-device', 'ide-hd,drive=disk,bus=' + ('compat.0' if machine == 'q35' else 'ide.0'),
            '-boot', 'c',
            '-qmp', f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists(): break
                if guest.poll() is not None: raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5)
                connection.connect(str(sock))
                stream = connection.makefile('rwb')
                stream.readline()

                def qmp(command, args=None):
                    stream.write((json.dumps({'execute': command, 'arguments': args or {}})+'\n').encode())
                    stream.flush()
                    while True:
                        reply = json.loads(stream.readline())
                        if 'return' in reply: return reply['return']
                        if 'error' in reply: raise RuntimeError(reply)

                def memory(address, size):
                    qmp('pmemsave', {'val': address, 'size': size, 'filename': str(work/'ram')})
                    return (work/'ram').read_bytes()

                def screen():
                    return memory(0xb8000, 4000)[::2].decode('ascii', 'replace')

                def key(name):
                    qmp('send-key', {'keys': [{'type': 'qcode', 'data': name}], 'hold-time': 40})
                    time.sleep(.12)

                def ready():
                    for _ in range(100):
                        if 'FILE EXPLORER' in screen(): return
                        if guest.poll() is not None: raise RuntimeError('guest exited')
                        time.sleep(.1)
                    raise AssertionError(screen())

                def open_test():
                    for _ in range(3): key('tab')
                    count = int.from_bytes(memory(SYMS['filelist_length'], 4), 'little')
                    entries = memory(SYMS['filelist_ptr'], count*24)
                    index = next(i for i in range(count) if entries[i*24:i*24+11] == b'HEXTEST BIN')
                    for _ in range(index): key('down')
                    key('f6')
                    assert 'LWOS HEX EDITOR' in screen(), screen()

                qmp('qmp_capabilities')
                ready(); open_test()
                assert memory(SYMS['hex_buffer'], 3) == b'ABC'
                key('4'); key('6'); key('ret')
                assert 'MODIFIED' in screen()
                key('f2'); assert 'SAVED' in screen(), screen()
                key('esc'); assert 'FILE EXPLORER' in screen(), screen()
                # Exit QEMU first so its disk backend is closed before rebooting.
                qmp('quit'); guest.wait(timeout=5)
                stream.close()
            subprocess.run(['mcopy', '-i', f'{disk}@@1048576', '::/HEXTEST.BIN',
                            str(work/'saved.bin')], check=True)
            assert (work/'saved.bin').read_bytes() == b'FBC'
            # A fresh process boots the persisted image, without a snapshot overlay.
            args = guest.args
            guest = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            time.sleep(.3)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5); connection.connect(str(sock))
                stream = connection.makefile('rwb'); stream.readline()
                qmp('qmp_capabilities'); ready(); open_test()
                assert memory(SYMS['hex_buffer'], 3) == b'FBC'
                key('1'); key('2'); key('ret'); key('esc')
                assert 'Discard edits?' in screen()
                key('n'); assert 'LWOS HEX EDITOR' in screen()
                key('esc'); key('y'); assert 'FILE EXPLORER' in screen()
                qmp('quit'); guest.wait(timeout=5); stream.close()
            print(f'PASS: {machine} TUI open/edit/save/exit, persistent reboot/reopen, discard confirmation')
        finally:
            if guest.poll() is None:
                guest.terminate(); guest.wait(timeout=5)


if __name__ == '__main__':
    for machine in ('pc', 'q35'):
        check(machine)
