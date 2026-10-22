#!/usr/bin/env python3
"""Real i386 CRT/printf and concurrent per-window stdout; temporary disks only."""
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from sleep_test import ROOT, symbols


def check(machine):
    rm=symbols(ROOT/'bin/resman.elf')
    with tempfile.TemporaryDirectory(prefix='lwos-stdio-') as directory:
        work=Path(directory)
        obj,elf,exe=[work/('host'+ext) for ext in ('.o','.elf','.exe')]
        subprocess.run(['gcc','-m32','-ffreestanding','-fno-pie','-fno-pic','-fno-stack-protector',
            '-fno-builtin','-nostdlib','-Os','-mno-sse','-mno-sse2','-mno-80387',
            '-fno-asynchronous-unwind-tables','-I'+str(ROOT/'lib/include'),
            '-c',str(ROOT/'tests/stdio_host.c'),'-o',str(obj)],check=True)
        objects=[str(ROOT/'build/lib/app'/name) for name in ('app.o','fs.o','mem.o','string.o','bitmap.o','convert.o')]
        libgcc=subprocess.check_output(['gcc','-m32','-print-libgcc-file-name'],text=True).strip()
        subprocess.run(['ld','-m','elf_i386','--no-warn-rwx-segments','--emit-relocs',
            '-T',str(ROOT/'tools/mkexe/lwp.ld'),str(obj),*objects,libgcc,'-o',str(elf)],check=True)
        subprocess.run([str(ROOT/'build/tools/mkexe'),str(elf),str(exe)],check=True,stdout=subprocess.DEVNULL)
        probe=symbols(elf)
        disk=work/'disk.img';shutil.copyfile(ROOT/'lwcnc.img',disk)
        subprocess.run(['mcopy','-o','-i',f'{disk}@@{2048*512}',str(exe),'::/GHOST.EXE'],check=True)
        sock=work/'qmp.sock'
        guest=subprocess.Popen(['qemu-system-i386','-machine',machine,'-accel','tcg','-m','32',
            '-display','none','-serial','none','-monitor','none','-no-reboot',
            '-drive',f'file={disk},format=raw,if=none,id=disk',
            *(['-device','piix3-ide,id=compat'] if machine=='q35' else []),
            '-device','ide-hd,drive=disk,bus='+('compat.0' if machine=='q35' else 'ide.0'),
            '-boot','c','-qmp',f'unix:{sock},server=on,wait=off'],
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if sock.exists():break
                if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5);connection.connect(str(sock))
                stream=connection.makefile('rwb');stream.readline()
                def qmp(command,arguments=None):
                    stream.write((json.dumps({'execute':command,'arguments':arguments or {}})+'\n').encode());stream.flush()
                    while True:
                        reply=json.loads(stream.readline())
                        if 'return' in reply:return reply['return']
                        if 'error' in reply:raise RuntimeError(reply)
                def word(address):
                    file=work/'memory.bin'
                    qmp('pmemsave',{'val':address,'size':4,'filename':str(file)})
                    return int.from_bytes(file.read_bytes(),'little')
                qmp('qmp_capabilities')
                task=rm['task']+168
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    if word(task+4)==5:break
                    if guest.poll() is not None:raise RuntimeError(guest.stderr.read().decode())
                    time.sleep(.02)
                else:raise AssertionError('stdio host timeout')
                base=word(task+28)
                assert word(base+probe['error'])==0, ('probe line',word(base+probe['error']))
                assert word(base+probe['stage'])==4 and word(task+52)==42
                for child in (2,3,4):assert word(rm['task']+child*168+4)==0, 'unreaped child'
                print(f'PASS: {machine}: two private stdout/cursors; 32-bit printf; CRT main; exit and reap',flush=True)
                qmp('quit');guest.wait(timeout=3)
        finally:
            if guest.poll() is None:guest.terminate();guest.wait(timeout=3)

if __name__=='__main__':
    for machine in ('pc','q35'):check(machine)
