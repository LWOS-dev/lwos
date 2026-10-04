#!/usr/bin/env python3
"""Smoke-test BIOS bootstrap on IDE/AHCI and the existing video trampoline.
Uses QEMU TCG, a snapshot and a temporary local QMP socket; no disk writes.
Run after make: python3 tests/boot_test.py
"""
import json, socket, subprocess, tempfile, time
from pathlib import Path
root=Path(__file__).resolve().parent.parent
for machine in ['pc','q35']:
    with tempfile.TemporaryDirectory(prefix='lwcnc-boot-') as work:
        work=Path(work); sock=work/'qmp.sock'
        args=['qemu-system-i386','-machine',machine,'-accel','tcg','-m','32',
              '-display','none','-serial','none','-monitor','none','-no-reboot',
              '-snapshot','-drive',f'file={root}/lwcnc.img,format=raw,if=none,id=bootdisk',
              '-device','ide-hd,drive=bootdisk,bus=ide.0','-boot','c',
              '-qmp',f'unix:{sock},server=on,wait=off']
        p=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for i in range(100):
                if sock.exists(): break
                if p.poll() is not None: raise RuntimeError(p.stderr.read().decode())
                time.sleep(.05)
            s=socket.socket(socket.AF_UNIX); s.connect(str(sock)); f=s.makefile('rwb')
            f.readline()
            def qmp(cmd,arguments=None):
                f.write((json.dumps({'execute':cmd,'arguments':arguments or {}})+'\n').encode());f.flush()
                while True:
                    r=json.loads(f.readline())
                    if 'return' in r: return r['return']
                    if 'error' in r: raise RuntimeError(r)
            qmp('qmp_capabilities')
            screen=''
            for i in range(60):
                if p.poll() is not None: raise RuntimeError('QEMU exited (possible triple fault)')
                time.sleep(.2)
                qmp('pmemsave',{'val':0xb8000,'size':4000,'filename':str(work/'vga.bin')})
                raw=(work/'vga.bin').read_bytes()
                screen='\n'.join(bytes(raw[row*160:row*160+160:2]).decode('ascii','replace') for row in range(25))
                if 'LWOS' in screen and '>' in screen: break
            if not ('LWOS' in screen and '>' in screen): raise RuntimeError(machine+' did not reach monitor')
            if machine == 'q35':
                tree = qmp('human-monitor-command', {'command-line': 'info qtree'})
                if 'ich9-ahci' not in tree:
                    raise RuntimeError('Q35 AHCI controller missing')
            # The legacy video trampoline shares the new mode-switching body.
            symbol = next(line.split()[0] for line in subprocess.check_output(
                ['nm', '-n', str(root/'bin/abi.elf')], text=True).splitlines()
                if line.endswith(' gfx_is_live'))
            def graphics_state():
                qmp('pmemsave', {'val': int(symbol,16), 'size':4,
                    'filename':str(work/'graphics.bin')})
                return int.from_bytes((work/'graphics.bin').read_bytes(), 'little')
            def command(keys):
                for key in keys:
                    qmp('send-key', {'keys':[{'type':'qcode','data':key}], 'hold-time':50})
                    time.sleep(.15)
                time.sleep(.2)
            command(['v','e','ret'])
            if graphics_state() != 1:
                raise RuntimeError(machine+' video trampoline enter failed')
            command(['v','x','ret'])
            if graphics_state() != 0:
                raise RuntimeError(machine+' video trampoline exit failed')
            print(f'PASS: {machine} boot reaches monitor; video enter/exit works', flush=True)
            qmp('quit'); p.wait(timeout=3)
        finally:
            if p.poll() is None: p.terminate();p.wait(timeout=3)
