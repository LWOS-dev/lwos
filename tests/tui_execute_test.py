#!/usr/bin/env python3
"""Execute TEST.EXE through F4 and check return, UI and allocations on PC/Q35."""
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
    with tempfile.TemporaryDirectory(prefix='lwos-execute-') as directory:
        work = Path(directory)
        disk = work/'test.img'
        shutil.copyfile(ROOT/'lwcnc.img', disk)
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

                qmp('qmp_capabilities')
                time.sleep(.5)
                key('a')  # RESMAN startup wait
                key('a')  # Monitor's startup alarm wait
                for _ in range(100):
                    if 'FILE EXPLORER' in screen():break
                    time.sleep(.1)
                else:raise AssertionError(screen())
                for _ in range(3):key('tab')
                count=int.from_bytes(memory(SYMS['filelist_length'],4),'little')
                pointer=SYMS['filelist_ptr']
                entries=memory(pointer,count*24)
                index=next(i for i in range(count) if entries[i*24:i*24+11]==b'TEST    EXE')
                for _ in range(index):key('down')
                before=memory(0x300000,0x20000)
                for _ in range(3):
                    key('f4')
                    for attempt in range(50):
                        if 'EXIT WITH 0000002A' in screen():break
                        time.sleep(.1)
                    else:raise AssertionError(screen())
                    assert memory(0x300000,0x20000)==before, 'allocation leak'
                    key('ret')
                    assert 'FILE EXPLORER' in screen()
                print(f'PASS: {machine} F4 TEST.EXE returns 42; repeat runs; bitmap unchanged; UI restored',flush=True)
                qmp('quit'); guest.wait(timeout=3)
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
