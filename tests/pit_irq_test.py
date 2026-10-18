#!/usr/bin/env python3
"""Check PIT initialization, repeated IRQ0/EOI and concurrent keyboard IRQ1."""
import json
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path
from resman_isr_test import ROOT, symbols

RM = symbols('resman')


def check(machine):
    with tempfile.TemporaryDirectory(prefix='lwcnc-pit-') as temporary:
        work=Path(temporary)
        qmp_path, gdb_path=work/'qmp.sock', work/'gdb.sock'
        guest=subprocess.Popen([
            'qemu-system-i386', '-machine', machine, '-accel', 'tcg', '-m', '32',
            '-display', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot',
            '-snapshot', '-drive', f'file={ROOT}/lwcnc.img,format=raw,if=none,id=disk',
            '-device', 'ide-hd,drive=disk,bus=ide.0', '-boot', 'c',
            '-qmp', f'unix:{qmp_path},server=on,wait=off',
            '-gdb', f'unix:{gdb_path},server=on,wait=off'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if qmp_path.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            connection=socket.socket(socket.AF_UNIX)
            connection.settimeout(5)
            connection.connect(str(qmp_path))
            stream=connection.makefile('rwb'); stream.readline()

            def qmp(command, arguments=None):
                stream.write((json.dumps({'execute':command, 'arguments':arguments or {}})+'\n').encode())
                stream.flush()
                while True:
                    reply=json.loads(stream.readline())
                    if 'return' in reply:return reply['return']
                    if 'error' in reply:raise RuntimeError(reply)

            def memory(address, size):
                path=work/'memory.bin'
                qmp('pmemsave', {'val':address,'size':size,'filename':str(path)})
                return path.read_bytes()

            qmp('qmp_capabilities')
            for _ in range(100):
                handler=struct.unpack('<I',memory(RM['handlers']+0x20*4,4))[0]
                if handler==RM['timer_irq']:break
                time.sleep(.1)
            else:raise AssertionError('IRQ0 handler not installed')
            time.sleep(.2)
            first=struct.unpack('<I',memory(RM['ticks'],4))[0]
            time.sleep(.2)
            second=struct.unpack('<I',memory(RM['ticks'],4))[0]
            assert first!=second, (first,second)
            tone=struct.unpack('<I',memory(RM['cnt'],4))[0]
            for _ in range(30):
                time.sleep(.05)
                if struct.unpack('<I',memory(RM['cnt'],4))[0]!=tone:break
            else:raise AssertionError('one-second tone divider stalled')
            before=struct.unpack('<I',memory(RM['kb_irq_cnt'],4))[0]
            qmp('send-key', {'keys':[{'type':'qcode','data':'a'}], 'hold-time':40})
            time.sleep(.2)
            after=struct.unpack('<I',memory(RM['kb_irq_cnt'],4))[0]
            assert after>before, (before,after)
            screen=memory(0xb8000,160)[::2]
            assert not screen.startswith(b'RESMAN UNHANDLED INTERRUPT'), screen
            print(f'PASS: {machine} IRQ0 ticks advance; one-second tone divider; IRQ1 remains responsive',flush=True)
            qmp('quit');guest.wait(timeout=3)
            stream.close();connection.close()
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
