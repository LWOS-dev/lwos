#!/usr/bin/env python3
"""Actual IRQ1 -> scan queue -> existing ABI calls, on PC and Q35 snapshots."""
import json
import re
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path
from resman_isr_test import ROOT, Remote, symbols

RM, ABI = symbols('resman'), symbols('abi')


def check(machine):
    with tempfile.TemporaryDirectory(prefix='lwcnc-keyboard-') as temporary:
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

            def key(*keys):
                qmp('send-key', {'keys':[{'type':'qcode','data':k} for k in keys], 'hold-time':40})
                time.sleep(.1)

            def expect_key(value):
                for _ in range(30):
                    if memory(0xb8000+160+8,16)[::2].decode()==f'{value:08X}':return
                    time.sleep(.05)
                raise AssertionError((value,memory(0xb8000+160,160)[::2]))

            def stage(value):
                for _ in range(60):
                    if struct.unpack('<I',memory(0x380100,4))[0]==value:return
                    time.sleep(.05)
                raise AssertionError(f'ABI probe did not reach stage {value}')

            qmp('qmp_capabilities')
            for _ in range(100):
                if memory(0xb8000,80)[::2].startswith(b'IRQ KEYBOARD READY'):break
                time.sleep(.1)
            else:raise AssertionError('keyboard initialization failed')
            key('a'); expect_key(ord('a'))
            key('b'); expect_key(ord('b')) # A second IRQ verifies EOI.
            key('f7'); expect_key(0x110)
            key('up'); expect_key(0x100)
            key('shift','a'); expect_key(ord('A'))
            key('ctrl','c'); expect_key(3)
            key('caps_lock'); expect_key(0x11b)
            key('a'); expect_key(ord('A'))
            key('caps_lock'); expect_key(0x11b)
            key('a'); expect_key(ord('a'))
            for _ in range(70):key('b') # Wrap the raw queue multiple times.
            expect_key(ord('b'))
            assert struct.unpack('<I',memory(RM['kb_dropped'],4))[0]==0
            registers=qmp('human-monitor-command', {'command-line':'info registers'})
            assert 'HLT=1' in registers, registers

            # Guest RAM probe uses the original ABI table slots, not private functions.
            # First drain the wakeup key, then re-attach ABI and exercise getp/getk/getc.
            source=work/'probe.s'
            source.write_text('''bits 32
org 0x380000
.drain:
call dword [0x100074]
cmp eax, -1
jne .drain
call dword [0x100004]
mov dword [0x380100], 1
call dword [0x10006c]
mov [0x380104], eax
mov dword [0x380100], 2
call dword [0x100070]
mov [0x380108], al
call dword [0x100070]
mov [0x380109], al
call dword [0x100070]
mov [0x38010a], al
mov dword [0x380100], 3
.wait_release:
call dword [0x100074]
mov [0x38010c], eax
jmp .wait_release
''')
            subprocess.run(['nasm','-f','bin',str(source),'-o',str(work/'probe.bin')],check=True)
            qmp('stop')
            remote=Remote(gdb_path)
            remote.write(0x380000,(work/'probe.bin').read_bytes())
            remote.write(0x380100,bytes(16))
            remote.start(0x380000)
            qmp('cont')
            key('q') # Wake the halted CPU; the probe drains this key.
            stage(1)
            key('b'); stage(2)
            assert struct.unpack('<I',memory(0x380104,4))[0]==ord('b')
            key('up'); stage(3)
            assert memory(0x380108,3)==b'\x1b[A'
            assert struct.unpack('<I',memory(0x38010c,4))[0]==0xffffffff
            # Re-attachment must preserve the bound queue source.
            assert struct.unpack('<I',memory(ABI['input_source'],4))[0]!=0
            print(f'PASS: {machine} IRQ1/EOI; F7/arrows/modifiers/Caps LEDs; queue wrap; idle HLT; ABI re-attach/getk/getc/getp',flush=True)
            qmp('quit');guest.wait(timeout=3)
            stream.close();connection.close()
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)


if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
