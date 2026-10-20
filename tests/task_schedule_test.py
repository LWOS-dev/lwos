#!/usr/bin/env python3
"""Start two non-yielding tasks through F7; verify TSS preemption and UI."""
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


RM = {v[2]: int(v[0], 16) for line in subprocess.check_output(
    ['nm', '-n', str(ROOT/'bin/resman.elf')], text=True).splitlines()
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
                selected=0
                for filename in (b'A       EXE', b'B       EXE'):
                    index=next(i for i in range(count) if entries[i*24:i*24+11]==filename)
                    for _ in range(abs(index-selected)):key('down' if index>selected else 'up')
                    selected=index
                    key('f7')
                # TASK has a 104-byte TSS at +60 and a work counter at +164.
                def task(n):
                    raw=memory(RM['task']+n*168,168)
                    return (raw[8:24].split(b'\0')[0],
                            int.from_bytes(raw[48:52],'little'),
                            int.from_bytes(raw[164:168],'little'))
                first=[task(n) for n in range(3)]
                time.sleep(.5)
                second=[task(n) for n in range(3)]
                assert [t[0] for t in second]==[b'MONITOR',b'A.EXE',b'B.EXE'],second
                for old,new in zip(first,second):assert new[1]>old[1], (first,second)
                for old,new in zip(first[1:],second[1:]):assert new[2]!=old[2], (first,second)
                # A returning task becomes DEAD without stalling the other tasks.
                index=next(i for i in range(count) if entries[i*24:i*24+11]==b'TEST    EXE')
                for _ in range(abs(index-selected)):key('down' if index>selected else 'up')
                key('f7')
                time.sleep(.2)
                dead=memory(RM['task']+3*168,168)
                assert int.from_bytes(dead[4:8],'little')==5,dead.hex()
                assert int.from_bytes(dead[52:56],'little')==42,dead.hex()
                # Keyboard still works while both CPU-bound loops execute.
                key('tab')
                for _ in range(4):key('down')
                key('ret')
                text=screen()
                assert 'A.EXE' in text and 'B.EXE' in text and 'WORK' in text,text
                print(f'PASS: {machine} A/B counters and all three tick counts advance; TASKS UI responsive',flush=True)
                qmp('quit'); guest.wait(timeout=3)
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
