#!/usr/bin/env python3
"""Actual GHOST/PS2 smoke test after 75 seconds without input. Build first."""
import json
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def symbols(path):
    return {parts[2]: int(parts[0], 16)
            for line in subprocess.check_output(['nm', '-n', str(path)], text=True).splitlines()
            if len(parts := line.split()) == 3}


def check(machine):
    rm = symbols(ROOT/'bin/resman.elf')
    with tempfile.TemporaryDirectory(prefix='lwos-ghost-idle-') as directory:
        work = Path(directory)
        disk = ROOT/'lwcnc.img'
        sock = work/'qmp.sock'
        guest = subprocess.Popen(['qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-snapshot', '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-drive', f'file={disk},format=raw,if=none,id=disk',
            *(['-device', 'piix3-ide,id=compat'] if machine == 'q35' else []),
            '-device', 'ide-hd,drive=disk,bus='+('compat.0' if machine == 'q35' else 'ide.0'),
            '-boot', 'c', '-qmp', f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            connection = socket.socket(socket.AF_UNIX)
            connection.settimeout(5)
            connection.connect(str(sock))
            stream = connection.makefile('rwb'); stream.readline()
            def qmp(command, arguments=None):
                stream.write((json.dumps({'execute':command, 'arguments':arguments or {}})+'\n').encode())
                stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'return' in reply:return reply['return']
                    if 'error' in reply:raise RuntimeError(reply)
            def memory(address, size):
                file=work/'memory.bin'
                qmp('pmemsave', {'val':address,'size':size,'filename':str(file)})
                return file.read_bytes()
            def word(address):return int.from_bytes(memory(address,4),'little')
            def wait_for(predicate):
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    if predicate():return
                    if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                    time.sleep(.01)
                raise AssertionError('probe timeout')
            qmp('qmp_capabilities')
            wait_for(lambda: word(rm['keyboard_owner'])==1 and word(rm['input_enabled'])==1)
            time.sleep(1)
            before=word(rm['timer_ticks'])
            print(f'{machine}: GHOST ready; waiting 75 seconds without input',flush=True)
            for _ in range(75):
                time.sleep(1)
                assert guest.poll() is None, 'guest stopped during idle'
            assert (word(rm['timer_ticks'])-before)&0xffffffff > 60000, 'PIT stopped'
            x,y=word(rm['position']),word(rm['position']+4)
            qmp('input-send-event', {'events':[
                {'type':'rel','data':{'axis':'x','value':20}},
                {'type':'rel','data':{'axis':'y','value':10}}]})
            wait_for(lambda: word(rm['position'])==x+20 and word(rm['position']+4)==y+10)
            # No click: initial desktop focus routes Escape to GHOST.
            qmp('send-key', {'keys':[{'type':'qcode','data':'esc'}], 'hold-time':40})
            wait_for(lambda: word(rm['keyboard_owner'])==0)
            print(f'PASS: {machine}: 75-second GHOST idle; PIT, PS/2 mouse and Escape/monitor handoff',flush=True)
            qmp('quit'); guest.wait(timeout=3)
            stream.close(); connection.close()
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(check, ('pc','q35')))
